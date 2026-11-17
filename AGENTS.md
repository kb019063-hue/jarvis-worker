# Working in this repository

Litebird is written by AI agents, at times more than one at once. Each agent
has a branch of its own. No agent is told what another one is doing: an agent
knows what `main` holds and what the open pull requests say, at the moment it
looks, and nothing else. Whatever cannot be said that way goes through the
person the agents work for. This file is what the agents have to agree on so
that two pieces of work do not collide.

## Notice — read this first (2026-11-17)

**New phase write-ups and findings no longer go in `ARCHITECTURE.md` and
`AUDIT.md`.** They go in `docs/phases/NN-short-title.md` and
`docs/findings/NN-short-title.md`; `docs/README.md` says what goes in one, and
why files rather than sections.

`ARCHITECTURE.md` is 408 KB, and 84% of it — 343 KB in 43 sections — is phase
write-ups. `AUDIT.md` is 282 KB, and 53 of its 61 findings are resolved. An
agent claiming a task has to find two integers in all that, which is about
660 KB of markdown for two numbers; and two agents that look at the same
moment still take the same one, which is what a number read out of a document
cannot prevent. So:

- **The two documents are frozen as append targets.** What is already in them
  stays exactly as it is: do not add to it, reorder it, re-wrap it or delete
  it in passing. They are migrated to `docs/` over time, and everything in
  them is in the repository's history besides.
- **The two numbers come from `python3 tools/next_ids.py`**, not from reading
  either document. It reads the documents, `docs/`, `docs/state.json` and the
  open pull requests; `--pr-body` prints the block a pull request opens with
  and `--sources` says where each number came from. It reserves nothing — a
  number is still taken by opening the pull request — but a collision is then
  visible in `gh pr list` instead of hidden in two documents.
- **CI enforces both rules** (`python3 tools/ci/check_docs.py`): a new
  `## Phase NN` heading in `ARCHITECTURE.md`, or a new `## NN.` finding in
  `AUDIT.md`, fails the build, and so does either file growing past its budget.

A change like this one — documents, tooling, CI, nothing that moves a
scoreboard — is not a phase and takes no phase or finding number.

## Before starting

1. **An open pull request is a claim.** Run `gh pr list --state open` and read
   the first lines of each: they name the task and the files it changes. (A
   pull request nobody has pushed to for days is not a claim; ask in a comment
   on it before taking its task.)
2. **Claim before doing the work.** Pick a row of the table below that nobody
   has, push a first commit (an empty one will do) and open a pull request
   whose title begins with `WIP:` and whose text begins:

   ```
   Task: <the row of the table>
   Files: <what it will change>
   Phase P, finding F
   ```

   `P` and `F` come from `python3 tools/next_ids.py`, which reads the
   documents, `docs/`, `docs/state.json` and the open pull requests and prints
   the next free number of each; `--pr-body` prints the three lines above with
   them filled in. (They used to be read out of ARCHITECTURE.md and AUDIT.md —
   see the notice at the top.) They are taken now so that two pull requests do
   not both call themselves Phase 27. A change that is not engine work —
   documents, tooling, CI — takes neither: write `No phase, no finding` and
   say why instead.
3. **One task at a time, inside its files.** If the work turns out to need a
   file that another open pull request names, stop before changing it and
   tell the person you work for: the other agent will not see a comment
   unless it happens to look. A bug found outside the task becomes a new row
   of the table; it is not fixed in passing.
4. **Look again once the pull request is open.** Two agents that look at the
   same moment both see the same number free, and the same file, and each of
   them did look first. `python3 tools/ci/check_pr.py claim N` lists what #N
   has in common with the other open pull requests. Of two that have the
   same phase, finding or file, **the one opened later moves**: it takes the
   next free numbers, or stays out of the file, and the earlier one changes
   nothing. A pull request holds the files it has changed as well as the
   ones it names. Put `[skip ci]` in the message of the first commit: an
   empty commit needs no 28 minutes of runner time.

## The files every pull request changes

`README.md`, `ARCHITECTURE.md`, `AUDIT.md`, `tests/test_suite.cpp` (one file,
with the suites registered in a list at its end), `Makefile`,
`.github/workflows/ci.yml` and the table below. Two pull requests written at
the same time will both have changed them. So:

1. Add a block of your own: a section, a group of tests. Do not re-wrap or
   reorder what is there.
2. **Before merging, merge `main` into your branch.** Resolve, correct the
   numbers that count things (the unit suites in README.md, the floors in
   `ci.yml` and the Makefile), run the checks again, push, and let CI pass on
   that commit. CI that passed on a branch behind `main` says nothing about
   the merge.

## What "done" means

- The checks: `make test`, `make conformance`, `make js-conformance`,
  `make dom-conformance`, `make url-conformance`, `make xml-conformance`,
  `make wpt`, `make diff-trees`, `make diff-xml`,
  `make fuzz`, `make deep`, `make asan`. The last two take
  about eight minutes each. CI runs all of them, and one CI run is about 28
  minutes of runner time: run the checks here first and push when they pass,
  not to find out whether they do. `make xml-conformance` downloads the
  W3C suite the first time (`tests/xml/README.md`), and `make diff-xml`
  needs `pip install lxml`.
- A unit suite that passes in the normal build can fail in the sanitizer
  build, and CI's log cannot be read from every sandbox: run `make asan`
  here first. When CI's sanitizer job fails all the same, its last step
  says what failed in the job's annotations, which the API itself serves:
  `gh run view RUN --json jobs` for the job's id, then
  `gh api repos/OWNER/REPO/check-runs/JOB_ID/annotations --jq '.[].message'`.
  (`tools/ci/annotate_failure.py`; any job that keeps its output with
  `tee` can end with the same step.)
- A floor in CI is not lowered to let a pull request pass.
  `tests/js-conformance/everyday-*.dat` and
  `tests/dom-conformance/everyday.dat` are not edited. A DOM case that
  fails is not "fixed" by recording another answer: its expectation is
  what jsdom prints, and where jsdom is wrong the case is taken out and
  the standard's answer goes into a unit test
  (`tests/dom-conformance/README.md`).
- A full score is where work on the DOM starts, not where it ends. The
  corpus knows the questions its author thought of. Before and after a
  change, run pages that are not in it through
  `tools/conformance/compare_with_jsdom.py`, and turn what that finds into
  cases.
- The three documents are updated: what was wrong, what changed, how it was
  verified (a command anyone can run, and the number it prints), and what is
  still open. AUDIT.md says what is not fixed as plainly as what is.
  (`docs/README.md` says where a write-up goes now, and what one says.)
- The pull request takes its row out of the table below, or rewrites the row
  to what is left.
- The `WIP:` comes off the title. Merged into `main` with a merge commit
  (`gh pr merge N --merge`) once CI is green, and CI on `main` checked
  afterwards.
- `python3 tools/ci/check_pr.py merge N` says "ready" right before the merge.
  It asks what is easy to believe and was wrong more than once: that `main`
  is in the head commit, that CI passed on that commit, that the numbers are
  still free.

## What has gone wrong, and what to do about it

Everything here happened between 15 and 17 November 2026, to agents that had
read this file. Most of it is a rule from above that was not looked up at the
moment it applied, so `tools/ci/check_pr.py` looks it up (it reads GitHub and
changes nothing):

```bash
python3 tools/ci/check_pr.py            # what each open pull request claims; the numbers taken; the next free pair
python3 tools/ci/check_pr.py claim N    # right after opening #N
python3 tools/ci/check_pr.py merge N    # right before `gh pr merge N --merge`
```

**Claims**

- **The same number twice.** #68 and #69 were opened 72 seconds apart and both
  were Phase 55, finding 61: neither could have seen the other. That is what
  step 4 above is for. Four more took a number that an open pull request
  already had: #63 took 52 and 58 half an hour after #62 had them, #66 took
  53 and 59 two hours after #64, #71 took 54 (which #67 held), then 55, then
  56, and #68 took 59 and 65 twenty-two minutes after #74 had them. Count the
  open pull requests, not `main` alone. And count the `## Phase N` headings
  of ARCHITECTURE.md, not its phase table alone: eight phases (36, 42, 45,
  47, 51, 52, 54, 56) had a section and no row until Phase 63 added the rows.
- **Whoever merges first keeps the number, whoever claimed it first.** #70
  claimed Phase 56 and finding 62 at 01:11 on 17 November. #71 put 56 in its
  title at 02:40, was told in a comment at 02:43, and was merged with 56 and
  62 at 03:02. Once a number is on `main` there is nothing left to settle:
  #70 became Phase 63, finding 69, two hours of documents later. So run
  `check_pr.py merge N` before you merge (it refuses a number that an
  earlier open pull request holds), and write so that a renumbering is one
  `sed`: always the words `Phase N` and `finding F`, which stand in six
  files (`ARCHITECTURE.md`, `AUDIT.md`, `README.md`, `AGENTS.md`,
  `tests/test_suite.cpp`, `tests/js-conformance/README.md`). Renumber on
  your branch *before* you merge `main`, while every `Phase N` in the tree
  is still yours.
- **A claim nobody can read.** #65 and #71 had no `Task:`, `Files:` and
  `Phase P, finding F` lines. So nothing said that #71 changed
  `src/js/JsEval.cpp`, which #68 named, and `src/js/JsOps.cpp`, which #70
  named. Without those lines the other agents work as if the pull request
  were not there.
- **A claim nobody holds.** #66 has the task #67 has since merged, and is
  still open. #63 stood for almost seven hours with nothing but the commit
  it was opened with, naming three files that five other rows of the table
  needed, before work on it began. An open pull request is a claim for as
  long as it is open: close the one you give up, say in it when it has to
  wait, and when your session moves to a new branch, close what the old
  branch left open.

**`main`**

- **Two pull requests that are each green can break `main` together.** #58
  and #60 were merged three minutes apart (20:42 and 20:45 on 16 November).
  Each had passed CI against a `main` without the other; one called a
  function the other deleted, and `main` did not compile for 43 minutes.
  Three agents then wrote the same one-line repair (#59, #62, #63). So:
  `git fetch` and merge `main` *last*, build and run the checks on that tree,
  push, let CI pass on that commit, and merge at once. If `main` moves in
  between, do it again; `check_pr.py merge N` says whether it has.
- **Watch CI on `main` after your merge until it ends.** The sanitizer job
  failed on `main` at two merges that evening and other pull requests were
  merged on top meanwhile. A red `main` is the next task of whoever merged
  last, before anything else, and the person we work for should hear of it.
  Look for a repair that exists (`gh pr list`, `git log origin/main`) before
  writing one.
- **A page that "reported nothing" in the sanitizer job was the engine's
  real-time limit, not chance.** The job failed that way on `main` at
  `73e23a2` and on #73's first commit: the engine stops a page's callbacks
  after 10 seconds of real time, in silence and with exit status 0, and the
  heaviest web-platform-test page spends 7 of them on its load handlers in
  the sanitizer build on an idle machine. Since Phase 63 `run_wpt.py`
  passes its own timeout as that limit (`--wall-limit`), and the engine
  says on standard error when the limit ended a run. The other runners
  still use the default: when a case of theirs ends with no output and exit
  status 0, look for that line. Read a failed job's annotations before
  anything else ("What done means" has the command), and when a job failed
  for a reason outside the tree, `gh run rerun RUN --failed` runs it again
  without the other six.
- **A push starts no CI run while the pull request conflicts with `main`.**
  It looks like a run that has not started yet. `gh pr view N --json
  mergeable` after `git fetch`, before spending a commit on CI.
- **A run is seven jobs, about 28 minutes.** `[skip ci]` goes on every
  commit that is not meant to be tested: the claim, work in progress, a
  change to the documents alone. The commit that is merged has a green run,
  or differs from one that has only in `*.md`, and the pull request says so.

**The sandbox**

- **It can be replaced in the middle of the work.** What comes back is the
  branch's base with the files of the last turn on top, `/tmp` and the build
  directories gone, and some binary test files damaged (seven under
  `tests/wpt/encoding/`). Do not commit that tree: `git fetch origin
  '+refs/heads/*:refs/remotes/origin/*'`, then `git reset --hard
  origin/BRANCH`. So commit and push after every step, and keep nothing that
  matters only in `/tmp`. The clone is shallow: `git fetch --deepen=60 origin
  main` before a merge or a `git log`.
- **It can be suspended, and its clock is not.** When a turn ends, what was
  running stops where it was and goes on when the next turn begins, hours
  later by the clock. A check that measures time then reports nonsense:
  `make asan` came back with two deep pages "timed out" after 6,703
  seconds. Do not leave a long check running across the end of a turn, and
  run again any check that was.
- **It is small: 2 cores, 3.9 GB.** `make deep` with its two jobs runs out of
  memory on one page; `python3 tools/fuzz/deep_pages.py --jobs 1` does not.
  Nothing else runs beside `make asan`, `make deep` or a timing. `make fuzz`
  needs the sanitizer build and is part of `make asan`.
- **CI's logs cannot be read from here; a failed job's annotations can.** See
  "What done means". A job that fails with nothing but "exit code 2" is a job
  that does not end with the annotations step.

**Measuring, and writing it down**

- **Run the whole of test262 for a change to the language, not the
  directories that look related.** For Phase 63 the 25 directories about
  iteration said eight more pass and none fewer. The whole suite found four
  fewer, in the directories on classes and async generators: other code had
  been leaning on what was fixed.
  `python3 tools/conformance/run_test262.py --binary before=MAIN --binary now=build/litebird built-ins language annexB`,
  about twelve minutes.
- **Measure what a change costs against `main`'s binary before calling it
  done**, with a second copy of `main`'s binary beside it to see how far a
  binary is from itself (3 to 5% here). The first version of Phase 43 was up
  to 39% slower, and nobody had asked. A loop that allocates can get slower the
  longer it runs, because memory is only reclaimed by `gc()` (Known gaps, 4):
  one over a `Map` took 12 microseconds a loop for its first thousand loops
  and 154 for its eighth thousand (the row on iterating a `Map` below says
  why). Keep such loops short and call `gc()` between measures.
- **A number is copied from a log, with the commit it was measured on.**
  Twice a sentence was written from memory with a number no run had printed,
  and one of them reached `main` (the cost paragraph of Phase 43, corrected
  in Phase 63). If the tree changes after the measurement, measure again or
  say which tree it was.
- **When you merge, correct what counts things**: the suite count on five
  lines of README.md (`grep -c "^    RUN_TEST(" tests/test_suite.cpp`), the
  floors, and the rows of the table below that say your pull request holds a
  file.

## Reading the queue

`ROADMAP.md` lists the arc from here to a Chromium-class engine in priority
(dependency) order, with rough sizes. **Priority is information, not force:
nothing in it makes any item compulsory, and agents pick whatever they like
from this table or from that file.** This table is the atomic claim-now queue;
that file is the map. If the two ever disagree about what is open, this table
wins — fix that file in the same PR.

## Open tasks

Rows that share files should not be in work at the same time.

| Task | Files | Described in | Note |
| :--- | :--- | :--- | :--- |
| **ES module workers and structured clone**: `{type:"module"}` workers are cleanly rejected with an error event (Phase 40) until the JS engine has an ES module loader; `postMessage` still serializes via JSON instead of structured clone / transferables; `symbol_registry()` in `JsOps.cpp` and `s_registry` in `DomBindings.cpp` can also be heap-allocated like `Heap::default_heap()` as defense-in-depth | `src/js/Worker.cpp`, `src/js/JsOps.cpp`, `src/js/DomBindings.cpp` | ARCHITECTURE.md, Phase 40; AUDIT.md, finding 45 | MIME enforcement, `WorkerOptions`, `addEventListener("error")`/`removeEventListener`, `importScripts`, nested worker teardown, and the exit-time `SIGSEGV` / `heap-use-after-free` are fixed in Phase 40 |
| **Cross-origin `Location` navigation and `MessageChannel`/`MessagePort`**: `iframe.contentDocument`/`contentWindow`, `src`/`srcdoc`/`sandbox`, cross-origin `WindowProxy` property get/set `SecurityError` checks, `postMessage`, subdocument layout/rasterization, `.xhtml` WPT execution (Phase 42), and cross-origin `WindowProxy`/`Location` reflection — `[[OwnPropertyKeys]]`, `getOwnPropertyDescriptor`, spread, rest, `for…in`, `JSON`, define/integrity/prototype refusal (Phase 56) — have landed; still open are cross-origin `childWin.location = url` navigation with a cross-document URL (`navigate()` is fragment-only), and `MessageChannel`/`MessagePort` | `src/js/DomWindowBindings.cpp` | ARCHITECTURE.md, Phase 56; AUDIT.md, findings 48, 62 | `make wpt` is at 7,998/8,009 across 103 files (0 `frames` failures); `make test` includes the cross-origin reflection suite |
| **`TextDecoder` and `TextEncoder` are not exposed to script**, and there is no encoder: the WHATWG Encoding Standard's 40 decoders and 228 labels landed in Phase 47 (`<meta charset>` prescan, `document.characterSet`, XML `[81] EncName`, `@charset`, `data:` URLs, XHR, subresources, and 2,543 `encoding/` WPT subtests), but a page that constructs `new TextDecoder('shift_jis')` gets a `ReferenceError`, `XMLSerializer` does not consult `document.characterSet` for an output declaration, and the prescan is one pass over the first 1,024 bytes with no streaming | `src/net/TextEncoding.cpp`, `include/litebird/net/TextEncoding.hpp`, `src/js/DomWindowBindings.cpp`, `src/js/DomBindings.cpp` | ARCHITECTURE.md, Phase 47, section 5; AUDIT.md, finding 53 | The decoder side is done and the `encoding/` pages that need no globals are vendored; what is left is the script API and the encoders (Big5's `U+00CA`/`U+00EA`, `gb18030`'s ranges in the other direction) |
| **An upgrade runs the constructor on the element itself.** The standard constructs a definition's constructor the ordinary way, with a new object as `this`; the element gets the class's prototype when `HTMLElement` hands it out, and the upgrade fails unless the constructor returns the element. `run_upgrade` gives the element the prototype first and passes its object as `this`. A class cannot tell, nor a compiled constructor that does what it is for. In a constructor written as a function `this === self` is `true`; one that never reaches `HTMLElement` (`function Plain() { this.touched = true; }`) has written on the element and its upgrade counts as done; after a constructor that throws early the element keeps the class's prototype (for a class that extends `Event` and was given to `customElements.define`, an element without `getAttribute`). Beside it: a customized built-in whose upgrade failed still matches `:defined` (`Element::is_defined`), and an element that moves from a frame's document into the page is not upgraded by the page's definition, where one from `createHTMLDocument` or `DOMParser` is | `src/js/DomObserverBindings.cpp` (`run_upgrade`, `construct_custom_element`), `src/js/JsValue.cpp` ("Constructions in place"), `src/js/JsEval.cpp` (`eval_super_call`), `src/dom/FormState.cpp` (`Element::is_defined`) | ARCHITECTURE.md, Phase 43, section 5; `tests/dom-conformance/open/compiled-custom-elements.js` (5 lines, 4 differ) | Phase 43 could not change the DOM bindings (they were #49's, which has merged since: the file is free), so its construction stack is beside the call context: `JsValue::construct` notes a construction whose `this` is an element, and `current_derived_this()` gives that element to its interface, once, whether `super()` or `Reflect.construct` reached it. Its place is the definition: `run_upgrade` pushes the element, constructs the ordinary way and compares what comes back; `construct_custom_element` takes the element from the stack and sets the prototype then. With that, `JsValue::construct` need not look at `this` any more, `current_derived_this()` loses its case for an interface that is not the element's, and `eval_super_call` the one for a parent that is no interface: a constructor that does not return the element fails the comparison. The 25 cases of `tests/dom-conformance/compiled.dat` say what has to keep working |
| **`eval` inside a function is global code.** `function f() { var local = 5; return eval('local + 1'); }` is a `ReferenceError`; the function's `this`, `arguments`, `new.target` and `super` are not seen either (`eval('super()')` in a constructor is a `SyntaxError`), and a `var` the evaluated text declares becomes a global | `src/js/JsInterpreter.cpp` (the `eval` global), `src/js/JsEval.cpp`, `src/js/JsParser.cpp` (where `super` and `new.target` may stand) | ARCHITECTURE.md, Phase 43, section 8; one failing case of `construct.dat` | Only a call written `eval(...)` is direct and runs in the scope it stands in; `(0, eval)(...)` and `window.eval(...)` stay global, as they are now |
| **A private name is a property whose name begins with `#`.** `obj['#p']` reads and writes a private field from outside its class, and `'#p' in obj` is `true`; `o.#p` for an object without the field is `undefined` and assigning to it adds one, where both are a `TypeError`; one class reads another's `#p`; `#p in o` is a `SyntaxError` | `src/js/JsParser.cpp`, `src/js/JsEval.cpp`, `src/js/JsValue.cpp` (`is_hidden_key`) | ARCHITECTURE.md, Phase 43, section 8 | A private name is an identity made when the class is evaluated, not a string: two classes that each have a `#p` have two names. The same question as the row on the engine's `%` slots, and the same files: not beside it |
| **A built-in does not make the subclass it was called on**: with `class List extends Array {}`, `List.from([1])`, `List.of(1)` and `new List().map(f)` are plain arrays; `P.resolve(1)` for a subclass of `Promise` is a plain promise. There is no `Symbol.species`, and `instanceof` does not ask `Symbol.hasInstance`. `new Map(iterable)` and `new Set(iterable)` fill the object without calling its `set` and `add`, so a subclass that overrides one is not asked | `src/js/BuiltinsArray.cpp`, `src/js/Promise.cpp`, `src/js/BuiltinsCollections.cpp`, `src/js/JsOps.cpp` (`js_instance_of`) | ARCHITECTURE.md, Phase 34, section 9; three failing cases of `prototypes.dat`, and for `Map` and `Set` one of `iteration.dat` | A native constructor can ask which function `new` was applied to since Phase 43 (`JsValue::current_new_target()`); `Reflect.construct(Array, [], G)` already gets `G.prototype` |
| **What a function is, in small**: nothing tells a built-in that is a constructor from one that is not (`new Math.max()` makes an object; `class X extends f.bind(null)` and `Reflect.construct(F, [], Date.now)` are accepted; `x instanceof` a function with no `prototype` answers `false` where it should throw, and a bound function is not asked for its target); `Object.getOwnPropertyNames(f)` lacks `length` and `name`, and `f.name = 'x'` takes effect; async and generator functions have `Function.prototype` itself as their prototype, where the language has an object for each kind between the two (three test262 tests, `AsyncFunctionPrototype-is-not-callable.js` and its relatives, passed while `Function.prototype` was not callable and fail since Phase 34); a property of a prototype that cannot be written does not stop an assignment to the object that inherits it | `src/js/JsOps.cpp`, `src/js/JsValue.cpp`, `src/js/BuiltinsObject.cpp` (`bind`), `include/litebird/js/Heap.hpp` (`JsObject::not_constructor` is the bit to set) | ARCHITECTURE.md, Phase 34, section 9; four failing cases of `prototypes.dat` | Six host constructors have no `prototype` (`Response`, `Headers`, `Worker`, `WebSocket`, `IntersectionObserver`, `ResizeObserver`): give them one before `instanceof` throws for a missing one |
| **A script can read and write the engine's own slots**: they are properties whose names begin with `%`, hidden from enumeration and from nothing else. After `el.classList` was read, `el['%classList'] = 5` makes `el.classList` a number; `method['%home']` is the object `super` is looked up from, and assigning to it changes what `super` finds. The other side of it: a property of the script's own whose name begins with `%`, `#` or `@@`, or has the shape `__x__`, is left out of `Object.keys`, `JSON.stringify`, `for…in`, `Object.assign` and spread, so `var m = {}; m['#/route'] = 'home'; JSON.stringify(m)` is `{}` | `src/js/DomBindings.cpp` (`slot`, `set_slot`, `clear_slot`), `src/js/JsEval.cpp` (`%home`, `%fn`, `%new.target`), `src/js/JsValue.cpp` (`is_hidden_key`) | AUDIT.md, finding 41 | Move the slots off the property table (Phase 34 moved one: `JsObject::not_constructor`), so that no name is special; refusing such a key from a script would only trade one fault for the other. Not beside a row that changes `src/js/Dom*Bindings.cpp` |
| **Array destructuring reads an array's elements without asking for its iterator.** `var a = [1, 2]; a[Symbol.iterator] = function* () { yield 9; }; var [x] = a` gives 1 where the language gives 9. With the iterator deleted from `Array.prototype`, `var [x] = [1]` still works where it is a `TypeError`. `var [x] = { next() { … } }` takes an object that only has `next` for an iterator. A parameter (`function f([p, q])`) and an assignment pattern go the same way | `src/js/JsEval.cpp` (`IterationCursor`) | ARCHITECTURE.md, Phase 63, section 6; two failing cases of `tests/js-conformance/iteration.dat` | `IterationCursor` has an array mode of its own, chosen by `is_array()`. `js_iterates_as_builtin(value)` (`JsOps.hpp`) is the question to ask in its place: true exactly when reading the elements is what the iterator would give. For the rest, `js_iterator_method(value, method)` and the protocol; a string already goes through `js_iterable_to_vector`. Not done in Phase 63 because `JsEval.cpp` belonged to open pull requests then (#68, #71) |
| **`Array.from` decides for itself what is iterable.** Whatever `is_array()` or is a string it takes for iterable without asking. So with the iterator deleted from `Array.prototype`, `Array.from([1, 2])` is a `TypeError` where the language reads the array as array-like and gives `[1, 2]` (`main` before Phase 63 gave that, by never asking anything), and the same for a string. A number whose prototype was given an iterator is not asked (`Array.from(3)` is `[]`). And it collects every value before it maps the first: a mapper that throws does not close the iterator, and an iterator without an end hangs it where the mapper would have stopped it | `src/js/BuiltinsArray.cpp` (`Array.from`) | ARCHITECTURE.md, Phase 63, section 6; three failing cases of `iteration.dat` | `js_iterator_method(source, method)` is the language's GetMethod: no method means array-like, for an array as for anything else. With a method, map inside the visitor of `js_iterate`, which closes the iterator when the visitor throws. Not done in Phase 63 because `BuiltinsArray.cpp` belonged to an open pull request then (#63) |
| **The default constructor of a subclass spreads its arguments.** For a class that has no constructor the parser writes `constructor(...args) { super(...args); }`, and since Phase 63 a spread asks the array for its iterator. So a script that replaced `Array.prototype[Symbol.iterator]` is called by `new Derived(1, 2)`, once for each default constructor on the way up, where the language hands the arguments on without iterating ("does not use argument evaluation", in the words of the test); a replacement that throws makes every such `new` throw. test262's `language/statements/class/subclass/default-constructor-spread-override.js` passed before Phase 63, when nothing asked an array, and fails now | `src/js/JsParser.cpp` (where the default constructor is made), `src/js/JsEval.cpp` (`eval_arguments`) | ARCHITECTURE.md, Phase 63, section 6; one failing case of `iteration.dat` | Mark the spread the parser makes (a flag on its `SpreadExpr`) and, for a marked one, copy the rest array's elements in `eval_arguments` without `js_iterate`. A constructor that a script writes with the same text has to keep asking. Not done in Phase 63 because both files belonged to an open pull request then (#68) |
| **`yield*` in an async generator does not ask for `Symbol.asyncIterator`.** It runs the sync protocol (`js_iterate`) whatever kind of generator it is in. `yield* inner()` over another async generator is a `TypeError` (`… is not iterable`); on `main` before Phase 63 it never returned, because an object with `next` and no `Symbol.iterator` was taken for an iterator and a promise has no `done`. Three test262 tests of async delegation passed only through that and fail since Phase 63 (`yield-star-getiter-async-return-method-is-null.js`, `yield-star-normal-notdone-iter-value-throws.js`, `yield-star-promise-not-unwrapped.js`), and one that failed passes, for no better reason | `src/js/JsEval.cpp` (`eval_yield`) | ARCHITECTURE.md, Phase 63, section 6; one failing case of `iteration.dat` | In an async generator: the method under `kAsyncIteratorKey`, or without one the sync method with each result awaited; `next`, `throw` and `return` handed on, each result awaited, the value yielded without a second await. `for await`, in the same file, already finds the async iterator. Not done in Phase 63 because `JsEval.cpp` belonged to open pull requests then (#68, #71) |
| **What an iterator is, in small**: `Array.prototype[Symbol.iterator]` is not `Array.prototype.values` (they are two functions, and its `name` is `@@iterator`), `Map.prototype[Symbol.iterator]` is not `entries`, and `Set`'s `values` and `keys` are not one function. An array iterator has `next` as a property of its own, where the language has one `next` on a prototype that a script can replace, and every kind of iterator has the same prototype (`Object.prototype.toString` gives `[object Object]`). Every built-in iterable also has a property whose name is the text `Symbol.iterator`, which `Object.getOwnPropertyNames` lists; and the symbol itself is a property named `@@iterator`, so `'@@iterator' in []` is true and an object with a property of that name is iterable. `arguments` has no `Symbol.iterator` of its own. An object whose prototype is an array has no `length` and no elements | `src/js/BuiltinsArray.cpp`, `src/js/BuiltinsCollections.cpp`, `src/js/JsValue.cpp` | ARCHITECTURE.md, Phase 63, section 6; five failing cases of `iteration.dat` | The day array iterators take `next` from a prototype they share, `js_iterates_as_builtin` has to compare that function too (the comment in it says so): the direct read is right now because nothing a script can reach stands between the installed method and the elements. Since Phase 63 nothing reads the properties named `Symbol.iterator` except `Array.from` and `IterationCursor` (the rows above) and `has_property` in `JsValue.cpp` |
| **A refused property write is refused in silence**: assigning to an out-of-range index of an `HTMLCollection` (`l[5] = x`) must throw a `TypeError` in strict mode, and does nothing at all here (2 subtests, word `strict-write`). `JsExotic::set` answers "handled" with no way to say "refused", and the strictness of the assignment is read in `JsEval.cpp`. The attribute block beside it — `Attr` as an object with `cloneNode`/`isSameNode`/`lookupNamespaceURI`, one qualified name in two namespaces, `NamedNodeMap`/`HTMLCollection` named own properties, `importNode(attr)`, an `Attr` adopted with its element — landed in Phase 51 | `src/js/DomCollectionBindings.cpp`, `include/litebird/js/Heap.hpp` (`JsExotic`), `src/js/JsValue.cpp`, `src/js/JsEval.cpp` | ARCHITECTURE.md, Phase 51, sections 3, 4 and 6; AUDIT.md, finding 57 | The write path (`JsValue::set_property`, `JsEval.cpp`) was claimed by PR #50 (Phase 43) when this row was written; that has merged, and the files are free |
| **What the DOM corpus cannot ask**: `getComputedStyle(el).width` is still `auto` (used values are available via `getBoundingClientRect()`/`offset*`/`client*` since Phase 36), style sheet objects (`sheet.cssRules`, `insertRule`), `document.write`, the SVG DOM (`className.baseVal`, `viewBox`, `getBBox()`: `tests/dom-conformance/open/dates-and-svg.js`) | `src/js/Dom*Bindings.cpp`, `src/main.cpp`, `src/shell/` | ARCHITECTURE.md, Phase 27, section 12 & Phase 36 | jsdom does none of the first two, so each needs a reference of its own before it is built |
| **Every box carries its own copy of a 920-byte computed style**: 100,000 times `word <b>bold</b>` takes 1.3 GB | `src/layout/`, `src/css/` | AUDIT.md, findings 12 and 28 | Same files as the layout rows |
| **Iterating a `Map` or a `Set` gets slower every time it is done.** `for (e of map)` over a `Map` of three entries takes 12 microseconds a loop for the first thousand loops and 154 for the eighth thousand (a `Set`: 8 and 129), where `map.forEach` stays at 2.5 and `for…of` over an array at 1.1. `CollectionData::new_cursor` keeps a weak reference to the cursor of every iterator that was ever made on the collection, so that removing entries can move them, and from the 64th on it walks the whole list for each new iterator to drop the dead ones; none is dead before a collection, and there is none without `gc()` (the row below). After `gc()` the same loop is back at 20. Pruning when the list has doubled since the last pruning would make it constant | `src/js/BuiltinsCollections.cpp` | AUDIT.md, finding 69 ("What each instrument found"); `./build/litebird --run-js` of the loop in that row | Found by a benchmark of Phase 63 whose line for a `Map` did not finish. Small, and nothing else needs the file |
| **Memory is reclaimed only when a script calls `gc()`**, and no leak audit has been done. One leak is known: a function bound to an object that holds it (`o.b = f.bind(o)`, 2,000 times: 2,000 cells left after `gc()`), and what a pending timer holds. The collector cannot see into what a native function captured. What it costs meanwhile: `for (i = 0; i < n; i++) o = { a: i }` keeps about 1 KB for each object it has dropped (433 MB of resident memory after 400,000), so a loop of three million takes more than 3 GB; a benchmark written for Phase 43 brought a 4 GB machine to a stop that way until it called `gc()` between its loops | `src/js/Heap.cpp`, and everything that allocates script objects | Known gaps, 4; ARCHITECTURE.md, Phase 34, section 9; one failing case of `tests/js-conformance/prototypes.dat` | Touches everything that makes script objects, the DOM bindings included: not beside a row that changes `src/js/Dom*Bindings.cpp` |
| **The corpus has moved to web-platform-tests** (1,973 cases, 1,868 passed before Phase 49's `<select>` work): vendor it, read its `#description` sections, add processing instructions | `tools/conformance/`, `tests/html5lib/`, `src/html/`, `include/litebird/dom/` | AUDIT.md, finding 30 | Touches `src/html/HtmlTokenizer.cpp`, `src/html/HtmlTreeBuilder.cpp`, and `src/html/HtmlTreeBuilderModes.cpp`. The node type is there since Phase 28 (`include/litebird/dom/ProcessingInstruction.hpp`, its interface, `<?target data>` in the markup serializer); what is left is the tokenizer and the tree builder |
| **Animated WebP, AVIF, and SVG `<text>`** | `src/render/WebPDecoder.cpp`, `src/render/ImageDecoder.cpp`, `src/render/SvgRenderer.cpp` | ARCHITECTURE.md, Phase 36, Phase 38 & Phase 54; [`docs/phases/57-lossy-webp-vp8-key-frame.md`](docs/phases/57-lossy-webp-vp8-key-frame.md); AUDIT.md, findings 43, 44 & 60; [`docs/findings/63-lossy-webp-vp8-refused.md`](docs/findings/63-lossy-webp-vp8-refused.md) | PNG, JPEG, GIF, **lossless WebP** (`VP8L`, all four transforms, meta prefix codes, colour cache), **lossy WebP** (a VP8 key frame: the boolean decoder, the 16x16/8x8/4x4 intra predictors, the 4x4 IDCT and the Walsh–Hadamard transform, the loop filter, plus the `VP8X` container and an `ALPH` plane in either compression method and all three spatial filters), SVG, BMP, and PPM are wired through `ImageDecoder::decode()`, and WebP is refused by status for the animated variant only. Canvas 2D text is done (Phase 46, finding 52). What is left: animated WebP (`ANMF` compositing and disposal), AVIF, and SVG `<text>` |
| **Migrate the 43 phase write-ups out of `ARCHITECTURE.md` and retire the 53 resolved findings in `AUDIT.md`** | `docs/phases/`, `docs/findings/`, `ARCHITECTURE.md`, `AUDIT.md` | AGENTS.md, "Notice — read this first"; `docs/README.md` | A move, not a rewrite: one file per section, the four things a write-up says kept in their order. `ARCHITECTURE.md` is 408 KB and 84% of it is these sections; `AUDIT.md` is 282 KB and 53 of its 61 findings are resolved. `python3 tools/ci/check_docs.py` has to stay green, and its budgets come **down** as the files shrink, not up to let a pull request pass. The floors in `docs/state.json` must not go down either. Not beside a row that edits either document |
