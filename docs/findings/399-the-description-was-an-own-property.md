# Finding 399 — a symbol's description was one of its own properties

**Severity:** three observable faults from one wrong place to store a string.
`Symbol().description` answered `""` where the language answers `undefined`;
`Object.prototype.hasOwnProperty.call(Symbol('x'), 'description')` answered
`true` where a browser answers `false`; and `Object.getOwnPropertyNames` on a
symbol listed a property nailing a symbol to an implementation detail the
standard does not have.

**Found by:** the row "A definition names the function it makes", which names
it as its still-open item and says it is "one line in `BuiltinsObject.cpp`",
with test262's `Symbol/prototype/description/*` as the oracle.

## What it was

```cpp
// JsValue::make_symbol — the description went into the property table
v.properties = std::make_shared<PropertyMap>();
(*v.properties)["description"] = JsValue::make_string(std::move(description));
```

A property in that table *is* an own property: `ownKeys`, `hasOwnProperty` and
`getOwnPropertyNames` all read it. So the description was visible as one, and
because the table held a `std::string` there was no way to spell "no
description at all" — the empty string was the only nothing available.

```js
typeof Symbol().description                                        // "string";  Node "undefined"
Object.prototype.hasOwnProperty.call(Symbol('x'), 'description')    // true;      Node false
Object.getOwnPropertyNames(Symbol('x')).join()                      // "description";  Node ""
```

The row's estimate of one line was about the getter. A getter that answers
`undefined` when the stored string is empty would have fixed the first line and
left the other two, and it would have been wrong for `Symbol("")`, whose
description genuinely is the empty string.

## The fix, and the wrong version of it

The description moved to the value's `Extra` block: `symbol_description` plus a
`symbol_has_no_description` flag, with `symbol_description()` for §20.4.3.2's
`undefined` and `symbol_description_text()` for what printing and property-key
hints use.

The first version of *that* kept the flag in the property table under a key of
its own. It made `.description` right and made
`hasOwnProperty(Symbol(), "@@noDescription")` answer `true` — fixing one
observable key by adding another. A probe written to look for exactly that
caught it.

## The pattern worth naming

This session now has two phases whose fix was **right in what it did and wrong
in where it put it**: the Phase 388 sweep, written below a shortcut that
returned early for every shape the tests used, and this flag, written into the
table a script can enumerate. Both compiled, both looked correct, and both
changed nothing or changed the wrong thing until the placement was questioned.
In neither case was the diagnosis the problem.

## Evidence

test262 15,168 → **15,177** (+9, 0 broken); `built-ins/Symbol` 65 → 66 of 98;
`language` unchanged at 11,163; `make test` **454/454**; `js-conformance`
unchanged at 1,152/1,158. Eighteen probe answers compared line for line with
Node v22.22.3, identical.

## Still open

`this-val-non-symbol.js` needs `Proxy`, which this engine does not have — the
case is failing for that, not for a description question.
