# Phase 158 — a method is not a constructor

Task: the first clause of the `AGENTS.md` row **"What a function is, in
small"** — "nothing tells a built-in that is a constructor from one that is
not (`new Math.max()` makes an object)". Finding 163.

## 1. How it was found

The failure census for a change is usually read by directory, and that is the
wrong axis here: at 14,802 / 15,686 (884 failures) the biggest directories are
`built-ins/Object/defineProperty` (110) and `defineProperties` (82), and both
are what Phase 146 and the array work left behind rather than one thing.

Grouping by **the message a case failed with** is what found this one:

```
113  Expected a TypeError to be thrown but no exception was thrown at all
101  Proxy is not defined
 55  $262 is not defined
 51  isConstructor(Object.defineProperty) must return false
 51  isConstructor(Reflect.get) must return false
 51  isConstructor(Object.prototype.hasOwnProperty) must return false
```

Fifty-one cases, one question, one answer that was wrong:

| probe | this engine, before | Node 22 |
| :--- | :--- | :--- |
| `isConstructor(Object.defineProperty)` | `true` | `false` |
| `new Object.defineProperty({}, "x", {value: 1})` | `{}` | `TypeError` |
| `class X extends Math.max {}` | accepted | `TypeError` |

`isConstructor` in test262's harness is not a property read — it asks
`Reflect.construct(function () {}, [], f)` and watches for the throw, which is
the only way a script can ask "does this have `[[Construct]]`?". So the suite
was asking about the *existence of an internal method*, and the engine answered
from a flag that only two call sites ever set.

## 2. Why the fix is one line and not 300

`js_is_constructor` takes every native function for a constructor unless
`not_constructor` is set on its cell. The obvious repair is an audit: walk
every `js_define_method` call and set the bit. That is 300 sites, and it is the
wrong shape, because the standard already draws the line in one place.

A **MethodDefinition** (§15.4) makes an ordinary function object and does *not*
give it `[[Construct]]`. `Object.keys` and `{ keys() {} }` are therefore the
same kind of function: neither can be `new`ed, and `isConstructor` is `false`
for both. Constructors are a different production. In this engine every
built-in method is installed by `js_define_method`, and nothing else is:
constructors are `js_make_native` and are registered by hand. So
`js_define_method` *is* the "this is a method" declaration, and the refusal goes
there.

One place did not come through it. `Math` installs its twenty-six methods
through a local `def` lambda, and each value is built first by `math_unary` or
by a lambda of its own — which is what keeps its `name` and `length` right, so
rebuilding the value around a wrapper would have traded one property for two.
`js_mark_as_method` is the second half of `js_make_method` split out for that:
mark a function that already exists.

## 3. What my own test caught

The suite added here failed on its first run with three lines, and only one of
them was the engine's fault:

```
Math.max: got true, expected false            <- real, and the reason Math is
                                                 in this phase at all
an object method: got true, expected false    <- my test's fault
an object getter: got true, expected false    <- my test's fault
```

`{ m: function () {} }` stores a **function expression**, which is
constructible in every engine, and a getter written as `{ get x() { … } }`
already passed — what failed was my writing `get: function () { … }` inside
`defineProperty`, which is a function expression too. Node confirms the
distinction:

```
({ m() {} }).m              -> isConstructor false
({ m: function () {} }).m   -> isConstructor true
```

That is worth keeping in the test rather than deleting, because "a method is
not a constructor" is only true of the production that makes one — so the suite
now asserts both sides and says why.

## 4. The scoreboard

Two binaries, test262 at `2e58f87d`:

```
before     14802 pass (94.4%)  [fail 884]
now        14856 pass (94.7%)  [fail 830]
54 pass in now and not in before; 0 pass in before and not in now
```

Of the 51 `isConstructor` cases, all 51 pass; the one left is
`Error/isError/is-a-constructor.js`, which fails on `Error.isError` not
existing rather than on the question. The 54th case is an `instanceof`
case that the same mark answered.

Everything else unchanged: `make test` **316/316** including the suite added
here (314 before `main` was merged into the branch), `make js-conformance` **1144/1158**, `make dom-conformance` **678/678**,
`make metrics` and `check_numbers` clean.

## 5. What is still open

- **`class X extends f.bind(null) {}` is accepted.** A bound function is a
  constructor — `new (f.bind(null))()` works — but it has no `prototype` to
  extend, and the language refuses the class body for that reason. Different
  question from this phase's, and `bind` is a file of its own.
- **`Error.isError` does not exist**, so that one `not-a-constructor` case
  cannot pass yet.
- **`%AsyncFunction.prototype%` and `%GeneratorFunction.prototype%` are not
  separate objects** — the rest of the same `AGENTS.md` row, unchanged.
- **The census axis.** The two biggest directories remain
  `Object/defineProperty`/`defineProperties` at 192 cases, which is the
  array-index attribute storage rejected in Phase 154 as too deep for a
  contained change. Nothing here touches them.
