# Phase 102 — a pattern asked an array instead of its iterator

Phase 63 taught every loop in the engine to ask a value for its
`Symbol.iterator` before reading it: `for…of`, spread, `yield*`, `Array.from`,
`Map`/`Set`, the four `Promise` combinators, template tags. It left one reader
out, and it left it out for a reason that had nothing to do with the language:
`src/js/JsEval.cpp` belonged to two open pull requests at the time (#68 and #71),
so the file that binds an array pattern could not be touched.

`IterationCursor` is what an array pattern binds from. It had three modes and
chose between them like this:

```cpp
if (source.is_array())            m_mode = Mode::Array;    // read the elements
else if (source.type == String)   m_mode = Mode::Buffered; // every code point, up front
else                              /* the protocol, or an object with a next */
```

The first line is the bug, and it is the shape of bug this repository keeps
finding: a question answered by the representation (`is_array()`) that has a
correct answer elsewhere (`js_iterates_as_builtin()`, `JsOps.hpp`). Everything
the first line made true was wrong in the language, and three of them were
visible from a page:

```js
var a = [1, 2]; a[Symbol.iterator] = function* () { yield 9; yield 8; };
var [x] = a;                        // 1 here, 9 in Node.js

delete Array.prototype[Symbol.iterator];
var [y] = [1];                      // 1 here, a TypeError in Node.js

var [z] = { next() { return { value: 3, done: false }; } };
                                    // 3 here, a TypeError in Node.js
```

The third was the one that made the bug easy to miss: the object *looks* like an
iterator and this engine took it in place of one, where a pattern asks
GetIterator and nothing else. A parameter (`function f([p, q])`), a `catch`
binding, a `for (var [a] of …)` head and an assignment pattern all reach the
same function, so all four were wrong together.

## What a pattern is supposed to do

ArrayBindingPattern and ArrayAssignmentPattern both begin with GetIterator
(ECMA-262 8.5.2 / 13.15.5.3), and their only use of an array is through it. So
the rule is the one every other loop already follows — ask
`js_iterates_as_builtin()`, read directly only when it says the read is what the
iterator would give, and otherwise follow the protocol — and there is nothing
about a pattern that should answer it differently.

`IterationCursor` asks it now. Three modes became:

| mode | when | what it binds |
| :--- | :--- | :--- |
| `Array` | `js_iterates_as_builtin()` on an array: the installed method, and the installed `next` | the elements, holes as `undefined` |
| `Units` | `js_iterates_as_builtin()` on a string | its code points, walked in place |
| `Protocol` | everything else with a method | what `next()` yields, `done` and all |

and everything else without one is `TypeError: … is not iterable`, including an
object that only has `next`.

## Two things the old code also got wrong

**A string was copied whole.** `Mode::Buffered` built `js_iterable_to_vector()` of
every code point before binding the first name, so `var [first] = hugeString`
walked a million code points to bind one. It walks UTF-16 units now, two at a
time across a surrogate pair, which is what the string iterator yields — and a
lone half is one code point of its own, as `'\uD83D'` is in the language.

**`close()` swallowed too much and checked too little.** IteratorClose
(7.4.11) says: if `return` is undefined or null, do nothing; if it is not
callable, `TypeError`; if it gives back a non-object, `TypeError`; if it throws,
that error replaces whatever ended the loop. The old `close()` tested
`is_function()` and called it inside no `try`, so a `return` of `5` was silently
skipped (a `TypeError` in the language) and a `return` of `{}` returning `5` was
accepted. It is the same three checks `close_iterator()` in `JsOps.cpp` makes,
and now this one makes them too.

`return()` throwing is the one deliberate difference from `JsOps.cpp`, which
drops it: there the loop is already unwinding with an error of the script's own,
which is what a script sees. A pattern that binds fewer names than the iterator
holds has not failed, so an error out of `return()` is the script's error and is
thrown.

## Verification

```bash
python3 tools/conformance/run_js_conformance.py --filter iteration
# 79 / 84 before, 79 / 84 after — and 84 / 84 on the two cases this names:
python3 tools/conformance/run_js_conformance.py --filter iteration --reference "node -e"
# iteration.dat   84/84   100.0%

make js-conformance
# JavaScript corpus: 1137 / 1156 (98.4%) before, 1141 / 1158 (98.5%) after
# Everyday probe (fixed 90 cases): 90/90 (100.0%), before and after

./build/test_suite
# 262 engine test suites, the new one being
# destructuring_asks_the_value_for_its_iterator
```

The two cases that changed are the two `iteration.dat` recorded as failing for
this row — "array destructuring asks the array for its iterator" and "array
destructuring: no iterator on `Array.prototype`, or an object that only has
`next`" — and the diff of the failing-case list before and after is those two
lines and nothing else. Two cases were added for what the old code never asked:
surrogate pairs and lone halves through a pattern, and what `return` must be for
`close()` to accept it. `--reference "node -e"` runs both under Node.js; the
expectation of the first was corrected to Node's (`'2 | z | '`, an elision, not
`'2 | z | undefined'`) before it was recorded.

The new unit suite's script was run under Node.js first and prints the same
answers there, with one line said differently in a comment: this engine's
`for…of` accepts an object that only has `next`, which Node does not, and
nothing in the suite asserts either way about it.

## What is still open

- **`for…of` takes an object that only has `next`.** `js_iterate()` falls back to
  `method_is_installed()` and then to a bare `{next}` in the same way the old
  cursor did. Node.js refuses it. That is a row of `AGENTS.md` of its own; this
  phase changed the pattern, not the loop.
- **`Symbol` is still a string.** A pattern asks `"@@iterator"` because that is
  the key this engine stores `Symbol.iterator` under, and
  `"@@iterator" in []` is `true` where the language says `false`.
- **`arguments` has no `Symbol.iterator` of its own** and an object whose
  prototype is an array is not iterable, so
  `var [a] = Object.create([1, 2])` is not the `1` it would be. Both are the
  same missing prototype walk, and both are rows of `AGENTS.md`.
- **A destructuring pattern still does not drain the iterator with `return`
  when the pattern is empty and the iterator is infinite**: `var [] = gen()`
  binds nothing and closes at once, which is right; but `[...rest]` on an
  infinite generator does not end, which no engine promises either.
