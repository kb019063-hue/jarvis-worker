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
import os
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile
import threading

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


def oracle(port: int, host: str, trust: Path) -> bool:
    ctx = ssl.create_default_context(cafile=str(trust))
    try:
        with socket.create_connection((host, port), timeout=3) as raw:
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


def url_for(host: str, port: int) -> str:
    """An IPv6 literal is bracketed in a URL and is not."""
    return f'https://[{host}]:{port}/' if ':' in host else f'https://{host}:{port}/'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/litebird')
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve())
    failures = 0
    with tempfile.TemporaryDirectory(prefix='litebird-https-') as tmp:
        root = Path(tmp)
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
             True, None, ['localhost']),
            ('trusted IP', identities['ip'][0], identities['ip'][1], '127.0.0.1', root_ca[0],
             True, None, []),
            ('wrong DNS (CN matches, SAN does not)', identities['wrong'][0], identities['wrong'][1],
             'localhost', root_ca[0], False, None, None),
            ('wrong IP', identities['wrong'][0], identities['wrong'][1], '127.0.0.1', root_ca[0],
             False, None, None),
            ('DNS SAN is not an IP SAN', identities['dns-ip'][0], identities['dns-ip'][1],
             '127.0.0.1', root_ca[0], False, None, None),
            ('DNS-only certificate on IP URL', identities['dns'][0], identities['dns'][1],
             '127.0.0.1', root_ca[0], False, None, None),
            ('untrusted DNS', identities['dns'][0], identities['dns'][1], 'localhost',
             identities['wrong'][0], False, None, None),
            ('untrusted IP', identities['ip'][0], identities['ip'][1], '127.0.0.1',
             identities['wrong'][0], False, None, None),
            # Chains: a leaf that is not signed by the trust anchor is only accepted
            # when the server hands over the intermediate that signed it.
            ('trusted chain', served['chained'], identities['chained'][1], 'localhost', root_ca[0],
             True, None, None),
            ('chain with the intermediate missing', identities['chained'][0],
             identities['chained'][1], 'localhost', root_ca[0], False, None, None),
            ('intermediate marked not a CA', served['bad-issuer'], identities['bad-issuer'][1],
             'localhost', root_ca[0], False, None, None),
            # Dates: the leaf, and separately the intermediate, outside its validity.
            ('expired certificate', served['expired'], identities['expired'][1], 'localhost',
             root_ca[0], False, None, None),
            ('certificate not valid yet', served['future'], identities['future'][1], 'localhost',
             root_ca[0], False, None, None),
            ('expired intermediate', served['expired-issuer'], identities['expired-issuer'][1],
             'localhost', root_ca[0], False, None, None),
            # IPv6: the literal is an address, so an iPAddress SAN is what has to match,
            # and an address of the other family is a different identity.
            ('trusted IPv6 literal', served['chained'], identities['chained'][1], '::1',
             root_ca[0], True, None, []),
            ('expanded IPv6 literal', served['chained'], identities['chained'][1],
             '0:0:0:0:0:0:0:1', root_ca[0], True, None, []),
            ('IPv4 SAN on an IPv6 URL', served['v4-only'], identities['v4-only'][1], '::1',
             root_ca[0], False, None, None),
            ('IPv6 SAN on an IPv4 URL', served['v6-only'], identities['v6-only'][1], '127.0.0.1',
             root_ca[0], False, None, None),
            # SNI: a name is sent for a DNS host and never for a literal, so the
            # virtual host answers `localhost` and refuses the other name.
            ('virtual host chosen by SNI', identities['default'][0], identities['default'][1],
             'localhost', root_ca[0], True, virtual, ['localhost']),
            ('literal URL gets no name and the default certificate', identities['default'][0],
             identities['default'][1], '127.0.0.1', root_ca[0], False, virtual, []),
        ]
        for label, certificates, key, host, trust, expected, virtual, names in cases:
            address = '::1' if ':' in host else '127.0.0.1'
            with server(certificates, key, address, virtual) as (port, _, _):
                reference = oracle(port, host, trust)
            with server(certificates, key, address, virtual) as (port, requests, seen):
                env = os.environ.copy()
                env['LITEBIRD_SSL_CA_FILE'] = str(trust)
                result = subprocess.run([binary, '--dump-dom', url_for(host, port)],
                                        env=env, capture_output=True, timeout=15)
            accepted = result.returncode == 0 and MARKER in result.stdout
            rejected = (result.returncode == 1 and not requests and
                        b'TLS handshake or certificate verification failed' in result.stderr)
            sent = seen if names is not None else None
            ok = reference == expected and (accepted if expected else rejected)
            ok = ok and (names is None or sent == names)
            failures += not ok
            detail = f'Python={reference}, Litebird={accepted}'
            if names is not None:
                detail += f', SNI={sent}'
            print(f'{"PASS" if ok else "FAIL"} {label}: {detail}')
            if not ok:
                print(result.stderr.decode(errors='replace')[:2000])
        print(f'{len(cases)} HTTPS cases; {failures} failure(s).')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
