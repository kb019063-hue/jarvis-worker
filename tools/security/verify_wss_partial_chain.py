#!/usr/bin/env python3
"""Loopback WSS partial-chain verification; no public network or CA changes.

Python's ssl is a second client of the same OpenSSL verification library,
not an independent TLS implementation. --node adds a Node >=22.9 TLS client
comparison, also OpenSSL-based; only Litebird performs the WebSocket upgrade.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile

from verify_https import chain_file, issued, root_certificate
from verify_websocket import (DualProtocolServer, HttpConnectProxy, make_server_ssl_ctx,
                              parse_checks, run_cli)

NODE = r'''
const tls = require('tls'), fs = require('fs');
let host = process.argv[3], options = {host, port:+process.argv[1],
  ca:fs.readFileSync(process.argv[2]), allowPartialTrustChain:true};
if (host !== '127.0.0.1') options.servername = host;
let s = tls.connect(options,
  () => {console.log('OK'); s.end();});
s.on('error', e => console.log(e.code));
'''


def reference(port: int, trust: Path, host: str) -> str:
    ctx = ssl.create_default_context(cafile=str(trust))
    ctx.verify_flags |= ssl.VERIFY_X509_PARTIAL_CHAIN
    try:
        with socket.create_connection(('127.0.0.1', port), timeout=5) as raw:
            with ctx.wrap_socket(raw, server_hostname=host):
                return 'OK'
    except ssl.SSLCertVerificationError as exc:
        return str(exc.verify_code)


def run(binary: str, timeout: float = 30.0, node: bool = False) -> tuple[int, int]:
    failures = 0
    with tempfile.TemporaryDirectory(prefix='litebird-wss-chain-') as tmp:
        root = Path(tmp)
        ca = root_certificate(root, 'root')
        inter = issued(root, 'inter', 'root', *ca, is_ca=True)
        exp_inter = issued(root, 'exp-inter', 'root', *ca, is_ca=True,
                           startdate='20200101000000Z', enddate='20200102000000Z')
        leaf = issued(root, 'leaf', 'inter', *inter, san='DNS:localhost')
        ip_leaf = issued(root, 'ip-leaf', 'inter', *inter, san='IP:127.0.0.1')
        wrong = issued(root, 'wrong', 'root', *ca, san='DNS:wrong.invalid')
        expired = issued(root, 'expired', 'inter', *inter, san='DNS:localhost',
                         startdate='20200101000000Z', enddate='20200102000000Z')
        exp_anchor_leaf = issued(root, 'exp-anchor-leaf', 'exp-inter', *exp_inter,
                                 san='DNS:localhost')
        chain = chain_file(root, 'chain', [leaf[0], inter[0]])
        # name, keypair, server chain, trust file, expected code, proxy, Node error
        cases = [
            ('trusted intermediate', leaf, leaf[0], inter[0], None, False, 'OK'),
            ('server-sent intermediate', leaf, chain, inter[0], None, False, 'OK'),
            ('trusted leaf', leaf, leaf[0], leaf[0], None, False, 'OK'),
            ('IP trusted leaf', ip_leaf, ip_leaf[0], ip_leaf[0], None, False, 'OK'),
            ('IP wrong identity', leaf, leaf[0], leaf[0], 64, False, 'ERR_TLS_CERT_ALTNAME_INVALID'),
            ('wrong identity on trusted leaf', wrong, wrong[0], wrong[0], 62, False, 'ERR_TLS_CERT_ALTNAME_INVALID'),
            ('expired leaf', expired, expired[0], inter[0], 10, False, 'CERT_HAS_EXPIRED'),
            ('expired intermediate', exp_anchor_leaf, exp_anchor_leaf[0], exp_inter[0], 10, False, 'CERT_HAS_EXPIRED'),
            ('untrusted intermediate', leaf, leaf[0], ca[0], 20, False, 'UNABLE_TO_GET_ISSUER_CERT_LOCALLY'),
            ('trusted intermediate through CONNECT', leaf, leaf[0], inter[0], None, True, 'OK'),
            ('wrong identity through CONNECT', wrong, wrong[0], wrong[0], 62, True, 'ERR_TLS_CERT_ALTNAME_INVALID'),
        ]
        plain = DualProtocolServer(name='yield')
        proxy = HttpConnectProxy()
        try:
            for index, (label, pair, cert, trust, code, tunneled, node_expected) in enumerate(cases):
                peer = DualProtocolServer(make_server_ssl_ctx(cert, pair[1]), name=label)
                try:
                    host = '127.0.0.1' if label.startswith('IP ') else 'localhost'
                    ref = reference(peer.port, trust, host)
                    node_result = None
                    if node:
                        result = subprocess.run(['node', '-e', NODE, str(peer.port), str(trust), host],
                                                capture_output=True, text=True, timeout=timeout, check=True)
                        node_result = result.stdout.strip()
                    with peer.lock:
                        peer.upgrades.clear()
                    with proxy.lock:
                        proxy.requests.clear()
                    page = root / f'case-{index}.html'
                    page.write_text(f'''<!doctype html><script>
var seq = [], pending = 1, tries = 0;
var s = new WebSocket('wss://{host}:{peer.port}/partial');
s.onopen = function() {{ seq.push('open'); s.send('hello'); }};
s.onmessage = function(e) {{ seq.push(e.data); s.close(1000); }};
s.onerror = function(e) {{ seq.push('err[' + e.message + ']'); }};
s.onclose = function(e) {{ seq.push('close:' + e.code + ':' + e.wasClean); console.log('WSCHECK:result=' + seq.join(',')); pending--; }};
function pump() {{ if (!pending || tries++ >= 120) {{ console.log('WSCHECK:done=' + (!pending)); return; }} fetch('http://127.0.0.1:{plain.port}/yield').then(pump, pump); }}
setTimeout(pump, 1);
</script>''', encoding='utf-8')
                    env = {key: '' for key in ('HTTPS_PROXY', 'https_proxy', 'ALL_PROXY', 'all_proxy', 'HTTP_PROXY', 'http_proxy', 'NO_PROXY', 'no_proxy')}
                    env['LITEBIRD_SSL_CA_FILE'] = str(trust)
                    if tunneled:
                        env['HTTPS_PROXY'] = proxy.url
                    proc = run_cli(str(Path(binary).resolve()), [str(page), f'--url={page.as_uri()}', '--virtual-time-budget=2000'], timeout, env)
                    record = parse_checks(proc.stdout + '\n' + proc.stderr)
                    observed = record.get('result', '')
                    expected_ref = 'OK' if code is None else str(code)
                    with peer.lock:
                        upgrades = list(peer.upgrades)
                    with proxy.lock:
                        requests = list(proxy.requests)
                    if code is None:
                        valid = observed == 'open,echo:hello,close:1000:true' and len(upgrades) == 1
                    else:
                        valid = (observed.startswith(f'err[TLS certificate verification failed (code {code},')
                                 and observed.endswith('],close:1006:false') and not upgrades)
                    valid = (valid and proc.returncode == 0 and record.get('done') == 'true'
                             and ref == expected_ref and (not node or node_result == node_expected)
                             and ((len(requests) == 1 and requests[0]['status'] == 200) if tunneled else not requests))
                    failures += not valid
                    print(f'{"PASS" if valid else "FAIL"} WSS partial chain: {label}' +
                          (f' (Python={ref}, Node={node_result}, Litebird={observed}, done={record.get("done")}, '
                           f'upgrades={len(upgrades)}, proxy={requests}, rc={proc.returncode})' if not valid else ''))
                finally:
                    peer.stop()
        finally:
            proxy.stop()
            plain.stop()
    return len(cases), failures


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', default='build/litebird')
    ap.add_argument('--timeout', type=float, default=30.0)
    ap.add_argument('--node', action='store_true')
    args = ap.parse_args()
    checks, failures = run(args.binary, args.timeout, args.node)
    print(f'{checks} WSS partial-chain cases; {failures} failure(s).')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
