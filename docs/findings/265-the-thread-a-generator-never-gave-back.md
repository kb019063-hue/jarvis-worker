# Finding 265 — the thread a generator never gave back

**Severity:** a hard failure under load, not a slow leak. A page that makes
generators in a loop — which is what `for (const x of gen())` inside any
repeated call does — dies at **15,730 generators** with

    RangeError: Too many suspended functions: out of threads

Node v22.22.3 makes 20,000 in the same loop without trouble.

**Found by:** the residue harness (`tools/conformance/verify_gc_residue.py`,
Phase 172), which certified fifteen churn shapes flat and had no shape that
made an iterator. Adding four found the difference on the first run.

## What it was

`make_generator` (`src/js/JsEval.cpp`) builds every generator through
`spawn_frame`, and `spawn_frame` starts a **real OS thread** for it
(`frame->worker = std::thread(std::move(body))`, line 1182). The thread parks
immediately and runs only while someone is inside `resume()`. That design is
deliberate and documented: a suspended generator's body is a thread stack, so
`await`/`yield` can block it.

The frame gives its thread two ways to be released:

* `reap()` joins it, and `make_generator`'s `advance` calls `reap()` when the
  body runs to its end;
* `~GeneratorData` terminates and detaches.

Neither runs for a generator that is **dropped instead of finished**. The
path to the frame is:

    the generator object's payload  ->  shared_ptr<GeneratorData>  ->  thread

and `~GeneratorData` only runs when that `shared_ptr` count reaches zero.
`m_generators` does not keep it alive — it is a list of `weak_ptr`. So the
frame dies exactly when the payload does, which is when the *heap cell* dies,
which is when a collection finds the cell unreachable.

## What was measured

Built `build/litebird` at `91460c8`; `tools/conformance/verify_gc_residue.py`
at two sizes, 200 and 2,000 iterations, residue read after a `gc()` and a
turn, exactly as the harness does it:

| shape (step body)                        | @200  | @2000 | cells/iter |
|------------------------------------------|-------|-------|-----------|
| `var o = {a:i}` — control                | 0     | 0     | 0 |
| `var p = Promise.resolve(1)`             | 0     | 0     | 0 |
| `function* g(){...}` — declaration only  | 0     | 0     | 0 |
| `function* g(){yield 1} var it=g()`      | 1400  | 14000 | **7** |
| …same, `it.next()` twice (finished)      | 1400  | 14000 | **7** |
| `var o={n:i}; … var x=o.n; … it=g()`     | 1600  | 16000 | **8** |

8× the iterations gives 8× the residue, so it is a leak, not a backlog. Three
`gc()` calls in a row do not move it: 16000 either way. `gc()` *inside* the
failing loop does not help either — the same 15,730 ceiling.

Two things this rules out:

* **not the payload bookkeeping.** A promise uses the identical storage
  (`set_promise_value`, same `std::variant` alternative) and is flat at 0.
  A bound function is flat at 0 too.
* **not "a suspended frame is expensive".** `generator-finished` advances past
  the end and still leaks 7. The declaration alone, which allocates nothing,
  leaks 0.

So the cost is per *created* generator, and it is the thread and its frame,
not anything the generator holds.

## Why the harness did not catch it

Fifteen shapes, and every one of them was a promise, a timer, a `bind`, or a
listener. None made an iterator. The harness's own docstring lists what it
covers, and the row was read as "fifteen flat" for several phases — the gaps
were invisible because a shape that is absent cannot fail.

The four shapes are now cases. They carry a `known` reason with the numbers
above, so the run prints `KNOWN` and stays green; a case that comes back flat
while it still carries a reason prints `FIXED` and **fails**, so the reason has
to be deleted when the defect is fixed. That is the convention
`tests/render/reftests.json` already uses, applied to this corpus.

## What is not yet known

The row's own open item was the `holders > 3` heuristic in
`Promise::trace_extra`. It was not the cause here, and this finding does not
close it: the shapes that exercise it (`promise-settled-from-timer`) are
still flat, and the heuristic still "can only over-retain".

The fix is not landed. Two candidates, neither tried past reading:

1. **Join on drop.** `GeneratorData` needs the thread released when the last
   `shared_ptr` goes, from whichever thread that is. Its destructor already
   detaches rather than joins, with a comment that this is "only reachable if
   the last reference died on the frame's own thread" — that comment is the
   assumption to re-examine.
2. **A cheaper frame.** A generator that has never been resumed has not
   started a body; it needs a frame, not a parked thread. Starting the thread
   on the first `next()` would make the leak proportional to *running*
   generators, not created ones, and would remove the 15,730 ceiling for the
   common `for…of` shape.

Option 2 is the better one if the frame can be built without a thread; option
1 is correct either way. Both want a full cycle: the frame is shared with
async functions (`spawn_frame(..., is_coroutine)`), so async functions have
the same shape and are not measured here.
