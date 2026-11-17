# Working in this repository

Litebird is written by AI agents, at times more than one at once. Each agent
has a branch of its own. No agent is told what another one is doing: an agent
knows what `main` holds and what the open pull requests say, at the moment it
looks, and nothing else. Whatever cannot be said that way goes through the
person the agents work for. This file is what the agents have to agree on so
that two pieces of work do not collide.

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

   `P` is the next free phase number (the phase table in ARCHITECTURE.md,
   plus the open pull requests) and `F` the next free finding number
   (AUDIT.md, plus the open pull requests). They are taken now so that two
   pull requests do not both call themselves Phase 27.
3. **One task at a time, inside its files.** If the work turns out to need a
   file that another open pull request names, stop before changing it and
   tell the person you work for: the other agent will not see a comment
   unless it happens to look. A bug found outside the task becomes a new row
   of the table; it is not fixed in passing.

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
- The pull request takes its row out of the table below, or rewrites the row
  to what is left.
- The `WIP:` comes off the title. Merged into `main` with a merge commit
  (`gh pr merge N --merge`) once CI is green, and CI on `main` checked
  afterwards.

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
| **`TextDecoder` and `TextEncoder` are not exposed to script**, and there is no encoder: the WHATWG Encoding Standard's 40 decoders and 228 labels landed in Phase 47 (`<meta charset>` prescan, `document.characterSet`, XML `[81] EncName`, `@charset`, `data:` URLs, XHR, subresources, and 2,543 `encoding/` WPT subtests), but a page that constructs `new TextDecoder('shift_jis')` gets a `ReferenceError`, `XMLSerializer` does not consult `document.characterSet` for an output declaration, and the prescan is one pass over the first 1,024 bytes with no streaming | `src/net/TextEncoding.cpp`, `include/litebird/net/TextEncoding.hpp`, `src/js/DomWindowBindings.cpp`, `src/js/DomBindings.cpp` | ARCHITECTURE.md, Phase 47, section 5; AUDIT.md, finding 53 | The decoder side is done and the `encoding/` pages that need no globals are vendored; what is left is the script API and the encoders (Big5's `U+00CA`/`U+00EA`, `gb18030`'s ranges in the other direction) |
| **An upgrade runs the constructor on the element itself.** The standard constructs a definition's constructor the ordinary way, with a new object as `this`; the element gets the class's prototype when `HTMLElement` hands it out, and the upgrade fails unless the constructor returns the element. `run_upgrade` gives the element the prototype first and passes its object as `this`. A class cannot tell, nor a compiled constructor that does what it is for. In a constructor written as a function `this === self` is `true`; one that never reaches `HTMLElement` (`function Plain() { this.touched = true; }`) has written on the element and its upgrade counts as done; after a constructor that throws early the element keeps the class's prototype (for a class that extends `Event` and was given to `customElements.define`, an element without `getAttribute`). Beside it: a customized built-in whose upgrade failed still matches `:defined` (`Element::is_defined`), and an element that moves from a frame's document into the page is not upgraded by the page's definition, where one from `createHTMLDocument` or `DOMParser` is | `src/js/DomObserverBindings.cpp` (`run_upgrade`, `construct_custom_element`), `src/js/JsValue.cpp` ("Constructions in place"), `src/js/JsEval.cpp` (`eval_super_call`), `src/dom/FormState.cpp` (`Element::is_defined`) | ARCHITECTURE.md, Phase 43, section 5; `tests/dom-conformance/open/compiled-custom-elements.js` (5 lines, 4 differ) | Phase 43 could not change the DOM bindings (they were #49's, which has merged since: the file is free), so its construction stack is beside the call context: `JsValue::construct` notes a construction whose `this` is an element, and `current_derived_this()` gives that element to its interface, once, whether `super()` or `Reflect.construct` reached it. Its place is the definition: `run_upgrade` pushes the element, constructs the ordinary way and compares what comes back; `construct_custom_element` takes the element from the stack and sets the prototype then. With that, `JsValue::construct` need not look at `this` any more, `current_derived_this()` loses its case for an interface that is not the element's, and `eval_super_call` the one for a parent that is no interface: a constructor that does not return the element fails the comparison. The 25 cases of `tests/dom-conformance/compiled.dat` say what has to keep working |
| **`eval` inside a function is global code.** `function f() { var local = 5; return eval('local + 1'); }` is a `ReferenceError`; the function's `this`, `arguments`, `new.target` and `super` are not seen either (`eval('super()')` in a constructor is a `SyntaxError`), and a `var` the evaluated text declares becomes a global | `src/js/JsInterpreter.cpp` (the `eval` global), `src/js/JsEval.cpp`, `src/js/JsParser.cpp` (where `super` and `new.target` may stand) | ARCHITECTURE.md, Phase 43, section 8; one failing case of `construct.dat` | Only a call written `eval(...)` is direct and runs in the scope it stands in; `(0, eval)(...)` and `window.eval(...)` stay global, as they are now |
| **A private name is a property whose name begins with `#`.** `obj['#p']` reads and writes a private field from outside its class, and `'#p' in obj` is `true`; `o.#p` for an object without the field is `undefined` and assigning to it adds one, where both are a `TypeError`; one class reads another's `#p`; `#p in o` is a `SyntaxError` | `src/js/JsParser.cpp`, `src/js/JsEval.cpp`, `src/js/JsValue.cpp` (`is_hidden_key`) | ARCHITECTURE.md, Phase 43, section 8 | A private name is an identity made when the class is evaluated, not a string: two classes that each have a `#p` have two names. The same question as the row on the engine's `%` slots, and the same files: not beside it |
| **A built-in does not make the subclass it was called on**: with `class List extends Array {}`, `List.from([1])`, `List.of(1)` and `new List().map(f)` are plain arrays; `P.resolve(1)` for a subclass of `Promise` is a plain promise. There is no `Symbol.species`, and `instanceof` does not ask `Symbol.hasInstance`. `new Map(iterable)` and `new Set(iterable)` fill the object without calling its `set` and `add`, so a subclass that overrides one is not asked | `src/js/BuiltinsArray.cpp`, `src/js/Promise.cpp`, `src/js/BuiltinsCollections.cpp`, `src/js/JsOps.cpp` (`js_instance_of`) | ARCHITECTURE.md, Phase 34, section 9; three failing cases of `prototypes.dat` | A native constructor can ask which function `new` was applied to since Phase 43 (`JsValue::current_new_target()`); `Reflect.construct(Array, [], G)` already gets `G.prototype` |
| **What a function is, in small**: nothing tells a built-in that is a constructor from one that is not (`new Math.max()` makes an object; `class X extends f.bind(null)` and `Reflect.construct(F, [], Date.now)` are accepted; `x instanceof` a function with no `prototype` answers `false` where it should throw, and a bound function is not asked for its target); `Object.getOwnPropertyNames(f)` lacks `length` and `name`, and `f.name = 'x'` takes effect; async and generator functions have `Function.prototype` itself as their prototype, where the language has an object for each kind between the two (three test262 tests, `AsyncFunctionPrototype-is-not-callable.js` and its relatives, passed while `Function.prototype` was not callable and fail since Phase 34); a property of a prototype that cannot be written does not stop an assignment to the object that inherits it | `src/js/JsOps.cpp`, `src/js/JsValue.cpp`, `src/js/BuiltinsObject.cpp` (`bind`), `include/litebird/js/Heap.hpp` (`JsObject::not_constructor` is the bit to set) | ARCHITECTURE.md, Phase 34, section 9; four failing cases of `prototypes.dat` | Six host constructors have no `prototype` (`Response`, `Headers`, `Worker`, `WebSocket`, `IntersectionObserver`, `ResizeObserver`): give them one before `instanceof` throws for a missing one |
| **A script can read and write the engine's own slots**: they are properties whose names begin with `%`, hidden from enumeration and from nothing else. After `el.classList` was read, `el['%classList'] = 5` makes `el.classList` a number; `method['%home']` is the object `super` is looked up from, and assigning to it changes what `super` finds. The other side of it: a property of the script's own whose name begins with `%`, `#` or `@@`, or has the shape `__x__`, is left out of `Object.keys`, `JSON.stringify`, `for…in`, `Object.assign` and spread, so `var m = {}; m['#/route'] = 'home'; JSON.stringify(m)` is `{}` | `src/js/DomBindings.cpp` (`slot`, `set_slot`, `clear_slot`), `src/js/JsEval.cpp` (`%home`, `%fn`, `%new.target`), `src/js/JsValue.cpp` (`is_hidden_key`) | AUDIT.md, finding 41 | Move the slots off the property table (Phase 34 moved one: `JsObject::not_constructor`), so that no name is special; refusing such a key from a script would only trade one fault for the other. Not beside a row that changes `src/js/Dom*Bindings.cpp` |
| **Array iteration skips a custom `Symbol.iterator`**: `var a=[1]; a[Symbol.iterator]=function(){return [9][Symbol.iterator]()}; Promise.all(a)` resolves to `[1]`, while Node resolves to `[9]`; a throwing Promise.resolve cannot close the array's overridden iterator because `js_iterate` takes its raw elements instead | `src/js/JsOps.cpp` (`js_iterate`) | AUDIT.md, finding 51; ARCHITECTURE.md, Phase 45, section 4 | Phase 45 fixes the four Promise combinators for general iterators, not this shared fast path. `JsOps.cpp` was claimed by PR #50 (Phase 43) when this row was written; that has merged, and the file is free |
| **A refused property write is refused in silence**: assigning to an out-of-range index of an `HTMLCollection` (`l[5] = x`) must throw a `TypeError` in strict mode, and does nothing at all here (2 subtests, word `strict-write`). `JsExotic::set` answers "handled" with no way to say "refused", and the strictness of the assignment is read in `JsEval.cpp`. The attribute block beside it — `Attr` as an object with `cloneNode`/`isSameNode`/`lookupNamespaceURI`, one qualified name in two namespaces, `NamedNodeMap`/`HTMLCollection` named own properties, `importNode(attr)`, an `Attr` adopted with its element — landed in Phase 51 | `src/js/DomCollectionBindings.cpp`, `include/litebird/js/Heap.hpp` (`JsExotic`), `src/js/JsValue.cpp`, `src/js/JsEval.cpp` | ARCHITECTURE.md, Phase 51, sections 3, 4 and 6; AUDIT.md, finding 57 | The write path (`JsValue::set_property`, `JsEval.cpp`) was claimed by PR #50 (Phase 43) when this row was written; that has merged, and the files are free |
| **What the DOM corpus cannot ask**: `getComputedStyle(el).width` is still `auto` (used values are available via `getBoundingClientRect()`/`offset*`/`client*` since Phase 36), style sheet objects (`sheet.cssRules`, `insertRule`), `document.write`, the SVG DOM (`className.baseVal`, `viewBox`, `getBBox()`: `tests/dom-conformance/open/dates-and-svg.js`) | `src/js/Dom*Bindings.cpp`, `src/main.cpp`, `src/shell/` | ARCHITECTURE.md, Phase 27, section 12 & Phase 36 | jsdom does none of the first two, so each needs a reference of its own before it is built |
| **Every box carries its own copy of a 920-byte computed style**: 100,000 times `word <b>bold</b>` takes 1.3 GB | `src/layout/`, `src/css/` | AUDIT.md, findings 12 and 28 | Same files as the layout rows |
| **Memory is reclaimed only when a script calls `gc()`**, and no leak audit has been done. One leak is known: a function bound to an object that holds it (`o.b = f.bind(o)`, 2,000 times: 2,000 cells left after `gc()`), and what a pending timer holds. The collector cannot see into what a native function captured. What it costs meanwhile: `for (i = 0; i < n; i++) o = { a: i }` keeps about 1 KB for each object it has dropped (433 MB of resident memory after 400,000), so a loop of three million takes more than 3 GB; a benchmark written for Phase 43 brought a 4 GB machine to a stop that way until it called `gc()` between its loops | `src/js/Heap.cpp`, and everything that allocates script objects | Known gaps, 4; ARCHITECTURE.md, Phase 34, section 9; one failing case of `tests/js-conformance/prototypes.dat` | Touches everything that makes script objects, the DOM bindings included: not beside a row that changes `src/js/Dom*Bindings.cpp` |
| **The corpus has moved to web-platform-tests** (1,973 cases, 1,868 passed before Phase 49's `<select>` work): vendor it, read its `#description` sections, add processing instructions | `tools/conformance/`, `tests/html5lib/`, `src/html/`, `include/litebird/dom/` | AUDIT.md, finding 30 | Touches `src/html/HtmlTokenizer.cpp`, `src/html/HtmlTreeBuilder.cpp`, and `src/html/HtmlTreeBuilderModes.cpp`. The node type is there since Phase 28 (`include/litebird/dom/ProcessingInstruction.hpp`, its interface, `<?target data>` in the markup serializer); what is left is the tokenizer and the tree builder |
| **Lossy `VP8`, animated WebP, AVIF, and SVG `<text>`** | `src/render/WebPDecoder.cpp`, `src/render/ImageDecoder.cpp`, `src/render/SvgRenderer.cpp` | ARCHITECTURE.md, Phase 36, Phase 38 & Phase 54; AUDIT.md, findings 43, 44 & 60 | PNG, JPEG, GIF, **lossless WebP** (`VP8L`, all four transforms, meta prefix codes, colour cache), SVG, BMP, and PPM are wired through `ImageDecoder::decode()`, and WebP is refused by status for the lossy and animated variants. Canvas 2D text is done (Phase 46, finding 52). What is left: the lossy `VP8` codec (a VP8 video keyframe), animated WebP (`ANMF` compositing), AVIF, and SVG `<text>` |
