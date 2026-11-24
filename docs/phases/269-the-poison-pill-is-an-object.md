# Phase 269 — the poison pill is an object, not an answer

Task: `%Function.prototype%` does not carry the `caller`/`arguments` poison
pill; this engine answered both questions out of two helpers instead. Build the
pill as a real accessor pair and delete both fictions. Finding 273.

## 1. What was wrong

The row named the shape of it: "This engine answers the `HasProperty` question
in `has_property` and the throw in `throw_if_forbidden_property`, so both
answers are right without the object existing."

```js
Object.getOwnPropertyDescriptor(Function.prototype, 'caller');
// was undefined — the property the engine answers for does not exist

'caller' in function () {}          // true  (right)
(function () {}).caller             // throws (right)
```

Right answers, no object. That is a fiction, and a fiction cannot be deleted,
described, enumerated or redefined. Four separate places knew about it:

- `has_property` returned `true` for either name on any function;
- `get_property` called `throw_if_forbidden_property` and then returned
  `undefined` for a function that does not store the pair;
- `get_property`'s no-cell branch called the same helper;
- `throw_if_forbidden_property` itself, whose whole body was the rule.

`function_stores_poison_pair` decided which functions store it, and the parser
(`JsEval.cpp`) gave a sloppy ordinary function its own `null` data properties.

## 2. What changed

**`src/js/JsInterpreter.cpp`** installs the pair on `%Function.prototype%`, once
per realm, beside the `constructor` back-link:

- one thrower, a native function that throws the standard's TypeError and is
  marked as a method (it is not a constructor);
- `define_accessor("caller", thrower, thrower, Configurable)` and the same for
  `arguments` — a **configurable, non-enumerable** accessor whose get and set
  are the same function, which is §10.2.6's `%ThrowTypeError%` shape;
- it is **inherited, never own**: `forbidden-ext` asserts
  `hasOwnProperty("caller") === false` on a strict function, and every function
  reaches the pair through its prototype.

**`src/js/JsValue.cpp`** — the four fictions deleted: the `has_property` early
return, both `throw_if_forbidden_property` calls and the `undefined` that
followed one of them, and the two helper functions themselves (24 lines). The
lookup now finds the accessor through the ordinary chain walk, runs its getter,
and the getter throws. No function in that file mentions `caller` any more.

A sloppy ordinary function still shadows the pair with its own `null` data
properties, installed by the parser — that is §10.2.5, and it is the matching
half: `f.caller` is `null` for a sloppy function and a TypeError for a strict
one, from the same object.

## 3. What is still open

The row's second half is untouched: `%AsyncFunction.prototype%` and
`%GeneratorFunction.prototype%` are not separate objects
(`Object.getPrototypeOf(async function () {})` is `Function.prototype`, and
`Object.prototype.toString` says `[object Function]` for both, where
`%Function.prototype%` is still wanted), and a native function lists
`name,length` where the standard's order is `length,name` because
`js_make_native` defines `name` first. Both are separate rows' business.

One asymmetry worth naming, because the suite had to be written around it: an
arrow or a method function written in **sloppy** code gets its own `null` pair
from the parser, so it does not reach the pill. Node answers identically
(`false/false,true/true,false/false,true/true,false/false` for
strict/arrow/class/method/generator) — checked, not assumed, and the suite
records which is which.

## 4. How it was verified

```
make test                        All 408 engine test suites passed successfully!
                                 Cross-origin suite: 114 checks, 0 failure(s)
run_test262.py language          21283 pass (92.5%)   was 21269   +14, 0 broke
run_test262.py built-ins/Function 467 pass (91.7%)    was  459   + 8, 0 broke
js-conformance                   1149/1158 (99.2%)    unchanged
bench-js-core                    total 2898.8ms; ratio to Node median 13.2x
```

The nine probes that were compared field for field with Node v22.22.3, all
identical: `hasOwnProperty` on `Function.prototype`, the descriptor's `get ===
set`, `enumerable` false and `configurable` true, `'caller' in f` true, the
throw for a strict function, a class and a generator, the `null` a sloppy
function reads, and that `delete Function.prototype.caller` succeeds and takes
the answers with it.

The eight `built-ins/Function` cases include the two this row named:
`Function/prototype/caller-arguments/accessor-properties.js` and
`Function/StrictFunction_restricted-properties.js`. The fourteen `language`
cases are the `13.2-*` family and the five `restricted-properties.js` files.

The new suite is `tests/suites/269-the-poison-pill-is-an-object.cpp`. It took
three corrections to get right, each time because the *test* was wrong and the
engine matched Node: the sloppy arrow/method pair, the sloppy function reading
`null` rather than throwing, and the expected string in the first form.
