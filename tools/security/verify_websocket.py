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
  6. HTTP CONNECT through HTTPS_PROXY/https_proxy and ALL_PROXY/all_proxy,
     uppercase/lowercase no_proxy host+port bypass, and fail-closed HTTP 407.
  7. Concurrent wss:// startup + immediate exit stability (--stress-runs).
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import os
import pathlib
import re
import select
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import zlib

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


def read_ws_frame(sock: socket.socket) -> tuple[int, bytes, bool, bytes, bool]:
    hdr = recv_exact(sock, 2)
    b0, b1 = hdr[0], hdr[1]
    opcode = b0 & 0x0F
    rsv1 = bool(b0 & 0x40)
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
    return opcode, bytes(payload), masked, mask, rsv1


def build_ws_frame(opcode: int, payload: bytes, fin: bool = True, rsv1: bool = False) -> bytes:
    header = bytearray([(0x80 if fin else 0) | (0x40 if rsv1 else 0) | (opcode & 0x0F)])
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
        pmd_mode = None
        pmd_wbits = 15
        pmd_nct = False
        if path == "/wss-pmd":
            pmd_mode = "default"
            pmd_wbits = 11
            resp += "Sec-WebSocket-Extensions: permessage-deflate; server_max_window_bits=11; client_max_window_bits=11\r\n"
        elif path == "/wss-pmd-nct":
            pmd_mode = "nct"
            pmd_wbits = 9
            pmd_nct = True
            resp += "Sec-WebSocket-Extensions: permessage-deflate; client_no_context_takeover; server_no_context_takeover; client_max_window_bits=9; server_max_window_bits=9\r\n"
        elif path == "/wss-pmd-bad":
            resp += "Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits=7\r\n"
        elif path == "/wss-pmd-bomb":
            pmd_mode = "bomb"
            resp += "Sec-WebSocket-Extensions: permessage-deflate\r\n"
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
            "offered_ext": headers.get("sec-websocket-extensions", ""),
            "cookie": headers.get("cookie"),
            "messages": [],
            "binary_messages": [],
            "compressed_wire_lens": [],
            "rsv1_flags": [],
            "masks_ok": True,
            "close_code": None,
            "close_reason": None,
        }

        def pmd_compress(comp_obj, data: bytes) -> bytes:
            raw = comp_obj.compress(data) + comp_obj.flush(zlib.Z_SYNC_FLUSH)
            assert raw.endswith(b"\x00\x00\xff\xff")
            return raw[:-4]

        comp = zlib.compressobj(6, zlib.DEFLATED, -pmd_wbits) if pmd_mode in ("default", "nct") else None
        decomp = zlib.decompressobj(-pmd_wbits) if pmd_mode in ("default", "nct") else None

        # Worker endpoints proactively push a greeting then close cleanly.
        if path == "/worker-ok":
            conn.sendall(build_ws_frame(0x01, b"wss-worker-ok"))
        elif path == "/worker-bin":
            conn.sendall(build_ws_frame(0x02, b"\x00\xfe\xff\x80"))
        elif pmd_mode == "bomb":
            bcomp = zlib.compressobj(9, zlib.DEFLATED, -15)
            bomb = pmd_compress(bcomp, b"\x00" * (20 * 1024 * 1024))
            conn.sendall(build_ws_frame(0x02, bomb, fin=True, rsv1=True))

        conn.settimeout(2.5)
        try:
            while True:
                opcode, payload, masked, mask, rsv1 = read_ws_frame(conn)
                if not masked or len(mask) != 4:
                    rec["masks_ok"] = False
                if opcode in (0x01, 0x02):
                    rec["rsv1_flags"].append(rsv1)
                    if pmd_mode in ("default", "nct") and rsv1:
                        rec["compressed_wire_lens"].append(len(payload))
                        payload = decomp.decompress(payload + b"\x00\x00\xff\xff")
                        if pmd_nct:
                            decomp = zlib.decompressobj(-pmd_wbits)
                if opcode == 0x01:
                    text = payload.decode("utf-8", errors="replace")
                    rec["messages"].append(text)
                    reply = f"echo:{text}".encode("utf-8")
                    if pmd_mode == "default" and len(rec["messages"]) == 2:
                        # Fragmented compressed reply across 2 frames + interleaved Ping!
                        c_rep = pmd_compress(comp, reply)
                        mid = max(1, len(c_rep) // 2)
                        conn.sendall(build_ws_frame(0x01, c_rep[:mid], fin=False, rsv1=True))
                        conn.sendall(build_ws_frame(0x09, b"pmd-ping", fin=True, rsv1=False))
                        conn.sendall(build_ws_frame(0x00, c_rep[mid:], fin=True, rsv1=False))
                    elif pmd_mode in ("default", "nct"):
                        c_rep = pmd_compress(comp, reply)
                        if pmd_nct:
                            comp = zlib.compressobj(6, zlib.DEFLATED, -pmd_wbits)
                        conn.sendall(build_ws_frame(0x01, c_rep, fin=True, rsv1=True))
                    else:
                        conn.sendall(build_ws_frame(0x01, reply))
                elif opcode == 0x02:
                    rec["binary_messages"].append(payload)
                    if pmd_mode in ("default", "nct"):
                        c_rep = pmd_compress(comp, payload)
                        if pmd_nct:
                            comp = zlib.compressobj(6, zlib.DEFLATED, -pmd_wbits)
                        conn.sendall(build_ws_frame(0x02, c_rep, fin=True, rsv1=True))
                    else:
                        conn.sendall(build_ws_frame(0x02, payload))
                elif opcode == 0x09:
                    conn.sendall(build_ws_frame(0x0A, payload))
                elif opcode == 0x0A:
                    rec["rsv1_flags"].append(("pong", rsv1))
                elif opcode == 0x08:
                    if len(payload) >= 2:
                        rec["close_code"] = struct.unpack("!H", payload[:2])[0]
                        rec["close_reason"] = payload[2:].decode("utf-8", errors="replace")
                    conn.sendall(build_ws_frame(0x08, payload))
                    break
        except (EOFError, OSError, ssl.SSLError, zlib.error):
            pass
        with self.lock:
            self.upgrades.append(rec)


class HttpConnectProxy:
    """A loopback HTTP proxy that records CONNECT and relays opaque TLS bytes."""

    def __init__(self, response_code: int = 200):
        self.response_code = response_code
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(64)
        self.sock.settimeout(0.25)
        self.port = self.sock.getsockname()[1]
        self.stop_event = threading.Event()
        self.lock = threading.Lock()
        self.requests: list[dict] = []
        self.workers: list[threading.Thread] = []
        self.thread = threading.Thread(target=self._accept_loop, daemon=True)
        self.thread.start()

    @property
    def url(self) -> str:
        return f"http://127.0.0.1:{self.port}"

    def stop(self) -> None:
        self.stop_event.set()
        try:
            self.sock.close()
        except OSError:
            pass
        self.thread.join(timeout=2.0)
        with self.lock:
            workers = list(self.workers)
        deadline = time.monotonic() + 2.0
        for worker in workers:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            worker.join(timeout=remaining)

    def _accept_loop(self) -> None:
        while not self.stop_event.is_set():
            try:
                conn, _ = self.sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            worker = threading.Thread(target=self._handle_conn, args=(conn,), daemon=True)
            with self.lock:
                self.workers.append(worker)
            worker.start()

    @staticmethod
    def _split_authority(authority: str) -> tuple[str, int] | None:
        if authority.startswith("["):
            close = authority.find("]")
            if close < 0 or authority[close + 1:close + 2] != ":":
                return None
            host, port_text = authority[1:close], authority[close + 2:]
        else:
            if ":" not in authority:
                return None
            host, port_text = authority.rsplit(":", 1)
        try:
            port = int(port_text)
        except ValueError:
            return None
        if not host or not 0 < port <= 65535:
            return None
        return host, port

    def _handle_conn(self, conn: socket.socket) -> None:
        conn.settimeout(3.0)
        upstream: socket.socket | None = None
        try:
            request = bytearray()
            while len(request) < 65536 and b"\r\n\r\n" not in request:
                byte = conn.recv(1)
                if not byte:
                    return
                request.extend(byte)
            end = request.find(b"\r\n\r\n")
            if end < 0:
                return
            lines = request[:end].decode("latin-1").split("\r\n")
            words = lines[0].split()
            headers: dict[str, str] = {}
            for line in lines[1:]:
                if ":" in line:
                    name, value = line.split(":", 1)
                    headers[name.strip().lower()] = value.strip()
            target = words[1] if len(words) >= 2 else ""
            record = {
                "method": words[0] if words else "",
                "request_line": lines[0],
                "target": target,
                "host": headers.get("host"),
                "headers": headers,
                "status": None,
            }
            with self.lock:
                self.requests.append(record)

            if self.response_code != 200:
                record["status"] = self.response_code
                if self.response_code == 407:
                    response = (
                        "HTTP/1.1 407 Proxy Authentication Required\r\n"
                        "Proxy-Authenticate: Basic realm=\"test\"\r\n"
                        "Content-Length: 0\r\nConnection: close\r\n\r\n"
                    )
                else:
                    response = (
                        f"HTTP/1.1 {self.response_code} Test refusal\r\n"
                        "Content-Length: 0\r\nConnection: close\r\n\r\n"
                    )
                conn.sendall(response.encode("ascii"))
                return

            if (len(words) == 3 and words[0] in ("GET", "HEAD", "POST", "OPTIONS") and
                    words[2] == "HTTP/1.1" and target.startswith("http://")):
                parsed_http = urllib.parse.urlsplit(target)
                dest_host = parsed_http.hostname or ""
                dest_port = parsed_http.port or 80
                if not dest_host:
                    record["status"] = 400
                    conn.sendall(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                    return
                origin_path = parsed_http.path or "/"
                if parsed_http.query:
                    origin_path += "?" + parsed_http.query
                try:
                    upstream = socket.create_connection((dest_host, dest_port), timeout=3.0)
                except OSError:
                    record["status"] = 502
                    conn.sendall(b"HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                    return
                record["status"] = 200
                fwd = [f"{words[0]} {origin_path} HTTP/1.1"]
                for hline in lines[1:]:
                    if not hline:
                        continue
                    hname = hline.split(":", 1)[0].strip().lower()
                    if hname in ("connection", "proxy-connection"):
                        continue
                    fwd.append(hline)
                fwd.append("Connection: close")
                upstream.sendall(("\r\n".join(fwd) + "\r\n\r\n").encode("latin-1"))
                while not self.stop_event.is_set():
                    data = upstream.recv(65536)
                    if not data:
                        return
                    conn.sendall(data)
                return

            destination = self._split_authority(target)
            if (len(words) != 3 or words[0] != "CONNECT" or words[2] != "HTTP/1.1" or
                    destination is None or headers.get("host") != target):
                record["status"] = 400
                conn.sendall(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                return

            try:
                upstream = socket.create_connection(destination, timeout=3.0)
            except OSError:
                record["status"] = 502
                conn.sendall(b"HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                return
            record["status"] = 200
            conn.sendall(b"HTTP/1.1 200 Connection Established\r\n\r\n")
            while not self.stop_event.is_set():
                readable, _, exceptional = select.select([conn, upstream], [], [conn, upstream], 0.25)
                if exceptional:
                    return
                for source in readable:
                    data = source.recv(65536)
                    if not data:
                        return
                    (upstream if source is conn else conn).sendall(data)
        except (OSError, ssl.SSLError, TimeoutError):
            return
        finally:
            if upstream is not None:
                try:
                    upstream.close()
                except OSError:
                    pass
            try:
                conn.close()
            except OSError:
                pass


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
        connect_proxy = HttpConnectProxy()
        lower_proxy: HttpConnectProxy | None = None
        refusing_proxy: HttpConnectProxy | None = None

        try:
            https_srv.routes["/worker-ws.js"] = (
                "application/javascript",
                (
                    f"var ws = new WebSocket('wss://127.0.0.1:{wss_valid.port}/worker-ok');\n"
                    f"ws.onmessage = function(e) {{ postMessage('W:' + e.data); ws.close(1000, 'wdone'); }};\n"
                    f"ws.onerror = function() {{ postMessage('W:ERR'); }};\n"
                ).encode("utf-8"),
            )
            https_srv.routes["/worker-bin-ws.js"] = (
                "application/javascript",
                (
                    f"var ws = new WebSocket('wss://127.0.0.1:{wss_valid.port}/worker-bin');\n"
                    "ws.binaryType = 'arraybuffer';\n"
                    "var step = 0;\n"
                    "ws.onmessage = function(e) {\n"
                    "  step++;\n"
                    "  if (step === 1) {\n"
                    "    var u = new Uint8Array(e.data);\n"
                    "    var ok1 = (e.data instanceof ArrayBuffer) && u.length === 4 && u[0] === 0 && u[1] === 254 && u[2] === 255 && u[3] === 128;\n"
                    "    ws.binaryType = 'blob';\n"
                    "    ws.send(new Uint8Array([9, 0, 255]).buffer);\n"
                    "    ws._ok1 = ok1;\n"
                    "  } else if (step === 2) {\n"
                    "    var ok2 = (e.data instanceof Blob) && e.data.size === 3 && e.data.type === '';\n"
                    "    postMessage('WB:' + ws._ok1 + ':' + ok2);\n"
                    "    ws.close(1000, 'wbdone');\n"
                    "  }\n"
                    "};\n"
                    "ws.onerror = function() { postMessage('WB:ERR'); };\n"
                ).encode("utf-8"),
            )

            env_ca = {"LITEBIRD_SSL_CA_FILE": str(pki["ca"])}
            no_proxy_for_wss_valid = f".localhost:{wss_valid.port}"
            env_proxy = {
                **env_ca,
                "HTTPS_PROXY": connect_proxy.url,
                "https_proxy": "",
                "NO_PROXY": no_proxy_for_wss_valid,
                "no_proxy": no_proxy_for_wss_valid,
                "ALL_PROXY": "",
                "all_proxy": "",
            }

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
var savedOpenEvent = null;
var openCallbackOrder = [];
wsIp.addEventListener('open', function(e) {{ openCallbackOrder.push('first'); e.stopImmediatePropagation(); }});
wsIp.addEventListener('open', function(e) {{ openCallbackOrder.push('second'); }});
wsIp.onopen = function(e) {{
  openCallbackOrder.push('property');
  savedOpenEvent = e;
  log('ip_open_event', (e instanceof Event) + ':' + (e instanceof MessageEvent) + ':' + e.type + ':' + (e.target === wsIp) + ':' + (e.currentTarget === wsIp) + ':' + e.eventPhase + ':' + e.isTrusted + ':' + (typeof e.composedPath === 'function' ? e.composedPath().length : -1));
  log('ip_proto', wsIp.protocol);
  wsIp.send('hi-ip');
}};
wsIp.onmessage = function(e) {{
  log('ip_message_event', (e instanceof MessageEvent) + ':' + (e instanceof Event) + ':' + e.type + ':' + (e.ports ? e.ports.length : -1) + ':' + (e.target === wsIp) + ':' + (e.currentTarget === wsIp) + ':' + e.eventPhase + ':' + e.isTrusted + ':' + (typeof e.composedPath === 'function' ? e.composedPath().length : -1));
  log('ip_msg', e.data);
  wsIp.close(3000, 'clean-ip');
}};
wsIp.onclose = function(e) {{
  log('ip_close_event', (e instanceof CloseEvent) + ':' + (e instanceof Event) + ':' + e.type + ':' + e.code + ':' + e.reason + ':' + e.wasClean + ':' + (e.target === wsIp) + ':' + (e.currentTarget === wsIp) + ':' + e.eventPhase + ':' + e.isTrusted + ':' + (typeof e.composedPath === 'function' ? e.composedPath().length : -1));
  log('ip_open_after_dispatch', (savedOpenEvent.currentTarget === null) + ':' + savedOpenEvent.eventPhase + ':' + savedOpenEvent.composedPath().length + ':' + (savedOpenEvent.target === wsIp));
  log('ip_open_callback_order', openCallbackOrder.join(','));
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
  var errorCallbackOrder = [];
  var savedErrorEvent = null;
  var s = new WebSocket(url);
  s.addEventListener('error', function(e) {{ errorCallbackOrder.push('first'); e.stopImmediatePropagation(); }});
  s.addEventListener('error', function(e) {{ errorCallbackOrder.push('second'); }});
  s.onerror = function(e) {{
    errorCallbackOrder.push('property');
    savedErrorEvent = e;
    seq.push('err[' + (e && e.message ? e.message : 'no-msg') + ']');
    if (tag === 'tls_untrusted') {{
      log('tls_error_event', (e instanceof Event) + ':' + (e instanceof MessageEvent) + ':' + e.type + ':' + (e.target === s) + ':' + (e.currentTarget === s) + ':' + e.eventPhase + ':' + e.isTrusted + ':' + (typeof e.composedPath === 'function' ? e.composedPath().length : -1) + ':' + (typeof e.message === 'string' && e.message.length > 0));
    }}
  }};
  s.onclose = function(e) {{
    seq.push('close:' + e.code + ':' + e.wasClean);
    log(tag, seq.join(','));
    if (tag === 'tls_untrusted') {{
      log('tls_error_after_dispatch', (savedErrorEvent.currentTarget === null) + ':' + savedErrorEvent.eventPhase + ':' + savedErrorEvent.composedPath().length + ':' + (savedErrorEvent.target === s));
      log('tls_error_callback_order', errorCallbackOrder.join(','));
    }}
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

// Binary wss:// round-trip (ArrayBuffer, TypedArray subarray, DataView, Blob, detached TypeError)
pending++;
var wsBin = new WebSocket('wss://127.0.0.1:{wss_valid.port}/wss-bin');
var binResults = [];
var binStep = 0;
function toHex(ab) {{
  var u = new Uint8Array(ab);
  var s = '';
  for (var i = 0; i < u.length; i++) s += (u[i] < 16 ? '0' : '') + u[i].toString(16);
  return s;
}}
wsBin.onopen = function() {{
  var det = new ArrayBuffer(4);
  det.transfer();
  try {{ wsBin.send(det); log('bin_detached', 'NO_THROW'); }}
  catch (e) {{ log('bin_detached', e.name); }}

  var b1 = new Uint8Array([0, 255, 254, 128, 65]);
  wsBin.send(b1.buffer);
  b1[0] = 99; // synchronous snapshot check
  var b2 = new Uint8Array([10, 20, 30, 40, 50]);
  wsBin.send(b2.subarray(1, 4));
  wsBin.send(new DataView(b2.buffer, 2, 2));
  wsBin.send(new Blob([b2.subarray(0, 2), new DataView(b2.buffer, 3, 2), '\\x00Z']));
  wsBin.send(new Uint8Array(0));
}};
wsBin.onmessage = function(e) {{
  binStep++;
  if (binStep === 1) {{
    var isBlob = (e.data instanceof Blob) && e.data.size === 5 && e.data.type === '';
    wsBin.binaryType = 'arraybuffer';
    pending++;
    e.data.arrayBuffer().then(function(ab) {{
      log('bin_blob', isBlob + ':' + toHex(ab));
      pending--;
    }});
  }} else {{
    binResults.push((e.data instanceof ArrayBuffer) + ':' + toHex(e.data));
    if (binStep === 5) {{
      log('bin_ab', binResults.join('|'));
      wsBin.close(1000);
    }}
  }}
}};
wsBin.onclose = function(e) {{
  log('bin_close', e.code + ':' + e.wasClean);
  pending--;
}};

// Worker binary wss:// (ArrayBuffer + Blob) inside HTTPS page
pending++;
var wkBin = new Worker('/worker-bin-ws.js');
wkBin.onmessage = function(e) {{
  log('worker_bin_msg', e.data);
  pending--;
  wkBin.terminate();
}};
wkBin.onerror = function(e) {{
  log('worker_bin_err', e.message || 'err');
  pending--;
}};

// RFC 7692 permessage-deflate over wss:// (window_bits=11, context takeover, fragmented + binary)
pending++;
var wsPmd = new WebSocket('wss://127.0.0.1:{wss_valid.port}/wss-pmd');
wsPmd.binaryType = 'arraybuffer';
var pmdGot = [];
wsPmd.onopen = function() {{
  log('pmd_ext', wsPmd.extensions);
  wsPmd.send('deflate-wss-chunk-'.repeat(60));
  wsPmd.send('deflate-wss-chunk-'.repeat(60));
  var b = new Uint8Array(64);
  for (var i = 0; i < 64; i++) b[i] = (i * 17) & 0xFF;
  wsPmd.send(b);
}};
wsPmd.onmessage = function(e) {{
  if (typeof e.data === 'string') {{
    pmdGot.push('S:' + e.data.length + ':' + e.data.slice(0, 12));
  }} else {{
    var u = new Uint8Array(e.data);
    pmdGot.push('B:' + u.length + ':' + u[0] + ',' + u[1] + ',' + u[63]);
  }}
  if (pmdGot.length === 3) {{
    log('pmd_got', pmdGot.join('|'));
    wsPmd.close(1000, 'pmd-done');
  }}
}};
wsPmd.onclose = function(e) {{
  log('pmd_close', e.code + ':' + e.wasClean);
  pending--;
}};

// RFC 7692 permessage-deflate no_context_takeover + window_bits=9
pending++;
var wsPmdNct = new WebSocket('wss://127.0.0.1:{wss_valid.port}/wss-pmd-nct');
var pmdNctGot = [];
wsPmdNct.onopen = function() {{
  log('pmd_nct_ext', wsPmdNct.extensions);
  wsPmdNct.send('nct-payload-'.repeat(80));
  wsPmdNct.send('nct-payload-'.repeat(80));
}};
wsPmdNct.onmessage = function(e) {{
  pmdNctGot.push(e.data.length);
  if (pmdNctGot.length === 2) {{
    log('pmd_nct_got', pmdNctGot.join(','));
    wsPmdNct.close(1000);
  }}
}};
wsPmdNct.onclose = function(e) {{
  log('pmd_nct_close', e.code + ':' + e.wasClean);
  pending--;
}};

// RFC 7692 invalid Sec-WebSocket-Extensions rejected fail-closed
pending++;
var wsPmdBad = new WebSocket('wss://127.0.0.1:{wss_valid.port}/wss-pmd-bad');
var pmdBadEv = [];
wsPmdBad.onerror = function() {{ pmdBadEv.push('err'); }};
wsPmdBad.onclose = function(e) {{
  pmdBadEv.push('close:' + e.code + ':' + e.wasClean);
  log('pmd_bad', pmdBadEv.join(','));
  pending--;
}};

// RFC 7692 decompression bomb (>16 MiB) rejected with 1009 without OOM
pending++;
var wsPmdBomb = new WebSocket('wss://127.0.0.1:{wss_valid.port}/wss-pmd-bomb');
var pmdBombEv = [];
wsPmdBomb.onerror = function() {{ pmdBombEv.push('err'); }};
wsPmdBomb.onclose = function(e) {{
  pmdBombEv.push('close:' + e.code + ':' + e.wasClean);
  log('pmd_bomb', pmdBombEv.join(','));
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
                env_extra=env_proxy,
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
            check("WebSocket delivers a trusted MessageEvent at the target",
                  r1.get("ip_message_event") == "true:true:message:0:true:true:2:true:1",
                  str(r1.get("ip_message_event")))
            check("WebSocket delivers a trusted CloseEvent with close details",
                  r1.get("ip_close_event") == "true:true:close:3000:clean-ip:true:true:true:2:true:1",
                  str(r1.get("ip_close_event")))
            check("WebSocket delivers a trusted Event for open at the target",
                  r1.get("ip_open_event") == "true:false:open:true:true:2:true:1",
                  str(r1.get("ip_open_event")))
            check("WebSocket clears open dispatch state and honors stopImmediatePropagation",
                  r1.get("ip_open_after_dispatch") == "true:0:0:true"
                  and r1.get("ip_open_callback_order") == "property,first",
                  f"after={r1.get('ip_open_after_dispatch')} callbacks={r1.get('ip_open_callback_order')}")
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
            check("WebSocket delivers a trusted Event for error at the target with its diagnostic",
                  r1.get("tls_error_event") == "true:false:error:true:true:2:true:1:true",
                  str(r1.get("tls_error_event")))
            check("WebSocket clears error dispatch state and honors stopImmediatePropagation",
                  r1.get("tls_error_after_dispatch") == "true:0:0:true"
                  and r1.get("tls_error_callback_order") == "property,first",
                  f"after={r1.get('tls_error_after_dispatch')} callbacks={r1.get('tls_error_callback_order')}")
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
            check("wss:// binary send(ArrayBuffer/TypedArray/DataView/Blob) & detached TypeError",
                  r1.get("bin_detached") == "TypeError"
                  and r1.get("bin_blob") == "true:00fffe8041"
                  and r1.get("bin_ab") == "true:141e28|true:1e28|true:0a142832005a|true:"
                  and r1.get("bin_close") == "1000:true",
                  f"det={r1.get('bin_detached')} blob={r1.get('bin_blob')} ab={r1.get('bin_ab')} close={r1.get('bin_close')}")
            check("Worker completes wss:// binary ArrayBuffer & Blob roundtrip",
                  r1.get("worker_bin_msg") == "WB:true:true",
                  str(r1.get("worker_bin_msg")))

            # Server-side wire assertions for HTTPS upgrades
            with wss_valid.lock:
                wss_upgrades = list(wss_valid.upgrades)
            with https_srv.lock:
                https_upgrades = list(https_srv.upgrades)

            pmd_rec = next((u for u in wss_upgrades if u["path"] == "/wss-pmd"), None)
            pmd_nct_rec = next((u for u in wss_upgrades if u["path"] == "/wss-pmd-nct"), None)
            pmd_bomb_rec = next((u for u in wss_upgrades if u["path"] == "/wss-pmd-bomb"), None)

            check("wss:// permessage-deflate (window_bits=11, takeover, fragmented, binary & wire RSV1)",
                  r1.get("pmd_ext") == "permessage-deflate; server_max_window_bits=11; client_max_window_bits=11"
                  and r1.get("pmd_got") == "S:1085:echo:deflate|S:1085:echo:deflate|B:64:0,17,47"
                  and r1.get("pmd_close") == "1000:true"
                  and pmd_rec is not None
                  and "permessage-deflate" in pmd_rec["offered_ext"]
                  and len(pmd_rec["compressed_wire_lens"]) == 3
                  and pmd_rec["compressed_wire_lens"][0] < 100
                  and pmd_rec["compressed_wire_lens"][1] <= pmd_rec["compressed_wire_lens"][0]
                  and ("pong", False) in pmd_rec["rsv1_flags"],
                  f"ext={r1.get('pmd_ext')} got={r1.get('pmd_got')} close={r1.get('pmd_close')} rec={pmd_rec}")
            check("wss:// permessage-deflate no_context_takeover & invalid extension fail-closed",
                  r1.get("pmd_nct_ext") == "permessage-deflate; client_no_context_takeover; server_no_context_takeover; client_max_window_bits=9; server_max_window_bits=9"
                  and r1.get("pmd_nct_got") == "965,965"
                  and r1.get("pmd_nct_close") == "1000:true"
                  and pmd_nct_rec is not None
                  and len(pmd_nct_rec["compressed_wire_lens"]) == 2
                  and pmd_nct_rec["compressed_wire_lens"][0] < 100
                  and r1.get("pmd_bad") == "err,close:1006:false",
                  f"nct={r1.get('pmd_nct_got')} bad={r1.get('pmd_bad')} rec={pmd_nct_rec}")
            check("wss:// permessage-deflate decompression bomb (>16 MiB) rejected with 1009",
                  r1.get("pmd_bomb") == "err,close:1009:false"
                  and pmd_bomb_rec is not None
                  and pmd_bomb_rec["close_code"] == 1009,
                  f"bomb={r1.get('pmd_bomb')} rec={pmd_bomb_rec}")
            with plain_srv.lock:
                plain_upgrades = list(plain_srv.upgrades)

            wss_cookie_up = next((u for u in wss_upgrades if u["path"] == "/wss-cookie"), None)
            check("wss:// upgrade sends the page's Secure cookie (wss matched as https)",
                  wss_cookie_up is not None and "wss_secure=1" in (wss_cookie_up["cookie"] or "")
                  and r1.get("wss_cookie_close") == "1000:true",
                  f"cookie={wss_cookie_up and wss_cookie_up['cookie']} close={r1.get('wss_cookie_close')}")
            wss_bin_up = next((u for u in wss_upgrades if u["path"] == "/wss-bin"), None)
            check("wss:// binary frames arrive on wire as opcode 0x02 with exact raw bytes",
                  wss_bin_up is not None
                  and wss_bin_up["binary_messages"] == [
                      b"\x00\xff\xfe\x80A",
                      b"\x14\x1e\x28",
                      b"\x1e\x28",
                      b"\x0a\x14\x28\x32\x00Z",
                      b"",
                  ],
                  f"bin_msgs={wss_bin_up and [m.hex() for m in wss_bin_up['binary_messages']]}")

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

            with connect_proxy.lock:
                p1_proxy_requests = list(connect_proxy.requests)
            valid_ip_authority = f"127.0.0.1:{wss_valid.port}"
            rel_authority = f"localhost:{https_srv.port}"
            check("HTTPS_PROXY sends wss:// through a validated HTTP CONNECT tunnel",
                  any(r["target"] == valid_ip_authority and r["status"] == 200
                      for r in p1_proxy_requests)
                  and any(r["target"] == rel_authority and r["status"] == 200
                          for r in p1_proxy_requests)
                  and all(r["request_line"] == f"CONNECT {r['target']} HTTP/1.1"
                          and r["host"] == r["target"] and set(r["headers"]) == {"host"}
                          and r["status"] == 200 for r in p1_proxy_requests),
                  f"requests={len(p1_proxy_requests)} targets={[r['target'] for r in p1_proxy_requests[:8]]}")
            check("NO_PROXY host-and-port suffix bypasses only its wss:// target",
                  f"localhost:{wss_valid.port}" not in [r["target"] for r in p1_proxy_requests]
                  and rel_authority in [r["target"] for r in p1_proxy_requests]
                  and wss_cookie_up is not None and wss_cookie_up["host"] == f"localhost:{wss_valid.port}",
                  f"targets={[r['target'] for r in p1_proxy_requests]}")
            p1_proxy_count = len(p1_proxy_requests)

            lower_proxy = HttpConnectProxy()
            lower_html = f"""<!doctype html><meta charset="utf-8"><script>
var pending = 2;
function log(k, v) {{ console.log('WSCHECK:' + k + '=' + v); }}
function pump() {{ if (pending <= 0) {{ log('done', '1'); return; }} fetch('/yield').then(pump, pump); }}
var lower = new WebSocket('wss://127.0.0.1:{wss_valid.port}/lowercase-proxy');
lower.onopen = function() {{ lower.send('lowercase-proxy'); }};
lower.onmessage = function(e) {{ log('lower_proxy_msg', e.data); lower.close(1000); }};
lower.onerror = function(e) {{ log('lower_proxy_err', e.message || 'error'); }};
lower.onclose = function(e) {{ log('lower_proxy_close', e.code + ':' + e.wasClean); pending--; }};
var bypass = new WebSocket('wss://localhost:{wss_valid.port}/lowercase-no-proxy');
bypass.onopen = function() {{ bypass.send('lowercase-no-proxy'); }};
bypass.onmessage = function(e) {{ log('lower_bypass_msg', e.data); bypass.close(1000); }};
bypass.onerror = function(e) {{ log('lower_bypass_err', e.message || 'error'); }};
bypass.onclose = function(e) {{ log('lower_bypass_close', e.code + ':' + e.wasClean); pending--; }};
setTimeout(pump, 1);
</script>"""
            lower_page = root / "lower_proxy_page.html"
            lower_page.write_text(lower_html, encoding="utf-8")
            lower_env = {
                **env_ca,
                "HTTPS_PROXY": "",
                "https_proxy": lower_proxy.url,
                "NO_PROXY": "",
                "no_proxy": f".localhost:{wss_valid.port}",
                "ALL_PROXY": "",
                "all_proxy": "",
            }
            lower_result = run_cli(
                str(binary),
                [str(lower_page), f"--url={https_origin}/lower-proxy-page", "--virtual-time-budget=2000"],
                args.timeout,
                env_extra=lower_env,
            )
            lower_checks = parse_checks(lower_result.stdout + "\n" + lower_result.stderr)
            with lower_proxy.lock:
                lower_requests = list(lower_proxy.requests)
            lower_ws_requests = [r for r in lower_requests if r["target"] == valid_ip_authority]
            check("lower-case https_proxy and no_proxy route through CONNECT or bypass by host and port",
                  lower_result.returncode == 0 and lower_checks.get("done") == "1"
                  and lower_checks.get("lower_proxy_msg") == "echo:lowercase-proxy"
                  and lower_checks.get("lower_proxy_close") == "1000:true"
                  and lower_checks.get("lower_bypass_msg") == "echo:lowercase-no-proxy"
                  and lower_checks.get("lower_bypass_close") == "1000:true"
                  and len(lower_ws_requests) == 1
                  and f"localhost:{wss_valid.port}" not in [r["target"] for r in lower_requests]
                  and all(r["request_line"] == f"CONNECT {r['target']} HTTP/1.1"
                          and r["host"] == r["target"]
                          and set(r["headers"]) == {"host"}
                          and r["status"] == 200 for r in lower_requests),
                  f"checks={lower_checks} requests={lower_requests}")
            lower_proxy.stop()
            lower_proxy = None

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
                env_extra=env_proxy,
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
            with connect_proxy.lock:
                p2_proxy_count = len(connect_proxy.requests)
            check("HTTPS_PROXY is not applied to plain ws:// connections",
                  p2.returncode == 0 and p2_proxy_count == p1_proxy_count,
                  f"CONNECT requests before={p1_proxy_count} after={p2_proxy_count}")

            all_proxy_html = f"""<!doctype html><meta charset="utf-8"><script>
var pending = 1;
function log(k, v) {{ console.log('WSCHECK:' + k + '=' + v); }}
function pump() {{ if (pending <= 0) {{ log('done', '1'); return; }} fetch('/yield').then(pump, pump); }}
var ws = new WebSocket('wss://127.0.0.1:{wss_valid.port}/all-proxy');
ws.onopen = function() {{ ws.send('all-proxy'); }};
ws.onmessage = function(e) {{ log('all_proxy_msg', e.data); ws.close(1000); }};
ws.onerror = function(e) {{ log('all_proxy_err', e.message || 'error'); }};
ws.onclose = function(e) {{ log('all_proxy_close', e.code + ':' + e.wasClean); pending--; }};
setTimeout(pump, 1);
</script>"""
            all_proxy_page = root / "all_proxy_page.html"
            all_proxy_page.write_text(all_proxy_html, encoding="utf-8")
            all_proxy_env = {
                **env_ca,
                "HTTPS_PROXY": "",
                "https_proxy": "",
                "ALL_PROXY": "",
                "all_proxy": connect_proxy.url,
                "NO_PROXY": "",
                "no_proxy": "",
            }
            with connect_proxy.lock:
                all_proxy_before = len(connect_proxy.requests)
            all_proxy_result = run_cli(
                str(binary),
                [str(all_proxy_page), f"--url={https_origin}/all-proxy-page", "--virtual-time-budget=2000"],
                args.timeout,
                env_extra=all_proxy_env,
            )
            all_proxy_checks = parse_checks(all_proxy_result.stdout + "\n" + all_proxy_result.stderr)
            with connect_proxy.lock:
                all_proxy_requests = list(connect_proxy.requests)
            all_proxy_record = all_proxy_requests[all_proxy_before:] if len(all_proxy_requests) > all_proxy_before else []
            all_proxy_ws = [r for r in all_proxy_record if r["target"] == valid_ip_authority]
            with wss_valid.lock:
                all_proxy_upgrade = next((u for u in wss_valid.upgrades if u["path"] == "/all-proxy"), None)
            check("ALL_PROXY is the fallback for wss:// and completes the TLS WebSocket handshake",
                  all_proxy_result.returncode == 0 and all_proxy_checks.get("done") == "1"
                  and all_proxy_checks.get("all_proxy_msg") == "echo:all-proxy"
                  and all_proxy_checks.get("all_proxy_close") == "1000:true"
                  and len(all_proxy_ws) == 1
                  and all_proxy_ws[0]["status"] == 200
                  and all(r["request_line"] == f"CONNECT {r['target']} HTTP/1.1"
                          and r["host"] == r["target"]
                          and set(r["headers"]) == {"host"}
                          and r["status"] == 200 for r in all_proxy_record)
                  and all_proxy_upgrade is not None,
                  f"checks={all_proxy_checks} records={all_proxy_record} upgrade={all_proxy_upgrade is not None}")

            refusing_proxy = HttpConnectProxy(response_code=407)
            refused_html = f"""<!doctype html><meta charset="utf-8"><script>
var pending = 1, errorMessage = '';
function log(k, v) {{ console.log('WSCHECK:' + k + '=' + v); }}
function pump() {{ if (pending <= 0) {{ log('done', '1'); return; }} fetch('/yield').then(pump, pump); }}
var ws = new WebSocket('wss://127.0.0.1:{wss_valid.port}/proxy-407');
ws.onerror = function(e) {{ errorMessage = e.message || ''; }};
ws.onclose = function(e) {{ log('proxy_407', errorMessage + '|' + e.code + ':' + e.wasClean); pending--; }};
setTimeout(pump, 1);
</script>"""
            refused_page = root / "proxy_407_page.html"
            refused_page.write_text(refused_html, encoding="utf-8")
            refusing_env = {
                **env_ca,
                "HTTPS_PROXY": refusing_proxy.url,
                "https_proxy": "",
                "NO_PROXY": "",
                "no_proxy": "",
                "ALL_PROXY": "",
                "all_proxy": "",
            }
            refused_result = run_cli(
                str(binary),
                [str(refused_page), f"--url={https_origin}/proxy-refused", "--virtual-time-budget=2000"],
                args.timeout,
                env_extra=refusing_env,
            )
            refused_checks = parse_checks(refused_result.stdout + "\n" + refused_result.stderr)
            with refusing_proxy.lock:
                refused_requests = list(refusing_proxy.requests)
            refused_ws = [r for r in refused_requests if r["target"] == valid_ip_authority]
            with wss_valid.lock:
                refused_upgrade = next((u for u in wss_valid.upgrades if u["path"] == "/proxy-407"), None)
            check("HTTP proxy 407 fails closed before TLS/Upgrade and sends no Proxy-Authorization",
                  refused_result.returncode == 0 and refused_checks.get("done") == "1"
                  and refused_checks.get("proxy_407") == "WebSocket HTTP proxy CONNECT failed (407)|1006:false"
                  and len(refused_ws) == 1
                  and all(r["status"] == 407
                          and r["request_line"] == f"CONNECT {r['target']} HTTP/1.1"
                          and "proxy-authorization" not in r["headers"]
                          for r in refused_requests)
                  and refused_upgrade is None,
                  f"checks={refused_checks} requests={refused_requests} upgrade={refused_upgrade is not None}")
            refusing_proxy.stop()
            refusing_proxy = None

            # 2b. Plain ws:// proxy routing via HTTP_PROXY / all_proxy / NO_PROXY and fail-closed 407
            plain_authority = f"127.0.0.1:{plain_srv.port}"
            plain_proxy = HttpConnectProxy()
            ws_proxy_html = f"""<!doctype html><meta charset="utf-8"><script>
var pending = 2;
function log(k, v) {{ console.log('WSCHECK:' + k + '=' + v); }}
function pump() {{ if (pending <= 0) {{ log('done', '1'); return; }} fetch('/yield').then(pump, pump); }}
var pws = new WebSocket('ws://127.0.0.1:{plain_srv.port}/ws-proxy');
pws.onopen = function() {{ pws.send('ws-via-http-proxy'); }};
pws.onmessage = function(e) {{ log('ws_proxy_msg', e.data); pws.close(1000); }};
pws.onclose = function(e) {{ log('ws_proxy_close', e.code + ':' + e.wasClean); pending--; }};
var bws = new WebSocket('ws://localhost:{plain_srv.port}/ws-noproxy');
bws.onopen = function() {{ bws.send('ws-bypass'); }};
bws.onmessage = function(e) {{ log('ws_bypass_msg', e.data); bws.close(1000); }};
bws.onclose = function(e) {{ log('ws_bypass_close', e.code + ':' + e.wasClean); pending--; }};
setTimeout(pump, 1);
</script>"""
            ws_proxy_page = root / "ws_proxy_page.html"
            ws_proxy_page.write_text(ws_proxy_html, encoding="utf-8")
            ws_proxy_env = {
                **env_ca,
                "HTTP_PROXY": plain_proxy.url,
                "http_proxy": "",
                "HTTPS_PROXY": "",
                "https_proxy": "",
                "NO_PROXY": f"localhost:{plain_srv.port}",
                "no_proxy": "",
                "ALL_PROXY": "",
                "all_proxy": "",
            }
            ws_proxy_res = run_cli(
                str(binary),
                [str(ws_proxy_page), f"--url={http_origin}/ws-proxy-page", "--virtual-time-budget=2000"],
                args.timeout,
                env_extra=ws_proxy_env,
            )
            ws_proxy_checks = parse_checks(ws_proxy_res.stdout + "\n" + ws_proxy_res.stderr)
            with plain_proxy.lock:
                ws_proxy_requests = list(plain_proxy.requests)
            ws_connects = [r for r in ws_proxy_requests if r["method"] == "CONNECT"]
            http_forwards = [r for r in ws_proxy_requests if r["method"] == "GET"]
            check("HTTP_PROXY routes plain ws:// through CONNECT while NO_PROXY bypasses direct",
                  ws_proxy_res.returncode == 0 and ws_proxy_checks.get("done") == "1"
                  and ws_proxy_checks.get("ws_proxy_msg") == "echo:ws-via-http-proxy"
                  and ws_proxy_checks.get("ws_proxy_close") == "1000:true"
                  and ws_proxy_checks.get("ws_bypass_msg") == "echo:ws-bypass"
                  and ws_proxy_checks.get("ws_bypass_close") == "1000:true"
                  and len(ws_connects) == 1
                  and ws_connects[0]["target"] == plain_authority
                  and ws_connects[0]["headers"] == {"host": plain_authority}
                  and ws_connects[0]["status"] == 200
                  and len(http_forwards) >= 1
                  and all(r["target"] == f"http://{plain_authority}/yield" and r["status"] == 200
                          for r in http_forwards),
                  f"checks={ws_proxy_checks} requests={ws_proxy_requests}")

            ws_all_before = len(ws_proxy_requests)
            ws_all_html = f"""<!doctype html><meta charset="utf-8"><script>
var pending = 1;
function log(k, v) {{ console.log('WSCHECK:' + k + '=' + v); }}
function pump() {{ if (pending <= 0) {{ log('done', '1'); return; }} fetch('/yield').then(pump, pump); }}
var aws = new WebSocket('ws://127.0.0.1:{plain_srv.port}/ws-all-proxy');
aws.onopen = function() {{ aws.send('ws-via-all-proxy'); }};
aws.onmessage = function(e) {{ log('ws_all_msg', e.data); aws.close(1000); }};
aws.onclose = function(e) {{ log('ws_all_close', e.code + ':' + e.wasClean); pending--; }};
setTimeout(pump, 1);
</script>"""
            ws_all_page = root / "ws_all_proxy_page.html"
            ws_all_page.write_text(ws_all_html, encoding="utf-8")
            ws_all_env = {
                **env_ca,
                "HTTP_PROXY": "",
                "http_proxy": "",
                "HTTPS_PROXY": "",
                "https_proxy": "",
                "NO_PROXY": "",
                "no_proxy": "",
                "ALL_PROXY": "",
                "all_proxy": plain_proxy.url,
            }
            ws_all_res = run_cli(
                str(binary),
                [str(ws_all_page), f"--url={http_origin}/ws-all-page", "--virtual-time-budget=2000"],
                args.timeout,
                env_extra=ws_all_env,
            )
            ws_all_checks = parse_checks(ws_all_res.stdout + "\n" + ws_all_res.stderr)
            with plain_proxy.lock:
                ws_all_reqs = list(plain_proxy.requests)[ws_all_before:]
            ws_all_connects = [r for r in ws_all_reqs if r["method"] == "CONNECT"]
            check("all_proxy is the fallback for plain ws:// through CONNECT",
                  ws_all_res.returncode == 0 and ws_all_checks.get("done") == "1"
                  and ws_all_checks.get("ws_all_msg") == "echo:ws-via-all-proxy"
                  and ws_all_checks.get("ws_all_close") == "1000:true"
                  and len(ws_all_connects) == 1
                  and ws_all_connects[0]["target"] == plain_authority
                  and ws_all_connects[0]["status"] == 200,
                  f"checks={ws_all_checks} reqs={ws_all_reqs}")
            plain_proxy.stop()

            refusing_ws_proxy = HttpConnectProxy(response_code=407)
            ws_407_html = f"""<!doctype html><meta charset="utf-8"><script>
var pending = 1, errorMessage = '';
function log(k, v) {{ console.log('WSCHECK:' + k + '=' + v); }}
function pump() {{ if (pending <= 0) {{ log('done', '1'); return; }} fetch('/yield').then(pump, pump); }}
var ws = new WebSocket('ws://127.0.0.1:{plain_srv.port}/ws-proxy-407');
ws.onerror = function(e) {{ errorMessage = e.message || ''; }};
ws.onclose = function(e) {{ log('ws_proxy_407', errorMessage + '|' + e.code + ':' + e.wasClean); pending--; }};
setTimeout(pump, 1);
</script>"""
            ws_407_page = root / "ws_407_page.html"
            ws_407_page.write_text(ws_407_html, encoding="utf-8")
            ws_407_res = run_cli(
                str(binary),
                [str(ws_407_page), f"--url={http_origin}/ws-proxy-407", "--virtual-time-budget=2000"],
                args.timeout,
                env_extra={
                    **env_ca,
                    "HTTP_PROXY": refusing_ws_proxy.url,
                    "http_proxy": "",
                    "HTTPS_PROXY": "",
                    "https_proxy": "",
                    "NO_PROXY": "",
                    "no_proxy": "",
                    "ALL_PROXY": "",
                    "all_proxy": "",
                },
            )
            ws_407_checks = parse_checks(ws_407_res.stdout + "\n" + ws_407_res.stderr)
            with refusing_ws_proxy.lock:
                ws_407_reqs = list(refusing_ws_proxy.requests)
            with plain_srv.lock:
                ws_407_up = next((u for u in plain_srv.upgrades if u["path"] == "/ws-proxy-407"), None)
            check("Plain ws:// HTTP_PROXY 407 fails closed before Upgrade and sends no Proxy-Authorization",
                  ws_407_res.returncode == 0 and ws_407_checks.get("done") == "1"
                  and ws_407_checks.get("ws_proxy_407") == "WebSocket HTTP proxy CONNECT failed (407)|1006:false"
                  and any(r["method"] == "CONNECT" and r["target"] == plain_authority and r["status"] == 407
                          for r in ws_407_reqs)
                  and all("proxy-authorization" not in r["headers"] for r in ws_407_reqs)
                  and ws_407_up is None,
                  f"checks={ws_407_checks} reqs={ws_407_reqs} upgrade={ws_407_up is not None}")
            refusing_ws_proxy.stop()

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
                sp = run_cli(str(binary), ["--run-js", js_stress], args.timeout, env_extra=env_proxy)
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
            if refusing_proxy is not None:
                refusing_proxy.stop()
            if lower_proxy is not None:
                lower_proxy.stop()
            connect_proxy.stop()
            https_srv.stop()
            wss_valid.stop()
            wss_untrusted.stop()
            wss_wrong.stop()
            wss_expired.stop()
            plain_srv.stop()

    # Exercise trust anchors that terminate before a self-signed root. Keep
    # these end-to-end checks in the same normal and sanitizer CI path.
    from verify_wss_partial_chain import run as check_partial_chain
    extra_checks, extra_failures = check_partial_chain(str(binary), args.timeout)
    checks += extra_checks
    failures += extra_failures

    print(f"\n{checks} checks; {failures} failure(s).")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
