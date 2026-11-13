#!/usr/bin/env python3
"""Runs the web-platform-tests URL corpus (tests/url/) against Litebird.

Every case goes through the script API a page uses: `new URL(input, base)`
and the setters of the object it returns. See tests/url/README.md.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

SCRIPT = r"""
var cases = %s;
var setters = %s;
var fields = ['href', 'protocol', 'username', 'password', 'host', 'hostname', 'port', 'pathname', 'search', 'hash'];
var result = { parse: { passed: 0, total: 0 }, setters: { passed: 0, total: 0 }, failures: [] };
function show(value) { return JSON.stringify(value); }

cases.forEach(function (c) {
  if (typeof c === 'string') return;            // a comment
  result.parse.total++;
  var base = c.base !== null && c.base !== undefined ? c.base : undefined;
  var what = show(c.input) + (base !== undefined ? ' against ' + show(base) : '');
  try {
    if (c.failure) {
      var threw = false;
      try { new URL(c.input, base); } catch (e) { threw = e instanceof TypeError; }
      if (!threw) { result.failures.push(what + ': expected a failure, got ' + show(new URL(c.input, base).href)); return; }
      if (URL.canParse(c.input, base)) { result.failures.push(what + ': URL.canParse says yes'); return; }
      result.parse.passed++;
      return;
    }
    var url = new URL(c.input, base);
    var wrong = [];
    fields.forEach(function (f) { if (url[f] !== c[f]) wrong.push(f + ' ' + show(url[f]) + ', expected ' + show(c[f])); });
    if ('origin' in c && url.origin !== c.origin) wrong.push('origin ' + show(url.origin) + ', expected ' + show(c.origin));
    if ('searchParams' in c && url.searchParams.toString() !== c.searchParams) {
      wrong.push('searchParams ' + show(url.searchParams.toString()) + ', expected ' + show(c.searchParams));
    }
    if (!URL.canParse(c.input, base)) wrong.push('URL.canParse says no');
    if (wrong.length) result.failures.push(what + ': ' + wrong.join('; ')); else result.parse.passed++;
  } catch (e) { result.failures.push(what + ': threw ' + e); }
});

Object.keys(setters).forEach(function (part) {
  if (part === 'comment') return;
  setters[part].forEach(function (c) {
    result.setters.total++;
    var what = 'set ' + part + ' of ' + show(c.href) + ' to ' + show(c.new_value);
    try {
      var url = new URL(c.href);
      url[part] = c.new_value;
      var wrong = [];
      Object.keys(c.expected).forEach(function (f) {
        if (url[f] !== c.expected[f]) wrong.push(f + ' ' + show(url[f]) + ', expected ' + show(c.expected[f]));
      });
      if (wrong.length) result.failures.push(what + ': ' + wrong.join('; ')); else result.setters.passed++;
    } catch (e) { result.failures.push(what + ': threw ' + e); }
  });
});
console.log('RESULT ' + JSON.stringify(result));
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--binary', default=os.path.join(ROOT, 'build', 'litebird'))
    parser.add_argument('--corpus', default=os.path.join(ROOT, 'tests', 'url'))
    parser.add_argument('--show', type=int, default=0, metavar='N', help='print the first N failures')
    parser.add_argument('--min-pass', type=int, default=0, metavar='N', help='exit 1 when fewer than N cases pass')
    parser.add_argument('--json', action='store_true', help='print the result as JSON')
    parser.add_argument('--timeout', type=float, default=120.0)
    args = parser.parse_args()

    with open(os.path.join(args.corpus, 'urltestdata.json'), encoding='utf-8') as f:
        cases = json.load(f)
    with open(os.path.join(args.corpus, 'setters_tests.json'), encoding='utf-8') as f:
        setters = json.load(f)

    with tempfile.TemporaryDirectory() as work:
        script = os.path.join(work, 'run.js')
        page = os.path.join(work, 'page.html')
        with open(script, 'w', encoding='utf-8') as f:
            # JSON is JavaScript: the two files are the script's data as they are.
            f.write(SCRIPT % (json.dumps(cases), json.dumps(setters)))
        with open(page, 'w', encoding='utf-8') as f:
            f.write('<!DOCTYPE html><title>URL conformance</title>')
        try:
            run = subprocess.run([args.binary, '--run-script', script, page], capture_output=True,
                                 timeout=args.timeout)
        except subprocess.TimeoutExpired:
            print('URL conformance: the run did not finish in %.0f s' % args.timeout)
            return 1
    out = run.stdout.decode('utf-8', 'replace')
    lines = [line for line in out.splitlines() if line.startswith('RESULT ')]
    if not lines:
        print('URL conformance: no result (exit code %d)' % run.returncode)
        print(out[-2000:])
        print(run.stderr.decode('utf-8', 'replace')[-2000:])
        return 1
    result = json.loads(lines[-1][len('RESULT '):])
    passed = result['parse']['passed'] + result['setters']['passed']
    total = result['parse']['total'] + result['setters']['total']
    if args.json:
        print(json.dumps(dict(result, passed=passed, total=total), indent=2))
    else:
        if args.show:
            print('First %d of %d failures:' % (min(args.show, len(result['failures'])), len(result['failures'])))
            for failure in result['failures'][:args.show]:
                print('  ' + failure)
            print()
        print('URL parsing (new URL):  %d/%d' % (result['parse']['passed'], result['parse']['total']))
        print('URL setters:            %d/%d' % (result['setters']['passed'], result['setters']['total']))
        print('URL conformance, total: %d/%d (%.1f%%)' % (passed, total, 100.0 * passed / max(total, 1)))
    if passed < args.min_pass:
        print('FAILED: %d passed, the floor is %d' % (passed, args.min_pass))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
