# Finding 159 — the oracle was a directory, not a reference implementation

## What was seen

`built-ins/Error/prototype/stack` in test262: 35 files, 35 failures. That is
the best signal the suite gives, because it means the directory has one cause.
A cluster where 20 of 35 pass is a pile of separate bugs; all 35 is a property
that does not exist.

## The wrong oracle

The obvious move was to reproduce the bug against Node 22 and read the shape
off the output. That fails immediately and in an instructive way:

```
$ node -e 'console.log(Object.getOwnPropertyDescriptor(Error.prototype, "stack"))'
undefined
```

V8 keeps no `stack` on the prototype at all. It installs one *per instance*,
at construction, as an own property. So Node is not merely a slightly different
implementation here — for this property it disagrees with the suite the two of
them are supposed to share. Had the phase been driven by Node, the "fix" would
have been to keep the own property that was already there.

The reason is worth stating: `stack` is not in ECMA-262. It is a
de-facto property with an agreed shape that engines are converging on, and
test262 records the target rather than any one engine's history. When the
property is not in the standard, a directory of tests is a better oracle than
the engine that happens to be installed, because the directory is the
specification and the engine is one implementation of it.

## What the directory said

Twenty-one `setter-*` files and eleven `getter-*` files, and between them they
pin down steps no summary would mention:

- `setter-non-string-value` — a non-string throws, so the old
  `err.stack = null` idiom (V8's way of releasing the stack) is now a
  `TypeError`.
- `setter-this-not-object` — a primitive receiver throws, and it lists
  `true`, `false`, `1`, `''`, `Symbol()`, `BigInt(0)` separately.
- `getter-data-property-shadows` — the getter answers from the internal slot
  even when an own data property has hidden it. That test is the statement
  that the property and the slot are two different things.
- `setter-receiver-is-other-prototype` — `TypeError.prototype.stack = 'x'`
  works, but `Error.prototype.stack = 'x'` throws.
- `setter-non-writable-stack` — a frozen instance with a non-writable own
  `stack` throws, which is Set with Throw=true and not `Reflect.set`.

Reading all 35 before writing any code is what made the change one edit. The
first attempt — the two obvious behaviours, get from the slot and set
an own property — passed 24 of them and failed on the two above.

## The bug the tests did not name

`set.call(TypeError.prototype, 'sentinel')` recursed until the stack blew
(`RangeError: Maximum call stack size exceeded`). Nothing in the engine was
wrong in isolation: `set_property` walks the prototype chain for an inherited
setter and runs it, which is correct for ordinary syntax, and the setter's own
write went back through `set_property`, which found the same inherited setter —
itself. The spec's `SetterThatIgnoresPrototypeProperties` exists precisely to
avoid that, and its name is the whole point: it sets on the receiver and does
not consult the prototype chain at all.

It was found by running the file, not by reading it. The file says
`set.call(proto, 'sentinel'); verifyProperty(proto, 'stack', {...})` and looks
like nothing much.
