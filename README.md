# 🦅 Litebird Browser Engine

> A lightweight, modular browser and JavaScript engine built from scratch in **C++20**.
> Designed for learning, experimentation, and building custom web engines without massive legacy dependencies.

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

### 2. HTML5 Parser (`include/litebird/html/`)
- Stateful streaming tokenizer (`HtmlTokenizer`).
- Supports start tags, end tags, attributes (quoted/unquoted, boolean), comments, and DOCTYPEs.
- Automatic handling of void elements (`<img />`, `<br>`, `<input>`, `<hr>`, `<meta>`, `<link>`).
- Raw text mode for `<style>` and `<script>` tags.
- Character entity decoding (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&nbsp;`).

### 3. CSS Engine, Cascade, Animations & Pseudo-Classes (`include/litebird/css/`)
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
- **Specificity Calculation:** Computes `(ID, Class, Tag)` triples for accurate cascade resolution.
- **Built-in User-Agent Stylesheet:** Default styling for headings, paragraphs, lists, and layout blocks.
- **Style Resolver:** Combines User-Agent styles, Author stylesheets, and inline `style="..."` attributes with property inheritance (`color`, `font-size`, `text-align`).

### 4. Layout Engine: Block, Flexbox, Grid, Positioning & IFC (`include/litebird/layout/`)
- Standard Box Model: `content`, `padding`, `border`, `margin`.
- Box types: `BlockNode`, `FlexNode`, `GridNode`, `InlineNode`, `AnonymousBlock`, `TextNode`, `ImageNode`.
- **CSS Box Sizing & Constraints:** Full support for `box-sizing: content-box | border-box`, plus `min-width`, `max-width`, `min-height`, and `max-height`.
- **CSS Positioning & Stacking Contexts:** `position: static | relative | absolute | fixed`, coordinate offsets (`top`, `right`, `bottom`, `left`), and hierarchical `z-index` stacking context sorting.
- **CSS Grid Layout (`display: grid`):** Track dimension resolution (`grid-template-columns: 1fr 2fr 1fr`), `repeat()` counts, `minmax()` track constraints, `auto-fit` auto-repetition, cell auto-placement, item alignment, and grid gap spacing (`row-gap`, `column-gap`, `gap`).
- **CSS Flexbox (`display: flex; flex-wrap: wrap`):** Single-line and multi-line flex container line breaking, cross-axis line alignment, space distribution, and flex item `gap` / `row-gap` / `column-gap` spacing without margin distortion.
- **Inline Formatting Context (IFC 2.1):**
  - Line box construction (`LineBox`) and inline fragment generation (`InlineFragment`).
  - True continuous text flowing across mixed inline elements (`<b>`, `<strong>`, `<i>`, `<em>`, `<a>`, `<code>`, `<span>`).
  - Inline element styling: custom colors, background pills, borders, padding, and text-decoration (`underline`).
  - Word wrapping across element boundaries with exact baseline alignment.
- **Incremental Layout & Invalidation:** Granular `dirty` marking (`mark_dirty()`) and subtree-isolated relayout avoiding redundant full-document recalculations.

### 5. Multi-Backend Rendering, SVG Vector Graphics, Canvas 2D & Image Decoding (`include/litebird/render/`)
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

### 6. Modern JavaScript Engine, Bytecode VM, ES6 Collections & Web APIs (`include/litebird/js/`, `include/litebird/net/`)
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

### 7. Interactive Browser Shell, EventLoop & Network (`include/litebird/shell/`, `include/litebird/net/`)
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

# Run the full 32-suite test suite
make test
```

### Running Examples

```bash
# 1. Run the Phase 9 Super-Showcase (SVG Vector Graphics + Animations + Pseudo-Classes + ES6 Map/Set)
./build/litebird examples/phase9_showcase.html

# 2. Run the Next-Gen Feature Showcase (Canvas 2D + Grid + Storage + ES6)
./build/litebird examples/nextgen_showcase.html

# 3. Launch the Interactive TUI Browser Shell
./build/litebird --shell examples/browser_portal.html

# 4. Export showcase page directly to a 24-bit BMP image
./build/litebird --render-bmp showcase.bmp examples/phase9_showcase.html

# 5. Run the live EventLoop Timer & Interactive Counter application
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

## 🧪 Running Tests

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

All 46 engine test suites passed successfully!
```
