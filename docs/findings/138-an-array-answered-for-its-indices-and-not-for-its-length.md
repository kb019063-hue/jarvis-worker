# Finding 138 — an array answered for its indices and not for its length

Two functions walk a prototype chain, and both of them knew that an array keeps
its indices in `array_elements` rather than in its property map. Neither knew it
keeps its `length` there too.

`get_property` computes `length` from `array_elements.size()`. It does so for
the cell that is itself an array, before any chain walk, and that is right — an
array's own `length` does not need the chain. `lookup_chain` walks the
prototypes and has a branch for an index, hole check included, that Phase 98
added. Nothing in the walk answered `length`. So a read that fell through the
chain onto an array found its elements and not its length:

```js
var o = Object.create([7, 8, 9]);
o[1]        // 8   -- Phase 98 taught the walk this
o.length    // undefined -- the standard answers 3
```

The same gap was in `has_property`, whose walk has the same shape:
`"length" in o` was `false`. It was not in the task row. It came out of asking
what else reads the chain for the same answer, and it says the fault is one
fault in the shape of the walk rather than one line in one function: two walks,
two answers about the same thing, both missing.

## What made it easy to miss

`length` is *computed*, not stored. Nothing about it is in the property map, so
every question that goes through the map — a descriptor, `Object.keys`, the
own-property check, `for-in` — is right to say there is nothing there, and the
engine said so. The only thing wrong was the one question the map cannot
answer, `HasProperty`, which has to go on to the prototype like every other
inherited property. The engine's own test suite had recorded the wrong answer
and kept it on purpose ("asserted here as it is, where a change to it will be
noticed"), so the answer was not silently untested — it was tested as wrong.

## What it cost

Because `length` is inherited rather than copied, the reader does not hold a
number of its own: it reads the array the walk lands on. An array prototype that
grows or shrinks after the reader was made is a different `length` the next time
it is read, and a two-level chain reaches the array through the objects in
between. A number that had been copied to the reader at creation would have
passed the simple case and been wrong in both of those.

## What was not changed, and why that is the check

`Object.getOwnPropertyDescriptor(o, "length")` is still `undefined`,
`o.hasOwnProperty("length")` is still `false`, `Object.keys(o)` is still `[]`,
and `JSON.stringify(o)` is still `{}`. Inheriting a property and owning one are
different questions, and the fix answers the first without touching the second.
That the second is unchanged is what shows the branch went in the walk and not
in the object.

## Measuring a read

The task row said to time it. 100,000 reads of an inherited `length`, three runs
each way: 30.6–31.0 ms before and 26.4–27.5 ms after, against 24.1–26.4 ms for a
plain array's own `length` in the same runs. The read already paid for the walk;
it now answers at the end of it instead of falling off. The "before" number is
not a cost avoided — it is the same cost spent on `undefined`, and the `NaN` the
benchmark summed is the failure showing up as arithmetic.
