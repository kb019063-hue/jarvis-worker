# Phase 210 — the loop was never iterable through a bare `next`

Task: the row that began "`for…of` takes an object that only has `next`",
which claimed `js_iterate()` accepts an object with a bare `{ next }` —
`method_is_installed()` falling back to it — and asked for the fallback to be
dropped. Finding 214
(`docs/findings/214-a-loop-was-never-iterable-through-next.md`).

**Engine work?** No scoreboard moves: the behavior this phase retires was
already the behavior of the engine. This is a probe, a pinning suite, and the
retirement of a row — the precedent is Phase 201 / finding 205.

## 1. The probe came before the code

The row carried its own check: `iteration.dat` has the cases that ask for
it. Against `build/litebird` built from `main` at `51fb965` (this branch
merged it at `25dce1a~1`):

```
$ python3 tools/conformance/run_js_conformance.py --filter iteration
 iteration.dat   81/84   96.4%
```

The three that fail are not the row's cases: the row named
`what is not iterable is a TypeError` and `an object with next and no
Symbol.iterator is not iterable`, and both **pass** — the engine answers
`TypeError` exactly where the case asks for it. The three failures are the
getter-reads-once case (a different defect, filed as its own row — see §5),
the `@@iterator`-is-a-string case (the Symbol-as-string row), and the
`arguments` object's own iterator row.

## 2. Every path, not one

A row this wide is measured with more than one form, because the first probe
is never the one that finds the seam. All of these were run through
`build/litebird --run-js` and through Node.js v22.22.3, and the two engines
print the same answers line for line:

| written | this engine | Node.js v22.22.3 |
| :--- | :--- | :--- |
| `[...bare]` where `bare = { next: function(){…} }` | `TypeError` | `TypeError` |
| `for (var x of bare) {}` | `TypeError` | `TypeError` |
| `new Set(bare)` | `TypeError` | `TypeError` |
| `new Map(bare)` | `TypeError` | `TypeError` |
| `new WeakSet(bare)` | `TypeError` | `TypeError` |
| `yield* bare` inside a generator | `TypeError` | `TypeError` |
| `var [q] = bare` | `TypeError` | `TypeError` |
| `var q; [q] = bare` | `TypeError` | `TypeError` |
| `Object.fromEntries(bare)` | `TypeError` | `TypeError` |
| `Array.from(bare)` | `0` (the array-like path; no `length`) | `0` |
| `[...{}]` (no symbol key at all) | `TypeError` | `TypeError` |
| `for (var x of {}) {}` | `TypeError` | `TypeError` |
| `new Set({})` | `TypeError` | `TypeError` |
| `o[Symbol.iterator] = null` then `[...o]` | `TypeError` | `TypeError` |
| `o[Symbol.iterator] = 5` then `[...o]` | `TypeError` | `TypeError` |
| `Promise.all(bare)` | returns a promise (rejects later) | returns a promise (rejects later) |

`Promise.all` is the one line where both engines *do not* throw
synchronously — the iterator is asked inside the returned promise — so the
unit suite does not assert on it. Fifteen probes, no divergence.

## 3. Why the row described an engine that does not exist

The row says `method_is_installed()` "falls back to a bare `{ next }`". The
function at this commit does no such thing (`src/js/JsOps.cpp`):

```cpp
bool method_is_installed(const JsValue& value, const JsValue& method) {
    if (!method.is_function()) return false;
    ...array / string fast paths by identity...
    return false;
}
```

And `js_iterate` throws for a missing or non-callable `@@iterator` one line
before `method_is_installed` is ever reached (`js_iterator_method`). The
bare `{ next }` acceptance the row tracks is not in the tree. It was true
when the row was written around Phase 63/94/102 and false by the Phase 92
iterator work or shortly after — rows in this table are carried forward by
editing a row only when its own phase retires it, so a row that time made
false stood until an agent took it, which is this phase.

## 4. What was changed

- `tests/test_suite.cpp`: the new suite
  `an_object_without_an_iterator_method_is_not_iterable` (15 assertions, each
  also printed identically by Node.js), its `RUN_TEST` line, and a two-line
  correction to a stale comment in `destructuring_asks_the_value_for_its_iterator`
  that said for…of still accepted a bare `{ next }` — left otherwise
  untouched.
- `AGENTS.md`: the row struck through and retired; a new row for the defect
  §5 names, as the rules require for anything found outside a task.
- Two documents: this file and finding 214.

No source file was touched, and none needed to be. Two corpus cases the row
named already pass; `js-conformance` is unchanged at 1146/1158, everyday
probe 90/90, `iteration.dat` 81/84 (failures are other rows).

## 5. What the probe found instead (filed, not fixed here)

`Array.from` decides "iterable or array-like?" by calling
`js_iterator_method` itself and then iterating with `js_iterate`, which
asks the source again — an own getter on `Symbol.iterator` runs **twice**.
`iteration.dat`'s `a getter for the iterator on an array is read once for
each iteration` measures it: expected `1 2 3 4`, this engine prints
`1 2 4 5` (`Array.from` is the +1). It is the new row in `AGENTS.md`; this
phase did not touch `src/js/BuiltinsArray.cpp`.

## 6. What this does not claim

- Not that iteration is correct. That is spread over several rows and two
  failing corpus files; the three `iteration.dat` failures and the 1 failure
  of `collections-iterators.dat` are untouched by this phase.
- Not that `method_is_installed()` is complete. It is the fast path only; the
  slow path consults the protocol.
- Not that the two named corpus cases are the whole oracle. They are quoted
  as the row's own; the pinning suite is the new one.
