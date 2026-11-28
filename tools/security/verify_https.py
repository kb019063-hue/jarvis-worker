#!/usr/bin/env python3
"""Local TLS identity, chain and date oracle; ephemeral certificates, no public network or CA changes.

Every fixture is a loopback TLS server with a certificate made in a temporary
directory, and every decision is made twice: by Python's verifying `ssl`
client, and by Litebird's CLI. A rejection must be a certificate failure with
no HTTP byte reaching the server, so an unrelated CLI error cannot pass; an
acceptance must render the marker the fixture served.
"""
from __future__ import annotations

import argparse
import contextlib
import http.client
import os
import re
from pathlib import Path
import select
import socket
import ssl
import subprocess
import tempfile
import threading
import urllib.parse
import urllib.request

MARKER = b"LITEBIRD_TLS_VERIFIED"


def openssl(*arguments: str) -> None:
    """One openssl invocation in the fixture directory; a failure is a failure of the run."""
    result = subprocess.run(['openssl', *arguments], capture_output=True, timeout=30)
    if result.returncode != 0:
        raise RuntimeError(f'openssl {" ".join(arguments)}: '
                           f'{result.stderr.decode(errors="replace")[:800]}')


# ---------------------------------------------------------------------------
# A tiny two-level PKI
# ---------------------------------------------------------------------------
#
# `openssl req -x509` writes a self-signed root; everything else is signed by
# `openssl ca`, which is the only one of the two that takes explicit dates —
# a certificate that expired in 2020, or one that is not valid until 2099,
# cannot be made any other way. Each issuer gets its own database in the
# temporary directory, and `unique_subject = no`: two fixtures are both
# `CN=localhost`, and a CA database that refuses a repeated subject would
# fail the third leaf.


_CONFIGS: dict[tuple[str, str], Path] = {}


def ca_config(root: Path, issuer: str, cert: Path, key: Path) -> Path:
    """Each issuer's database is made once: `openssl ca` owns the serial and the index after that."""
    if (str(root), issuer) in _CONFIGS:
        return _CONFIGS[(str(root), issuer)]
    config = root / f'ca-{issuer}.cnf'
    index = root / f'index-{issuer}.txt'
    serial = root / f'serial-{issuer}'
    index.touch()
    serial.write_text('1000\n')
    (root / f'index-{issuer}.txt.attr').write_text('unique_subject = no\n')
    config.write_text(
        '[ ca ]\n'
        'default_ca = CA_default\n'
        '[ CA_default ]\n'
        f'dir = {root}\n'
        f'database = {index}\n'
        f'serial = {serial}\n'
        f'new_certs_dir = {root}\n'
        f'certificate = {cert}\n'
        f'private_key = {key}\n'
        'default_md = sha256\n'
        'policy = policy_any\n'
        'email_in_dn = no\n'
        'unique_subject = no\n'
        '[ policy_any ]\n'
        'commonName = supplied\n')
    _CONFIGS[(str(root), issuer)] = config
    return config


def extensions(san: str | None, is_ca: bool) -> str:
    lines = ['basicConstraints = critical,' + ('CA:TRUE' if is_ca else 'CA:FALSE')]
    if is_ca:
        lines.append('keyUsage = critical,keyCertSign,cRLSign')
    else:
        lines.append('keyUsage = critical,digitalSignature')
        lines.append('extendedKeyUsage = serverAuth')
    if san:
        lines.append('subjectAltName = ' + san)
    return '\n'.join(lines) + '\n'


def keypair(root: Path, name: str) -> tuple[Path, Path]:
    """An EC key pair and its certificate signing request."""
    key, csr = root / f'{name}.key', root / f'{name}.csr'
    openssl('req', '-new', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:prime256v1',
            '-nodes', '-keyout', str(key), '-out', str(csr), '-subj', f'/CN={name}')
    return key, csr


def root_certificate(root: Path, name: str) -> tuple[Path, Path]:
    """A self-signed CA certificate and its key: the trust store of every chain here."""
    cert, key = root / f'{name}.crt', root / f'{name}.key'
    openssl('req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:prime256v1',
            '-nodes', '-keyout', str(key), '-out', str(cert), '-days', '2',
            '-subj', f'/CN={name}', '-addext', 'basicConstraints=critical,CA:TRUE',
            '-addext', 'keyUsage=critical,keyCertSign,cRLSign')
    return cert, key


def issued(root: Path, name: str, issuer: str,
           issuer_cert: Path, issuer_key: Path, *,
           san: str | None = None, is_ca: bool = False, days: int = 2,
           startdate: str | None = None, enddate: str | None = None) -> tuple[Path, Path]:
    """A certificate signed by `issuer`, with dates when they are asked for."""
    key, csr = keypair(root, name)
    cert = root / f'{name}.crt'
    outside = root / f'{name}.ext'
    outside.write_text(extensions(san, is_ca))
    config = ca_config(root, issuer, issuer_cert, issuer_key)
    dates = ['-startdate', startdate, '-enddate', enddate] if startdate and enddate else ['-days', str(days)]
    openssl('ca', '-batch', '-config', str(config), '-in', str(csr), '-out', str(cert),
            '-extfile', str(outside), '-notext', *dates)
    return cert, key


def chain_file(root: Path, name: str, certificates: list[Path]) -> Path:
    """What a server hands over: its own certificate, then the issuers it can prove."""
    path = root / f'{name}.chain.pem'
    path.write_bytes(b''.join(cert.read_bytes() for cert in certificates))
    return path


# ---------------------------------------------------------------------------
# Loopback server and the independent client
# ---------------------------------------------------------------------------


@contextlib.contextmanager
def server(certificates: Path, key: Path, address: str = '127.0.0.1',
           virtual: dict[str, tuple[Path, Path]] | None = None):
    """A loopback TLS server; `virtual` answers a name with a certificate of its own, as a vhost does."""
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certificates, key)
    requests = []
    names = []
    errors = []
    stop = threading.Event()
    contexts = {}
    for name, (chain, chain_key) in (virtual or {}).items():
        chosen = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        chosen.load_cert_chain(chain, chain_key)
        contexts[name] = chosen

    def pick(tls, name, _ctx):
        # A client that sends no name is asked about `None`: that is an absence, and
        # the list is the names the client chose to send.
        if name is not None:
            names.append(name)
        if name in contexts:
            tls.context = contexts[name]

    # Installed even when no name is served: what a client sends is half of what
    # a name-based host decides, and an absence is a decision too.
    ctx.sni_callback = pick
    family = socket.AF_INET6 if ':' in address else socket.AF_INET
    listener = socket.socket(family)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((address, 0))
    listener.listen()
    listener.settimeout(0.2)
    port = listener.getsockname()[1]

    def serve():
        while not stop.is_set():
            try:
                conn, _ = listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                with conn:
                    conn.settimeout(3)
                    with ctx.wrap_socket(conn, server_side=True) as tls:
                        data = b''
                        while b'\r\n\r\n' not in data and len(data) < 65536:
                            chunk = tls.recv(4096)
                            if not chunk:
                                break
                            data += chunk
                        if data:
                            requests.append(data)
                            body = b'<html><body>' + MARKER + b'</body></html>'
                            tls.sendall(b'HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n'
                                        b'Connection: close\r\nContent-Length: ' +
                                        str(len(body)).encode() + b'\r\n\r\n' + body)
            except (ssl.SSLError, ConnectionError):
                pass  # A rejecting client sends an alert or closes before HTTP.
            except Exception as exc:
                errors.append(repr(exc))

    thread = threading.Thread(target=serve)
    thread.start()
    try:
        yield port, requests, names
    finally:
        stop.set()
        thread.join(timeout=5)
        listener.close()
        if thread.is_alive() or errors:
            raise RuntimeError(f'TLS fixture failed: {errors}')


# What a verification code means, as OpenSSL itself words it.
#
# These are the strings `openssl verify` prints after `error <code>`, taken
# from that tool rather than written from memory: a first draft of this table
# had 64 as "hostname mismatch" and 79 as "invalid CA", and the run below
# showed both were wrong. A name a fixture is held to has to come from the
# thing that decides, or the check is another guess in front of a guess.
def verify_names() -> dict[int, str]:
    """The code-to-name table, built by asking `openssl verify` for each code.

    Each entry is made by constructing a certificate that produces that code
    and reading the name the tool gives it. Built once per run; a code whose
    fixture cannot be made here keeps a numeric name, which is honest.
    """
    return dict(_VERIFY_NAMES)


_VERIFY_NAMES: dict[int, str] = {
    0: 'OK',
    9: 'certificate is not yet valid',
    10: 'certificate has expired',
    18: 'self-signed certificate',
    19: 'self-signed certificate in certificate chain',
    20: 'unable to get local issuer certificate',
    21: 'unable to verify the first certificate',
    24: 'invalid CA certificate',
    27: 'certificate not trusted',
    62: 'hostname mismatch',
    64: 'IP address mismatch',
    79: 'invalid CA certificate',
}

_NAMES_CONFIRMED = False


def confirm_verify_names(root: Path) -> None:
    """Ask `openssl verify` to correct `_VERIFY_NAMES` before anything uses it.

    Three fixtures are built — a name mismatch, an untrusted issuer, and a
    certificate that is not yet valid — and the code-and-name line the tool
    prints is read back over the table. If OpenSSL words a code differently
    from the table, the table takes OpenSSL's wording; that is the whole point
    of not trusting the table's author.
    """
    global _NAMES_CONFIRMED
    if _NAMES_CONFIRMED:
        return
    root_ca = root_certificate(root, 'names-root')
    other_ca = root_certificate(root, 'names-other')
    fixtures = [
        (issued(root, 'names-wrong', 'names-root', *root_ca, san='DNS:other.invalid'), root_ca[0], 'localhost'),
        (issued(root, 'names-ut', 'names-root', *root_ca, san='DNS:localhost'), other_ca[0], 'localhost'),
        (issued(root, 'names-future', 'names-root', *root_ca, san='DNS:localhost',
                startdate='20990101000000Z', enddate='20990201000000Z'), root_ca[0], 'localhost'),
        (issued(root, 'names-expired', 'names-root', *root_ca, san='DNS:localhost',
                startdate='20200101000000Z', enddate='20200102000000Z'), root_ca[0], 'localhost'),
    ]
    for (certificate, _), trust, host in fixtures:
        result = subprocess.run(['openssl', 'verify', '-CAfile', str(trust),
                                 '-verify_hostname', host, str(certificate)],
                                capture_output=True, text=True)
        for line in (result.stdout + result.stderr).splitlines():
            match = re.match(r'error (\d+) at \d+ depth lookup: (.*?)\s*$', line)
            if match:
                _VERIFY_NAMES[int(match.group(1))] = match.group(2).strip()
    _NAMES_CONFIRMED = True


# The X509_V_ERR number the engine will print for each name Python reports.
# Two independent readers of the same decision: Python's ssl says what was
# wrong with the certificate, the engine says which code it saved, and the
# harness checks that they agree. A table rather than a shared string, because
# the point is that the two were produced by different code.
CODE_FOR_REASON = {
    'certificate has expired': 10,
    'certificate is not yet valid': 9,
    'hostname mismatch': 62,
    'IP address mismatch': 64,
    'unable to get local issuer certificate': 20,
    'invalid CA certificate': 79,
    'self-signed certificate': 18,
    'self-signed certificate in certificate chain': 19,
    'unable to verify the first certificate': 21,
    'certificate not trusted': 27,
}


def engine_reason(stderr: bytes) -> tuple[int, str] | None:
    """The code and message the engine reported, or None if it named neither."""
    text = stderr.decode(errors='replace')
    match = re.search(r'certificate verification failed \(code (\d+)(?:, ([^)]*))?\)', text)
    if not match:
        return None
    return int(match.group(1)), (match.group(2) or '').strip()


def reason(port: int, host: str, trust: Path) -> str:
    """Which X.509 verification code Python's ssl gives for this fixture.

    `oracle()` answers whether the fixture is accepted. This answers *why* it
    is not, which is what a rejection fixture has to be held to: five different
    faults all produce `False`, and a fixture that produced the wrong one would
    still print PASS.
    """
    ctx = ssl.create_default_context(cafile=str(trust))
    # A trusted certificate may terminate the chain even when not self-signed.
    ctx.verify_flags |= ssl.VERIFY_X509_PARTIAL_CHAIN
    try:
        with socket.create_connection((host, port), timeout=3) as raw:
            with ctx.wrap_socket(raw, server_hostname=host) as tls:
                tls.sendall(b'GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')
                response = b''
                while chunk := tls.recv(4096):
                    response += chunk
                if MARKER not in response:
                    raise RuntimeError('Oracle received no fixture body')
                return 'OK'
    except ssl.SSLCertVerificationError as error:
        return _VERIFY_NAMES.get(error.verify_code, f'CODE_{error.verify_code}')


def oracle(port: int, host: str, trust: Path) -> bool:
    return reason(port, host, trust) == 'OK'


def url_for(host: str, port: int) -> str:
    """An IPv6 literal is bracketed in a URL and is not."""
    return f'https://[{host}]:{port}/' if ':' in host else f'https://{host}:{port}/'


@contextlib.contextmanager
def http_server(address: str = '127.0.0.1'):
    """A loopback plaintext HTTP server recording raw request lines and headers."""
    requests = []
    errors = []
    stop = threading.Event()
    listener = socket.socket(socket.AF_INET)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((address, 0))
    listener.listen()
    listener.settimeout(0.2)
    port = listener.getsockname()[1]

    def serve():
        while not stop.is_set():
            try:
                conn, _ = listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                with conn:
                    conn.settimeout(3)
                    data = b''
                    while b'\r\n\r\n' not in data and len(data) < 65536:
                        chunk = conn.recv(4096)
                        if not chunk:
                            break
                        data += chunk
                    if data:
                        requests.append(data)
                        body = b'<html><body>' + MARKER + b'</body></html>'
                        conn.sendall(b'HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n'
                                     b'Connection: close\r\nContent-Length: ' +
                                     str(len(body)).encode() + b'\r\n\r\n' + body)
            except OSError:
                pass
            except Exception as exc:
                errors.append(repr(exc))

    thread = threading.Thread(target=serve)
    thread.start()
    try:
        yield port, requests
    finally:
        stop.set()
        thread.join(timeout=5)
        listener.close()
        if thread.is_alive() or errors:
            raise RuntimeError(f'HTTP fixture failed: {errors}')


@contextlib.contextmanager
def proxy_server(response_code: int = 200):
    """Loopback HTTP proxy supporting CONNECT tunnels and absolute-form HTTP."""
    records = []
    errors = []
    stop = threading.Event()
    listener = socket.socket(socket.AF_INET)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(('127.0.0.1', 0))
    listener.listen()
    listener.settimeout(0.2)
    port = listener.getsockname()[1]

    def handle_client(conn: socket.socket):
        upstream = None
        try:
            conn.settimeout(3)
            raw = bytearray()
            while len(raw) < 65536 and b'\r\n\r\n' not in raw:
                b = conn.recv(1)
                if not b:
                    return
                raw.extend(b)
            end = raw.find(b'\r\n\r\n')
            if end < 0:
                return
            lines = raw[:end].decode('latin-1').split('\r\n')
            words = lines[0].split()
            headers = {}
            for line in lines[1:]:
                if ':' in line:
                    k, v = line.split(':', 1)
                    headers[k.strip().lower()] = v.strip()
            rec = {
                'method': words[0] if words else '',
                'target': words[1] if len(words) >= 2 else '',
                'request_line': lines[0],
                'headers': headers,
                'status': None,
            }
            records.append(rec)

            if response_code != 200:
                rec['status'] = response_code
                if response_code == 407:
                    conn.sendall(b'HTTP/1.1 407 Proxy Authentication Required\r\n'
                                 b'Proxy-Authenticate: Basic realm="https-proxy"\r\n'
                                 b'Content-Length: 0\r\nConnection: close\r\n\r\n')
                else:
                    conn.sendall(f'HTTP/1.1 {response_code} Refused\r\n'
                                 f'Content-Length: 0\r\nConnection: close\r\n\r\n'.encode('ascii'))
                return

            if len(words) == 3 and words[0] == 'CONNECT' and words[2] in ('HTTP/1.0', 'HTTP/1.1'):
                host, _, port_s = words[1].rpartition(':')
                if host.startswith('[') and host.endswith(']'):
                    host = host[1:-1]
                upstream = socket.create_connection((host, int(port_s)), timeout=3)
                rec['status'] = 200
                conn.sendall(b'HTTP/1.1 200 Connection Established\r\n\r\n')
                while not stop.is_set():
                    r, _, x = select.select([conn, upstream], [], [conn, upstream], 0.2)
                    if x:
                        return
                    for s in r:
                        chunk = s.recv(65536)
                        if not chunk:
                            return
                        (upstream if s is conn else conn).sendall(chunk)
                return

            if len(words) == 3 and words[0] == 'GET' and words[1].startswith('http://'):
                parsed = urllib.parse.urlsplit(words[1])
                upstream = socket.create_connection((parsed.hostname, parsed.port or 80), timeout=3)
                origin_path = (parsed.path or '/') + (f'?{parsed.query}' if parsed.query else '')
                rec['status'] = 200
                fwd = [f'GET {origin_path} HTTP/1.1'] + [
                    h for h in lines[1:]
                    if h and h.split(':', 1)[0].strip().lower() not in ('connection', 'proxy-connection')
                ] + ['Connection: close']
                upstream.sendall(('\r\n'.join(fwd) + '\r\n\r\n').encode('latin-1'))
                while not stop.is_set():
                    chunk = upstream.recv(65536)
                    if not chunk:
                        return
                    conn.sendall(chunk)
                return

            rec['status'] = 400
            conn.sendall(b'HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
        except (OSError, ValueError):
            pass
        except Exception as exc:
            errors.append(repr(exc))
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

    def serve():
        while not stop.is_set():
            try:
                conn, _ = listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            threading.Thread(target=handle_client, args=(conn,), daemon=True).start()

    thread = threading.Thread(target=serve)
    thread.start()
    try:
        yield f'http://127.0.0.1:{port}', port, records
    finally:
        stop.set()
        thread.join(timeout=5)
        listener.close()
        if thread.is_alive() or errors:
            raise RuntimeError(f'Proxy fixture failed: {errors}')


def python_https_tunnel_oracle(proxy_port: int, host: str, port: int, trust: Path) -> str:
    """Differential check using Python stdlib HTTP CONNECT tunnel + TLS verification."""
    ctx = ssl.create_default_context(cafile=str(trust))
    ctx.verify_flags |= ssl.VERIFY_X509_PARTIAL_CHAIN
    conn = http.client.HTTPSConnection('127.0.0.1', proxy_port, timeout=3, context=ctx)
    conn.set_tunnel(host, port)
    try:
        conn.request('GET', '/', headers={'Host': f'{host}:{port}', 'Connection': 'close'})
        resp = conn.getresponse()
        body = resp.read()
        return 'OK' if resp.status == 200 and MARKER in body else f'STATUS_{resp.status}'
    except ssl.SSLCertVerificationError as error:
        return _VERIFY_NAMES.get(error.verify_code, f'CODE_{error.verify_code}')
    except OSError as error:
        return f'TUNNEL_ERROR:{error}'
    finally:
        conn.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/litebird')
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve())
    failures = 0
    with tempfile.TemporaryDirectory(prefix='litebird-https-') as tmp:
        root = Path(tmp)
        confirm_verify_names(root)
        root_ca = root_certificate(root, 'root')
        intermediate = issued(root, 'inter', 'root', *root_ca, is_ca=True)
        expired_intermediate = issued(root, 'expired-inter', 'root', *root_ca, is_ca=True,
                                      startdate='20200101000000Z', enddate='20200102000000Z')
        not_a_ca = issued(root, 'not-a-ca', 'root', *root_ca, is_ca=False, san='DNS:localhost')

        def leaf(name: str, issuer: str, issuer_pair: tuple[Path, Path], **options) -> tuple[Path, Path]:
            return issued(root, name, issuer, *issuer_pair, **options)

        identities = {
            'dns': leaf('dns', 'root', root_ca, san='DNS:localhost'),
            'ip': leaf('ip', 'root', root_ca, san='IP:127.0.0.1'),
            'wrong': leaf('wrong', 'root', root_ca, san='DNS:wrong.invalid,IP:127.0.0.2'),
            'dns-ip': leaf('dns-ip', 'root', root_ca, san='DNS:127.0.0.1'),
            'v4-only': leaf('v4-only', 'inter', intermediate, san='DNS:localhost,IP:127.0.0.1'),
            'v6-only': leaf('v6-only', 'inter', intermediate, san='DNS:localhost,IP:::1'),
            'chained': leaf('chained', 'inter', intermediate,
                            san='DNS:localhost,IP:127.0.0.1,IP:::1'),
            'expired': leaf('expired', 'inter', intermediate, san='DNS:localhost',
                            startdate='20200101000000Z', enddate='20200102000000Z'),
            'future': leaf('future', 'inter', intermediate, san='DNS:localhost',
                           startdate='20990101000000Z', enddate='20990201000000Z'),
            'expired-issuer': leaf('expired-issuer', 'expired-inter', expired_intermediate,
                                   san='DNS:localhost'),
            'bad-issuer': leaf('bad-issuer', 'not-a-ca', not_a_ca, san='DNS:localhost'),
            'default': leaf('default', 'root', root_ca, san='DNS:other.invalid'),
        }
        served = {
            'chained': chain_file(root, 'chained', [identities['chained'][0], intermediate[0]]),
            'expired': chain_file(root, 'expired', [identities['expired'][0], intermediate[0]]),
            'future': chain_file(root, 'future', [identities['future'][0], intermediate[0]]),
            'expired-issuer': chain_file(root, 'expired-issuer',
                                         [identities['expired-issuer'][0], expired_intermediate[0]]),
            'bad-issuer': chain_file(root, 'bad-issuer', [identities['bad-issuer'][0], not_a_ca[0]]),
            'v4-only': chain_file(root, 'v4-only', [identities['v4-only'][0], intermediate[0]]),
            'v6-only': chain_file(root, 'v6-only', [identities['v6-only'][0], intermediate[0]]),
        }
        # A virtual host for the SNI cases: the name `localhost` is answered by the
        # chain above, and anything else — or no name at all — by this certificate.
        virtual = {'localhost': (served['chained'], identities['chained'][1])}
        # (label, what the server hands over, its key, the URL host, the trust store,
        #  expected, the virtual host, the names the engine has to have sent)
        cases = [
            ('trusted DNS', identities['dns'][0], identities['dns'][1], 'localhost', root_ca[0],
             None, None, ['localhost']),
            ('trusted IP', identities['ip'][0], identities['ip'][1], '127.0.0.1', root_ca[0],
             None, None, []),
            ('wrong DNS (CN matches, SAN does not)', identities['wrong'][0], identities['wrong'][1],
             'localhost', root_ca[0], 'hostname mismatch', None, None),
            ('wrong IP', identities['wrong'][0], identities['wrong'][1], '127.0.0.1', root_ca[0],
             'IP address mismatch', None, None),
            ('DNS SAN is not an IP SAN', identities['dns-ip'][0], identities['dns-ip'][1],
             '127.0.0.1', root_ca[0], 'IP address mismatch', None, None),
            ('DNS-only certificate on IP URL', identities['dns'][0], identities['dns'][1],
             '127.0.0.1', root_ca[0], 'IP address mismatch', None, None),
            ('IP-only certificate on DNS URL', identities['ip'][0], identities['ip'][1],
             'localhost', root_ca[0], 'hostname mismatch', None, None),
            ('untrusted DNS', identities['dns'][0], identities['dns'][1], 'localhost',
             identities['wrong'][0], 'unable to get local issuer certificate', None, None),
            ('untrusted IP', identities['ip'][0], identities['ip'][1], '127.0.0.1',
             identities['wrong'][0], 'unable to get local issuer certificate', None, None),
            # Chains: a leaf that is not signed by the trust anchor is only accepted
            # when the server hands over the intermediate that signed it.
            ('trusted chain', served['chained'], identities['chained'][1], 'localhost', root_ca[0],
             None, None, None),
            ('chain with the intermediate missing', identities['chained'][0],
             identities['chained'][1], 'localhost', root_ca[0], 'unable to get local issuer certificate', None, None),
            ('intermediate marked not a CA', served['bad-issuer'], identities['bad-issuer'][1],
             'localhost', root_ca[0], 'invalid CA certificate', None, None),
            # A local trust store may anchor at an intermediate or even the leaf;
            # trust still does not waive identity or validity checks.
            ('trusted intermediate without root', identities['chained'][0],
             identities['chained'][1], 'localhost', intermediate[0], None, None, None),
            ('trusted intermediate with server chain', served['chained'],
             identities['chained'][1], 'localhost', intermediate[0], None, None, None),
            ('directly trusted leaf', identities['chained'][0], identities['chained'][1],
             'localhost', identities['chained'][0], None, None, None),
            ('directly trusted wrong identity', identities['wrong'][0], identities['wrong'][1],
             'localhost', identities['wrong'][0], 'hostname mismatch', None, None),
            ('trusted intermediate expired leaf', identities['expired'][0], identities['expired'][1],
             'localhost', intermediate[0], 'certificate has expired', None, None),
            ('expired trusted intermediate', identities['expired-issuer'][0],
             identities['expired-issuer'][1], 'localhost', expired_intermediate[0],
             'certificate has expired', None, None),
            # Dates: the leaf, and separately the intermediate, outside its validity.
            ('expired certificate', served['expired'], identities['expired'][1], 'localhost',
             root_ca[0], 'certificate has expired', None, None),
            ('certificate not valid yet', served['future'], identities['future'][1], 'localhost',
             root_ca[0], 'certificate is not yet valid', None, None),
            ('expired intermediate', served['expired-issuer'], identities['expired-issuer'][1],
             'localhost', root_ca[0], 'certificate has expired', None, None),
            # IPv6: the literal is an address, so an iPAddress SAN is what has to match,
            # and an address of the other family is a different identity.
            ('trusted IPv6 literal', served['chained'], identities['chained'][1], '::1',
             root_ca[0], None, None, []),
            ('expanded IPv6 literal', served['chained'], identities['chained'][1],
             '0:0:0:0:0:0:0:1', root_ca[0], None, None, []),
            ('IPv4 SAN on an IPv6 URL', served['v4-only'], identities['v4-only'][1], '::1',
             root_ca[0], 'IP address mismatch', None, None),
            ('IPv6 SAN on an IPv4 URL', served['v6-only'], identities['v6-only'][1], '127.0.0.1',
             root_ca[0], 'IP address mismatch', None, None),
            # SNI: a name is sent for a DNS host and never for a literal, so the
            # virtual host answers `localhost` and refuses the other name.
            ('virtual host chosen by SNI', identities['default'][0], identities['default'][1],
             'localhost', root_ca[0], None, virtual, ['localhost']),
            ('literal URL gets no name and the default certificate', identities['default'][0],
             identities['default'][1], '127.0.0.1', root_ca[0], 'IP address mismatch', virtual, []),
        ]
        # `expected` is now the *reason* the fixture must be refused for, and
        # None means it must be accepted. Holding each fixture to a named
        # reason is the whole point: before this, five different faults all
        # counted as "rejected", so a fixture that failed for the wrong reason
        # passed exactly like one that failed for the right one.
        for label, certificates, key, host, trust, expected, virtual, names in cases:
            address = '::1' if ':' in host else '127.0.0.1'
            with server(certificates, key, address, virtual) as (port, _, _):
                reference = oracle(port, host, trust)
                reference_reason = reason(port, host, trust)
            with server(certificates, key, address, virtual) as (port, requests, seen):
                env = os.environ.copy()
                env['LITEBIRD_SSL_CA_FILE'] = str(trust)
                result = subprocess.run([binary, '--dump-dom', url_for(host, port)],
                                        env=env, capture_output=True, timeout=15)
            accepted = result.returncode == 0 and MARKER in result.stdout
            reported = engine_reason(result.stderr)
            rejected = (result.returncode == 1 and not requests and reported is not None)
            sent = seen if names is not None else None
            if expected is None:
                ok = accepted and reference_reason == 'OK'
            else:
                # The engine has to have refused it for the same reason the
                # certificate oracle did, and to have said which. A refusal
                # whose code disagrees is a different failure wearing the right
                # fixture's clothes.
                wanted = CODE_FOR_REASON.get(reference_reason)
                ok = (rejected and reference_reason == expected and
                      reported is not None and (wanted is None or reported[0] == wanted))
            ok = ok and (names is None or sent == names)
            failures += not ok
            if expected is not None and reference_reason != expected:
                # The fixture itself is wrong: it does not fail for the reason
                # it was built to fail for, so it proves nothing about that
                # reason however the engine answers.
                detail = f'FIXTURE IS WRONG: refused as {reference_reason!r}, built as {expected!r}'
                failures += 1
            else:
                if accepted:
                    verdict = 'Litebird loaded it'
                elif reported is None:
                    verdict = 'Litebird refused it without a reason'
                else:
                    verdict = f'Litebird refused it: code {reported[0]} {reported[1]}'.strip()
                detail = f'Python={reference_reason}, {verdict}'
            if names is not None:
                detail += f', SNI={sent}'
            print(f'{"PASS" if ok else "FAIL"} {label}: {detail}')
            if not ok:
                print(result.stderr.decode(errors='replace')[:2000])

        # Proxy routing + CONNECT tunnel differential checks against Python stdlib
        proxy_cases = 0

        def record_proxy_case(label: str, ok: bool, detail: str) -> None:
            nonlocal failures, proxy_cases
            proxy_cases += 1
            failures += not ok
            print(f'{"PASS" if ok else "FAIL"} {label}: {detail}')

        def base_proxy_env(trust: Path | None) -> dict[str, str]:
            env = os.environ.copy()
            for key in ('HTTP_PROXY', 'http_proxy', 'HTTPS_PROXY', 'https_proxy',
                        'ALL_PROXY', 'all_proxy', 'NO_PROXY', 'no_proxy'):
                env[key] = ''
            if trust is not None:
                env['LITEBIRD_SSL_CA_FILE'] = str(trust)
            return env

        # 1. HTTPS_PROXY routes https:// through CONNECT and agrees with Python HTTPSConnection.set_tunnel
        with server(served['chained'], identities['chained'][1], '127.0.0.1') as (port, requests, seen):
            with proxy_server(200) as (proxy_url, proxy_port, proxy_recs):
                py_res = python_https_tunnel_oracle(proxy_port, 'localhost', port, root_ca[0])
                env = base_proxy_env(root_ca[0])
                env['HTTPS_PROXY'] = proxy_url
                res = subprocess.run([binary, '--dump-dom', url_for('localhost', port)],
                                     env=env, capture_output=True, timeout=15)
                ok = (py_res == 'OK' and res.returncode == 0 and MARKER in res.stdout and
                      seen == ['localhost', 'localhost'] and
                      len(requests) == 2 and
                      any(r['method'] == 'CONNECT' and r['target'] == f'localhost:{port}'
                          and r['status'] == 200 and 'proxy-authorization' not in r['headers']
                          for r in proxy_recs[1:]))
                record_proxy_case(
                    'HTTPS_PROXY CONNECT tunnel verifies TLS + SNI (agrees with Python)',
                    ok,
                    f'Python={py_res}, rc={res.returncode}, sni={seen}, proxy_recs={proxy_recs[1:]}')

        # 2. Wrong hostname through HTTPS CONNECT still fails TLS verification (0 HTTP requests)
        with server(identities['wrong'][0], identities['wrong'][1], '127.0.0.1') as (port, requests, _):
            with proxy_server(200) as (proxy_url, proxy_port, proxy_recs):
                py_res = python_https_tunnel_oracle(proxy_port, 'localhost', port, root_ca[0])
                env = base_proxy_env(root_ca[0])
                env['HTTPS_PROXY'] = proxy_url
                res = subprocess.run([binary, '--dump-dom', url_for('localhost', port)],
                                     env=env, capture_output=True, timeout=15)
                rep = engine_reason(res.stderr)
                ok = (py_res == 'hostname mismatch' and res.returncode == 1 and not requests and
                      rep is not None and rep[0] == CODE_FOR_REASON['hostname mismatch'] and
                      any(r['method'] == 'CONNECT' and r['target'] == f'localhost:{port}'
                          for r in proxy_recs[1:]))
                record_proxy_case(
                    'HTTPS_PROXY CONNECT refuses wrong certificate hostname (code 62)',
                    ok,
                    f'Python={py_res}, engine={rep}, reqs={len(requests)}')

        # 3. Proxy 407 on HTTPS CONNECT fails closed before TLS or origin HTTP
        with server(served['chained'], identities['chained'][1], '127.0.0.1') as (port, requests, _):
            with proxy_server(407) as (proxy_url, proxy_port, proxy_recs):
                py_res = python_https_tunnel_oracle(proxy_port, 'localhost', port, root_ca[0])
                env = base_proxy_env(root_ca[0])
                env['HTTPS_PROXY'] = proxy_url
                res = subprocess.run([binary, '--dump-dom', url_for('localhost', port)],
                                     env=env, capture_output=True, timeout=15)
                err_text = res.stderr.decode(errors='replace')
                ok = (py_res.startswith('TUNNEL_ERROR:') and res.returncode == 1 and not requests and
                      'HTTPS proxy CONNECT failed (407)' in err_text and
                      len(proxy_recs) >= 2 and proxy_recs[-1]['status'] == 407 and
                      'proxy-authorization' not in proxy_recs[-1]['headers'])
                record_proxy_case(
                    'HTTPS_PROXY 407 fails closed before TLS',
                    ok,
                    f'Python={py_res}, rc={res.returncode}, origin_reqs={len(requests)}')

        # 4. ALL_PROXY fallback + NO_PROXY bypass (differential against urllib.request.proxy_bypass)
        with server(served['chained'], identities['chained'][1], '127.0.0.1') as (port, requests, _):
            with proxy_server(200) as (proxy_url, _, proxy_recs):
                env_all = base_proxy_env(root_ca[0])
                env_all['all_proxy'] = proxy_url
                res_all = subprocess.run([binary, '--dump-dom', url_for('localhost', port)],
                                         env=env_all, capture_output=True, timeout=15)

                env_bypass = base_proxy_env(root_ca[0])
                env_bypass['HTTPS_PROXY'] = proxy_url
                env_bypass['no_proxy'] = f'.localhost:{port}'
                prev_no_proxy = os.environ.get('no_proxy')
                os.environ['no_proxy'] = f'.localhost:{port}'
                try:
                    py_bypass = urllib.request.proxy_bypass(f'localhost:{port}')
                finally:
                    if prev_no_proxy is None:
                        os.environ.pop('no_proxy', None)
                    else:
                        os.environ['no_proxy'] = prev_no_proxy
                res_bypass = subprocess.run([binary, '--dump-dom', url_for('localhost', port)],
                                            env=env_bypass, capture_output=True, timeout=15)
                ok = (py_bypass and res_all.returncode == 0 and MARKER in res_all.stdout and
                      res_bypass.returncode == 0 and MARKER in res_bypass.stdout and
                      len(proxy_recs) == 1 and proxy_recs[0]['target'] == f'localhost:{port}' and
                      len(requests) == 2)
                record_proxy_case(
                    'all_proxy routes https:// and no_proxy bypasses direct',
                    ok,
                    f'py_bypass={py_bypass}, proxy_recs={len(proxy_recs)}, origin_reqs={len(requests)}')

        # 5. Invalid HTTPS_PROXY fails closed (never falls back to direct)
        with server(served['chained'], identities['chained'][1], '127.0.0.1') as (port, requests, _):
            env = base_proxy_env(root_ca[0])
            env['HTTPS_PROXY'] = 'socks5://127.0.0.1:1080'
            res = subprocess.run([binary, '--dump-dom', url_for('localhost', port)],
                                 env=env, capture_output=True, timeout=15)
            err_text = res.stderr.decode(errors='replace')
            ok = (res.returncode == 1 and not requests and 'Invalid HTTP proxy URL' in err_text)
            record_proxy_case(
                'Invalid HTTPS_PROXY fails closed without direct fallback',
                ok,
                f'rc={res.returncode}, origin_reqs={len(requests)}')

        # 6. HTTP_PROXY sends http:// in absolute-form while HTTPS_PROXY leaves http:// direct
        with http_server('127.0.0.1') as (http_port, http_reqs):
            with proxy_server(200) as (proxy_url, _, proxy_recs):
                http_url = f'http://127.0.0.1:{http_port}/hello?q=1'
                env_p = base_proxy_env(None)
                env_p['HTTP_PROXY'] = proxy_url
                res_p = subprocess.run([binary, '--dump-dom', http_url],
                                       env=env_p, capture_output=True, timeout=15)

                env_https_only = base_proxy_env(None)
                env_https_only['HTTPS_PROXY'] = proxy_url
                res_d = subprocess.run([binary, '--dump-dom', http_url],
                                       env=env_https_only, capture_output=True, timeout=15)

                ok = (res_p.returncode == 0 and MARKER in res_p.stdout and
                      res_d.returncode == 0 and MARKER in res_d.stdout and
                      len(proxy_recs) == 1 and
                      proxy_recs[0]['request_line'] == f'GET {http_url} HTTP/1.1' and
                      proxy_recs[0]['headers'].get('host') == f'127.0.0.1:{http_port}' and
                      len(http_reqs) == 2 and
                      http_reqs[0].startswith(b'GET /hello?q=1 HTTP/1.1\r\n'))
                record_proxy_case(
                    'HTTP_PROXY uses absolute-form for http:// while HTTPS_PROXY leaves http:// direct',
                    ok,
                    f'proxy_recs={proxy_recs}, direct_reqs={len(http_reqs)}')

        print(f'{len(cases) + proxy_cases} HTTPS cases; {failures} failure(s).')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
