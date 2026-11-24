# Finding 372 — `arguments` was a copy

`arguments` was the call's values, copied into an array-shaped object. Every
place in it was a second copy of a parameter, so the two could not be one
thing. The five divergences this phase started from:

```
function f(a) { arguments[0] = 42; return a; }        f(1)   → 1     (Node 42)
function g(a) { a = 99; return arguments[0]; }        g(1)   → 1     (Node 99)
function h(a) { a = 99; return arguments[0]; }        strict → 1     (correct)
typeof f().callee                                            → undefined (Node function)
arguments.callee === f                                       → false (Node true)
```

§10.4.4 makes a sloppy function's arguments object a **mapped** arguments
exotic object: for each simple parameter there is one binding, and the object's
place and the parameter are that same binding seen twice. The call's values are
the *initial* values, written into both at the start of the call, and after that
they move together.

## Why it is by name

The obvious implementation is a pair of slots per place — the element and a
pointer to the parameter's storage. Neither exists here in a form that can be
pointed at:

- `JsValue` elements are dense (`array_elements`, a `std::deque` of values), and
  a parameter's storage is a `JsEnvironment::Binding` inside the call's
  environment. They are different objects in different lifetimes;
- the arguments object is built **before** `bind_parameters` runs, so at the
  moment the object exists there is nothing to point at yet;
- and a parameter's binding is never removed from its environment for the
  duration of the call, so a name is a stable handle on it.

So the mapping stores the parameter's **name** per place and resolves it through
`JsEnvironment::find_own` on every access. `arguments[0] = 42` writes the
binding; `a = 99` is the binding, and the read through `arguments[0]` finds 99.
There is no second copy to keep in step, which is what the phase is for.

`MappedArguments` is a `JsExotic` (the engine's host-object hook) and implements
the five operations the object needs: `get`, `set`, `has`, `remove`, `keys`. A
place past the parameter count has an empty name and is an ordinary element;
`arguments[2]` in `function (a, b)` is not mapped, which is §10.4.4.2 step 8.

## Two bugs found under the first one

**The read direction never reached the mapping.** The write direction worked
immediately (`arguments[0] = 42; return a` answered 42) and the read direction
did not (`a = 99; return arguments[0]` answered 1), with the exotic installed and
correct. `JsValue::get_property` already asks `exotic->get` before it reads
array elements. The bypass was one level up: `eval_arguments` has a fast path
for `base[number]` on an array-like, and it returned the element directly
without calling `get_property` at all. A `fprintf` in `get_property` proved it —
the fast path answered and the lookup never ran. The path now also requires
`!base.object_val->exotic`.

**A define was applied twice.** Defining a descriptor on a place had to *unmap*
it: `Object.defineProperty(arguments, "0", { writable: false })`, and an
accessor, both leave the place no longer following its parameter. The first
attempt asked the host object by *calling* it from the array shortcut in
`define_from_descriptor` and then called it again on the way to the table. Asking
had consequences, and the second call saw a place the first had already given up,
so the descriptor was applied to the wrong thing. The question is now a separate,
consequence-free virtual (`owns_place`), and applying is a separate one
(`defining`).

## What a descriptor does to a mapped place

Measured against Node, not reasoned from the spec: the spec's
`[[DefineOwnProperty]]` step 5.b.i unmaps a place whose descriptor value differs
from the parameter's, and **Node does not do that** — it writes the value into
the parameter and keeps the mapping:

```
Object.defineProperty(arguments, "0", {value: 5})   then a = 9   →  arguments[0] is 9
```

This was the most expensive wrong turn of the phase, because two readings of the
spec agree with each other and neither agrees with the engine. What Node does,
and what this engine now does:

| descriptor | the place |
|---|---|
| `{value: v}` | stays mapped; `v` is written into the parameter |
| `{enumerable: false}`, `{configurable: false}` | stays mapped; the attribute is the place's |
| `{writable: false}` | **unmaps**, keeping the value it had |
| `{get}`, `{set}` | **unmaps**; the accessor answers |

Which is what the three attribute-only descriptors being *kept* costs: the
attributes cannot live in the dense elements, so such a place becomes a hole with
an entry in the property table for its attributes, and `set` mirrors each write
of the parameter into that entry. The entry's value is a placeholder and reads
still come from the binding, which is why the descriptor read-back asks the host
object for the value (`host_value`).

Six test262 files (`15.2.3.6-4-291-1` and its relatives) and three in
`defineProperties` are exactly this: a `defineProperty` with an accessor or a
`writable: false` on a mapped place, then a check that the place no longer
follows its parameter.

## Two smaller things

`delete arguments[0]` must let the ordinary `[[Delete]]` run: the mapping drops
that place (returning `false` from `remove`) and the element really goes, so
`0 in arguments` is false, `Object.keys` is empty of it, and a later `a = 5`
does not reappear.

`element_there` in `define_from_descriptor` briefly grew an
`&& !value.is_undefined()` clause while chasing a stale element. That was wrong on
its face — an element legitimately holding `undefined` would stop being one —
and the real cause was a map entry being written under a placeholder key. It is
reverted; the value of an element is never a test for its existence.

## Counted

Differential, two `make all` builds of one tree (`before` is the tree without
this change, `now` is with it):

- full test262 at `2e58f87d8350`: 15,147/15,686 → **15,149/15,686**, `+2`,
  **0 lost** (the merge from `main` this branch carries is what moved the
  baseline from 15,143 to 15,147; the delta is this phase's)
- `make test`: **442** engine suites, from 441
