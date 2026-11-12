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
