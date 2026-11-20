# 🦅 Litebird Browser Engine — Comprehensive Architecture & Contributor Guide

> **Notice for Future AI Agents & Human Developers:**
> This document is designed as a self-contained reference and onboarding manual. It details the internal design, subsystem contracts, memory lifecycle, data flows, and expansion playbooks across the entire codebase. When implementing new features or debugging, consult this guide first.
>
> **The phase write-ups have moved** to one file each in `docs/phases/`, and are
> indexed at the bottom of this document. A citation of the form
> "ARCHITECTURE.md, Phase NN" means the file that index points to.

---


## 📑 Table of Contents

1. [1. High-Level Philosophy & Constraints](#1-high-level-philosophy--constraints)
2. [2. Repository Layout & Module Structure](#2-repository-layout--module-structure)
3. [3. End-to-End Pipeline & Data Flow](#3-end-to-end-pipeline--data-flow)
4. [4. Subsystem Deep Dives](#4-subsystem-deep-dives)
   - [4.1 DOM Core & Event Dispatch](#41-dom-core--event-dispatch)
   - [4.2 HTML5 Tokenizer & Parser](#42-html5-tokenizer--parser)
   - [4.3 CSS Parser, Cascade & Media Queries](#43-css-parser-cascade--media-queries)
   - [4.4 Layout Box Tree & Formatting Contexts](#44-layout-box-tree--formatting-contexts)
   - [4.5 Display List & Multi-Backend Rendering](#45-display-list--multi-backend-rendering)
   - [4.6 JavaScript Engine, Bytecode VM & GC](#46-javascript-engine-bytecode-vm--gc)
   - [4.7 Web Storage & Cookie Engine](#47-web-storage--cookie-engine)
   - [4.8 Canvas 2D Rasterizer](#48-canvas-2d-rasterizer)
   - [4.9 Network, EventLoop & TUI Shell](#49-network-eventloop--tui-shell)
   - [4.10 SVG Vector Graphics Engine (`include/litebird/render/SvgRenderer.hpp`)](#410-svg-vector-graphics-engine-includelitebirdrendersvgrendererhpp)
   - [4.11 CSS Transitions & Keyframe Animations (`include/litebird/css/StyleSheet.hpp`)](#411-css-transitions--keyframe-animations-includelitebirdcssstylesheethpp)
   - [4.12 Advanced CSS Pseudo-Classes & Selectors (`include/litebird/css/StyleResolver.hpp`)](#412-advanced-css-pseudo-classes--selectors-includelitebirdcssstyleresolverhpp)
   - [4.13 ES6 Collections & Language Expansion (`include/litebird/js/`)](#413-es6-collections--language-expansion-includelitebirdjs)
5. [5. Critical Memory & ABI Safety Rules](#5-critical-memory--abi-safety-rules)
6. [6. Extension Playbooks (How to Add Features)](#6-extension-playbooks-how-to-add-features)
   - [Adding a New DOM API / Method](#adding-a-new-dom-api--method)
   - [Adding a New CSS Property](#adding-a-new-css-property)
   - [Adding a New JavaScript Bytecode Opcode](#adding-a-new-javascript-bytecode-opcode)
   - [Adding a Test Suite Entry](#adding-a-test-suite-entry)
7. [7. Current Phase Status & Roadmap](#7-current-phase-status--roadmap)
8. [Known gaps (the honest list)](#known-gaps-the-honest-list)
9. [Strict-mode writes that a property refuses (the old `strict-write` rows)](#strict-mode-writes-that-a-property-refuses-the-old-strict-write-rows)
10. [Phase write-ups](#phase-write-ups)

---

## 1. High-Level Philosophy & Constraints

- **Language Standard:** C++20 (`-std=c++20 -O2 -Wall -Wextra`).
- **Zero Third-Party Dependencies:** Zero dependencies outside the C++ standard library and standard POSIX sockets (`<sys/socket.h>`, `<unistd.h>`). No external graphical libraries (no Skia, Cairo, WebKit, V8, or Qt).
- **Dual Rendering Modes:**
  1. Terminal ANSI 24-bit Truecolor renderer for interactive TUI browsing and CLI inspection.
  2. Software Bitmap (.bmp) rasterizer generating standalone 24-bit uncompressed image files using an embedded bitmap font.
- **Modularity:** Each engine phase (DOM, HTML, CSS, Layout, Paint, JS, Net, Shell) is decoupled and independently testable via unit tests.

---

## 2. Repository Layout & Module Structure

```text
/home/user/jarvis-worker/
├── include/litebird/
│   ├── dom/               # Node, Element, Document, EventTarget, Form controls
│   ├── html/              # HtmlTokenizer, HtmlParser
│   ├── css/               # CssParser, StyleSheet, StyleResolver, Media Queries
│   ├── layout/            # LayoutBox, LayoutTree, InlineFormattingContext
│   ├── render/            # PaintCommand, DisplayList, TerminalRenderer, BitmapRenderer,
│   │                      # ImageDecoder, Canvas2D
│   ├── js/                # JsValue, JsLexer, JsParser, AST, Bytecode, VM,
│   │                      # Heap (GC), Promise, Json, Storage, EventLoop
│   ├── net/               # ResourceLoader (POSIX HTTP/1.1 client, URL resolution)
│   └── shell/             # BrowserShell (TUI keyboard navigation & DevTools)
├── src/                   # C++ implementation files matching include/
├── tests/
│   └── test_suite.cpp     # Comprehensive monolithic unit & integration test runner
├── examples/              # Visual showcase HTML pages
├── Makefile               # Build scripts for CLI (litebird) and test suite
├── README.md              # Public project documentation
└── ARCHITECTURE.md        # This guide
```

---

## 3. End-to-End Pipeline & Data Flow

```text
[Raw HTML String]
       │
       ▼
HtmlTokenizer ───► HtmlParser ───► Document DOM Tree
                                           │
[Raw CSS / Inline <style>]                  │
       │                                   │
       ▼                                   │
CssParser ───► StyleSheet Rules ───────────┤
       │                                   │
       ▼                                   ▼
StyleResolver (Cascade + Viewport) ──► ComputedStyle per Element
                                           │
                                           ▼
                                   LayoutTree::build()
                                           │
                                           ▼
                                   LayoutBox Root Box
                               (Block / Flex / Grid / IFC / Canvas)
                                           │
                                           ▼
                               LayoutBox::layout(viewport_w, viewport_h)
                               (Calculates content.x, y, width, height)
                                           │
                                           ▼
                                  DisplayList::build()
                                           │
                                           ▼
                        Atomic Paint Commands (DrawRect, DrawText, etc.)
                                           │
                          ┌────────────────┴────────────────┐
                          ▼                                 ▼
                  TerminalRenderer                    BitmapRenderer
               (ANSI 24-bit Truecolor)               (24-bit BMP image)
```

---

## 4. Subsystem Deep Dives

### 4.1 DOM Core & Event Dispatch
- **Base Hierarchy:** `Node` is the abstract base class with `NodeType` (Document, Element, Text, Comment).
- **Element:** Subclasses `Node`, inherits from `EventTarget`. Holds tag name, attribute dictionary (`m_attributes`), class list, and computed inline/resolved styles.
- **EventTarget:** Manages `addEventListener(type, listener, useCapture)` and `removeEventListener`.
- **W3C 3-Phase Event Pipeline:**
  1. **Capture Phase:** Traverses down from `Document` to the target's immediate parent invoking listeners registered with `useCapture = true`.
  2. **Target Phase:** Invokes all listeners on the target node.
  3. **Bubble Phase:** Traverses up from the target's immediate parent back to `Document` (unless `event.stopPropagation()` is invoked or `event.bubbles = false`).
- **Form Controls:** `<input>`, `<textarea>`, `<button>`, `<select>` retain live values in DOM properties (`value`, `checked`). Mutating via UI triggers synthetic `"change"` and `"input"` events.

### 4.2 HTML5 Tokenizer & Parser
- **Streaming State Machine (`HtmlTokenizer`):** Handles `Data`, `TagOpen`, `TagName`, `BeforeAttributeName`, `AttributeName`, `BeforeAttributeValue`, `AttributeValueQuoted`, `SelfClosingStartTag`, and raw text modes.
- **Void Elements:** `<area>`, `<base>`, `<br>`, `<col>`, `<embed>`, `<hr>`, `<img>`, `<input>`, `<link>`, `<meta>`, `<param>`, `<source>`, `<track>`, `<wbr>`. The parser never expects matching closing tags for void elements.
- **Raw Text Modes:** Switched on encounter of `<style>` or `<script>` tags, ensuring CSS and JS syntax characters (`<`, `>`, `&`) are treated as literal characters until the matching `</style>` or `</script>` end tag.

### 4.3 CSS Parser, Cascade & Media Queries
- **Selectors:** Supports tag names, `#id`, `.class`, attribute selectors `[attr]` and `[attr="value"]`, pseudo-classes, and descendant space combinators.
- **Specificity:** Represented as `Specificity { int ids, int classes, int tags }`. Triple lexicographical comparison resolves rule precedence.
- **Media Queries (`@media`):**
  - Evaluated in `StyleResolver::collect_matching_rules`.
  - Condition metrics: `min-width`, `max-width`, `min-height`, `max-height`.
  - Dynamically tested against `m_viewport_width` and `m_viewport_height`.
- **Style Cascade Priority:**
  1. Default User-Agent Stylesheet (`ua.css` defaults built into code).
  2. Author Stylesheet Rules (sorted by specificity, then source order).
  3. Element Inline `style="..."` attribute (highest specificity).
  4. Inherited properties (`color`, `font-size`, `text-align`, etc.) propagated down the DOM hierarchy.

### 4.4 Layout Box Tree & Formatting Contexts
- **LayoutBox:** Core geometry representation using the box model:
  ```cpp
  struct Dimensions {
      Rect content;
      EdgeSizes padding;
      EdgeSizes border;
      EdgeSizes margin;
  };
  ```
- **Formatting Contexts:**
  - **Block Formatting:** Vertical flow, auto margins, collapsing vertical margins.
  - **Flexbox (`display: flex`):** Main and cross axis alignment, `flex-grow`, `flex-shrink`, `flex-basis`, `flex-wrap: wrap`, and `gap`.
  - **Grid (`display: grid`):** Track dimension resolution (`grid-template-columns: 1fr 2fr 1fr`), cell placement, and track gaps.
  - **Inline Formatting Context (IFC):** Continuous inline flowing across text and elements (`<span>`, `<b>`, `<a>`), line breaking into `LineBox`es, and word wrapping.
  - **Positioning:** `static`, `relative`, `absolute`, `fixed`, coordinate offsets (`top`, `right`, `bottom`, `left`), and `z-index` stacking context sorting.
- **Incremental Layout:** Elements track an `m_is_dirty` flag. Calling `mark_dirty()` invalidates ancestors up to the formatting root, avoiding full-tree recomputations when only small subtrees mutate.

### 4.5 Display List & Multi-Backend Rendering
- **DisplayList:** An ordered `std::vector<PaintCommand>` of atomic draw calls:
  - `DrawRectCommand`: Filled rectangle with background color and border radius.
  - `DrawBorderCommand`: Individual border sides with color, width, and style.
  - `DrawTextCommand`: Text fragment at `(x, y)` with font color and style.
  - `DrawImageCommand`: Raster image blit with scaling and alpha blending.
- **TerminalRenderer:** Projects display list commands onto an 80x25 character grid using ANSI 24-bit Truecolor foreground/background escape sequences (`\033[38;2;R;G;Bm`).
- **BitmapRenderer:** Rasterizes commands into a 2D ARGB pixel buffer and outputs a standard uncompressed Windows BMP file (54-byte header + BGR pixel rows with 4-byte padding).

### 4.6 JavaScript Engine, Bytecode VM & GC
- **Ast & Bytecode:**
  - `JsLexer` -> `JsParser` produces AST nodes (`Stmt`, `Expr`).
  - `BytecodeCompiler` compiles AST into a `BytecodeFunction` containing an array of 8-bit opcodes and a constant pool.
- **Stack VM (`VM`):**
  - Manages `CallFrame` stack with local variable slot offsets.
  - Opcodes include `OpConstant`, `OpGetLocal`, `OpSetLocal`, `OpGetGlobal`, `OpSetGlobal`, `OpAdd`, `OpSubtract`, `OpMultiply`, `OpDivide`, `OpModulo`, `OpNegate`, `OpNot`, `OpEqual`, `OpNotEqual`, `OpLess`, `OpGreater`, `OpJump`, `OpJumpIfFalse`, `OpLoop`, `OpCall`, `OpReturn`, `OpPop`.
- **Mark-and-Sweep Garbage Collector (`Heap`):**
  - Allocates GC-managed cells (`JsObject`, `JsArray`, `JsFunction`).
  - Cyclic reference graphs (e.g. `A.b = B; B.a = A`) are collected by tracing roots (stack frames, global environment, and registered handles) and sweeping unreached cells.
- **ES6 Features:**
  - Arrow functions (`(a, b) => expr` and `(x) => { ... }`) with lexical environment capture.
  - Template literals (`` `Hello ${name}!` ``) with expression interpolation.
  - Promises (`new Promise()`, `.then()`, `.catch()`, `Promise.resolve()`, `Promise.reject()`) executed via microtask queues.
  - JSON serialization/deserialization (`JSON.parse`, `JSON.stringify`).

### 4.7 Web Storage & Cookie Engine
- **Storage (`include/litebird/js/Storage.hpp`):**
  - Key-value string map supporting W3C `Storage` interface: `getItem`, `setItem`, `removeItem`, `clear`, `key`, and `length`.
  - Instantiated for `window.localStorage` and `window.sessionStorage`.
- **CookieJar (`include/litebird/js/Storage.hpp`):**
  - Parses RFC cookie assignment strings: `name=value; Path=/; Max-Age=3600; Expires=...`.
  - Serializes matching active cookies into a formatted header string when `document.cookie` is queried.

### 4.8 Canvas 2D Rasterizer
- **Canvas2D (`include/litebird/render/Canvas2D.hpp`):**
  - Rasterizer maintaining an underlying `ImageData` buffer (32-bit ARGB `0xAARRGGBB`).
  - Supports path commands: `beginPath`, `moveTo`, `lineTo`, `closePath`, `stroke`, `fill`.
  - Integer Bresenham line algorithm for strokes and multi-node scanline polygon filling.
  - Direct primitives: `fillRect`, `clearRect`, `strokeRect`, and `arc`.
  - Integrated into `LayoutTree` as `BoxType::ImageNode`, enabling `<canvas>` to be laid out and rendered seamlessly.

### 4.9 Network, EventLoop & TUI Shell
- **ResourceLoader (`include/litebird/net/ResourceLoader.hpp`):**
  - Page requests use `load_for_page(url, context)` (Phase 33): the document URL is the capability, the base URL only resolves references, and web documents cannot read local files. Embedder navigation is the separate `load_navigation`.
  - Zero-dependency POSIX socket HTTP/1.1 client. Caller-controlled methods, request targets, and headers are validated against HTTP syntax/control-byte rules; request/response headers are capped at 64 KiB and the full response stream at 16 MiB.
  - Resolves relative URLs (`../`, `./`, `/`).
- **EventLoop (`include/litebird/js/EventLoop.hpp`):**
  - High-resolution timer queue executing `setTimeout` and `setInterval` tasks.
  - Dynamically triggers layout and repaint on DOM mutations.
- **BrowserShell (`include/litebird/shell/BrowserShell.hpp`):**
  - Keyboard-driven interactive TUI browser with navigation history (`back`, `forward`, `reload`).
  - URL entry prompt (`[o]`) and DevTools inspector pane (`[d]`).

### 4.10 SVG Vector Graphics Engine (`include/litebird/render/SvgRenderer.hpp`)
- **Direct DOM Integration:** `<svg>` root elements are recognized in `LayoutTree::build()` and converted directly into `BoxType::ImageNode` layout boxes.
- **ViewBox Coordinate Normalization:** Computes scaling factors (`scale_x = width / vb_w`, `scale_y = height / vb_h`) to map abstract coordinate space onto physical pixel buffers.
- **Vector Shape Rasterization:**
  - `<rect>`: Rendered with fill and stroke bounding outlines.
  - `<circle>` & `<ellipse>`: Midpoint and scanline rasterization.
  - `<line>`, `<polyline>`, `<polygon>`: Bresenham lines and point-in-polygon scanline fills.
  - `<path>`: Parses SVG path data commands (`M`/`m`, `L`/`l`, `H`/`h`, `V`/`v`, `Z`/`z`) into vector polygon vertices and fills/strokes via `Canvas2D`.
- **ImageData Generation:** Creates ARGB 32-bit pixel buffers (`ImageData`) embedded into the layout box tree for immediate painting by terminal or bitmap renderers.

### 4.11 CSS Transitions & Keyframe Animations (`include/litebird/css/StyleSheet.hpp`)
- **`@keyframes` Rule Engine:** Parses percentage keyframe steps (`0%`, `50%`, `100%`, `from`, `to`) and stores animated properties (`KeyframeRule`, `KeyframeStep`).
- **Timing Functions:** Supports standard cubic bezier approximations: `linear`, `ease`, `ease-in`, `ease-out`, `ease-in-out` (`StyleResolver::evaluate_timing_function`).
- **Property Interpolation:** High-precision numerical interpolation for continuous transitions and keyframe steps (`StyleResolver::interpolate_property`, `interpolate_color`) across `opacity`, `width`, `height`, `color`, and `background-color`.
- **Animation Properties:** Full support for `animation-name`, `animation-duration`, `animation-timing-function`, `animation-delay`, `animation-iteration-count`, `transition-property`, and `transition-duration`.
- **Frame Ticks (`requestAnimationFrame`):** Bound to the global window / JS scope, enqueuing frame callbacks with high-resolution timestamps.

### 4.12 Advanced CSS Pseudo-Classes & Selectors (`include/litebird/css/StyleResolver.hpp`)
- **Structural Pseudo-Classes:**
  - `:first-child`, `:last-child`: Evaluates child index among sibling elements.
  - `:nth-child(An+B)` & `:nth-of-type(An+B)`: Supports integer indices, `even` (`2n`), `odd` (`2n+1`), and arbitrary `An+B` linear patterns.
- **User Action & State Pseudo-Classes:**
  - `:hover`, `:focus`, `:active`: Evaluates interactive UI states tracked on DOM elements.
  - `:disabled`, `:checked`: Matches form control attributes on buttons, checkboxes, and inputs.
- **Negation Pseudo-Class (`:not(...)`):**
  - Parses nested simple selectors inside `:not(...)` and inverts match evaluation.

### 4.13 ES6 Collections & Language Expansion (`include/litebird/js/`)
- **ES6 `Map` Collection:** Implements key-value mapping with arbitrary key types, insertion-ordered iteration, `.set(k, v)`, `.get(k)`, `.has(k)`, `.delete(k)`, `.clear()`, and dynamic `.size` getter.
- **ES6 `Set` Collection:** Implements unique value collection with `.add(v)`, `.has(v)`, `.delete(v)`, `.clear()`, and dynamic `.size` getter.
- **Destructuring Declarations:**
  - Array destructuring: `const [a, b, c] = [1, 2, 3];`
  - Object destructuring: `const { x, y } = { x: 10, y: 20 };`
  - Fully integrated in AST evaluator and fallback bytecode compiler.
- **Default Function Parameters:**
  - Supports default parameter values in function declarations `function foo(x, y = 10) { ... }` and arrow functions `(a, b = 2) => a + b`.

---

## 5. Critical Memory & ABI Safety Rules

1. **Header Layout vs. Make Dependency Tracking:**
   - When modifying class member layouts in core headers (such as `include/litebird/dom/Node.hpp`, `Element.hpp`, `JsValue.hpp`, or `LayoutBox.hpp`), **ALWAYS** execute `make clean && make -j$(nproc)` before running tests or CLI binaries.
   - *Reason:* Object files compiled against older header definitions will experience binary structure misalignment leading to fatal `std::bad_alloc` or segmentation faults.
2. **Bytecode Stack Call Frame Offsets:**
   - In `VM::OpCall`, local slots must be offset by `m_stack.size() - arg_count - 1` to account for the function receiver on the stack.
   - In `VM::OpReturn`, the return value must be stored at `frame->slots_offset` before truncating the stack.
3. **Canvas Pixel Format:**
   - `ImageData::pixels` stores 32-bit ARGB integer values (`uint32_t`), indexed as `pixels[y * width + x]`. Do not treat `ImageData::pixels` as a `uint8_t` byte array.

---

## 6. Extension Playbooks (How to Add Features)

### Adding a New DOM API / Method
1. Declare the method in `include/litebird/dom/Element.hpp` (or `Node.hpp`).
2. Implement the method logic in `src/dom/Node.cpp`.
3. In `src/js/JsValue.cpp` (`JsValue::get_property`), add the method name to the DOM element property dispatch table:
   ```cpp
   if (prop == "myNewMethod") {
       return JsValue::make_native_function([node](const std::vector<JsValue>& args) {
           // unwrap args and call node->my_new_method(...)
           return JsValue::make_undefined();
       });
   }
   ```
4. Add unit test assertions in `tests/test_suite.cpp`.

### Adding a New CSS Property
1. Add property enum value or data field to `ComputedStyle` in `include/litebird/css/StyleSheet.hpp`.
2. In `src/css/StyleResolver.cpp` (`apply_declaration`), add a handler mapping the CSS string property (e.g., `"opacity"`) to the computed field.
3. In `src/layout/LayoutBox.cpp` or `src/render/PaintCommand.cpp`, incorporate the property into box dimensions or display list commands.

### Adding a New JavaScript Bytecode Opcode
1. Add the opcode identifier to `enum class OpCode : uint8_t` in `include/litebird/js/Bytecode.hpp`.
2. Add disassembly formatting in `src/js/Bytecode.cpp`.
3. In `src/js/BytecodeCompiler.cpp`, emit the opcode during AST traversal (`emit_byte(OpCode::OpMyOp)`).
4. In `src/js/VM.cpp`, add the `case OpCode::OpMyOp:` dispatch branch inside `VM::run()`.

### Adding a Test Suite Entry
1. Open `tests/test_suite.cpp`.
2. Write a self-contained test function:
   ```cpp
   void test_my_feature() {
       // Setup document or JS environment
       // Execute logic
       // assert(...)
   }
   ```
3. Register the function inside `main()` with `RUN_TEST(test_my_feature);`.
4. Run `make test` and ensure all suites pass.

---

## 7. Current Phase Status & Roadmap

| Phase | Milestone Description | Status |
| :--- | :--- | :--- |
| **Phase 1** | Initial Engine Core (HTML, CSS Specificity, Block Layout, Bitmap/Terminal Render, Basic JS) | ✅ Completed |
| **Phase 2** | Inline Formatting Context (IFC), Mark-and-Sweep Tracing GC, DOM EventTarget Bubbling | ✅ Completed |
| **Phase 3** | Interactive TUI Browser Shell, Asynchronous EventLoop with Timers, Resource Loader | ✅ Completed |
| **Phase 4** | CSS Positioning (Relative/Absolute/Fixed), Box Sizing Constraints, JS Prototypes & Arrays, Incremental Layout | ✅ Completed |
| **Phase 5** | Promises & Microtasks, JSON, CSS Grid Layout, Multi-line Flex Wrapping, 3-Phase DOM Events, Image Decoding | ✅ Completed |
| **Phase 8** | HTML5 `<canvas>` 2D Context, Web Storage (`localStorage`/`sessionStorage`), `document.cookie`, CSS Media Queries (`@media`), ES6 Arrow Functions & Template Literals | ✅ Completed (28/28 tests passing) |
| **Phase 9** | CSS Transitions & `@keyframes` Animations, SVG Vector Graphics (`<svg>`), Advanced CSS Pseudo-Classes (`:nth-child`, `:hover`, `:focus`, `:not`), ES6 Map/Set Collections & Destructuring | ✅ Completed (32/32 tests passing) |
| **Phase 10** | CSS 2D Transforms & Affine Matrix Math (`TransformMatrix2D`, `translate`/`rotate`/`scale`/`skew`/`transform-origin`, inverse hit-testing), Canvas 2D Gradients & Bézier Curves (`CanvasGradient`, `quadraticCurveTo`, `bezierCurveTo`), ES6 Classes & `instanceof` (`class`, `extends`, `super`, static/instance methods), and `async`/`await` Engine Support | ✅ Completed (36/36 tests passing) |
| **Phase 11** | Modern Web App & Network Foundation: CSS Custom Properties & Variables (`var()`), CSS Math Expression Engine (`calc()`), Modern Fetch API (`fetch()`, `Response`, `Headers`), and Standard `URL` / `URLSearchParams` Objects | ✅ Completed (40/40 tests passing) |
| **Phase 12** | Multi-Threaded Web Workers (`postMessage`/`onmessage`), Modern CSS Grid & Flexbox Refinements (`repeat`, `minmax`, `gap`), and RFC 6455 WebSocket Client (`new WebSocket`) | ✅ Completed (43/43 tests passing) |
| **Phase 13** | Modern DOM & Web Components: MutationObserver API (`new MutationObserver`, microtask batching, record generation), Autonomous Custom Elements (`customElements.define`, `whenDefined`, upgrade, lifecycles), and Shadow DOM (`attachShadow`, scoped CSS encapsulation, `<slot>` content projection) | ✅ Completed (46/46 tests passing) |
| **Phase 14** | Modern Browser Capabilities & Advanced Engine Platform: Observers & Client Routing (`IntersectionObserver`, `ResizeObserver`, `History` SPA navigation API), CSS Visual Filters & Canvas Blending (`blur`, `brightness`, `contrast`, `sepia`, `drop-shadow`, `backdrop-filter`, `globalCompositeOperation`), JS Generators, Iterators & AbortController (`function*`, `yield`, `for...of`, `AbortController`, `fetch` cancellation), and CSS Grid Named Areas (`grid-template-areas`, `grid-area`) & Layout Containment (`contain: layout paint strict`) | ✅ Completed (52/52 tests passing) |
| **Phase 15** | **Spec Fidelity & The Scoreboard:** complete WHATWG tree construction (23 insertion modes, adoption agency, foster parenting, foreign content, templates), spec-complete tokenizer with the full 2231-entry entity table, CSS shorthand expansion + `!important` cascade tiers + relative/viewport units, plus the conformance harness, sanitizer targets, fuzzing and CI. **1571/1790 (87.8%) on the html5lib tree-construction corpus; 58/58 test suites; 0 fuzz findings over 500 mutations.** | ✅ Completed |
| **Phase 16** | **Floats, Block Formatting Contexts & Table Layout:** `float: left/right` with shrink-to-fit sizing, per-line line-box shrinking around floats, `clear`, BFC establishment and float containment, plus `display: table/table-row/table-cell/table-row-group` with anonymous table structure, automatic column sizing and per-row height resolution; bitmap text advance aligned with the layout metric. **1575/1790 (88.0%) conformance; 63/63 test suites; 0 fuzz findings.** | ✅ Completed |
| **Phase 17** | **Conformance push: the scoreboard pays off.** The full script-data state machine (escaped and double-escaped runs), `<pre>`/`<listing>`/`<textarea>` first-newline handling, `<nobr>` adoption agency, `<html>`/`<body>` inside templates, namespace-aware scope checks (an `<svg td>` is not an HTML `td`), SVG attribute case adjustment, foreign-content `</p>`/`</br>` breakouts, modern `<select>` content parsing, `eof-in-tag` discarding, and three harness-fidelity fixes. **1575/1790 (88.0%) → 1770/1792 (98.8%); 69/69 suites; 0 fuzz findings over 900 mutations.** | ✅ Completed |
| **Phase 18** | **Layout fidelity: percentages and clipping.** Percentage `width`/`height`/`min`/`max` and percentage `margin`/`padding` now resolve against the containing block at layout time (before this, `width: 50%` silently became *fifty pixels*), percentage table cells resolve against the table width, and `overflow: hidden|auto|scroll` / `contain: paint` emit clip commands that both renderers honour. **98.8% conformance unchanged; 71/71 suites.** | ✅ Completed |
| **Phase 19** | **JavaScript compatibility:** function-scoped `var` with hoisting, `typeof`, `switch`/`case`/`default`/`break`, right-associative exponentiation (`**`), and short-circuit nullish coalescing (`??`); AST interpreter fallback for constructs not yet emitted by bytecode. **80/80 engine suites.** | ✅ Completed |
| **Phase 20** | **Compact JavaScript values:** move cold function/DOM/canvas/promise/generator state behind a copy-on-write tagged payload and use typed accessors. `sizeof(JsValue)` reduced **296 → 96 bytes (67.6%)**; local 20,000-node render RSS fell **2.4%**, and a 200k-iteration bytecode-loop median improved **59.4%** (baseline/worktree comparison; details in `AUDIT.md`). **81/81 engine suites; ASan/UBSan and 400-mutation fuzz smoke passed.** | ✅ Completed |
| **Phase 21** | **Indexed CSS cascade:** pre-index author selectors by a rightmost ID/class/tag key with a universal fallback; query candidate selectors per element, retain full selector matching and stable source order. Selective 1,600-rule/1,600-element benchmark improved **88.25 → 30.35 ms (2.9×)**. **82/82 engine suites.** | ✅ Completed |
| **Phase 22** | **Pipeline fuzz coverage:** run HTML corpus mutations through layout and bitmap rendering, plus valid and malformed BMP/PPM headers through image loading and rendering. Fuzzing exposed and fixed signed overflow while parsing overlong PPM numeric fields. **82/82 engine suites; 400 parser mutations + 400 pipeline runs, 0 findings under ASan/UBSan.** | ✅ Completed |
| **Phase 23** | **The JavaScript engine, rebuilt against an outside scoreboard:** an 801-case corpus with expected output recorded from Node.js (`make js-conformance`, enforced in CI); new lexer, parser, evaluator and standard library on shared prototypes; promises with the specification's ordering and `await` that suspends; an event loop with a microtask checkpoint and a virtual clock; a collector that derives its roots from reference counts and collects closure cycles; `DOMContentLoaded`/`load` and `removeEventListener`; deep JavaScript ends in a `RangeError`, not a crash. | ✅ Completed (corpus 140 → 796 of 801 cases; 85/85 unit suites) |
| **Phase 24** | **Strings that cost what they read:** a string value is a 24-byte handle on a shared block of UTF-8 (`JsString`) instead of text stored inside the value. Reading, passing and indexing a string no longer copy or re-scan it, `s += x` appends in place, a large slice is a range of its parent. Lookbehind in regular expressions is matched right to left. A loop over 200,000 characters went from **77 s to 91 ms**, 100,000 appends from **9.8 s to 49 ms**; short strings cost the same as before. | ✅ Completed (corpus 840 of 845 cases, 34 of them new long-string scripts; 88/88 unit suites; `make tsan`, `make bench`, `make diff-strings`) |
| **Phase 25** | **Trees of any depth:** the HTML parser attaches nothing deeper than 512 levels (an element that would be deeper becomes a sibling; none is dropped, and the parser's state is untouched), every question about the stack of open elements is answered from an index, and nothing that walks the DOM recurses, so a script may nest as deep as it likes. Bitmaps of `<canvas>` and `<svg>` have a budget, `calc()` a nesting limit. `tools/fuzz/deep_pages.py` generates the pages a mutation fuzzer cannot: 300,000 nested `<div>` went from **`SIGSEGV` to rendering in 4 s**, a paragraph of 100,000 images from **35 s to 1.3 s**. | ✅ Completed (deep pages: 264 of 392 runs failed, now 0; 95/95 unit suites; html5lib 1771; `make deep` in CI) |
| **Phase 26** | **The tree builder against pages nobody wrote:** random pages and fragments parsed by Litebird and by parse5, every difference cut down to a few tags and read against the standard. A void element written with a slash (`<br/>`) closed its parent; `innerHTML` parsed everything as the content of a `<body>`; an end tag could close an SVG or MathML element of the same name; 21 faults in all, 13 of them with no case in the corpus. **408 of 6,000 random pages got a wrong tree, now none.** | ✅ Completed (html5lib 1771 → 1781 of 1792, the 11 left all `<select>`; 103/103 unit suites; `make diff-trees` over four kinds of page) |
| **Phase 27** | **The DOM a script sees, rebuilt against an outside scoreboard:** a 572-case corpus with expected output recorded from jsdom (`make dom-conformance`, enforced in CI). A DOM node was a special kind of value whose members were a list of names inside `JsValue::get_property`; it is now an object with its interface's prototype, one per node. About 170 interfaces, a selector engine, the standard's serializer, form state and validation, events with default actions, mutation observers, custom elements, live ranges, a computed style with a real cascade, `location`/`history`, and the URL Standard's parser (web-platform-tests data, `make url-conformance`). A page's scripts run while it is parsed. | ✅ Completed (DOM corpus 61 → 572 of 572; URL data 2 → 1188 of 1188; 110/110 unit suites) |
| **Phase 28** | **XML:** an XML 1.0 (Fifth Edition) parser with namespaces, written against the W3C XML Conformance Test Suite (`make xml-conformance`) and compared with Expat and libxml2 on generated documents (`make diff-xml`). It reads nothing outside the document and bounds what entities and attribute defaults may expand to. `DOMParser` with an XML type gives an XML document, or a `parsererror` document; it gave an HTML one. A document knows whether it is HTML, elements and attributes have namespaces, a long attribute list is indexed, CDATA sections and processing instructions are nodes. `XMLSerializer` and `innerHTML` in an XML document follow *DOM Parsing and Serialization* and are checked by writing trees made to break them and reading them back. `XMLHttpRequest` answers `data:` URLs, so `responseXML` exists. The web-platform-tests for `dom/nodes` and `domparsing` run with their own harness (`make wpt`). | ✅ Completed (W3C XML suite 1702 of 1702 counted; web-platform-tests 4640 of 5192 subtests, the others listed with reasons; DOM corpus 572 → 629 of 629; 129/129 unit suites) |
| **Phase 29** | **Table spans and collapsed borders:** cells are placed into an occupancy grid before sizing, so `rowspan`/`colspan` reserve their slots (`rowspan="0"` spans to the end of the group); a spanning cell distributes its width requirement over the columns it covers; row heights grow from single-row cells first, then rowspan cells grow the last row they cover; `border-collapse: collapse` makes each cell paint half of a shared border and forces `border-spacing` to 0; `border-spacing: <h> <v>` moves cells in from the table edge and apart; percentage cell widths resolve against the table's width (`width_basis_override`); table cells and floats keep their style edges through `apply_style_edges()`. | ✅ Completed (html5lib 1781 of 1792 unchanged; 113/113 unit suites) |
| **Phase 30** | **Deletion and front edits are linear:** the property map is now an insertion-ordered `std::list` with a hash index that is built once above 12 entries and erased in O(1), so `delete` does not rebuild an index; array storage is `std::deque`, so `shift()` and `unshift()` move no tail. 50,000 deletes went from 131,423 ms to 25–29 ms on the machine used to verify Phase 23; `shift()` until empty went from 8,228 ms to about 10 ms. | ✅ Completed (113/113 unit suites; JS corpus 840 of 845 unchanged) |
| **Phase 31** | **CSS whitespace trimming is linear:** the parser and resolver scan outer whitespace once and shift the surviving text at most once; `nth-child` no longer copies and lowercases padding per child. 300,000 spaces in the measured `:nth-child`, grid, filter, duration and transform paths went from seconds to tens or hundreds of milliseconds with the same layout/paint. | ✅ Completed (117/117 unit suites; `make bench-css` compares 20 padded runs with compact pages) |
| **Phase 32** | **The CSS color keywords are the CSS values everywhere:** the renderer’s 19-name table (15 wrong) is gone; `Color::parse()` and the computed style’s color path share one 148-name table, all CSS Color 4 keyword values, and `transparent` has alpha 0. `tomato`, `silver`, `rebeccapurple`, `aqua`/`cyan`, `grey` aliases and the rest now paint and compute as the specification says; unknown names still parse to black. | ✅ Completed (114/114 unit suites) |
| **Phase 33** | **Page resource loads are origin-aware:** `fetch()` and `new Worker()` no longer send arbitrary paths to the filesystem. `ResourceLoader::load_for_page` requires the document address, resolves relative references against the document/base URL, blocks web documents from `file:`/local paths, and keeps local-file access only for documents the embedder loaded from `file:`; trusted CLI/TUI navigation is a separate `load_navigation` entry point. A failed worker load is an error, not inline source. | ✅ Completed (132/132 unit suites; `make security-fetch` 3 checks, 0 failures) |
| **Phase 34** | **A function can be a prototype:** a function is a value and a prototype link points at a heap cell, so `Object.getPrototypeOf(Derived)` was not `Base`, `Object.getPrototypeOf(TypeError)` was not `Error`, a static member was found one parent up and no further, and `Function.prototype` was an object. A function's cell now knows the function it stands for (`JsValue::as_prototype`, `JsObject::function_value`, with no object and no value growing), the collector takes the pair apart, a class is linked to its parent instead of keeping it in a hidden `%super`, `super()` calls whatever the prototype is when it runs, `Reflect.construct` honours its third argument, `Object.defineProperty` follows the standard's ValidateAndApplyPropertyDescriptor, and the DOM's interface objects have their parents as prototypes. Classes compiled by Babel and TypeScript run. | ✅ Completed (144/144 unit suites; JS corpus 907 of 921 with 76 new cases; web-platform-tests 4658 of 5192; DOM corpus 640 of 640; 108 more of 15,686 test262 tests and none fewer) |
| **Phase 35** | **Attribute-list end removals are linear:** the item list and the lazy index's ascending numbers are vectors with a logical start offset: a front erase advances the offset and compacts only when the dead prefix reaches the live tail, a back erase pops, and an empty list allocates nothing; lookup, value-update, copy/move, order and random-access iterators are unchanged. Draining 50,000 attributes from the front went from about 7.1 s to 0.09 s whole-process on the verification machine; the back stayed cheap. A middle removal still moves the tail. | ✅ Completed (135/135 unit suites; `make bench-attributes` 6 cases, 0 failures) |
| **Phase 36** | **Usable browser pipeline: HTTPS/HTTP 1.1/CORS, subresources, live reflow, TrueType and the web GUI.** The row was not added when the phase merged; Phase 63 added it so that the table can be counted. The section below is the description. | ✅ Completed (see the section "Phase 36 Architecture") |
| **Phase 37** | **Iterative RegExp VM, modern CSS cascade/units, CSSOM `el.style`, and HTML5 date/time inputs:** replaces recursive CPS regex matching and recursive AST destruction with an iterative bytecode VM and heap backtracking stack; adds CSS child/sibling combinators (`>`, `+`, `~`), `:is()`/`:where()`/selector-list `:not()`/`:host`, `@layer`, `@supports`, MQ L4 range/boolean evaluation, `min()`/`max()`/`clamp()`, `dvh`/`dvw`/`vmax`, percentage `border-radius`, iterative CSS block/selector parsing + `min_ancestors` / `AllDescendant` DP caching; expands `el.style` shorthands, canonical CSSOM values, and vendor aliases; and implements `date`/`time`/`month`/`week`/`datetime-local` input sanitization, `valueAsDate`/`valueAsNumber`, `stepUp()`/`stepDown()`, and constraint validation. | ✅ Completed (174/174 unit suites; JS corpus 926/940, `regexp.dat` 39/39; DOM corpus 644/644) |
| **Phase 38** | **Real-web JPEG/GIF decoders, upgraded Canvas 2D & SVG vector renderer:** standalone Baseline/Progressive JPEG and GIF87a/89a decoders verified pixel-by-pixel against `libjpeg` and ImageMagick, plus CTM-aware Canvas 2D and SVG paths/arcs/gradients/`<use>`/`<clipPath>`. | ✅ Completed (139/139 unit suites; `make verify-media` 35/35; `make fuzz-media` 1,800 release / 4,500 ASan) |
| **Phase 39** | **The tokenizer's repeated attribute names are linear:** the HTML tokenizer dropped a repeated attribute name by comparing it with every attribute already accepted, which is the square of the count. A tag with 32 or more accepted attributes now keeps a hash set of views of their names, rebuilt when the attribute vector grows under it; shorter tags keep the scan. One element with 50,000 attributes went from 3,474 ms to 93 ms whole-process, an end tag with that many from 3,170 ms to 25 ms, and many small tags did not change. First-wins, ASCII case folding, order and every value spelling are unchanged. | ✅ Completed (159/159 unit suites; `make bench-tokenizer-attributes` 7 cases, 0 failures) |
| **Phase 40** | **Worker MIME/module policy, EventTarget dispatch, and shutdown crash fix:** enforces the 16 WHATWG JavaScript MIME types and HTTP 2xx status on `Worker` scripts and `importScripts(...)`, parses `WorkerOptions` (`{type, name}`), rejects `{type:"module"}` cleanly with an error event instead of silently running classic JS, dispatches both `onerror`/`onmessage` and `addEventListener`/`removeEventListener` (`once`, `handleEvent`, `preventDefault`, `stopImmediatePropagation`) on page and worker scopes, supports nested worker wake/teardown, breaks the `inst` ↔ `WorkerObject` leak cycle, and joins active worker threads cleanly before static destruction (fixing the exit-time `SIGSEGV` / `heap-use-after-free` crash). | ✅ Completed (157/157 unit suites; `make security-worker` 17/17 checks, 200/200 4-worker startup/exit runs, 0 crashes in Release & ASan+UBSan) |
| **Phase 41** | **A string built half a surrogate pair at a time is linear:** a join no longer writes a high surrogate half that ends its result; the handle owes the half (`m_pending`), and settling happens on first read. Half-at-a-time appends went from 77–252 ms at 5–20k pairs to 17–28 ms (cross-bench numbers in `AUDIT.md`, finding 47). | ✅ Completed (162/162 unit suites) |
| **Phase 42** | **Nested browsing contexts (`<iframe>`), cross-realm DOM wrappers, origin isolation, and `.xhtml` execution.** The row was not added when the phase merged; Phase 63 added it so that the table can be counted. The section below is the description. | ✅ Completed (see the section "Phase 42") |
| **Phase 43** | **`new.target`, and what a constructor makes:** `new.target` is the function `new` was applied to (through `super(...)`, `Reflect.construct(F, args, G)` and a bound function), an object a parent's constructor returns becomes `this` in the child, `this` is bound once (a second `super()`, none, and a returned primitive throw), `Reflect.construct` of a DOM interface with a `new.target` makes the object, and a custom element compiled for older browsers is made and upgraded (the standard's construction stack, kept beside the call context). | ✅ Completed (191/191 unit suites; JS corpus 974/989, `construct.dat` 47/49 against 13 before; DOM corpus 669/669, `compiled.dat` 25/25 against 2 before; test262 43 more and none fewer of 47,967) |
| **Phase 44** | **Unicode 17.0 identifier validation, `\uXXXX` / `\u{...}` identifier escapes, escaped keyword/contextual-keyword rules, and Unicode whitespace/line terminators:** adds compact `constexpr` Unicode 17.0 `ID_Start` (682 ranges) and `ID_Continue` + ZWNJ/ZWJ (795 ranges) tables (`JsIdentifierTables.hpp`), full UTF-8 + `\uXXXX`/`\u{...}` decoding in `JsLexer` (including `#\u...` private names and arbitrary leading zeros in `\u{...}`), `JsTokenType::EscapedKeyword` and `JsToken::has_escape` enforcement across `JsParser` (rejecting escaped reserved words and contextual keywords outside `IdentifierName`, enforcing strict-mode future reserved words, async/generator/arrow-parameter `[Yield, Await]` and `static {}`/field-initializer `ContainsArguments` early errors), and full ECMA-262 Unicode `WhiteSpace` and `U+2028`/`U+2029` line-terminator handling. | ✅ Completed (165/165 unit suites; JS corpus 921/935 with 14 new Node-verified cases in `identifiers.dat`; Test262 default 21 dirs 10,835→12,275 / 15,686 (+1,440, 0 regressions); `language/{identifiers,keywords,future-reserved-words,reserved-words}` 374/374 = 100.0%) |
| **Phase 45** | **Resolve a Promise iterator one item at a time.** The row was not added when the phase merged; Phase 63 added it so that the table can be counted. The section below is the description. | ✅ Completed (see the section "Phase 45 Architecture") |
| **Phase 46** | **Canvas 2D text:** `fillText`/`strokeText`/`measureText` with the `font` (CSS shorthand subset), `textAlign` and `textBaseline` canvas state — rasterised with the Phase 36 TrueType AA rasterizer into an AA coverage buffer, blitted through the CTM by inverse mapping (max-filtered down-sampling, user-space gradient sampling), with `maxWidth` horizontal condensing, solid/gradient paint, clip and `globalAlpha` honoured. SVG/path/hit-test gaps of row 6 remain. | ✅ Completed (finding 52; 179/179 unit suites) |
| **Phase 47** | **The WHATWG Encoding Standard, the `<meta charset>` prescan, and XML/WPT encodings.** The row was not added when the phase merged; Phase 63 added it so that the table can be counted. The section below is the description. | ✅ Completed (see the section "Phase 47 Architecture") |
| **Phase 48** | **Indexed sized-float placement:** partition each block’s vertical bands into intervals and augment a balanced treap with the best available width; find the first fitting band edge after the flow cursor instead of sorting and sweeping every previous band for every new float. The existing bands for line wrapping, zero-height accounting and clearance remain unchanged. | ✅ Completed (178/178 engine suites; 10,000 same-size floats 5.244 → 0.096 s end-to-end; six 1k/3k and three 10k out-of-corpus layout dumps identical) |
| **Phase 49** | **Customizable `<select>` content and `<selectedcontent>` cloning:** adds `"select"` as a default scope boundary in `m_scope_boundary_indices[0]`, reconstructs active formatting elements on characters and `<option>`/`<optgroup>`/`<hr>` in `Mode::InSelect`, restricts `<select>` breakout to `<input>` (keeping `<keygen>`/`<textarea>` inside `<select>` via `send_to_in_body`), pops open elements in `stop_parsing()`, and iteratively clones the selected `<option>`'s children into the `<select>`'s first `<selectedcontent>` (outside nested `<select>`/`<datalist>`) with disabled/ancestor-`<optgroup>`/`multiple`/`size` checks and `m_script_ran` fallback. | ✅ Completed (**1792/1792 (100.0%)** html5lib tree construction; 181/181 unit suites; CI floor `1781 → 1792`) |
| **Phase 50** | **CSS Color 4/5 color spaces, §13.2 OKLCH gamut mapping, `color-mix()`, relative colors, and `light-dark()`:** replaces three duplicate legacy parsers with a unified `CssColor` engine (`include/litebird/common/Color.hpp`, `src/common/Color.cpp`) implementing all 15 CSS Color 4 spaces (`srgb`, `srgb-linear`, `display-p3`, `display-p3-linear`, `a98-rgb`, `prophoto-rgb`, `rec2020`, `xyz-d50`, `xyz-d65`, `lab`, `lch`, `oklab`, `oklch`, `hsl`, `hwb`), exact §17 matrices & Bradford D65↔D50 adaptation, per-channel missing-component (`none`) and powerless-hue semantics, §13.2 OKLCH chroma-reduction gamut mapping, CSS Values 4 math inside color channels, 1..N-argument `color-mix()` with all hue modifiers, relative color syntax (`from ...`), `light-dark()` with `color-scheme` inheritance, and §15 computed serialization, wired through `StyleResolver` (also fixing balanced-paren `drop-shadow`/`box-shadow`/`border` parsing), `DomStyleBindings`, and `SvgRenderer`. | ✅ Completed (185/185 unit suites; `make verify-color` 180/180 space matrix, 13/13 gamut map, 133/133 WPT valid, 61/61 invalid; `make fuzz-color` 5,008 inputs, 0 crashes in Release & ASan+UBSan; 4,860/4,860 live upstream WPT `color-computed-*.html`) |
| **Phase 51** | **The attribute model: namespaces, `Attr` objects, and named properties.** The row was not added when the phase merged; Phase 63 added it so that the table can be counted. The section below is the description. | ✅ Completed (see the section "Phase 51 Architecture") |
| **Phase 52** | **Repairing `main` after the Phase 49 and Phase 51 merges.** The row was not added when the phase merged; Phase 63 added it so that the table can be counted. The section below is the description. | ✅ Completed (see the section "Phase 52 Architecture") |
| **Phase 53** | **UTF-16 storage for non-ASCII `JsString`:** stores ASCII buffers in 1 byte per code unit and non-ASCII buffers in 2-byte UTF-16 (`char16_t`) code units while keeping `sizeof(JsString) == 24` and building UTF-8 (`Utf8Cache` + `marks[]`, `Utf8Slice`) and wide `Utf16View` (`Wide`) caches lazily. High surrogates are ordinary 16-bit code units rather than 3-byte UTF-8 prefixes that must rewrite into 4 bytes when a low half follows, so completing a surrogate pair after reading, `indexOf`, or `substring` writes in place (`17 ms` vs Node `81 ms` at 20,000 pairs with reads, down from `893 ms`), alongside a 64-unit stack fast path in `Utf16View::{find,rfind}` and a `sole_thread` + `cache_covers_past` CAS tail rewind. | ✅ Completed (193/193 unit suites; `make tsan` green; `make bench` 45/45; `make bench-surrogates` 6/6; `make diff-strings` 50,485/50,580) |
| **Phase 54** | **Lossless WebP: the container and the `VP8L` bitstream.** The row was not added when the phase merged; Phase 63 added it so that the table can be counted. The section below is the description. | ✅ Completed (see the section "Phase 54 Architecture") |
| **Phase 55** | **Complete RegExp flags (`d`/`u`/`v`), Unicode 17.0 property escapes & case folding, ES2025 modifiers / duplicate named groups / `RegExp.escape()`, and `Symbol.*` dispatch:** generates compact deduplicated Unicode 17.0 tables (`RegExpUnicodeTables.{hpp,cpp}` via `tools/generate_regexp_unicode_tables.py`: 22,207 shared ranges covering 80 General_Category, 98 Binary, 344 Script, 344 Script_Extensions aliases, all 7 Properties of Strings with 2,760 multi-CP sequences, 1,482 Unicode case classes, and 1,144 legacy case classes); upgrades `BuiltinsRegExp.cpp` with O(log R) normalized `CharClass`, `ClassSet` + `RegExpOp::MatchStringSet` (indexed by edge CP in longest-first order) for ES2024 `v`-mode `--`/`&&`/nested `[...]`/`\q{...}` and static `MayContainStrings`, distinct `iu` vs `iv` case-closure/complement ordering, ES2022 `d` (`hasIndices`) `match.indices`/`indices.groups`, ES2025 `(?ims-ims:...)` 3-bit scoped modifiers, alternative-scoped duplicate named capturing groups with Unicode `RegExpIdentifierName`, `RegExp.escape()`, parse-time regex literal validation (`js_regexp_validate` in `JsParser.cpp`), and full `Symbol.{match,matchAll,replace,search,split,species}` dispatch in `BuiltinsRegExp.cpp` and `BuiltinsString.cpp`. | ✅ Completed (198/198 unit suites; test262 `built-ins/RegExp` 739→1,849/1,879 (98.4%, +1,110), `language/literals/regexp` 68→237/240 (98.8%, 0 fail), `String.prototype/{match,matchAll,replace,replaceAll,search,split}` 333/339 (98.2%); JS corpus 999/1,014; `make verify-regexp` 588/588 diff + 112/112 ES2025; `make diff-strings` 50,509/50,580 (71 diff, down from 95); ASan+UBSan clean) |
| **Phase 56** | **Cross-origin `Window` and `Location` reflection.** The row was not added when the phase merged; Phase 63 added it so that the table can be counted. The section below is the description. | ✅ Completed (see the section "Phase 56 Architecture") |
| **Phase 57** | **Lossy WebP — the `VP8` key frame, the `VP8X` container's `ALPH` plane, and the loop filter:** `Vp8Decoder` (`include/litebird/render/Vp8Decoder.hpp`, `src/render/Vp8Decoder.cpp`, tables in `Vp8Tables.cpp`) decodes a VP8 intra frame — the frame tag and `9d 01 2a` start code, the boolean arithmetic coder with its literal and signed-literal forms, one to eight token partitions, segmentation, the `[4][8][3][11]` coefficient probabilities and quantiser deltas, macroblock mode rows (16x16 luma, 8x8 chroma, and sixteen 4x4 modes through the `[10][10][9]` context), residuals with the eob / zero-run / ±1 triple, the inverse Walsh–Hadamard transform for Y2 and the 4x4 IDCT, the 16x16/8x8/4x4 intra predictors over a 16-sample border, the loop filter as a whole-frame pass in the bitstream's four-edge order, and fancy 9/3/3/1 chroma upsampling with libwebp's fixed-point BT.601 matrix — and `WebPDecoder` gains the lossy branch: a `VP8 ` chunk in a simple or `VP8X` file, an `ALPH` plane in either compression method and all three spatial filters (level-reduction refused as `UnsupportedAlpha`, reserved bits `Corrupt`), with `Status::UnsupportedLossy` retired. The write-up is in [`docs/phases/57-lossy-webp-vp8-key-frame.md`](docs/phases/57-lossy-webp-vp8-key-frame.md). | ✅ Completed (finding 63; 208/208 unit suites — `webp_lossy_vp8_decodes_bit_exactly`, `webp_lossy_alpha_plane_and_container_consistency`; `make verify-media` 63 → 88 checks, the 26 new ones all lossy and every one bit-exact against libwebp 1.2.4, plus the loop filter, partitions, segmentation and the alpha filters; `make fuzz-media` 2,400 cases with 14 new seeds, 0 findings; `make asan` green, `asan-deep` 882 runs 0 failed) |
| **Phase 59** | **Temporal dead zone for derived constructor `this` and `let`/`const`/`class` bindings:** adds `JsEnvironment::Binding::is_initialized` (zero `sizeof(Binding)` increase) and `AssignResult::Uninitialized`, enforces TDZ on derived `this` (`lookup_identifier("this")`, arrow captures, `super.m` / `super[computed]` before evaluating the key, and early `return this` before `super()`), and pre-declares lexical bindings in `Program`, `BlockStmt`, `SwitchStmt`, function/async/generator bodies, non-simple parameters, `for`/`for-of`/`for-in` heads (with `EnvScope` rooting), `try/catch`, and class `extends`/computed keys (while keeping `static` field initializers and `static {}` blocks after initialization). | ✅ Completed (201/201 unit suites; `make js-conformance` 1003/1017 Release / 1002/1017 ASan, `construct.dat` 49/50, `lexical.dat` 41/41; `make test262` 12,359/15,686 (+43, 0 regressions); 13 TDZ/scope dirs +49, 0 regressions; Release & ASan+UBSan green) |
| **Phase 60** | **Unicode 17.0 `String.prototype.normalize()`, full & locale case mapping (`tr`/`az`, `lt`, `el`), and CLDR 48 / UCA 17.0 `localeCompare()`:** generates compact static Unicode 17.0 / ICU 78.2 / CLDR 48 tables (`StringUnicodeTables.{hpp,cpp}` via `tools/generate_string_unicode_tables.py`, 293 KB `.o`: CCC, canonical/compatibility decompositions, composition pairs, QuickCheck/Cased/Case_Ignorable/Soft_Dotted/Variable properties, simple + `SpecialCasing` mappings, and CLDR 48 root collation CEs + contractions); implements UAX #15 `NFC`/`NFD`/`NFKC`/`NFKD` with Hangul Jamo and QuickCheck fast paths, full Unicode 17.0 case mapping (`Final_Sigma`, multi-code-point expansions, `tr`/`az`, `lt` `More_Above`/`After_I`, ICU 78.2 `el` `GreekUpper::toUpper` state machine), CLDR 48 UCA 17.0 root collation (S2.1 contiguous + discontiguous contractions, all 7 implicit weight groups, `sensitivity`, `caseFirst`, `numeric`, `ignorePunctuation`, `-u-kf`/`-u-kn`), ECMA-402 BCP-47/options validation, and fixes `this_string()` dangling-reference UB and ES String prototype metadata in `BuiltinsString.cpp`. | ✅ Completed (204/204 unit suites + 80 cross-origin checks; `make diff-strings` **50,580/50,580 (0 diffs)**, down from 71; `make verify-string-unicode` 12,255,232 CP×op + 1,112 strings + 1,038 pairs + 400,680 UCD `NormalizationTest` + 206,297 CLDR `CollationTest` = **0 diffs**; JS corpus 1,020/1,034 (17/17 `string_unicode.dat`); test262 9 String Unicode/locale suites 105→152/156 (97.4%), full `built-ins/String` 1,199/1,223 (98.0%); ASan+UBSan clean) |
| **Phase 63** | **Iteration asks the value for its iterator:** `for…of`, spread, `yield*`, `Array.from`, the `Map`, `Set`, `WeakMap` and `WeakSet` constructors, `Object.fromEntries`, the `Promise` combinators and the DOM's sequence arguments read an array's elements and a string's code points without asking for `Symbol.iterator`. `js_iterate` and `js_iterable_to_vector` ask now (GetMethod, one read), and keep the direct read where a check that runs no script (`js_iterates_as_builtin`) finds the function the realm installed. The loop is the language's: `next` read once, `done` before `value`, IteratorClose with its checks. An object with `next` and no `Symbol.iterator` is no longer iterable. | ✅ Completed (206/206 unit suites; JS corpus 1085/1112, `iteration.dat` 65/78 against 28 before; test262 10 more and 4 fewer of 47,967, the four being rows of the task table; 0.94 to 1.06 of `main`'s time over two runs) |
| **Phase 65** | **`eval` runs in the scope that called it:** a call written `eval(...)` whose identifier resolves to the global `eval` is a direct eval — it runs in the calling function's environment, shares its `this`, `arguments`, `new.target`, `super` and strictness, keeps its `let`/`const`/`class` to itself, and leaves its sloppy `var`s in the calling function; `(0, eval)`, an alias and a shadowed `eval` stay global code. The parser marks a function that calls `eval` with `uses_arguments`/`uses_new_target`/`uses_super`, so the bindings the text may name are hoisted even though the body does not mention them. The write-up is in [`docs/phases/65-direct-eval-scope.md`](docs/phases/65-direct-eval-scope.md). | ✅ Completed (finding 71; 210/210 unit suites — `js_direct_eval_runs_in_the_callers_scope`, count as merged with Phase 64; `make js-conformance` 1085/1112 → 1124/1150 with a new `eval.dat` of 38 cases, `construct.dat` 50/50; `--reference "node -e"` 1150/1150; `make wpt` 9550/9744 unchanged either way) |
| **Phase 72** | **The CSS Object Model:** `HTMLStyleElement.sheet` is a real `CSSStyleSheet` (`cssRules`/`rules`, `ownerNode`, `type`, `href`, `title`, `disabled`, `insertRule`, `deleteRule`), `document.styleSheets` answers those objects, and `CSSRuleList`/`CSSRule`/`CSSStyleRule` exist — `cssRules[i].selectorText`, `.style` (a `CSSStyleDeclaration` over the rule's own block) and `.cssText`. The model is the cascade's own `ParsedSheet`, so a rule a script inserts is a rule the next `getComputedStyle` applies; a rule object is made once per rule id and follows the rule when the list moves; the element's text is the sheet's rules, read again in place when it changes, and stays the same object through it. `document.fonts` exists too (`FontFaceSet.status`/`ready`/`size`), which is what two grid tests needed to run at all. The write-up is in [`docs/phases/72-cssom.md`](docs/phases/72-cssom.md). | ✅ Completed (finding 78; 224/224 unit suites — `cssom_sheets_rules_insert_amounts_and_fonts`; `make wpt` 9550/9744 → 9567/9824 with 0 unlisted failures, the two `document-fonts` rows gone and 65 new `grid-resolved-tracks` rows where a harness row ran no subtest; DOM corpus 678/678, JS corpus 1127/1152; `WPT_FLOOR` 9538 → 9567) |
| **Phase 69** | **An upgrade is an ordinary construction:** the definition keeps the HTML standard's construction stack, so `run_upgrade` constructs the class on an object of its own and `super(...)` (`construct_custom_element`) hands back the element — which is what `this` becomes — and the upgrade counts only if the constructor returns that element. A constructor that throws, never reaches the interface or returns something else leaves the element not `:defined` (a customized built-in included: `Element::is_defined`) and told nothing, with the prototype the interface gave it still on it. The call-context machinery that did this before (`JsValue`'s "constructions in place" list, a mutex, an atomic and a walk per request) is gone: measurement found it was no longer reached. | ✅ Completed (the whole unit suite — `js_an_upgrade_constructs_the_definition_the_ordinary_way` is the new one; DOM corpus 669/669 → 678/678 with a new `upgrades.dat` of 9 cases recorded from jsdom, `--reference` 678/678; js-conformance 1126/1152 and wpt 9550/9744 unchanged; the open probe `open/compiled-custom-elements.js` was 4 of 5 lines wrong and is cases now) |
| **Phase 88** | **Private fields, methods and accessors are identities, not properties:** a class mints its private names when it is evaluated — `JsObject::private_slots`, a table no string key reaches, keyed by `"\x01" + serial + ":" + name` — and declares them as bindings in its own scope, so a method resolves `#x` through the class it was written in and a subclass, another class, or a second evaluation of the same class expression cannot name the slot. `get_private`/`set_private`/`has_private`/`define_private` on `JsValue` hold the brand check: reading or writing a name an object does not carry is a `TypeError`, `#p in o` (`PrivateInExpr`) is the only way to ask and is a `TypeError` on a non-object, and a getter-only, setter-only or method member refuses the other direction. Instance methods and accessors are installed with the brand at construction (non-writable, one slot per getter/setter pair), static ones on the constructor, so `C.prototype.#m` and `A.get(B)` for a static private are `TypeError`s. The parser keeps the names of every enclosing class body and rejects an undeclared, duplicated, `delete`d or `super`'d private name, `#constructor`, and a private name outside a class body; a direct eval written in the class body is parsed with the class's names in scope. The write-up is in [`docs/phases/88-private-names-are-identities.md`](docs/phases/88-private-names-are-identities.md). | ✅ Completed (finding 94; `private_names_are_identities_and_not_strings` is the new suite and `make test` is 236/236; `make test262` over its 21 default directories goes 12,657 → 13,654 of 15,686 (80.7% → 87.0%), **973 pass and none lost**, and the three class directories alone 6,832 → 7,776 of 8,551; a 21-case battery run in Node.js 22 and in this engine prints the same output) |
| **Phase 79** | **Animated WebP — frames, a canvas, and the order they are drawn in:** `WebPDecoder::decode_frames()` walks a `VP8X` canvas's `ANIM` (background colour, loop count) and `ANMF` chunks — each a rectangle stored in two-pixel units, a duration, a blending bit and a disposal bit around a `VP8`/`VP8L` image, decoded by the still decoders already in the tree — and composites them the way libwebp does: a key-frame rule (the first frame, a full-canvas frame with no transparency or no blending, and a frame after one that cleared the whole canvas are drawn unblended), blending only over pixels an earlier frame left, split around the previous frame's cleared rectangle, with the straight-alpha fixed-point blend libwebp uses. `decode_frames()`, `first_frame()` and an `Animation`/`AnimationFrame` pair mirror `GifDecoder`; `decode()` and `decode_detailed()` are the first frame, so every `<img>` path gained animated WebP, and `Status::UnsupportedAnimation` is gone. Caps stay the still decoder's: `kMaxDimension` a side, `kMaxDecodedPixels` for the canvas, `kMaxAnimationPixels` and `max_frames` for the walk. The write-up is in [`docs/phases/79-animated-webp-frames-on-a-canvas.md`](docs/phases/79-animated-webp-frames-on-a-canvas.md). | ✅ Completed (finding 85; `webp_animated_frames_are_composited` is the new suite — a 244-byte four-frame fixture hashed frame by frame, plus five new refusals, with `make test` at 227/227 after the Phase 71 merge; `make verify-media` 95 → 103 checks, 16 of them animated and every frame bit-exact; `make fuzz-media` 2,400 cases with 4 new seeds, 0 findings; the decoder itself compared with libwebp 1.5's `WebPAnimDecoder` over **658 animations / 2,262 frames, every frame byte-identical**, and against `webpmux -info` field for field) |
| **Phase 81** | **A hole in an array is not an element:** an index of an array is now either an own property or a hole, as the standard has it. `delete a[1]` sets a hole instead of writing `undefined`; `new Array(3)` makes three holes; growing `length` leaves holes where it grew; writing a place — even `undefined` — makes it an element again. The holes are a bitmap (`JsObject::array_holes`, one `unique_ptr` per array that has one, so `JsObject` does not grow and an array without a hole allocates nothing new) beside the elements vector, which is unchanged and still holds a dead `undefined` in a hole's place, so an index costs what it did. Every built-in then asks the question the standard asks: `1 in a`, `hasOwnProperty`, `Object.keys`, `getOwnPropertyDescriptor`, `forEach`, `map`, `filter`, `some`, `every`, `reduce`, `find*` and `indexOf` no longer find a hole, while `includes`, `join`, `toString`, the iterator (and so spread, `for…of`, destructuring and `Array.from`), `slice`/`splice`/`concat`/`reverse`/`sort`/`copyWithin`/`flat` carry it into their answer and `JSON.stringify` writes it as `null`. The write-up is in [`docs/phases/81-array-holes.md`](docs/phases/81-array-holes.md). | ✅ Completed (finding 87; 235/235 unit suites — `array_holes_are_not_elements` asserts 60 facts, all compared in the page, and the same text run through Node.js 22.22.3 gives the same lines; js-conformance 1130/1152 → 1137/1156, the case being `arrays.dat`'s *delete leaves a hole*; DOM corpus 678/678, wpt 9567/9824 with 0 unlisted, URL 1188/1188 and XML 1718/1718 unchanged. ) |
| **Phase 96** | **A subclass keeps the holes `super()` made:** `class List extends Array {}` with `new List(3)` is three *holes*, as the standard has it, not three elements. The `Array` constructor made them correctly; `super(...)` hands a built-in parent's state to the object `new` created (`take_over` in `JsEval.cpp`), and that copy took the elements and everything else beside them and left `array_holes` behind, so `0 in l` was `true` and `Object.keys(l)` listed all three. One copy fixes the `super(...)` path and `Reflect.construct(Array, [3], List)`, which goes through the same function; a probe of `Map`, `Set`, `WeakMap`, `Error`, `Date`, `RegExp` and `Promise` subclasses found nothing else. The write-up is in [`docs/phases/96-a-subclass-keeps-the-holes.md`](docs/phases/96-a-subclass-keeps-the-holes.md). | ✅ Completed (finding 102; the eleven assertions are in `array_holes_are_not_elements`; 259/259 unit suites, js-conformance 1137/1156, DOM 678/678, wpt 9567/9824 with 0 unlisted, URL 1188/1188 and XML 1718/1718 unchanged. With the line reverted the test fails and names the assertion. `Symbol.species` — the other half of the same AGENTS.md row, `list.slice()` is a plain array — is still open and is not this change. ) |
| **Phase 98** | **A hole is a place the prototype can fill:** `find`, `findIndex`, `findLast` and `findLastIndex` call the predicate for every index, holes included, as the standard does, where Phase 81 had them skip one; and the eight methods that ask `HasProperty` (`every`, `some`, `map`, `filter`, `reduce`, `reduceRight`, `indexOf`, `lastIndexOf`) now see an index `Array.prototype` has. Reaching that took the layer under them: `lookup_chain`, `has_property` and the array branch of `get_property` read an array prototype's `array_elements`, where the indices live, instead of only its property map; `eval_index`'s `array[number]` fast path no longer answers a hole from the elements; and `Object.defineProperty` and `delete` on an *index* of an array no longer lose a data property redefined over an accessor or leave a hole where no element was. The write-up is in [`docs/phases/98-a-hole-is-a-place-the-prototype-can-fill.md`](docs/phases/98-a-hole-is-a-place-the-prototype-can-fill.md). | ✅ Completed (finding 104; the 12 test262 tests this row was written from pass, run one at a time; `built-ins/Array` and `built-ins/Object` 4,952 → 5,010 passing; 261/261 unit suites with `a_hole_is_a_place_the_prototype_can_fill` asserting 33 facts; js-conformance 1137/1156, DOM 678/678, wpt 9567/9824 with 0 unlisted, URL 1188/1188 and XML 1718/1718 unchanged. An accessor on an element's own index is still not used, and `length` is still not inherited from an array prototype: both named in AGENTS.md. ) |
| **Phase 108** | **An accessor on an array index is the index's property:** `Object.defineProperty(a, "1", { get() { … } })` put the accessor in the property map, where an array did not look for an index, so the getter and the setter were never called; on a hole the index was not even an own property, which is the form of the last four of the 25 test262 tests Phase 81 lost. `JsValue::define_accessor` now makes the place a hole, and a hole is where the map is asked, by `has_own_property`, by a write, by the descriptor and by the list of keys; a shorter `length` takes the mapped indices it cuts and stops at one that is not configurable; and `join`, `includes`, `at`, `pop`, `shift`, the iterator, `JSON.stringify` and the methods that copy read a place as `a[i]` does (`js_array_get`), which makes them see an index `Array.prototype` has as well. The length is taken once where a getter can now change it, `with` and `toSpliced` do not read what they replace or drop, and whether a hole can read as anything is remembered between holes, so that a hole does not cost a lookup by name. The write-up is in [`docs/phases/108-an-accessor-on-an-array-index.md`](docs/phases/108-an-accessor-on-an-array-index.md). | ✅ Completed (finding 114; the 25 tests pass, each run alone; the whole of test262 31,366 → 31,643 passing of 47,967, 277 gained and none lost, `main` at `01bbf07` against the same tree with this change; 269/269 unit suites with `an_accessor_on_an_array_index_is_its_property` holding 58 cases whose answers Node.js printed; js-conformance and DOM conformance unchanged. The methods that move elements in place do not go through an accessor, array destructuring reads the elements, a data property on an index keeps no attributes, and `length` is still not inherited from an array prototype: all named in AGENTS.md.) |

---

## Known gaps (the honest list)

1. **Layout is still behind parsing.** Floats, block formatting contexts and
   basic table layout landed in Phase 16; Phase 18 added percentage lengths and
   real `overflow` clipping; Phase 29 added table spans (`rowspan`/`colspan`,
   including `rowspan="0"`), `border-spacing` and an approximate
   `border-collapse: collapse` (cells paint half of a shared border rather
   than resolving the CSS 2.1 "widest wins" rule edge by edge); Phase 36 added
   block-in-inline splitting (`<a>x<p>y</p></a>`), `inline-block` shrink-to-fit,
   form-control `InlineBlock` boxes, and on-demand script reflow. Still missing:
   multicol, `direction` and `unicode-bidi` (the bidi algorithm itself runs, and
   reorders each line — Phase 109), complex font shaping/HarfBuzz, the table's
   outer border merge in
   the collapsed model, `empty-cells`, `caption-side`, `table-layout: fixed`,
   per-element scrollable `overflow: auto` containers (the viewport scrolls, inner
   boxes clip), and rounded-corner clipping. The DOM is trustworthy; the box tree
   handles everyday layouts but not full CSS2.2/CSS3 edge cases.
2. **JavaScript is interpreted, and catastrophic-backtracking limits remain.**
   Scripts run on a tree-walking interpreter: 50 to 100 times slower than V8
   on loops and calls. Long strings are no longer quadratic (Phase 24), and
   neither are deleting many keys from one object or emptying a long array
   with `shift()` (Phase 30). In regular expressions, Phase 37 (finding 42)
   replaced the recursive CPS matcher with an iterative bytecode VM on an
   explicit heap backtracking stack, eliminating native stack exhaustion on
   repeated groups (`(?:a|b)+` on 120,000 characters succeeds cleanly); an
   intentional `kMaxMatchSteps = 30,000,000` step guard still bounds
   catastrophic backtracking with `RangeError` (AUDIT.md, finding 26 & 42).
3. **Single process, no sandbox.** Chrome's original headline feature was
   process-per-tab isolation. Untrusted input is parsed in-process. Page-initiated
   `fetch()`, `XMLHttpRequest`, `Worker`, stylesheet, script, and `<img>` loads
   enforce the document-origin boundary so web pages cannot read local files
   (Phases 33 and 36), whereas local `file:` documents retain local relative
   access by design.
4. **Memory is only reclaimed on request.** The collector is sound (Phase 23,
   section 5) but runs only when a script calls `gc()` or the shell's heap
   view is opened; a long-running page grows until then. Leak detection is
   switched off in `make asan` and no leak audit has been done since the
   engine was rebuilt. One leak is known: the collector cannot see into what
   a native function captured, so `o.b = f.bind(o)` is never freed, and
   neither is what a pending timer holds (Phase 34, section 9).
5. **Percentage lengths are resolved in layout, not the cascade** (the cascade
   has no containing block to resolve against) -- that is now correct for box
   dimensions, margins, padding, percentage `font-size`, percentage/unitless
   `line-height` (Phase 36), and scalar percentage `border-radius` (Phase 37);
   two-axis elliptical `border-radius` (`50% / 25%`) remains deferred.
6. **All 1,792 cases of the vendored html5lib corpus now pass** (`100.0%`
   after Phase 49 fixed the 11 `<select>` / `<selectedcontent>` cases in
   `webkit02.dat`, `tests1.dat`, `tests_innerHTML_1.dat`, and `tests7.dat`).
   The corpus is not the last word: the vendored copy is the last one
   html5lib-tests published before its tree-construction tests moved to
   web-platform-tests (June 2026). That copy has 1,973 cases now and
   Litebird passed 1,868 of them before Phase 49 (leaving 88 that need
   processing instructions, which the standard's parser now makes and
   Litebird's does not — though the node type exists since Phase 28 — and 6
   that need a script to run while the page is parsed). And a corpus does not
   hold everything: `<br/>` closing its parent had no case in it (Phase 26).
   `make diff-trees` compares random pages with parse5 for that reason.
7. **Conformance is not rendering.** 100.0% of the vendored parser corpus is a
   DOM guarantee, not a promise that real pages look right.
8. **TrueType `.ttf` rendering has no `@font-face` or HarfBuzz shaping.** When a
   system `.ttf` font is present, `FontRenderer` rasterizes anti-aliased UTF-8
   glyphs at the computed `font-size`; when no `.ttf` file is available on the
   host, rendering falls back to the unscaled 8×8 bitmap font. Webfont
   downloads (`@font-face`, WOFF/WOFF2) and complex-script shaping are not
   implemented; text is reordered by UAX #9 (Phase 109) but Arabic letters are
   drawn unjoined, which is shaping and not bidi.
9. **The DOM a script sees is measured against jsdom and a part of
   web-platform-tests, plus targeted end-to-end integration tests.**
   It passes all 678 cases of its corpus (Phases 27, 28, 34, 37, 43 and 69) and 7,968 of
   the 8,009 subtests of `dom/nodes`, `domparsing` and `encoding` across 103
   files; the corpus can only ask what jsdom can answer and what somebody
   thought to ask, and the rest of web-platform-tests has not been run.
   Phase 36 added on-demand layout
   (`getBoundingClientRect()`, `offset*`, `client*`), external classic/deferred
   `<script src>` and `<link rel="stylesheet">` + `@import`, CLI/shell XML & SVG
   document loading, and HTTP/HTTPS + CORS `fetch()` and `XMLHttpRequest`; Phase 37
   added `el.style` shorthand expansion/aliases and HTML5 date/time `<input>`
   semantics; Phase 42 added nested browsing contexts (`<iframe>`
   `contentDocument`/`contentWindow`/`srcdoc`/`sandbox`, cross-origin
   `WindowProxy` checks, `postMessage`, subdocument layout/rasterization, and
   `.xhtml` script execution); Phase 47 added the WHATWG Encoding Standard's 40
   decoders and 228 labels for pages, XML and subresources. Still missing:
   `getComputedStyle(el).width` layout metrics, `document.write`, SVG DOM
   (`className.baseVal`, `viewBox`, `getBBox()`), CORS preflight `OPTIONS`, and
   `TextDecoder`/`TextEncoder` (the encodings themselves are read since Phase
   47). The style sheet objects arrived in Phase 72 (`style.sheet`,
   `document.styleSheets`, `cssRules`, `insertRule`, `document.fonts`). Absent: `crypto`, `indexedDB`,
   `MessageChannel`, XPath, Web Animations, `ElementInternals`, and what needs
   typed arrays (`ArrayBuffer`). The full lists are in Phase 27, section 12,
   Phase 28, section 10, Phase 36, section 9, and Phase 42.
10. **JavaScript features that do not exist:** BigInt, `Proxy`, typed arrays,
   modules, `with`, `Intl`. Holes in arrays arrived in Phase 81 (finding 87): an
   index is an own property or a hole, and every built-in agrees — `delete`,
   `new Array(n)`, a grown `length` and an elision in a literal all make holes.
   Details and the approximate
   corners are in Phase 23, section 10. Around classes: there is
   no `Symbol.species` and `instanceof` does not ask `Symbol.hasInstance`,
   nothing tells a built-in that is a constructor from one that is not
   (Phase 34, section 9), `this` can be used before `super()` has run, `let`
   and `const` have no dead zone, `eval` inside a function is global code,
   and what a private name is, and is not, is settled: since Phase 88 it is
   an identity in a table of its own, which no string key reaches (Phase 43,
   section 8 had it as a property called `#p`). Of 15,686 test262 tests on
   functions, classes and objects, 13,654 pass (`make test262`; 10,835 before
   Phase 44, 12,657 before Phase 88); of
   the 47,967 in all of its `built-ins`, `language` and `annexB`, 31,158.
11. **A page can still be slow by being long.** Depth no longer ends the
   process (Phase 25): the parser stops nesting at 512, nothing that walks
   the DOM recurses, and layout stops when the stack is nearly used up, which
   means a tree a script built deeper than that is laid out only so far.
   Length is bounded for what `make deep` generates, not in general.
   Sized floats in one box are placed by a band-edge sweep: a float lands
   on the exact bottom edge of the float in the way, so 10,000 of them run
   in seconds and none run out of the box (AUDIT.md, finding 28); removing
   the first child of 50,000, one at a time, takes a second. The CSS whitespace cost
   was removed in Phase 31. At the nesting limit, misnested formatting tags
   can leave content out of its written order. Measurements are in AUDIT.md,
   finding 28.

---
## Strict-mode writes that a property refuses (the old `strict-write` rows)

The gap this file documented as open ("a write that a property refuses is refused
in silence", 2 subtests, `strict-write`) is closed. No open change claimed
`JsEval.cpp`; the note was stale.

`JsExotic::set` answered only "was this dealt with?", so a collection that
refuses an index write had no way to say "refused", and strict-mode assignments
could not throw. `JsExotic` now answers with a tri-state
`SetResult::{NotHandled, Handled, Refused}`: `NotHandled` leaves the ordinary
rules to the engine, `Refused` is an object that does not take the write
(a getter-only indexed property of a collection), and strict-mode assignments
throw a `TypeError` for it, as `[[Set]]`'s false requires. `JsValue::set_property`
is a `bool` for the same reason — every refusal path (a primitive receiver, a
non-writable or frozen property, an accessor with no setter, a non-extensible
cell, a wrapped string's indices) reports `false`, and the evaluator's write
reference throws in strict mode when it is told `false`. Sloppy mode keeps the
silent refusal, as the spec does.

The eight exotic `set` implementations in `Dom*Bindings.cpp` were re-tagged to
the tri-state. Three changes in behaviour beyond the bookkeeping:

- An assignment to a read-only-refused index of `HTMLCollection`,
  `DOMTokenList`, `NamedNodeMap` (and the getter-only index of a
  `CSSStyleDeclaration`) throws a `TypeError` in strict mode — the two failing
  subtests and their siblings now pass.
- A strict-mode write to a primitive's property, an accessor with no setter
  (own or inherited) and a non-writable property anywhere else in the engine
  throws as well, which is what `[[Set]]`'s false means there too.
- `assign_identifier`'s global fall-through checks the same boolean.

WPT: `document`/`Element-getElementsByTagName.html` strict-mode subtests pass
and are off the list; the census stays truthful (0 unlisted).

## Phase write-ups

One file per phase, in `docs/phases/`. They are not here: this document
describes the engine **as it is**, and a log of work appended to it is why
it reached 435 KB. A new write-up goes in `docs/phases/` (see the notice at
the top of `AGENTS.md`).

- **Phase 10** — [8. Phase 10 Architectural Implementation Details](docs/phases/10-architectural-implementation-details.md)
- **Phase 11** — [9. Phase 11 Architectural Implementation Details](docs/phases/11-architectural-implementation-details.md)
- **Phase 12** — [Phase 12 Architecture](docs/phases/12-architecture.md)
- **Phase 13** — [Phase 13 Architecture: Modern DOM & Web Components](docs/phases/13-architecture-modern-dom-web-components.md)
- **Phase 14** — [Phase 14 Architecture: Modern Browser Capabilities & Advanced Engine Platform](docs/phases/14-architecture-modern-browser-capabilities-advanced-engine.md)
- **Phase 15** — [Phase 15 Architecture: Spec Fidelity & The Scoreboard](docs/phases/15-architecture-spec-fidelity-the-scoreboard.md)
- **Phase 16** — [Phase 16 Architecture: Floats, Block Formatting Contexts & Table Layout](docs/phases/16-architecture-floats-block-formatting-contexts-table.md)
- **Phase 17** — [Phase 17 Architecture: Conformance Push](docs/phases/17-architecture-conformance-push.md)
- **Phase 18** — [Phase 18 Architecture: Percentages and Clipping](docs/phases/18-architecture-percentages-and-clipping.md)
- **Phase 23** — [Phase 23 Architecture: The JavaScript Engine, Rebuilt](docs/phases/23-architecture-the-javascript-engine-rebuilt.md)
- **Phase 24** — [Phase 24 Architecture: Strings That Cost What They Read](docs/phases/24-architecture-strings-that-cost-what-they-read.md)
- **Phase 25** — [Phase 25 Architecture: Trees of Any Depth](docs/phases/25-architecture-trees-of-any-depth.md)
- **Phase 26** — [Phase 26 Architecture: The Tree Builder Against Pages Nobody Wrote](docs/phases/26-architecture-the-tree-builder-against-pages-nobody-wrote.md)
- **Phase 27** — [Phase 27 Architecture: The DOM a Script Sees](docs/phases/27-architecture-the-dom-a-script-sees.md)
- **Phase 28** — [Phase 28 Architecture: XML](docs/phases/28-architecture-xml.md)
- **Phase 29** — [Phase 29 Architecture: Table Spans and Collapsed Borders](docs/phases/29-architecture-table-spans-and-collapsed-borders.md)
- **Phase 30** — [Phase 30 Architecture: Deletion and Front Edits Are Linear](docs/phases/30-architecture-deletion-and-front-edits-are-linear.md)
- **Phase 31** — [Phase 31 Architecture: CSS Whitespace Trimming Is Linear](docs/phases/31-architecture-css-whitespace-trimming-is-linear.md)
- **Phase 32** — [Phase 32 Architecture: The CSS Color Keywords, Once](docs/phases/32-architecture-the-css-color-keywords-once.md)
- **Phase 33** — [Phase 33 Architecture: Page Resource Loads Are Origin-Aware](docs/phases/33-architecture-page-resource-loads-are-origin-aware.md)
- **Phase 34** — [Phase 34 Architecture: A Function Can Be a Prototype](docs/phases/34-architecture-a-function-can-be-a-prototype.md)
- **Phase 35** — [Phase 35 Architecture: Attribute-List End Removals Are Linear](docs/phases/35-architecture-attribute-list-end-removals-are-linear.md)
- **Phase 36** — [Phase 36 Architecture: Usable Browser Pipeline, HTTPS/HTTP1.1/CORS, Subresources, Live Reflow, TrueType & Web GUI](docs/phases/36-architecture-usable-browser-pipeline-https-http1-1-cors.md)
- **Phase 37** — [Phase 37 Architecture: Iterative RegExp VM, Modern CSS Cascade & Units, CSSOM `el.style`, and HTML5 Date/Time Inputs](docs/phases/37-architecture-iterative-regexp-vm-modern-css-cascade.md)
- **Phase 38** — [Phase 38 Architecture: Real-Web JPEG/GIF Decoding, SVG & Canvas 2D](docs/phases/38-architecture-real-web-jpeg-gif-decoding-svg-canvas-2d.md)
- **Phase 39** — [Phase 39 Architecture: The Tokenizer's Repeated Attribute Names Are Linear](docs/phases/39-architecture-the-tokenizer-s-repeated-attribute-names.md)
- **Phase 40** — [Phase 40 Architecture: Worker MIME/Module Policy, EventTarget Dispatch & Lifecycle Safety](docs/phases/40-architecture-worker-mime-module-policy-eventtarget.md)
- **Phase 41** — [Phase 41 Architecture: A String Built Half a Surrogate Pair at a Time Is Linear](docs/phases/41-architecture-a-string-built-half-a-surrogate-pair-at-a.md)
- **Phase 42** — [Phase 42 — Nested Browsing Contexts (`<iframe>`), Cross-Realm DOM Wrappers, Origin Isolation, and `.xhtml` Execution](docs/phases/42-nested-browsing-contexts-iframe-cross-realm-dom-wrappers.md)
- **Phase 43** — [Phase 43 Architecture: `new.target`, and What a Constructor Makes](docs/phases/43-architecture-new-target-and-what-a-constructor-makes.md)
- **Phase 44** — [Phase 44 Architecture: Unicode 17.0 Identifiers, `\u` Identifier Escapes & Escaped Keyword Semantics](docs/phases/44-architecture-unicode-17-0-identifiers-u-identifier.md)
- **Phase 45** — [Phase 45 Architecture: Resolve a Promise Iterator One Item at a Time](docs/phases/45-architecture-resolve-a-promise-iterator-one-item-at-a.md)
- **Phase 46** — [Phase 46 Architecture: Canvas 2D Text — `fillText`, `strokeText`, `measureText`](docs/phases/46-architecture-canvas-2d-text-filltext-stroketext.md)
- **Phase 47** — [Phase 47 Architecture: The WHATWG Encoding Standard, the `<meta charset>` Prescan, and XML/WPT Encodings](docs/phases/47-architecture-the-whatwg-encoding-standard-the-meta.md)
- **Phase 48** — [Phase 48 Architecture: Sized-Float Placement Index](docs/phases/48-architecture-sized-float-placement-index.md)
- **Phase 49** — [Phase 49 Architecture: Customizable `<select>` Content, Scope Boundaries, and `<selectedcontent>` Cloning](docs/phases/49-architecture-customizable-select-content-scope.md)
- **Phase 50** — [Phase 50 Architecture: CSS Color 4/5 (`oklch`/`oklab`/`lab`/`lch`/`hwb`/`color()`, `color-mix()`, relative colors, `light-dark()`)](docs/phases/50-architecture-css-color-4-5-oklch-oklab-lab-lch-hwb-color.md)
- **Phase 51** — [Phase 51 Architecture: The Attribute Model — Namespaces, `Attr` Objects, and Named Properties](docs/phases/51-architecture-the-attribute-model-namespaces-attr-objects.md)
- **Phase 52** — [Phase 52 Architecture: Repairing `main` After the Phase 49 and Phase 51 Merges](docs/phases/52-architecture-repairing-main-after-the-phase-49-and-phase.md)
- **Phase 53** — [Phase 53 Architecture: UTF-16 Storage for Non-ASCII `JsString` (Lazy UTF-8 & Tail Rewind)](docs/phases/53-architecture-utf-16-storage-for-non-ascii-jsstring-lazy.md)
- **Phase 54** — [Phase 54 Architecture: Lossless WebP — the Container and the `VP8L` Bitstream](docs/phases/54-architecture-lossless-webp-the-container-and-the-vp8l.md)
- **Phase 55** — [Phase 55 Architecture: Complete RegExp Flags (`d`/`u`/`v`), Unicode 17.0 Property Escapes & Case Folding, and ES2025 RegExp Features (Finding 61)](docs/phases/55-architecture-complete-regexp-flags-d-u-v-unicode-17-0.md)
- **Phase 56** — [Phase 56 Architecture: Cross-Origin `Window` and `Location` Reflection](docs/phases/56-architecture-cross-origin-window-and-location-reflection.md)
- **Phase 59** — [Phase 59 Architecture: Temporal Dead Zone for Derived `this` and Lexical Bindings (Finding 65)](docs/phases/59-architecture-temporal-dead-zone-for-derived-this-and.md)
- **Phase 60** — [Phase 60 Architecture: Unicode 17.0 String Normalization, Full & Locale Case Mapping, and CLDR 48 UCA Collation (Finding 66)](docs/phases/60-architecture-unicode-17-0-string-normalization-full.md)
- **Phase 63** — [Phase 63 Architecture: Iteration Asks the Value for Its Iterator](docs/phases/63-architecture-iteration-asks-the-value-for-its-iterator.md)
- **Phase 77** — [Async-generator delegation and return resumption](docs/phases/77-async-generator-yield-star.md)
- **Phase 83** — [WebSocket handshake security](docs/phases/83-websocket-handshake-security.md)
