#!/usr/bin/env python3
"""Compare path-bearing reference resolution with Node's WHATWG URL."""
import argparse
import json
from pathlib import Path
import subprocess

BASES = [
    'http://example.test/dir/page.html',
    'https://example.test/dir/page.html?x=/old/path#fragment/part',
    'https://example.test',
    'http://example.test:80/a//b/',
    'file:///tmp/a%20b/page.html?old#fragment',
    'https://user:pw@example.test:8443/a/b',
]
REFERENCES = [
    'other.html', './other.html', '../other.html', '../../up', 'sub/page.html',
    'sub/', './', '../', '/absolute/path', '/a/../end', '/a//b/',
    '//cdn.example.test/file.js', '//cdn.example.test:8080/lib.js',
    'other.html?x=/value/../x#part', 'sub/a//b?x=a//b',
    'a%20b.html', 'space here.html', './a:b/c', '%2e%2e/up',
    'one/./two/../three', '\\absolute\\path', 'file.js#fragment/../z',
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/relative_navigation_fixture')
    args = parser.parse_args()
    pairs = [(base, ref) for base in BASES for ref in REFERENCES]
    expected = json.loads(subprocess.run(
        ['node', '-e', 'const fs=require("fs"); const p=JSON.parse(fs.readFileSync(0,"utf8")); process.stdout.write(JSON.stringify(p.map(([b,r])=>{try{return new URL(r,b).href}catch{return null}})))'],
        input=json.dumps(pairs), text=True, capture_output=True, check=True).stdout)
    failures = 0
    for (base, ref), want in zip(pairs, expected):
        result = subprocess.run([str(Path(args.binary).resolve()), base, ref],
                                text=True, capture_output=True, timeout=10)
        # The string-valued resolver signals an invalid path-bearing
        # reference against a valid hierarchical base with an empty string.
        expected_value = '' if want is None else want
        if result.returncode or result.stdout != expected_value:
            failures += 1
            print(f'FAIL {base!r} + {ref!r}: got {result.stdout!r}, expected {expected_value!r}, exit {result.returncode}')
    print(f'Path reference oracle: {len(pairs) - failures}/{len(pairs)} agree with Node URL')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
