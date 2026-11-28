# Phase 489: Date.toJSON converts, then invokes

## What was wrong

`Date.prototype.toJSON` used `time_slot` to require a genuine Date, returned
null for its invalid slot, and formatted the slot directly. ECMA-262
§21.4.4.37 instead defines a generic method: ToObject(this), ToPrimitive with
the number hint, return null only for a non-finite Number, then
Invoke(O, "toISOString"). An ordinary Date with an overridden `toISOString`
was serialized as though that override did not exist. Non-Date receivers
always threw, and overriding a Date's numeric conversion was ignored.

## What changed

Only the `toJSON` installation in `src/js/BuiltinsDate.cpp` changes. It uses
the existing `js_to_object`, `js_to_primitive` (Number hint), property lookup
and `js_call` operations. It does not coerce the conversion result to a
Number, passes no arguments to `toISOString`, preserves its return value,
and lets conversion, lookup and call exceptions propagate unchanged.
Other Date methods retain their Date-slot checks.

`tests/suites/489-date-tojson-is-generic.cpp` is an independently registered
suite with 39 checks. It covers ordinary and invalid dates, overridden
methods, JSON.stringify integration, generic and boxed receivers, all three
non-finite Numbers, nonnumeric primitives, exact hint/lookup/call order,
receiver identity, zero call arguments, unchanged return values,
noncallable methods, and exception identity and ordering. The C++ test
asserts the returned success string so an uncaught script exception cannot
silently pass the suite.

## Verification

On 28 November 2026, the baseline binary was built from `0c4b6e0` (the
session's base; the claim commit changes no files), copied to
`build/litebird-before-489`, then compared with the fixed binary:

```sh
make -j2 test build/litebird
python3 tools/conformance/run_test262.py built-ins/Date built-ins/JSON/stringify \
  --binary before=build/litebird-before-489 --binary now=build/litebird \
  --json build/489-date-json-comparison.json
```

- Unit tests: **524/524 engine suites**, plus **114 cross-origin checks**,
  no failures.
- test262 at `2e58f87d8350ad51224617a041c6e041d52f8444`: **587 → 595
  passing out of 677**, **8 gained, 0 lost**.
- Inside that run: Date **543 → 551 / 610**, specifically `toJSON`
  **5 → 13 / 13**; JSON.stringify unchanged at **44 / 67**.
- The suite's 39 JavaScript checks produce `ok 39` in Node **v22.22.3**
  and the fixed engine. The baseline disagrees on 24 checks.
- Broader gates: `make conformance` **1,973/1,973**;
  `make js-conformance` **1,153/1,158** (90/90 everyday probe);
  `make dom-conformance` **678/678**;
  `make url-conformance` **1,188/1,188**;
  `make xml-conformance` **1,718/1,718 applicable documents**;
  `make wpt` **9,570/9,824**, with only the 254 listed failures.
- `make diff-trees`: **6,000/6,000** equal the corrected parse5 reference;
  `make diff-xml`: **4,000 documents**, no disagreement where the two
  references agree, plus **3,000/3,000 round trips**. The latter needs
  `lxml`; here it was installed only under ignored `build/489-python-deps`
  and run with that directory in `PYTHONPATH`.
- `make -j2 asan-build` succeeds. The same 39-check probe prints `ok 39`
  under AddressSanitizer/UBSan, and all **13/13** test262 toJSON tests
  pass under that build (`ASAN_OPTIONS=detect_leaks=0`, as in CI).
  `make asan-js` passes its configured floor: **1,152/1,158**, 90/90
  everyday cases, zero crashes/timeouts or sanitizer reports. This is a
  different build from the plain 1,153/1,158 run, not a measured change
  against a sanitizer baseline.
- `make -j2 fuzz` passes all six parts: 400 parser mutations, 400 pipeline
  runs, XML corpus round trips plus 600 mutations, 2,400 media cases,
  5,008 CSS colour inputs, and the Brotli verifier (including 2,000
  mutations). No findings were reported.

Local validation limits: the full `make deep` run was stopped while
competing with the cold sanitizer build on this two-core, 4 GB sandbox;
it is **not** recorded as passing. It had queued table/details cases for
serial retry after their concurrent runs hit the time limit. The entire
`make asan` aggregate (including sanitized unit/DOM/XML/WPT/deep targets)
was not run locally. The landing session's full CI must still validate
those gates before merging; focused sanitizer checks are not a substitute.

To run exactly the same probe in Node and Litebird, with no additional
verifier or hand-maintained copy of the cases:

```sh
python3 - <<'PY'
from pathlib import Path
import subprocess
source = Path('tests/suites/489-date-tojson-is-generic.cpp').read_text()
js = source.split('R"JS(', 1)[1].split(')JS"', 1)[0]
probe = 'console.log(' + js.strip().removesuffix(';') + ');'
for command in [['node', '-e', probe], ['build/litebird', '--run-js', probe]]:
    subprocess.run(command, check=True)
PY
```

## What is still open

This is not a general Date or JSON conformance claim. The remaining Date
failures include component-coercion ordering, the expanded-year parse rule,
missing Date.prototype[Symbol.toPrimitive], and tests requiring `$262`
realm helpers. JSON.stringify's other failures are unchanged. The shared
ToPrimitive helper's handling of a non-nullish, noncallable
`Symbol.toPrimitive` is outside this one-file task; this change neither
repairs nor duplicates that algorithm. No metrics ledger or global
conformance percentage was changed on the strength of this focused run.
