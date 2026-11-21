#!/usr/bin/env python3
"""End-to-end WebSocket wss:// TLS, Origin, mixed-content, API, and teardown check.

Starts real local HTTP/HTTPS and ws:// / wss:// RFC 6455 peers with an
ephemeral OpenSSL CA and leaf certificates (valid DNS+IP SAN, untrusted,
wrong-host SAN, expired, and plaintext-on-wss) and drives the Litebird CLI.
Verifies:
  1. Valid wss:// to IPv4 literal (IP SAN check) and localhost (DNS SAN + SNI),
     random 16-byte Sec-WebSocket-Key, subprotocol negotiation, masked client
     frames, unmasked server echo, and clean close handshake (wasClean=true).
  2. Fail-closed TLS rejection for untrusted CA, wrong SAN, expired cert, and
     plaintext TCP peer on wss:// (onerror + abnormal 1006 close, 0 HTTP bytes).
  3. Fetch Origin & mixed-content enforcement across https://, http://,
     file:// (Origin: null), and <iframe sandbox="allow-scripts"> (Origin: null),
     including synchronous SecurityError for non-loopback ws:// and ws://[::2]/,
     loopback ws:// exemption, and relative WebSocket('/...') URL resolution.
  4. WHATWG constructor & method validation (TypeError without new, SyntaxError
     on bad scheme/fragment/duplicate/invalid subprotocol/oversized close
     reason, InvalidAccessError on invalid close code, InvalidStateError on
     send() while CONNECTING, readonly attributes, binaryType validation).
  5. Web Worker wss:// connection with inherited https:// Origin.
  6. Concurrent wss:// startup + immediate exit stability (--stress-runs).
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import os
import pathlib
import re
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def generate_test_pki(cert_dir: pathlib.Path) -> dict[str, pathlib.Path]:
    ca_key = cert_dir / "ca.key"
    ca_pem = cert_dir / "ca.pem"
    subprocess.run(
        [
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", str(ca_key), "-out", str(ca_pem),
            "-days", "30", "-subj", "/CN=Litebird-WebSocket-Test-CA",
        ],
        check=True,
        capture_output=True,
    )

    (cert_dir / "index.txt").write_text("", encoding="utf-8")
    (cert_dir / "serial").write_text("01\n", encoding="utf-8")

    def make_signed_cert(
        stem: str,
        san: str,
        startdate: str | None = None,
        enddate: str | None = None,
    ) -> tuple[pathlib.Path, pathlib.Path]:
        key = cert_dir / f"{stem}.key"
        csr = cert_dir / f"{stem}.csr"
        pem = cert_dir / f"{stem}.pem"
        cnf = cert_dir / f"{stem}.cnf"
        subprocess.run(
            [
                "openssl", "req", "-new", "-newkey", "rsa:2048", "-nodes",
                "-keyout", str(key), "-out", str(csr),
                "-subj", f"/CN={stem}.litebird.test",
            ],
            check=True,
            capture_output=True,
        )
        cnf.write_text(
            f"[ca]\ndefault_ca = myca\n"
            f"[myca]\ndir = {cert_dir}\ndatabase = $dir/index.txt\n"
            f"serial = $dir/serial\nnew_certs_dir = $dir\n"
            f"certificate = $dir/ca.pem\nprivate_key = $dir/ca.key\n"
            f"default_md = sha256\npolicy = pol\ncopy_extensions = copy\n"
            f"[pol]\ncommonName = optional\n"
            f"[ext]\nsubjectAltName = {san}\n"
            f"basicConstraints = CA:FALSE\n"
            f"keyUsage = digitalSignature, keyEncipherment\n"
            f"extendedKeyUsage = serverAuth\n",
            encoding="utf-8",
        )
        cmd = [
            "openssl", "ca", "-batch", "-config", str(cnf),
            "-extensions", "ext", "-in", str(csr), "-out", str(pem),
        ]
        if startdate and enddate:
            cmd.extend(["-startdate", startdate, "-enddate", enddate])
        else:
            cmd.extend(["-days", "30"])
        subprocess.run(cmd, check=True, capture_output=True)
        return pem, key

    valid_pem, valid_key = make_signed_cert("valid", "DNS:localhost,IP:127.0.0.1")
    wrong_pem, wrong_key = make_signed_cert("wrong", "DNS:wrong.example,IP:127.0.0.2")
    expired_pem, expired_key = make_signed_cert(
        "expired",
        "DNS:localhost,IP:127.0.0.1",
        startdate="20200101000000Z",
        enddate="20200102000000Z",
    )

    # Self-signed certificate NOT signed by ca.pem
    untrusted_key = cert_dir / "untrusted.key"
    untrusted_pem = cert_dir / "untrusted.pem"
    untrusted_ext = cert_dir / "untrusted.cnf"
    untrusted_ext.write_text(
        "[req]\ndistinguished_name = dn\nx509_extensions = v3\nprompt = no\n"
        "[dn]\nCN = localhost\n"
        "[v3]\nsubjectAltName = DNS:localhost,IP:127.0.0.1\n",
        encoding="utf-8",
    )
    subprocess.run(
        [
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", str(untrusted_key), "-out", str(untrusted_pem),
            "-days", "30", "-config", str(untrusted_ext),
        ],
        check=True,
        capture_output=True,
    )

    return {
        "ca": ca_pem,
        "valid_pem": valid_pem,
        "valid_key": valid_key,
        "wrong_pem": wrong_pem,
        "wrong_key": wrong_key,
        "expired_pem": expired_pem,
        "expired_key": expired_key,
        "untrusted_pem": untrusted_pem,
        "untrusted_key": untrusted_key,
    }


def recv_exact(sock: socket.socket, n: int) -> bytes:
    out = bytearray()
    while len(out) < n:
        chunk = sock.recv(n - len(out))
        if not chunk:
            raise EOFError("unexpected EOF")
        out.extend(chunk)
    return bytes(out)


def read_ws_frame(sock: socket.socket) -> tuple[int, bytes, bool, bytes]:
    hdr = recv_exact(sock, 2)
    b0, b1 = hdr[0], hdr[1]
    opcode = b0 & 0x0F
    masked = bool(b1 & 0x80)
    length = b1 & 0x7F
    if length == 126:
        length = struct.unpack("!H", recv_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", recv_exact(sock, 8))[0]
    mask = recv_exact(sock, 4) if masked else b""
    payload = bytearray(recv_exact(sock, length))
    if masked:
        for i in range(len(payload)):
            payload[i] ^= mask[i % 4]
    return opcode, bytes(payload), masked, mask


def build_ws_frame(opcode: int, payload: bytes) -> bytes:
    header = bytearray([0x80 | (opcode & 0x0F)])
    n = len(payload)
    if n < 126:
        header.append(n)
    elif n < 65536:
        header.append(126)
        header.extend(struct.pack("!H", n))
    else:
        header.append(127)
        header.extend(struct.pack("!Q", n))
    return bytes(header) + payload


class DualProtocolServer:
    """Thread-per-connection HTTP/HTTPS + ws:// / wss:// RFC 6455 test peer."""

    def __init__(self, ssl_ctx: ssl.SSLContext | None = None, name: str = "plain"):
        self.name = name
        self.ssl_ctx = ssl_ctx
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(64)
        self.sock.settimeout(0.25)
        self.port = self.sock.getsockname()[1]
        self.stop_event = threading.Event()
        self.lock = threading.Lock()
        self.upgrades: list[dict] = []
        self.tls_failures = 0
        self.leaked_http_after_tls_fail = 0
        self.sni_names: list[str | None] = []
        self.routes: dict[str, tuple[str, bytes]] = {}
        self.thread = threading.Thread(target=self._accept_loop, daemon=True)
        if self.ssl_ctx is not None:
            def on_sni(_ssl_sock, server_name, _ctx):
                with self.lock:
                    self.sni_names.append(server_name)
                return None

            self.ssl_ctx.sni_callback = on_sni
        self.thread.start()

    def stop(self) -> None:
        self.stop_event.set()
        try:
            self.sock.close()
        except OSError:
            pass
        self.thread.join(timeout=2.0)

    def _accept_loop(self) -> None:
        while not self.stop_event.is_set():
            try:
                conn, _ = self.sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            t = threading.Thread(target=self._handle_conn, args=(conn,), daemon=True)
            t.start()

    def _handle_conn(self, raw_conn: socket.socket) -> None:
        conn: socket.socket = raw_conn
        conn.settimeout(3.0)
        try:
            if self.ssl_ctx is not None:
                try:
                    conn = self.ssl_ctx.wrap_socket(raw_conn, server_side=True)
                except (ssl.SSLError, OSError):
                    with self.lock:
                        self.tls_failures += 1
                    return

            raw_req = bytearray()
            while len(raw_req) < 65536 and b"\r\n\r\n" not in raw_req:
                try:
                    chunk = conn.recv(4096)
                except (ssl.SSLError, OSError):
                    if self.ssl_ctx is not None:
                        with self.lock:
                            self.tls_failures += 1
                    return
                if not chunk:
                    if self.ssl_ctx is not None and not raw_req:
                        with self.lock:
                            self.tls_failures += 1
                    return
                raw_req.extend(chunk)
                # Plaintext port receiving TLS ClientHello (0x16 0x03 ...): fail fast.
                if self.ssl_ctx is None and raw_req[:1] == b"\x16":
                    conn.sendall(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                    return

            header_end = raw_req.find(b"\r\n\r\n")
            if header_end < 0:
                return
            header_text = raw_req[:header_end].decode("latin-1")
            lines = header_text.split("\r\n")
            req_line = lines[0].split(" ")
            path = req_line[1] if len(req_line) >= 2 else "/"
            headers: dict[str, str] = {}
            for line in lines[1:]:
                if ":" in line:
                    k, v = line.split(":", 1)
                    headers[k.strip().lower()] = v.strip()

            if headers.get("upgrade", "").lower() == "websocket":
                self._handle_ws(conn, path, headers)
            else:
                self._handle_http(conn, path)
        except Exception:
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def _handle_http(self, conn: socket.socket, path: str) -> None:
        if path == "/yield":
            time.sleep(0.02)
            body = b"ok"
            ct = "text/plain"
            status = "200 OK"
        elif path in self.routes:
            ct, body = self.routes[path]
            status = "200 OK"
        else:
            ct = "text/plain"
            body = b"not found"
            status = "404 Not Found"
        resp = (
            f"HTTP/1.1 {status}\r\n"
            f"Content-Type: {ct}\r\n"
            f"Content-Length: {len(body)}\r\n"
            f"Access-Control-Allow-Origin: *\r\n"
            f"Connection: close\r\n\r\n"
        ).encode("ascii") + body
        conn.sendall(resp)

    def _handle_ws(self, conn: socket.socket, path: str, headers: dict[str, str]) -> None:
        sec_key = headers.get("sec-websocket-key", "")
        offered = [p.strip() for p in headers.get("sec-websocket-protocol", "").split(",") if p.strip()]
        selected_proto = ""
        if offered:
            selected_proto = "superchat" if "superchat" in offered else offered[0]
        accept = base64.b64encode(hashlib.sha1((sec_key + WS_GUID).encode("ascii")).digest()).decode("ascii")
        resp = (
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Accept: {accept}\r\n"
        )
        if selected_proto:
            resp += f"Sec-WebSocket-Protocol: {selected_proto}\r\n"
        # Cookies set on an upgrade response: an ordinary Set-Cookie header on
        # the one response the handshake ever gets.
        if path == "/ws-cookie":
            resp += "Set-Cookie: harness_after=1; Path=/\r\n"
        resp += "\r\n"
        conn.sendall(resp.encode("ascii"))

        rec = {
            "server": self.name,
            "path": path,
            "origin": headers.get("origin"),
            "host": headers.get("host"),
            "key": sec_key,
            "offered": offered,
            "selected": selected_proto,
            "cookie": headers.get("cookie"),
            "messages": [],
            "masks_ok": True,
            "close_code": None,
            "close_reason": None,
        }

        # Worker endpoints proactively push a greeting then close cleanly.
        if path.startswith("/worker"):
            conn.sendall(build_ws_frame(0x01, b"wss-worker-ok"))

        conn.settimeout(2.5)
        try:
            while True:
                opcode, payload, masked, mask = read_ws_frame(conn)
                if not masked or len(mask) != 4:
                    rec["masks_ok"] = False
                if opcode == 0x01:
                    text = payload.decode("utf-8", errors="replace")
                    rec["messages"].append(text)
                    conn.sendall(build_ws_frame(0x01, f"echo:{text}".encode("utf-8")))
                elif opcode == 0x02:
                    conn.sendall(build_ws_frame(0x02, payload))
                elif opcode == 0x09:
                    conn.sendall(build_ws_frame(0x0A, payload))
                elif opcode == 0x08:
                    if len(payload) >= 2:
                        rec["close_code"] = struct.unpack("!H", payload[:2])[0]
                        rec["close_reason"] = payload[2:].decode("utf-8", errors="replace")
                    conn.sendall(build_ws_frame(0x08, payload))
                    break
        except (EOFError, OSError, ssl.SSLError):
            pass
        with self.lock:
            self.upgrades.append(rec)


def make_server_ssl_ctx(cert: pathlib.Path, key: pathlib.Path) -> ssl.SSLContext:
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certfile=str(cert), keyfile=str(key))
    return ctx


def parse_checks(output: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in output.splitlines():
        if line.startswith("WSCHECK:"):
            k, _, v = line[len("WSCHECK:"):].partition("=")
            out[k] = v
    return out


def run_cli(
    binary: str,
    args: list[str],
    timeout: float,
    env_extra: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    if env_extra:
        env.update(env_extra)
    return subprocess.run(
        [binary, *args],
        capture_output=True,
        text=True,
        timeout=timeout,
        env=env,
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", default="build/litebird")
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--stress-runs", type=int, default=60)
    args = ap.parse_args()

    binary = pathlib.Path(args.binary)
    if not binary.is_file():
        print(f"binary not found: {binary}", file=sys.stderr)
        return 2

    checks = 0
    failures = 0

    def refused_before_close(record: str | None) -> bool:
        """An error event followed by an unclean 1006 close, in that order."""
        record = record or ""
        return record.startswith("err") and record.endswith(",close:1006:false")

    def tls_refusal_code(record: str | None) -> int | None:
        """The X509_V_ERR code a wss:// refusal named, from the page's own event.

        A refusal on a socket does not reach the engine's stderr: it surfaces as
        the error event's message, which the page logs. A first version of this
        check searched stderr and found nothing, so it was blind in exactly the
        way it was written to cure. Every refusal used to say only
        "certificate verification failed"; the code is what separates an
        expired certificate from an untrusted issuer from a wrong name.
        """
        match = re.search(r"certificate verification failed \(code (\d+)", record or "")
        return int(match.group(1)) if match else None

    def check(label: str, cond: bool, detail: str = "") -> None:
        nonlocal checks, failures
        checks += 1
        print(f"{'PASS' if cond else 'FAIL'} {label}" + (f" ({detail})" if detail and not cond else ""))
        if not cond:
            failures += 1

    with tempfile.TemporaryDirectory(prefix="litebird-ws-sec-") as tmp:
        root = pathlib.Path(tmp)
        pki = generate_test_pki(root / "pki" if False else root)

        https_srv = DualProtocolServer(make_server_ssl_ctx(pki["valid_pem"], pki["valid_key"]), name="https_valid")
        wss_valid = DualProtocolServer(make_server_ssl_ctx(pki["valid_pem"], pki["valid_key"]), name="wss_valid")
        wss_untrusted = DualProtocolServer(make_server_ssl_ctx(pki["untrusted_pem"], pki["untrusted_key"]), name="wss_untrusted")
        wss_wrong = DualProtocolServer(make_server_ssl_ctx(pki["wrong_pem"], pki["wrong_key"]), name="wss_wrong")
        wss_expired = DualProtocolServer(make_server_ssl_ctx(pki["expired_pem"], pki["expired_key"]), name="wss_expired")
        plain_srv = DualProtocolServer(None, name="plain")

        try:
            https_srv.routes["/worker-ws.js"] = (
                "application/javascript",
                (
                    f"var ws = new WebSocket('wss://127.0.0.1:{wss_valid.port}/worker-ok');\n"
                    f"ws.onmessage = function(e) {{ postMessage('W:' + e.data); ws.close(1000, 'wdone'); }};\n"
                    f"ws.onerror = function() {{ postMessage('W:ERR'); }};\n"
                ).encode("utf-8"),
            )

            env_ca = {"LITEBIRD_SSL_CA_FILE": str(pki["ca"])}

            # 1. HTTPS document verifying wss:// (IP SAN, DNS SAN + SNI, relative URL),
            #    loopback ws://, mixed-content blocking, TLS rejections, WebIDL API, and Worker wss://.
            https_html = f"""<!doctype html><meta charset="utf-8"><script>
var pending = 0;
function log(k, v) {{ console.log('WSCHECK:' + k + '=' + v); }}

// Synchronous mixed-content & validation checks
try {{ new WebSocket('ws://remote.example/chat'); log('mixed_host', 'NO_THROW'); }}
catch (e) {{ log('mixed_host', e.name); }}

try {{ new WebSocket('ws://[::2]/chat'); log('mixed_ipv6', 'NO_THROW'); }}
catch (e) {{ log('mixed_ipv6', e.name); }}

try {{ WebSocket('wss://127.0.0.1:{wss_valid.port}/x'); log('no_new', 'NO_THROW'); }}
catch (e) {{ log('no_new', e.name); }}

try {{ new WebSocket('http://127.0.0.1:{plain_srv.port}/x'); log('bad_scheme', 'NO_THROW'); }}
catch (e) {{ log('bad_scheme', e.name); }}

try {{ new WebSocket('wss://127.0.0.1:{wss_valid.port}/x#frag'); log('bad_frag', 'NO_THROW'); }}
catch (e) {{ log('bad_frag', e.name); }}

try {{ new WebSocket('wss://127.0.0.1:{wss_valid.port}/x', ['dup', 'dup']); log('dup_proto', 'NO_THROW'); }}
catch (e) {{ log('dup_proto', e.name); }}

try {{ new WebSocket('wss://127.0.0.1:{wss_valid.port}/x', ['bad proto']); log('bad_proto', 'NO_THROW'); }}
catch (e) {{ log('bad_proto', e.name); }}

// wss:// to 127.0.0.1 (IP SAN) + subprotocols + send/close + WebIDL readonly + send while CONNECTING
pending++;
var wsIp = new WebSocket('wss://127.0.0.1:{wss_valid.port}/ip?q=1', ['chat', 'superchat']);
try {{ wsIp.send('too-early'); log('send_early', 'NO_THROW'); }}
catch (e) {{ log('send_early', e.name); }}
try {{ wsIp.close(999); log('bad_code', 'NO_THROW'); }}
catch (e) {{ log('bad_code', e.name); }}
try {{ wsIp.close(3000, 'x'.repeat(124)); log('long_reason', 'NO_THROW'); }}
catch (e) {{ log('long_reason', e.name); }}
wsIp.url = 'https://hijack.example/';
wsIp.binaryType = 'arraybuffer';
wsIp.binaryType = 'bogus';
log('ip_ro', wsIp.url + '|' + wsIp.binaryType);
wsIp.onopen = function() {{
  log('ip_proto', wsIp.protocol);
  wsIp.send('hi-ip');
}};
wsIp.onmessage = function(e) {{
  log('ip_msg', e.data);
  wsIp.close(3000, 'clean-ip');
}};
wsIp.onclose = function(e) {{
  log('ip_close', e.code + ':' + e.reason + ':' + e.wasClean + ':' + wsIp.readyState);
  pending--;
}};

// A Secure cookie on an https page goes out on a wss:// upgrade: `wss` is
// matched as `https`, which is the only reason a `Secure` cookie can ever be
// sent on a WebSocket at all.
pending++;
document.cookie = 'wss_secure=1; Path=/; Secure';
var wsCookie = new WebSocket('wss://localhost:{wss_valid.port}/wss-cookie');
wsCookie.onopen = function() {{ log('wss_cookie_page', document.cookie); wsCookie.close(1000); }};
wsCookie.onclose = function(e) {{ log('wss_cookie_close', e.code + ':' + e.wasClean); pending--; }};

// wss:// to localhost (DNS SAN + SNI)
pending++;
var wsDns = new WebSocket('wss://localhost:{wss_valid.port}/dns');
wsDns.onopen = function() {{ wsDns.send('hi-dns'); }};
wsDns.onmessage = function(e) {{ log('dns_msg', e.data); wsDns.close(1000, 'ok'); }};
wsDns.onclose = function(e) {{ log('dns_close', e.code + ':' + e.wasClean); pending--; }};

// Relative URL '/wss-rel' from https://localhost:<https_port>/app/page.html -> wss://localhost:<https_port>/wss-rel
pending++;
var wsRel = new WebSocket('/wss-rel');
log('rel_url', wsRel.url);
wsRel.onopen = function() {{ wsRel.send('hi-rel'); }};
wsRel.onmessage = function(e) {{ log('rel_msg', e.data); wsRel.close(1000); }};
wsRel.onclose = function(e) {{ log('rel_close', e.code + ':' + e.wasClean); pending--; }};

// Loopback ws:// from https:// (potentially trustworthy loopback allowed)
pending++;
var wsLoop = new WebSocket('ws://127.0.0.1:{plain_srv.port}/loop');
wsLoop.onopen = function() {{ wsLoop.send('hi-loop'); }};
wsLoop.onmessage = function(e) {{ log('loop_msg', e.data); wsLoop.close(1000); }};
wsLoop.onclose = function(e) {{ log('loop_close', e.code + ':' + e.wasClean); pending--; }};

// Fail-closed TLS rejection tests
function expectTlsFail(tag, url) {{
  pending++;
  var seq = [];
  var s = new WebSocket(url);
  s.onerror = function(e) {{ seq.push('err[' + (e && e.message ? e.message : 'no-msg') + ']'); }};
  s.onclose = function(e) {{
    seq.push('close:' + e.code + ':' + e.wasClean);
    log(tag, seq.join(','));
    pending--;
  }};
}}
expectTlsFail('tls_untrusted', 'wss://127.0.0.1:{wss_untrusted.port}/u');
expectTlsFail('tls_wronghost', 'wss://127.0.0.1:{wss_wrong.port}/w');
expectTlsFail('tls_expired',   'wss://127.0.0.1:{wss_expired.port}/e');
expectTlsFail('tls_plaintext', 'wss://127.0.0.1:{plain_srv.port}/p');

// Worker wss:// inside HTTPS page
pending++;
var wk = new Worker('/worker-ws.js');
wk.onmessage = function(e) {{
  log('worker_msg', e.data);
  pending--;
  wk.terminate();
}};
wk.onerror = function(e) {{
  log('worker_err', e.message || 'err');
  pending--;
}};

var tries = 0;
function pump() {{
  if (pending === 0 || tries >= 120) {{
    log('done', '1');
    return;
  }}
  tries++;
  fetch('/yield').then(pump, pump);
}}
setTimeout(pump, 1);
</script>"""
            https_page = root / "https_page.html"
            https_page.write_text(https_html, encoding="utf-8")
            https_origin = f"https://localhost:{https_srv.port}"
            p1 = run_cli(
                str(binary),
                [str(https_page), f"--url={https_origin}/app/page.html", "--virtual-time-budget=2000"],
                args.timeout,
                env_extra=env_ca,
            )
            r1 = parse_checks(p1.stdout + "\n" + p1.stderr)
            check("HTTPS page exited cleanly", p1.returncode == 0 and r1.get("done") == "1", f"rc={p1.returncode}")
            check("HTTPS blocks ws:// remote host (SecurityError)", r1.get("mixed_host") == "SecurityError", str(r1.get("mixed_host")))
            check("HTTPS blocks ws://[::2] IPv6 non-loopback (SecurityError)", r1.get("mixed_ipv6") == "SecurityError", str(r1.get("mixed_ipv6")))
            check("WebSocket() without new throws TypeError", r1.get("no_new") == "TypeError", str(r1.get("no_new")))
            check("http:// scheme throws SyntaxError", r1.get("bad_scheme") == "SyntaxError", str(r1.get("bad_scheme")))
            check("fragment throws SyntaxError", r1.get("bad_frag") == "SyntaxError", str(r1.get("bad_frag")))
            check("duplicate subprotocols throw SyntaxError", r1.get("dup_proto") == "SyntaxError", str(r1.get("dup_proto")))
            check("invalid subprotocol token throws SyntaxError", r1.get("bad_proto") == "SyntaxError", str(r1.get("bad_proto")))
            check("send() in CONNECTING throws InvalidStateError", r1.get("send_early") == "InvalidStateError", str(r1.get("send_early")))
            check("close(999) throws InvalidAccessError", r1.get("bad_code") == "InvalidAccessError", str(r1.get("bad_code")))
            check("close() >123B reason throws SyntaxError", r1.get("long_reason") == "SyntaxError", str(r1.get("long_reason")))
            check("readonly url & valid/invalid binaryType assignment", r1.get("ip_ro") == f"wss://127.0.0.1:{wss_valid.port}/ip?q=1|arraybuffer", str(r1.get("ip_ro")))
            check("wss://127.0.0.1 IP SAN handshake, subprotocol, send & clean close",
                  r1.get("ip_proto") == "superchat" and r1.get("ip_msg") == "echo:hi-ip" and r1.get("ip_close") == "3000:clean-ip:true:3",
                  f"{r1.get('ip_proto')} / {r1.get('ip_msg')} / {r1.get('ip_close')}")
            check("wss://localhost DNS SAN + SNI echo & clean close",
                  r1.get("dns_msg") == "echo:hi-dns" and r1.get("dns_close") == "1000:true" and "localhost" in wss_valid.sni_names,
                  f"{r1.get('dns_msg')} / {r1.get('dns_close')} / sni={wss_valid.sni_names}")
            check("relative WebSocket('/wss-rel') resolves & connects over wss://",
                  r1.get("rel_url") == f"wss://localhost:{https_srv.port}/wss-rel" and r1.get("rel_msg") == "echo:hi-rel" and r1.get("rel_close") == "1000:true",
                  f"{r1.get('rel_url')} / {r1.get('rel_msg')}")
            check("HTTPS allows potentially-trustworthy loopback ws://127.0.0.1",
                  r1.get("loop_msg") == "echo:hi-loop" and r1.get("loop_close") == "1000:true",
                  f"{r1.get('loop_msg')} / {r1.get('loop_close')}")
            check("wss:// untrusted CA rejected fail-closed with 0 upgrades",
                  refused_before_close(r1.get("tls_untrusted")) and len(wss_untrusted.upgrades) == 0
                  and tls_refusal_code(r1.get("tls_untrusted")) == 18,
                  str(r1.get("tls_untrusted")))
            check("wss:// wrong SAN rejected fail-closed with 0 upgrades",
                  refused_before_close(r1.get("tls_wronghost")) and len(wss_wrong.upgrades) == 0
                  and tls_refusal_code(r1.get("tls_wronghost")) == 64,
                  str(r1.get("tls_wronghost")))
            check("wss:// expired cert rejected fail-closed with 0 upgrades",
                  refused_before_close(r1.get("tls_expired")) and len(wss_expired.upgrades) == 0
                  and tls_refusal_code(r1.get("tls_expired")) == 10,
                  str(r1.get("tls_expired")))
            check("wss:// plaintext TCP peer rejected fail-closed",
                  refused_before_close(r1.get("tls_plaintext"))
                  and tls_refusal_code(r1.get("tls_plaintext")) is None,
                  str(r1.get("tls_plaintext")))
            check("Worker inherits Origin and completes wss:// roundtrip",
                  r1.get("worker_msg") == "W:wss-worker-ok",
                  str(r1.get("worker_msg")))

            # Server-side wire assertions for HTTPS upgrades
            with wss_valid.lock:
                wss_upgrades = list(wss_valid.upgrades)
            with https_srv.lock:
                https_upgrades = list(https_srv.upgrades)
            with plain_srv.lock:
                plain_upgrades = list(plain_srv.upgrades)

            wss_cookie_up = next((u for u in wss_upgrades if u["path"] == "/wss-cookie"), None)
            check("wss:// upgrade sends the page's Secure cookie (wss matched as https)",
                  wss_cookie_up is not None and "wss_secure=1" in (wss_cookie_up["cookie"] or "")
                  and r1.get("wss_cookie_close") == "1000:true",
                  f"cookie={wss_cookie_up and wss_cookie_up['cookie']} close={r1.get('wss_cookie_close')}")

            keys = [u["key"] for u in wss_upgrades + https_upgrades + plain_upgrades]
            decoded_key_lens = [len(base64.b64decode(k)) for k in keys if k]
            check("Sec-WebSocket-Key is 16 random bytes & distinct across connections",
                  len(keys) >= 5 and all(n == 16 for n in decoded_key_lens) and len(set(keys)) == len(keys),
                  f"keys={len(keys)} unique={len(set(keys))}")
            check("Client data & close frames are masked on wire",
                  all(u["masks_ok"] for u in wss_upgrades + https_upgrades + plain_upgrades))
            ip_up = next((u for u in wss_upgrades if u["path"].startswith("/ip")), None)
            wk_up = next((u for u in wss_upgrades if u["path"] == "/worker-ok"), None)
            rel_up = next((u for u in https_upgrades if u["path"] == "/wss-rel"), None)
            loop_up = next((u for u in plain_upgrades if u["path"] == "/loop"), None)
            check("HTTPS page and Worker send exact Origin header on ws/wss upgrade",
                  ip_up is not None and ip_up["origin"] == https_origin
                  and wk_up is not None and wk_up["origin"] == https_origin
                  and rel_up is not None and rel_up["origin"] == https_origin
                  and loop_up is not None and loop_up["origin"] == https_origin,
                  f"origins={[ip_up and ip_up['origin'], wk_up and wk_up['origin'], rel_up and rel_up['origin'], loop_up and loop_up['origin']]}")

            # 2. HTTP page Origin + relative ws:// + sandboxed iframes (srcdoc & data:) opaque Origin: null
            data_iframe_src = (
                "data:text/html,<script>"
                "try{new WebSocket('/rel-in-data');parent.postMessage('data_rel:NO_THROW','*');}"
                "catch(e){parent.postMessage('data_rel:'+e.name,'*');}"
                f"var dws=new WebSocket('ws://127.0.0.1:{plain_srv.port}/sbx-data');"
                "dws.onopen=function(){dws.send('from-data');};"
                "dws.onmessage=function(e){parent.postMessage('data_msg:'+e.data,'*');dws.close(1000);};"
                "</script>"
            )
            http_html = f"""<!doctype html><meta charset="utf-8">
<iframe id="sbx" sandbox="allow-scripts" srcdoc="<script>
var rws = new WebSocket('/sbx-rel');
parent.postMessage('sbx_rel:' + rws.url, '*');
rws.onopen = function() {{ rws.send('from-sbx'); }};
rws.onmessage = function(e) {{ parent.postMessage('sbx_msg:' + e.data, '*'); rws.close(1000); }};
</script>"></iframe>
<iframe id="sbx_data" sandbox="allow-scripts" src="{data_iframe_src}"></iframe>
<script>
var pending = 6;
function log(k, v) {{ console.log('WSCHECK:' + k + '=' + v); }}
// The upgrade is a GET of this document's own URL space, so it carries the
// cookies the page set and keeps what the 101 response sets.
document.cookie = 'harness_pre=1; Path=/';
var cws = new WebSocket('/ws-cookie');
cws.onopen = function() {{ log('ws_cookie_page', document.cookie); cws.close(1000); pending--; }};
cws.onerror = function() {{ log('ws_cookie_page', 'ERR'); pending--; }};
window.addEventListener('message', function(e) {{
  var idx = String(e.data).indexOf(':');
  log(String(e.data).slice(0, idx), String(e.data).slice(idx + 1));
  pending--;
}});
var hws = new WebSocket('/http-rel');
log('http_rel_url', hws.url);
hws.onopen = function() {{ hws.send('hi-http'); }};
hws.onmessage = function(e) {{ log('http_msg', e.data); hws.close(1000); }};
hws.onclose = function(e) {{ log('http_close', e.code + ':' + e.wasClean); pending--; }};

var tries = 0;
function pump() {{
  if (pending <= 0 || tries >= 120) {{
    log('done', '1');
    return;
  }}
  tries++;
  fetch('/yield').then(pump, pump);
}}
setTimeout(pump, 1);
</script>"""
            http_page = root / "http_page.html"
            http_page.write_text(http_html, encoding="utf-8")
            http_origin = f"http://127.0.0.1:{plain_srv.port}"
            p2 = run_cli(
                str(binary),
                [str(http_page), f"--url={http_origin}/dir/index.html", "--virtual-time-budget=2000"],
                args.timeout,
            )
            r2 = parse_checks(p2.stdout + "\n" + p2.stderr)
            with plain_srv.lock:
                p2_upgrades = list(plain_srv.upgrades)
            http_rel_up = next((u for u in p2_upgrades if u["path"] == "/http-rel"), None)
            sbx_up = next((u for u in p2_upgrades if u["path"] == "/sbx-rel"), None)
            data_up = next((u for u in p2_upgrades if u["path"] == "/sbx-data"), None)
            check("HTTP page relative ws:// resolves, sends HTTP Origin, and echoes cleanly",
                  p2.returncode == 0 and r2.get("done") == "1"
                  and r2.get("http_rel_url") == f"ws://127.0.0.1:{plain_srv.port}/http-rel"
                  and r2.get("http_msg") == "echo:hi-http"
                  and r2.get("http_close") == "1000:true"
                  and http_rel_up is not None and http_rel_up["origin"] == http_origin,
                  f"{r2} origin={http_rel_up and http_rel_up['origin']}")
            check("Sandboxed srcdoc iframe uses fallback base URL while sending Origin: null",
                  r2.get("sbx_rel") == f"ws://127.0.0.1:{plain_srv.port}/sbx-rel"
                  and r2.get("sbx_msg") == "echo:from-sbx"
                  and sbx_up is not None and sbx_up["origin"] == "null",
                  f"{r2.get('sbx_rel')} / {r2.get('sbx_msg')} / origin={sbx_up and sbx_up['origin']}")
            check("Sandboxed data: iframe gets Origin: null and refuses relative URLs",
                  r2.get("data_rel") == "SyntaxError"
                  and r2.get("data_msg") == "echo:from-data"
                  and data_up is not None and data_up["origin"] == "null",
                  f"{r2.get('data_rel')} / {r2.get('data_msg')} / origin={data_up and data_up['origin']}")

            cookie_up = next((u for u in p2_upgrades if u["path"] == "/ws-cookie"), None)
            check("ws:// upgrade from a page carries document.cookie",
                  cookie_up is not None and "harness_pre=1" in (cookie_up["cookie"] or ""),
                  f"cookie={cookie_up and cookie_up['cookie']}")
            check("Set-Cookie on the 101 reaches the page's document.cookie",
                  "harness_after=1" in r2.get("ws_cookie_page", ""),
                  f"ws_cookie_page={r2.get('ws_cookie_page')}")

            # 3. file:// document sends Origin: null and rejects relative WebSocket URL
            file_html = f"""<!doctype html><meta charset="utf-8"><script>
var pending = 1;
function log(k, v) {{ console.log('WSCHECK:' + k + '=' + v); }}
try {{ new WebSocket('/file-rel'); log('file_rel', 'NO_THROW'); }}
catch (e) {{ log('file_rel', e.name); }}
var fws = new WebSocket('ws://127.0.0.1:{plain_srv.port}/from-file');
fws.onopen = function() {{ fws.send('hi-file'); }};
fws.onmessage = function(e) {{ log('file_msg', e.data); fws.close(1000); }};
fws.onclose = function() {{ pending--; }};
var tries = 0;
function pump() {{
  if (pending <= 0 || tries >= 120) {{ log('done', '1'); return; }}
  tries++;
  fetch('http://127.0.0.1:{plain_srv.port}/yield').then(pump, pump);
}}
setTimeout(pump, 1);
</script>"""
            file_page = root / "file_page.html"
            file_page.write_text(file_html, encoding="utf-8")
            p3 = run_cli(
                str(binary),
                [str(file_page), f"--url={file_page.as_uri()}", "--virtual-time-budget=2000"],
                args.timeout,
            )
            r3 = parse_checks(p3.stdout + "\n" + p3.stderr)
            with plain_srv.lock:
                file_up = next((u for u in plain_srv.upgrades if u["path"] == "/from-file"), None)
            check("file:// page sends Origin: null and rejects relative WebSocket URL",
                  p3.returncode == 0 and r3.get("done") == "1"
                  and r3.get("file_rel") == "SyntaxError"
                  and r3.get("file_msg") == "echo:hi-file"
                  and file_up is not None and file_up["origin"] == "null",
                  f"{r3} origin={file_up and file_up['origin']}")

            # 4. Stress-exit stability: concurrent wss:// connections torn down on immediate exit
            js_stress = (
                f"for (var i = 0; i < 4; i++) "
                f"new WebSocket('wss://127.0.0.1:{wss_valid.port}/stress' + i);"
            )
            stress_failures = 0
            for _ in range(args.stress_runs):
                sp = run_cli(str(binary), ["--run-js", js_stress], args.timeout, env_extra=env_ca)
                if sp.returncode != 0:
                    stress_failures += 1
                    print(f"  stress failure rc={sp.returncode}: {sp.stderr[:200]}")
                    if stress_failures >= 3:
                        break
            check(
                f"4x concurrent wss:// startup/exit stability ({args.stress_runs - stress_failures}/{args.stress_runs})",
                stress_failures == 0,
            )
        finally:
            https_srv.stop()
            wss_valid.stop()
            wss_untrusted.stop()
            wss_wrong.stop()
            wss_expired.stop()
            plain_srv.stop()

    print(f"\n{checks} checks; {failures} failure(s).")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
