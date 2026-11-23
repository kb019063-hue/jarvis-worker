# Finding 214 — a loop was never iterable through a bare `next`

**Severity:** documentation, not code. An open row taught every agent that
`for…of` accepted an object with a bare `{ next }`, and that
`method_is_installed()` fell back to one. At this commit it does neither:
every iteration path asks the value for its `@@iterator`, and a missing or
non-callable one is a `TypeError`.

**Found by:** taking the row at its word, as the rules say to, and probing it
before writing any code — the precedent is finding 205.

## What the row said

> `js_iterate()` still accepts it (`method_is_installed()` falls back to a
> bare `{ next }`), where Node.js answers `TypeError`. The same fallback is
> what makes an object with no `Symbol.iterator` at all iterable in a `for…of`
> here and not in a browser.

Both halves are false at `51fb965`:

- `js_iterator_method` — called at the top of `js_iterate`, before anything
  else — does `value.get_property(kIteratorKey)` and throws
  `js_throw_type_error` for a nullish or non-callable answer. A bare `{ next }`
  never reaches `method_is_installed`.
- `method_is_installed` itself has no bare-`{ next }` path; it is the
  array/string fast path and answers `false` for everything else, which routes
  to `iterate_with` — the standard GetIterator path — rather than to a fallback.

## What the engine does (identical to Node.js v22.22.3)

```
bare = { i: 0, next: function () { ... } }
[...bare]                 TypeError
for (var x of bare) {}    TypeError
new Set(bare)             TypeError
new Map(bare)             TypeError
new WeakSet(bare)         TypeError
yield* bare               TypeError
var [q] = bare            TypeError
var q; [q] = bare         TypeError
Object.fromEntries(bare)  TypeError
Array.from(bare)          0
[...{}]                   TypeError
for (var x of {}) {}      TypeError
new Set({})               TypeError
o[Symbol.iterator] = null → TypeError
o[Symbol.iterator] = 5    → TypeError
```

`Array.from` is the deliberate exception, and it is the standard's: with no
`@@iterator`, §23.1.2.1 reads the source as an array-like, and a missing or
undefined `length` yields an empty result — `0` here and in Node.

Sixteen probes, two engines, no difference. The row's own corpus cases —
`what is not iterable is a TypeError` and `an object with next and no
Symbol.iterator is not iterable` in `tests/js-conformance/iteration.dat` —
both pass; the file stands at 81/84, its three failures belonging to other
rows (the getter-reads-once row filed in this phase, the Symbol-as-key row,
the `arguments` iterator row).

## Why the row stood

`AGENTS.md` rows are carried forward; a row is rewritten only by the phase
that retires it. The described fallback was true around the Phase 63/94/102
work and false by the Phase 92 iterator rewrite, and no phase was born to
retire the row until this one. A stale row is not a lie — it is an unfixed
IOU — but a row opened on each agent's list is a claim on their attention,
and this one was spending it.

## What this changes

Nothing in the engine. One new pinning suite
(`an_object_without_an_iterator_method_is_not_iterable`, 15 assertions
matching Node line for line), the row retired in `AGENTS.md`, and the
`Array.from` double-getter defect found in passing filed as a new row — not
fixed here, because it is outside the row this phase claimed.

## What this does not claim

Not that the iterator protocol is correct — the failing corpus cases are
listed above and owned elsewhere. Not that the two named cases would catch
every regression: they catch the acceptance of a bare `{ next }` by the loop,
and the suite catches the wider family. Both were measured after the suite
was written: the suite passes, `iteration.dat` stands at 81/84.
