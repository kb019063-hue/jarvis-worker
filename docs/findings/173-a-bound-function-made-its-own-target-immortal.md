# Finding 173 — a bound function made its own target immortal

## What was wrong

`Function.prototype.bind` built the bound function's entry point by capturing
the target, the bound `this` and the bound arguments in a `std::function`, and
kept a fourth copy of the target in the payload member `instanceof` reads.
None of the four was counted by the collector, which does not look inside C++
storage. It counts references it can see and treats any payload with more
owners than it found as a root:

```cpp
for (const auto& [payload, info] : m_payloads) {
    if (info.owners != info.seen) mark_payload(info);
}
```

For a bound function that number was `owners = 3, seen = 0` — the copies
inside the callable, plus `bound_target`, none of them visible. So the payload
became a **root** and never became garbage, whatever happened to the bound
function. The target, its closure and every variable the closure held stayed
alive for the life of the process.

Three cells an iteration, and no cycle needed:

```
                                  litebird (before)   after
o.b = f.bind(o)        x 2,000        +6,000            +0
o.b = f.bind(null)     x 2,000        +3,603            +0
```

The audit row said `o.b = f.bind(o)` 2,000 times left 2,000 cells. That is
the case, but it understated the count by three, and it named the cycle as the
cause when a discarded (or merely stored) bound function leaked too.

## Why it took a while to see

Three probes said the collector was fine before this one said it was not:

- `gc()` reported **no survivors**, so the cells were not being kept by a
  stray handle — they were being *marked*, which is a different stage;
- the bound function's payload was `owners = 1` at the moment `bind` returned,
  so the copies were not being made there;
- the rooted payload was the **script** function inside the bound one, not the
  bound function, so tracing "the bound function's own Extra" found nothing.

The number that settled it was the per-collection delta: one extra iteration
between collections moved `alive_cells` by exactly 3, every round, for six
rounds. A leak that is exactly linear in iterations, and that stops when the
bound function is never stored, is a payload being rooted from outside — and
`Extra::bound_target` was the one member with no `count_value` beside it.

## What changed

`Extra::captured` holds what the entry point needs, the call context carries
it, and both it and `Extra::bound_target` are counted and marked. The full
account is in Phase 168 (`docs/phases/168-a-capture-the-collector-could-not-see.md`).

## The rule this leaves behind

A value a native entry point needs after its call returns must be reachable
from the payload, not from the callable. `std::function` captures are outside
the heap by construction: a `JsValue` that lives only in one is a reference the
collector cannot count, and an uncounted reference is not an error the linker
or the compiler can catch — it is a payload that becomes a root, silently, for
ever. `JsValue::current_captured()` is the route for the ones that need this;
`current_this()` and `current_is_construct()` were already the same idea.

## False premise retired

The row this phase started from — "`set_property` does not create a property on
an object that has no property table yet" — does not reproduce at `0b637ab`.
`JsValue::set_property`'s tail creates the table:

```cpp
PropertyMap* map = loose_properties(true);
if (!map) {
    properties = std::make_shared<PropertyMap>();
    map = properties.get();
}
```

and a write to a native function, a `null`-prototype object, a class's
`prototype`, a `RegExp`, an `Error`, a promise and a bound function all read
back the value written, exactly as Node does. The hand-written `[[Set]]` in
`install_error_stack_accessor` was written against a bug that was not there;
it is left in place, because `src/js/JsOps.cpp` was claimed by another open
pull request, and should be deleted by whichever phase takes that file next.
