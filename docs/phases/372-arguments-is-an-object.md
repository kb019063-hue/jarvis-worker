# Phase 372 — `arguments` is an object with one binding per place

*Finding 372.* The `arguments` object was an array of the call's values with no
`callee`, and none of its places was connected to the parameter it came from.
This phase makes it the mapped arguments exotic object §10.4.4 describes.

## The defect, in five lines

```
function f(a) { arguments[0] = 42; return a; }      f(1)  →  1   (Node 42)
function g(a) { a = 99; return arguments[0]; }      g(1)  →  1   (Node 99)
typeof f().callee                                        →  undefined   (Node function)
arguments.callee === f                                   →  false       (Node true)
```

Two directions of one missing binding, and a property that was never made. (The
strict case was already right and stays right: a strict function's parameters
and its `arguments` are separate from the first line.)

## What it is now

`MappedArguments` (in `JsEval.cpp`, beside `bind_parameters`) is a `JsExotic`
holding the call's environment and one parameter **name** per place — empty for a
place past the parameter count. Every access resolves the name with
`JsEnvironment::find_own`, so the place and the parameter are one binding rather
than two copies kept in step. Names and not pointers because:

- the object is built **before** `bind_parameters` binds anything, so there is
  nothing to point at yet;
- the elements are a dense `std::deque<JsValue>` and a binding lives in the
  environment — different storage with different lifetimes;
- a parameter's binding is never removed for the duration of the call, so the
  name is a stable handle.

It is installed only where §10.4.4 says a mapping exists:
`!is_strict && simple_params && !is_arrow && !is_method && !is_generator &&
!is_async`. `callee` is an own property in the same gate's sloppy half,
`Writable | Configurable` and non-enumerable, holding the function when there is
one to hold.

## Where the work actually went

Four defects, three of them found only by running Node and test262, not by
reading the code:

1. **The read direction bypassed the mapping entirely.** `eval_arguments` has a
   fast path for `base[number]` that answers the element without calling
   `get_property`, and `get_property` is where `exotic->get` is asked. The
   mapping was installed correctly and never consulted. A debug print inside
   `get_property` proved that the lookup was not being reached at all. The path
   now requires `!base.object_val->exotic`.

2. **A define was applied twice**, because asking the host object had
   consequences: `define_from_descriptor` called `defining` as a question and
   then the table write called it again, and by then the place had been given up
   by the first call. Split into `owns_place` (a question) and `defining` (the
   change).

3. **The spec and Node disagree, and Node wins.** §10.6 step 5.b.i unmaps a
   mapped place when the descriptor's value differs from the parameter's. Node
   does not: `{value: 5}` writes 5 into the parameter and keeps the mapping, so a
   later `a = 9` reads 9 through `arguments[0]`. Two independent readings of the
   spec agreed with each other and neither agreed with the engine. The engine now
   follows Node, and the finding records the table of what each descriptor does.

4. **`remove` has to let `[[Delete]]` run.** Returning "handled, deleted" left
   the element in place: `0 in arguments` stayed true and `Object.keys` still
   listed it.

## The shape of a defined place

The one thing that is not obvious in the code. A place that has been defined on
has attributes the dense elements cannot carry, so it becomes a hole with an
entry in the property table for its flags — and because the place is still mapped,
`set` mirrors each write of the parameter into that entry, and the entry's value
stays a placeholder. Reads come from the binding, so the descriptor read-back
(`host_value`) asks the host object for the value rather than trusting the table.

A place that has *not* been defined on stays a plain element and pays nothing.

## Measured

Differential, two builds of the same tree differing only in this change:

| | before | after | |
|---|---|---|---|
| test262, full suite | 15,147 / 15,686 | **15,149 / 15,686** | **+2** |
| engine suites | 441 | **442** | +1 |

`0 pass in before and not in now` on the full suite, run three times as the code
settled. (The committed baseline was 15,143; the merge from `main` this branch
carries brought other phases' fixes and moved it to 15,147. The comparison above
is same-tree, so the +2 is this phase's.)

The gain is small because the row this came from is not the row it looked like.
The 143 test262 cases that name `callee` are nearly all about *strict*`arguments`,
which was already correct here; and the 190 `defineProperty` cases in the
neighbouring row were being lost for a reason this phase had to fix but not for
the reason the row's title suggests — three of them were the double-apply above.

## What is still open

`Object.getOwnPropertyDescriptor` on a mapped place answers from the binding, but
`Object.freeze(arguments)` and the integrity-level tests over a mapped object are
not exercised by this phase — the `[[PreventExtensions]]` / `[[DefineOwnProperty]]`
interaction on a still-mapped place has no test262 case that reaches it here and
is recorded rather than guessed at. The `callee` of a function called through
`Function.prototype.call` with a bound `this` is correct; `arguments` of an
`eval`-declared function is Phase 199's row, untouched.
