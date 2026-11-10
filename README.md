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
scoreboard cannot see at all.

| Metric | Value | How it is measured |
| :--- | :--- | :--- |
| **DOM tree-construction conformance** | **1770 / 1792 (98.8%)** | html5lib/WPT derivation suite run by `make conformance` |
| Character/entity table | 2231 named references (complete) | generated from the WHATWG table |
| Engine unit tests | 82 suites, all passing | `make test` |
| Fuzz robustness | 400 parser mutations + 400 layout/render/image runs, 0 findings | `make asan` (ASan + UBSan) |

```bash
make conformance     # per-file pass rate + failing-case diffs
make test            # 82 engine suites
make asan            # sanitizer build, tests, and a fuzz run
```

Every remaining failure can be inspected with
`python3 tools/conformance/run_tree_construction.py --filter NAMEFRAGMENT --show 5`. The residual
1.2% is **22 cases**: the `selectedcontent` cloning behaviour of the 2025
customizable-`<select>` work (6 cases), plus a handful of adoption-agency,
`innerHTML` and character-reference edge cases. They are catalogued in
`ARCHITECTURE.md` rather than papered over.

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

> **Honest scope:** 98.8% on parser conformance means the DOM this engine builds
> mostly matches a real browser's. It does **not** mean it renders real pages:
> floats and tables now lay out (Phase 16), percentages resolve (Phase 18) and
> `overflow` clips (Phase 18), but there is no bidi, no `rowspan`/`colspan`, no
> `border-collapse`, and the JS engine is a bytecode interpreter without a JIT.
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
- **W3C 3-Phase DOM Event Dispatch:** Standard capture, target, and bubbling phases (`Event`, `addEventListener(type, listener, useCapture)`, `dispatchEvent()`, `stopPropagation()`, `preventDefault()`).
- **HTML Form Controls:** Native DOM state and change event dispatch for `<input type="text|checkbox">`, `<textarea>`, `<button>`, and `<select>`.
- **MutationObserver API:** Observation of DOM mutations (`childList`, `attributes`, `attributeOldValue`, `subtree`, `attributeFilter`), microtask batching, and `takeRecords()`.
- **Autonomous Custom Elements:** `customElements.define()`, `whenDefined()`, dynamic element upgrade, and lifecycle callbacks (`connectedCallback`, `disconnectedCallback`, `attributeChangedCallback`).
- **Shadow DOM & Slot Content Projection:** `attachShadow({ mode: 'open' | 'closed' })`, `shadowRoot`, scoped CSS encapsulation with `:host` rules, and `<slot>` content projection (`assignedNodes()`, `assignedElements()`).

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

### 7. Modern JavaScript Engine, Bytecode VM, ES6 Collections & Web APIs (`include/litebird/js/`, `include/litebird/net/`)
- **ES6 Classes & Prototype Inheritance:** Full syntactic support for `class`, `extends`, `super(...)`, `super.method()`, static methods, instance methods, and runtime `instanceof` prototype chain traversal.
- **Async Functions & Await Syntax:** First-class `async function`, async arrow functions, and `await` operator, coordinated with the engine microtask EventLoop.
- **Modern Fetch API & Network Stack:** Standard `fetch(url, options)`, `Response` object (`status`, `statusText`, `ok`, `headers`, `json()`, `text()`), and case-insensitive `Headers` object.
- **URL & URLSearchParams Standards:** Complete WHATWG URL decomposition (`href`, `origin`, `protocol`, `host`, `hostname`, `port`, `pathname`, `search`, `hash`) and `URLSearchParams` query manipulation with live bidirectional synchronization.
- **ES6 Collections & Syntactic Extensions:**
  - **`Map` & `Set` Collections:** Standard ES6 collections with `.set()`, `.get()`, `.add()`, `.has()`, `.delete()`, `.clear()`, and dynamic `.size` property getters.
  - **Destructuring Declarations:** Array destructuring (`const [a, b] = [1, 2]`) and object destructuring (`const { name, role } = user`).
  - **Default Function Parameters:** Default argument evaluation for function declarations and arrow functions (`(x = 1, y = 2) => x + y`).
  - **Arrow Functions:** `(x, y) => x + y` and block bodies `(val) => { ... }` with lexical scope binding.
  - **Template Literals:** Multi-part interpolation `` `Value: ${val}, count: ${count + 1}` `` with expression evaluation.
  - **Prototype Chain & Built-ins:** `Object.create()`, `prototype` inheritance, and `Array.prototype` methods (`map`, `filter`, `reduce`, `push`, `pop`, `join`).
- **Asynchronous Promises & JSON:** ECMAScript Promises (`new Promise()`, `.then()`, `.catch()`, `Promise.resolve()`, `Promise.reject()`), microtask execution queue, and `JSON.parse()` / `JSON.stringify()`.
- **Web Storage & Cookie Jar:**
  - `localStorage`: Key-value storage persisted across navigation.
  - `sessionStorage`: In-memory session key-value storage.
  - `document.cookie`: RFC-compliant cookie jar supporting `key=value`, `Path`, `Max-Age`, and `Expires`.
- **Web Workers & Concurrent Execution:** Multi-threaded JavaScript workers (`new Worker(url)`), thread-isolated bytecode execution, deep-cloned JSON IPC message queues, `postMessage()`, `onmessage`, and thread lifecycle termination (`terminate()`).
- **RFC 6455 WebSocket Client:** Standard full-duplex WebSocket networking (`new WebSocket(url)`), cryptographic handshake with zero-dependency SHA-1/Base64, frame masking/unmasking, background network polling thread, and `onopen`, `onmessage`, `send()`, and `close()`.
- **Instruction Set & Bytecode Virtual Machine:** Compact 8-bit opcodes, constant pooling, local slot indexing, jump patching, and stack frame call dispatch.
- **Mark-and-Sweep Garbage Collector (`Heap`):**
  - Tracing cycle collector detecting and reclaiming isolated circular reference graphs.
  - Memory diagnostics via `gc()` (`total_cells`, `alive_cells`, `collected_cells`, `bytes_allocated`).
- **DOM & Web API Bindings:**
  - `window`, `document`, `console.log/warn/error`, `Math.*`
  - `fetch(url)` integrating native socket HTTP requests with JavaScript Promises.
  - `canvas.getContext("2d")` returning live 2D drawing contexts.

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

# Run the full 58-suite test suite
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
| `--run-js "<code>"` | Evaluate a JavaScript snippet directly |
| `--repl` | Launch the interactive JavaScript & DOM REPL |

---

## 🧪 Running Tests & Conformance

```bash
make test
```

Output:
```text
=== Running Litebird Browser Engine Test Suite ===

Running test: color_parsing... PASSED
Running test: html_parser... PASSED
Running test: css_parser_and_specificity... PASSED
Running test: style_resolver_and_cascade... PASSED
Running test: layout_box_tree... PASSED
Running test: javascript_engine... PASSED
Running test: bytecode_virtual_machine... PASSED
Running test: flexbox_layout... PASSED
Running test: text_wrapping... PASSED
Running test: anti_aliased_rasterizer_and_shadows... PASSED
Running test: inline_formatting_context... PASSED
Running test: garbage_collector_and_cycles... PASSED
Running test: dom_event_target_and_bubbling... PASSED
Running test: event_loop_and_timers... PASSED
Running test: resource_loader_and_url_resolution... PASSED
Running test: browser_shell_navigation_and_hit_testing... PASSED
Running test: css_positioning_and_stacking_contexts... PASSED
Running test: box_sizing_and_constraints... PASSED
Running test: javascript_prototype_chain_and_arrays... PASSED
Running test: incremental_layout_and_invalidation... PASSED
Running test: js_async_promises_and_json... PASSED
Running test: css_flex_wrapping_and_grid_layout... PASSED
Running test: dom_3phase_events_and_form_controls... PASSED
Running test: image_decoding_and_rendering... PASSED
Running test: js_template_literals_and_arrow_functions... PASSED
Running test: web_storage_and_cookies... PASSED
Running test: html5_canvas_2d_context... PASSED
Running test: css_media_queries... PASSED
Running test: css_pseudo_classes... PASSED
Running test: svg_vector_rendering... PASSED
Running test: es6_map_set_and_destructuring... PASSED
Running test: css_variables_and_cascade... PASSED
Running test: css_calc_expressions... PASSED
Running test: fetch_api_and_headers... PASSED
Running test: url_and_url_search_params... PASSED
Running test: web_workers_and_concurrent_execution... PASSED
Running test: css_flexbox_and_grid_modern_refinements... PASSED
Running test: websocket_rfc6455_client_and_handshake... PASSED
Running test: mutation_observer_api... PASSED
Running test: custom_elements_api... PASSED
Running test: shadow_dom_and_slot_projection... PASSED
Running test: intersection_and_resize_observers... PASSED
Running test: history_api_and_client_routing... PASSED
Running test: css_filters_and_canvas_blending... PASSED
Running test: js_generators_iterators_and_for_of... PASSED
Running test: abort_controller_and_fetch_cancellation... PASSED
Running test: css_grid_template_areas_and_containment... PASSED

All 52 engine test suites passed successfully!
```
