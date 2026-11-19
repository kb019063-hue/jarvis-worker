#!/usr/bin/env python3
"""Local TLS identity oracle; ephemeral certificates, no public network or CA changes."""
from __future__ import annotations

import argparse
import contextlib
import os
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile
import threading

MARKER = b"LITEBIRD_TLS_VERIFIED"


def certificate(root: Path, name: str, san: str) -> tuple[Path, Path]:
    cert, key = root / (name + '.crt'), root / (name + '.key')
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'ec', '-pkeyopt',
                    'ec_paramgen_curve:prime256v1', '-nodes', '-keyout', str(key),
                    '-out', str(cert), '-days', '2', '-subj', '/CN=localhost',
                    '-addext', 'subjectAltName=' + san], check=True,
                   capture_output=True, timeout=20)
    return cert, key


@contextlib.contextmanager
def server(cert: Path, key: Path):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    requests = []
    errors = []
    stop = threading.Event()
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen()
    listener.settimeout(0.2)
    port = listener.getsockname()[1]

    def serve():
        while not stop.is_set():
            try:
                conn, _ = listener.accept()
            except socket.timeout:
                continue
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
        yield port, requests
    finally:
        stop.set()
        thread.join(timeout=5)
        listener.close()
        if thread.is_alive() or errors:
            raise RuntimeError(f'TLS fixture failed: {errors}')


def oracle(port: int, host: str, trust: Path) -> bool:
    ctx = ssl.create_default_context(cafile=str(trust))
    try:
        with socket.create_connection(('127.0.0.1', port), timeout=3) as raw:
            with ctx.wrap_socket(raw, server_hostname=host) as tls:
                tls.sendall(b'GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')
                response = b''
                while chunk := tls.recv(4096):
                    response += chunk
                if MARKER not in response:
                    raise RuntimeError('Oracle received no fixture body')
                return True
    except ssl.SSLCertVerificationError:
        return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/litebird')
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve())
    failures = 0
    with tempfile.TemporaryDirectory(prefix='litebird-https-') as tmp:
        root = Path(tmp)
        identities = {
            'dns': certificate(root, 'dns', 'DNS:localhost'),
            'ip': certificate(root, 'ip', 'IP:127.0.0.1'),
            'wrong': certificate(root, 'wrong', 'DNS:wrong.invalid,IP:127.0.0.2'),
            'dns-ip': certificate(root, 'dns-ip', 'DNS:127.0.0.1'),
        }
        cases = [
            ('trusted DNS', 'dns', 'localhost', 'dns', True),
            ('trusted IP', 'ip', '127.0.0.1', 'ip', True),
            ('wrong DNS (CN matches, SAN does not)', 'wrong', 'localhost', 'wrong', False),
            ('wrong IP', 'wrong', '127.0.0.1', 'wrong', False),
            ('DNS SAN is not an IP SAN', 'dns-ip', '127.0.0.1', 'dns-ip', False),
            ('DNS-only certificate on IP URL', 'dns', '127.0.0.1', 'dns', False),
            ('untrusted DNS', 'dns', 'localhost', 'wrong', False),
            ('untrusted IP', 'ip', '127.0.0.1', 'wrong', False),
        ]
        for label, identity, host, trusted, expected in cases:
            cert, key = identities[identity]
            trust = identities[trusted][0]
            with server(cert, key) as (port, _):
                reference = oracle(port, host, trust)
            with server(cert, key) as (port, requests):
                env = os.environ.copy()
                env['LITEBIRD_SSL_CA_FILE'] = str(trust)
                result = subprocess.run([binary, '--dump-dom', f'https://{host}:{port}/'],
                                        env=env, capture_output=True, timeout=15)
            accepted = result.returncode == 0 and MARKER in result.stdout
            rejected = (result.returncode == 1 and not requests and
                        b'TLS handshake or certificate verification failed' in result.stderr)
            ok = reference == expected and (accepted if expected else rejected)
            failures += not ok
            print(f'{"PASS" if ok else "FAIL"} {label}: Python={reference}, Litebird={accepted}')
            if not ok:
                print(result.stderr.decode(errors='replace')[:2000])
        print(f'{len(cases)} HTTPS cases; {failures} failure(s).')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
