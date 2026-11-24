#!/usr/bin/env python3
"""Compare six partial-chain HTTPS decisions with Node.js >=22.9, no public network.

Both clients use an explicitly supplied CA file. Node's allowPartialTrustChain
is deliberately enabled; Node and Litebird both use OpenSSL, so this checks an
independent client implementation, not an independent X.509 library.
"""
import os
from pathlib import Path
import subprocess
import tempfile

from verify_https import (MARKER, chain_file, issued, root_certificate, server,
                          url_for, engine_reason)

NODE = r'''
const https = require('https'), fs = require('fs');
https.get(process.argv[1], {ca: fs.readFileSync(process.argv[2]), allowPartialTrustChain: true},
  res => {let text = ''; res.on('data', b => text += b); res.on('end', () => {
    console.log(text.includes('LITEBIRD_TLS_VERIFIED') ? 'OK' : 'BAD_BODY');
  });}).on('error', e => console.log(e.code));
'''


def main():
    binary = Path('build/litebird').resolve()
    failures = 0
    with tempfile.TemporaryDirectory(prefix='litebird-https-node-') as temp:
        root = Path(temp)
        ca = root_certificate(root, 'root')
        inter = issued(root, 'inter', 'root', *ca, is_ca=True)
        expired_inter = issued(root, 'expired-inter', 'root', *ca, is_ca=True,
                               startdate='20200101000000Z', enddate='20200102000000Z')
        leaf = issued(root, 'leaf', 'inter', *inter, san='DNS:localhost')
        wrong = issued(root, 'wrong', 'root', *ca, san='DNS:wrong.invalid')
        expired = issued(root, 'expired', 'inter', *inter, san='DNS:localhost',
                         startdate='20200101000000Z', enddate='20200102000000Z')
        expired_issuer_leaf = issued(root, 'expired-issuer-leaf', 'expired-inter',
                                    *expired_inter, san='DNS:localhost')
        chain = chain_file(root, 'chain', [leaf[0], inter[0]])
        cases = [
            ('intermediate alone', leaf, leaf[0], inter[0], 'OK', None),
            ('intermediate sent', leaf, chain, inter[0], 'OK', None),
            ('direct leaf', leaf, leaf[0], leaf[0], 'OK', None),
            ('wrong identity', wrong, wrong[0], wrong[0], 'ERR_TLS_CERT_ALTNAME_INVALID', 62),
            ('expired leaf', expired, expired[0], inter[0], 'CERT_HAS_EXPIRED', 10),
            ('expired anchor', expired_issuer_leaf, expired_issuer_leaf[0], expired_inter[0],
             'CERT_HAS_EXPIRED', 10),
        ]
        for name, pair, cert, trust, expected, code in cases:
            with server(cert, pair[1], '127.0.0.1') as (port, _, _):
                reference = subprocess.run(['node', '-e', NODE, url_for('localhost', port), str(trust)],
                                           capture_output=True, text=True, timeout=15, check=True).stdout.strip()
            with server(cert, pair[1], '127.0.0.1') as (port, requests, _):
                env = dict(os.environ, LITEBIRD_SSL_CA_FILE=str(trust))
                result = subprocess.run([str(binary), '--dump-dom', url_for('localhost', port)],
                                        capture_output=True, env=env, timeout=15)
            error = engine_reason(result.stderr)
            engine_ok = (result.returncode == 0 and MARKER in result.stdout) if code is None else (
                result.returncode == 1 and not requests and error is not None and error[0] == code)
            ok = reference == expected and engine_ok
            failures += not ok
            verdict = 'loaded' if result.returncode == 0 and MARKER in result.stdout else str(error)
            print(f'{"PASS" if ok else "FAIL"} {name}: Node={reference}, Litebird={verdict}')
    print(f'{len(cases)} Node comparisons; {failures} failure(s)')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
