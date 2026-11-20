# Phase 114 — `instanceof` asks `Symbol.hasInstance`

Task row: **Several built-ins still do not make the subclass they were called on**
— the `instanceof` half, and the `prototype`-less right-hand side from **What a
function is, in small**. Finding 119.

## 1. What was wrong

The operator was four lines of a chain walk that never asked the object it was
given:

```cpp
bool js_instance_of(const JsValue& value, const JsValue& constructor) {
    if (!constructor.is_function()) js_throw_type_error("Right-hand side of 'instanceof' is not callable");
    ...
    JsValue proto = constructor.get_property("prototype");
```

The language (§13.10.2) is `InstanceofOperator`: `GetMethod(rhs, @@hasInstance)`
and, if that answers, a call to it. So five things were wrong at once, and all
five were measured against Node.js before anything changed:

| probe | Node.js | this engine, before |
| :--- | :--- | :--- |
| `class A { static [Symbol.hasInstance](x) { return x === 1 } }; 1 instanceof A` | `true` | `false` |
| `var o = { [Symbol.hasInstance](x) { return x === 5 } }; 5 instanceof o` | `true` | `TypeError: Right-hand side of 'instanceof' is not callable` |
| `typeof Function.prototype[Symbol.hasInstance]` | `function` | `undefined` |
| `new F() instanceof F.bind(null)` | `true` | `false` |
| `({}) instanceof (() => {})` | `TypeError: Function has non-object prototype 'undefined' in instanceof check` | `false` |
| `1 instanceof 1` | `TypeError: Right-hand side of 'instanceof' is not an object` | `TypeError: … is not callable` |

`prototypes.dat` shows what that costs beyond six probes: the case that defines
`Symbol.hasInstance` **aborted the file**, so every case after it in the same
file was lost as well.

## 2. What changed

`src/js/JsOps.cpp`, `src/js/BuiltinsObject.cpp`, `src/js/JsValue.cpp`,
`include/litebird/js/JsOps.hpp`, `include/litebird/js/JsValue.hpp` — no other
file.

- **The operator is the standard's three steps.** A right-hand side that is not
  an object is a `TypeError`; a right-hand side that is a bound function
  delegates the *whole* operator to what it was bound from; otherwise
  `GetMethod(rhs, @@hasInstance)` is read and, when it answers, called with the
  right-hand side as `this`. Only `undefined`/`null` fall back to the ordinary
  walk, and a value that is not callable there is the "not callable"
  `TypeError`.
- **`Function.prototype[Symbol.hasInstance]` exists**, because everything a
  script sees inherits it and because the language requires `Object[Symbol.hasInstance]
  === Function.prototype[Symbol.hasInstance]`. It is `[Symbol.hasInstance]` with
  length 1, non-writable, non-enumerable and non-configurable, and it does
  OrdinaryHasInstance: a receiver that is not callable answers `false`, so
  `Function.prototype[Symbol.hasInstance].call({}, {})` is `false` as in a
  browser.
- **The ordinary case does not go through the property.** `js_instance_of`
  compares what it finds with the method `install()` left on
  `Function.prototype`, by identity, and takes the walk directly when it is the
  same function — the state of every function in the language, so `a instanceof
  Array` still costs no call. The comparison is only made when the function's
  own `[[Prototype]]` *is* `Function.prototype`: a class with a
  `static [Symbol.hasInstance]`, or a subclass that inherits one, takes the
  general path. That distinction is not a detail: the first form of this phase
  treated `class B extends A {}` as the ordinary case and answered `3
  instanceof B` — where `A` defines the method — `false`.
- **`OrdinaryHasInstance` is a function of its own** (`js_ordinary_has_instance`
  in `JsOps.hpp`), because the method on `Function.prototype` and the operator's
  fallback are the same walk and must not drift apart. It unwraps a bound
  function to its target first (step 2), then reads `prototype`.
- **A bound function remembers what it was bound from.** `JsValue::Extra` gained
  `bound_target`, set in `Function.prototype.bind`, and `JsValue::bound_target()`
  reads it. Nothing else could tell a bound function — which rightly has no
  `prototype` — from an arrow function, which rightly has none either and whose
  `instanceof` is a `TypeError`.
- **A `prototype` that is not an object.** It is a `TypeError` for a script
  function with none, which is now what an arrow function, a method and
  `Math.max` get. A *native* function with none still answers `false`: six of
  the host's constructors (`Response`, `Headers`, `Worker`, `WebSocket`,
  `IntersectionObserver`, `ResizeObserver`) are installed without a
  `prototype`, and `r instanceof Response` must not start to throw before they
  have one. They are the row in `AGENTS.md` that this phase deliberately leaves
  alone.

## 3. How it is verified

- `make test` → **273 / 273** suites (272 on `main`, plus this one). The new one,
  `instanceof_asks_symbol_has_instance`, is 19 checks compared inside the page
  against strings every one of which Node.js printed first: the method's
  presence, name, length and property descriptor, that a function does not own
  it, that `Object[Symbol.hasInstance]` is the same function, that assignment to
  it does not take; a class's static method, a plain object's own method, a
  subclass inheriting it, a function answering for itself, a getter being read;
  a bound function and a bound bound function; a throwing method reaching the
  operator and a getter being read exactly once; `null` falling back; a
  non-callable answer, a `prototype`-less script function, `1 instanceof 1` and
  `1 instanceof {}` each being a `TypeError` with the message Node gives; a
  primitive left-hand side answering `false` before `prototype` is read; the
  ordinary walk unchanged; and, on the DOM side, that
  `document.body instanceof HTMLElement`, `instanceof Node`,
  `HTMLDivElement`, `Event` and `MouseEvent` all still answer `true` and that
  `document.body instanceof Response` answers rather than throws.
- `make js-conformance` → **1142 / 1158 (98.6%)**, up from 1141: the case
  `prototypes.dat` records for `Symbol.hasInstance` passes, and the file no
  longer aborts. `prototypes.dat` itself is **69 / 76**, up from 68.
- `make dom-conformance` **678 / 678**, `make conformance` **1973 / 1973**,
  `make url-conformance` **1188 / 1188**, `make wpt` **9567 / 9824** with 257
  listed failures and none unlisted — all unchanged.
- `make metrics` and `python3 tools/ci/check_docs.py` → 0 failures.

## 4. What is still open

- **`Symbol.species` and the statics that should construct the subclass.**
  `List.from([1])`, `new List().map(f)` and `P.resolve(1)` are plain arrays and
  plain promises; `typeof Array[Symbol.species]` is `undefined`. This is the
  other half of the same `AGENTS.md` row and the three other failing cases of
  `prototypes.dat`.
- **Six host constructors have no `prototype`**, so `r instanceof Response` is
  `false` where a browser answers `true`. Giving them one is what lets the
  operator throw for every function that has none, which is what it should do.
- **`1 instanceof Math.max` is `false`** where the language throws: a native
  function with no `prototype` is exactly the case the six constructors above
  keep answering. It goes away with them.
- **A non-callable `@@hasInstance` says "Right-hand side of 'instanceof' is not
  callable"** where V8 says "number 5 is not a function"; both are `TypeError`s,
  and this engine's own call error ("expression is not a function") is further
  from V8's, so the message was left as it is.
- **`Symbol.hasInstance` is still a string in this engine** — the key
  `"@@hasInstance"`, as `Symbol.iterator` is `"@@iterator"` — so
  `"@@hasInstance" in f` is `true` where the language says `false`. That is the
  row on what an iterator still is not.
