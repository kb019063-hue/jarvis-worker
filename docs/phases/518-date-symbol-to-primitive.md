# Phase 518: Date has a primitive-conversion method

## What was wrong

`Date.prototype[Symbol.toPrimitive]` did not exist. `js_to_primitive` in
`src/js/JsOps.cpp` approximated its default string preference by checking
whether the receiver had the Date class tag. That made ordinary Dates
look plausible but could not implement the specified generic method,
its reflected metadata, or its use on other objects. An object inheriting
from Date.prototype did not inherit Date's default string preference.

The class-tag workaround would also be wrong once the real method existed:
a Date with an own null/undefined Symbol.toPrimitive should opt out of the
exotic conversion and receive ordinary number-first default conversion.

## What changed

`src/js/BuiltinsDate.cpp` installs the method with name
`[Symbol.toPrimitive]`, length 1, no [[Construct]] or own `prototype`, and
property attributes writable=false, enumerable=false, configurable=true.
The receiver must be an object, but need not be a Date. The hint must be
exactly the primitive string `default`, `string` or `number`; it is never
coerced. Default/string try toString then valueOf; number reverses them.

This is OrdinaryToPrimitive, not a recursive call to `js_to_primitive`:
each property is read only when needed, noncallables are skipped, methods
receive the original object and zero arguments, exceptions propagate, and
the first primitive result is returned unchanged. Two unsuccessful choices
throw TypeError. The implementation neither asks for nor invokes an
exotic method on the borrowed receiver.

In `src/js/JsOps.cpp`, only the three-line Date-class default-hint special
case is removed. Ordinary fallback now applies uniformly, while Dates
get their default string preference from the actual prototype method.
This file was added to the claim after checking it was unclaimed, before
editing it. Other shared conversion-helper behavior is unchanged.

## Verification

The standalone suite is `tests/suites/518-date-symbol-to-primitive.cpp`.
Its 93 checks cover metadata, receiver/hint validation, getter/call order,
noncallable methods, object fallback, all primitive return categories,
exception identity at each stage, function/array/wrapper receivers,
implicit Date conversion, inherited generic conversion, null/undefined
opt-out, prevention of recursive exotic dispatch, and collection inside
conversion hooks. The script result is asserted in C++, so a missing
method or an uncaught script exception cannot silently pass the suite.
Node v22.22.3 with `--expose-gc` returns `ok 93` for the same probe.

Measured on 28 November 2026. The baseline is `d67b49b` (the claim commit
changed no files); its binary was copied to `build/litebird-before-518`
before the source changes. At pinned test262 revision
`2e58f87d8350ad51224617a041c6e041d52f8444`:

```sh
python3 tools/conformance/run_test262.py built-ins/Date \
  --binary before=build/litebird-before-518 --binary now=build/litebird \
  --json build/518-date-comparison.json
python3 tools/conformance/run_test262.py \
  --binary before=build/litebird-before-518 --binary now=build/litebird \
  --json build/518-default-test262.json
```

- Date: **579 → 593 / 610**, **14 gained, zero lost**.
- Date's Symbol.toPrimitive directory: **4 → 18 / 18**. Four TypeError-only
  cases already passed without establishing that the method existed;
  the new suite first checks its presence explicitly.
- The runner's 21 default directories, covering Object/Reflect/Function,
  errors, symbols, classes and expressions: **15,236 / 15,686** before
  and after, **zero gained and zero lost**; 14 module tests skipped.
  This is a regression comparison of those directories, not all test262.

Run the same focused probe without a separately maintained copy:

```sh
python3 - <<'PY'
from pathlib import Path
import subprocess
source = Path('tests/suites/518-date-symbol-to-primitive.cpp').read_text()
js = source.split('R"JS(', 1)[1].split(')JS"', 1)[0]
probe = 'console.log(' + js.strip().removesuffix(';') + ');'
for command in [['node', '--expose-gc', '-e', probe], ['build/litebird', '--run-js', probe]]:
    subprocess.run(command, check=True)
PY
```

Both return **`ok 93`**. A full-engine AddressSanitizer/UBSan build at
explicit **`-O0 -g0`**, rather than the default CI optimization flags,
also returns `ok 93` and scores **593/610** on the Date directory, with
no sanitizer reports:

```sh
make -j2 BUILD_DIR=build-asan-fast \
  CXXFLAGS='-std=c++20 -O0 -g0 -Wall -Wextra -Iinclude -MMD -MP -fsanitize=address,undefined -fno-omit-frame-pointer' all
ASAN_OPTIONS=detect_leaks=0 python3 tools/conformance/run_test262.py built-ins/Date \
  --binary asan=build-asan-fast/litebird --min-pass 593
```

Broader plain-build validation:

| Command | Result |
| --- | --- |
| `make test` | **554/554** engine suites and **114/114** cross-origin checks |
| `make conformance` | **1,973/1,973** |
| `make js-conformance` (standalone rerun) | **1,153/1,158**, everyday **90/90**, no crashes/timeouts |
| `make dom-conformance` | **678/678** |
| `make url-conformance` | **1,188/1,188** |
| `make xml-conformance` | **1,718/1,718** applicable documents, 2,585 run |
| `make wpt` | **9,570/9,824**, only the 254 listed failures |
| `make diff-trees` | **6,000/6,000** match corrected parse5 |
| `make diff-xml` | 4,000 documents, no disagreement where both references agree; **3,000/3,000** round trips |
| `make render-conformance` | **67/68** original pages (recorded AVIF decoder gap), **41/41** body-origin pages, **45,016/45,016** canvas checks |

The first JS corpus run, alongside the before/after test262 comparison and
sanitizer Date run, had two timeouts and scored 1,151/1,158. The standalone
rerun with no competing processes restored 1,153/1,158 with zero timeouts.
It is the latter result recorded above, not a silent lowering of the floor.
`diff-xml` used `lxml` installed only in ignored `build/518-python-deps`
and added to `PYTHONPATH`. Documentation, suite-shape, metrics checks and
`git diff --check` pass.

Validation limits: extended `make deep`, `make fuzz`, and the default full
`make asan` aggregate were not run locally. The active landing session's
full CI must validate those gates before merging. Focused sanitizer checks
are not a replacement for that run.

## What is still open

The shared ToPrimitive helper's handling of a non-nullish, noncallable
exotic method remains outside this task; it still falls back where
GetMethod should throw. Other Date parsing/formatting, Temporal and realm
support are separate. A focused Date run is not an overall ECMAScript
conformance percentage, and historical metrics are not rewritten from it.
