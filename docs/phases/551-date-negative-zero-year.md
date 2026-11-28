# Phase 551: Date parsing does not turn a negative-zero year into year zero

## What was wrong

The ISO parser in `src/js/BuiltinsDate.cpp` read a signed six-digit year
into an integer and multiplied by the sign. For `-000000`, that integer
became indistinguishable from zero and the parser accepted the forbidden
expanded-year representation as year zero.

This is the Date parsing gap left open by Phases 493, 506 and 518. Pinned
test262 `built-ins/Date/parse/year-zero.js` expects NaN for UTC, local and
numeric-offset date-times with this expanded year.

## What changed

After recognizing and validating the existing ISO syntax, the parser
checks the separately retained sign and zero year. For the negative-zero
case it returns a recognized parse result containing NaN, rather than
`false` (the caller's signal to retry the text under legacy heuristics).
The invalid recognized form therefore stays in the ISO path.

Valid `0000`, `+000000`, positive and negative nonzero expanded years,
TimeClip bounds, local/UTC interpretation and input conversion remain
unchanged. No other source file or general legacy parser rule changes.
The check applies to the forms already consumed by the existing ISO
parser; it is not a general strict-ISO parser rewrite.

## Verification

`tests/suites/551-date-negative-zero-year.cpp` contains **111 checks**.
It covers thirteen recognized ISO suffixes with plain and boxed strings
through Date.parse and the single-argument constructor; positive year-zero
controls; known UTC values and both TimeClip boundaries; whitespace
already trimmed by the parser; input conversion hint/receiver/arguments,
once-only conversion, getter/call exceptions, ordinary fallback order,
ignored extra parse arguments and collection in conversion hooks.

### Standard and Node are distinguished

The reference is ECMA-262 at
`7cff4afe1dd8ee44d877de8dd7e6853f71cf1fd1`, sections
[Expanded Years](https://github.com/tc39/ecma262/blob/7cff4afe1dd8ee44d877de8dd7e6853f71cf1fd1/spec.html)
and Date.parse, plus pinned test262 revision
`2e58f87d8350ad51224617a041c6e041d52f8444`.
The expanded-years clause says the representation `-000000` is invalid.
Date.parse permits implementation-specific heuristics for strings that
do not conform to the format.

Node **v22.22.3** agrees with **103/111** checks. It accepts the date-only
shapes `-000000-03` and `-000000-03-31` via legacy parsing, as dates in
2001, accounting for the eight differences (plain/boxed inputs through
two entry points). This task chooses to reject those recognized ISO-like
forms rather than invoke legacy heuristics. Those differences are stated
as an engine policy, **not counted as a Node conformance improvement**.
The pinned test262 rejection cases are all date-times, on which Node and
the suite agree. None of the expectations is silently rewritten to match
the oracle.

### Same-base engine evidence

The baseline is **cb2b04bf** (the claim commit changed no files); its binary
was copied to `build/litebird-before-551` before the source edit. It fails
**63/111** focused checks. The fixed plain engine returns **`ok 111`** in
UTC, America/New_York and Asia/Kolkata. The standalone C++ suite also
passes as its own stretch:

```sh
LITEBIRD_SUITE_SHARD=1/3 \
LITEBIRD_SUITE_CUTS=date_parsing_rejects_negative_zero_without_losing_valid_year_zero,color_parsing \
  ./build/test_suite
```

Extract the exact suite script for CLI/Node reproduction:

```sh
python3 - <<'PY'
from pathlib import Path
source = Path('tests/suites/551-date-negative-zero-year.cpp').read_text()
js = source.split('R"JS(', 1)[1].split(')JS"', 1)[0]
Path('build/551-probe.js').write_text('console.log(' + js.strip().removesuffix(';') + ');')
PY
for zone in UTC America/New_York Asia/Kolkata; do
  TZ="$zone" build/litebird --run-js "$(cat build/551-probe.js)"
done
node --expose-gc build/551-probe.js
```

The engine prints `ok 111` three times; Node prints the eight date-only
policy differences described above, with no other differences.

```sh
python3 tools/conformance/run_test262.py built-ins/Date \
  --binary before=build/litebird-before-551 --binary now=build/litebird \
  --json build/551-date-comparison.json
```

This improves **593 → 595 / 610**, **two gained and zero lost**:
`built-ins/Date/parse/year-zero.js` and `built-ins/Date/year-zero.js`.
The parse directory improves **6 → 7 / 8**. The remaining parse case is
`without-utc-offset.js` (signed-zero handling), not this expanded-year
check. This is a focused Date comparison, not an overall engine score.

### Canonical sanitizer engine

```sh
make -j2 asan-build
for zone in UTC America/New_York Asia/Kolkata; do
  TZ="$zone" ASAN_OPTIONS=detect_leaks=0 build-asan/litebird \
    --run-js "$(cat build/551-probe.js)"
done
ASAN_OPTIONS=detect_leaks=0 python3 tools/conformance/run_test262.py \
  built-ins/Date --binary asan=build-asan/litebird --min-pass 595 \
  --json build/551-asan-date.json
```

The full engine uses the repository's canonical **-O1 -g ASan/UBSan** flags.
It returns **`ok 111` in all three zones** and passes **595/610** Date
test262 cases, with no sanitizer reports in the complete result JSON.
Leak detection is disabled as in the repository's sanitizer targets.

### Broader plain-build checks

| Command | Result |
| --- | --- |
| `make test` | **583/583** engine suites and **114/114** cross-origin checks |
| `make conformance` | **1,973/1,973** |
| `make js-conformance` (standalone rerun) | **1,154/1,158**, everyday **90/90**, no crashes/timeouts |
| `make dom-conformance` | **678/678** |
| `make url-conformance` | **1,188/1,188** |
| `make xml-conformance` | **1,718/1,718** applicable cases |
| `make wpt` | **9,570/9,824**, only the 254 listed failures |
| `make diff-trees` | **6,000/6,000** match corrected parse5 |
| `make diff-xml` | 4,000 documents, zero discrepancies where both references agree; **3,000/3,000** round trips |
| `make render-conformance` | **67/68** original pages (recorded AVIF decoder gap), **41/41** body-origin pages, **45,016/45,016** canvas checks |
| `make fuzz` | **400** parser mutations, **400** pipeline runs, **600** XML mutations and **1,927** original documents, **2,400** media mutations, **5,009** color inputs, Brotli **53 valid / 49 bomb-cap / 6 invalid / 2,000 mutations**; all clean |

The first JS corpus run overlapped cold sanitizer compilation and scored
1,152/1,158 with two timeouts. The standalone rerun, with no competing
processes, restored 1,154/1,158 with zero timeouts. Its floor was not
lowered. `diff-xml` used lxml installed only under ignored
`build/551-python-deps` and added to PYTHONPATH. No metrics ledger,
existing corpus expectation or floor changed.

Documentation, suite-shape, metrics and whitespace checks pass. Full
`make fuzz` completes successfully with zero findings.

### Local validation limits

The full local `make deep` and `make asan` aggregates were not run. In
particular, this task does not retry the monolithic sanitizer units that
exhausted the 4 GB sandbox in Phase 538 (that phase records the actual
failure/recovery evidence). Focused canonical-engine sanitizer results
above are not a passing full sanitizer-unit gate. Complete default full
CI, including all sanitizer and deep-page jobs, must pass in the landing
watch (or on this PR if no watch is active) before merge.

## What is still open

General legacy heuristics, malformed non-ISO input, other strict ISO
validation (including offset bounds), negative-year formatting, timezone
algorithms and realms remain separate. This closes recognition of the
forbidden zero expanded year in the existing ISO path, not every Date
parsing or ECMAScript conformance issue.
