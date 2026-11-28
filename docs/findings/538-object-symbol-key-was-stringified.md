# Finding 538: object-produced Symbol keys went through ToString

## What was wrong

The shared ToPropertyKey helper accepted a direct Symbol but routed an
object through ToString. If its exotic or ordinary conversion produced
a Symbol, ToString threw instead of preserving the Symbol property key.
This was the remaining conversion gap recorded by Phase 519, separate
from the engine's string-backed internal Symbol storage.

## What changed

The initial fix changed only the fallback branch of `js_to_property_key`:
ToPrimitive with string hint, preserve a resulting Symbol's internal key,
otherwise stringify the primitive. A concurrent equivalent implementation
landed in **PR #544** during this task's validation. The main sync retained
that implementation verbatim. **PR #538's final diff therefore adds only
regression coverage and documentation**, not another production fix or
another improvement over current main. Direct string/Symbol/numeric fast
paths, callers and property storage remain unchanged.

## Verification

`tests/suites/538-symbol-property-key-conversion.cpp` checks eighteen
callers, both exotic and ordinary conversion, hint/receiver/arguments,
once-only dispatch, primitive controls, exceptions and collection inside
a hook. Node v22.22.3 with `--expose-gc` and the fixed plain engine both
return **`ok 309`**. **92** checks fail on the same-base baseline.
Four more indexed read/write scripts are tested through both interpreter
and bytecode VM. C++ asserts the explicit script result, not merely that
the script evaluation returned.

The original same-base pinned test262 comparison improves **15,253 →
15,264 / 15,686**, **11 gained, zero lost**. This predates the main sync
and is not a second gain over the equivalent fix in #544. The canonical -O1 -g ASan/UBSan engine
passes all **309** focused checks and all **11** gained test262 cases.
Plain full units pass **570/570** plus **114/114** cross-origin checks
before the sync, and **579/579 + 114/114** after syncing main at 78bccd34.

Local sanitizer units have an explicit limit: **566/570** suites and
**114/114** cross-origin checks pass in fresh sequential stretches (test
harness only built with -O0 -g0). The deep-nesting suite exhausts this
4 GB sandbox even in isolation, so this is **not** a passing full
sanitizer-unit gate. Full default sanitizer/deep CI must pass before
merge. See [Phase 538](../phases/538-symbol-property-key-conversion.md)
for commands, broader comparisons and the interruption/recovery record.

## What is still open

Symbol.prototype[Symbol.toPrimitive] is absent: an unmodified boxed Symbol
uses its toString method and produces a display string. It is a separate
prototype-installation defect, recorded as a new AGENTS.md task, and not
fixed by changing the ordinary conversion order here. Internal Symbol
keys remain strings, including the existing collision with some literal
well-known Symbol spellings. This fixes conversion into the existing
representation, not complete Symbol or ECMAScript conformance.
