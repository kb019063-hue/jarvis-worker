# Finding 538: object-produced Symbol keys went through ToString

## What was wrong

The shared ToPropertyKey helper accepted a direct Symbol but routed an
object through ToString. If its exotic or ordinary conversion produced
a Symbol, ToString threw instead of preserving the Symbol property key.
This was the remaining conversion gap recorded by Phase 519, separate
from the engine's string-backed internal Symbol storage.

## What changed

Only the fallback branch of `js_to_property_key` in `src/js/JsOps.cpp`
changes: ToPrimitive with string hint, preserve a resulting Symbol's
internal key, otherwise stringify the primitive. Direct string/Symbol/
numeric fast paths stay unchanged. No caller or property storage changes.

## Verification

`tests/suites/538-symbol-property-key-conversion.cpp` checks eighteen
callers, both exotic and ordinary conversion, hint/receiver/arguments,
once-only dispatch, primitive controls, exceptions and collection inside
a hook. Node v22.22.3 with `--expose-gc` and the fixed plain engine both
return **`ok 309`**. **92** checks fail on the same-base baseline.
Four more indexed read/write scripts are tested through both interpreter
and bytecode VM. C++ asserts the explicit script result, not merely that
the script evaluation returned.

See [Phase 538](../phases/538-symbol-property-key-conversion.md) for
reproducible commands, broader comparisons and validation limits.

## What is still open

Symbol.prototype[Symbol.toPrimitive] is absent: an unmodified boxed Symbol
uses its toString method and produces a display string. It is a separate
prototype-installation defect, recorded as a new AGENTS.md task, and not
fixed by changing the ordinary conversion order here. Internal Symbol
keys remain strings, including the existing collision with some literal
well-known Symbol spellings. This fixes conversion into the existing
representation, not complete Symbol or ECMAScript conformance.
