# Phase 146 — a function's `name` and `length` were computed, not stored

Task: the `AGENTS.md` row **"A function's `name` and `length` are virtual, so
their descriptors lie"**, raised by Phase 138 when it unblocked 43 test262
cases that were sitting behind the empty-name ones. Finding 151.

## 1. What was wrong

`function_implicit_property` answered `name`, `length` and a constructor's
`prototype` out of the function payload. Nothing was in a property table, so
every question a script can ask *about* a property rather than *of* it got the
wrong answer:

| probe | Node.js 22 | this engine, before |
| :--- | :--- | :--- |
| `Object.getOwnPropertyNames(function F(a) {})` | `["length","name","arguments","caller","prototype"]` | `[]` |
| `f.hasOwnProperty("name")` | `true` | `false` |
| `f.name = "zz"; f.name` | `"F"` | `"zz"` |
| `delete g.name; g.name` | `""` | `"G"` |
| `Object.getOwnPropertyDescriptor(f,"length").writable` | `false` | `true` after `f.length = 9` |

The last two are the ones that matter, because they are not cosmetic: a
non-writable property accepted a write, and a configurable one survived a
delete. The 43 test262 cases are `verifyProperty`, which proves an attribute by
writing and then deleting — an oracle this storage could not answer at all, so
the cases failed with a message (`name descriptor should not be writable`) that
reads like a bug in `name` and is really a bug in the property map.

## 2. What changed

`make_function` installs `length` and then `name` as own properties with
`PropertyMap::Configurable` — `{ writable: false, enumerable: false,
configurable: true }`, §10.2.9 and §10.2.5 — and `function_implicit_property`
stops answering them. Insertion order *is* the standard's order here, and Node
prints `["length","name",...]`, so the two `entry()` calls are in that order
and say why.

`prototype` stays implicit and now has to be made eagerly as well, because
`getOwnPropertyNames` lists it before the first read:

```cpp
auto& entry = cell->properties.entry("prototype");
JsValue proto = JsValue::make_object();
proto.define_property("constructor", fn, PropertyMap::Hidden);
entry.second = proto;
entry.flags = PropertyMap::Writable;      // writable, non-enumerable, non-configurable
```

## 3. The pair that is not a pair

`caller` and `arguments` are the interesting case, because §10.2.4 says two
different things depending on the function:

- a **sloppy ordinary function** has them as own `null` data properties,
  neither writable nor configurable;
- a **strict** function, an arrow, a method, a generator, an async function and
  a class **must not have own properties of either name** — "must not be
  created with own properties named `caller` or `arguments`" — and reading
  either throws a `TypeError`.

The first version stored the throwing accessors as *own properties* of a strict
function, which fails the very test that asserts the absence:

```
language/expressions/function/forbidden-ext/b1/func-expr-strict-forbidden-ext-direct-access-prop-caller.js
  assert.sameValue(f.hasOwnProperty("caller"), false);
```

So the refusal belongs on the lookup, not in storage: `throw_if_forbidden_property`
runs in the function branch of `get_property`, and `has_own_property` keeps
answering only what is really stored. `HasProperty` still answers `true` for
every function — the pair exists on `%Function.prototype%` for the kinds that
do not store it — which is why `'caller' in f` is true where `f.caller` throws.

That needed one new field, `FunctionData::is_strict`: the payload did not
record whether the body was strict, and the whole rule hangs on it.

## 4. Two things that went wrong

Both were caught by a scoreboard rather than by the tests this phase was
aimed at, and both are now asserted:

**The eager `prototype` had no `constructor` back-link.** `p.constructor === P`
became `false`, and `make js-conformance` went from 1143 to 1142, naming
`functions.dat: function as constructor returns object`. The lazy path in
`function_implicit_property` had made that link for years; the new one had to
make it too.

**The caller rule was put in `has_own_property` instead of `has_property`.** It
compiled, it made `f.caller` throw, and it lost 4 test262 cases — the ones that
assert the property is *absent* rather than poisoned. `has_own_property` is the
one question that must keep answering `false`.

## 5. The scoreboard

Two binaries, test262 at `2e58f87d`:

```
before     14249 pass (90.8%)  [fail 1437]
now        14335 pass (91.4%)  [fail 1351]
86 pass in now and not in before; 0 pass in before and not in now
```

Everything else unchanged: `make test` **302/302** including the suite added
here, `make js-conformance` **1143/1158**, `make dom-conformance` **678/678**.

## 6. What is still open

- **`%Function.prototype%` has no own `caller`/`arguments`.** The standard puts
  the poison pill there, which is what makes `'caller' in f` true for classes
  and arrows. This engine answers the `HasProperty` question by fiat (in
  `has_property`) and the *read* by fiat (`throw_if_forbidden_property`), so
  the answers are right without the object existing.
- **`%AsyncFunction.prototype%` and `%GeneratorFunction.prototype%` do not
  exist** as separate objects: `Object.getPrototypeOf(async function () {})` is
  `Function.prototype`, and `Object.prototype.toString` says `[object Function]`
  for both. That is `prototypes.dat`'s case and a phase of its own.
- **A native function's order is `name,length`, not `length,name`** —
  `js_make_native` defines `name` first. One line, but it belongs with the
  native-function audit rather than here.
- **`Object.defineProperty` on an array index drops the attributes.** They are
  defined on a plain array and `getOwnPropertyDescriptor` reports all three as
  `true` whatever was asked for. That is array-index storage, which is
  `AGENTS.md`'s "an array's length is inherited" row and #163's ground, not
  this phase's.
