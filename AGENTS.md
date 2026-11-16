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
  build, and CI's log cannot be read from every sandbox: `make asan` here
  is the only way to see why.
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
| **Cross-origin `WindowProxy` own-property enumeration and `Location` setter navigation**: `iframe.contentDocument`/`contentWindow`, `src`/`srcdoc`/`sandbox`, cross-origin `WindowProxy` property get/set `SecurityError` checks, `postMessage`, subdocument layout/rasterization, and `.xhtml` WPT execution landed in Phase 42 (resolving all 495 `frames` WPT failures); still open are cross-origin `Object.getOwnPropertyDescriptor`/`Reflect.ownKeys` filtering on `WindowProxy`, cross-origin `childWin.location = url` navigation, and `MessageChannel`/`MessagePort` | `src/js/DomWindowBindings.cpp`, `src/js/BuiltinsObject.cpp` | ARCHITECTURE.md, Phase 42; AUDIT.md, finding 48 | `make wpt` is at 7,968/8,009 across 103 files (0 `frames` failures) |
| **`TextDecoder` and `TextEncoder` are not exposed to script**, and there is no encoder: the WHATWG Encoding Standard's 40 decoders and 228 labels landed in Phase 47 (`<meta charset>` prescan, `document.characterSet`, XML `[81] EncName`, `@charset`, `data:` URLs, XHR, subresources, and 2,543 `encoding/` WPT subtests), but a page that constructs `new TextDecoder('shift_jis')` gets a `ReferenceError`, `XMLSerializer` does not consult `document.characterSet` for an output declaration, and the prescan is one pass over the first 1,024 bytes with no streaming | `src/net/TextEncoding.cpp`, `include/litebird/net/TextEncoding.hpp`, `src/js/DomWindowBindings.cpp`, `src/js/DomBindings.cpp` | ARCHITECTURE.md, Phase 47, section 5; AUDIT.md, finding 53 | The decoder side is done and the `encoding/` pages that need no globals are vendored; what is left is the script API and the encoders (Big5's `U+00CA`/`U+00EA`, `gb18030`'s ranges in the other direction) |
| **`new.target` is the function that is running**, not the one `new` was applied to. `if (new.target === Shape) throw …` in a parent's constructor stops `new Circle()`; inside `F`, `Reflect.construct(F, [], G)` has `new.target === F`; and `Reflect.construct(HTMLElement, [], Derived)` is `Illegal constructor`, so a custom element compiled by Babel cannot be made. Also here: an object that a parent's constructor returns does not become `this` in the child | `src/js/JsEval.cpp` (`invoke_function`, `eval_super_call`, the `%new.target` binding), `src/js/JsOps.cpp` (`js_construct`), `src/js/DomObserverBindings.cpp` (`construct_custom_element`) | ARCHITECTURE.md, Phase 34, section 9; one failing case of `tests/js-conformance/prototypes.dat`; `tests/dom-conformance/open/compiled-custom-elements.js` (4 of 9 lines as jsdom) | `js_construct` already takes the third argument and gives the object its `prototype`; what is missing is the value inside the function and through `super()`. Check with `make test262` before and after (`language/expressions/new.target`, `built-ins/Reflect/construct`) |
| **A built-in does not make the subclass it was called on**: with `class List extends Array {}`, `List.from([1])`, `List.of(1)` and `new List().map(f)` are plain arrays; `P.resolve(1)` for a subclass of `Promise` is a plain promise. There is no `Symbol.species`, and `instanceof` does not ask `Symbol.hasInstance` | `src/js/BuiltinsArray.cpp`, `src/js/Promise.cpp`, `src/js/BuiltinsCollections.cpp`, `src/js/JsOps.cpp` (`js_instance_of`) | ARCHITECTURE.md, Phase 34, section 9; three failing cases of `prototypes.dat` | Needs the row above for `new.target` only where a built-in is called through `Reflect.construct` |
| **What a function is, in small**: nothing tells a built-in that is a constructor from one that is not (`new Math.max()` makes an object; `class X extends f.bind(null)` is accepted; `x instanceof` a function with no `prototype` answers `false` where it should throw, and a bound function is not asked for its target); `Object.getOwnPropertyNames(f)` lacks `length` and `name`, and `f.name = 'x'` takes effect; async and generator functions have `Function.prototype` itself as their prototype, where the language has an object for each kind between the two (three test262 tests, `AsyncFunctionPrototype-is-not-callable.js` and its relatives, passed while `Function.prototype` was not callable and fail since Phase 34); a property of a prototype that cannot be written does not stop an assignment to the object that inherits it | `src/js/JsOps.cpp`, `src/js/JsValue.cpp`, `src/js/BuiltinsObject.cpp` (`bind`), `include/litebird/js/Heap.hpp` (`JsObject::not_constructor` is the bit to set) | ARCHITECTURE.md, Phase 34, section 9; four failing cases of `prototypes.dat` | Six host constructors have no `prototype` (`Response`, `Headers`, `Worker`, `WebSocket`, `IntersectionObserver`, `ResizeObserver`): give them one before `instanceof` throws for a missing one |
| **A script can read and write the engine's own slots**: they are properties whose names begin with `%`, hidden from enumeration and from nothing else. After `el.classList` was read, `el['%classList'] = 5` makes `el.classList` a number; `method['%home']` is the object `super` is looked up from, and assigning to it changes what `super` finds | `src/js/DomBindings.cpp` (`slot`, `set_slot`, `clear_slot`), `src/js/JsEval.cpp` (`%home`, `%fn`, `%new.target`), `src/js/JsValue.cpp` (`is_hidden_key`) | AUDIT.md, finding 41 | Either refuse such a key in `get_property`/`set_property` when it comes from a script, or move the slots off the property table (Phase 34 moved one: `JsObject::not_constructor`). Not beside a row that changes `src/js/Dom*Bindings.cpp` |
| **Completing a surrogate pair after the half was read copies the string**: `s += '\uD83D'; n += s.indexOf('zz'); s += '\uDE00'` in a loop is quadratic (58/242/893 ms at 5,000/10,000/20,000). Reading the text settles the owed half into the block, and the four bytes of the pair start three bytes inside it, so the join copies; the same for a piece from `substring` that ends in a high half and is then completed | `src/js/JsString.cpp`, `include/litebird/js/JsString.hpp` | AUDIT.md, finding 47; ARCHITECTURE.md, Phase 41, section 5 | Needs text that can be read without being flattened (a rope, or UTF-16 storage): a change of representation, not of the join. Building half a pair at a time without reading it is linear since Phase 41 |
| **Array iteration skips a custom `Symbol.iterator`**: `var a=[1]; a[Symbol.iterator]=function(){return [9][Symbol.iterator]()}; Promise.all(a)` resolves to `[1]`, while Node resolves to `[9]`; a throwing Promise.resolve cannot close the array's overridden iterator because `js_iterate` takes its raw elements instead | `src/js/JsOps.cpp` (`js_iterate`) | AUDIT.md, finding 51; ARCHITECTURE.md, Phase 45, section 4 | Phase 45 fixes the four Promise combinators for general iterators, not this shared fast path. `JsOps.cpp` is claimed by PR #50; do not change it beside that work |
| **An `Attr` is not a `Node`** (`attr.cloneNode()`, `importNode(attr)`, an `Attr` adopted with its element: 10 subtests); **one element cannot have two attributes with one qualified name** in different namespaces (14); a `NamedNodeMap` and an `HTMLCollection` do not list their names as own properties (8) | `src/js/DomCollectionBindings.cpp`, `include/litebird/dom/AttributeList.hpp`, `include/litebird/dom/Element.hpp` | `tests/wpt/README.md`, the words `attr-is-not-a-node`, `one-name-two-namespaces`, `named-properties` | Attributes are stored under their qualified names; the second needs them stored under namespace and local name. Only a script can make such a pair |
| **What the DOM corpus cannot ask**: `getComputedStyle(el).width` is still `auto` (used values are available via `getBoundingClientRect()`/`offset*`/`client*` since Phase 36), style sheet objects (`sheet.cssRules`, `insertRule`), `document.write`, the SVG DOM (`className.baseVal`, `viewBox`, `getBBox()`: `tests/dom-conformance/open/dates-and-svg.js`) | `src/js/Dom*Bindings.cpp`, `src/main.cpp`, `src/shell/` | ARCHITECTURE.md, Phase 27, section 12 & Phase 36 | jsdom does none of the first two, so each needs a reference of its own before it is built |
| **Every box carries its own copy of a 920-byte computed style**: 100,000 times `word <b>bold</b>` takes 1.3 GB | `src/layout/`, `src/css/` | AUDIT.md, findings 12 and 28 | Same files as the layout rows |
| **Memory is reclaimed only when a script calls `gc()`**, and no leak audit has been done. One leak is known: a function bound to an object that holds it (`o.b = f.bind(o)`, 2,000 times: 2,000 cells left after `gc()`), and what a pending timer holds. The collector cannot see into what a native function captured | `src/js/Heap.cpp`, and everything that allocates script objects | Known gaps, 4; ARCHITECTURE.md, Phase 34, section 9; one failing case of `tests/js-conformance/prototypes.dat` | Touches everything that makes script objects, the DOM bindings included: not beside a row that changes `src/js/Dom*Bindings.cpp` |
| **`<select>` content as the standard now has it**: the 11 corpus cases that fail | `src/html/HtmlTreeBuilder.cpp`, `src/html/HtmlTreeBuilderModes.cpp` | AUDIT.md, finding 30 | parse5 has the older rules too, so it cannot be the reference |
| **The corpus has moved to web-platform-tests** (1,973 cases, 1,868 pass): vendor it, read its `#description` sections, add processing instructions | `tools/conformance/`, `tests/html5lib/`, `src/html/`, `include/litebird/dom/` | AUDIT.md, finding 30 | Same files as the row above. The node type is there since Phase 28 (`include/litebird/dom/ProcessingInstruction.hpp`, its interface, `<?target data>` in the markup serializer); what is left is the tokenizer and the tree builder |
| **WebP/AVIF decoding, SVG `<text>`, and Canvas 2D text (`fillText`/`measureText`)** | `src/render/ImageDecoder.cpp`, `src/render/SvgRenderer.cpp`, `src/render/Canvas2D.cpp` | ARCHITECTURE.md, Phase 36 & Phase 38; AUDIT.md, findings 43 & 44 | PNG, JPEG, GIF, SVG, BMP, and PPM are now wired through `ImageDecoder::decode()` |
