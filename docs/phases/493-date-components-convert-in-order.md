# Phase 493: Date components convert in JavaScript order

## What was wrong

Both multi-argument `new Date(year, month, ...)` and `Date.UTC(year, month,
...)` share `time_from_components` in `src/js/BuiltinsDate.cpp`. That helper
converted the year first, then passed six calls to `arg_or` as C++ function
arguments to `make_time`. C++ does not require those argument expressions
to be evaluated left to right. The GCC build evaluated them right to left:
year, milliseconds, seconds, minutes, hours, date, month.

These calls perform JavaScript ToNumber, which can invoke user code and
throw. The order is observable: a month conversion that throws must prevent
all later conversions, and an earlier hook can change what a later hook
returns. This was an embedding error, not a compiler bug.

ECMA-262's Date constructor (`sec-date-constructor`) and Date.UTC
(`sec-date.utc`) require conversion of the supplied components from year
through milliseconds before the calendar calculation. An invalid numeric
component such as NaN is not an abrupt completion: later supplied arguments
still have to be converted. Phase 489's Date run exposed four test262
failures for order and abrupt completions, one pair for each entry point.

## What changed

The helper now materializes each numeric component in a separate declaration,
in source order, before normalizing the two-digit year and invoking
`make_time` with numbers only. Omitted-argument defaults, year mapping,
TimeClip and local-time/UTC calculation are unchanged. No parser, VM,
conversion-helper, Date setter or other agent's engine file changes.

The standalone suite `tests/suites/493-date-components-convert-in-order.cpp`
checks both entry points with `valueOf` and `Symbol.toPrimitive` hooks,
number hints, exact conversion order, exception identity and stopping at
every field, Symbol conversion errors, NaN/infinities/undefined at each
field without prematurely skipping later conversions, missing defaults,
ignored extra arguments, two-digit year mapping, and hooks that mutate
later component values. It also guards the single-year Date.UTC default
and the non-constructing Date function. The C++ assertion checks the final
script result, not merely whether eval returned.

## Verification

Measured on 28 November 2026. The baseline is `4ea7bfb` (the claim commit
changes no files); its binary was saved as `build/litebird-before-493` before
applying the fix. Node v22.22.3 and the fixed engine both return **`ok 116`**
for the standalone suite. The baseline fails **108 of the 116** checks.

```sh
python3 tools/conformance/run_test262.py built-ins/Date \
  --binary before=build/litebird-before-493 --binary now=build/litebird \
  --json build/493-date-comparison.json
```

At pinned test262 commit `2e58f87d8350ad51224617a041c6e041d52f8444`:
**551 → 555 / 610**, **four fixed, zero lost**. The gains are exactly
`coercion-order.js` and `coercion-errors.js` for Date and Date.UTC.

For a focused sanitizer check, the engine was also built with
AddressSanitizer/UBSan at **`-O0 -g0`**, distinct from the default CI build:

```sh
make -j2 BUILD_DIR=build-asan-fast \
  CXXFLAGS='-std=c++20 -O0 -g0 -Wall -Wextra -Iinclude -MMD -MP -fsanitize=address,undefined -fno-omit-frame-pointer' all
```

The same probe returns **`ok 116`** with `ASAN_OPTIONS=detect_leaks=0`.
The entire 610-test Date directory also scores **555/610** in that sanitizer
build, with no sanitizer reports. This does not claim completion of the
default `make asan` aggregate.

Broader validation on the fixed plain build:

| Command | Result |
| --- | --- |
| `make test` (serial rerun) | **533/533** engine suites; **114/114** cross-origin checks |
| `make conformance` | **1,973/1,973** |
| `make js-conformance` | **1,153/1,158**, everyday probe **90/90**, no crashes/timeouts |
| `make dom-conformance` | **678/678** |
| `make url-conformance` | **1,188/1,188** |
| `make xml-conformance` | **1,718/1,718** applicable documents, 2,585 documents run |
| `make wpt` | **9,570/9,824**, only the 254 listed failures |
| `make diff-trees` | **6,000/6,000** match the corrected parse5 reference |
| `make diff-xml` | 4,000 documents, no disagreement where both references agree; **3,000/3,000** round trips |
| `make render-conformance` | **66/67** original pages (one recorded AVIF decoder gap), **41/41** body-origin pages, **45,016/45,016** canvas checks |

`diff-xml` used `lxml` installed only under ignored
`build/493-python-deps`, added to `PYTHONPATH`. Documentation, suite-shape,
metrics checks and `git diff --check` also pass.

Validation limits: the first unit run was stopped at
`dom_trees_of_any_depth_can_be_walked_and_released` during severe memory
pressure while the cold sanitizer build ran alongside it on this two-core,
4 GB sandbox. The complete serial rerun after the build passed, including
that suite in six seconds. Extended `make deep`, `make fuzz`, and the default
full `make asan` aggregate were not run locally for this task; the landing
session's full CI must validate those before merging.

The JavaScript probe is extracted directly from the suite, without a second
maintained copy:

```sh
python3 - <<'PY'
from pathlib import Path
import subprocess
source = Path('tests/suites/493-date-components-convert-in-order.cpp').read_text()
js = source.split('R"JS(', 1)[1].split(')JS"', 1)[0]
probe = 'console.log(' + js.strip().removesuffix(';') + ');'
for command in [['node', '-e', probe], ['build/litebird', '--run-js', probe]]:
    subprocess.run(command, check=True)
PY
```

## What is still open

This closes component-conversion ordering only. Date parsing, DST/time-zone
handling, setter coercion on invalid Dates, the missing
Date.prototype[Symbol.toPrimitive], and `$262` realm support are separate.
Historical Phase 489 measurements remain historical; no global metrics
ledger or advertised percentage is changed by this focused Date run.
