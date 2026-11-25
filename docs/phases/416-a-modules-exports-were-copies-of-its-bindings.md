# Phase 416 — a module's exports were copies of its bindings

*Finding 416.* A module's `export let n` was published as `exports.n = n`, a
copy of the value `n` had when the module body finished. An importer reading
`n` again after the module changed it went on reading the copy. Measured on
`8603d64e`, a module worker whose counter module exports the binding and a
function that changes it:

```
import * as ns from './counter.js'      {"before":1,"after":1}   (Node: 1, 2)
export * from './counter.js'            {"before":1,"after":1}   (Node: 1, 2)
import { n, bump } from './counter.js'  {"before":1,"after":1}   (Node: 1, 2)
```

ECMA-262 §16.2.1.6 makes a module namespace answer for the *binding*. A
binding is not a value: it is a place a name resolves to, and two modules that
name it are looking at one place.

## What the wrapper did

`WorkerModuleLoader::evaluate_source` (`src/js/Worker.cpp`) builds a function
around each module's body and passes it a bag of imports and a bag of exports:

```cpp
wrapper += "const " + binding.local_name + " = " + imports_name + "[" + ... + "];\n";
...
wrapper += exports_name + "[" + module_quote(binding.exported_name) + "] = " +
           binding.local_name + ";\n";
```

Both lines copy. The import line copies the dependency's value into a `const`
local; the export line copies this module's local into the exports bag. Phase
105 had already made the *namespace* live — `make_live_namespace` gives every
name a getter, and the getter re-reads the bag on each access — but a getter
over a snapshot answers the snapshot for ever. The seam was between the getter
and the bag, not in the getter.

## Two halves, and only one of them was obvious

The export side is the one the measurement points at, and it is a getter closed
over the module's own scope:

```cpp
wrapper += "Object.defineProperty(exports, " + module_quote(binding.exported_name) + ", {" +
           " get: function(){ return " + binding.local_name + "; },"
           " enumerable: true, configurable: true });\n";
```

The wrapper returns, the module's scope would have gone, and the getter is what
keeps it: the thunk is a closure over `local_name`, so `exports.n` answers the
binding as it is now. That alone turns the first two measurements to `1, 2`.

The second half was found by the suite rather than by the probe, and it is the
reason this phase touched the harness at all. `exports` used to be a **global**:

```cpp
m_interpreter.global_env()->define(exports_name, exports);
```

Nothing ever undefined it. Before this phase that leaked one empty bag per
module; with the getter above it leaks the module's entire scope per module,
for the worker's whole life, because the bag holds the only reference to every
thunk, and every thunk holds its module's scope. The heap says so:

```
exports on a global          alive after the suite    39,201 cells
exports returned by run()    alive after the suite     1,626 cells
```

`run()` returns a value, so the wrapper ends with `return exports;` and the
caller takes it. `imports` stays a global, because a function's parameters are
the only way to pass a value into the wrapper and that name is not captured by
anything that outlives the call: it is set to `undefined` straight after.

## What is still not live

A **named import** is still a copy. `const n = imports[slot]` reads the
dependency's namespace once, at import, and the local is a `const`. The
namespace paths — `import * as ns`, and `export *` through it — are the ones
this phase fixed:

| path | before | after |
| --- | --- | --- |
| `import * as ns from './counter.js'` | `{"before":1,"after":1}` | `{"before":1,"after":2}` |
| `export *` then `import * as ns` | `{"before":1,"after":1}` | `{"before":1,"after":2}` |
| `import { n, bump }` | `{"before":1,"after":1}` | `{"before":1,"after":1}` |

The named path needs the importer's local to *be* the binding, which means
something other than a `const` holding a value: a thunk the compiler emits for
every use of an imported name, or a scope whose imports resolve lazily. That
is a change to how the wrapper's body is generated, not to two lines at its
end, and it is left open on the row.

A test that calls a function to read the value cannot see any of this: `get()`
reads `n` out of its own scope and answers the new number whether or not the
export is live. The suite reads the *name* — `ns.n` — for that reason, and
says so where it does.

## The suite, and the heap

`tests/suites/416-module-bindings-are-live.cpp` builds one exporting module and
two importers in a temporary origin root, runs them as `type: 'module'`
workers, and asserts both namespace paths answer `{"before":1,"after":2}`.
Reverted to the old wrapper it fails on `direct`; with the wrapper above, all
471 suites pass.

It is a suite that spawns workers, and the run had one more thing to say about
that. `automatic_gc_reclaims_churn_without_calling_gc` sizes its expectation
from the live heap, and `Heap::reset_auto_gc_after_collect_locked` puts the
next threshold at `max(min_threshold, alive_at_that_collection)`. A module
worker needs a `Document` to resolve its imports against, the document and its
cells belong to the main agent, and this test allocates on the main agent and
expects 12,000 allocations to cross the threshold. They do not, once a worker
suite has grown the live heap, so the test failed — not on the engine's
behaviour but on what ran before it.

That test now sets the floor before it measures, which is what it always meant:
`heap.set_auto_gc_min_threshold(Heap::kDefaultAutoGcMinThreshold)` resets the
threshold and the count. It is the one line of `tests/test_suite.cpp` this
phase changed, and it makes the test independent of its neighbours rather than
dependent on an accident of ordering.
