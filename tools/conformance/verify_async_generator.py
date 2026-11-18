#!/usr/bin/env python3
"""Compare generator delegation with Node. Run: python3 tools/conformance/verify_async_generator.py."""
import argparse
import subprocess
import sys

CASES = {
    "async delegate and sync fallback": r"""
(async () => {
  async function* inner() { yield 7; return 9; }
  async function* outer() { let result = yield* inner(); yield result; yield* [Promise.resolve(3)]; }
  let g = outer();
  for (let i = 0; i < 4; i++) { let r = await g.next(); console.log(r.value, r.done); }
})();
""",
    "return through finally": r"""
(async () => {
  async function* inner() { try { yield 1; } finally { yield 2; } }
  async function* outer() { yield* inner(); }
  let g = outer();
  for (let r of [await g.next(), await g.return(5), await g.next()]) console.log(r.value, r.done);
})();
""",
    "direct yield rejection is caught": r"""
(async () => {
  async function* g() { try { yield Promise.reject('bad'); } catch(e) { yield 'caught:' + e; } }
  let r = await g().next(); console.log(r.value, r.done);
})();
""",
    "sync yield star retains result object and lazy value": r"""
let reads = 0;
let step = {done:false}; Object.defineProperty(step, 'value', {get() { reads++; return 4; }});
let it = { [Symbol.iterator]() { return this; }, next() { return step; } };
let g = (function*(){ yield* it; })(); let result = g.next();
console.log(result === step, reads, result.value, reads);
""",
    "async iterator value remains untouched": r"""
(async () => {
  let calls = 0; let value = {then() { calls++; }};
  let it = { [Symbol.asyncIterator]() { return this; }, next() { return Promise.resolve({value, done:false}); } };
  let g = (async function*(){ yield* it; })(); let result = await g.next();
  console.log(result.value === value, calls, result.done);
})();
""",
    "missing throw propagates close errors": r"""
let marker = new Error('close');
let it = { [Symbol.iterator]() { return this; }, next() { return {value:1, done:false}; }, return() { throw marker; } };
let g = (function*(){ yield* it; })(); g.next();
try { g.throw('bad'); } catch(e) { console.log(e === marker); }
""",
    "invalid iterator methods and sync result": r"""
(async () => {
  let sync = { [Symbol.iterator]() { return { next() { return 42; } }; } };
  try { await (async function*(){ yield* sync; })().next(); } catch(e) { console.log('primitive', e.name); }
  let calls = 0;
  let it = { [Symbol.iterator]() { return this; }, next() { return {value:1, done:false}; }, throw:1, return() { calls++; return {}; } };
  let g = (function*(){ yield* it; })(); g.next();
  try { g.throw('x'); } catch(e) { console.log('throw', e.name, calls); }
  it.return = 1; g = (function*(){ yield* it; })(); g.next();
  try { g.return(3); } catch(e) { console.log('return', e.name); }
})();
""",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/litebird')
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    failures = 0
    for name, source in CASES.items():
        try:
            node = subprocess.run([args.node, '-e', source], capture_output=True, text=True, timeout=10)
            bird = subprocess.run([args.binary, '--run-js', source], capture_output=True, text=True, timeout=10)
        except (OSError, subprocess.TimeoutExpired) as error:
            print(f'FAIL {name}: {error}')
            failures += 1
            continue
        if node.returncode or bird.returncode or node.stdout != bird.stdout:
            print(f'FAIL {name}\n  node ({node.returncode}): {node.stdout!r} {node.stderr!r}'
                  f'\n  litebird ({bird.returncode}): {bird.stdout!r} {bird.stderr!r}')
            failures += 1
        else:
            print(f'PASS {name}')
    print(f'{len(CASES) - failures}/{len(CASES)} match Node')
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
