# Finding 273 — the pill was a promise, not a property

**Severity:** correctness of observable state. `Function.prototype` did not have
the `caller`/`arguments` pair §10.2.6 puts there; the engine answered for it
from two helpers, so every answer was right and the property did not exist.
`Object.getOwnPropertyDescriptor` was `undefined`, `delete` had nothing to
delete, `for...in` showed nothing, and a script could not redefine or remove
what it was told was there.

**Found by:** `AGENTS.md`'s own row — it names both helpers and says the pill is
absent. This phase is that row, taken at its word.

## What it was

```js
'caller' in function () {}                                    // true
Object.getOwnPropertyDescriptor(Function.prototype, 'caller')  // undefined
```

Two places answered one question each (`has_property` and
`throw_if_forbidden_property`), and a third decided which functions store the
pair (`function_stores_poison_pair`). The answers agreed with the standard; the
object the standard describes did not exist. The visible cost is not the
answers — it is everything *about* a property that a helper cannot have: its
descriptor, its enumerability, its configurability, its deletability, whether
it is own or inherited, and whether a script may replace it.

## What the fix holds

- **The pill is one thrower used as both get and set**, which is
  `%ThrowTypeError%`'s shape: `Object.getOwnPropertyDescriptor(...).get === .set`
  is `true` in a browser and now here.
- **Configurable, non-enumerable.** Configurable because §10.2.6 says so and
  because a test deletes it; non-enumerable because an inherited accessor is
  not a for-in key.
- **Inherited, never own.** `forbidden-ext` asserts
  `hasOwnProperty("caller") === false` on a strict function, so the pair goes on
  the prototype and nowhere else. Getting this backwards turns 4 passing
  `forbidden-ext` cases into failures — the row warned about it.
- **Sloppy functions shadow it, and that is not a bug.** §10.2.5 gives a sloppy
  ordinary function its own `null` pair, which the parser installs. So the
  throw is the answer for a *strict* function, a class and a generator, and
  `null` is the answer for a sloppy one — including a sloppy arrow or method,
  which surprised the first version of the suite. Node answers identically;
  the suite now records the split rather than asserting one rule for all five.
- **Deleting it takes the answers with it.** That is the test that a fiction
  cannot pass, and the one that proves the fix: with the accessor gone,
  `'caller' in f` is false and reading it is `undefined`.

## Still open

`%AsyncFunction.prototype%` and `%GeneratorFunction.prototype%` are still not
separate objects, and `js_make_native` still defines `name` before `length`
where the standard's order is the other way. Both stay in the row above.
