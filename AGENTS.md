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

## Open tasks

Rows that share files should not be in work at the same time.

| Task | Files | Described in | Note |
| :--- | :--- | :--- | :--- |
| **`XMLHttpRequest` answers `data:` URLs only**: `http:`, `https:` and relative addresses end in `error`. Requests over the network, with the same-origin rule and what CORS allows; `responseType = 'arraybuffer'` (null: the engine has no `ArrayBuffer`) | `src/js/DomWindowBindings.cpp` (`perform`), `src/net/` | ARCHITECTURE.md, Phase 28, sections 6 and 10 | Phase 33 added `ResourceLoader::load_for_page(url, context)`; use that boundary, not `load_navigation`. The unit suite `xml_http_request_answers_data_urls_and_nothing_else` asserts that a request reads no file, and must keep asserting it |
| **Worker script loading has no MIME/module policy**: a successful load is run as classic JavaScript whatever its `Content-Type`, `{type:"module"}` is ignored, and `addEventListener("error", …)` listeners are stored but not called (only `onerror` is) | `src/js/Worker.cpp`, `include/litebird/js/Worker.hpp` | ARCHITECTURE.md, Phase 33, section 5 | The local-file boundary is fixed; this is the remaining worker fetch/EventTarget policy. Shares files with the external-script row's interpreter entry points |
| **Cross-origin HTTP `fetch()` is not CORS-checked**: any web/document-local context may read an `http:` response the socket client can reach; `https:` is not implemented | `src/net/ResourceLoader.cpp`, `src/net/Fetch.cpp`, `src/js/DomWindowBindings.cpp` | ARCHITECTURE.md, Phase 33, section 5; AUDIT.md, finding 37 | Same network policy work as the XHR row above; keep the Phase 33 local-file denial and its tests |
| **The command line loads every file as HTML**, and there are no frames with documents: `litebird feed.xml` or `drawing.svg` should go through the XML parser (`parse_xml_document`), and `iframe.contentDocument` should exist. 495 of the 534 web-platform-tests subtests that fail need one or the other (`frames` in `tests/wpt/expected-failures.txt`) | `src/main.cpp`, `src/js/JsInterpreter.cpp`, `src/shell/`, `tools/conformance/run_wpt.py` | ARCHITECTURE.md, Phase 28, section 10; `tests/wpt/README.md` | Same files as the row "External scripts and stylesheets are not loaded". The `.xhtml` tests of `domparsing/` are left out of `tests/wpt` for this reason: add them back |
| **Four text encodings**: UTF-8, UTF-16 in both orders and windows-1252, and only for XML and for responses. A page is read as UTF-8 whatever it declares (`<meta charset>` is not looked for), `document.characterSet` is always `UTF-8`, a label such as `shift_jis` is taken for no label, and there is no `TextDecoder` | `src/net/DataUrl.cpp` (`decode_text`), `src/xml/XmlParser.cpp` (`decode_xml`), `src/main.cpp`, `src/html/` | ARCHITECTURE.md, Phase 28, section 10 | The Encoding Standard has the tables (`index-*.txt`) and web-platform-tests has `encoding/`. One listed subtest (`page-encoding`) |
| **`new.target` is the function that is running**, not the one `new` was applied to. `if (new.target === Shape) throw …` in a parent's constructor stops `new Circle()`; inside `F`, `Reflect.construct(F, [], G)` has `new.target === F`; and `Reflect.construct(HTMLElement, [], Derived)` is `Illegal constructor`, so a custom element compiled by Babel cannot be made. Also here: an object that a parent's constructor returns does not become `this` in the child | `src/js/JsEval.cpp` (`invoke_function`, `eval_super_call`, the `%new.target` binding), `src/js/JsOps.cpp` (`js_construct`), `src/js/DomObserverBindings.cpp` (`construct_custom_element`) | ARCHITECTURE.md, Phase 34, section 9; one failing case of `tests/js-conformance/prototypes.dat`; `tests/dom-conformance/open/compiled-custom-elements.js` (4 of 9 lines as jsdom) | `js_construct` already takes the third argument and gives the object its `prototype`; what is missing is the value inside the function and through `super()`. Check with `make test262` before and after (`language/expressions/new.target`, `built-ins/Reflect/construct`) |
| **A built-in does not make the subclass it was called on**: with `class List extends Array {}`, `List.from([1])`, `List.of(1)` and `new List().map(f)` are plain arrays; `P.resolve(1)` for a subclass of `Promise` is a plain promise. There is no `Symbol.species`, and `instanceof` does not ask `Symbol.hasInstance` | `src/js/BuiltinsArray.cpp`, `src/js/Promise.cpp`, `src/js/BuiltinsCollections.cpp`, `src/js/JsOps.cpp` (`js_instance_of`) | ARCHITECTURE.md, Phase 34, section 9; three failing cases of `prototypes.dat` | Needs the row above for `new.target` only where a built-in is called through `Reflect.construct` |
| **What a function is, in small**: nothing tells a built-in that is a constructor from one that is not (`new Math.max()` makes an object; `class X extends f.bind(null)` is accepted; `x instanceof` a function with no `prototype` answers `false` where it should throw, and a bound function is not asked for its target); `Object.getOwnPropertyNames(f)` lacks `length` and `name`, and `f.name = 'x'` takes effect; async and generator functions have `Function.prototype` itself as their prototype, where the language has an object for each kind between the two (three test262 tests, `AsyncFunctionPrototype-is-not-callable.js` and its relatives, passed while `Function.prototype` was not callable and fail since Phase 34); a property of a prototype that cannot be written does not stop an assignment to the object that inherits it | `src/js/JsOps.cpp`, `src/js/JsValue.cpp`, `src/js/BuiltinsObject.cpp` (`bind`), `include/litebird/js/Heap.hpp` (`JsObject::not_constructor` is the bit to set) | ARCHITECTURE.md, Phase 34, section 9; four failing cases of `prototypes.dat` | Six host constructors have no `prototype` (`Response`, `Headers`, `Worker`, `WebSocket`, `IntersectionObserver`, `ResizeObserver`): give them one before `instanceof` throws for a missing one |
| **A script can read and write the engine's own slots**: they are properties whose names begin with `%`, hidden from enumeration and from nothing else. After `el.classList` was read, `el['%classList'] = 5` makes `el.classList` a number; `method['%home']` is the object `super` is looked up from, and assigning to it changes what `super` finds | `src/js/DomBindings.cpp` (`slot`, `set_slot`, `clear_slot`), `src/js/JsEval.cpp` (`%home`, `%fn`, `%new.target`), `src/js/JsValue.cpp` (`is_hidden_key`) | AUDIT.md, finding 41 | Either refuse such a key in `get_property`/`set_property` when it comes from a script, or move the slots off the property table (Phase 34 moved one: `JsObject::not_constructor`). Not beside a row that changes `src/js/Dom*Bindings.cpp` |
| **A page that ends while a worker is starting can end the process**: a read of freed memory on the worker's thread, in a table that process exit has destroyed. `for (var i = 0; i < 4; i++) new Worker('data:text/javascript,postMessage(1)')` as the whole script of a page: 7 of 60 runs of the release build end in `SIGSEGV`, and the sanitizer build reports `heap-use-after-free` | `src/js/Worker.cpp`, `src/main.cpp`, `src/js/DomBindings.cpp` (the table of interfaces) | AUDIT.md, finding 41 | The main thread returns from `main` while worker threads are still building their realms. Run the script 60 times; one run proves nothing |
| **A string built half a surrogate pair at a time costs the square of its length**: `s += '\uD83D'; s += '\uDE00'` 5,000 times takes 0.3 s, 10,000 times 1.3 s, 20,000 times 4.3 s; a pair at a time, 20,000 take 5 ms. One case of `tests/js-conformance/long-strings.dat` does it 60,000 times and takes 6 of the 10 seconds a case is allowed, so it times out on a busy machine | `src/js/JsString.cpp` | AUDIT.md, finding 41; the comment at the JavaScript floor in `.github/workflows/ci.yml` | The floor in CI is one below the score for this case alone: when it is fixed, make the floor the score |
| **A Unicode escape in an identifier is a syntax error**: `var \u0061 = 1`, `o.\u2118 = 3`, `{ \u{6F}: 2 }` all end in `Invalid or unexpected token` (in a string the escapes work). 1,259 of the 4,851 test262 tests that fail under `make test262` fail on this alone | `src/js/JsLexer.cpp` | `tests/js-conformance/README.md`, "Beside the corpus: test262" | Small. Run `make test262` before and after: the count should move by about that much, and nothing that passed should fail |
| **`Promise.all`, `any`, `race` and `allSettled` read an iterable to its end before they look at an element**: with one that never ends they never return, and the process grows until it is killed. The language takes one element, calls `Promise.resolve` on it, and closes the iterator if that throws | `src/js/Promise.cpp` | AUDIT.md, finding 41; test262 `built-ins/Promise/{all,any,race}/invoke-*-error-close.js` | Found by running all of test262 (`run_test262.py --timeout 4 built-ins language annexB`): three of the four tests that end out of memory. The fourth is an array index near 2^32, which is "holes in arrays" |
| **An `Attr` is not a `Node`** (`attr.cloneNode()`, `importNode(attr)`, an `Attr` adopted with its element: 10 subtests); **one element cannot have two attributes with one qualified name** in different namespaces (14); a `NamedNodeMap` and an `HTMLCollection` do not list their names as own properties (8) | `src/js/DomCollectionBindings.cpp`, `include/litebird/dom/AttributeList.hpp`, `include/litebird/dom/Element.hpp` | `tests/wpt/README.md`, the words `attr-is-not-a-node`, `one-name-two-namespaces`, `named-properties` | Attributes are stored under their qualified names; the second needs them stored under namespace and local name. Only a script can make such a pair |
| **Date and time inputs are text inputs**: value sanitization, `valueAsDate`, `valueAsNumber`, `stepUp()`, `min`/`max` for `date`, `time`, `month`, `week`, `datetime-local` | `src/dom/FormState.cpp`, `src/js/DomFormBindings.cpp` | AUDIT.md, finding 31; `tests/dom-conformance/open/dates-and-svg.js` | `number` and `range` are done and show where it goes |
| **`el.style` keeps most shorthands whole** (`style.flex = '1'` leaves `flexGrow` empty), lists a shorthand once in `length`/`item()`, and does not rewrite values the way CSSOM does (`url("a.png")`, `translate(10px, 20px)`) | `src/js/DomStyleBindings.cpp` | ARCHITECTURE.md, Phase 27, section 12; `tests/dom-conformance/open/inline-style.js` | The computed style in the same file already takes ten shorthands apart (`take_apart`): the block of `el.style` should use it |
| **What the DOM corpus cannot ask**: sizes and positions while scripts run (`getBoundingClientRect()` is zero: layout happens once, after them; `getComputedStyle(el).width` is `auto`), style sheet objects (`sheet.cssRules`, `insertRule`), `document.write`, the SVG DOM (`className.baseVal`, `viewBox`, `getBBox()`: `tests/dom-conformance/open/dates-and-svg.js`) | `src/js/Dom*Bindings.cpp`, `src/main.cpp`, `src/shell/` | ARCHITECTURE.md, Phase 27, section 12 | jsdom does none of the first two, so each needs a reference of its own before it is built |
| **A block inside an inline box is not laid out**: `<a href="#">x<p>y</p></a>` shows "x" and no "y" | `src/layout/` | AUDIT.md, finding 28 | The open bug a real page is most likely to meet |
| **Floats**: 10,000 with a size in one box take 46 s, and past the first 1,024 they are placed outside the box | `src/layout/FloatTableLayout.cpp` | AUDIT.md, finding 28 | Same files as the row above |
| **Every box carries its own copy of a 920-byte computed style**: 100,000 times `word <b>bold</b>` takes 1.3 GB | `src/layout/`, `src/css/` | AUDIT.md, findings 12 and 28 | Same files as the layout rows |
| **A regular-expression group repeated over about 12,000 times ends in `RangeError`** | `src/js/BuiltinsRegExp.cpp` | AUDIT.md, finding 26 | A rewrite of the matcher. Shares no file with another row |
| **Memory is reclaimed only when a script calls `gc()`**, and no leak audit has been done. One leak is known: a function bound to an object that holds it (`o.b = f.bind(o)`, 2,000 times: 2,000 cells left after `gc()`), and what a pending timer holds. The collector cannot see into what a native function captured | `src/js/Heap.cpp`, and everything that allocates script objects | Known gaps, 4; ARCHITECTURE.md, Phase 34, section 9; one failing case of `tests/js-conformance/prototypes.dat` | Touches everything that makes script objects, the DOM bindings included: not beside a row that changes `src/js/Dom*Bindings.cpp` |
| **`<select>` content as the standard now has it**: the 11 corpus cases that fail | `src/html/HtmlTreeBuilder.cpp`, `src/html/HtmlTreeBuilderModes.cpp` | AUDIT.md, finding 30 | parse5 has the older rules too, so it cannot be the reference |
| **The corpus has moved to web-platform-tests** (1,973 cases, 1,868 pass): vendor it, read its `#description` sections, add processing instructions | `tools/conformance/`, `tests/html5lib/`, `src/html/`, `include/litebird/dom/` | AUDIT.md, finding 30 | Same files as the row above. The node type is there since Phase 28 (`include/litebird/dom/ProcessingInstruction.hpp`, its interface, `<?target data>` in the markup serializer); what is left is the tokenizer and the tree builder |
| **One element with 50,000 attributes takes 2.9 s in the HTML parser**, and 0.5 s with 25,000: the square | `src/html/HtmlTokenizer.cpp` | AUDIT.md, finding 28 | Small. It was 10.4 s before Phase 28 gave an element's attribute list an index; what is left is the tokenizer's search for a name it has seen (`std::any_of` over the token's attributes). The XML parser does the same check with a set |
| **External scripts and stylesheets are not loaded** (`<script src>`, `<link rel=stylesheet>`) | `src/net/`, `src/js/JsInterpreter.cpp`, `src/main.cpp` | ARCHITECTURE.md, Phase 27, sections 6 and 12 | A page is loaded by `JsInterpreter::load_page()`, which runs each inline script where the parser reports it: a script with `src` is fetched there |
