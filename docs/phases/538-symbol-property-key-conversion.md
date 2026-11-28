# Phase 538: regression coverage for object-produced Symbol property keys

## What was wrong

`js_to_property_key` in `src/js/JsOps.cpp` preserved direct Symbols but
sent objects straight to `js_to_string`. An object whose exotic or
ordinary conversion returned a Symbol therefore threw TypeError, even
though ToPropertyKey must preserve that primitive unchanged. This was
explicitly left open by Phase 519.

For example, `Object.hasOwn(object, key)` failed when `key` returned the
Symbol naming an own property. Pinned test262's
`built-ins/Object/hasOwn/symbol_property_toPrimitive.js` expects true
and exactly one conversion call for this case. The corresponding
`symbol_property_toString.js` and `symbol_property_valueOf.js` cover
ordinary conversion.

## What changed

The initial implementation changed only the fallback branch of the shared
property-key helper: ToPrimitive with string hint, preserve a resulting
Symbol's existing internal key, and stringify any other primitive. Direct
string, Symbol and numeric-index fast paths remained unchanged.

**A concurrent equivalent fix landed in PR #544 while this task was being
validated.** Syncing with main found the overlap in the same fallback
branch. The merge retains #544's implementation byte-for-byte. The final
PR #538 therefore adds **regression coverage and documentation only**:
no production file differs from main. Its original baseline measurements
below are historical evidence for the suite, not additional gains over
current main. The suite adds eighteen caller matrices and explicit VM
probes beyond #544's implementation tests.

This does not attempt to replace the engine's string-backed Symbol key
representation. It fixes conversion into the representation that direct
Symbol keys already use.

## Verification

`tests/suites/538-symbol-property-key-conversion.cpp` contains **309 checks**
across eighteen callers: read, write, `in`, delete, Object.defineProperty,
getOwnPropertyDescriptor, hasOwn, prototype hasOwnProperty and
propertyIsEnumerable; Reflect.get/set/has/deleteProperty/defineProperty/
getOwnPropertyDescriptor; Object.fromEntries; and computed object/class
names.

Each caller tests exotic, ordinary-first and ordinary-fallback Symbol
results, null-prototype key objects and direct Symbols, seven non-Symbol
primitives, getter and call exception identity, object results, noncallable hooks and
collection during the hook. It checks hint, receiver, arguments, order,
exactly-once conversion, Symbol identity, well-known Symbols and the
fact that String(objectReturningSymbol) still throws.

The script produces **`ok 309`** in Node v22.22.3 with `--expose-gc`.
C++ asserts that exact final result so a script exception cannot silently
pass the test suite. The same **309 checks pass in the fixed engine**;
**92 fail on the baseline** (`46cf738`, saved before editing the helper).
Four additional scripts exercise indexed reads/writes with exotic or
ordinary Symbol-returning keys through both `eval` and `eval_bytecode`,
so the VM's separate indexed access callers are also tested.

Pinned test262 revision `2e58f87d8350ad51224617a041c6e041d52f8444`,
same-base before/after comparison:

```sh
python3 tools/conformance/run_test262.py \
  --binary before=build/litebird-before-538 --binary now=build/litebird \
  --json build/538-default-test262.json
```

The runner's 21 default helper-regression directories improve
**15,253 → 15,264 / 15,686**, **11 gained, zero lost**, with 14 module
cases skipped. The gains are the three Symbol-conversion cases in each
of Object.hasOwn, Object.prototype.hasOwnProperty and
Object.prototype.propertyIsEnumerable, plus the computed Symbol name
case in both class declarations and expressions. This is not all test262
or an engine-wide conformance percentage.

Extract and run the exact 309-check script without maintaining a second
copy:

```sh
python3 - <<'PY'
from pathlib import Path
import subprocess
source = Path('tests/suites/538-symbol-property-key-conversion.cpp').read_text()
js = source.split('R"JS(', 1)[1].split(')JS"', 1)[0]
probe = 'console.log(' + js.strip().removesuffix(';') + ');'
for command in [['node', '--expose-gc', '-e', probe], ['build/litebird', '--run-js', probe]]:
    subprocess.run(command, check=True)
PY
```

The focused C++ suite, including all four additional VM/interpreter
scripts, passes as its own stretch:

```sh
LITEBIRD_SUITE_SHARD=1/3 \
LITEBIRD_SUITE_CUTS=object_property_keys_preserve_symbols_and_convert_only_once,color_parsing \
  ./build/test_suite
```

Broader plain-build results on 28 November 2026, before syncing the later
main changes (recorded here rather than retroactively changing their base):

| Command | Result |
| --- | --- |
| `make test` | **570/570** engine suites and **114/114** cross-origin checks |
| `make conformance` | **1,973/1,973** |
| `make js-conformance` | **1,154/1,158**, everyday **90/90**, no crashes/timeouts; same result on baseline |
| `make dom-conformance` | **678/678** |
| `make url-conformance` | **1,188/1,188** |
| `make xml-conformance` | **1,718/1,718** applicable cases |
| `make wpt` | **9,570/9,824**, only the 254 listed failures |
| `make diff-trees` | **6,000/6,000** match corrected parse5 |
| `make diff-xml` | 4,000 documents with no discrepancies where both references agree; **3,000/3,000** round trips |
| `make render-conformance` | **67/68** original pages (recorded AVIF decoder gap), **41/41** body-origin pages, **45,016/45,016** canvas checks |
| `make fuzz` | **400** parser mutations; **400** pipeline runs; **600** XML mutations plus **1,927** original documents; **2,400** media mutations; **5,009** color inputs; Brotli **53 valid / 49 bomb-cap / 6 invalid / 2,000 mutations**, all clean |

`diff-xml` used lxml installed only in ignored `build/538-python-deps`
and added to PYTHONPATH. No corpus, metrics ledger or floor changed.

After syncing main at **78bccd34**, rebuilding the engine and both unit
binaries and running `make test` passes **579/579 engine suites and
114/114 cross-origin checks**. The focused script still returns `ok 309`.
`git diff origin/main -- src/js/JsOps.cpp` is empty, confirming the final
PR keeps #544's production implementation without modification.
Documentation, suite-shape, metrics and whitespace checks also pass.

Before the main sync, the canonical **-O1 -g ASan/UBSan engine build**
(`make -j2 asan-build`) passes **all 309 focused checks**, including collection inside hooks,
and **all eleven newly passing test262 cases**, with no sanitizer reports.
The latter can be reproduced by passing the eleven gained file paths
from the comparison JSON to `run_test262.py`, with
`--binary asan=build-asan/litebird --min-pass 11` and
`ASAN_OPTIONS=detect_leaks=0`.

### Local validation limits and workspace recovery

`make -j1 asan-unit TEST_OPT='-O0 -g0'` was started after the plain units
finished. Engine objects used the canonical -O1 -g sanitizer flags; only
the large test harness translation unit used -O0 -g0 to reduce compilation
memory. The process reached about 2.36 GB resident memory and later
logged `deep_nesting_never_exhausts_the_native_stack`, after which command
execution became unresponsive. The log has no final suite summary. Stop
and kill attempts also failed/timed out. **This is not a passing full
sanitizer-unit run.**

Execution subsequently recovered. The log ended with `test` killed and
`asan-unit` exiting 2, and no old `test_suite` process survived. The six
standard balanced-suite cuts were retried as fresh sequential processes.
Stretches **0, 1, 4 and 5 pass: 303 + 38 + 70 + 155 = 566 suites**, plus
**114/114** cross-origin checks. These include the new suite and its VM
probes. Stretch 2 reached **3,845,340 KB RSS** in the same deep-nesting
test even in a fresh process; it was explicitly killed to avoid another
sandbox lock-up. Stretch 3 was not attempted after that. Thus **four deep
suites remain unvalidated locally under sanitizers**; 566/570 is a partial
result, not a passing full sanitizer-unit gate.

The rerun used the exact cuts from Makefile's UNIT_SUITE_CUTS, with
`ASAN_OPTIONS=detect_leaks=0` and `LITEBIRD_SUITE_SHARD=K/6`, running each
K in a fresh process. No cut, expectation, depth or CI floor was changed.
A full default sanitizer/deep CI run must pass before merge, either on
this pull request when no watch is active or in the active landing lot. The canonical engine build and focused checks do not replace it.
The remaining sanitizer aggregate parts and full local CLI deep-page gate
have not run.

## What is still open

Symbol.prototype[Symbol.toPrimitive] is absent. An unmodified boxed Symbol
therefore returns a display string via ordinary string-first conversion;
this is recorded as a separate open AGENTS.md task for prototype
installation, not fixed in another agent's BuiltinsObject.cpp claim.
Symbols are still backed by string keys internally; user strings can
collide with internal well-known Symbol spellings. Other built-in-specific
conversion fast paths, Proxy support and unimplemented language features
are separate. This is the shared ToPropertyKey conversion fix, not a
claim of complete Symbol semantics or overall ECMAScript conformance.
