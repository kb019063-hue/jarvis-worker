# Phase 399 — a symbol's description is not a property of it

Task: the row "A definition names the function it makes" names this among its
still-open items — "`Symbol.prototype.description` on an undescribed symbol
(`Symbol()` answers `""` here, the language `undefined` — one line in
`BuiltinsObject.cpp`, and test262's `Symbol/prototype/description/*` is the
oracle)". It was not one line, and the row was right about the oracle. Finding
399.

## 1. What was wrong

Three faults, one root: the description was kept **in the symbol's property
table**.

```cpp
// JsValue::make_symbol
v.properties = std::make_shared<PropertyMap>();
(*v.properties)["description"] = JsValue::make_string(std::move(description));
```

That made it an own property of the symbol, which §20.4.1.1's symbol does not
have — and it is the reason the getter could not tell "no description" from
"the empty string":

```js
typeof Symbol().description                                  // was "string", Node "undefined"
Object.prototype.hasOwnProperty.call(Symbol('x'), 'description')  // was true, Node false
Object.getOwnPropertyNames(Symbol('x')).join()               // was "description", Node ""
```

The third fault followed from the same place: `Symbol()` reduced its missing
argument to an empty string *before* building the symbol, so by the time the
value existed there was nothing left to say "there was no argument".

## 2. What changed

The description moved to the value's `Extra` block — `symbol_description` and
`symbol_has_no_description` — with `symbol_description()` answering §20.4.3.2's
`undefined`, and `symbol_description_text()` the text that printing and a
property-key hint use. The flag is only set for `Symbol()` and
`Symbol(undefined)`; `Symbol("")` stores the empty string like any other.

`make_symbol` still takes the description and stores it, empty or not, so
every other maker (`Symbol.for`, the well-known symbols) is untouched.

## 3. The wrong place, twice

My first fix stored the "no description" flag **in the property table** under a
key of its own. It worked for `.description` and it made
`hasOwnProperty(Symbol(), "@@noDescription")` answer `true` — I had fixed one
observable key by adding another. The probe that caught it was written to check
exactly that, and it is the reason the flag lives in `Extra` now.

That is the second time this session that a correct-looking fix was wrong in
*where* it kept its state rather than in what it did — the Phase 388 sweep
below a shortcut that never ran, and this one inside the table a script can
enumerate. Both read as "the diagnosis was wrong" until the placement was
questioned.

## 4. What it was measured against

Node v22.22.3 answered every probe first. Two screens of probes (eighteen
answers) are identical line for line, including the four that are easy to get
wrong: `Symbol("")` keeps a `string` of length 0, `Symbol(undefined)` behaves
like `Symbol()`, `Symbol.for("k").description` is `"k"`, and
`Reflect.ownKeys(Symbol())` throws because a symbol is not an object.

## 5. The numbers

| corpus | before | now |
|---|---|---|
| test262, all of it | 15,168 (96.7%) | **15,177 (96.8%)**, +9, 0 broken |
| `built-ins/Symbol` | 65/98 | **66/98** |
| `language` | 11,163/11,328 | unchanged |
| `make test` | 453/453 | **454/454** |
| `make js-conformance` | 1,152/1,158 | 1,152/1,158 unchanged |

The description cases gained: `for/description.js`,
`prototype/description/{description-symboldescriptivestring,descriptor,get,is-not-own-property,this-val-symbol,wrapper}.js`.

`is-not-own-property.js` is the one that shows the root cause was worse than
the row said: the row called it a one-line getter fix, and a one-line getter
fix would have left that case failing.

## 6. What is still open

- `built-ins/Symbol/prototype/description/this-val-non-symbol.js` wants the
  getter to throw for a `Proxy` receiver, and there is no `Proxy` in this
  engine — it is listed as failing for that reason, not for a description one.
- `built-ins/Symbol` is 66/98. The rest are a mixture this phase did not
  diagnose; nothing in it was made worse, and the row's own claim about
  descriptions is now closed.
