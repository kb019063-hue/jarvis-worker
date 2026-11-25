# Phase 415 — one iterator loop, not two

Task: the row "`Array.from` asks the source once, and maps as the values
arrive" ended with an open clause: *"that loop is a copy of `iterate_with` in
`src/js/JsOps.cpp` and the two belong together as one exported function (#261
held that file)"*. Finding 415.

## 1. What there was

Two loops over an iterator, both implementing GetIterator and IteratorStep
(§7.4.3, §7.4.8):

- `iterate_with` in `src/js/JsOps.cpp`, the one every other caller uses —
  spread, `for…of`, `yield*`, destructuring, `Set`, `Map`. It reads `next`
  **once**, closes the iterator when the loop leaves it unfinished, and does
  **not** close it when the iterator itself failed. It also knows the one thing
  the second loop never learned: a `GeneratorAbortException` is a generator
  that is already closing itself, so `return` must not run on top of it.
- a copy in `src/js/BuiltinsArray.cpp` that `Array.from` used, written in
  Phase 225 when `Array.from` had to stop reading `@@iterator` twice. It had
  drifted: no `GeneratorAbortException` branch, and every `IteratorClose` rule
  written out again by hand.

The two were the same idea twice, which is the shape a defect grows in.

## 2. What was done

`iterate_with` lives in an unnamed namespace in `JsOps.cpp`, so it cannot be
declared in the header. Rather than rename it — every caller in the engine is
in files that already have it — the header gained a wrapper beside it:

```cpp
// include/litebird/js/JsOps.hpp
void iterate_with_method(const JsValue& iterable, const JsValue& method,
                         const std::function<bool(const JsValue&)>& visit);
```

```cpp
// src/js/JsOps.cpp, after js_iterate
void iterate_with_method(const JsValue& iterable, const JsValue& method,
                         const std::function<bool(const JsValue&)>& visit) {
    if (js_iterates_as_builtin(iterable)) {
        js_iterate(iterable, visit);
        return;
    }
    iterate_with(iterable, method, visit);
}
```

The fast path is the copy's own: an array, a string or an arguments object is
walked directly, and only a source that is not builtin is walked through the
protocol with the method the caller already holds. `Array.from` lost its copy
and calls the shared function; `iterate_with_method` in `BuiltinsArray.cpp`
now appears once, in the header, for anyone who has the method in hand.

**This is a consolidation, not a bug fix.** `GeneratorAbortException` is a
control-flow signal inside the interpreter and does not escape to script, so no
program could observe the drift: `/tmp/it1.html` and a five-case probe of the
generator shape answer on this tree exactly what Node v22.22.3 answers
(`fromThrow:err=stop closed=1 | fromReads=1`, `finally=1`, `return=1`). What
the phase removes is the possibility that the next edit to either loop changes
only one of them.

## 3. The suite

`tests/suites/415-one-iterator-loop-not-two.cpp` pins the parts of the walk
that are observable, so a future edit to the shared loop is caught:

| assertion | what it pins |
|---|---|
| `fromReadsNextOnce` | `next` is read once for a whole walk, not once per step |
| `fromThrow` / `fromClosedOnce` | a mapper that throws closes the iterator, exactly once |
| `theMapperErrorWins` | a `return` that throws is dropped when the loop already has an error |
| `noIteratorNoLength`, `numberSource`, `nullSource`, `badLength` | the array-like fallback and what is a TypeError |
| `array`/`string`/`set`/`map`/`arrayLike` | the iterable path is unchanged |

Every assertion was run in Node v22.22.3 first, and one was **wrong**: the
suite first asserted that `Array.from({})` is a `TypeError`. It is not — `{}`
has no `Symbol.iterator`, so it is read as an array-like of length 0, and only
a nullish source throws. Node corrected the assertion, which is the whole
argument for writing it out of Node before writing it into C++.

## 4. What was found on the way, and not fixed here

Two defects outside this loop, both recorded in the finding rather than
patched in this pull request, because each belongs to a different row:

1. **`Array.fromAsync`'s own loop is a third copy** — Phase 412's, written a
   day before this phase. It reads `next` inside the loop (three reads where
   Node reads one), and it never closes the async iterator: `closed=0` on a
   mapper throw and on a rejected `next`, against Node's `1` for both. It is
   §7.4.1's async walk and belongs beside this one; it is not the loop this
   phase consolidated.
2. **An interpreter whose generator is aborted mid-walk leaves cells
   reachable.** Not "leaks" in the sense of a script that cannot release
   memory, but the interpreter's own cells stay reachable after the suite, and
   `make test` counts them: a suite that aborted a patched generator left
   `alive_cells` at 51,007 before the GC tests where a run without it reads
   52,753, and the phase-145 test's threshold arithmetic (`max(8192, alive)`)
   then failed its second assertion by one collection. The suite therefore
   does not abort a generator; it pins the same rule with a plain source. Row:
   the GC leak audit.

`make test` counts 471 suites, unchanged from the tree before this phase plus
one.

## 5. Numbers

- `make test` **488/488** on the merged tree (486 on `main` at `39645176`; the
  suite is the addition, and the run before this phase's branch merged `main`
  was 470/470 of a smaller main).
- **test262 15,210 of 15,686, unchanged** — measured with both binaries on the
  merged tree, `main` at `39645176` against this branch:
  `15210 before / 15210 now, 0 pass in now and not in before; 0 pass in before
  and not in now`. (The 15,180 in this session's earlier notes was stale: the
  branch point `24e5c7a` itself reports 15,182, and `main` has gained 28 more
  since through other phases.)
- js-conformance **1,153/1,158** — unchanged.
- WPT **9,570/9,824**, 254 fail as listed, **0 unlisted** — unchanged.
