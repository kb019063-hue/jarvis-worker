# 🦅 Litebird Browser Engine

> A lightweight, modular browser and JavaScript engine built from scratch in **C++20**.
> Designed for learning, experimentation, and building custom web engines without massive legacy dependencies.

---

## 📊 Conformance scoreboard

Phase 15 replaced the hand-rolled HTML parser with the WHATWG tree-construction
algorithm and added a way to *measure* the engine instead of eyeballing it.
Phase 17 then spent the scoreboard: it drove the number from 88.0% to 98.8% by
fixing what the corpus actually disagreed with, and audited the harness itself
(see "What the scoreboard measured wrong" below). Phase 18 moved back to
*layout* -- percentage lengths and `overflow` clipping -- which the parser
scoreboard cannot see at all. Phase 26 compared the tree builder with another
parser on pages nobody wrote, which found what the corpus has no case for
(`<br/>` closed the paragraph it stood in) and took the number to 99.4% on
the way (see "Pages nobody wrote"). Phase 27 did for the DOM a script sees
what Phase 23 did for the language: a corpus recorded from another
implementation first, then new bindings written against it (see "The DOM
scoreboard").

| Metric | Value | How it is measured |
| :--- | :--- | :--- |
| **DOM tree-construction conformance** | **1781 / 1792 (99.4%)** | html5lib/WPT derivation suite run by `make conformance` |
| Trees for random pages | 6,000 of 6,000 equal to the ones parse5 builds once nine rules of parse5 are corrected to the standard; 5,951 equal to parse5 as published | `make diff-trees` (needs Node.js) |
| **JavaScript corpus** | **840 / 845 (99.4%)** | `tests/js-conformance`, expected output recorded from Node.js, run by `make js-conformance` |
| **DOM corpus** (what a script sees) | **557 / 557**; 61 before Phase 27 | `tests/dom-conformance`, expected output recorded from jsdom, run by `make dom-conformance` |
| **URL parsing and setters** | **1150 / 1188 (96.8%)**; 2 before Phase 27 | `tests/url`, the web-platform-tests data, run by `make url-conformance` |
| Character/entity table | 2231 named references (complete) | generated from the WHATWG table |
| Engine unit tests | 108 suites, all passing | `make test` |
| Fuzz robustness | 400 parser mutations + 400 layout/render/image runs, 0 findings | `make asan` (ASan + UBSan) |
| Deep and oversized pages | 145 pages that nest or repeat one thing up to 200,000 times + 300 random pages at the parser's nesting limit: 1,292 runs, 0 failed | `make deep` (8 MB stack, 2 GB, 60 s a run) |

```bash
make conformance     # HTML: per-file pass rate + failing-case diffs
make js-conformance  # JavaScript: per-file pass rate
make dom-conformance # the DOM a script sees: per-file pass rate
make url-conformance # URL parsing and setters (web-platform-tests data)
make test            # 108 engine suites
make deep            # pages nested 200,000 deep, oversized bitmaps, very long runs of one thing
make asan            # sanitizer build, tests, the JavaScript, DOM and URL corpora, a fuzz run and the deep pages
make tsan            # the string type's stress tests under ThreadSanitizer, and under ASan with leak checking
make bench           # string-heavy scripts timed against Node; fails if one is wrong or takes over 3 s
make diff-strings    # 50,580 string results compared with Node's; fails if more than the known 95 differ
make diff-trees      # 6,000 random pages and fragments parsed here and by parse5; fails if a tree differs
```

### The JavaScript scoreboard

`tests/js-conformance/` holds 845 small scripts. Each one records what Node.js
prints for it, and a case passes when `litebird --run-js` prints the same and
exits 0. CI re-runs the whole corpus under Node first, so an expectation
cannot be edited to match what Litebird does.

Read the number for what it is:

- **It is not test262.** The corpus was written alongside the engine. It
  covers the language and library features ordinary scripts use (syntax,
  closures, classes, destructuring, generators, promises and async functions,
  strings, arrays, regular expressions, dates, JSON), not every clause of the
  specification. A run of the official suite would score far lower.
- **The five failures are whole features that do not exist**: BigInt (2
  cases), `Proxy`, typed arrays, and holes in arrays.
- **It measures the language, not the DOM.** The bindings a page script calls
  (`document`, elements, events) are a much thinner layer; see "Known gaps" in
  `ARCHITECTURE.md`.

The engine as it was before Phase 23 passes 140 of the 801 cases the corpus
had when it was replaced, and 25 of the 90 cases in the fixed "everyday" probe
(`everyday-es5.dat` and `everyday-es2015.dat`), which now passes 90/90.

Four other checks sit behind the number. A differential run evaluates 67,545
generated expressions in Litebird and in Node and compares the results: 99
(0.15%) differ, mostly the last digit of `Math.atan2`, `cbrt` and `pow`.
`make diff-strings` does the same for strings (36 subjects through every
string method and 51 regular expressions): 95 of 50,580 results differ, all
of them Unicode data the engine does not have (collation, normalization, some
case mappings, part of `\p{L}`).
63 of the corpus cases (`deep-nesting.dat`) hand the engine input nested
200,000 deep, which used to end the process and must now end in an error the
script can catch. And 34 (`long-strings.dat`) loop over, build or take apart
strings of 100,000 characters and more, work that used to grow with the
square of the length: reading a string copied it. The engine before that fix
passes 12 of the 34 at best and times out on 15 (AUDIT.md, finding 25).

Every remaining failure can be inspected with
`python3 tools/conformance/run_tree_construction.py --filter NAMEFRAGMENT --show 5`. The residual
0.6% is **11 cases, all of them about `<select>`**: the standard changed how
the content of a `<select>` is parsed (the 2025 customizable-`<select>` work,
with `<selectedcontent>` cloning) and Litebird has the older rules. They are
catalogued in `ARCHITECTURE.md` rather than papered over.

### The DOM scoreboard

`tests/dom-conformance/` holds 557 cases: a page, a script, and what the
script prints in [jsdom](https://github.com/jsdom/jsdom) 31.0.0, an
independent implementation of the DOM and HTML standards. A case passes when
`litebird --run-script` prints the same and exits 0. CI runs the whole corpus
in jsdom first (`--reference`), so an expectation cannot be edited to match
what Litebird does.

```bash
make dom-conformance
python3 tools/conformance/run_dom_conformance.py --show 20 --filter forms
```

| | Passed |
| :--- | ---: |
| Before Phase 27 (`main` at `6f08a70`) | 61 / 557; the everyday probe 13 / 68 |
| Now | 557 / 557; the everyday probe 68 / 68 |

`everyday.dat` is the fixed probe: 68 snippets adapted from
[You Might Not Need jQuery](https://github.com/HubSpot/YouMightNotNeedjQuery),
which is what plain DOM code on real pages looks like. It was written before
any of the new code and is never edited. The other 19 files go through the
DOM by area: nodes, elements, attributes, `classList`, `dataset`, collections,
selectors, markup (`innerHTML` and friends), text, style, events, forms,
mutation observers, custom elements and shadow trees, the document, the
window, loading, errors, and 16 small programs (a to-do list, a router on the
hash, a form with validation).

The bindings were rewritten for it (`src/js/Dom*Bindings.cpp`, 12,400 lines):
every DOM object has its interface's prototype, so `el instanceof HTMLElement`,
`Object.getPrototypeOf(document.body) === HTMLBodyElement.prototype`,
`class MyEl extends HTMLElement` and patching `Element.prototype` all work;
a node has one object for as long as it lives; collections are live; selectors
are the real grammar; and errors are the `DOMException`s the standards name.
A page's scripts run while it is parsed, so a script sees the page up to
itself. ARCHITECTURE.md, Phase 27, has the design.

Three things about the number:

- **It is 100% of what the corpus asks, not of the DOM.** jsdom does no layout
  and loads no other page, so nothing here checks `getBoundingClientRect()`
  (zero while scripts run), scrolling or navigation. What is known to be
  missing is listed in ARCHITECTURE.md (Known gaps, 9).
- **jsdom is the reference, not the authority.** In five places it differs
  from the standards. The corpus stays away from those and a unit test
  asserts the standard's answer (`tests/dom-conformance/README.md` names
  them).
- **URLs are measured separately**, with the data browsers use:
  `tests/url/` is `urltestdata.json` and `setters_tests.json` from
  web-platform-tests, 1,188 cases, of which 1,150 pass. The 38 others are
  host names outside ASCII, which need Unicode's IDNA tables.

### Pages nobody wrote

The corpus is 1,792 inputs somebody thought of. `make diff-trees`
(`tools/conformance/diff_trees.py`) makes pages nobody did, a few dozen tags
in random order and most of them misnested, and compares the tree Litebird
builds with the one parse5 builds, an independent parser written from the
same standard. A failing corpus case says a bug exists. A random page says
how often a page meets it, and `--reduce` cuts the page down to a few tags
to read the standard against.

The first run of it, in Phase 25, found five bugs in 6,000 pages. Phase 26
fixed those and then widened what the pages contain, and the wider pages
found more than the first ones had:

| What the pages contain | Trees equal before Phase 26 | Now |
| :--- | ---: | ---: |
| Tags, text and comments | 1,489 of 1,500 | 1,500 |
| The same with what else the tree builder reads: `<br/>`, `type=hidden`, CDATA, U+0000, a stray doctype | 1,371 | 1,500 |
| Those parsed as fragments, which is what `innerHTML` does, in 45 kinds of element | 1,371 | 1,500 |
| Those fragments with scripting disabled | 1,361 | 1,500 |

The worst of what they found is not in the corpus at all, because no case
there writes it: **a void element written the XHTML way closed its parent.**
In `<p>a<br/>b</p>` the "b" landed outside the paragraph, and a
`<link .../>` in the head put the rest of the head, and the body, outside
`<html>`. One line was wrong, and 292 of the 408 wrong pages in the table
come right when only that line is changed. One of the example pages in this
repository was parsed wrongly for that reason, and so was any markup read
back from `innerHTML` and assigned again, because Litebird itself writes
`<br />`.

The fragment rows led to the other thing a page would notice. The parser
was right about fragments for a `<tr>` or an `<svg>`, and `innerHTML` never
asked it: a script's `element.innerHTML = ...` was parsed as the content of
a `<body>` whatever the element was. `table.innerHTML =
"<tr><td>1</td><td>2</td></tr>"` left the text "12" in the table and no
row. It is parsed for its element now. The rest is in AUDIT.md, findings 29
and 30.

Two things about the numbers. The comparison is with a copy of parse5 in
which nine rules are corrected, because parse5 8.1 is itself behind the
standard there (a form inside a `<template>`, CDATA in an SVG `<title>` and
seven more; `diff_trees.py` lists them with the correction, a few lines
each). Against parse5 as published 5,951 of the 6,000 trees are equal and
the other 49 differ for those nine reasons. For four of the nine the
web-platform-tests copy of the corpus has cases that say the same, for two
more Blink's source does, and for the last three there is only the
standard's text (AUDIT.md, finding 30). And `<select>` is left out of the
pages: parse5 has the older rules for it, and so does Litebird.

The check needs Node.js and, the first time, the npm registry to fetch
parse5. CI runs it in the conformance job and fails if a tree differs.

### Pages that are hostile by size

A conformance corpus is made of small pages, and so is what a mutation fuzzer
starts from. Neither finds the oldest way to end a browser from a web page:
nest something deeper than the stack is long, or repeat it until work that
grows with the square of its count never finishes. A file of 300,000 nested
`<div>` (3.3 MB, no script) used to end Litebird with a `SIGSEGV`.

`make deep` generates those pages (`tools/fuzz/deep_pages.py`) and runs each
in a child process with an 8 MB stack, 2 GB of memory and 60 seconds:

| | Before Phase 25 | Now |
| :--- | :--- | :--- |
| Runs that fail, of 392 over 145 pages | 264 | 0 |
| 300,000 nested `<div>` | `SIGSEGV` | renders in 4 s |
| A script that nests 200,000 elements with `appendChild`, reads `textContent` and `innerHTML`, and drops the tree | `SIGSEGV` at 20,000 | 0.7 s |
| One paragraph of 100,000 images | 35 s | 1.3 s |
| `<svg width="100000" height="100000">` | abort (`std::bad_alloc`) | blank box |

Three rules do it. The HTML parser attaches nothing deeper than 512 levels,
the limit Blink and WebKit use: an element that would be deeper becomes a
sibling instead, and none is dropped. A tree built by script may be as deep as
the script likes, so nothing that walks the DOM recurses. Layout and painting
still recurse and stop when the stack is nearly used up. What this does not
cover is listed in AUDIT.md, finding 28: among others, 10,000 floats with a
size in one box still take 46 s, and a block inside an inline box is not laid
out.

### What the scoreboard measured wrong

Phase 17 also fixed three harness bugs that were *mis-scoring the engine*, and
they are worth stating because they change what the number means:

1. **Case splitting.** The runner split `.dat` files on blank lines, but a case
   legitimately contains blank lines (`<pre>` data does). Cases whose data
   contained one were truncated and their expected trees came out empty — a
   guaranteed fail. Cases now split where the corpus format says they do: at a
   `#data` line that starts a case.
2. **Lone CR.** Reading the corpus in text mode turned a lone `\r` into `\n`, so
   a case asserting that a `&#x0D;` reference survives could never pass.
3. **Trailing newline.** The expected tree kept a trailing blank line, which made
   comparisons fail for cosmetic reasons.

Fixing these moved the denominator from 1790 to 1792 cases and was worth ~55
cases on its own. The gain is *measurement honesty*, not new engine code.

The corpus is the same tree-construction material WebKit, Blink, Gecko and
Ladybird use as their parser baseline, so the number is comparable across
engines rather than self-referential. It is vendored under
`tests/html5lib/tree-construction/` (BSD-licensed, see the LICENSE there).

The vendored copy is the last one html5lib-tests published: in June 2026 its
tree-construction tests moved into web-platform-tests, and that copy has
grown to 1,973 cases since. Litebird passes 1,868 of them. Of the other 105,
88 need processing instructions (`<?x?>` is a node of its own in the
standard now, and a comment here), 6 need a script to run while the page is
being parsed, and 11 are the `<select>` cases above. That copy is not
vendored yet, so `make conformance` and CI still score the 1,792.

> **Honest scope:** 99.4% on parser conformance means the DOM this engine builds
> mostly matches a real browser's. It does **not** mean it renders real pages:
> floats and tables now lay out (Phase 16), percentages resolve (Phase 18) and
> `overflow` clips (Phase 18), but there is no bidi, no `rowspan`/`colspan`, no
> `border-collapse`, external stylesheets and scripts are not loaded, and the
> JS engine is a tree-walking interpreter without a JIT.
> See "Known gaps" in `ARCHITECTURE.md`.

---

## 🌟 Features & Architecture

Litebird implements the complete modern browser rendering pipeline:

```text
[Raw HTML] ───► HTML Tokenizer ───► HTML Parser ───► DOM Tree
                                                         │
[Raw CSS]  ───► CSS Tokenizer  ───► CSS Parser  ───► Style Rules (@media, @keyframes)
                                                         │
                                                         ▼
                                                 Style Resolver (Cascade, Viewport & Pseudo-Classes)
                                                         │
                                                         ▼
                                                 Layout Box Tree
                                                 (Block / Flexbox / Grid / IFC / Canvas / Image / SVG)
                                                         │
                                                         ▼
                                                 Display List
                                                 (DrawRect, DrawBorder, DrawText, DrawImage)
                                                         │
                                        ┌────────────────┴────────────────┐
                                        ▼                                 ▼
                                 Terminal Renderer                 Bitmap Renderer
                              (24-bit ANSI Truecolor)             (24-bit .BMP Image)

                                                 ▲
                                                 │
[JavaScript] ───► Lexer ───► AST Parser ───► Bytecode Compiler ───► Stack VM
                                                                         │
                                                 ├──► DOM Bindings (getElementById, textContent)
                                                 ├──► HTML5 Canvas 2D (getContext("2d"))
                                                 ├──► SVG Vector Graphics (<svg>, <path>, <circle>)
                                                 ├──► ES6 Collections (Map, Set) & Destructuring
                                                 ├──► Web Storage & Cookies (localStorage, cookies)
                                                 └──► Async / Promises / EventLoop (fetch, setTimeout, rAF)
```

### 1. DOM Core, W3C EventTarget & Web Components (`include/litebird/dom/`)
- Polymorphic node hierarchy (`Node`, `Element`, `Text`, `Document`, `DocumentFragment`).
- Tree navigation (`parent()`, `children()`, `append_child()`, `remove_child()`).
- DOM query methods: `getElementById()`, `getElementsByTagName()`, `querySelector()`, `querySelectorAll()`.
- Interactive tree printer (`dumpTree()`).
- **No recursion over the tree:** the destructor, `textContent`, `innerHTML`, the selector and tag searches and the dumps are loops (`include/litebird/dom/TreeWalk.hpp`), so a tree a script nested 200,000 levels deep can be built, queried and released.
- **W3C 3-Phase DOM Event Dispatch:** Standard capture, target, and bubbling phases (`Event`, `addEventListener(type, listener, useCapture)`, `dispatchEvent()`, `stopPropagation()`, `preventDefault()`).
- **HTML Form Controls:** Native DOM state and change event dispatch for `<input type="text|checkbox">`, `<textarea>`, `<button>`, and `<select>`.
- **MutationObserver API:** Observation of DOM mutations (`childList`, `attributes`, `attributeOldValue`, `subtree`, `attributeFilter`), microtask batching, and `takeRecords()`.
- **Autonomous Custom Elements:** `customElements.define()`, `whenDefined()`, dynamic element upgrade, and lifecycle callbacks (`connectedCallback`, `disconnectedCallback`, `attributeChangedCallback`).
- **Shadow DOM & Slot Content Projection:** `attachShadow({ mode: 'open' | 'closed' })`, `shadowRoot`, scoped CSS encapsulation with `:host` rules, and `<slot>` content projection (`assignedNodes()`, `assignedElements()`).

#### The DOM a script sees (Phase 27, `src/js/Dom*Bindings.cpp`)
- **Interfaces with prototypes:** about 170 (`Node`, `Element`, `HTMLElement` and the HTML element interfaces, `Document`, `Event` and its kinds, `DOMTokenList`, `HTMLCollection`, `NodeList`, `Range`, `MutationObserver`, `FormData`, `URL`, ...). Members live on the prototype and check what they are called on (`Illegal invocation`), constructors can be subclassed, and one node has one object.
- **Nodes and trees:** `append`/`prepend`/`before`/`after`/`replaceWith`/`replaceChildren`, `insertAdjacentHTML/Element/Text`, `cloneNode`, `contains`, `compareDocumentPosition`, `isEqualNode`, `normalize`, `DocumentFragment`, `<template>.content`, `Range`, `TreeWalker`, `NodeIterator`, `DOMParser`, `XMLSerializer`, and the hierarchy checks with the `DOMException` each one names.
- **Selectors** (`src/dom/Selector.cpp`): compound and complex selectors with all four combinators, attribute selectors with every operator and the `i` flag, `:is()`, `:where()`, `:not()`, `:has()`, the `:nth-*` family with `of`, and the state pseudo-classes of forms; `querySelector(All)`, `matches`, `closest`. An invalid selector is a `SyntaxError`.
- **Markup:** `innerHTML`/`outerHTML` written by the standard's serializer (escaped text and attributes, void elements, namespaces) and read by the parser for the element it is assigned to.
- **Attributes and their reflections:** `classList`, `dataset`, `attributes`, `style` as a live declaration block, `id`/`className`/`hidden`/`tabIndex`/`href`/... per element type, `getComputedStyle` for what the cascade knows.
- **Events:** `Event` and 27 subclasses with their init dictionaries, capture/target/bubble with `composedPath()`, `once`/`passive`/`signal`, `on*` properties and attributes, default actions (`click()` on a checkbox, a label, a submit button, a link to a fragment), `AbortController`.
- **Forms** (`src/dom/FormState.cpp`): `value`/`checked`/`selected` with their dirty flags, radio groups, `<select>` and `options`, constraint validation (`validity`, `checkValidity`, `reportValidity`, `setCustomValidity`), `form.elements`, submission and reset with their events, `FormData`, `requestSubmit`.
- **Observers and components:** `MutationObserver` with every option, custom elements (autonomous and customized built-ins, upgrades, all reactions, in the order the standard queues them), shadow trees and slots.
- **The window:** `location` and `history` (`pushState`, `hashchange`, `popstate`), `URL`/`URLSearchParams` on the URL Standard's parser, timers, `localStorage`, cookies, `matchMedia`, `XMLHttpRequest`, `Blob`/`File`/`FileReader`, `window.onerror` and `unhandledrejection`, named access (`window.myId`, `document.forms.search`).
- **Loading:** scripts run as the parser reaches them (`document.currentScript`, `document.readyState`), then `DOMContentLoaded` and `load`.

### 2. HTML5 Parser: spec tree construction (`include/litebird/html/`)
- **Full WHATWG tree-construction algorithm** (`HtmlTreeBuilder`): 23 insertion modes (`initial` through `after after frameset`), including `in table`, `in table body`, `in row`, `in cell`, `in caption`, `in column group`, `in select`, `in template` and `text`.
- **Implied element synthesis:** `<html>`, `<head>`, `<body>`, `<tbody>` and `<colgroup>` are created when the markup omits them; `<p>`, `<li>`, `<dd>`/`<dt>`, `<option>` and heading elements auto-close per the spec's implied-end-tag rules.
- **Active formatting elements + the adoption agency algorithm:** misnested `<b><i></b></i>` repairs produce the same DOM a browser does.
- **Foster parenting:** stray content inside `<table>` is relocated before the table instead of being dropped.
- **Scopes:** default, list-item, button, table and select scoping rules drive every "is this tag in scope?" decision.
- **Foreign content:** SVG and MathML namespaces, tag *and* attribute case adjustment (`<foreignobject>` → `foreignObject`, `viewbox` → `viewBox`), `xlink`/`xml` attribute namespacing, HTML/SVG/MathML integration points, `</p>`/`</br>` breakouts, and `<![CDATA[ ]]>` sections. Scope checks are namespace-aware, so an `<svg td>` never satisfies an HTML table-scope test.
- **`<template>`:** contents live in a separate `DocumentFragment`, exactly as the spec requires.
- **Comments and doctypes are real nodes** (`Comment`, `Document::doctype()`), including quirks/limited-quirks/no-quirks document modes.
- **Spec-complete tokenizer:** all tag, attribute, comment, bogus-comment, doctype and CDATA states, RCDATA/RAWTEXT/plaintext content models, CRLF normalisation, the complete 2231-entry named character reference table, and the full **script-data state machine** including the escaped and double-escaped runs (`<!--<script </script>-->` stays text, as it should).
- **`eof-in-tag`, `eof-before-tag-name` and NUL handling** match the spec: a tag cut off by EOF is discarded, `</` at EOF is text, and a NUL in a comment becomes U+FFFD.
- **Fragment parsing with a context element**, which is what makes `innerHTML` (and `<tbody>` vs `<div>` contexts) behave correctly.
- **A nesting limit of 512** (`HtmlTreeBuilder::kMaxTreeDepth`, the number Blink and WebKit use): an element that would be deeper is attached beside the element it was written in. The stack of open elements is not shortened, so the parser's state is what the standard says at any depth, and every per-token question about that stack is answered from an index.
- Legacy helpers: `HtmlParser::parse()`, `HtmlParser::parse_fragment_with_context()`.

### 3. HTML Parser helpers (`include/litebird/html/`)
- Stateful streaming tokenizer (`HtmlTokenizer`).
- Supports start tags, end tags, attributes (quoted/unquoted, boolean), comments, and DOCTYPEs.
- Automatic handling of void elements (`<img />`, `<br>`, `<input>`, `<hr>`, `<meta>`, `<link>`).
- Raw text mode for `<style>` and `<script>` tags.
- Character entity decoding (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&nbsp;`).

### 4. CSS Engine, Cascade, Animations & Pseudo-Classes (`include/litebird/css/`)
- Selector parser supporting:
  - Universal (`*`)
  - Type / Tag selectors (`div`, `h1`, `p`)
  - Class selectors (`.card`, `.btn`, `.navbar`)
  - ID selectors (`#header`, `#main`)
  - Attribute selectors (`[attr]`, `[attr="value"]`)
  - Descendant combinators (`div .card p`)
  - **Advanced CSS Pseudo-Classes:**
    - Structural: `:first-child`, `:last-child`, `:nth-child(An+B)`, `:nth-of-type(An+B)` (supports `odd`, `even`, and linear index formulas).
    - User Action & State: `:hover`, `:focus`, `:active`, `:disabled`, `:checked`.
    - Negation: `:not(...)` functional selector inversion.
- **CSS Variables & Custom Properties (`var()`):** Lexical custom property cascade (`--custom-name: value`) with inheritance down DOM subtrees and recursive nested fallback evaluation (`var(--color, var(--fallback, #fff))`).
- **CSS Math Expression Engine (`calc()`):** Recursive descent arithmetic evaluator resolving additions, subtractions, multiplications, divisions, percentages, and variable substitutions.
- **CSS 2D Transforms & Affine Matrix Math:** Homogeneous 3x3 transform matrices (`TransformMatrix2D`) implementing `translate()`, `rotate()`, `scale()`, `skew()`, and `transform-origin`, complete with inverse-matrix hit-testing.
- **CSS Transitions & Keyframe Animations (`@keyframes`):**
  - Declarative `@keyframes` rule parsing with percentage offsets (`0%`, `50%`, `100%`) and `from`/`to`.
  - Animation properties: `animation-name`, `animation-duration`, `animation-timing-function`, `animation-delay`, `animation-iteration-count`, `transition-property`, `transition-duration`.
  - Timing function calculation: `linear`, `ease`, `ease-in`, `ease-out`, `ease-in-out`.
  - Continuous property interpolation for `opacity`, `width`, `height`, `color`, and `background-color`.
- **CSS Media Queries (`@media`):** Supports responsive breakpoints (`(max-width: ...)`, `(min-width: ...)`, `(max-height: ...)`, `(min-height: ...)`). Dynamically re-evaluates rule application against active viewport dimensions.
- **Longhand Shorthands (Phase 15):** `margin`, `padding`, `border`, `border-width`, `border-color`, per-side borders, `background`, `font`, `border-radius` and `flex-flow` are expanded into longhands before cascading, so `margin: 10px 20px` and `border: 3px solid #0f0` behave as expected.
- **`!important` and the full cascade order (Phase 15):** author normal → inline normal → author `!important` → inline `!important`, applied in `(importance, specificity, source order)` tiers.
- **Percentage lengths (Phase 18):** `width`, `height`, `min`/`max` dimensions, `margin` and `padding` percentages resolve against the containing block at layout time (`width: 50%` is half the parent, not fifty pixels), and percentage table cells resolve against the table width.
- **Relative and viewport units (Phase 15):** `em`/`ex`/`ch` resolve against the element's own computed font size, `rem` against the root size, and `vw`/`vh`/`vmin`/`vmax` against the viewport; `pt`, `pc`, `in`, `cm`, `mm` and `q` are converted to CSS pixels.
- **Specificity Calculation:** Computes `(ID, Class, Tag)` triples for accurate cascade resolution.
- **Built-in User-Agent Stylesheet:** Default styling for headings, paragraphs, lists, and layout blocks.
- **Style Resolver:** Combines User-Agent styles, Author stylesheets, and inline `style="..."` attributes with property inheritance (`color`, `font-size`, `text-align`).

### 5. Layout Engine: Block, Flexbox, Grid, Positioning & IFC (`include/litebird/layout/`)
- Standard Box Model: `content`, `padding`, `border`, `margin`.
- Box types: `BlockNode`, `FlexNode`, `GridNode`, `InlineNode`, `AnonymousBlock`, `TextNode`, `ImageNode`, `TableNode`, `TableRowNode`, `TableCellNode`.
- **CSS Box Sizing & Constraints:** Full support for `box-sizing: content-box | border-box`, plus `min-width`, `max-width`, `min-height`, and `max-height`.
- **CSS Positioning & Stacking Contexts:** `position: static | relative | absolute | fixed`, coordinate offsets (`top`, `right`, `bottom`, `left`), and hierarchical `z-index` stacking context sorting.
- **CSS Grid Layout (`display: grid`):** Track dimension resolution (`grid-template-columns: 1fr 2fr 1fr`), `repeat()` counts, `minmax()` track constraints, `auto-fit` auto-repetition, cell auto-placement, item alignment, and grid gap spacing (`row-gap`, `column-gap`, `gap`).
- **CSS Flexbox (`display: flex; flex-wrap: wrap`):** Single-line and multi-line flex container line breaking, cross-axis line alignment, space distribution, and flex item `gap` / `row-gap` / `column-gap` spacing without margin distortion.
- **Inline Formatting Context (IFC 2.1):**
  - Line box construction (`LineBox`) and inline fragment generation (`InlineFragment`).
  - True continuous text flowing across mixed inline elements (`<b>`, `<strong>`, `<i>`, `<em>`, `<a>`, `<code>`, `<span>`).
  - Inline element styling: custom colors, background pills, borders, padding, and text-decoration (`underline`).
  - Word wrapping across element boundaries with exact baseline alignment.
- **Floats & Block Formatting Contexts (CSS 2.1 §9.5):**
  - `float: left | right | none` with shrink-to-fit widths, floating out of the normal flow, and floats stacking side by side until a band is full.
  - `clear: left | right | both` pushes a box below the relevant float bottoms.
  - Line boxes shrink around floats per line (`LineBox::left_intrusion` / `right_intrusion`), so text wraps beside a float and reclaims the full width once it has passed.
  - A block formatting context (`overflow != visible`, floats, flex/grid containers, table cells, inline-blocks, the root) grows to contain its floats instead of collapsing.
  - `overflow: hidden | auto | scroll` and `contain: paint` clip painting: the display list carries `PushClipCommand`/`PopClipCommand` and both renderers honour the clip stack.
- **CSS Table Layout (`display: table | table-row | table-cell | table-row-group`):**
  - Anonymous table structure: rows and row groups are flattened into a row grid, and stray cells are collected into rows.
  - Automatic column sizing: each column is at least as wide as its widest cell's min-content width or explicit width; an explicit table width distributes the surplus across the columns.
  - Row heights are the maximum of the row's cell heights; cells carry their own padding, borders and backgrounds.
  - Percentage cell widths resolve against the table width (a `width: 25%` cell in a 300px table is a 75px column).
  - UA stylesheet provides the default `table`/`tr`/`td` display values plus `<th>` centering and bold weight.
- **Incremental Layout & Invalidation:** Granular `dirty` marking (`mark_dirty()`) and subtree-isolated relayout avoiding redundant full-document recalculations.

### 6. Multi-Backend Rendering, SVG Vector Graphics, Canvas 2D & Image Decoding (`include/litebird/render/`)
- **Native SVG Vector Graphics Engine (`SvgRenderer`):**
  - Direct rasterization of `<svg>` DOM trees into `ImageNode` layout boxes.
  - Coordinate normalization via `viewBox` attribute scaling factors.
  - Shape rasterization: `<rect>`, `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>`, and `<path>` (commands `M`/`m`, `L`/`l`, `H`/`h`, `V`/`v`, `Z`/`z`).
  - Native fill colors, stroke outlines, and stroke widths.
- **HTML5 `<canvas>` 2D Context API (`CanvasRenderingContext2D`):**
  - Vector path building: `beginPath()`, `moveTo()`, `lineTo()`, `closePath()`, `stroke()`, `fill()`.
  - Geometric primitives: `fillRect()`, `clearRect()`, `strokeRect()`, `arc()`.
  - **Gradients & Smooth Curves:** Linear and radial gradients (`createLinearGradient`, `createRadialGradient`, `addColorStop`), quadratic Bézier curves (`quadraticCurveTo`), and cubic Bézier curves (`bezierCurveTo`).
  - Integer Bresenham line drawing and multi-node scanline polygon rasterization.
  - Alpha blending and image blitting via `drawImage()`.
  - Direct integration into the Layout Tree as an `ImageNode`.
- **Native Image Decoders (`ImageDecoder`):** Decodes PNG (DEFLATE / uncompressed), JPEG baseline, Netpbm PPM, and BMP formats directly into 32-bit ARGB pixel buffers with bilinear filtering.
- **Terminal Renderer:** Renders web pages and images directly inside your terminal using 24-bit ANSI Truecolor half-block characters and ASCII borders.
- **Bitmap Renderer:** Exports 24-bit uncompressed `.bmp` images using an embedded font with anti-aliasing. Zero external GUI or graphics libraries required.

### 7. JavaScript Engine & Web APIs (`include/litebird/js/`, `include/litebird/net/`)

The engine was rebuilt in Phase 23. It is a tree-walking interpreter: a lexer
and a recursive-descent parser produce an AST, and the evaluator runs it
directly. A script with a syntax error does not run at all, and no script runs
twice.

- **Language:** `var`/`let`/`const` with hoisting and the temporal dead zone,
  strict mode, closures, all operators including `?.`, `??` and the logical
  assignments, destructuring, spread and rest, default parameters, template
  literals and tagged templates, labelled statements, `switch`, `try`/`catch`/
  `finally`, getters and setters, computed keys, symbols, iterators,
  `for`...`of` and `for`...`in`, generators, classes (fields, private names,
  statics, accessors, `super`, subclassing built-ins), regular-expression
  literals.
- **Promises and async functions:** promises follow the specification's
  ordering (reactions are FIFO microtasks, thenables are adopted), with `all`,
  `allSettled`, `any`, `race` and `withResolvers`. `await` suspends the
  function and returns to its caller; `for await` and async generators work.
  A rejection nothing handles is reported as `Uncaught (in promise)`.
- **Standard library:** `Object`, `Function`, `Array`, `String`, `Number`,
  `Boolean`, `Symbol`, `Math`, `JSON`, `Date`, `RegExp` (a backtracking matcher
  with lookaround, named groups and the `g`, `i`, `m`, `s`, `u`, `y` flags),
  `Map`, `Set`, `WeakMap`, `WeakSet`, `Reflect`, the `Error` family,
  `structuredClone`, `globalThis`, `eval` and `Function`. Methods live on
  shared prototypes; each interpreter has its own set (a realm).
- **Event loop:** tasks, microtasks and timers, with a microtask checkpoint
  whenever the script stack empties. `setTimeout`/`setInterval` (with extra
  arguments), `queueMicrotask`, `requestAnimationFrame`, `performance.now()`.
  The loop's clock can skip ahead, so `--run-js` and one-shot page rendering
  never sleep waiting for a timer.
- **Strings:** a string value is a 24-byte handle on a shared block of UTF-8
  (`JsString`). Passing, reading and indexing a string copy nothing, `s += x`
  appends in place, and a large slice is a range of its parent, so a loop over
  a long string or a string built piece by piece costs what it reads
  (`ARCHITECTURE.md`, Phase 24).
- **Garbage collector:** mark and sweep, including cycles through closures.
  Every value holds a counted handle to its object, and the collector treats
  an object with more handles than it can find inside the heap as held by
  native code. Nothing has to be registered as a root. `gc()` runs a
  collection and returns its statistics.
- **Page lifecycle:** `document.readyState`, `DOMContentLoaded` and `load`
  fire once the document's scripts have run.
- **Web APIs:** `fetch`, `Response`, `Headers`, `URL`, `URLSearchParams`,
  `localStorage`/`sessionStorage`, `document.cookie`, `AbortController`,
  `WebSocket` (RFC 6455), `Worker` (each worker is a thread with its own
  interpreter and event loop; messages are copied as JSON), and DOM bindings:
  `document.getElementById`/`querySelector(All)`/`createElement`, element
  properties and methods, `addEventListener`/`removeEventListener` with capture
  and bubbling, `canvas.getContext("2d")`.
- **What is not there:** BigInt, `Proxy`, typed arrays and `ArrayBuffer`,
  modules (`import`/`export`), `with`, holes in arrays, `Intl`. See "Known
  gaps" in `ARCHITECTURE.md` for the limits of what is there.
- **Bytecode VM:** an earlier experiment, kept for `--dump-bytecode`. It
  covers a small subset of the language and scripts do not run on it.

### 8. Modern Browser Capabilities & Advanced Engine Platform (Phase 14) (`include/litebird/dom/`, `include/litebird/render/`, `include/litebird/net/`, `include/litebird/js/`)
- **IntersectionObserver API:** Viewport and root-relative intersection detection, threshold crossing calculations (`thresholds`, `rootMargin`), ratio computation, entry records, and EventLoop microtask delivery.
- **ResizeObserver API:** Element content and border box size change observation (`contentRect`, `borderBoxSize`), target tracking, and reactive microtask dispatch.
- **History API & Client-Side SPA Routing:** Session history stack management (`pushState`, `replaceState`, `back`, `forward`, `go`), navigation listeners, and standard `popstate` window event dispatch.
- **CSS Visual Filters & Backdrop Filters:** Full parsing and software rasterization for CSS `filter` and `backdrop-filter` operations:
  - Separable Gaussian-approximation box blur (`blur()`).
  - Color channel adjustments: `brightness()`, `contrast()`, `grayscale()`, `sepia()`, `invert()`, and `opacity()`.
  - Multi-parameter drop shadows: `drop-shadow(dx dy blur color)`.
- **Canvas 2D Blend Modes (`globalCompositeOperation`):** Standard Porter-Duff and color blending algorithms implemented in software: `source-over`, `copy`, `multiply`, `screen`, `lighter`, `destination-over`.
- **JavaScript Generators & Iteration Protocol:**
  - Generator functions: `function*`, cooperative suspension/resumption via `yield` expressions, and delegated iteration (`yield*`).
  - Generator objects with standard `.next()`, `.return()`, and `.throw()` methods.
  - Well-known Symbol `Symbol.iterator` support on arrays, generators, and custom iterables.
  - `for...of` iteration statement over arrays, strings, and generator sequences.
- **AbortController & Network Cancellation:**
  - Standard `AbortController` and `AbortSignal` APIs (`aborted`, `reason`, `abort()`, `throwIfAborted()`, `AbortSignal.timeout(ms)`).
  - Integration with `fetch(url, { signal })` to cleanly abort inflight network requests and immediately reject pending promises with `AbortError`.
- **CSS Grid Named Areas & Layout Containment:**
  - `grid-template-areas`: Multi-row and multi-column visual ASCII grid template definitions.
  - `grid-area`: Item placement into named template areas or explicit grid line coordinates (`row-start / col-start / row-end / col-end`).
  - CSS Containment (`contain: none | strict | content | layout | paint | size`): Layout isolation stopping dirty bit propagation at containment boundaries and size containment.

### 9. Interactive Browser Shell, EventLoop & Network (`include/litebird/shell/`, `include/litebird/net/`)
- **Interactive TUI Browser Shell (`BrowserShell`):**
  - Terminal User Interface with keyboard-driven browsing: `[Tab]` / `[Shift+Tab]` cycles focus, `[Enter]` activates links and buttons, `[↑/↓/PgUp/PgDn]` scrolls viewport.
  - Navigation history (`back()`, `forward()`, `reload()`, `navigate_to()`).
  - Interactive URL bar (`[o]` key) supporting local files and remote HTTP URLs.
  - Integrated DevTools inspector pane (`[d]` key) with live DOM and Heap memory metrics.
- **Asynchronous EventLoop & Timers (`EventLoop`):**
  - High-resolution timer queue for `setTimeout()` and `setInterval()`.
  - Timer-driven DOM mutations trigger reactive relayout and repaints.
- **Resource Loader & Native HTTP Client (`ResourceLoader`):**
  - Native POSIX socket HTTP/1.1 client supporting GET requests and headers without external dependencies.
  - Rejects unsafe HTTP methods, URL request targets, and caller header names/values; caps request/response headers at 64 KiB and total HTTP responses at 16 MiB.
  - Full relative URL path resolution.

---

## 🚀 Getting Started

### Prerequisites

- `g++` with C++20 support (GCC 11+)
- `make`

### Building

```bash
# Build the main litebird CLI
make

# Run the 108 engine test suites
make test
```

### Running Examples

```bash
# 1. Run the Phase 14 Super-Showcase (Observers + Visual Filters & Blending + Generators + Grid Areas)
./build/litebird examples/phase14_showcase.html

# 2. Run the Phase 9 Super-Showcase (SVG Vector Graphics + Animations + Pseudo-Classes + ES6 Map/Set)
./build/litebird examples/phase9_showcase.html

# 3. Run the Next-Gen Feature Showcase (Canvas 2D + Grid + Storage + ES6)
./build/litebird examples/nextgen_showcase.html

# 4. Launch the Interactive TUI Browser Shell
./build/litebird --shell examples/browser_portal.html

# 5. Export Phase 14 showcase directly to a 24-bit BMP image
./build/litebird --render-bmp phase14_showcase.bmp examples/phase14_showcase.html

# 6. Run the live EventLoop Timer & Interactive Counter application
./build/litebird examples/timer_counter.html

# 6. Run the DOM EventTarget and Mark-and-Sweep GC showcase
./build/litebird examples/events_and_gc.html

# 7. Render an Inline Formatting Context (IFC) page with mixed inline badges
./build/litebird examples/ifc.html

# 8. Render a Flexbox layout with responsive cards and navbar
./build/litebird examples/flexbox.html
```

---

## 🛠️ CLI Options

| Flag | Description |
| :--- | :--- |
| `--shell [file/url]` | Launch interactive TUI Browser Shell |
| `--dump-dom` | Print the hierarchical DOM tree |
| `--dump-css` | Print parsed CSS rules and specificity calculations |
| `--dump-layout` | Print the computed Layout Box tree with geometry |
| `--dump-paint` | Print atomic display list commands |
| `--dump-bytecode` | Disassemble JavaScript code into VM bytecode |
| `--render-terminal` | Render page to ANSI Truecolor terminal (default) |
| `--render-bmp <file>` | Render page to an uncompressed 24-bit BMP image |
| `--run-js "<code>"` | Run a script, then its timers and promise callbacks. Exits 1 if anything was left uncaught |
| `--run-script <file>` | Load the page, run the file as one more script, then the page's load events and timers. Nothing is drawn. Exits 1 if anything was left uncaught |
| `--url=<URL>` | The address the page is said to come from (`location.href`); by default the `file:` URL of the file |
| `--virtual-time-budget=<ms>` | How much timer time a page gets to settle before it is rendered (default 16, one frame) |
| `--repl` | Launch the interactive JavaScript & DOM REPL |

---

## 🧪 Running Tests & Conformance

```bash
make test
```

Output (108 suites; the first and last lines shown):
```text
=== Running Litebird Browser Engine Test Suite ===

Running test: color_parsing... PASSED
Running test: html_parser... PASSED
...
All 108 engine test suites passed successfully!
```

```bash
make js-conformance                                                     # the JavaScript scoreboard
python3 tools/conformance/run_js_conformance.py --show 10 --filter async   # failing cases, with diffs
```

```bash
make dom-conformance                                                    # the DOM scoreboard
python3 tools/conformance/run_dom_conformance.py --show 10 --filter forms  # failing cases, with diffs
python3 tools/conformance/run_dom_conformance.py --reference            # the corpus itself, against jsdom (needs Node.js and npm)
make url-conformance                                                    # URL parsing and setters
```
