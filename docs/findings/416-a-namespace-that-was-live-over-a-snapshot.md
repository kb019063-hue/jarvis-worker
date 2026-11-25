# Finding 416 — a namespace that was live over a snapshot

A module namespace looked right from every direction but one. It had the right
names, they were enumerable, they were read-only, they could not be deleted,
and every one of them was an accessor — `make_live_namespace` had made sure of
that in Phase 105, so nothing in the namespace was a copied value:

```cpp
auto getter = js_make_native("get", 0, [own_bag, name](const std::vector<JsValue>&) {
    return own_bag.get_property(name);
});
ns.define_accessor(name, getter, JsValue::make_undefined(), kNamespaceFlags);
```

The indirection was there. The getter re-read `own_bag` on every access. The
bag was the problem: it had been filled by the module wrapper before the
wrapper returned, and by then each entry was already a copy.

## A live getter over a dead value

The wrapper is a function the module body is spliced into. Its last act was to
write every export into the bag it had been handed:

```cpp
wrapper += exports_name + "[" + module_quote(binding.exported_name) + "] = " +
           binding.local_name + ";\n";
```

`binding.local_name` is an identifier, and reading an identifier reads the
value it holds *now*. That was the trap: the line is correct JavaScript for a
snapshot, and it is exactly what makes an export not a binding. The module
continues to exist after the wrapper returns; its `let n` continues to be a
place; the bag held the number the place had at the end of evaluation.

So the layers were:

- the namespace — accessors, live, correct since Phase 105;
- the bag — plain properties, written once, never again;
- the module's scope — where the binding actually lived, unreachable from
  outside once the wrapper returned.

Every read went through all three and stopped at the second.

## The two mistakes a closure makes possible

Closing over the scope fixes the read. It also creates a lifetime, and a
lifetime is where the second half of this finding comes from.

An export published as a getter is a thunk that captures the module's scope.
The scope is alive as long as any thunk is reachable. So *where the exports bag
is stored* decides whether a module is ever reclaimed:

```cpp
m_interpreter.global_env()->define(exports_name, exports);   // before
```

A global is reachable for the worker's whole life, and nothing undefined that
name when the module finished. Before this work the leak was one empty bag per
module. After the getters it was one module `Scope` per module — its bindings,
its closures, everything the module declared. The suite showed it up as a
neighbour's failure:

```
`exports` left on a global      39,201 cells alive where the suite ran
`exports` returned by run()      1,626 cells alive where the suite ran
```

The fix is to stop storing it. `run()` returns a `JsValue`, so the wrapper can
`return exports;` and the caller can keep it in the `ModuleRecord`, which is
what wanted it in the first place. The `imports` bag stays a global because
it is a parameter — and it is set back to `undefined` after the call, so it
does not outlive the wrapper either.

## Why a function could not see either defect

The first probe of this phase was wrong and looked right. It imported `{ bump,
get }` and called `get()` before and after `bump()`, and answered `1, 2` on a
build where nothing was live:

```js
var before = get();   // 1
bump();
var after = get();    // 2   -- and it was 2 with the old wrapper too
```

`get` is a function declared in the exporting module. It reads `n` out of its
own scope, where the binding has been live all along. It never consults the
export bag, so it cannot distinguish an export that is a binding from one that
is a copy. Everything about module liveness has to be measured through the
*name* — `ns.n`, or a bare imported `n` — and the honest first measurement is
the one that goes through the namespace:

```
import * as ns from './counter.js'   ns.n before bump, ns.n after bump
```

## What is left

A named import is still a copy, and now for a reason that is visible:

```cpp
wrapper += "const " + binding.local_name + " = " + imports_name + "[" + slot + "];\n";
```

`imports[slot]` is the dependency's namespace, and reading it invokes the
getter, which is live — so the value read is current *at import time*. It is
then held in a `const`. Re-reading it later is not possible because there is
nothing to re-read: the name resolves to a local holding a number.

Making that live means the wrapper's body cannot treat an imported name as a
value anywhere it appears. Either every use is rewritten to re-read the
namespace, or the module's scope has import entries that resolve through the
dependency instead of holding a value. Both are changes to how the body is
generated; neither is a change to the three lines at its end that this phase
could make. The row keeps it open, with the namespace paths recorded as fixed.
