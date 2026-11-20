# Collection at a safe point, and the corpus as the oracle for it

*Phase 103, finding 109.*

## 1. What was wrong

`ROADMAP.md` lists automatic collection as one of the three Tier 1 items with
"almost no" oracle, and `README.md` says plainly that "memory is only reclaimed
on request". Both statements were true, and together they meant something
worse than the gap they describe: **no test in this repository had ever run a
program through a collection it did not ask for.**

Every other oracle here is an outside implementation — Node.js, jsdom,
html5lib, web-platform-tests, the W3C XML suite, libwebp, ImageMagick — and
where none of those could be had, an artifact nobody wrote for the engine: a
font's own `kern` table, Ahem's 1 em advance, a face whose `OS/2` disagrees
with its own name. Collection had neither. The two suites that touched it
(`garbage_collector_and_cycles`, `verify-collection-iterators`) call
`collect_garbage()` themselves, at a moment the test chooses, with a root set
it passes in by hand. That measures the collector on its best day: the
interpreter is idle, the stack is empty, and the one question that matters has
already been answered by whoever wrote the call.

The question is: **does a collection ever free something still alive?** A
collector that answers yes is not slow, it is wrong, and it fails
intermittently and far from the cause. Nothing here had asked.

## 2. What changed

**A seeded request, served at a safe point.** `Heap::set_gc_stress(seed,
interval)` counts allocations; every *interval* allocations on average it
raises a request; `splitmix64` from the seed decides which allocations those
are. It does not collect inside the allocation — that is the one place a
partially built object is guaranteed. `JsInterpreter::exec` serves the request
at the statement boundary and `VM::run` at the bytecode boundary. This is the
shape of V8's `--gc-interval` and HotSpot's `-XX:+StressGCM`.

The statement boundary is the whole design. Entering a statement, the previous
statement's values are held by C++ locals whose handles the collector counts,
and the next statement has built nothing; a half-built array, or a call's
arguments half-evaluated, cannot be on the stack. A collection anywhere else
inside the interpreter would be testing the harness.

**`--gc-stress=SEED[:INTERVAL]`** arms it from the CLI and reports to *stderr*
(`[gc-stress] seed=1 interval=16 collections=596`), so a corpus comparing
stdout is unaffected and a harness can tell "the program agreed under stress"
from "stress never fired".

**The oracle is the program against itself.** `tools/conformance/verify_gc_stress.py`
runs each case twice — once as a page runs it, once with stress armed — and
requires the same stdout and exit status. This is the one place in the
repository where the expectation is not another implementation's output, and
the difference is worth stating because `README.md` has the receipt for the
opposite ("It was 100% before it was right"):

- nothing is asserted about *collection*. No case says what a correct
  collector does, so no case can assert it wrongly. The only claim is that two
  runs of one program agree, which is true or false independent of anybody's
  reading of a spec.
- the corpus is not this checkpoint's. `--full` runs all 1,156 cases of
  `tests/js-conformance`, whose expectations were recorded from Node.js, and
  `--dom` runs all 678 of `tests/dom-conformance`, recorded from jsdom.
- the graded corpus added here, `tests/gc-conformance` (28 cases), has its
  `#expect` sections recorded from Node by `add_js_case.py`'s rule — they are
  not the engine's own output, and a case whose un-stressed run disagrees with
  Node is *reported and skipped*, never counted as agreement.

**The negative control runs first.** `--self-test` runs three cases the
harness must be seen to catch: one whose output a collection legitimately
changes (`gc().collections_count` around 400 allocations), one that already
fails (a `throw`, which says nothing about the collector and must be refused
rather than counted), and one whose allocation rate never reaches the interval
(`--interval 1000000`, which must be reported as "stress never fired" rather
than passing). It also checks *why* each was caught, so "caught for the wrong
reason" fails. A harness that cannot fail is not evidence.

**The DOM corpus matters more than the JavaScript one.** A DOM object is a
`JsObject` whose C++ half holds nodes, live collections and attribute maps the
collector does not own. A binding that keeps a raw pointer across a collection
that a callback triggered is exactly this class of bug, and `--dom` runs the
678 pages whose expectations came from jsdom.

## 3. How it was verified

```bash
make verify-gc-stress        # the negative control, the corpus, and the DOM
make verify-gc-stress-full   # interval 1 over both corpora
```

What they print, on this tree:

| Run | Result |
| :--- | :--- |
| `--self-test` | 3 of 3 controls caught |
| `tests/gc-conformance`, seeds 1,2,3, interval 16 | 28 of 28 cases agree; 958 collections served |
| `--full`, 1,186 cases, **interval 1**, seed 1 | **1,186 of 1,186 agree; 254,174 collections served** — 0 differed; 83 out of scope (19 already fail against Node in the JS scoreboard, 64 are size programs too slow to stress) |
| `--full`, interval 16, seeds 1,2,3 | 158,355 collections served, no case differing (measured before the rebase onto `main` at `01bbf07`; the corpus has since grown to 1,186) |
| `--dom`, 678 pages, interval 2, seeds 1,2 | **678 of 678 agree**; 11,176 collections served |
| `--dom`, interval 1 | 678 of 678 agree; 7,169 collections served |
| The same under `-fsanitize=address,undefined` (`--binary build-asan/litebird`) | corpus at interval 4: 28 of 28, 948 collections; DOM at interval 4: **678 of 678, 4,178 collections**, no sanitizer report |

Two of the 1,186 finished only when given 600 s instead of 120 (`collect: 2,000
classes with parents are freed when nothing holds them` agrees with 13,212
collections served, and so does `property found far up a long prototype
chain`). They are cost, not correctness: a collection is O(heap), so a program
that holds 10^5 objects alive costs (allocations / interval) x heap under
stress. `deep-nesting.dat` is excluded by name for the same reason — 63
programs of 200,000-deep input — and `make deep` covers it. Both exclusions are
printed and counted, never silent, because the one thing this harness must not
do is go green by not collecting.

**No case printed anything different because of when it was collected.** That
is the result, stated as plainly as it can be: the collector's root counting
survived 254,174 collections placed by a seed inside 1,186 JavaScript programs
and 11,176 inside 678 DOM pages, including generators suspended across them,
iterators live across `clear()`, `Map`s keyed by objects, private fields,
`arguments` objects, `eval`, and the built-ins that call back into script
(`sort`, `forEach`, `map`, `filter`, getters) — the last of which is where a
native frame holding a pointer into an array while script allocates would have
shown up.

The counts are the load-bearing part of the table. A run that agrees because
nothing was collected is the failure this checkpoint exists to prevent, which
is why the harness fails a case whose stress run served zero collections, and
why the totals are reported beside the verdicts.

The suite `collection_stress_asks_at_a_seeded_allocation_count` (`make test`,
268 suites on the tree this landed against) holds the arming itself: one request per countdown, taken once and
re-armed, the same seed raising the same number of requests as itself and a
different number from another seed, `interval 0` behaving as "every
allocation", and a heap that was never armed never asking. `make check-numbers`
still divides; the JS and DOM scoreboards are unchanged.

## 4. What is still open

**This measures the collector. It does not schedule it.** Collection still
happens only when a script calls `gc()` or the shell asks. The Tier 1 item —
a long-running page that does not grow without bound — is untouched by this
phase, and the checkpoint above is the thing that makes changing it safe: an
automatic collector is a change in *when* collection happens, which is exactly
the variable this now holds fixed. Finding 109 keeps that open.

**Three classes of case are excluded, and the exclusion is a real gap.**

- Programs that take more than 2 s un-stressed are skipped: a collection is
  O(heap), so stressing a large live set measures the interval, not the
  collector. `make deep` covers those programs for depth, not for collection.
  A stress mode with an incremental collector would not need the exclusion.
- Threads. The countdown is per heap and the request is atomic, but a worker
  agent collecting at its own safe point while another runs is not a case this
  corpus has. `tests/cross_origin_windows.cpp` and the worker suites would be
  the place to add it.
- Leaks. This finds premature frees, not retained garbage: `o.b = f.bind(o)` is
  still never freed (ARCHITECTURE.md, known gaps 4), and a stress run does not
  make it worse or better. A heap-growth bound per page — the shape
  `bench-styles` already uses — is the missing half of the oracle, and it is
  the one that would have to be built before automatic collection is attempted.

**The interpreter is the only place the request is served in the tree-walking
path.** Direct calls into native code that never returns to `exec` — a
`Promise` reaction driven from the event loop, a DOM callback fired from the C++
side — run between statements, so the request waits for the next one. That is
sound (the request is not lost) but it means a program whose entire allocation
happens inside one native call is under-collected rather than wrongly
collected. `VM::run` has its own boundary; the event loop does not have one
yet.
