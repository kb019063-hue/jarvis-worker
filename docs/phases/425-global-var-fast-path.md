# Phase 425 — the global `var` was one property write, not two

Task: take the next open tree-walker optimization recorded in Phase 204, finding
208. Finding 425. Agent: Qelari.

## 1. What the source does

The open item in Phase 204 described a global `var` assignment as writing a
scope binding and then writing the same value through the global object. That
is not the path this tree has. `run_program` represents a script-level `var`
as an own data property of the global object when there is no existing global
environment binding. `assign_identifier` first calls `env->try_assign`; for
that property-backed name the lookup misses, and the generic path then calls
`set_property` on the global object.

On a page, `set_property` asks `WindowExotic::set` and the global object's
`assign_hook` whether the property backs an environment binding. Both lookups
miss for this property-backed `var`, and the ordinary property store happens
once. The wasted work is the second lookup path (and, for `i++`, resolving a
`Reference` and repeating the environment walk), not a second store to a
binding. The benchmark from Phase 204 already showed the shape of the cost:
a top-level loop measured 748 ms where the same loop inside a function was
355 ms.

## 2. The fast path

`assign_identifier` now stores directly into an own, writable data property
when `env->try_assign` has found no binding and the global object is not frozen.
The global object belongs to the interpreter executing the identifier, so the
Window same-origin check is necessarily for that realm; the hooks would only
repeat the failed binding lookup before storing the same property. Accessors,
non-writable properties, and frozen globals still use `set_property`,
preserving setters and strict-mode refusals.

`eval_update` now handles a property-backed global identifier as it already
did a scope binding: one lookup, conversion, and store. It takes this path only
for writable own data properties on an unfrozen global whose current value is
a primitive with side-effect-free numeric conversion. An object conversion can
execute script that deletes or changes the property, so objects, accessors,
read-only properties, frozen globals, `with` scopes and unresolved names retain
the general `Reference` path. A global environment binding still takes the
original binding path.

The new suite, `global_var_fast_path_preserves_global_object_semantics`, checks
that identifier and `window` reads see the same writes, primitive and
side-effecting object coercions, a descriptor changed during `valueOf`,
accessor setters, the strict refusal for a non-writable global, `with` lookup,
local shadowing, and writes to a frozen global.

## 3. Measurement

`make bench-js-core` now has seven cases: the top-level global-var loop plus
the six local cases from Phase 204. Its Node.js v22.22.3 reference runs the
global source with `node -e` so the top-level `var` is global rather than
CommonJS-local. With five interleaved runs per binary, compared to the binary
built before this phase:

| case | before | Phase 425 | speedup |
| :--- | ---: | ---: | ---: |
| `global` — top-level `var` loop | 821.2 ms | 424.0 ms | **1.94×** |
| `loop` | 343.4 ms | 322.2 ms | 1.07× |
| `property` | 828.3 ms | 822.8 ms | 1.01× |
| `call` | 957.3 ms | 967.9 ms | 0.99× |
| `recursion` | 102.7 ms | 102.3 ms | 1.00× |
| `array` | 162.2 ms | 164.8 ms | 0.98× |
| `method` | 586.2 ms | 608.5 ms | 0.96× |

The global case measured 43.1 ms in Node (`9.8×` for Phase 425). The six local
cases' small changes are within this host's ±20% run-to-run movement; this
phase claims only the measured global-var speedup.

## 4. Verified

- `make test` — **478/478** engine suites; the cross-origin Window/Location
  suite — **114/114**.
- `make js-conformance` — **1153/1158**, everyday probe **90/90**, 0 crashes
  and 0 timeouts.
- `make dom-conformance` — **678/678**, everyday probe **68/68**, 0 crashes
  and 0 timeouts; `make conformance` — **1973/1973** tree-construction cases.
- `BENCH_JS_BASELINE=/tmp/litebird-before-425 make bench-js-core` — all seven
  cases completed; the global case improved **1.94×**.
- `node --version` — **v22.22.3**.
