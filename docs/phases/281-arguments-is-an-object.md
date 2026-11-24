# Phase 281 — an arguments object is an object

Task: Phase 123's row names it — "`arguments`' prototype is `Array.prototype`
here, so its message says `#<Array>` where V8 says `#<Object>`". §10.4.4 gives
an arguments object `%Object.prototype%`. Finding 286.

## 1. What was wrong

`make_function_call` built the object with `make_array(args)` and then relabelled
it `ObjectClass::Arguments`. The label was used for `Array.isArray` and for
`Object.prototype.toString`, and the prototype — the thing every *inherited*
name is found through — stayed `Array.prototype`:

```js
Object.getPrototypeOf((function () { return arguments; })())
// was Array.prototype;  was Object.prototype

typeof (function () { return arguments; })().push
// was "function" — `push` was inherited from an array
```

and the message the row quoted:

```
Cannot delete property '0' of #<Array>      was
Cannot delete property '0' of #<Object>     a browser
```

An arguments object is an ordinary object with index properties and a `length`,
not an Array exotic object, so nothing of `Array.prototype` belongs to it.

## 2. What changed

`src/js/JsEval.cpp`, where the object is made: the prototype is
`JsIntrinsics::get().object_proto` instead of the array one.

**The prototype alone is not enough, and the first version of this change was
wrong** — it broke 75 test262 cases. An arguments object's iteration came from
`Array.prototype[@@iterator]` with the rest; with the prototype moved, `[...arguments]`,
`for…of arguments` and `yield* arguments` all stopped working
(`TypeError: [object Array] is not iterable`).

§10.4.4.7 is the missing half, and it is why the standard does not simply leave
the object with nothing: it gets its **own** `@@iterator`, and the standard
names it — *the same function object* as `%Array.prototype.values%`, which is
why `arguments[Symbol.iterator] === Array.prototype.values` is `true` in a
browser. That identity is what this installs, writable and configurable, on the
object rather than on the prototype.

## 3. What is still open

**`callee`.** §10.4.4.6 gives a sloppy arguments object its own `callee`, the
function it belongs to. This engine answers `undefined` and does not own the
name — on `main` before this phase as well as after it, so it is not a
regression and not this row's. Recorded here because the suite tried to assert
it and had to be cut back.

**Mapped arguments.** §10.4.4.2 makes a sloppy object's index properties and
the parameter bindings one thing, so `arguments[0] = 1` changes the parameter
and `a = 1` changes `arguments[0]`. This engine keeps them separate. A larger
piece, untouched here.

**Strict-mode `caller`/`arguments` on the object** and the `%ThrowTypeError%`
behaviour of a strict `callee` come with `callee` above.

## 4. How it was verified

```
make test                        All 419 engine test suites passed successfully!
                                 Cross-origin suite: 114 checks, 0 failure(s)
run_test262.py language          21326 pass (92.7%)   was 21322   +4, 0 broke
run_test262.py built-ins/Array    2774 pass (90.0%)   unchanged
run_test262.py built-ins/Function  467 pass (91.7%)   unchanged
js-conformance                   1150/1158 (99.3%)    was 1149    +1
```

The sequence is worth recording, because the middle step is the interesting
one. The prototype change alone gave `language` **21,249 — 75 broken, 2 fixed**.
With §10.4.4.7 installed the same corpus is **21,326, +4 fixed and 0 broken**.
The regression was real, was caught by measuring rather than by reasoning, and
its cause was in the standard the row already cited.

The new suite is `tests/suites/281-arguments-is-an-object.cpp`: the prototype,
`Array.isArray`, `typeof args.push`, `[object Arguments]`, the three iteration
forms, the identity of `@@iterator` with `Array.prototype.values`, the exact
`#<Object>` message, and that the places and `length` did not move.
