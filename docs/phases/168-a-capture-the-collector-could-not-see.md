# Phase 168 — a capture the collector could not see

Task: the GC leak audit row's own example. `o.b = f.bind(o)`, 2,000 times,
left 2,000 cells behind after `gc()`. It left more than that. Finding 173.

## 1. The row's example, and three cells an iteration

The audit row named one case and its number:

```
o.b = f.bind(o)  x 2,000   ->   2,000 cells left after gc()
```

Measured on `main` at `0b637ab` with the engine's own counter, beside a
control that makes the same allocations without `bind`:

| shape | cells left after `gc()` |
| :--- | ---: |
| `o.b = f;` (control) | **+0** for 20,000 iterations |
| `o.b = f.bind(o);` | **+60,000** for 20,000 iterations |
| `o.b = f.bind(null);`, no cycle at all | **+36,000** for 20,000 |

So it was **three cells per iteration**, and the cycle was not the cause: a
bound function that simply became garbage leaked its target too. One
extra iteration between collections moved `alive_cells` by exactly 3, every
time, for as long as the loop ran.

The cycle only made the third cell visible. A bound function is a native
function whose entry point captures its target; the target's closure holds a
scope, and the scope holds the `o` the bound function was `bind`-ed to.

## 2. Why: a `std::function` capture is invisible, and it counted as an owner

The collector does not scan the native stack or C++ storage. It counts the
references it can see — inside cells, scopes and payloads — and compares each
total with the number of references that exist. Whatever has more references
than the heap can account for is held from outside, and is a root. That is
sound as long as everything that stores a `JsValue` on the heap's behalf is
either counted or left uncounted on purpose.

`Function.prototype.bind` broke that rule. It built its entry point the
obvious way:

```cpp
JsValue bound = JsValue::make_native_function([target, bound_this, bound_args](Args call_args) {
    ...
    return target.call_with_this(bound_this, all);
});
bound.set_bound_target(target);      // a second copy, unaccounted for
```

Three copies of the target's payload were then in play, and the collector
could find **none** of them:

- `target` inside the `std::function`, which `trace`/`mark_payload` cannot
  look into;
- `bound_this` and `bound_args` beside it;
- `Extra::bound_target`, the payload member `instanceof` reads, which was
  never counted either.

`count_value` then recorded, for that payload, `owners = 3` and `seen = 0`,
and stage 2 of the collection — `if (info.owners != info.seen) mark_payload`
— made the dead target a **root**, together with its closure and everything
the closure held. Every collection re-rooted it; nothing ever released it.

The row and the note on it were therefore wrong about the mechanism, and this
is the correction: the defect was not that a write to an object with no
property table was dropped (`JsValue::set_property` has had a table-creating
tail since Phase 154 and the probe confirms a native function accepts the
write), it was that a payload's invisible references made it immortal. The
`Error.prototype.stack` setter's hand-written `[[Set]]` was written to work
around a bug that was not there.

## 3. The fix: keep what the entry point needs where the trace can find it

`Extra` gained `captured`, a `std::shared_ptr<std::vector<JsValue>>`, and the
call context carries it to the call:

```cpp
struct CallContext {
    ...
    const std::vector<JsValue>* captured{nullptr};
};

// CallContextScope's constructor
t_call_context.captured = callee ? callee->payload_captured() : nullptr;
```

A native entry point reads its arguments out of `JsValue::current_captured()`
for the duration of its call — the same route `current_this()` and
`current_is_construct()` already take, so a capture that needs a value the
collector must see is no longer the only option. `bind` stores its four
values in the order it reads them: **target, bound `this`, then the arguments
already bound.**

`Heap::count_value` counts `payload_captured()` and `payload_bound_target()`
like any other reference, and `Heap::mark_payload` marks them. `Extra`'s
destructor and the release queue had to learn about `captured` as well, or a
payload that owns nothing but a capture would have skipped the deferred
release path.

## 4. Result

| shape | before | after |
| :--- | ---: | ---: |
| `o.b = f.bind(o)`, 2,000 | +6,000 | **+0** |
| `o.b = f.bind(null)`, 2,000 | +3,603 | **+0** |
| `f.bind({})` discarded, 2,000 | +0 | +0 |
| one iteration between collections | +3 per round | **+0 per round** |

`bind` itself is unchanged in behaviour: `name`, `length` (including the
`max(0, …)` clamp), the bound arguments prepended to the call's, `new` on a
bound constructor ignoring the bound `this`, `new.target` arriving as the
target, `instanceof` on either function, binding a bound function, and
binding a class. Eleven such expressions produce the same string in this
engine as in Node.js v22.22.3, and the suite asserts that string.

## 5. What this does not fix

A promise reaction leaks by the same mechanism and is left as work: a
`then` handler is a `JsValue` in `PromiseData`, and `Promise.resolve(1).then(f)`
in a loop still costs two cells an iteration (`then(...).then(...)` costs
four). A settled `new Promise` with no handler is clean (+0), so what leaks
is the reaction list, not the promise. Phase 145's row already names promise
reactions as storage the collector must not count; the fix is the same shape
as this one, and belongs beside `PromiseData` rather than in `bind`.
