# Phase 506: an invalid Date still converts setter arguments

## What was wrong

The shared `setter` helper in `src/js/BuiltinsDate.cpp` captured the Date's
time value, then immediately returned NaN for an invalid Date unless it was
a FullYear setter. Consequently `setMonth`, `setDate`, `setHours`,
`setMinutes`, `setSeconds` and `setMilliseconds`, and their UTC counterparts,
never converted any supplied arguments on an invalid Date. That suppressed
observable conversion hooks and their exceptions.

The FullYear branch recovered an invalid Date by setting its captured time
to zero, then applying LocalTime to it. `setFullYear` instead uses +0 as
calendar time when the captured value is NaN; LocalTime applies only to a
valid captured time. This misplaced conversion was invisible in UTC but
gave the wrong default calendar fields and time of day in other zones.

## What changed

The helper keeps its receiver check and time snapshot before conversion,
then converts supplied arguments in order before handling NaN. An abrupt
completion immediately propagates and preserves any hook's own mutation.
A non-FullYear method whose captured time was NaN returns NaN without
writing the slot, even if a conversion hook changed it. FullYear methods
recover from the captured NaN with calendar +0, ignoring any replacement
time installed by a hook. LocalTime is applied only to valid snapshots.
The remaining calendar arithmetic, clipping and omitted-argument defaults
are unchanged. `setTime`, timezone algorithms and other engine files are
not part of this task.

The new suite is `tests/suites/506-invalid-date-setter-conversion.cpp`.
It exercises all 14 methods through ordinary and exotic numeric conversion,
throws and Symbol results at each argument position, omitted and extra
arguments, non-finite inputs, receiver-check ordering, and hooks that mutate
the Date before returning or throwing. Local and UTC FullYear defaults
are checked against midnight calendar-epoch controls, not a hardcoded
UTC-only timestamp.

## Oracle choice

ECMA-262 source was inspected at
[`7cff4afe1dd8ee44d877de8dd7e6853f71cf1fd1`](https://github.com/tc39/ecma262/blob/7cff4afe1dd8ee44d877de8dd7e6853f71cf1fd1/spec.html),
particularly `sec-date.prototype.setmonth`, `sethours`, `setmilliseconds`,
`setfullyear` and `setutcfullyear`. The order is explicit: capture
[[DateValue]], convert the supplied arguments, then return for a captured
NaN (or use calendar +0 for FullYear). These are the same requirements as
the independent test262 `arg-coercion-order.js` and
`date-value-read-before-tonumber-*` files in the Date setter directories.

Node v22.22.3 is **not an oracle for every case in this suite**: it skips
optional conversions on an invalid Date and rereads a time mutated by a
conversion hook. A separately downloaded Node **v26.13.1** agrees on
218 of 222 checks, but differs on four FullYear snapshot cases. Those
four expectations are retained from the specification, not changed to
Node's output. In particular a hook that changes an invalid Date to a
2044 timestamp must not supply its month/day/time defaults to setFullYear,
and a hook invalidating a valid Date must not replace the initial snapshot.
No reference engine or downloaded spec is a runtime dependency or vendored
artifact; all are under ignored `build/`.

## Verification

Measured on 28 November 2026. The baseline is `c89f70e`; the claim commit
changed no files. Its binary was copied to `build/litebird-before-506`
before the fix. At test262 revision
`2e58f87d8350ad51224617a041c6e041d52f8444`:

```sh
python3 tools/conformance/run_test262.py built-ins/Date \
  --binary before=build/litebird-before-506 --binary now=build/litebird \
  --json build/506-date-comparison.json
```

- Full Date directory: **555 → 579 / 610**, **24 gained and zero lost**.
- The 14 shared-setter directories: **171 → 195 / 195**.
- Every gain is an `arg-coercion-order.js` or
  `date-value-read-before-tonumber-when-date-is-invalid.js` case, two
  apiece for the 12 non-FullYear setters.
- The standalone suite returns **`ok 222`** in UTC, America/New_York and
  Asia/Kolkata. The baseline fails **150/222** in UTC and **156/222** in
  each non-UTC zone.

Extract and run the exact suite without maintaining a separate probe:

```sh
python3 - <<'PY'
from pathlib import Path
source = Path('tests/suites/506-invalid-date-setter-conversion.cpp').read_text()
js = source.split('R"JS(', 1)[1].split(')JS"', 1)[0]
Path('build/506-probe.js').write_text('console.log(' + js.strip().removesuffix(';') + ');')
PY
for zone in UTC America/New_York Asia/Kolkata; do
  TZ="$zone" build/litebird --run-js "$(cat build/506-probe.js)"
done
```

A separate full-engine sanitizer build uses explicit **`-O0 -g0`**, not the
default CI optimization flags:

```sh
make -j2 BUILD_DIR=build-asan-fast \
  CXXFLAGS='-std=c++20 -O0 -g0 -Wall -Wextra -Iinclude -MMD -MP -fsanitize=address,undefined -fno-omit-frame-pointer' all
for zone in UTC America/New_York Asia/Kolkata; do
  TZ="$zone" ASAN_OPTIONS=detect_leaks=0 build-asan-fast/litebird \
    --run-js "$(cat build/506-probe.js)"
done
```

All three sanitizer probes return **`ok 222`**, with no sanitizer reports.
The entire Date test262 directory also scores **579/610** in that build,
with no sanitizer reports. This is focused sanitizer evidence, not
completion of the default full `make asan` aggregate.

Broader validation on the fixed plain build:

| Command | Result |
| --- | --- |
| `make test` | **538/538** engine suites and **114/114** cross-origin checks |
| `make conformance` | **1,973/1,973** |
| `make js-conformance` | **1,153/1,158**, everyday probe **90/90**, no crashes/timeouts |
| `make dom-conformance` | **678/678** |
| `make url-conformance` | **1,188/1,188** |
| `make xml-conformance` | **1,718/1,718** applicable documents, 2,585 run |
| `make wpt` | **9,570/9,824**, only the 254 listed failures |
| `make diff-trees` | **6,000/6,000** match corrected parse5 |
| `make diff-xml` | 4,000 documents, no disagreement where both references agree; **3,000/3,000** round trips |
| `make render-conformance` | **66/67** original pages (recorded AVIF decoder gap), **41/41** body-origin pages, **45,016/45,016** canvas checks |

`diff-xml` used `lxml` installed only in ignored `build/506-python-deps`
and added to `PYTHONPATH`. Documentation, suite-shape, metrics checks and
`git diff --check` pass. The unit run was deliberately started after the
sanitizer build to avoid the memory contention observed in Phase 493.

Validation limits: extended `make deep`, `make fuzz`, and the default full
`make asan` aggregate were not run locally for this task. The active
landing session's full CI must validate those gates before merging.

## What is still open

Date parsing, timezone/DST conversion algorithms, Date.prototype's missing
Symbol.toPrimitive method, and realm helpers remain separate. This is a
shared-setter argument-conversion/invalid-time fix, not a general Date
compatibility claim. Historical scores in Phases 489 and 493 remain
historical; no global conformance ledger is changed by this focused run.
