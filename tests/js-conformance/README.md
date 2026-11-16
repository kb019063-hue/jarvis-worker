# JavaScript conformance corpus

Small scripts with the exact output a real engine produces. This is the
JavaScript counterpart of `tests/html5lib/tree-construction/`: a scoreboard
that measures the engine against something other than its own tests.

```bash
make js-conformance                                   # per-file pass rate
python3 tools/conformance/run_js_conformance.py --show 20 --filter strings
python3 tools/conformance/run_js_conformance.py --reference "node -e"   # check the corpus itself
```

## Where the expectations come from

Every `#expect` section was produced by running the `#code` under Node.js
(V8), not under Litebird. `--reference "node -e"` re-runs the whole corpus
under Node and fails on any mismatch, so an expectation cannot be bent to
match what Litebird happens to print. CI runs that check.

The runner sets `TZ=UTC` for both engines so date cases are stable.

## Files

| File | What it covers |
| :--- | :--- |
| `everyday-es5.dat`, `everyday-es2015.dat` | The fixed 90-case probe the headline number is reported on. **Do not edit these two files**; add cases elsewhere. |
| `lexical.dat`, `operators.dat`, `control-flow.dat` | Core syntax and operator semantics |
| `functions.dat`, `objects.dat`, `arrays.dat`, `classes.dat` | The object model |
| `strings.dat`, `numbers-math.dat`, `json.dat`, `regexp.dat`, `date.dat` | Built-in libraries |
| `errors.dat`, `collections-iterators.dat`, `async.dat`, `misc.dat` | Exceptions, Map/Set/generators, promises and timers |
| `programs.dat` | 20-40 line programs that combine features the way real scripts do |
| `deep-nesting.dat` | Input nested or chained far deeper than the native stack: 200,000 nested parentheses, blocks or functions, a million-node linked list, a 100,000-link promise chain. Too deep must be a `RangeError` the script can catch, never the end of the process. Engines differ on where "too deep" begins, so these cases accept either a result or a `RangeError` and print which invariant held |
| `long-strings.dat` | Strings of 100,000 characters and more: read one character at a time, built with `+=`, eaten from the front by a tokenizer, searched by a regular expression. Each script does work proportional to its input, so it takes milliseconds unless reading, appending or slicing a string costs its whole length, in which case it takes minutes and the 10 s limit ends it. The rest of the file is what sharing text must not change: every earlier value of a string that was appended to, the two halves of a surrogate pair meeting, order by code unit, `RangeError` for a string too long to exist |
| `gc-safety.dat` | Scripts that run a collection in the middle of everything: inside callbacks, half-built expressions, loops, suspended generators and async functions. The result must not change |
| `prototypes.dat` | A function in a prototype chain: the parent of a class (`Object.getPrototypeOf(Derived) === Base`, static members through any number of parents, `super()` after the parent was replaced), the prototype of an object (`Object.create(fn)`), `Function.prototype` itself, the error types, `Reflect.construct` with a third argument, what `Object.defineProperty` allows on a property that cannot be configured, and three whole programs as Babel and TypeScript compile classes for older browsers. Eleven cases collect in the middle and check that a class reachable only as a prototype keeps what it closed over. Nine cases fail and were recorded to: what classes still lack (ARCHITECTURE.md, Phase 34, section 9) |

## Beside the corpus: a grid compared with Node

The corpus holds scripts somebody thought of. `tools/conformance/diff_strings.py`
checks strings the other way: it sends 36 subject strings (ASCII, accented,
CJK, characters outside the BMP, lone halves of a surrogate pair, text long
enough to cross the engine's internal boundaries) through every string method,
the operators, the URI and JSON functions and 51 regular expressions, in
Litebird and in Node, and lists every result that differs.

```bash
python3 tools/conformance/diff_strings.py              # 50,580 results; the summary and the first 20 differences
python3 tools/conformance/diff_strings.py --only split # only expressions that contain "split"
```

It has no expectations to maintain, since Node answers at run time, so it is
not part of `make js-conformance`. When it finds something a page could meet,
that becomes a corpus case.

## Beside the corpus: test262, for the difference between two builds

The corpus was written alongside the engine. test262, the suite of the
language's standard, was not, and `tools/conformance/run_test262.py` runs
directories of it (downloaded the first time, at a pinned commit, into
`build/test262`; nothing of it is in the repository).

```bash
make test262                       # 21 directories on functions, classes and objects: 15,686 tests, 12,275 pass
python3 tools/conformance/run_test262.py --binary before=/tmp/litebird-main --binary now=build/litebird
python3 tools/conformance/run_test262.py --show 20 language/expressions/super
```

It is not a scoreboard and has no floor: 78.3% of those directories pass
(Phase 44 / finding 50 added Unicode 17.0 identifier validation and `\uXXXX` /
`\u{...}` identifier escape handling, moving the 21 default directories from
10,835 to 12,275 of 15,686 with 0 regressions, and moving `language/identifiers`,
`keywords`, `future-reserved-words` and `reserved-words` to 374/374 = 100.0%).
Of the whole suite, ~27,741 of 47,967 pass (`run_test262.py --timeout 4
built-ins language annexB`, a quarter of an hour for two builds). What it is for
is the second form: build `main` somewhere (`git worktree add /tmp/main origin/main`,
`make` there), give the runner both binaries, and it lists every test that passes
in the first and not in the second and exits 1 if there is one. A change to how
functions, classes or properties work should be run through it before it is
called done: Phase 34 passed every case it had written for itself and still
failed two tests that `main` passed. Both became corpus cases.

The runner says in its first lines how a test is run (strict or not, the
harness files, negative and async tests) and what it leaves out (`module`
tests; `$262`, so nothing that needs another realm passes).

## Format

```
#name <short title, unique within the file>
#code
<one or more lines of JavaScript>
#expect
<exact standard output>
```

A case starts at `#name`. Blank lines inside a section belong to it; cases
may be separated by a blank line. A case passes when the engine prints exactly
the expected output and exits 0.

## Rules for new cases

1. Print only primitives. `console.log` of an object or array is formatted
   differently by every engine; use `.join()` or `JSON.stringify` instead.
2. Never print an engine-generated error message. Print `e.name` or use
   `instanceof`; message wording is not standardised.
3. No local-time output. Use the UTC getters or `toISOString()`.
4. Keep timers short (under 100 ms) and let every promise settle.
5. Write the code, run it with `node -e`, paste the output as `#expect`, then
   run `--reference "node -e"` to confirm. `tools/conformance/add_js_case.py
   FILE.dat "name" script.js` does the first three steps in one, so the
   expectation is never typed by hand.
6. Add cases for what JavaScript does, including things Litebird cannot do
   yet. A corpus that only contains passing cases measures nothing.
7. A case that needs `gc()` must still run under Node, which has none. Start
   it the way `gc-safety.dat` does: `var gc = globalThis.gc || function(){ return {}; };`.
   Node then checks the logic, and Litebird checks that collecting does not
   change the answer. `make asan` runs the corpus under the sanitizers, where
   a freed object that is still in use is an error rather than a wrong value.
8. Where the limit is an engine's own (how deep a recursion may go, how much
   nesting the parser takes), do not expect one outcome. Accept the outcomes
   that are correct and print whether one of them happened, as
   `deep-nesting.dat` does: `console.log(outcome === 'ran' || outcome === 'RangeError')`.
   What such a case then checks is that the engine survived and kept working.
