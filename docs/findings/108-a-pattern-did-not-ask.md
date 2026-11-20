# Finding 108 — the pattern read the array and never asked it

**Status: fixed** (Phase 102).

`IterationCursor` (`src/js/JsEval.cpp`) chose its array mode with `is_array()`.
An array pattern therefore bound the elements of an array without asking for
`Symbol.iterator`, and three separate faults came out of that one line:

| written | this engine gave | the language gives | test262 / corpus |
| :--- | :--- | :--- | :--- |
| `var a = [1,2]; a[Symbol.iterator] = function*(){yield 9;}; var [x] = a` | `1` | `9` | `iteration.dat`, "array destructuring asks the array for its iterator" |
| `delete Array.prototype[Symbol.iterator]; var [x] = [1]` | `1` | `TypeError` | `iteration.dat`, the same row's second half |
| `var [x] = { next(){ return {value:3,done:false}; } }` | `3` | `TypeError` | `iteration.dat`, "…or an object that only has `next`" |

A parameter (`function f([p, q])`), an assignment pattern (`[x] = a`), and the
pattern in `catch`/`for` heads reach the same cursor, so all of them were wrong
at once.

## What else the same function did

- **It checked `close()` less than IteratorClose does.** A `return` of `5` was
  skipped where the language makes it a `TypeError`; a `return` that gave back a
  primitive was accepted. It also ran `return()` inside no `try`, so a `return`
  that threw was dropped.
- **It copied a string whole.** `Mode::Buffered` materialised every code point of
  a string into a `std::vector` before binding the first name, so
  `var [first] = longString` cost the length of the string.

## Why it was left

`AGENTS.md` recorded the fix and the file it was in, and said why it could not be
made when it was found: `src/js/JsEval.cpp` was the file of two open pull
requests (#68 and #71) during Phase 63. It merged afterwards, and the row stood
open until Phase 102 — which is the rule working rather than failing: the row
names the file, and a claim takes a file an open pull request does not.

## The fix

`IterationCursor` asks `js_iterates_as_builtin()` — the same question
`js_iterate()` asks — and follows the protocol for everything that question does
not settle. Strings walk UTF-16 units in place, two at a time across a surrogate
pair. `close()` makes the three checks of IteratorClose: absent or null
`return` is nothing, non-callable is a `TypeError`, a non-object result is a
`TypeError` — and an error out of `return()` is thrown, because a pattern that
binds fewer names than the iterator holds has not failed on its own account.

## Verified

```bash
python3 tools/conformance/run_js_conformance.py --quiet
# JS conformance, full corpus: 1141 / 1158 (98.5%)   [1137 / 1156 before]
python3 tools/conformance/run_js_conformance.py --filter iteration --reference "node -e"
# iteration.dat   84/84   100.0%
./build/test_suite
# 267 engine test suites, all passing
```

Two cases were added to `tests/js-conformance/iteration.dat` (a string's code
points through a pattern, and what `return` must be), and one unit suite,
`destructuring_asks_the_value_for_its_iterator`, to `tests/test_suite.cpp`. The
unit suite's script prints the same answers under Node.js.

## Still open, and not this finding

`for…of` accepts an object that only has `next`; a pattern no longer does, and
neither does Node.js accept it, so the two disagree — that is the row of
`AGENTS.md` about iteration, not this one. `Symbol` is still a string, so a
property key cannot be one.
