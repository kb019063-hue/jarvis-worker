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
case it returns a recognized parse result containing NaN. It must not
return `false`: that signals the caller to retry the text through the
legacy parser, which can accept numeric tokens under another meaning.

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

Broader plain and sanitizer results follow once their runs finish.

## What is still open

General legacy heuristics, malformed non-ISO input, other strict ISO
validation (including offset bounds), negative-year formatting, timezone
algorithms and realms remain separate. This closes recognition of the
forbidden zero expanded year in the existing ISO path, not every Date
parsing or ECMAScript conformance issue.
