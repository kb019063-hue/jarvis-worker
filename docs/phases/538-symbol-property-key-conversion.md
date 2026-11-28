# Phase 538: object conversion can produce a Symbol property key

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

Only the fallback branch of the shared property-key helper changes.
It first performs ToPrimitive with string hint, preserves a resulting
Symbol's existing internal key, and stringifies any other primitive.
The direct string, Symbol and numeric-index fast paths remain unchanged.
No caller, interpreter, reflection builtin or property storage changes.

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
results, null-prototype key objects and direct Symbols, seven non-Symbol primitives, getter
and call exception identity, object results, noncallable hooks and
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

Broader engine evidence follows the builds.

## What is still open

Symbols are still backed by string keys internally; user strings can
collide with internal well-known Symbol spellings. Other built-in-specific
conversion fast paths, Proxy support and unimplemented language features
are separate. This is the shared ToPropertyKey conversion fix, not a
claim of complete Symbol semantics or overall ECMAScript conformance.
