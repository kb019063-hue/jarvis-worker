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
   run `--reference "node -e"` to confirm.
6. Add cases for what JavaScript does, including things Litebird cannot do
   yet. A corpus that only contains passing cases measures nothing.
