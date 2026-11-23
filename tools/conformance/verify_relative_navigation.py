#!/usr/bin/env python3
"""Compare pathless URL references with Node's WHATWG URL implementation."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASES = [
    'http://snapshot.test/url-params/index.html',
    'https://example.test/dir/file.html?old=/a/b#old?fragment',
    'https://example.test',
    'http://example.test:80/a%20b/',
    'file:///tmp/a%20b.html?old#fragment',
]
REFERENCES = ['', '?', '?x=1', '?firstName=Ayse&food=rice%20pudding',
              '?new=/a/b?c#part', '#', '#id', '#part?query', '#a/b', '?x=hello world']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/relative_navigation_fixture')
    args = parser.parse_args()
    pairs = [(base, ref) for base in BASES for ref in REFERENCES]
    expected = json.loads(subprocess.run(
        ['node', '-e', 'const fs=require("fs"); const p=JSON.parse(fs.readFileSync(0,"utf8")); process.stdout.write(JSON.stringify(p.map(([b,r])=>new URL(r,b).href)))'],
        input=json.dumps(pairs), text=True, capture_output=True, check=True).stdout)
    failures = 0
    for (base, ref), want in zip(pairs, expected):
        result = subprocess.run([str(Path(args.binary).resolve()), base, ref],
                                text=True, capture_output=True, timeout=10)
        if result.returncode or result.stdout != want:
            failures += 1
            print(f'FAIL {base!r} + {ref!r}: got {result.stdout!r}, expected {want!r}, exit {result.returncode}')
    print(f'Pathless reference oracle: {len(pairs) - failures}/{len(pairs)} agree with Node URL')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
