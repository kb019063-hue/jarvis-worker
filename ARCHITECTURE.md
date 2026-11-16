# 🦅 Litebird Browser Engine — Comprehensive Architecture & Contributor Guide

> **Notice for Future AI Agents & Human Developers:**
> This document is designed as a self-contained reference and onboarding manual. It details the internal design, subsystem contracts, memory lifecycle, data flows, and expansion playbooks across the entire codebase. When implementing new features or debugging, consult this guide first.

---

## 📑 Table of Contents

1. [High-Level Philosophy & Constraints](#1-high-level-philosophy--constraints)
2. [Repository Layout & Module Structure](#2-repository-layout--module-structure)
3. [End-to-End Pipeline & Data Flow](#3-end-to-end-pipeline--data-flow)
4. [Subsystem Deep Dives](#4-subsystem-deep-dives)
   - [4.1 DOM Core & Event Dispatch](#41-dom-core--event-dispatch)
   - [4.2 HTML5 Tokenizer & Parser](#42-html5-tokenizer--parser)
   - [4.3 CSS Parser, Cascade & Media Queries](#43-css-parser-cascade--media-queries)
   - [4.4 Layout Box Tree & Formatting Contexts](#44-layout-box-tree--formatting-contexts)
   - [4.5 Display List & Multi-Backend Rendering](#45-display-list--multi-backend-rendering)
   - [4.6 JavaScript Engine, Bytecode VM & GC](#46-javascript-engine-bytecode-vm--gc)
   - [4.7 Web Storage & Cookie Engine](#47-web-storage--cookie-engine)
   - [4.8 Canvas 2D Rasterizer](#48-canvas-2d-rasterizer)
   - [4.9 Network, EventLoop & TUI Shell](#49-network-eventloop--tui-shell)
   - [4.10 SVG Vector Graphics Engine](#410-svg-vector-graphics-engine)
   - [4.11 CSS Transitions & Keyframe Animations](#411-css-transitions--keyframe-animations)
   - [4.12 Advanced CSS Pseudo-Classes & Selectors](#412-advanced-css-pseudo-classes--selectors)
   - [4.13 ES6 Collections & Language Expansion](#413-es6-collections--language-expansion)
5. [Critical Memory & ABI Safety Rules](#5-critical-memory--abi-safety-rules)
6. [Extension Playbooks (How to Add Features)](#6-extension-playbooks-how-to-add-features)
   - [Adding a New DOM API / Method](#adding-a-new-dom-api--method)
   - [Adding a New CSS Property](#adding-a-new-css-property)
   - [Adding a New JavaScript Bytecode Opcode](#adding-a-new-javascript-bytecode-opcode)
   - [Adding a Test Suite Entry](#adding-a-test-suite-entry)
7. [Current Phase Status & Roadmap](#7-current-phase-status--roadmap)
8. [Phase 10 Architectural Implementation Details](#8-phase-10-architectural-implementation-details)
9. [Phase 11 Architectural Implementation Details](#9-phase-11-architectural-implementation-details)
10. [Phase 12 Architecture](#phase-12-architecture)
11. [Phase 13 Architecture: Modern DOM & Web Components](#phase-13-architecture-modern-dom--web-components)
12. [Phase 14 Architecture: Modern Browser Capabilities & Advanced Engine Platform](#phase-14-architecture-modern-browser-capabilities--advanced-engine-platform)

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
| **Phase 38** | **Real-web JPEG/GIF decoders, upgraded Canvas 2D & SVG vector renderer:** standalone Baseline/Progressive JPEG and GIF87a/89a decoders verified pixel-by-pixel against `libjpeg` and ImageMagick, plus CTM-aware Canvas 2D and SVG paths/arcs/gradients/`<use>`/`<clipPath>`. | ✅ Completed (139/139 unit suites; `make verify-media` 35/35; `make fuzz-media` 1,800 release / 4,500 ASan) |
| **Phase 39** | **The tokenizer's repeated attribute names are linear:** the HTML tokenizer dropped a repeated attribute name by comparing it with every attribute already accepted, which is the square of the count. A tag with 32 or more accepted attributes now keeps a hash set of views of their names, rebuilt when the attribute vector grows under it; shorter tags keep the scan. One element with 50,000 attributes went from 3,474 ms to 93 ms whole-process, an end tag with that many from 3,170 ms to 25 ms, and many small tags did not change. First-wins, ASCII case folding, order and every value spelling are unchanged. | ✅ Completed (159/159 unit suites; `make bench-tokenizer-attributes` 7 cases, 0 failures) |
| **Phase 40** | **Worker MIME/module policy, EventTarget dispatch, and shutdown crash fix:** enforces the 16 WHATWG JavaScript MIME types and HTTP 2xx status on `Worker` scripts and `importScripts(...)`, parses `WorkerOptions` (`{type, name}`), rejects `{type:"module"}` cleanly with an error event instead of silently running classic JS, dispatches both `onerror`/`onmessage` and `addEventListener`/`removeEventListener` (`once`, `handleEvent`, `preventDefault`, `stopImmediatePropagation`) on page and worker scopes, supports nested worker wake/teardown, breaks the `inst` ↔ `WorkerObject` leak cycle, and joins active worker threads cleanly before static destruction (fixing the exit-time `SIGSEGV` / `heap-use-after-free` crash). | ✅ Completed (157/157 unit suites; `make security-worker` 17/17 checks, 200/200 4-worker startup/exit runs, 0 crashes in Release & ASan+UBSan) |
| **Phase 43** | **`new.target`, and what a constructor makes:** `new.target` is the function `new` was applied to (through `super(...)`, `Reflect.construct(F, args, G)` and a bound function), an object a parent's constructor returns becomes `this` in the child, `this` is bound once (a second `super()`, none, and a returned primitive throw), `Reflect.construct` of a DOM interface with a `new.target` makes the object, and a custom element compiled for older browsers is made and upgraded (the standard's construction stack, kept beside the call context). | ✅ Completed (168/168 unit suites; JS corpus 955/970, `construct.dat` 47/49 against 13 before; DOM corpus 663/663, `compiled.dat` 23/23 against 2 before; test262 @@T262_ALL_GAINED@@ more and @@T262_ALL_LOST_WORD@@ fewer of @@T262_ALL_TOTAL@@) |

---

## 8. Phase 10 Architectural Implementation Details

### Pillar 1: CSS 2D Transforms & Affine Matrix Engine
- **Data Structures (`include/litebird/common/Geometry.hpp`):**
  - `TransformMatrix2D`: Standard 3x3 homogeneous affine matrix representation:
    $$M = \begin{bmatrix} a & c & e \\ b & d & f \\ 0 & 0 & 1 \end{bmatrix}$$
    with matrix multiplication, determinant calculation, analytical inversion (`inverse()`), point mapping (`map_point(Point)`), and axis-aligned bounding box transformation (`map_rect(Rect)`).
  - Generators for elementary transformations: `translation(tx, ty)`, `scale(sx, sy)`, `rotation(radians)`, `skew(rx_rad, ry_rad)`.
- **Style Resolution (`src/css/StyleResolver.cpp`):**
  - Parser for CSS `transform` declarations chaining `translate(x, y)`, `rotate(deg/rad)`, `scale(x, y)`, `skew(x, y)`.
  - Support for `transform-origin` percentages and pixel lengths (defaulting to 50% 50% box center). The resolved matrix incorporates origin offsets: $M_{\text{final}} = T_{\text{origin}} \times M \times T_{-\text{origin}}$.
- **Display List & Renderers:**
  - `PushTransformCommand` and `PopTransformCommand` wrap transformed element draw sequences in `DisplayList::build_recursive`.
  - `TerminalRenderer` and `BitmapRenderer` maintain transform stacks applying affine mappings to coordinates and dimensions during drawing.
- **Hit-Testing (`src/shell/BrowserShell.cpp`):**
  - `element_at(x, y)` computes the inverse matrix $M^{-1}$ of any transformed layout box, transforming cursor hit coordinates into local untransformed box space for exact hit-testing.

### Pillar 2: Canvas 2D Gradients & Bézier Curves
- **Gradient Engine (`include/litebird/render/Canvas2D.hpp`, `src/render/Canvas2D.cpp`):**
  - `CanvasGradient` class supporting both `Linear` and `Radial` gradient types.
  - Color stop list storing normalized offsets `[0.0, 1.0]` and parsed `Color` values.
  - Linear sampling projects query $(x, y)$ along the baseline $(x_0, y_0) \to (x_1, y_1)$ using vector dot product:
    $$t = \frac{(x - x_0)dx + (y - y_0)dy}{dx^2 + dy^2}$$
  - Radial sampling maps radial distance between inner circle $(x_0, y_0, r_0)$ and outer circle $(x_1, y_1, r_1)$.
  - Color interpolation performs linear blending of ARGB components between bounding color stops.
- **Bézier Curves:**
  - `quadratic_curve_to(cpx, cpy, x, y)` and `bezier_curve_to(cp1x, cp1y, cp2x, cp2y, x, y)` approximate smooth parametric curves using incremental line segments added directly to current path subpaths.
- **JavaScript Bridge:**
  - `createLinearGradient(x0, y0, x1, y1)`, `createRadialGradient(x0, y0, r0, x1, y1, r1)`, and `addColorStop(offset, color)` JS bindings.
  - Setting `fillStyle` or `strokeStyle` accepts gradient objects, dispatching gradient fill and stroke during `fill_rect`, `stroke_rect`, and `fill()`.

### Pillar 3: ES6 Classes & `instanceof` Operator
- **Syntax Parsing (`src/js/JsParser.cpp`):**
  - Lexer tokens: `JsTokenType::Class`, `Extends`, `Super`, `Static`, `InstanceOf`.
  - AST Nodes: `ClassDeclStmt`, `MethodDefinition`, `SuperExpr`.
  - Parses class declarations with optional `extends` superclass, constructor resolution, static methods, and instance methods.
- **Evaluation & Runtime Semantics (`src/js/JsInterpreter.cpp`):**
  - `ClassDeclStmt` allocates prototype object `proto_obj` linked to `super_ctor.prototype` if extending.
  - Constructor function `ctor_fn` is bound with prototype and `__super_constructor__`.
  - `NewExpr` allocates an object cell via `Heap::default_heap().allocate_object()`, links `instance.prototype = ctor.prototype`, binds `"this"` and `"__super_constructor__"`, and executes constructor body.
  - `SuperExpr` dispatches `super(...)` constructor calls and `super.method(...)` parent prototype method invocations with `"this"` preserved.
  - Binary operator `JsTokenType::InstanceOf` walks the prototype chain of the left operand (`left.prototype -> curr->prototype`) checking for pointer equality with `right.prototype`.

### Pillar 4: `async` / `await` Syntax Support
- **Syntax Parsing (`src/js/JsParser.cpp`):**
  - Tokens: `JsTokenType::Async`, `JsTokenType::Await`.
  - Parser handles `async function` declarations, async function expressions, async arrow functions (`async () => ...`), and unary `await` expressions (`AwaitExpr`).
- **Runtime & Microtask Scheduling (`src/js/JsInterpreter.cpp`, `src/js/Promise.cpp`):**
  - Async functions return a `Promise` instance encapsulating `PromiseData`. Normal execution or return statements resolve the promise; uncaught exceptions reject the promise.
  - `AwaitExpr` evaluates its operand; if it yields a `Promise`, it synchronously ticks the engine `EventLoop` until fulfillment (returning the unwrapped value) or rejection (throwing a JavaScript runtime error).

---

## 9. Phase 11 Architectural Implementation Details

### Pillar 1: CSS Variables & Custom Property Cascade (`var()`)
- **Data Representation (`include/litebird/css/StyleResolver.hpp`):**
  - `ComputedStyle` maintains `custom_properties`: a map of string property names (`--*`) to their string values.
  - Inheritance: Custom properties inherit down the DOM tree by default. In `StyleResolver::resolve()`, child element styles copy their parent's `custom_properties` before matching rules are applied.
- **Substitution Engine (`src/css/StyleResolver.cpp`):**
  - `StyleResolver::resolve_var_functions(val_str, computed_style)` scans declarations for `var(--name, fallback)`.
  - Nested comma handling balances parentheses to extract optional fallback expressions.
  - Recursively resolves variable chains and fallback expressions; unrecognized variables without fallbacks yield empty values.

### Pillar 2: CSS `calc()` Math Expression Engine
- **Recursive Descent Parser (`src/css/StyleResolver.cpp`):**
  - `CalcLexer`: Lexes tokens for numbers, pixel lengths (`px`), percentages (`%`), operators (`+`, `-`, `*`, `/`), and parentheses `(` / `)`.
  - Operator Precedence Grammar:
    - Addition/Subtraction: `parse_additive()`
    - Multiplication/Division: `parse_multiplicative()`
    - Primaries & Subexpressions: `parse_primary()` handles parenthesized sub-expressions, lengths, percentages, and pure scalar numbers.
- **Unit and Reference Resolution:**
  - `evaluate_calc_expression(expr, base_ref, style)` resolves lengths to absolute pixels using reference bases (e.g., container width/height or font size for percentages).
  - Integrates seamlessly with `resolve_var_functions()` so expressions like `calc(var(--card-size) * 2 + 10px)` resolve variable tokens prior to arithmetic reduction.

### Pillar 3: Modern Fetch API (`fetch`, `Response`, `Headers`)
- **Data Structures (`include/litebird/net/Fetch.hpp`, `src/net/Fetch.cpp`):**
  - `HeadersObject`: Implements standard Fetch headers with case-insensitive ASCII name lookups, `get(name)`, `set(name, value)`, `append(name, value)`, `has(name)`, and `delete(name)`.
  - `ResponseObject`: Wraps HTTP / data response metadata:
    - `status` (integer code, e.g. 200), `statusText` ("OK"), `ok` (boolean, status in range 200-299), `url` (string), and `headers` (`HeadersObject`).
    - Body readers: `text()` returning a Promise resolving to raw body string, and `json()` returning a Promise resolving to parsed JavaScript object/array via `Json::parse()`.
- **Async Execution & Microtask Integration:**
  - `FetchAPI::fetch(input, init, context)` creates an unfulfilled Promise, decodes `RequestInit` options (`method`, `headers`, `body`), and dispatches through `ResourceLoader::load_for_page` with the document context captured at call time (Phase 33).
  - Seamlessly integrates with the microtask scheduler and JavaScript `async` / `await` workflows.

### Pillar 4: Standard `URL` & `URLSearchParams` APIs
- **Data Structures (`include/litebird/net/URL.hpp`, `src/net/URL.cpp`):**
  - `URLSearchParamsObject`: Stores ordered pairs of key-value query parameters. Implements `get()`, `getAll()`, `set()`, `append()`, `has()`, `delete()`, and `toString()` with RFC 3986 percent-encoding/decoding (`+` for spaces, `%XX` sequences).
  - `URLObject`: Full WHATWG URL decomposition parsing `href`, `origin`, `protocol`, `host`, `hostname`, `port`, `pathname`, `search`, and `hash`.
- **Live Bidirectional Synchronization:**
  - Modifying `url.searchParams` automatically notifies `URLObject` through an `on_change` callback, regenerating `url.search` and `url.href`.
  - Assigning a new search query or updating search parameters recomputes `url.href` consistently across both the JavaScript interface and native model.

---

## Phase 12 Architecture

### Pillar 1: Web Workers & Concurrent JS Execution
- **Dedicated Execution Threads (`include/litebird/js/Worker.hpp`, `src/js/Worker.cpp`):**
  - `WorkerThread`: Spawns a dedicated OS thread hosting its own isolated `JsInterpreter` environment, runtime heaps, and microtask queues, preventing worker operations from blocking the main UI thread.
  - Script loading supports `data:text/javascript,...` and goes through `ResourceLoader::load_for_page` with the creator's context (Phase 33): a web page cannot load a local worker script, a `file:` document can, and a failed load is an error event rather than the path executed as source.
  - Thread-local isolation ensures `JsValue` function callers (`s_function_caller`) remain thread-local and re-entrant across worker contexts without cross-thread clobbering.
- **Inter-Process/Thread Communication (IPC):**
  - Thread-safe queues (`std::mutex`, `std::condition_variable`) transmit deep-cloned JSON messages between the main thread and worker threads.
  - `postMessage(data)` / `onmessage = function(e)` semantics replicate the standard browser Web Worker API.
  - Worker lifecycle management: `worker.terminate()` halts the worker thread event loop cleanly and joins worker threads.
  - Global scope exposure: Worker scripts access `self`, `self.postMessage`, and `self.onmessage`.

### Pillar 2: CSS Modern Grid & Flexbox Refinements
- **CSS Grid Modern Syntax (`include/litebird/css/StyleResolver.hpp`, `src/css/StyleResolver.cpp`):**
  - Parsing and track resolution for `repeat(count, track_spec)` and `minmax(min_val, max_val)`:
    - Fixed track count repetition and `repeat(auto-fit, minmax(...))` calculations based on container dimensions.
    - Track definition expansion supporting `px`, `%`, `fr` fractions, and `auto`.
- **Flexbox and Grid `gap` Property Support (`src/layout/LayoutBox.cpp`):**
  - `row-gap` and `column-gap` (or shorthand `gap`) integrated across flexbox and grid layouts.
  - Flex layout calculates inter-item distribution accounting for gaps without altering individual item base sizes or margins.
  - Grid layout spaces tracks and grid cells with explicit row and column gaps during cell positioning.

### Pillar 3: RFC 6455 WebSocket Client
- **WebSocket Protocol Architecture (`include/litebird/net/WebSocket.hpp`, `src/net/WebSocket.cpp`):**
  - RFC 6455 framing and state machine (`CONNECTING`, `OPEN`, `CLOSING`, `CLOSED`).
  - Cryptographic handshake implementation using zero-dependency standard C++20 SHA-1 (RFC 3174) and standard Base64 encoding for `Sec-WebSocket-Key` and `Sec-WebSocket-Accept` validation.
  - Client-to-server 4-byte XOR frame masking and server frame unmasking.
  - Opcode handling: Continuous (`0x0`), Text (`0x1`), Binary (`0x2`), Close (`0x8`), Ping (`0x9`), Pong (`0xA`).
  - Background network reader thread handles non-blocking socket stream reception and frame defragmentation.
- **JavaScript WebSocket Bindings:**
  - Standard JavaScript interface: `new WebSocket(url, [protocols])`.
  - Event dispatch: `onopen`, `onmessage`, `onerror`, `onclose` triggered asynchronously via the main thread `EventLoop`.
  - Transmission methods: `ws.send(data)` frames payload with client masking; `ws.close(code, reason)` negotiates graceful RFC 6455 connection teardown.
  - Built-in POSIX TCP socket loopback testing server for zero-dependency verified end-to-end WebSocket roundtrips.

---

## Phase 13 Architecture: Modern DOM & Web Components

### Pillar 1: MutationObserver API
- **Data Structures (`include/litebird/dom/MutationObserver.hpp`, `src/dom/MutationObserver.cpp`):**
  - `MutationRecord`: Encapsulates mutation notification data:
    - `type`: `"childList"` or `"attributes"`.
    - `target`: The mutated DOM node.
    - `added_nodes` / `removed_nodes`: Lists of nodes inserted or removed.
    - `previous_sibling` / `next_sibling`: Contextual sibling node pointers.
    - `attribute_name`: Qualified name of the mutated attribute.
    - `old_value`: Attribute value prior to mutation (if `attributeOldValue: true`).
  - `MutationObserverInit`: Options struct parsed from JS observe configuration (`childList`, `attributes`, `attributeOldValue`, `subtree`, `attributeFilter`).
  - `MutationObserver`: Registered observer instance with `observe(target, options)`, `disconnect()`, and synchronous `takeRecords()`.
- **Microtask Batching & Asynchronous Delivery:**
  - DOM mutation hooks (`Node::append_child`, `Node::insert_before`, `Node::remove_child`, `Element::set_attribute`, `Element::remove_attribute`) trigger `notify_child_list` and `notify_attribute_changed`.
  - Observers queue records in memory and schedule a microtask via `EventLoop::post_microtask` if not already scheduled.
  - In accordance with W3C specifications, multiple mutations within a single synchronous tick are batched together and delivered as an array in a single microtask callback invocation.

### Pillar 2: Autonomous Custom Elements API
- **Custom Element Registry (`include/litebird/dom/CustomElementRegistry.hpp`, `src/dom/CustomElementRegistry.cpp`):**
  - W3C tag name validation (`is_valid_custom_element_name`): Requires a lowercase ASCII hyphen (`-`), disallows leading hyphens, and reserves built-in SVG/MathML annotation tags (`font-face`, `color-profile`, etc.).
  - `customElements.define(name, constructor)`: Validates name, extracts `observedAttributes` via static class method invocation, binds constructor and prototype chain, and fulfills pending `whenDefined` promises.
  - `customElements.get(name)`: Retrieves registered constructor or `undefined`.
  - `customElements.whenDefined(name)`: Returns a standard `Promise` resolving when the tag is defined.
- **Element Upgrade & Lifecycle Callbacks:**
  - `upgrade_element(element)`: Upgrades existing or newly created DOM elements by assigning prototype linkages, constructing instances, and triggering initial attribute observation.
  - `connectedCallback()`: Dispatched recursively when an element is inserted into an active document tree.
  - `disconnectedCallback()`: Dispatched when an element is removed from the document tree.
  - `attributeChangedCallback(name, oldValue, newValue)`: Automatically invoked when attributes in `observedAttributes` are mutated.

### Pillar 3: Shadow DOM & Slot Content Projection
- **Shadow Tree Encapsulation (`include/litebird/dom/ShadowRoot.hpp`, `src/dom/ShadowRoot.cpp`):**
  - `element.attachShadow({ mode: "open" | "closed" })`: Instantiates a `ShadowRoot` (specialized `DocumentFragment`) bound to host element.
  - `element.shadowRoot`: Exposes the open shadow root while keeping closed roots encapsulated.
  - `shadowRoot.host`: Direct reference back to the host element.
  - Fragment parsing: `shadowRoot.set_inner_html()` automatically parses templates, extracts `<style>` sheets, and invalidates layout.
- **Scoped CSS Styling & `:host` Handling (`src/css/StyleResolver.cpp`):**
  - Shadow DOM element style resolution queries only the scoped stylesheets extracted from its containing `ShadowRoot`, completely insulating internal components from external document CSS rules.
  - `:host` pseudo-class: Matches the host element from within shadow stylesheets, supporting optional parameter selectors (`:host(.active)`, `:host([disabled])`).
  - Heap-allocated stylesheet caching prevents dangling selector pointers during style resolution.
- **`<slot>` Content Distribution (`src/layout/LayoutTree.cpp`):**
  - Named and default slot mapping: `ShadowRoot::assigned_nodes()` and `ShadowRoot::assigned_elements()` project light DOM children into matching shadow slots (`<slot name="...">` vs default `<slot>`).
  - Layout tree construction automatically replaces `<slot>` placeholders with their assigned light DOM nodes or fallback default content, generating contiguous rendering box hierarchies.

---

## Phase 14 Architecture: Modern Browser Capabilities & Advanced Engine Platform

### Pillar 1: Observers & Client-Side SPA Routing

- **IntersectionObserver API (`include/litebird/dom/IntersectionObserver.hpp`, `src/dom/IntersectionObserver.cpp`):**
  - `IntersectionObserverInit`: Holds root element (or viewport root if null), parsed root margins (`rootMargin`), and threshold thresholds list (defaults to `[0.0]`).
  - `IntersectionObserverEntry`: Encapsulates intersection geometry:
    - `time`: High-resolution timestamp of intersection record.
    - `rootBounds`: Bounding rect of intersection root.
    - `boundingClientRect`: Target element bounding box.
    - `intersectionRect`: Clipped intersection rectangle.
    - `isIntersecting`: Boolean flag indicating non-zero intersection area.
    - `intersectionRatio`: Exact floating-point ratio ($0.0 \le \text{ratio} \le 1.0$).
    - `target`: Target DOM element.
  - Geometry & Threshold Crossing Detection: `check_intersections(root_bounds)` calculates intersection clipped rects, detects threshold boundaries crossed since prior checks, and queues records.
  - Microtask Batching: Pending entries are queued and dispatched via `EventLoop::post_microtask` ensuring asynchronous, low-overhead microtask execution.

- **ResizeObserver API (`include/litebird/dom/ResizeObserver.hpp`, `src/dom/ResizeObserver.cpp`):**
  - `ResizeObserver`: Manages target DOM element tracking and registration.
  - `ResizeObserverEntry`: Reports `contentRect`, `borderBoxSize`, and `contentBoxSize` dimensions.
  - Delta Detection: Compares current element dimensions against cached dimensions, queuing records on size alterations and dispatching via microtasks.

- **History API & Client-Side SPA Routing (`include/litebird/dom/History.hpp`, `src/dom/History.cpp`):**
  - `HistoryItem`: Stores state object (`JsValue`), title, and URL for each session stack entry.
  - Session Stack Management:
    - `pushState(state, title, url)`: Truncates forward history past current index, appends new item, updates document URL, and fires navigation listeners.
    - `replaceState(state, title, url)`: Replaces current history item without mutating stack depth.
    - `back()`, `forward()`, `go(delta)`: Navigates session history, clamps bounds, and dispatches standard W3C `popstate` events with cloned state objects to the `window` target.

### Pillar 2: CSS Visual Filters & Canvas Blending

- **CSS Visual Image Filters (`include/litebird/render/ImageFilter.hpp`, `src/render/ImageFilter.cpp`):**
  - Software image filter pipeline operating directly on 32-bit ARGB pixel buffers:
    - `blur(radius)`: Optimized two-pass separable horizontal and vertical box blur approximating Gaussian distribution.
    - `brightness(amount)`: Scales RGB color channels by factor.
    - `contrast(amount)`: Centers contrast expansion around midpoint 128.
    - `grayscale(amount)`: Rec. 601 luma weighted transformation ($0.299 R + 0.587 G + 0.114 B$).
    - `sepia(amount)`: Matrix transformation simulating antique photographic tones.
    - `invert(amount)`: Inverts RGB channels ($255 - C$).
    - `opacity(amount)`: Multiplies existing alpha values.
    - `drop-shadow(dx, dy, blur, color)`: Allocates expanded shadow buffer, applies separable box blur, and blends original content over offset shadow.
  - Parser & Style Resolver Integration: Parses CSS filter functions from `filter` and `backdrop-filter` rules, binding to `ComputedStyle::filters` and applying before blitting to render targets.

- **Canvas 2D Blend Modes (`include/litebird/render/Canvas2D.hpp`, `src/render/Canvas2D.cpp`):**
  - `globalCompositeOperation` Property: Dynamic JavaScript property binding on `CanvasRenderingContext2D` supporting:
    - `"source-over"`: Standard alpha compositing ($S + D \cdot (1 - S_a)$).
    - `"copy"`: Direct pixel overwrite ignoring destination alpha.
    - `"multiply"`: Per-channel multiplicative blending ($C_r = S_c \cdot D_c / 255$).
    - `"screen"`: Additive inverted multiplication ($C_r = 255 - ((255 - S_c)(255 - D_c) / 255)$).
    - `"lighter"`: Clamped channel addition ($C_r = \min(255, S_c + D_c)$).
    - `"destination-over"`: Under-blending existing pixel content beneath destination.

### Pillar 3: JS Generators, Iterators & AbortController

- **JavaScript Coroutine Generators (`include/litebird/js/Generator.hpp`, `src/js/Generator.cpp`, `src/js/JsParser.cpp`):**
  - Parser Support: `function*` declarations, function expressions, `yield` expressions, and delegated `yield*`.
  - Coroutine Engine (`GeneratorData`, `GeneratorContext`):
    - Implemented with lightweight cooperatively suspended execution contexts.
    - Running generator functions on dedicated worker threads synchronized with mutexes and condition variables.
    - Stepping with `.next(value)` passes values into yield points, resumes execution until the next yield or return, and produces `{ value, done }` objects.
    - Thread-local active context pointers enable nested generator yield suspension across function call stacks.
  - Iteration Protocols & `for...of`:
    - Array, string, and custom iterable support via `Symbol.iterator`.
    - AST and runtime support for `for (const x of iterable) { ... }` executing iteration protocol step loops natively.

- **AbortController & Network Cancellation (`include/litebird/net/AbortController.hpp`, `src/net/AbortController.cpp`, `src/net/Fetch.cpp`):**
  - Standard DOM API: `controller = new AbortController()`, `controller.signal`.
  - `AbortSignal`:
    - `aborted`: Dynamic property returning abort status.
    - `reason`: Stores custom abort reason or defaults to `DOMException("AbortError")`.
    - `addEventListener("abort", listener)`: Event target dispatching on abort.
    - `throwIfAborted()`: Throws active abort reason if already aborted.
    - `AbortSignal.timeout(ms)`: Factory creating an automatically aborting signal with `TimeoutError` via the `EventLoop`.
  - Fetch Cancellation Integration: `fetch(url, { signal })` registers abort listeners; triggering abort immediately terminates sockets, stops background transfers, and rejects promises with `AbortError`.

### Pillar 4: CSS Grid Named Areas & Layout Containment

- **CSS Grid Named Areas (`include/litebird/css/StyleResolver.hpp`, `src/layout/LayoutBox.cpp`):**
  - `grid-template-areas`: ASCII grid template syntax parsing (e.g., `\"header header\" \"sidebar content\" \"footer footer\"`).
  - Validation: Enforces rectangular area constraints; non-rectangular definitions are safely rejected.
  - `grid-area`: Assigns grid items to named areas (e.g. `grid-area: header`) or explicit numerical line indexes (`row-start / col-start / row-end / col-end`).
  - Layout Grid Placement: Area coordinates map items to 1-based start and end row/column tracks, seamlessly handling multi-row and multi-column spans.

- **CSS Layout Containment (`contain`):**
  - Full parser support for `contain: none | strict | content | layout | paint | size`.
  - Layout Isolation: Elements with `contain: layout` or `contain: strict` act as independent layout boundaries. Internal mutations mark dirty bits only up to the containment root, preventing unnecessary layout passes across parent trees.
  - Paint Isolation: Precludes child elements from rendering outside containment clipping bounds.

## Phase 15 Architecture: Spec Fidelity & The Scoreboard

### 1. Why this phase existed

Through Phase 14 the engine could *demonstrate* features but could not *match a
browser*. The parser was a 180-line stack machine: no insertion modes, no
implied tags, no foster parenting, no comments in the DOM, and a five-entity
character reference table. Real pages do not survive that.

Phase 15 therefore added two things: a parser that builds the *same DOM a
browser builds*, and a way to prove it.

### 2. The tokenizer (`src/html/HtmlTokenizer.cpp`)

- All tag states: tag open, end tag open, tag name, before/after attribute
  name, before/after attribute value (double-quoted, single-quoted, unquoted),
  self-closing start tag.
- All comment states, including `<!-->`, `<!--- -->` and the `--!>` terminator.
- Doctype states with public/system identifiers, plus `force-quirks` handling.
- Bogus comment handling for `<!...`, `<?...` and `</...` with no name.
- Content models: `RCDATA` (title, textarea), `RAWTEXT` (style, xmp, iframe,
  noembed, noframes), script data, and `PLAINTEXT`, switched by the tree
  builder as it inserts those elements.
- CDATA sections, gated on the tree builder reporting that the current node is
  in a foreign namespace.
- CRLF normalisation at input time, and NUL handling per state (literal in the
  data state, U+FFFD in RCDATA/RAWTEXT, U+FFFD in foreign content).
- Character references: numeric (with the C1/Windows-1252 remapping) and named,
  with longest-match resolution over a generated 2231-entry table
  (`tools/gen_entity_table.py` → `src/html/EntityTable.cpp`) and the
  "no semicolon inside an attribute followed by `=` or alphanumeric" rule.

### 3. The tree builder (`src/html/HtmlTreeBuilder*.cpp`)

- **Insertion modes:** initial, before html, before head, in head, in head
  noscript, after head, in body, text, in table, in table text, in caption,
  in column group, in table body, in row, in cell, in select, in select in
  table, in template, after body, in frameset, after frameset, after after
  body, after after frameset.
- **Scopes:** the default, list-item, button, table and select scope
  predicates, driven by the spec's boundary-element lists (including the
  MathML/SVG integration points).
- **Active formatting elements:** markers, the Noah's Ark clause (three
  identical entries), reconstruction, and the full adoption agency algorithm
  (outer loop of 8, inner loop of 3, furthest block, common ancestor, bookmark).
  The common ancestor is passed as an *override target* to the insertion
  algorithm — omitting that turns foster parenting into a self-parented node.
- **Foster parenting:** content that cannot live directly in a table is
  relocated before the table (or into the table's parent), including inside
  `<template>` contents.
- **Foreign content:** the breakout tag list, `font` with `color`/`face`/`size`,
  SVG tag-name case adjustment, `xlink`/`xml`/`xmlns` attribute namespacing,
  and MathML's `definitionURL`.
- **Templates:** contents are a separate `DocumentFragment` and template
  insertion modes are stacked separately, per the spec.
- **Fragment parsing** with a context element and the context-sensitive
  "reset the insertion mode" substitution, which is what makes `innerHTML`
  behave (and what makes a `<tr>` survive in a `<tbody>` context but not in a
  `<div>` context).

### 4. The scoreboard (`tools/conformance/run_tree_construction.py`)

The runner parses each `.dat` case, feeds `#data` through
`litebird --dump-dom-webplatform` (the exactly-specified tree-dump format), and
diffs against the expected `#document`. It handles `#document-fragment`
contexts, `#script-on`/`#script-off`, and treats a timeout as a failure rather
than hanging the suite. `--json` output is consumed by CI, which enforces a
floor so the score cannot silently regress.

### 5. Robustness

- `make asan` builds the CLI and tests with AddressSanitizer +
  UndefinedBehaviorSanitizer, runs the full engine suite, then runs the HTML
  parser and pipeline fuzzers against `build-asan/litebird`.
- `tools/fuzz/fuzz_parser.py` mutates the conformance corpus (byte flips,
  token splicing, truncation, duplication, doubling) and feeds each mutant to
  `--dump-dom-webplatform`.
- `tools/fuzz/fuzz_pipeline.py` sends HTML corpus mutations through
  `--dump-layout` and `--render-bmp`, then exercises BMP/PPM file decoding with
  valid and hostile header seeds. It helped expose signed overflow in long PPM
  decimal fields; that parser now rejects overflow without UB.
- LeakSanitizer is deliberately disabled: the JS engine has pre-existing leaks
  that deserve their own pass (see "Known gaps").

---

## Phase 16 Architecture: Floats, Block Formatting Contexts & Table Layout

### 1. Why this phase existed

Phase 15 made the DOM trustworthy but left the box tree unable to express two of
the most common constructs on the web. Every second real page uses a float for
an image or a table for tabular data; until Phase 16 the engine laid both out as
ordinary blocks (floats overlapped their siblings, tables stacked their cells
vertically). Phase 16 closes that gap without changing the parser.

### 2. Float bands (`LayoutBox::float_bands`)

A float is described by a *band* — a horizontal exclusion in the coordinate
space of the box that owns it:

```cpp
struct FloatBand {
    float top, bottom;   // vertical extent, relative to the owner's content box
    float left, right;   // intrusion depth from the owner's left/right content edge
};
```

`band_at(bands, y, left, right)` returns the union of every band covering `y`.
That single predicate drives all three consumers:

- **Float placement.** A floated child is laid out with a shrink-to-fit width
  (`layout_float`), then slid down until the remaining band width can hold it.
  Left floats pack from the left intrusion outward; right floats pack from the
  right edge inward, so `float: left` and `float: right` can share a row.
- **Line-box shrinking.** `layout_inline_formatting_context` queries the band at
  each line's y, reduces that line's available width, and offsets the line's
  fragments by the intrusion. Text therefore wraps beside a float and reclaims
  the full width once the float's bottom edge is passed — per line, not per box.
- **Clearance.** `clearance_y(bands, clear)` returns the lowest bottom edge the
  `clear` value cares about; the flow cursor jumps down to it.

A float never advances the vertical flow cursor, which is exactly what makes it
out of flow.

### 3. Float context propagation

Bands live in the coordinate space of their owner, so a descendant cannot read
them directly. Each block hands its children a `(pointer, y-offset)` pair —
`inherited_bands` / `inherited_bands_offset_y` — pointing at the nearest ancestor
that owns bands, with the child's content origin expressed in that ancestor's
coordinates. The anonymous block that wraps a run of inline content therefore
wraps its lines around a float that is a *sibling* of that content.

`establishes_bfc()` mirrors CSS 2.1 §9.4.1: the root, floats, non-`visible`
`overflow`, flex/grid containers, table cells and inline-blocks establish a new
context, and their height grows to include their own floats. A box with only
floated children is *not* treated as establishing an inline formatting context,
so its floats still get placed by the block path.

### 4. Table layout (`src/layout/FloatTableLayout.cpp`)

Table boxes are real box types (`TableNode`, `TableRowNode`, `TableCellNode`), so
the renderer paints cell backgrounds and borders with no special casing.

1. **Anonymous structure.** Row groups (`<tbody>`, `<thead>`, `<tfoot>`) are
   flattened into a row grid; cells that appear directly in a table are collected
   into anonymous rows; a column count comes from the widest row.
2. **Column sizing (auto layout).** Each column is at least as wide as the widest
   cell's min-content width — the longest unbreakable word, so text can still
   wrap — or the cell's explicit `width`. An explicit table width distributes the
   surplus across the columns; without one the table shrink-wraps its content.
3. **Row sizing.** Each cell is laid out at its column width, positioned on the
   column origin, and the row height is the tallest cell's margin-box height.
4. **Height.** The table is the sum of its rows, or an explicit `height`.

### 5. A rendering bug worth fixing on the way

The bitmap renderer advanced text by `8 * scale` pixels per glyph while the
layout engine measures characters at `0.55 * font-size`. At 16px that is 16px of
advance for 8.8px of layout width, so every line overran its box and glyphs
collided. The renderer now uses the same advance the layout computed. This was a
pre-existing bug, not a Phase 16 regression: it made float and table output
impossible to verify by eye, which is why it was fixed here.

### 6. What is still approximate

Row/column sizing is a first-order approximation of CSS 2.1 §17.5.2 (no
percentage columns, no `rowspan`/`colspan`, no `border-collapse`), floats do not
interact with nested float contexts inside a float, and `overflow` establishes a
block formatting context but does not clip painting.

---

## Phase 17 Architecture: Conformance Push

Phase 16 left the scoreboard at 88.0% and a "known gaps" list that named the
script-data escaped states as unimplemented. Phase 17 was the phase where the
scoreboard was treated as a work queue rather than a report card: each cluster
of failures was traced to a rule, and the rule was implemented. The result is
98.8% (1770/1792) with the residual failures catalogued instead of hidden.

### 1. The script data state machine

The tokenizer had one simplification standing in for five tokenizer states: a
script ran until the first `</script` that was followed by space, `/` or `>`.
That is wrong the moment a script contains a comment. `<script><!--<script
</script>-->` is *escaped* script data, and the `</script` inside it is text;
`<script><!--<script </script -->` is *double escaped*, where `</script` only
leaves the double-escaped state.

`HtmlTokenizer::scan_script_data` now implements the full state set from
WHATWG 13.2.5.20-24: script data, less-than sign, end tag open/name, escape
start (+dash), escaped, escaped dash, escaped dash dash, escaped less-than,
escaped end tag open/name, double escape start, double escaped (+dash, dash
dash, less-than) and double escape end. Two behaviours matter for correctness:

* the end tag is *handed back* to the main tokenizer (the cursor rewinds to the
  `<`), so `</script>` is parsed by the normal end-tag path;
* EOF mid-construct flushes the characters the state had consumed but not yet
  emitted (`<`, `</`, `</scr`), which is what the corpus expects for truncated
  input. Missing that flush alone cost 46 cases.

This one change was worth ~47 cases: `tests16.dat` went from 159/197 to
195/197 and `scriptdata01.dat` from 17/26 to 25/26.

### 2. Whitespace the parser is required to swallow

`<pre>`, `<listing>` and `<textarea>` drop exactly one newline immediately after
the start tag. The builder already had an `m_skip_next_newline` flag; nothing
ever consumed it. It is now consumed at the single token entry point, so it
applies to both normal and fragment parses.

### 3. Scoping is namespace-aware

The stack helpers (`has_element_in_scope`, `has_element_in_table_scope`,
`has_element_in_select_scope`, `pop_until_tag`, `element_index_in_stack`)
matched on `tag_name()` alone. An SVG `<td>` therefore satisfied an HTML "is
there a `td` in table scope" test, and `</p>` inside `<svg>` was swallowed as a
foreign end tag. Every HTML tree-construction rule means *an HTML element*, so
those helpers now require the HTML namespace. This single fix was worth ~18
cases across `svg.dat`, `math.dat`, `foreign-fragment.dat` and `tests9.dat`.

### 4. The rest of the queue

* `<nobr>` was listed in the generic formatting-element set, so its own rule
  (run the adoption agency when another `nobr` is in scope) was unreachable.
* `<html>`/`<body>` start tags inside a `<template>` must be ignored; they were
  merging attributes into the document's real `<html>`/`<body>`.
* SVG attribute case adjustment (`viewBox`, `preserveAspectRatio`, ... 60 names)
  was missing, so the DOM exposed the tokenizer's lower-cased spelling.
* `xml:base` was being namespace-adjusted; it is not in the spec's table.
* `eof-in-tag`: a tag that hits EOF mid-way is discarded, not emitted (this is
  why `alt=">` swallowing the rest of the document must produce *no* `<img>`).
* `<!` + anything else started its bogus comment *including* the `!`; `</` at
  EOF became a comment instead of the text `</`.
* Numeric character references with no digits (`&#xZOO`) dropped the `x`.
* Unknown elements inside `<select>` are kept as ordinary elements (the modern
  select parsing rules the WPT corpus encodes), rather than dropped.

### 5. The harness was mis-scoring the engine

Three harness bugs were worth ~55 "failures" that had nothing to do with the
engine: cases were split on blank lines (truncating any case whose data
contained one), the corpus was read in text mode (turning a lone `\r` into
`\n`, which `&#x0D;` cases assert about), and the expected tree kept a trailing
blank line. Fixing them moved the denominator from 1790 to 1792. It is recorded
here because a scoreboard that is wrong in the engine's favour is as bad as one
that is wrong against it: the honest number before and after is stated
(88.0% → 98.8%) rather than quietly re-baselined.

---

## Phase 18 Architecture: Percentages and Clipping

Phase 17 was measurable because a corpus said what the DOM should be. Layout has
no such oracle in this repository, so Phase 18 was chosen for a different
reason: it fixes things that were *observably wrong* rather than merely
incomplete, and both are verifiable with assertions that do not depend on taste.

### 1. `width: 50%` was fifty pixels

`StyleResolver::parse_length` ends with `return number;` for any unit it does
not recognise -- including `%`. The comment there explained that percentages
"are resolved by the layout stage instead", but nothing carried the percentage
through: `width: 50%` stored `50.0f` in the pixel field and layout happily used
fifty pixels. Every percentage length in every stylesheet was wrong, and one
unit test (`css_relative_units`) *asserted the bug*.

The fix keeps the two halves separate, because only layout knows the containing
block:

* `ComputedStyle` gains `width_percent`, `height_percent`,
  `min_width_percent`, `max_width_percent`, `min_height_percent`,
  `max_height_percent` and `margin_percent` / `padding_percent` edge sets.
  A negative value means "not a percentage"; the pixel field stays `-1`
  (auto) so every existing `>= 0` test keeps meaning "definite length".
* `LayoutBox::resolve_percentages`, called from `LayoutBox::layout` before
  dispatch, multiplies the percentages by the containing block. It *keeps* the
  percentage fields, so a relayout at a different viewport recomputes instead of
  reusing a stale pixel value.
* Percentage widths, min/max widths, margins and padding resolve against the
  containing block's **width**; percentage heights resolve against a definite
  containing height and fall back to `auto` otherwise, which is what CSS does
  for the usual auto-height ancestor. `layout_block` now hands its children a
  definite height when it has one, so `height: 50%` inside a fixed-height box
  works.
* Table cells resolve their percentage widths against the table's own width,
  after the table width is known (CSS 2.1 17.5.2), so `width: 25%` on a cell in
  a 300px table is a 75px column.

### 2. `overflow` now clips

`overflow: hidden` already established a block formatting context (Phase 16),
but content still painted outside the box, because the display list had no way
to express a clip. It does now:

* `PushClipCommand` / `PopClipCommand` join the display-list vocabulary, emitted
  around a box's own background and its whole subtree for
  `overflow: hidden|auto|scroll` and `contain: paint`, using the padding box
  (CSS 2.1 11.1.1).
* `BitmapRenderer` checks every pixel write against the active clip stack in
  `set_pixel` -- which every drawing path (rect, border, glyph, image, shadow)
  funnels through, so one check covers them all.
* `TerminalRenderer` applies the same test per cell.

### 3. How this is verified without a reftest corpus

There is no pitch-comparable rendering corpus here, so the tests assert
*geometric* facts and *pixel* facts rather than screenshots:

* `percentage_lengths_resolve_against_containing_block` checks a 50% child of a
  400px box is 200px, that `padding: 10px 5%` is 20px a side, that
  `max-width: 25%` caps an auto box at 100px, that `height: 50%` inside a
  200px-tall box is 100px, and that a 25% cell in a 300px table gets a 75px
  column.
* `overflow_hidden_clips_painting` builds the display list, asserts exactly one
  clip push/pop pair, and then rasterises: a 300x200 red child inside a 100x50
  `overflow: hidden` box leaves white pixels outside, while the same markup with
  `overflow: visible` bleeds red across the page.

### 4. What is still approximate

Percentage `border-radius`, percentage `line-height` and percentage `font-size`
are still not resolved (they are not box dimensions), percentage heights inside
auto-height ancestors fall back to `auto`, and clip rects are rectangles -- no
rounded-corner clipping, no `clip-path`. `overflow: auto`/`scroll` clip but do
not create scrollbars or scrollable overflow.

---

## Phase 23 Architecture: The JavaScript Engine, Rebuilt

### 1. Why this phase existed

Phase 15 gave the HTML parser an outside scoreboard, and the parser has been
trustworthy since. The JavaScript engine had none: it was measured by unit
tests written next to the code, and they passed. Phase 23 started by building
the missing scoreboard (`tests/js-conformance/`, expected output recorded from
Node.js) and running the engine against it:

| | Everyday probe (90 cases) | Whole corpus (801 cases) |
| :--- | :--- | :--- |
| Engine before Phase 23 | 25 | 140 |
| Engine after Phase 23 | 90 | 796 |

The failures were not edge cases. Reading the engine found the causes:

- The lexer turned any character it did not know (`?`, `&`, `|`, `^`, `~`) into
  end-of-file. The rest of the script was dropped without an error.
- `try`, `catch`, `throw`, `continue`, `do` and `in` were not keywords.
- `eval()` ran the bytecode VM first and, on any exception, ran the whole
  script again in the tree-walker. Every side effect up to the exception
  happened twice.
- Strings had no `length` and no methods. Arrays carried six closures each and
  had no prototype. `Map` keyed by the string form of the key. `Symbol()`
  returned a string. An undeclared name read as `undefined` instead of
  throwing.

Patching these one at a time would have left the two overlapping execution
paths in place, so the front end, the evaluator and the standard library were
rewritten. The DOM bindings, the event model and the host APIs were kept.

### 2. Front end and evaluator

`JsLexer` produces tokens and reports anything it cannot tokenise as a
`JsSyntaxError`. `JsParser` is a recursive-descent parser that builds an AST
(`NodeKind`, `Pattern`, `FunctionNode`, `ClassNode`, `Program`) and resolves
what can be resolved once: which names a function declares with `var`, whether
it uses `arguments`, `super` or `await`, whether a block declares anything and
so needs a scope of its own. A script that does not parse does not run.

`JsEval.cpp` walks the AST. Statements return a completion
(`Flow {Normal, Break, Continue, Return}`) instead of throwing C++ exceptions
for control flow; only a JavaScript `throw` unwinds the native stack, as a
`JsThrow` carrying the thrown value. `eval()`/`eval_ast()` report an uncaught
error on stderr as `Uncaught ...` and count it; `run()` lets it propagate.
Host code that calls into script from a place with nothing above it to catch
(an event listener, a timer) goes through `js_run_guarded`.

Native functions keep the signature `JsValue(const std::vector<JsValue>&)`.
The receiver travels in a per-thread call context that `call_with_this()` and
`construct()` set; natives read `JsValue::current_this()`.

Functions and classes record where they start and end in the script, and the
script text is kept for as long as any of them is alive, so
`Function.prototype.toString` returns the source. That is not cosmetic:
libraries tell built-ins from user code by testing for `[native code]`.

The bytecode compiler and VM from earlier phases still build and back
`--dump-bytecode`, but no script runs on them. They cover a small subset of
the language, and keeping two engines that must agree was the source of the
double execution.

### 3. The standard library and realms

Built-in methods live on prototype objects (`Array.prototype.map` is one
function, not one closure per array). The set of prototypes and constructors
is a realm (`JsIntrinsics`). Each interpreter owns one, so a page that patches
`Array.prototype` does not change it for the next page or for a worker. Code
with no interpreter at hand (host bindings, unit tests) uses the realm current
on its thread, or a process-wide default.

The library is split by area: `BuiltinsObject.cpp` (Object, Function,
Boolean, Symbol, Reflect, the Error family), `BuiltinsArray.cpp`,
`BuiltinsString.cpp`, `BuiltinsNumber.cpp` (Number, Math), `BuiltinsCollections.cpp`
(Map, Set, WeakMap, WeakSet), `BuiltinsJson.cpp`, `BuiltinsRegExp.cpp` (a
backtracking matcher written for this engine), `BuiltinsDate.cpp` and
`Promise.cpp`.

### 4. Promises, async functions and the event loop

A promise's state is a `PromiseData` shared between the script-visible
objects and the engine code that settles it. Reactions always run as
microtasks, first in, first out. Resolving with a thenable adopts it from a
microtask, never synchronously. A rejection is the thrown value itself.

`await` has to stop a function in the middle of an expression and give
control back to its caller. A tree-walking evaluator keeps that state on the
native stack, so a function that can suspend runs on a stack of its own: an
async function whose body contains `await`, like a generator, gets a thread.
The threads never run at the same time. Control is passed with a strict
handshake (`GeneratorData::resume` / `yield_to_main`): the frame's thread runs
only while the thread that resumed it is blocked waiting for it. Script is
therefore still single-threaded, and ThreadSanitizer reports nothing on the
async and generator cases. The cost is one OS thread per suspended call; see
"What is still approximate".

The event loop (`EventLoop`) holds tasks, microtasks and timers. There is a
microtask checkpoint whenever the script stack empties (`JsScriptScope`), which
is what makes a promise callback run after the current script and before the
next timer. The loop has its own clock, which can skip ahead to the next timer
instead of sleeping; `Date.now()` and `performance.now()` follow it. The
command-line paths use that: `--run-js` runs a script's timers to completion
without waiting for them, and a page rendered once settles for one frame of
virtual time (`--virtual-time-budget`) before layout.

A page and each worker is an agent with its own event loop. A worker's timers
and promise callbacks run on the worker's thread, not the page's.

### 5. The collector finds its own roots

Every object is a heap cell and nothing but the collector frees one, so the
collector must never free a cell something still refers to. The engine used to
ask everything that held a value to register it as a root. That cannot be made
complete: values sit in C++ locals of the evaluator, in half-built argument
lists, in `std::function` captures of DOM listeners and timers, on the stack
of a suspended generator. Each one that was missed was a use-after-free the
next time `gc()` ran. Two were found by testing, not by reading: a listener's
closure, and the scope of a `for (let ...)` loop.

The collector now needs no registration.

- Every `JsValue` that points at a cell holds a counted handle (`CellRef`), so
  a cell knows how many references to it exist. Scopes and function payloads
  are already reference-counted by `shared_ptr`.
- A collection counts the references it can enumerate: those stored in cells,
  in scopes and in function payloads.
- Anything with more references than that is held from somewhere the collector
  cannot see. It does not need to know where: the thing is a root.
- From the roots it marks as before. Unmarked cells are freed and unmarked
  scopes emptied, which also collects the cycle every closure forms with the
  scope it was created in.

Two rules keep it sound, and they are stated in `Heap.cpp` next to the code. A
reference is only counted if whatever stores it cannot be kept alive behind
the collector's back; storage native code may co-own (a pending promise's
reactions, for example) is left uncounted, which makes its contents roots. And
every stored reference is counted at most once. Counting too little only keeps
garbage alive, so every doubtful case errs that way.

Cells and scopes are tagged with the agent that created them. A collection
examines and frees only what belongs to the agent running it, so a page
collecting does not look into objects a worker thread is changing.

`tests/js-conformance/gc-safety.dat` runs a collection in the middle of
everything (callbacks, getters, half-built literals, loops, suspended
generators and async functions) and requires Node's answer. `make asan` runs
it under AddressSanitizer, where a freed object still in use is an error.

### 6. Deep JavaScript cannot exhaust the native stack

A recursive-descent parser and a tree-walking evaluator recurse as deep as
their input nests, and C++ destructors recurse as deep as the structure they
free. Both are fine for ordinary code and both end the process on input built
to be deep: 200,000 nested parentheses, or a linked list of a million closures
that is dropped. Stress pages found 28 such cases (AUDIT.md, finding 21).

This section is about the JavaScript engine. The DOM has the same weakness and
it is not fixed: see "Known gaps", item 11.

Two mechanisms close this, and both are deliberately independent of how much
stack one level costs, which differs by a factor of 50 between an optimised
build and a sanitizer build:

- **Recursion that follows the input checks the stack.** `js_check_stack()`
  compares the stack pointer with the end of the thread's stack
  (`common/StackLimit.hpp`) and throws a `RangeError` while a megabyte is
  still left. The parser's recursive entry points call it, as do the
  evaluator's statement and pattern walks, function calls, and the library
  functions that recurse over data (`join`, `flat`, `structuredClone`,
  `JSON.stringify`, the JSON parser). A script that is too deep is reported as
  `Uncaught RangeError: Maximum call stack size exceeded`; the next script on
  the page runs.
- **Recursion that would only free memory is turned into a loop.**
  `common/ReleaseQueue.hpp` is a per-thread queue: a destructor hands over what
  it owns instead of destroying it, and the outermost destructor drains the
  queue. Syntax trees (`ast_delete`), scopes, promise chains and function
  payloads are released this way. Running out of stack while freeing cannot be
  reported to anyone, so it must not be able to happen.

Long operator chains are not nesting, even though `a+b+c+...` parses into a
left-leaning tree as deep as the chain is long. `eval_binary_chain` walks such
a chain in a loop, so a sum of a million terms is evaluated, not refused.

Generators and async functions recurse in a third way. A frame that resumes
another frame waits for it, so `function* g() { yield* g(); }` stacks up
threads instead of stack frames, and no stack check can see that. Frames may
therefore be resumed from inside one another at most 2,000 deep
(`kMaxFrameNesting`); beyond that the script gets the same `RangeError`.
Without the limit the recursion ended when the system refused another thread,
at a depth that differs by machine and can be more than its memory holds.

Prototype chains are walked without a depth limit. They cannot be circular:
`js_set_prototype` is the one place a script can change the prototype of an
object that already exists, and it refuses a change that would close a loop.

### 7. Page lifecycle

`document` is an event target, `document.readyState` exists, and
`JsInterpreter::notify_document_loaded()` fires `DOMContentLoaded` and `load`
after the document's scripts have run. The command-line renderer and the
shell both call it. `removeEventListener`, the `{capture, once}` options and
`this` inside a listener were added on the way.

### 8. How this is verified

| Check | Result |
| :--- | :--- |
| JavaScript corpus (`make js-conformance`) | 796 / 801; every expectation re-checked under Node 22 in CI |
| Differential run against Node: 67,545 generated expressions, results compared | 99 differ (0.15%); it was 835 before the fixes in AUDIT.md findings 22 and 23 |
| Unit tests (`make test`) | 85 / 85 |
| ASan + UBSan: unit tests, the corpus, both fuzzers (`make asan`) | 85 / 85; corpus 795 / 801, the extra failure being a recursion 2,000 deep, which a sanitizer build has no stack for; 400 + 400 fuzz runs; no sanitizer report anywhere |
| Stress pages: 88 pages of deep JavaScript nesting and long chains, normal and ASan builds | no crash and no sanitizer report; the pages that run out of time or memory instead are listed in AUDIT.md, finding 21 |
| ThreadSanitizer: async, generator, iterator and control-flow cases | no reports when run; not repeated since the collector was rewritten |
| html5lib tree construction | 1770 / 1792, unchanged |
| The 16 example pages, DOM and layout dumps against the previous engine | 12 identical; 4 differ because a script that used to stop now runs to the end |

The differential run and the stress pages are generated by scripts that are
not in the repository; what they found is, as corpus cases.

Three existing unit tests were changed. `gc_roots_are_complete_under_empty_root_set`
compares the live cell count with the default realm's permanent cells instead
of zero, because built-in prototypes are now real objects. `js_async_promises_and_json`
calls `then` with its receiver, because `then` now lives on `Promise.prototype`.
`strict_equality_does_not_coerce` constructs a `Document`; it used to pass by
comparing `undefined === undefined`.

### 9. Speed

Milliseconds for the whole process, best of three, one run on one machine;
repeat runs vary by about 30%. An empty script takes Node 21 ms and Litebird
9 ms. "Before" is the engine at the end of Phase 22.

| Script | Before | After | Node 22 |
| :--- | :--- | :--- | :--- |
| 2M-iteration loop | 2098 | 376 | 28 |
| 300k function calls | 1994 | 165 | 28 |
| 200k string concatenations and `length` | 297, wrong result | 56 | 34 |
| 200k `push`, then `map`/`filter`/`reduce` | 5447, wrong result | 254 | 41 |
| 100k object allocations and property reads | 322 | 103 | 32 |
| 200k method calls on a class instance | 1396 | 95 | 27 |
| 2k awaits in sequence | 18 | 243 | 28 |

The rebuilt engine is 3 to 15 times faster than the one it replaced, except
at `await`, where it now pays for a thread (section 4) and the old engine did
not suspend at all.

These scripts are too short to compare with Node, whose figures are mostly
start-up. On work long enough for that not to matter, a 20-million-iteration
loop takes Litebird 7.1 s and Node 0.11 s, and 3 million function calls take
2.8 s and 0.05 s: a tree-walking interpreter is 50 to 100 times slower than a
JIT, and this one is no exception.

**Where it was worse than that.** A string was stored inside the value that
held it, so reading a string variable copied the string, and `length` and
indexing scanned it. A loop over 200,000 characters took 76 s where Node takes
35 ms. That is fixed: see Phase 24 below. Two containers had the same
shape:

| Script | Litebird before Phase 30 | Node |
| :--- | :--- | :--- |
| `delete` of 50,000 keys from one object | over 120 s | 91 ms |
| `shift()` until a 50,000-element array is empty | 5.3 s | 0.2 s |

The property map rebuilt its index on every `delete`, and arrays were plain
vectors. Phase 30 removed both.

### 10. What is still approximate

- **One OS thread per suspended function.** Each generator and each async call
  that is waiting on an `await` holds a thread. Thousands at once work; the
  system's thread limit is the engine's limit, reported as a `RangeError`.
  Starting one costs about a tenth of a millisecond: 2,000 awaits in sequence
  take 240 ms where Node takes 28.
- **Recursion depth.** A JavaScript call costs about 2 KB of native stack, so
  a default 8 MB stack gives roughly 3,000 nested calls before
  `RangeError: Maximum call stack size exceeded`. Node allows about 12,000.
  Generators delegating to generators, and async functions calling async
  functions before their first `await`, may nest 2,000 deep.
- **No automatic collection.** The collector is sound and collects cycles, but
  it only runs when `gc()` is called or the shell's heap view is opened.
- **Missing language features:** BigInt, `Proxy`, typed arrays and
  `ArrayBuffer`, modules, `with`, and holes in arrays (`delete a[1]` leaves
  `undefined`, and `1 in a` stays true).
- **Approximate library corners:** `localeCompare` is not real collation,
  `normalize()` returns its string unchanged, case mapping covers Latin, Greek
  and Cyrillic, Unicode property escapes in regular expressions are
  approximate (`\p{L}` misses fullwidth letters and letters outside the BMP),
  there is no `Intl`, regular expressions have no `d` flag, and
  `WeakMap`/`WeakSet` hold their keys strongly. The first four are all missing
  Unicode data; `tools/conformance/diff_strings.py` lists the results they
  change.
- **`console.log` of an object** prints `[object Object]`, not its contents.
- **`await` outside an async function** (the top level of a script, an async
  generator body) blocks and runs the event loop instead of suspending.
- **Workers** have their own interpreter, event loop and collector scope, but
  allocate from the same heap as the page. In the one-shot command-line modes
  (`--run-js`, rendering a page once) the process exits when the page's own
  event loop is idle, without waiting for a worker's reply; workers are only
  usable in the shell.
- **Early errors.** Duplicate labels and `break` to an unknown label are not
  rejected when the script is parsed.

---

## Phase 24 Architecture: Strings That Cost What They Read

### 1. Why this phase existed

Phase 23 left one defect that made ordinary scripts appear to hang. A string
was a `std::string` inside the `JsValue` that held it. Reading a variable
copied the text, and `length` and indexing scanned it to find out whether it
was ASCII, so anything done once per character cost the whole string each
time:

| Script | Phase 23 | Phase 24 | Node 22 |
| :--- | ---: | ---: | ---: |
| `s.charCodeAt(i)` over 200,000 characters | 77,082 ms | 91 ms | 31 ms |
| `s += x`, 100,000 times | 9,811 ms | 49 ms | 45 ms |
| tokenizer that eats its input, `s = s.slice(k)`, 20,000 tokens | 2,556 ms | 18 ms | 31 ms |
| `re.exec(s)` in a loop, 20,000 matches in 200,000 characters | killed: out of memory | 44 ms | 33 ms |

A longer table is in AUDIT.md under finding 25. `make bench` runs these and 35
more against Node and fails if a result differs or a script takes over 3 s.

### 2. `JsString`

`include/litebird/js/JsString.hpp`, `src/js/JsString.cpp`. A string value
holds a handle of 24 bytes:

```
JsString  { block*, byte offset, byte length, unit offset, unit length }
              |
              v
Block     { refs, used, capacity, moves, wide* } [ UTF-8 text ......... free room ]
                                         |
                                         v
Wide      { units ready, bytes read } [ UTF-16 code units ... ] [ a mark every 256 units ]
```

The text is UTF-8 in a block that is shared and reference-counted. A handle is
a range of its block. Bytes that have been written are never written again;
that one rule is what makes the sharing invisible to script.

- **Reading.** Copying a value copies the handle and bumps a count. `length`
  is a field. A string whose byte length equals its unit length is ASCII, and
  indexing it reads one byte. Any other string gets a UTF-16 copy (`Wide`) the
  first time it is indexed. The copy belongs to the block, is extended when
  the block's text grows, and is never rebuilt; next to it is a table that
  says where every 256th code unit is in the bytes, so a slice can be located
  by index without reading what comes before it.
- **Appending.** `a + b` looks at whether `a` ends where its block's text
  ends and whether there is room after it. If so it claims the room (one
  compare-and-swap on `used`) and writes `b` there; `a` is not copied, and
  every other handle still sees the bytes it saw. Otherwise the result goes
  into a new block. Text that has moved three times in a row is being built
  in a loop and gets half again as much room as it needs, so n appends copy
  about 3n bytes in total instead of n²/2. Text joined once or twice carries
  no spare room.
- **Pieces.** `s.slice(a, b)` is another range of the same block when it is a
  quarter of the block's bytes or more, and a copy when it is smaller. The
  quarter bounds what a slice can keep alive to four times its own size. A
  tokenizer that does `s = s.slice(n)` therefore copies less than its input
  once, in a few steps, as the remainder shrinks past each quarter.
- **One spelling.** Bytes that are not UTF-8 become U+FFFD on the way in, as
  when a browser decodes a page. Half of a surrogate pair may stand alone
  (three bytes), since script can make one, but a high half followed by a low
  half is always stored as the one four-byte character. Equal strings
  therefore have equal bytes, and `===`, property keys and `Map` keys can
  compare bytes. The cost is that a piece cannot share its block if it starts
  or ends between the two halves of a pair; it is built from the code units
  instead.
- **Order.** `<` and `sort()` compare UTF-16 code units. Comparing the bytes
  agrees with that except for a character outside the BMP, which has the
  highest bytes in UTF-8 but is a surrogate pair, below U+E000, in UTF-16.
  `JsString::compare` compares bytes until the texts differ and settles that
  one case there. Nothing else in the engine may order string text.
- **Limit.** A string is at most 2^29 − 24 bytes. Past that script gets
  `RangeError: Invalid string length`. For ASCII that is V8's limit exactly;
  other text reaches it sooner than in V8, which counts code units.
- **Threads.** Suspended async functions, generators and workers run on other
  OS threads (Phase 23, section 4), so handles are copied, read, appended to
  and dropped from several threads. The count, the claim on free room and the
  publication of the UTF-16 copy are atomic; building the UTF-16 copy takes
  one global mutex, and reading it takes none.

### 3. What changed around it

- `JsValue::string_val` is a `JsString`, and a value is 88 bytes (was 96).
  Engine code reads a string as `view()` (the bytes, no copy), `length()`,
  `unit_at(i)`, `substring(b, e)`, or through `Utf16View`, which addresses
  any string by code unit. `str()` makes an owned copy and is the one read
  whose cost grows with the text; code that runs once per character must not
  call it. `JsString::stats()` counts bytes copied and units converted per
  thread, and the unit tests assert on those counts, not on time.
- `String.prototype` methods take their receiver as a handle and search it in
  place. The regular-expression matcher reads its subject through a
  `Utf16View`; it used to compare the subject with a cached UTF-16 copy, and
  copy that, on every call. A match array's `input` is the subject itself,
  not a copy.
- `'key' + i` has a direct path in `js_add`. The general path converts both
  operands first, and with a string that is now a block of its own that was a
  measurable cost.
- Property names are still `std::string`. A string value used as a key
  (`o[k]`) is copied once per lookup; that is bounded by the key's length and
  is the same as before.
- Text that leaves the engine is UTF-8. Inside, half of a surrogate pair is
  three bytes that no decoder accepts; `console.log` and the uncaught-error
  report pass their text through `js_output_text`, which writes U+FFFD for a
  lone half, as Node does. The other direction is strict too:
  `decodeURIComponent` throws `URIError` for bytes that are not UTF-8
  (a surrogate, an overlong form, anything above U+10FFFF).

### 4. Lookbehind

`(?<=x)` used to be tried forward from every earlier position in the subject,
so one lookbehind that failed cost the length of everything before it:
`/(?<=,)\w+/g` over 120,000 characters took 50 s. The body of a lookbehind is
now matched right to left from the current position, as the language defines
it. The matcher keeps a direction; inside a lookbehind a sequence runs from
its last term to its first, a character is read from before the position, a
back reference is compared against the text that ends there, and a capture is
recorded as `[low, high)` whichever way it was walked. A lookahead inside a
lookbehind runs forward again. Lookarounds finish before their continuation
runs, so the direction is plain state that a scope object sets and restores.

In unicode mode the subject is a sequence of code points, so a match may not
begin between the halves of a surrogate pair; both the search loop and the
`lastIndex` a script sets are held to that.

### 5. How this is verified

| Check | Result |
| :--- | :--- |
| JavaScript corpus (`make js-conformance`) | 840 / 845; the five failures are the Phase 23 ones (BigInt, `Proxy`, typed arrays, array holes). 44 cases are new: `long-strings.dat` (34), lookbehind and unicode cases in `regexp.dat` (6), surrogate halves, `isWellFormed`, console output and `decodeURIComponent` in `strings.dat` (4). Phase 23's engine passes 810 of the 845 at best (809 when one script that takes it 9.9 s misses the 10 s limit) |
| The corpus re-run under Node 22 | 845 / 845 |
| Unit tests (`make test`) | 88 / 88. Three are new: a random-edit model test, a thread test, and one that asserts on counted work through the interpreter |
| `make tsan`: the two stress tests alone, under ThreadSanitizer, and under ASan + UBSan with leak checking | no report, nothing leaked |
| Planted bugs: 47 in `JsString`, one at a time | 46 caught by the stress tests; the other changes no result and no counted work |
| Planted data races: 4 | ThreadSanitizer reports all 4; the assertions alone catch 2 |
| `make diff-strings`: 1,133 string expressions over 36 subjects, evaluated here and in Node | 95 of 50,580 results differ, every one of them Unicode data the engine lacks: collation (46), the letter table behind `\p{L}` (24), normalization (17), case mapping of `ǅ`, `İ` and fullwidth letters (8). Phase 23's engine differs on 1,103: 720 are the two methods it did not have, 290 are fixed, 93 remain |
| Differential run against Node when the matcher changed: 75 lookbehind patterns over 20 subjects | 0 of 1,500 results differ; Phase 23's engine differs on 89 |
| ASan + UBSan: unit tests, the corpus, both fuzzers (`make asan`) | 88 / 88; corpus 839 / 845, the extra failure being the recursion 2,000 deep that a sanitizer build has no stack for; 400 + 400 fuzz runs; no sanitizer report anywhere |
| `make bench`: 39 string-heavy scripts against Node | every result equal to Node's; the slowest takes 0.2 s, the limit is 3 s |
| html5lib tree construction | 1770 / 1792, unchanged |

The string differential is `tools/conformance/diff_strings.py`; it takes a few
seconds, needs Node, and runs in CI with the 95 as a ceiling. The scripts that
plant bugs and the lookbehind grid are not in the repository; what they found
is, as corpus cases and as assertions in `tests/js_string_stress.hpp`.

### 6. What is still open here

- **Regular expressions over long input.** A group repeated more than about
  12,000 times ends in a `RangeError`, and a match is abandoned after 30
  million steps. Both are in AUDIT.md, finding 26; the first needs the matcher
  to keep its backtracking state on the heap.
- **`delete` and `shift()`** were quadratic in the size of the object or
  array (Phase 23, section 9). Phase 30 makes both linear: the property map
  keeps insertion order in a list with a stable hash index, and arrays are
  deques. They were found by the same timing run and are not string
  problems.
- **`repeat`, `padStart` and `padEnd`** build their result in a temporary and
  copy it into a block: twice the memory for a moment, on a very long result.
- **Short strings have no inline form.** Every string except the empty one and
  the 128 one-character ASCII strings has a block. Measured cost: no time,
  about 20 bytes a string (AUDIT.md, finding 25).

---

## Phase 25 Architecture: Trees of Any Depth

### 1. Why this phase existed

Phase 23 made deep JavaScript end in a `RangeError` instead of a crash. The
DOM had no such protection. A file of 300,000 nested `<div>` elements, 3.3 MB
and no script, ended the process with a `SIGSEGV` in every output mode, and a
script got there with `appendChild` in a loop. The fuzzers start from the
html5lib inputs, a few lines each, and change them a few bytes at a time, so
they never came near it.

`tools/fuzz/deep_pages.py` generates the pages they cannot: 145 pages
that each nest or repeat one construct, most of them 200,000 times, run in a
child process with an 8 MB stack, 2 GB of memory and 60 seconds. On `main`
264 of its 392 runs fail. Not all of them fail on depth:
the same pages found a crash in `calc(`, a 60-byte page that crashes the
parser, bitmaps as large as an element asks for, and loops that cost the
square of their input. AUDIT.md has the measurements under findings 24, 27
and 28.

### 2. Who may build a deep tree, and who walks it

| Tree | How deep | What walks it |
| :--- | :--- | :--- |
| Built by the HTML parser | 512 levels at most | anything |
| Built by script (`appendChild`, `attachShadow`) | no limit | DOM code, which does not recurse; layout, painting, SVG and hit testing, which recurse and check the stack |

A browser limits the first and not the second, and so does Litebird. The two
kinds of walker need different answers. A DOM query that stops halfway
returns a wrong result, so the DOM walks became loops. Layout that stops
halfway draws less, which is acceptable for a tree no person wrote, so layout
kept its recursion and asks `stack_is_exhausted()` at every level
(`include/litebird/common/StackLimit.hpp`, from AUDIT.md finding 5). A fixed
depth would not work there: a level of `build_box` is 2.8 KB in a normal
build and 34 KB under AddressSanitizer.

### 3. The nesting limit (`HtmlTreeBuilder`)

`kMaxTreeDepth` is 512, counting `<html>` as 1. It is the number Blink
(`kMaximumHTMLParserDOMTreeDepth`) and WebKit use; the HTML standard has no
limit, and Gecko was not looked at.

Every insertion goes through `place_for_new_node()`, which is the standard's
"appropriate place for inserting a node" passed through
`within_depth_limit()`. When the node would be deeper than the limit, the
place moves up to the nearest ancestor that can take it. The new element is
still pushed on the stack of open elements, and the insertion mode, the list
of formatting elements and the form and head pointers are untouched. So the
parser's state is what the standard says at any depth; only the tree is
flatter. 6,000 nested `<div>` give 6,000 elements: 510 inside one another and
5,490 beside the innermost.

This is Blink's rule (`HTMLConstructionSite::Attach`). WebKit's
(`attachLater`), which pops the current node instead, was implemented first
and abandoned: it leaves the insertion mode naming an element that is no
longer open, and 12 of 1,500 random pages written to sit on the limit crashed.

Three details:

- **Depth is measured in the tree.** The length of the stack is not the
  depth: `</form>` removes the form from the stack and leaves what is open
  inside it, and a `<template>`'s contents hang off the template.
  `element_depth()` caches the depth of each open element in `m_open_depths`.
- **The adoption agency algorithm obeys it.** It moves a block and everything
  in it under a new element, then moves the block's children under a clone.
  Both attachments are limited, and at the limit the clone goes beside the
  block instead of inside it.
- **Order is not always kept at the limit.** An element written inside
  another is attached beside it, and when misnested formatting tags then move
  the outer one, the inner one stays. `<section>x<p>y</p>` inside 300 open
  `<b>` at the limit reads "yx" once the `</b>` tags arrive. All the content
  is there.

### 4. The stack of open elements is indexed

Without a cap on the stack, anything the standard answers by walking down it
costs the depth of the page per token. 20,000 elements left open followed by
20,000 `<table></table>` took 63 s. Earlier work (AUDIT.md, finding 16)
indexed the scope queries. Now every per-token question is answered from
state kept as elements are pushed and popped:

| Question | Kept in |
| :--- | :--- |
| Is a `<p>`, `<li>`, `<table>`... open, and where is the nearest one | `m_open_html_tag_indices`: for each tag, the positions on the stack that hold it |
| Where on the stack is this element | `m_open_positions` |
| Where is the nearest boundary of a scope, and the nearest "special" element | `m_scope_boundary_indices[0..4]`: the default, list item and button scopes, every "special" element, and the same without `address`, `div` and `p` (what `<li>` and `<dd>` stop at) |
| Is any open element SVG or MathML | `m_open_foreign_elements`, a count. Gone since Phase 26: it guarded a walk that is no longer needed, because the SVG and MathML elements that are special are in the indexes above |
| How deep in the tree is this open element | `m_open_depths`, measured only near the limit |

Removing an element from the middle of the stack (the adoption agency,
`</form>`) reindexes from that position up
(`unindex_open_elements_from` / `index_open_elements_from`). The rule for new
code: no loop over `m_open_elements` on a path that runs per token.

### 5. The DOM without recursion (`include/litebird/dom/`)

- `TreeWalk.hpp` has the two walks everything else is built from.
  `for_each_descendant(root, visit)` visits a subtree in tree order and keeps
  its place in a vector, so its depth is limited by memory, like the tree
  itself. `subtree_elements(root)` returns the elements as a list, for walks
  that call into script (custom element callbacks) and may find the tree
  changed under them.
- `~Node` releases its subtree with a loop. A node destroyed while another is
  being destroyed puts its children on a queue (`ReleaseQueue`) instead of
  destroying them, and the outermost destructor works through the queue one
  list at a time. No destructor runs inside the destructor of its parent.
- `text_content`, `inner_markup`, `get_element_by_id`, the tag, class and
  selector searches and `notify_subtree_connected` are loops. `dump` prints
  `kMaxDumpDepth` (1,024) levels and then one line saying how many nodes it
  left out and how deep they go.
- The cycle check in `append_child` walks up from the new parent and down
  from the node being inserted in turn, and stops when either walk ends. From
  the parent alone it cost the depth of the tree for every insertion, which
  made a chain built by script quadratic: 40,000 levels took 13 s.
- `take_children()` detaches all children in one step and
  `replace_children(nodes)` swaps them. `textContent`, `innerHTML`, fragment
  insertion and the fragment parser use them. Removing children from the
  front one at a time made `innerHTML` with 100,000 elements take 9 s.

### 6. Limits on what a value can ask for

- **Bitmaps** (`include/litebird/render/RasterBudget.hpp`). A `<canvas>` or
  `<svg>` is drawn into a bitmap. One bitmap may be 16,384 pixels a side and
  16 million pixels; all that are alive, 64 million. An element over the
  budget keeps its size in layout and is blank, and gets a bitmap when it is
  resized to something that fits. Before, `<svg width="100000"
  height="100000">` was a `std::bad_alloc`.
- **`calc()`** nests 100 levels (`kMaxCalcDepth`, Blink's number) and is
  invalid beyond that. A `calc(` that is never closed ends where the value
  ends; it used to read past it.
- **`var()`** is resolved in one pass over the value. Each unclosed `var(`
  used to scan the rest of the value again.

### 7. Two loops in layout

Both were found by pages that the nesting limit had turned from deep into
long.

- **Inline children.** After a block breaks its inline content into lines,
  each text child is told which of its words are on which line and each image
  where it was put. Every child searched every fragment of every line; one
  pass over the fragments now collects it for all of them. A paragraph of
  100,000 images went from 35 s to 1.3 s.
- **Floats of no height.** Placing a float looks at the band of every float
  before it in the box. A float with no height left a band that no position
  falls inside. It is no longer kept as a band, and what it was still read
  for is noted as the floats are placed: `clear` goes below it and a box that
  contains its floats reaches down to it. 200,000 floats of no size in one
  box went from 20 s to 1.2 s.

### 8. How this is verified

| Check | Result |
| :--- | :--- |
| `make deep`: 145 generated pages, each run three ways (the DOM, layout and paint dumps with a bitmap, in one process; the terminal renderer; for a page without script, the parser alone), then 300 random pages at the nesting limit | 1,292 runs, 0 failed, under 10 minutes on two cores; the slowest run takes 21 s of the 60 allowed. On `main`, 264 of the 392 runs over the generated pages fail: 139 by `SIGSEGV`, 106 out of time, 8 aborted, 7 out of memory, 3 with a wrong result, 1 with more than 4 GB of output |
| The same pages in the ASan + UBSan build, for memory errors: 10,000 levels on a 2 MB stack (`make asan-deep`, part of `make asan`) | 842 runs, 0 failed, no sanitizer report. The same at 50,000 levels, here and in CI, where it added 14 minutes to the job |
| ASan + UBSan: unit tests, the JavaScript corpus, both fuzzers (`make asan-core`) | 95 / 95; corpus 839 / 845, as before; 400 + 400 fuzz runs; no report |
| A chain of 1,000,000 nodes walked and released in a unit test, in both builds | passes with 8 bytes of stack a level, less than a function that calls itself uses |
| Random pages at the limit, from the tags the tree builder treats specially | 4,500 over three seeds: no crash, no element deeper than 512 |
| Unit tests (`make test`) | 95 / 95; seven are new and one was rewritten |
| html5lib tree construction | 1771 / 1792, one more than before (an `<a>` inside an `<a>`) |
| 1,746 pages (every distinct html5lib input and the 16 examples) through five output modes, `main` against this | 8,730 outputs; 4 differ, all one page, whose tree now equals parse5's |
| `make diff-trees` (new): random pages of 3 to 39 misnested tags parsed here and by parse5, an independent parser, trees compared | 1,489 of 1,500 equal, 1,478 on `main`. Over four seeds 5,948 of 6,000, 5,919 on `main`, and no page that was equal there differs now. The 52 that differ are five bugs older than this phase (AUDIT.md, finding 29) |
| JavaScript corpus | 840 / 845, the same five failures as before; 845 / 845 under Node 22 |
| Bugs planted in the float change, one at a time | 6 of 6 caught by a unit test |
| 915 runs over 305 pages of random floats, before and after the float change | identical output |

`make deep` is a CI job of its own. The random pages are generated from a
fixed seed (`--seed`), so a failure names a page that can be generated again.
In its first two runs that job took 8 and 12 minutes, 7 and 10 of them in
`make deep`. It runs beside the sanitizer job, which went from 9 minutes to
11, and the pipeline as a whole from 11 minutes to 13.

### 9. What is still open here

AUDIT.md, finding 28, has the list with measurements. In short:

- **A block inside an inline box is not laid out**
  (`<a href="#">x<p>y</p></a>` paints only "x"). Fixed in Phase 36: the inline
  is split around the block, so the block paints. The nesting limit makes it
  easier to meet.
- **Many floats with a size in one box** used to cost far more than their
  number: 10,000 took 46 s, and past 512 slide steps a float was placed
  where it stood, running out of the box to the right. Float placement was
  rewritten as a sweep of the band edges: the float lands at the exact bottom
  edge of the float in the way, at any row, with no step cap; 10,000 sized
  floats run end to end in about 5 s. (AUDIT.md, finding 28.)
- **A tree a script built deeper than the stack reaches** is laid out as far
  as the stack goes.
- **Smaller quadratic loops**: removing the first child of very many, one at a
  time; very long runs of spaces inside a selector or a CSS function; an
  element with tens of thousands of attributes.
- **Memory.** A word and an inline element with a word in it cost about
  13 KB between them (1.3 GB for 100,000), because each box, line item and
  fragment copies a 920-byte style (AUDIT.md, finding 12).

---

## Phase 26 Architecture: The Tree Builder Against Pages Nobody Wrote

### 1. Why this phase existed

A corpus says a parser is right about the inputs somebody thought of. Phase
25 added a second kind of check, `tools/conformance/diff_trees.py`: pages of
a few dozen random tags, parsed by Litebird and by parse5, an independent
parser, with the trees compared. Its first run found five bugs in 6,000
pages and Phase 25 left them (AUDIT.md, finding 29). This phase fixed them,
and then gave the pages more to contain. The second step found far more
than the first: 408 of 6,000 pages got a tree the standard does not ask for
(AUDIT.md, finding 30), and the commonest reason has no case in the corpus
at all.

### 2. Namespaces: what an end tag may close

The stack of open elements holds HTML, SVG and MathML elements, and the
standard's rules nearly always mean an HTML one when they name an element.
Two things were wrong.

- **Which elements are "special".** Every SVG and MathML element counted.
  The standard names nine of them: MathML `mi`, `mo`, `mn`, `ms`, `mtext`
  and `annotation-xml`, SVG `foreignObject`, `desc` and `title`
  (`is_special_foreign_element()`). The same nine bound a scope. An `<svg>`
  is an ordinary element: the adoption agency algorithm walks past it, so
  `<a><svg></a>` closes both and moves nothing.
- **Names were compared without the namespace.** `</title>` closed an SVG
  `<title>`; an SVG `<rt>` got an implied end tag; a table row was put into
  an SVG `<template>`. `html_name(element)` returns the tag name of an HTML
  element and an empty string for any other, and every rule that names an
  HTML element now asks it.

`index_open_element()` puts the nine into the scope and special indexes of
Phase 25 (section 4 there), so those indexes are exact with SVG and MathML
open. Before, a count of open foreign elements switched every query to a
walk down the stack as soon as there was one; the count and the walks are
gone. `open_element_closed_by_end_tag()` is the standard's "any other end
tag" loop answered from the index, and `any_other_end_tag()` is the one
place that acts on it: the "in body" rules call it, and so does the adoption
agency algorithm when it finds no formatting element.

### 3. Four rules that were read wrongly

- **`</button>`** closes its element in scope whatever is open inside it,
  like `</div>`. It was missing from that list.
- **"Noah's Ark".** A fourth identical formatting element pushes the
  earliest of the three out of the list of active formatting elements.
  `push_formatting_element()` removed the latest.
- **The adoption agency algorithm** (`adoption_agency()`, now `void`). With
  no formatting element of the token's name after the last marker it does
  what "any other end tag" does; it used to return as if it had handled the
  token. The furthest block is the first special element after the
  formatting element, found from the special index.
- **Text in a table.** The rule that collects text to see whether it is all
  whitespace applies when the current node is a `table`, `tbody`, `tfoot`,
  `thead`, `tr` or `template`, not whenever the insertion mode is "in
  table". U+0000 is dropped before the text is looked at, so it no longer
  turns whitespace into "not whitespace".

The frameset-ok flag had two: a `<noscript>` with scripting enabled does not
touch it, and the body made for the first thing after the head sets it to
"ok". And a second `<head>` start tag is ignored; it used to close the head.

### 4. A start tag that closes itself

`<br/>`. The standard says to "acknowledge the token's self-closing flag",
which means only: do not report it as an error. `acknowledge_self_closing()`
popped the current node. For a void element that had already been popped,
so the parent was closed: the paragraph around a `<br/>`, the list item
around an `<img ... />`, the head around a `<meta ... />`, after which the
rest of the head and the body were attached to the document, outside
`<html>`.

The function and its nine calls are removed. Nothing replaces them, because
nothing has to happen: a void element is popped where it is inserted, a
slash on any other HTML start tag is ignored, and in SVG and MathML the
foreign-content rules pop a self-closing element themselves.

Of the 408 wrong pages, 292 come right with this change alone, and the
corpus score does not move, because none of its 1,792 inputs writes a void
element with a slash. `examples/nextgen_showcase.html` does, and was parsed
wrongly.

### 5. Templates and fragments

- **"Parsing template contents"** (`parsing_template_contents()`) is true
  when a `<template>` is open or the fragment is being parsed for one. The
  form element pointer is then neither read nor set, so a form may be
  nested there and `<template><table><form>` keeps its form. (The standard
  changed the table case in September 2026.)
- **A fragment for a `<template>`** starts with "in template" on the list
  of template insertion modes. The list was empty, `replace_template_mode()`
  read its last entry, and the process ended with `SIGSEGV`.
- **A fragment for an element whose content is text** (`<script>`,
  `<style>`, `<textarea>`, `<title>`): no start tag was seen, so there is no
  "appropriate end tag", and `</script>` in it is text. A fragment for a
  `<noscript>` is text only with scripting enabled.
- **CDATA** is allowed wherever the adjusted current node is not an HTML
  element (`allow_cdata_in_foreign_content()`, asked before each token),
  which includes the top of a fragment parsed for an SVG element.

### 6. `innerHTML` parses for its element

The fragment parser takes a context element, and the corpus runner gives it
one for each of the 192 fragment cases. `Element::set_inner_html`, which is
what `element.innerHTML = ...` in a script calls, went through a helper that
said `<body>` for every element. A `<td>` is dropped in a body, so
`table.innerHTML = "<tr><td>1</td><td>2</td></tr>"` left the text "12" in
the table.

`HtmlParser::parse_inner_html(context, html)` parses for the element it is
given. `HtmlTreeBuilder::parse_fragment(html, context)` builds a stand-in
for it, so the parser cannot reach the tree the element is in, and takes
over the three things about the element that change the result:

| What | Why |
| :--- | :--- |
| Its name and namespace | the insertion mode to start in (rows in a `<tbody>`, cells in a `<tr>`), text content for `<textarea>` and `<style>`, SVG elements inside an `<svg>` |
| The `encoding` attribute | a MathML `annotation-xml` holds HTML when it says `text/html` |
| Whether a `<form>` is above it | the form element pointer: a `<form>` in the fragment is then ignored |

The form is looked for only when the markup has the letters "form" in it:
only a `<form>` or a `</form>` asks, and the walk to the root costs the depth
of the tree. Made at every assignment it turned a script that nests 30,000
levels through `innerHTML` from 0.2 s into 44 s. `make deep` has that page
and it still passed, at 48 of its 60 seconds; the line naming the slowest
run is what showed it, and a unit test bounds the loop now. Markup that does
hold a form still makes the walk (AUDIT.md, finding 28).

A shadow root still parses in a `<body>` context: every element that can
host one amounts to that.

### 7. The tokenizer and the dump

- `&#X` with no digits after it is text as typed; it came out as `&#x`.
- A comment the page ends in (`<!--x--`) does not keep the dashes that had
  begun to close it.
- `=` where an attribute name should begin starts the name: `<a =>` has an
  attribute called `=`.
- The doctype is dumped where it stood among the comments before `<html>`
  (`Doctype::preceding_children`); the tree was right, the dump was not.
- An empty file is parsed like any other page. It used to exit with status
  1 and no tree.

### 8. A reference that can be corrected

parse5 8.1 is itself behind the standard in nine rules (AUDIT.md, finding
30, has the table), so after the fixes 49 of 6,000 pages still differed
from it. A ceiling of 49 would let a new bug in behind an old excuse.
`diff_trees.py --corrected` instead compares with a copy of parse5 in which
those nine rules are changed: `CORRECTIONS` in the script is 13 edits, each
a piece of parse5's source that must occur exactly once and its
replacement, applied to a copy in `build/parse5/corrected`. The script
refuses to run them against any other version than the one they were
written for. `make diff-trees` uses the corrected copy and allows no page to
differ; each line it prints also says how many pages equal parse5 as
published.

The cost is stated in the script and in the audit: on those nine rules both
sides were written here from the same text. For four of them the
web-platform-tests copy of the corpus has 15 cases, which parse5 fails as
published and passes corrected. For two more Blink's source agrees with the
text. Three have the text alone, and on one of those (whitespace directly in
a `<template>` that is in a table mode) Blink does what parse5 does.

### 9. How this is verified

| Check | Result |
| :--- | :--- |
| `make diff-trees`: 1,500 pages of each of four kinds (plain; with the extras; those as fragments in 45 kinds of element; those with scripting disabled) against the corrected parse5 | 6,000 of 6,000 trees equal; 5,592 on `main`. Against parse5 as published 5,951, the 49 others being the nine rules |
| The same pages of finding 29 (plain, four seeds) | 6,000 of 6,000; 5,947 on `main` |
| A wider run: five lists of elements, with and without the extras, documents and fragments, scripting on and off, three seeds | 180,000 pages, none differs from the corrected parse5; 1,548 differ from parse5 as published |
| html5lib tree construction (`make conformance`) | 1781 / 1792, ten more than before; the 11 left are all `<select>` |
| The web-platform-tests copy of the corpus, 1,973 cases (not vendored) | 1,868, from 1,851; the 105 left are 88 that need processing instructions, 6 that need a script run during parsing, 11 `<select>` |
| parse5 itself against the vendored corpus (`--check-reference`) | 1,764 of 1,792 as published and corrected alike; the 28 are `<select>` |
| Unit tests (`make test`) | 103 / 103; eight are new |
| 1,746 pages (every distinct html5lib input and the 16 examples) through five output modes, `main` against this | 8,730 outputs; 37 differ, on 9 pages: 8 inputs of corpus cases that pass now, and `examples/nextgen_showcase.html`, whose `<input ... />` no longer closes the row it stands in |
| JavaScript corpus | 840 / 845, as before |
| `make fuzz` | 400 + 400 runs, 0 findings |
| `make deep` | 1,292 runs, 0 failed; the slowest takes 17 s of the 60 allowed |
| `make asan` (ASan + UBSan): unit tests, the JavaScript corpus, both fuzzers, the deep pages at 10,000 levels | 103 / 103; 839 / 845, as before; 400 + 400 runs; 842 runs, 0 failed; no sanitizer report |

`make diff-trees` is part of the conformance job in CI from this phase on,
next to a floor on the corpus score that is now the exact number of passing
cases (1,781) instead of 98%.

### 10. What is still open here

- **`<select>`**: the 11 corpus cases. The standard's new rules for its
  content (customizable `<select>`, `<selectedcontent>`) are not
  implemented, and parse5 does not have them either, so the random pages
  leave `<select>` out.
- **The corpus has moved to web-platform-tests** and grown. Vendoring it
  needs the runner to read `#description` sections, and passing it needs
  processing instructions (`<?x?>` is a node in the standard now, a comment
  here) and scripts that run while the page is parsed.
- **Writing markup.** `innerHTML` read back does not escape `<` and `&` in
  text and writes `<br />`. The parser reads that correctly now, but text
  that holds `<b>` still reads back as an element.
- **One place follows the text against two implementations**: whitespace
  directly in a `<template>` that is in a table mode is collected like text
  in a table, as the standard says; parse5 and Blink treat it as ordinary
  text. The difference is where a text node sits inside a template.

---

## Phase 27 Architecture: The DOM a Script Sees

### 1. Why this phase existed

Phase 23 rebuilt the language against a corpus recorded from Node.js and left
the DOM as it was (Known gaps, 9): `getElementById`, one-word selectors,
`appendChild`, `innerHTML`, events. A script that did
`document.querySelector('ul li')`, `el.classList.add(...)` or
`new CustomEvent(...)` stopped with an error on its first line, and most
scripts on real pages do one of those on their first line.

The reason was structural. A DOM node was a *kind of value*
(`JsType::DomNode`), and what it could do was a chain of name comparisons in
`JsValue::get_property`. Nothing was an object: there was no
`HTMLElement.prototype` to inherit from, extend or test with `instanceof`,
and every new member was another branch in one function. So the phase started the way Phase
23 did, with a scoreboard from outside (section 9), and then replaced the
bindings instead of adding branches.

### 2. Three layers

```
src/dom/, include/litebird/dom/      the tree: Node, Element, Document, attributes,
                                     selectors, form state, the serializer.
                                     Knows nothing about scripts.
src/js/Dom*Bindings.cpp              the interfaces of the DOM and HTML standards,
                                     over that tree. 13,700 lines, 14 files.
src/js/ (Heap, JsValue, JsOps)       the engine: what an object is.
```

**The tree** gained what the interfaces need and the old bindings did
without: an owner document on every node, attributes as an ordered list with
namespaces (`AttributeList.hpp`), a selector engine (`Selector.cpp`), the
values and dirty flags of form controls and constraint validation
(`FormState.cpp`), and the standard's fragment serializer (`Node.cpp`). It
tells whoever is listening what changed through `DomHooks`, a table of
function pointers (`Node.hpp`): children changed, an attribute changed, an
element was connected or disconnected. The bindings fill the table in; the
tree calls nothing in the bindings by name, and a tree with no interpreter
attached (the parser's corpus run, a fragment) pays for a null check.

**The engine** gained one thing: an object can carry native state and answer
for its own properties (`JsExotic` in `Heap.hpp`). That is what a
`NodeList` (indexed), a `DOMStringMap` (named), `document` and `window`
(named access) are. `JsType::DomNode` is gone.

### 3. One object per node (`DomBindings.cpp`)

`wrap(realm, node)` returns the node's object, creating it the first time.
The object's prototype is chosen from the node: `HTMLDivElement.prototype`
for a `<div>`, `HTMLUnknownElement.prototype` for a tag nobody knows, the
class of a custom element. From then on:

- The object owns the node (`JsObject::dom_node`, a `shared_ptr`). The node
  points back with a plain pointer (`Node::script_object`), cleared when the
  collector frees the object.
- **A node that has an object, its parent has one too.** Marking an object
  marks the objects of its parent, its children and its document
  (`trace_wrapper`). So holding any node of a tree keeps every object of the
  tree, and `el.myData = ...` is still there when the script finds `el`
  again by another route. The rule is kept by `wrap` (ancestors first, in a
  loop: a tree can be as deep as memory allows) and by a hook for when
  native code puts a node that has an object under one that has none.
- Objects that are not nodes (events, lists, style declarations, ranges)
  carry their C++ state as the object's `JsExotic`. A binding recognises
  what it was called on by the dynamic type of that state (`this_state<T>`),
  never by a property a script could set, and throws `Illegal invocation`
  otherwise: `Element.prototype.getAttribute.call({})` is a `TypeError`.

### 4. Interfaces (`DomBindings.hpp`)

An interface is a row in a table: its name, its parent, a function that puts
its members on a prototype, and a constructor or none (`InterfaceDef`,
`register_interface`). A `Realm`, one per interpreter, builds a prototype
and its constructor the first time either is asked for, parents first. The
window finds interfaces by name the same way, when a script first says
`HTMLElement`: an interpreter that never touches the DOM builds none of it,
and a page that uses ten interfaces builds ten.

Members are native functions made by `make_function`, defined as accessors
and methods with the attributes WebIDL gives them. `ElementMembers` covers
the bulk: a reflected string, boolean, integer, enumerated keyword or URL
attribute is one line. The constructors can be subclassed
(`class Card extends HTMLElement`), which is what custom elements need:
`construct_as_super` in the engine lets a native constructor return the
object the derived class is building.

### 5. What the files hold

| File | Interfaces |
| :--- | :--- |
| `DomNodeBindings.cpp` | `Node`, `CharacterData`, `Text`, `Comment`, `DocumentFragment`, `DocumentType`; insertion with the hierarchy checks, adoption, `cloneNode` |
| `DomElementBindings.cpp` | `Element`: attributes, `classList`, selectors, `innerHTML`/`outerHTML`/`insertAdjacent*`, shadow roots, `DOMRect` |
| `DomCollectionBindings.cpp` | `NodeList`, `HTMLCollection`, `DOMTokenList`, `NamedNodeMap`, `Attr`, `DOMStringMap`: live, indexed and named |
| `DomHtmlBindings.cpp` | `HTMLElement` and the HTML element interfaces: reflected attributes, links, images, tables, templates, dialogs, canvas |
| `DomFormBindings.cpp` | Form controls, `form.elements`, validation, submission and reset, `FormData`, focus |
| `DomEventBindings.cpp` | `EventTarget`, `Event` and 27 subclasses, dispatch, `on*` handlers, `AbortController` |
| `DomStyleBindings.cpp` | `CSSStyleDeclaration`: `el.style` as a live declaration block, and `getComputedStyle` with a cascade of its own (section 8) |
| `DomDocumentBindings.cpp` | `Document`, `DOMImplementation`, `DOMParser`, `XMLSerializer`, cookies, named access |
| `DomTraversalBindings.cpp` | `Range` and `Selection`, which follow the tree (section 9), `TreeWalker`, `NodeIterator`, `NodeFilter` |
| `DomObserverBindings.cpp` | `MutationObserver`, custom elements and their reactions |
| `DomFileBindings.cpp` | `Blob`, `File`, `FileReader` |
| `DomUrlBindings.cpp` | `URL`, `URLSearchParams` |
| `DomWindowBindings.cpp` | `Window`, `Location`, `History`, `Navigator`, `Storage`, `XMLHttpRequest`, `matchMedia`, error reporting, the load events |

### 6. When things run

**Custom element reactions** wait for the DOM call that caused them to
return, then run before the script goes on, which is what `[CEReactions]`
means in the standards: `el.setAttribute('a', 1); el.setAttribute('a', 2)`
gives two `attributeChangedCallback`s, each after its own call, never one in
the middle of the other. Every native DOM function runs inside a
`ReactionScope` (in `make_function`), which costs a counter, a comparison
and a `try` block when the queue is empty.

**Mutation observers** get their records in a microtask. There is one
microtask queue, the engine's, so a promise callback queued before a
mutation runs before the observer's callback.

**Events** are dispatched by `DomEventBindings.cpp` for scripts and for
native code alike (a hook: the shell's click reaches script listeners).
The path is computed first; listeners are objects or functions, with
`once`, `passive`, `capture` and `signal`; an exception in one is reported
to `window.onerror` and the next still runs. After the dispatch comes the
default action: a click toggles a checkbox (and puts it back if a listener
called `preventDefault()`), follows a label to its control, submits a
form, moves the page to a fragment.

**A page's scripts run while it is parsed.** `HtmlTreeBuilder` reports each
`<script>` when its end tag has been read (`set_script_handler`), and
`JsInterpreter::load_page()` runs it there, as `document.currentScript`,
after a microtask checkpoint. The script sees the page up to itself:
`document.scripts.length` in the first script is 1, and
`scripts[scripts.length - 1]` is the script that asks. What it does to the
tree is there for the rest of the parse; the parser keeps its own
references to the open elements and goes on inserting into them wherever
they now are, as the standard says. The CLI and the shell both load pages
this way. Then the document is `interactive`, `DOMContentLoaded` is fired,
and `load`.

### 7. URLs (`src/net/UrlParser.cpp`)

Addresses were resolved by cutting strings: `?q=1` against
`/app/page.html?tab=2` gave `/app/?q=1`, `//cdn.test/lib.js` stayed on the
page's host, `mailto:` was read as `https:`, and `new URL('nope')` did not
throw. `Url` is the URL Standard's state machine, with the state override
its setters use: special schemes, opaque paths, IPv4 and IPv6 hosts in every
spelling, the percent-encode sets, `file:` URLs, origins. Everything that
has an address goes through it: `a.href` and the parts of a link,
`location`, `form.action`, `baseURI` with `<base href>`, `pushState`, and
the `URL` object, whose `searchParams` is a second view of the same record.
The page's own address is normalised when the window is made.

Navigation is only within the page: assigning to `location.hash`, following
a link to a fragment, `location.href = '#x'`. Those change the address at
once and fire `popstate` and `hashchange` from a task. Anything else would
be loading another page, and does nothing.

### 8. The computed style (`DomStyleBindings.cpp`)

`getComputedStyle` has a cascade of its own. It is not the layout engine's
(`StyleResolver`), which resolves into the typed values layout needs: a
script wants text back, for every property there is, and wants it as the
standards write it.

For one element, `compute_block` collects the declarations that apply and
orders them by weight: what the HTML standard's Rendering section gives the
element (a heading is bold and `2em`, a list has `40px` of padding, a link
is underlined), then the rules of the document's `<style>` elements by
specificity and position (`SelectorList::matching_specificity`: ids,
classes, types; `:is()` and `:not()` count as their most specific argument,
`:where()` as nothing), then the style attribute, then the `!important`
ones of the same three. Shorthands are taken apart on the way, so
`overflow: hidden` answers for `overflowY` and `font: 12px/1.5 Arial` for
`fontSize`.

What a script reads is the computed value of that:

- a property nobody declared has its inherited value or, from a table of
  182, its initial one;
- a colour is `rgb()` or `rgba()`: names, hex, `hsl()`, `currentcolor`;
- a length is in px: `em` and `rem` through the font size, `pt`, `in`,
  `cm`, `mm`, `pc`, `q`, and `calc()` of those. A font size is the chain of
  its ancestors' (`2em` of `150%` of `larger` of `20px`), and so is
  `bolder`;
- a border or outline that is not drawn is `0px` wide, one that is drawn is
  a whole number of pixels;
- a shorthand is put together from the computed values of its parts.

Everything that goes up the tree (inheritance, the font size, the weight) is
a loop, and the root of the tree is found once for each question: asking
about an element 20,000 levels down takes 100 ms and no stack.

What it does not know is layout. `width: auto` and `50%` stay what they
are, where a browser gives the used value in px. Section 12 has the rest.

### 9. Ranges that follow the tree (`DomLive.hpp`)

A `Range` is two boundary points, each a node and an offset. The standard
says where each goes when the tree changes after the range was made: nodes
inserted before a boundary push it on, a boundary inside something that is
removed lands where that was, text that is edited or split takes its
boundaries along. A `NodeIterator` whose reference node is removed steps
out of what is going away. A selection is a range, so it follows too.

The realm keeps a weak list of the ranges and iterators script can still
reach. The places where the tree changes tell it: the `children_changed`
hook of the tree, `replace_data()` behind `insertData`/`deleteData`/
`replaceData`, `splitText()`, `normalize()`. With no range alive each of
those is one lookup. With some, a change costs a walk up from each boundary
(to learn whether it was inside what went away), which is why the list is
cleared of ranges the collector has freed as soon as one is met: a page
that made 100,000 ranges and kept one pays for the one.

`insertNode()` and `surroundContents()` are the standard's steps on top of
that: text is split at the boundary, and a node the range cuts through that
is not text is an `InvalidStateError`.

### 10. What it costs

Measured against the binary of `main` on one machine, a page with an empty
body and a script (milliseconds, the better of two runs):

| | `main` | Phase 27 |
| :--- | ---: | ---: |
| Create 20,000 elements, give each an id and text, append them | 70 | 66 |
| `getElementById` for each of the 20,000 | 5,640 | 16 |
| `textContent` of each of the 20,000 children | 15 | 14 |
| `getAttribute` 200,000 times | 122 | 100 |
| 20,000 clicks on an element with one listener | 102 | 155 |
| Remove the 20,000 children, last first | 97 | 84 |

A click is slower: it now builds an event object with a prototype, computes
the path, and runs the default action. A lookup by id was a walk over the
whole document; these bindings were slower still (7.4 s), which is how it
was noticed. `Document::get_element_by_id` now answers from an index, built
on the second lookup after the tree last changed and thrown away when it
changes again. The first lookup after a change still walks, so a script that
changes the page between lookups pays what it always did. Nothing else was
measured: there is no benchmark of the DOM in the repository, and these six
lines are not one.

`Element` grew by one pointer (`m_rare`, for what few elements have: a
namespace that is neither HTML, SVG nor MathML, a prefix). The binary is
7.9 MB, from 3.7 MB: the bindings are templates over lambdas, one
instantiation for each member.

### 11. How this is verified

**The scoreboard.** `tests/dom-conformance/` is 21 files, 572 cases. A case
is a page (`#html`), a script (`#code`) and what the script prints
(`#expect`). `tools/conformance/run_dom_conformance.py` loads the page with
`litebird --run-script`, which runs the script after the page's own, fires
the load events and runs timers to the end on the event loop's virtual
clock, without layout or painting. Every `#expect` was written by
`--record`, which runs the case in jsdom 31.0.0 (`jsdom_run.mjs`), and
`--reference` runs them all in jsdom again: CI does, so an expectation
cannot be bent. `everyday.dat` was written first, from *You Might Not Need
jQuery*, and is not edited.

jsdom is the reference, not the authority. Where it differs from the
standards (the corpus README names the places), the answer is not in the
corpus, and a unit test asserts the standard's:
`dom_follows_the_standard_where_jsdom_does_not`,
`computed_style_answers_for_every_property`, `ranges_follow_the_tree`.

**What the scoreboard did not see.** The corpus was written by the hand
that wrote the code, and a case nobody thought of protects nothing. At 557
of 557:

- `getComputedStyle(el).position` was `""`, and so was every other property
  nobody had declared: no case read one. `#id { color: red }` lost to a
  later `p { color: blue }`: no case had two rules for one element. A
  heading had no font size.
- A range kept its boundaries where they were put, whatever happened to the
  tree afterwards, and `surroundContents()` inside one text node put the
  wrapper after the text.

Both were found by running pages that were *not* in the corpus through
jsdom and Litebird side by side
(`tools/conformance/compare_with_jsdom.py`), and each became cases: seven
for the computed style, eight for ranges. The same comparison says what is
still different, and that list is section 12. It is the list of what was
looked at, not of what is wrong.

| Check | Result |
| :--- | :--- |
| DOM corpus (`make dom-conformance`) | 572 / 572; 61 on `main`. The everyday probe 68 / 68; 13 on `main` |
| The corpus in jsdom (`--reference`) | 572 / 572 |
| URL data of web-platform-tests (`make url-conformance`): 904 parsing cases, 284 setter cases | 873 and 277, 1150 of 1188; 0 and 2 on `main`, where a URL object had no `username`. All 1,188 since host names outside ASCII go through Unicode's UTS #46 |
| Unit tests (`make test`) | 110 / 110; seven are new (the URL parser, URLs in the DOM, page scripts, where jsdom is not the standard, lookups by id, the computed style, ranges), and five old ones asserted what no browser does and were corrected |
| html5lib tree construction (`make conformance`) | 1781 / 1792, as before |
| JavaScript corpus (`make js-conformance`) | 840 / 845, as before |
| `make diff-trees` | 4 × 1,500 random pages against parse5 8.1.1, 0 differ, as before |
| `make fuzz` (in the sanitizer build) | 400 parser mutations and 400 pipeline runs, 0 findings |
| `make deep` | 1,292 runs over 445 pages at 200,000 levels, 0 failed. The slowest takes 19 s of the 60 allowed; the same page takes 21 s with the binary of `main` |
| `make asan` (ASan + UBSan): unit tests, the three corpora, both fuzzers, the deep pages at 10,000 levels | 110 / 110; JavaScript 839 / 845, as before; DOM 572 / 572; URL 1,188 / 1,188; 400 + 400 runs; 842 runs over 295 pages, 0 failed. No report |

The DOM corpus has a job of its own in CI (the reference check, the
scoreboard with a floor of 572, the URL data with a floor of 1188), and
`make asan` runs both corpora in the sanitizer build.

### 12. What is still open here

- **The corpus measures what jsdom does.** jsdom lays nothing out and loads
  no second page, so nothing checks geometry, scrolling or navigation. In
  the CLI the layout is made once, after the scripts, so
  `getBoundingClientRect()` and `offsetWidth` are zero while they run.
- **The computed style is computed, not used.** `width: auto` and `50%`
  stay; a browser gives px. A value of several parts is left as declared
  (`box-shadow: 0 0 1em red` keeps its `em`), `var()` is not substituted,
  and the shorthands that are not taken apart (`transition`, `animation`,
  `grid`, `background` beyond its colour) leave their parts `""`: not
  known, which is better than an initial value that would be wrong. The
  defaults are the HTML standard's, and the layout engine paints with a
  sheet of its own: a heading is `2em` to a script and 28px on the screen.
- **`el.style` keeps most shorthands whole.** `style.flex = '1'` leaves
  `style.flexGrow` empty; only margin, padding and the border family are
  taken apart. `length` and `item()` count a shorthand once where a browser
  lists its longhands. Values are not rewritten the way CSSOM writes them:
  `url(a.png)` stays unquoted, `translate( 10px , 20px )` keeps its spaces,
  `webkitTransform` is not `transform`.
  `tests/dom-conformance/open/inline-style.js` shows each.
- **`DOMParser` knows HTML only.** `parseFromString(text,
  'application/xml')` or `'image/svg+xml'` parses the text as HTML: there
  is no XML parser, so no XML document, no `parsererror`, no `responseXML`.
  The answer is wrong and nothing says so. (Phase 28 wrote the parser.)
- **Date and time inputs are text inputs.** `type="date"`, `time`,
  `month`, `week` and `datetime-local` keep whatever is assigned, and have
  no `valueAsDate`, `valueAsNumber` or `stepUp()`. `number` and `range`
  have all of it.
- **SVG elements have no SVG DOM.** They are `SVGElement` with the members
  of `Element`: `className` is a string, there is no `viewBox`, no
  `getBBox()`. `tests/dom-conformance/open/dates-and-svg.js` shows the
  last two; until Phase 28 it showed all three.
- **Nothing is fetched.** A `<script src>` or a stylesheet link is not
  loaded (a script inserted with a `src` gets an `error` event).
  `document.write` does nothing.
- **Style sheets are not objects.** `document.styleSheets` lists the
  `<style>` elements and nothing more: no `cssRules`, no `insertRule`. A
  script that writes its rules that way changes nothing; one that appends
  a `<style>` element is fine.
- **Absent**: `TextEncoder`/`TextDecoder`, `crypto`, `indexedDB`,
  `BroadcastChannel`, `MessageChannel`, `Request`, `EventSource`,
  `document.evaluate` (XPath), Web Animations (`el.animate`),
  `ElementInternals`, constructable stylesheets, the clipboard, the ARIA
  reflections (`el.ariaLabel`), and whatever needs typed arrays
  (`Blob.arrayBuffer()`, `FileReader.readAsArrayBuffer`).
- **The parser upgrades a custom element when it is connected** instead of
  constructing it before it is inserted. The element and its callbacks are
  the same; a mutation observer hears of the insertion one callback earlier
  than in a browser.
- **A worker has no DOM**, as in a browser, but its `window`-less global
  still comes from the code that was there before this phase.

---

## Phase 28 Architecture: XML

### 1. Why this phase existed

`new DOMParser().parseFromString(text, 'application/xml')` gave back an HTML
document: `documentElement.nodeName` was `HTML` whatever the text held, a
CDATA section became a comment, and text that was not well formed gave a tree
like any other. There was no XML parser. `XMLHttpRequest` was a stub whose
every request ended in `error`, so `responseXML` could only be null. Phase 27
listed the first under "known to be wrong" (AUDIT.md, finding 31): a wrong
answer, not a missing one, and nothing told the script.

XML is a small language with a hard edge. A parser must refuse what is not
well formed, with no recovery, so every one of its rules can be tested from
outside: a document is accepted or it is not. That made it possible to start
with the references and write the parser against them:

| Reference | What it judges |
| :--- | :--- |
| The W3C XML Conformance Test Suite (2,585 documents) | Accepting and refusing, and the canonical form of what is accepted |
| Expat 2.5.0 and libxml2 2.14.6, on documents nobody wrote | The same, where the suite has no document |
| web-platform-tests, `domparsing/` and `dom/nodes/` (76 files) | The DOM an XML document has, and the serializer |
| jsdom 31.0.0 (57 recorded cases) | The same, and `XMLHttpRequest` |

No two of them agree everywhere, and section 7 says where each is not the
truth.

### 2. The parser (`src/xml/XmlParser.cpp`)

XML 1.0, Fifth Edition, with Namespaces in XML 1.0: what a browser's parser
is. It does not validate, and it reads nothing but the text it was given.

- **It knows nothing of the DOM.** `parse_xml(text, handler, error, options)`
  reports to an `XmlHandler`: `doctype`, `start_element` (the name and the
  attributes with their namespaces resolved), `end_element`, `characters`,
  `cdata_section`, `comment`, `processing_instruction`. One handler builds a
  tree (section 4); two others write the canonical form the conformance
  suite compares and the event dump the differential run compares
  (`XmlTools.cpp`, `--xml-canonical` and `--xml-events` on the command
  line).
- **The internal subset is read, and used.** Entities declared there are
  expanded; an attribute's declared type decides how its value is normalized
  (3.3.3: anything but `CDATA` is trimmed and collapsed) and a declared
  default is added to an element that does not have the attribute, before
  namespaces are resolved, because a default can be an `xmlns`. Element
  types and notations are checked for form and then not used: nothing is
  validated.
- **Nothing outside the document is read.** An external entity, an external
  subset and a parameter entity that points at a file are never fetched: no
  code path opens anything. What the standard asks of a parser that does
  not read them follows from that: declarations that come after a reference
  to an external parameter entity are not used (what was not read could
  have changed them), and in a document that has an external subset or
  refers to a parameter entity, an entity that is not declared is no error
  of form (4.1); it stands for nothing. `standalone="yes"` takes both back.
- **`&nbsp;` in XHTML.** The HTML standard asks a browser to treat eleven
  public identifiers (the XHTML and MathML DTDs) as declaring the entity
  names of HTML. A DOCTYPE with one of them does; any other document has the
  five entities of XML and its own.
- **Errors end the parse.** The first one is reported with its line and
  column, and the handler's tree is thrown away by the caller. There is one
  place where "not well formed" is a judgement: a version number.
  `version="1.7"` is accepted, as the Fifth Edition says; `version="2.0"`
  and `version="1."` are not (the grammar is `'1.' [0-9]+`).
- **The encoding** of bytes is found by `decode_xml()`: a byte order mark,
  then the charset of the transport, then the first bytes and the encoding
  declaration, then UTF-8 (appendix F). It reads UTF-8, UTF-16 in both
  orders and windows-1252 under its names (`ISO-8859-1`, `US-ASCII`); a
  document that declares another encoding is not read at all. `DOMParser`
  never gets here: it is handed a string.

### 3. What a hostile document costs

A browser's XML parser is handed text by a page, so the page chooses the
worst case.

| Document | What bounds it | Time |
| :--- | :--- | ---: |
| Nested 200,000 deep | Nothing recurses: open elements are a vector, the namespace scope is a stack of the declarations made (not a copy a level), an entity inside an entity value is expanded from a vector of sources, and a content model's parentheses are counted | 130 ms |
| Entities that expand to entities (a "billion laughs"), in text or in an attribute value | Expansion is counted. Past 8 MB a document may expand to 100 times its own size and no more; then it is not well formed. The figures are Expat's | refused in 83 ms |
| An entity that contains itself, directly or through 50,000 others | Refused where it is used (4.1, "No Recursion"); the chain without the ring is followed in a loop | 0.1 s |
| One attribute default and 100,000 empty tags that get it | A default is text the document did not write where it appears: one of a megabyte made 100 GB of attribute values from 1.4 MB. Counted with what entities add, under the same limit | refused in 0.1 s |
| One element with 50,000 attributes | A name given twice is found with a set, and the element's own list has an index (section 4) | 40 ms; 7.1 s before the index |
| 50,000 namespace declarations on one element, each used by one child | A prefix is looked up in a map of stacks, not by walking the declarations | 148 ms; over two minutes as first written |
| 50,000 attributes declared for one element in the DOCTYPE | The first declaration of a name counts, so each is looked up: in a set | 0.1 s; 4.5 s as first written |

The attribute and namespace rows were found by pages
`tools/fuzz/deep_pages.py` generates, the two about the DOCTYPE by asking the
same questions of declarations while this section was written. All of them
are pages there now (20 for XML), and the unit suite
`xml_parser_bounds_what_a_hostile_document_costs` has each with a time limit.

### 4. XML in the DOM

**A document knows what it is.** `Document` has a content type and is an HTML
document or it is not; a `DOMParser` document, `createDocument()`,
`new Document()` and a response document are not. In such a document names
keep their case (`createElement('P').nodeName` is `P`), `createElement` makes
an element in no namespace (the XHTML one when the type is
`application/xhtml+xml`), `createCDATASection` works and `document.write`
throws, and `innerHTML` is XML in both directions: assigning text that is
not well formed is a `SyntaxError`, where HTML would make something of it.

**Every element has a namespace, a prefix and a local name**, and so has
every attribute. An element in the XHTML namespace gets its HTML interface
wherever it was made (`<p xmlns="http://www.w3.org/1999/xhtml">` in a feed is
an `HTMLParagraphElement`); one in another namespace, or none, is an
`Element`. Selectors learned the namespace bar (`*|b`, `|b`) and match a type
by local name, case kept outside HTML.

**Two new kinds of node**: `ProcessingInstruction` and `CDATASection` (a
`Text`). Both are in the node bindings, the cloning, the comparisons and both
serializers; the HTML one writes `<?target data>`.

**A document keeps to its shape.** One element, one doctype, the doctype
first: `appendChild`, `insertBefore`, `replaceChild`, `append`, `before` and
their relatives check "ensure pre-insert validity" for a document as the
parent, which they did not, and `document.doctype` is the doctype that is
there now, not the one the parser saw.

**Names are checked by the DOM standard's own rules**, which since 2025 are
not XML's: a name is what the HTML parser could have made. So
`createElement('a<b')` gives an element and `createElement('a b')` throws;
`setAttribute('<', '')` is allowed and `setAttribute('a=b', '')` is not. The
target of a processing instruction is still an XML name. `createElementNS`
and `createDocument` validate and split a qualified name and refuse a prefix
without a namespace, `xml` and `xmlns` with the wrong one.

**Not well formed** gives what browsers give: a document whose root is
`parsererror` in the namespace
`http://www.mozilla.org/newlayout/xml/parsererror.xml`, holding the message.
A script tests for it with `getElementsByTagName('parsererror')`.

**The attribute list** (`include/litebird/dom/AttributeList.hpp`). An
element's attributes are a vector with a logical start offset in the order they were added (Phase 27),
and every lookup was a scan of it. That is right for the handful an element
has and the square of the count for one that has many: 50,000 `setAttribute`
calls on one element took 6.3 s. The list now builds an index the first time
an element with many attributes is searched. The items and the index's
ascending numbers are vectors with a logical start offset; a front erase
advances the offset and compacts only when the dead prefix reaches the live
tail, so end removals are linear and an empty list allocates nothing (Phase
35). A middle removal still moves the tail. Namespaces
are kept beside the list, for the few attributes that have one, with indexes
of their own. The
HTML parser now gives the attributes of inline SVG and MathML their
namespaces ("adjust foreign attributes") and keeps their names:
`getAttribute('xlink:href')` and `getAttributeNS('http://www.w3.org/1999/xlink',
'href')` were both null on every `<use xlink:href>` a page held.

### 5. The serializer (`src/xml/XmlDom.cpp`)

`XMLSerializer.serializeToString()` and `innerHTML` of an XML document are
the algorithm of *DOM Parsing and Serialization*, which decides for every
element and attribute whether its prefix can be written as it is, whether a
declaration must be added, and when a prefix has to be made up (`ns1`). It
is a loop with a stack of scopes, each holding only what its element
declared.

The algorithm as published has faults that are filed against it, and tests
that assert them. Litebird follows the text except where following it writes
a document that reads back as another tree, or as none:

- a made-up `nsN` passes over a prefix that is already declared on the
  element (issue 44: the text writes `xmlns:ns1` twice);
- a prefix that a nearer element has given another namespace is not used for
  the old one (issues 45 and 75);
- an attribute named `xmlns` in no namespace is written as an attribute and
  never taken for a declaration (issue 83).

And it follows the change of 1 September 2026: an attribute keeps its prefix
where it can. `tests/wpt/README.md` has each case, what the test expects and
what Litebird writes.

What decides is not a reading of the text. **A tree that is written and read
again must be the same tree**, and that can be checked with no reference at
all: `tools/fuzz/xml_roundtrip.py` builds trees made to break it (one prefix
for two namespaces on an element and its parent, `xmlns` attributes that
contradict the names beside them, the prefixes the serializer makes up),
writes each, reads the text with `DOMParser` and with Expat, and compares.
With the published algorithm 265 of 3,000 such trees do not come back; with
issue 45 left in, 498.

"The same tree" means every node, and every element and attribute with its
namespace and local name. Two things are not kept, by the standard's choice,
and Litebird and jsdom both follow it there:

- A namespace declaration that says what is so already is not written: one
  for the `xml` prefix, or one that repeats an ancestor's. The names it was
  for are unchanged; the `xmlns:` attribute is gone from the element.
- `innerHTML` writes each child without its surroundings. A child in no
  namespace is written `<c/>`, and when that text is assigned to an element
  with a default namespace the child lands in it. So `el.innerHTML =
  el.innerHTML` is not always the identity in an XML document. Done a second
  time it changes nothing more, and the mutation fuzzer (section 8) checks
  that.

### 6. `XMLHttpRequest`

A request is answered when its address is a `data:` URL, and only then. That
is enough for `responseXML` to exist and be tested, and it is all that could
be done without deciding what a request over a network may reach
(section 10).

What is there follows the standards step by step, and four of them meet in
it:

| Standard | What | Where |
| :--- | :--- | :--- |
| Fetch | The `data:` URL processor: the type before the comma, percent escapes, `;base64` and what counts as base64 | `src/net/DataUrl.cpp` |
| MIME Sniffing | Reading and writing a MIME type; which types are XML (`text/xml`, `application/xml`, anything `+xml`) | the same |
| Encoding | Bytes into text: a byte order mark first, then the label, errors as U+FFFD | the same |
| XMLHttpRequest | `open`, `send`, `abort`, the states and events, `responseType`, `overrideMimeType`, the final MIME type and encoding, the text, document, JSON and blob responses | `src/js/DomWindowBindings.cpp` |

`responseXML` is a document when the final type is an XML one and the bytes
are a well-formed document, and null otherwise; there is no `parsererror`
document here. The document has the response's type and address, and no
script in it runs. An HTML response is a document only when the script asks
for one (`responseType = 'document'`), and is parsed with scripting off, as
`DOMParser` parses. `send()` fires `loadstart` and returns; the response
arrives in a task after that (the states, `progress`, `load`, `loadend`). A
synchronous request has it before `send()` returns, and fires no `loadstart`
and no `progress`.

`DataUrl.cpp` does no input or output: it turns a string into a type and
bytes. The request code calls nothing that opens a file or a socket, and the
unit suite asserts it from outside: a `file:` address, a path, a relative
address that climbs to one, `http:`, `https:`, `blob:`, `about:` and
`javascript:` all end in `error` with status 0 and an empty response. For
the first three the file is there to be read.

### 7. Four references, and where each is not the truth

**The W3C suite** judges 1,702 of its 2,585 documents for this parser. The
rest ask for something a browser's parser does not do (validate, read an
external entity, XML 1.1) or were answered differently before the Fifth
Edition; `tests/xml/README.md` has the count for each reason. All 2,585 are
run, counted or not, so that none can crash the parser. The suite is
downloaded and checked against a pinned SHA-256, not kept in the repository:
the archive has no licence for the whole, and one of its parts forbids
passing it on in any other form than its own.

**Expat and libxml2** read documents `tools/fuzz/diff_xml.py` generates, half
of them damaged in one place. The rule is: what the two read alike, Litebird
must read alike. They disagree with each other on 13 documents in 4,000,
and both are wrong about one thing: Expat does not check a version number
and libxml2 accepts `version="1."`, so a document whose version is outside
the grammar is judged by the grammar. On the W3C suite Expat gives the
expected answer for 1,404 of 1,718 documents and libxml2 for 1,697, which is
why neither is the reference alone.

**web-platform-tests** are the tests browsers share, run with their own
harness (`tests/wpt/`, 76 test files, 5,192 subtests). Five subtests of the
serializer's file assert a text the standard has since changed, or a fault
that is filed against it (section 5). 495 load a document into a frame,
which Litebird does not have. Every failing subtest is listed with a word
for its reason in `tests/wpt/expected-failures.txt`; the runner fails when
one fails that is not listed and when a listed one passes.

**jsdom** recorded 57 cases (`xml.dat`, `xhr.dat`). Its XML serializer
follows the text of before September 2026 and makes up a prefix where an
attribute has one; its `XMLHttpRequest` reads an XML response as UTF-8
whatever the document in it declares, puts U+FFFD where the bytes are not
text and gives a document, where XML says there is none (4.3.3); it calls every
response document `application/xml`, keeps the fragment in `responseURL`,
fires a `progress` event on a synchronous request and one after `abort()`,
and parses an HTML response with scripting on. The corpus stays away from
each of these and the unit suites
`xml_dom_follows_the_standard_where_jsdom_does_not` and
`xml_http_request_answers_data_urls_and_nothing_else` assert the standard;
`tests/dom-conformance/README.md` has the table.

### 8. How this is verified

| Check | Result |
| :--- | :--- |
| W3C XML suite (`make xml-conformance`) | 1702 / 1702 counted, 261 canonical forms the same, 2,585 documents run, 0 crashes, 0 timeouts. `main` has no XML parser |
| Generated documents against Expat and libxml2 (`make diff-xml`) | 4,000 documents, half of them damaged: 3,958 read as both read them, 13 where the two disagree, 29 with a version number outside the grammar and all refused, 0 differ |
| Round trip (`make diff-xml`) | 3,000 trees written by `XMLSerializer`: 3,000 come back the same from Litebird and from Expat |
| web-platform-tests (`make wpt`) | 4640 / 5192 subtests in 76 files; the 552 others are listed with a reason, 495 of them frames. `main` passes 1,127 of the 2,257 it gets as far as reporting |
| DOM corpus (`make dom-conformance`) | 629 / 629 in 23 files: the 572 of Phase 27, 38 in `xml.dat`, 19 in `xhr.dat`. `main` passes none of the 57 |
| The corpus in jsdom (`--reference`) | 629 / 629 |
| Unit tests (`make test`) | 129 / 129; twelve are new (the parser, what a hostile document costs, XML documents in the DOM, the serializer, `innerHTML` in XML, names, the shape of a document, foreign attributes, where jsdom is not the standard, selectors with namespaces, the attribute list, `XMLHttpRequest`) |
| html5lib tree construction (`make conformance`) | 1781 / 1792, as before |
| `make diff-trees` | 4 × 1,500 random pages against parse5, 0 differ, as before: the HTML parser now stores foreign attributes with their namespaces |
| JavaScript corpus, URL data | 840 / 845 and 1150 / 1188, as before |
| `make deep` | 1,332 runs over 465 pages at 200,000 levels, 0 failed; 20 of the pages are XML, the slowest of those 1.6 s. The slowest of all takes 22.8 s of the 60 allowed (a table 200,000 deep, which this phase did not touch) |
| `make asan` (ASan + UBSan): unit tests, the corpora, the XML suite, web-platform-tests, generated documents, the fuzzers, the deep pages at 10,000 levels | 129 / 129; JavaScript 839 / 845, as before; DOM 629 / 629; URL 1150 / 1188; XML 1702 / 1702; web-platform-tests 4640 / 5192; 1,000 generated documents and 1,000 round trips, none differ; 400 + 400 fuzz runs, 0 findings; 882 runs over 315 deep pages, 0 failed. No report from either sanitizer |
| Mutated documents (`tools/fuzz/fuzz_xml.py`, in `make fuzz` and so in `make asan`) | 600 mutants of the W3C suite's documents, 38 of them still documents, and 1,927 of its documents as they are, 1,551 of them documents to this parser: no signal, no report, no timeout, and each document the same tree after it is written and read. Three other seeds by hand, 1,800 mutants: the same |

CI has a job for the XML suite, the differential run and the
web-platform-tests, and the sanitizer job runs all three in the sanitizer
build.

**What the sanitizer build found.** The first commit of this phase that CI
ran failed there, and only there. `XMLHttpRequest`'s "get a final encoding"
read the charset through `response_mime(state).parameter("charset")`: a
pointer into a `MimeType` that was a temporary, used one statement after the
temporary was gone. Every request for a type with a charset read freed
memory; the normal build passed every suite and every case, because the
bytes were still there. The fix is a name for the temporary, and the rvalue
overload of `MimeType::parameter()` is deleted, so the same line no longer
compiles.

Two things followed. The sandbox the checks are run in could not show the
failure, because the unit tests of the sanitizer build were killed there for
lack of memory: nothing in the engine collects unasked, AddressSanitizer
keeps what is freed, and 125 suites in one process had reached 3.8 GB of
the machine's 3.9. After each suite the sanitizer build now collects and
hands the allocator's pages back (`give_memory_back()` in
`tests/test_suite.cpp`); the run peaks at 2.6 GB and takes two minutes. And
a parser of hostile text that had only ever been given documents somebody
understood now has a mutation fuzzer: `fuzz_xml.py` takes the W3C suite's
documents, changes bytes, cuts them short and pastes over them, and gives
each to the parser as bytes and to `XMLHttpRequest`, `DOMParser`,
`XMLSerializer` and `innerHTML` through a page.

### 9. What it costs

Release build, on the machine the checks ran on; "before" is `main`
(`db705e6`) built and run there the same hour.

| | Before | Now |
| :--- | ---: | ---: |
| `setAttribute` 50,000 times on one element | 6.3 s | 49 ms |
| `setAttributeNS` 20,000 times on one element | 0.7 s | 41 ms |
| `removeAttribute` of 25,000 of 50,000, from the back | 2.5 s | 22 ms |
| `removeAttribute` of all 50,000, from the front | 8.2 s | 9.0 s at Phase 28: each one moved the rest of a vector; fixed in Phase 35 (0.09 s whole-process) |
| The HTML parser, one element with 50,000 attributes | 10.4 s | 2.9 s at Phase 28: the tokenizer compared every name with every accepted one; fixed in Phase 39 (0.09 s whole-process) |
| An XML document 200,000 deep: parsed, serialized | | 130 ms, 37 ms |
| An XML element with 50,000 attributes: parsed, serialized | | 40 ms, 4 ms |
| The unit tests in the sanitizer build, highest memory | killed at 3.8 GB (this branch before `give_memory_back()`) | 2.6 GB |

### 10. What is still open here

- **`XMLHttpRequest` answers `data:` only.** `http:`, `https:`, `file:` and
  relative addresses end in `error`. `fetch()` is older code with a loader
  of its own (`src/net/ResourceLoader.cpp`), and that loader takes whatever
  is not `data:` or `http://` for a path on this machine: a page at
  `https://example.test/` that calls `fetch('/home/user/notes.txt')` gets
  the file. Requests were not put on that loader, and the hole is a row in
  AGENTS.md.
- **Four encodings**: UTF-8, UTF-16 in both orders, windows-1252. A label
  for any other is taken for no label; an XML document that declares another
  is not read. A page is read as UTF-8 whatever it declares, `<meta
  charset>` is not looked for (in a page or in an HTML response), and
  `document.characterSet` is `UTF-8` for every document.
- **The command line loads every file as HTML.** XML documents exist
  through `DOMParser`, `createDocument()` and `XMLHttpRequest`, not by
  opening `feed.xml` or `drawing.svg`. That, and frames with documents in
  them, is what 495 of the 552 failing web-platform-tests subtests need.
- **An `Attr` is not a `Node`** (10 subtests), **two attributes of one
  element cannot share a qualified name** in different namespaces (14: only
  a script can make such a pair), and a `NamedNodeMap` or an
  `HTMLCollection` does not list its names as own properties (8).
- **A function cannot have a function as its prototype** in the JavaScript
  engine: `Object.getPrototypeOf(TypeError) !== Error`, and for a class,
  `Object.getPrototypeOf(Derived) !== Base`. 18 subtests fail on it, each an
  `assert_throws_js`. Not a DOM fault. (Fixed in Phase 34.)
- **Removing attributes from the front** of a very long list was quadratic
  at Phase 28; fixed in Phase 35 (vectors with a logical start offset).
- **`responseType = 'arraybuffer'`** gives null: the engine has no
  `ArrayBuffer`.
- **SVG elements still have no SVG DOM** (`className.baseVal`, `viewBox`),
  in an XML document as in an HTML one.
- **The HTML parser does not make processing instructions yet.** The
  standard now does and the node type is here; the tokenizer and the tree
  builder are not changed.
- **Not planned, as in browsers:** XML 1.1, validation, external entities.
  **Absent:** XPath (`document.evaluate`), XSLT.

---

## Phase 29 Architecture: Table Spans and Collapsed Borders

Phase 16's table algorithm knew one fact per cell: its column index. A cell
that covered two columns or two rows had no representation, so neither did
`colspan`, `rowspan`, `border-collapse` or `border-spacing`. This phase finishes
the model. It re-lands the implementation written in PR #16 (8 November, on top
of a tree that was subsequently rebuilt through Phases 23–27) on current `main`.

### 1. Spans: placement before sizing

Every cell is placed into an **occupancy grid** before any measurement
happens. Row `r`, column `c` is reserved for a cell with
`colspan`/`rowspan` by marking the slots it covers, so the next cell on the
row skips them. `rowspan="0"` spans to the end of the rows the table has;
`colspan` is clamped to 1000, so a hostile `colspan="99999999"` cannot
allocate a grid. Occupied slots are sized by `std::vector<bool>`, so the
reservation is one byte per cell.

### 2. Column widths with spanning cells

A spanning cell distributes its width requirement over the columns it covers
(requirement minus the width the covered columns already have, spread evenly,
clamped at zero) instead of demanding one wide column. Percentage cell widths
resolve against the *table's* width exactly as before, but a percentage
spanning cell now distributes over its covered columns the same way, and a
cell marks itself with `width_basis_override = <table width>` so percentage
lengths inside it resolve against the table, not against the column it happens
to sit in (CSS 2.1 §17.5.2).

### 3. Row heights

Single-row cells set their row's height first. Then each rowspan cell that
needs more room than the rows it covers already provide grows the *last* row
it covers. The spanning cell is itself grown to fill the covered rows, which
is why a `rowspan="2"` cell with a background paints as one tall box instead of
a box with a gap under it.

### 4. The two border models

- `border-collapse: separate` (the default) keeps per-cell borders and
  honours `border-spacing: <h> <v>`: the cells are inset by `h` from the
  table's left edge and each cell is offset `h` from the previous, and the
  same vertically with `v`. One value means horizontal and vertical are equal.
- `border-collapse: collapse` makes each cell paint **half** of a shared edge
  (4px + 4px renders as one 4px line, not 8px) and forces `border-spacing`
  to zero. The implementation is `LayoutBox::border_scale`, applied when the
  used border is computed from the style — never by mutating the style, so a
  relayout that scaled twice would otherwise erode borders to nothing. Odd
  borders are rounded *up* (`std::ceil`), so a 3px border paints 2px + 2px
  rather than vanishing.

### 5. Two latent bugs this surfaced, and how they were fixed here

1. **Floats and table cells lost their style edges.** `layout_float` and the
   table code called `layout_block`/`layout_table_cell` directly, and only
   `layout()` copied margin/padding/border from the computed style onto the
   box — a float with padding measured as if it had none, a cell with a
   border lost it. Both now go through a shared `LayoutBox::apply_style_edges()`.
   Floats bypass `layout()` because a floated inline keeps
   `BoxType::InlineNode`; routing them through `layout()` would have sent a
   floated `<span style="float:left">` down the inline path. Floats now
   resolve percentages against their containing block too.
2. **Cells laid out through `layout()` directly.** Table cells are
   `BoxType::TableCellNode`, so they now go through the normal dispatch and
   keep their edges via the same path as everything else.

### 6. Verification

```
make test              # 112 engine test suites, all passing (2 new:
                       # table_colspan_and_rowspan_geometry,
                       # table_border_collapse_and_spacing)
make conformance       # html5lib tree-construction: 1781 / 1792 (unchanged;
                       # the parser was not touched)
```

### 7. What is still approximate (unchanged from the PR, restated)

- Collapsed borders are halved rather than resolved per edge by the CSS 2.1
  "widest wins" rule; the table's outer border is not merged with edge cells.
- `empty-cells`, `caption-side` and `table-layout: fixed` are unimplemented.
- Extra height demanded by a rowspan cell lands entirely on the last covered
  row.
- `rowspan="0"` spans to the end of the table, not to the end of the row
  *group* the cell is in.
- A spanned cell's extra width is spread evenly over the covered columns;
  CSS 2.1 does not mandate a distribution algorithm for auto layout.

---

## Phase 30 Architecture: Deletion and Front Edits Are Linear

Phase 23 measured two containers that went quadratic with their input. A
large object's property map rebuilt its hash index on every `delete`; an array
was a `std::vector<JsValue>`, so `shift()` copied the whole tail each call.
This phase changes the storage, not the public semantics.

### 1. The property map

An insertion-ordered table still has to be an insertion-ordered walk for
`Object.keys`, `for...in` and `JSON.stringify`. `PropertyMap` now holds:

- an `std::list<Entry>` with the properties in insertion order; and
- an optional `std::unordered_map<std::string, std::list<Entry>::iterator>`.

Up to twelve entries, `find` and `entry` are a linear list walk, exactly as
they were for small vectors. The first time a map grows past twelve entries,
the index is built once. Every later insertion and deletion keeps the list and
the index in step: an erase removes one list node and one hash entry, and no
surviving iterator moves. If the map later drops to six entries or fewer, its
bucket array is released and the map becomes linear again until it crosses the
threshold once more. `clear()` resets both structures. The enumeration loops
in `src/js/JsValue.cpp`, `src/js/JsEval.cpp` and the property-order users
elsewhere are unchanged: they see a list iterator with the same dereference
shape as the vector they saw before.

This is O(1) erase and O(1) lookup for a large map, at the cost of one list
node per property (the same order of overhead a hash entry already had) and
the inability to use raw index arithmetic. That trade is confined to dynamic
JavaScript objects; no parser, DOM or layout table uses `PropertyMap`.

### 2. Arrays

`JsObject::array_elements` is now a `std::deque<JsValue>`. The ends are the
hot paths a page meets early:

- `shift()` and `unshift()` move only a deque chunk boundary, not every
  surviving element;
- `push()` remains amortized O(1);
- reading by position still re-computes from the deque's block map, not by
  shifting elements when the front is removed;
- cloning into a vector (`js_iterable_to_vector`, JSON and `structuredClone`)
  copies through an explicit range; and
- `splice(0, n)` takes the front of a deque instead of memmoving the tail.

Middle `splice()` is still linear in the number of elements moved, because a
deque is not a random-access gap buffer; that is the same order as before and
is not what the phase set out to solve. Deleting an array element with
`delete a[i]` still creates a hole.

### 3. Compatibility

The change touches storage only. `Object.keys`, `for...in`, `JSON.stringify`,
`Object.assign`, cloning, DOM bindings and array builtins all retain their
order rules. `tests/test_suite.cpp` adds one suite,
`js_property_maps_and_arrays_keep_their_order_through_front_edits`, covering
order after `delete` and reassignment, a 2,000-property workload, identity
through `shift()`/`unshift()`, `splice(0, n)` and a full array consumed from
the front.

### 4. Measured

The Phase 23 scripts, run on the same machine before and after this phase:

| Script | Before | After |
| :--- | ---: | ---: |
| `delete` of the 50,000 keys of one object | 131,423 ms | 25–29 ms |
| `shift()` until a 50,000-element array is empty | 8,228 ms | 9–10 ms |

`tools/bench/bench_delete_shift.py` compares the two cases and the adjacent
front paths against Node and exits nonzero on a wrong result, a timeout or a
case over `--max-ms`. One verification run printed:

```
python3 tools/bench/bench_delete_shift.py --reps 3 --max-ms 2500
case                                           litebird  reference  result
delete: 50k keys                                   58ms       59ms  0
delete: reinsert after erase                       49ms       61ms  last
shift: until an array is empty                     30ms      189ms  1249975000
unshift: 20k times                                  9ms       50ms  19999 20000
splice: remove the first half                      20ms       25ms  25000 25000
enumerate: order after delete and insert            4ms       23ms  a,c,d,b
```

`make test` passes all 113 suites (the new one included). `make js-conformance`
is unchanged at 840 of 845 (everyday probe 90 of 90; 839 of 845 in the ASan
build, the same one deep-nesting case the sanitizer cannot fit); `make
dom-conformance` is 572 of 572, `make url-conformance` is 1188 of 1188, and
`make conformance` is 1781 of 1792, unchanged — the parser was not touched.
`make fuzz` reports 0 findings in 400 parser mutations and 400 layout/render
and image cases; `make deep` reports 0 failures in 1,292 runs over 445 pages;
`make asan` builds the engine with ASan and UBSan, reports no sanitizer
finding, and its ASan deep run covers 842 executions.

### 5. What is still approximate

- Property maps below twelve entries keep the linear scan; that is deliberate.
- A map that grew large keeps its bucket array until it falls to six entries
  or is cleared/destroyed. The bucket array is not shrunk incrementally.
- `splice()` from the middle is linear in the elements moved.
- Holes from `delete a[i]` are still holes in the array, not `undefined`
  tombstones that traversal can distinguish; this is recorded elsewhere as a
  missing feature, not changed here.

---

## Phase 31 Architecture: CSS Whitespace Trimming Is Linear

### 1. What was wrong

Finding 28 measured 300,000 spaces inside `:nth-child( )` at about nine
seconds. The cause was not the HTML parser or layout: several CSS trim sites
did this:

```cpp
while (!text.empty() && std::isspace(text.front())) text.erase(text.begin());
```

Every erase shifted the whole remaining string. A run of n spaces before a
value therefore cost O(n²) before the value was read. The nth matcher also
took its argument by value and lowercased the padding before trimming it,
once for every child it tested.

### 2. What changed

`include/litebird/css/CssText.hpp` provides `css_trim_view` and `css_trim`.
The view scans the two outer runs without copying. The owned-string version
erases the leading run once and resizes the tail once. The attribute-selector
quote stripper uses the same bounded-scan shape with its existing quote set.
All front-erasure loops in `src/css/CssParser.cpp` and
`src/css/StyleResolver.cpp` were replaced; `grep -n 'erase(.*begin())'
src/css/*.cpp` now finds none.

The nth matcher takes a view, trims before allocating/lowercasing, and
shorthand/duration trimming stays in views until a token is stored. The
whitespace set is deliberately unchanged: `std::isspace` in the process
locale, with an unsigned-char cast. The engine does not call `setlocale`;
in its C locale this includes VT. Changing that to the CSS whitespace
production would be a tokenizer change, not this cost fix.

### 3. Measured through the page pipeline

`tools/bench/bench_css_whitespace.py` writes a compact page and the same page
with spaces in parser/resolver arguments, runs `--dump-layout --dump-paint`,
and compares the bytes. With `--baseline` it also compares the old binary's
dump and reports its time. Times below are whole-process milliseconds, best
of three for the new binary and one run for the baseline, on the same
two-core verification machine; they are not microbenchmarks of trim alone.

| 300,000 spaces per placeholder | Before | After |
| :--- | ---: | ---: |
| `:nth-child` formula | 14,627 | 111 |
| attribute name and quoted value | 4,687 | 90 |
| attribute presence | 2,331 | 58 |
| keyframe selector | 47 | 55 |
| grid `minmax(...)` | 9,786 | 199 |
| grid `repeat(...)` | 22,545 | 188 |
| filter name/argument/colour | 7,191 | 156 |
| shorthand tokens | 181 | 171 |
| durations and `grid-area` | 5,791 | 283 |
| transform functions/arguments | 11,694 | 304 |

The keyframe and shorthand cases were already cheap in this pipeline because
other whitespace consumption happens before the trim; they are kept as
behaviour guards, not claimed as speedups.

```bash
make bench-css
# or reproduce the table against a saved pre-change binary:
python3 tools/bench/bench_css_whitespace.py \
  --sizes 30000,300000 --reps 3 --max-ms 3000 --baseline build/litebird-before
```

The checked-in CI guard uses 30,000 and 300,000 spaces, three repetitions,
compact/padded output equality and a generous 3,000 ms per-run limit. It
fails on a crash, timeout, sanitizer report, different dump or slow case.

### 4. Verification and limits

Three appended unit suites cover the helper against the old character model
(including empty input, embedded NUL, high bytes and VT), parsed selector
fields and resolved values across every changed path, and 300,000-space
inputs. `make test` passes 117 suites. The conformance corpora are unchanged
(HTML 1781/1792, JS 840/845, DOM 572/572, URL 1150/1188). `make diff-trees`
has 0 differing pages, `make deep` 0 failures in 1,292 runs, `make fuzz`
0 findings in 800 runs, and `make asan` no sanitizer finding (ASan JS
839/845, ASan deep 842 runs, 0 failed). `make tsan`, `make bench` and
`make diff-strings` also pass.
This phase does not make the CSS parser spec-complete, does not change CSS
whitespace recognition, and does not address the separate layout, tokenizer
attribute-count or style-object tasks. The `:nth-child(2n + 1)` sign-space
parsing bug found while building the benchmark is fixed (AUDIT.md finding 39:
whitespace tokens are ignored inside the `<an+b>` micro-syntax and the offset
and coefficient are strictly signed integers, so an invalid formula matches
nothing); the benchmark kept `+1` glued while the bug was open, so its
timings never depended on the fix.

---

## Phase 32 Architecture: The CSS Color Keywords, Once

Phase 27 found that a red box and a `getComputedStyle(el).color ===
'rgb(255, 0, 0)'` did not agree. The bindings knew the answer; the renderer
had a separate table of its own. There is now one table.

### 1. One source of truth

`Color::named_color_value(lower, out)` in
`include/litebird/common/Color.hpp` is the 148 CSS Color 4 keyword table that
used to live as `named_color_value()` in `DomStyleBindings.cpp`. Both sides of
the engine call it:

- `Color::parse()` converts `red`, `green`, `tomato`, `rebeccapurple`, the
  `grey`/`gray` aliases and the rest of the keywords to the specification's
  bytes; `transparent` returns alpha 0.
- The computed style returns the same bytes as `rgb(...)` text, and the
  declaration normalizer accepts the same 148 names before storage.

There is no longer a Bootstrap-looking 19-name table with a different red, and
no copies of the 148-name list in JavaScript DOM bindings and the common
color helper. The common table is the one both callers use.

### 2. What did not change

An unknown keyword still parses to opaque black; Phase 32 is about giving a
name the wrong answer is not, not about rejecting unknown CSS. `currentcolor`
is still context-dependent (handled by the computed style path, not by a
standalone renderer color), and `rgb()`, hex parsing and percentage or
parenthesis-separated `rgba()` values keep their existing behavior.

### 3. Verification

`css_color_keywords_match_the_css_specification` checks every one of the 148
keywords through `Color::named_color_value` and `Color::parse`, plus case-folded
and whitespace-surrounded input, several aliased spellings
(`aqua`/`cyan`, `fuchsia`/`magenta` and the `gray`/`grey` family), `transparent`, two of the
formerly missing names (`tomato`, `silver`) and an unknown name. The old
`color_parsing` expectation for `blue` was corrected to CSS's
`rgb(0, 0, 255)`. `make test` prints `All 114 engine test suites passed
successfully!`. The first DOM conformance rerun caught a 571/572 regression:
the declaration normalizer had accepted `transparent` and `currentcolor`, which
the raw 148-entry value table does not contain, and the shared table alone
would have dropped both. The normalizer now stores either keyword explicitly;
the rerun is 572/572. `make conformance` is 1781/1792,
`make js-conformance` is 840/845, `make dom-conformance` is 572/572,
`make url-conformance` is 1188/1188, `make diff-trees` has 0 differing pages,
and `make asan` reports no sanitizer finding (ASan JS corpus 839/845; its
ASan deep run covers 842 executions). The HTML and JS floors are
unchanged.

### 4. What is still approximate

- `Color::parse()` does not implement `rgba()`, `hsl()`/`hsla()`, or CSS Color
  4's `hwb()`/`lab()` syntax. It is a keyword, hex and integer-`rgb()` parser;
  that was its scope before this phase and remains the row of the known gaps.
- `Color::parse("currentcolor")` is opaque black outside the computed-style
  cascade, because it has no inherited color to resolve against. Canvas and
  SVG strings that literally say `currentcolor` do not get a useful color.
- The legacy `dark_gray()` and `light_gray()` helper functions retain their
  historical call-site values; `Color::parse()` no longer uses them.
- A malformed `#` value with fewer than three hexadecimal digits returns black,
  as before.
---

## Phase 33 Architecture: Page Resource Loads Are Origin-Aware

### 1. What was wrong

`ResourceLoader::load` treated any URL that did not begin with `data:` or
`http://` as a path on this machine. `FetchAPI::fetch` called it with the
script's string and no document context, so a page loaded from
`https://example.test/` could read `/home/user/notes.txt`, `file:///…`, or a
relative path that happened to name a local file. `WorkerObject` had the same
loader and, when loading failed, returned the URL/path itself as script source.
The loader was not told where the page came from.

### 2. The boundary

`ResourceLoader.hpp` now has two entry points:

- `load_for_page(url, context, …)` is the only page-initiated path. The
  `ResourceRequestContext` carries `document_url` (the capability) and
  `base_url` (relative resolution only). A `<base href>` may change the base,
  but it cannot change the document's origin. The URL is parsed with the
  WHATWG parser before dispatch; there is no string-prefix fallback to disk.
- `load_navigation(url)` is embedder-only (CLI/TUI address bar, history,
  reload). A bare path is a local file there. It is not exposed to script.

Policy in `load_for_page`:

| Document address | Request | Result |
| :--- | :--- | :--- |
| `http:` / `https:` | `data:` | allowed; self-contained |
| `http:` / `https:` | resolved `http:` | network loader (HTTPS target is not implemented) |
| `http:` / `https:` | `file:`, local path, other scheme | refused before disk/network |
| `file:` | `file:` / relative file | allowed; local CLI/TUI capability |
| `about:blank`, empty/invalid | `data:` only | opaque context |

`data:` goes through the Fetch Standard processor already used by
`XMLHttpRequest` (`process_data_url`), so percent-encoding and forgiving
base64 are shared. `ResourceResponse::url` records the resolved address
without a fragment, and `fetch()` reports it as `Response.url`.

### 3. Callers

`JsInterpreter` installs `fetch` and `Worker` with a context provider read at
call time: the current document URL and the document's `<base href>`. An
alive guard keeps a retained native closure from touching a destroyed realm.
The bytecode `VM` uses its document address as the context. A worker inherits
the creator's capability; a classic worker's base URL becomes its loaded
script URL, while a `data:` worker keeps the creator's base. A failed worker
load posts an error event instead of executing the refused path as source.
`BrowserShell` navigation/history/reload use `load_navigation`.

### 4. Verification

`make test` passes 132 suites. The three new suites are
`resource_loader_page_requests_are_origin_aware`,
`fetch_and_workers_use_the_document_origin`, and
`fetch_relative_requests_use_the_document_base_url` (a loopback server proves
`../secret.txt` from a web document goes to the resolved HTTP path, not disk).
`make security-fetch` writes a marker file and runs the CLI against generated
pages: web document, web document with a `file:` base, and local `file:`
document. It prints `3 checks; 0 failure(s).` The full release run also kept
html5lib 1781/1792, JS 840/845, DOM 629/629, URL 1150/1188, XML 1702/1702,
WPT 4640/5192, diff-trees/diff-xml 0 differing, deep 1332 runs/0 failed,
ASan 132/132 (JS 839/845), and ASan deep 882 runs/0 failed. Outside the DOM
corpus, `tools/conformance/compare_with_jsdom.py` on a generated base/history
probe printed `4 lines, 4 the same` before and after; jsdom 31 has no
fetch/Worker, so the resource policy itself is checked by the unit suites and
`make security-fetch`, not recorded as a jsdom expectation.

Repro:

```bash
printf 'LOCAL_SECRET\n' > /tmp/secret.txt
printf '<script>fetch("/tmp/secret.txt").then(r=>r.text()).then(console.log, e=>console.log("ERR:"+e));</script>' > /tmp/page.html
./build/litebird /tmp/page.html --url=https://example.test/page.html
make security-fetch
```

Before the change the web page printed `LOCAL_SECRET`; after it the request is
refused/resolved as a web URL and the marker is not printed.

### 5. What is still open

This is the local-file disclosure boundary, not a complete Fetch/XHR network
stack. Phase 36 subsequently added `https:` (via runtime `dlopen` of OpenSSL 3),
HTTP/1.1 chunked + gzip/deflate decoding, CORS enforcement for cross-origin
`fetch()` and `XMLHttpRequest`, external `<script src>` / `<link rel="stylesheet">`,
and origin-checked `<img src>` loading (Finding 44). Still open: CORS preflight
`OPTIONS` requests and credentialed CORS cookies, `blob:`/`about:` URL fetching,
and worker script MIME/module policy (`Worker.cpp`). A `file:` document is still
treated as a local embedder context and can read local files, so opening
untrusted local HTML remains a trust decision.

---

## Phase 36 Architecture: Usable Browser Pipeline, HTTPS/HTTP1.1/CORS, Subresources, Live Reflow, TrueType & Web GUI

Before Phase 36, Litebird's parsers and DOM passed thousands of conformance
cases, yet opening a real multi-file web page or web app failed immediately:
`https:` did not exist, HTTP/1.1 `Transfer-Encoding: chunked` and
`Content-Encoding: gzip`/`deflate` failed, `XMLHttpRequest` rejected every
non-`data:` URL, `<link rel="stylesheet">`, `@import`, and `<script src>` were
ignored, `<img src>` could only read raw uncompressed BMP/PPM files (and
bypassed document-origin checks — AUDIT.md, finding 44), `getBoundingClientRect()`
returned `0×0` inside scripts, `<a href="#">x<p>y</p></a>` dropped `<p>y</p>`,
the CLI parsed `.xml`/`.svg` files as HTML, text rendered only as unscaled 8×8
blocks, and there was no graphical browser UI beyond terminal ANSI or offline
`.bmp` dumps.

Phase 36 wires those missing browser subsystems end-to-end while keeping zero
compile-time library link flags (`Makefile` links no `-lssl`, `-lz`, or
`-lfreetype`).

### 1. Pure-C++20 DEFLATE / zlib / gzip (`Deflate.hpp`, `Deflate.cpp`)

Implements RFC 1951 (stored, fixed Huffman, dynamic Huffman), RFC 1950 (zlib
header + Adler-32 verification), and RFC 1952 (gzip headers/trailers + CRC-32
verification, including concatenated gzip members), plus `compress_zlib_stored`
for valid PNG encoding. Decompression enforces a configurable output ceiling
(default 64 MiB) so zip/gzip bombs abort deterministically without exhausting
heap memory.

### 2. HTTPS/TLS, HTTP/1.1 Transport, and CORS (`ResourceLoader`, `Fetch`, `DomWindowBindings`)

- **Runtime `dlopen` TLS (`OpenSslDynamic`)**: Loads `libssl.so.3` /
  `libcrypto.so.3` on demand via `dlopen`/`dlsym`, enables SNI
  (`SSL_ctrl(..., SSL_CTRL_SET_TLSEXT_HOSTNAME, ...)`), verifies server
  certificates against the system trust store (`SSL_CTX_set_default_verify_paths`
  + `X509_VERIFY_PARAM_set1_host`), and degrades cleanly with an explicit error
  if no OpenSSL runtime is installed on the host.
- **HTTP/1.1 Wire Framing**: Non-blocking POSIX socket connect across IPv4/IPv6
  (`getaddrinfo` + `poll`), reserved `.test`/`.invalid` fast-fail so offline
  conformance suites never stall on DNS, `Transfer-Encoding: chunked`
  reassembly, transparent `gzip`/`deflate` body decompression, and up to 5
  `301`/`302`/`303`/`307`/`308` redirects (`303` and `301`/`302` `POST`
  rewritten to `GET`, cross-origin `Authorization` stripped).
- **CORS & Same-Origin Enforcement**: `ResourceLoader::is_same_origin` and
  `check_cors_access_control` enforce `Access-Control-Allow-Origin` (`*` or
  exact origin match) on cross-origin `fetch()` and `XMLHttpRequest` responses
  while preserving Phase 33's local-file isolation (`xml_http_request_answers_data_urls_and_nothing_else`
  and `make security-fetch`). `xhr.responseType = 'arraybuffer'` returns `null`
  cleanly until `ArrayBuffer` exists.

### 3. External Stylesheets, `<base href>`, `@import`, and Classic/Deferred `<script src>`

- **`DocumentStyles` (`DocumentStyles.hpp`, `DocumentStyles.cpp`)**: Collects
  author stylesheets in tree order across inline `<style>`, `<link rel="stylesheet">`
  (honouring `disabled`, `media` queries against the viewport, and `<base href>`
  via `Document::base_uri()`), and XML `<?xml-stylesheet?>` processing
  instructions. Leading `@import` rules are expanded recursively up to depth 5
  with URL cycle detection and per-session `StyleSheetCache` deduplication.
- **`JsInterpreter::load_page`**: Fetches and executes classic `<script src>`
  elements synchronously at parser-insertion points (`document.currentScript`
  set during execution, `error` event dispatched on failed loads without
  executing HTTP 4xx/5xx error pages as JS), and queues external `<script src defer>`
  scripts to run in document order right before `DOMContentLoaded`.

### 4. Origin-Safe PNG/SVG `<img>` Subresources (`ImageDecoder`, `LayoutTree`)

`ImageDecoder` adds full 8-bit/16-bit grayscale, RGB, palette (`PLTE`+`tRNS`),
and RGBA non-interlaced + Adam7 interlaced PNG decoding (`decode_png`), PNG
encoding (`encode_png` / `save_png`), and SVG subresource rasterization.
`LayoutTree` fetches `<img src>` exclusively through `ResourceLoader::load_for_page`
with per-document caching and `0×0` unloaded sizing (AUDIT.md, finding 44).

### 5. Live DOM Geometry, Block-in-Inline Splitting & Layout Reflow

- **Zero-overhead box storage on `Element`**: `Element` stores `m_has_layout_box`
  in an existing padding byte (`sizeof(Element)` unchanged) and border widths on
  `Element::Rare`, keeping per-node memory flat on 200,000-node stress pages.
- **On-demand reflow (`Document::ensure_layout`)**: `getBoundingClientRect()`,
  `getClientRects()`, `offsetWidth`/`offsetHeight`/`offsetLeft`/`offsetTop`/`offsetParent`,
  `clientWidth`/`clientHeight`/`clientLeft`/`clientTop`, and `scrollWidth`/`scrollHeight`
  compare `Document::tree_version()` against the last laid-out version and
  rebuild style + layout on demand when DOM/attributes/styles/viewport changed.
  Detached or `display: none` nodes immediately return `0` / empty rects / `null`
  `offsetParent`.
- **Layout fixes**: `LayoutTree` splits `InlineNode` boxes around block-level
  descendants (`<a href="#">x<p>y</p></a>`) in linear time and unions fragment
  rects back onto the inline element, copies only CSS inherited properties into
  `TextNode` boxes (`inherited_only_style`, fixing doubled background/border/padding
  on text children), synthesizes `InlineBlock` boxes with `FormState` labels/values
  for `<button>`/`<input>`/`<select>`/`<textarea>`, shrink-wraps `inline-block`
  auto widths, and accounts for child padding+border in flex/grid item advances.

### 6. TrueType (`.ttf`) Font Parser & Anti-Aliased Rasterizer (`FontRenderer`)

`FontRenderer` parses binary TrueType tables (`cmap` format 4 & 12, `head`,
`hhea`, `hmtx`, `maxp`, `loca`, `glyf` simple + compound glyphs) in pure C++20,
flattens quadratic Bézier contours, and scanline-rasterizes 4×4 supersampled
coverage bitmaps with UTF-8 codepoint decoding and an LRU glyph cache. It
auto-discovers a system sans-serif `.ttf` (e.g. `DejaVuSans.ttf`,
`LiberationSans-Regular.ttf`) when present and falls back cleanly to the
built-in 8×8 bitmap font when none exists. Both `LayoutBox` text measurement
and `BitmapRenderer::fill_text` share `FontRenderer` metrics so wrapped lines
and painted glyphs align.

### 7. CLI XML/SVG Loading, `--render-png`, and Embedded `--gui` Browser (`WebBrowserServer`)

- `src/main.cpp` and `BrowserShell` load `http://`/`https://` URLs via
  `ResourceLoader::load_navigation`, route `.xml`/`.xhtml`/`.svg` paths and
  `application/xml` / `text/xml` / `*+xml` responses through `parse_xml_document`,
  and add `--render-png <file>`.
- `litebird --gui [--port=8080] [url]` starts `WebBrowserServer`, an embedded
  multi-threaded HTTP/1.1 browser UI bound to `0.0.0.0:<port>` with serialized
  engine access, live PNG viewport rendering, address bar, Back/Forward/Reload,
  viewport resize, click/scroll/keyboard/form forwarding (`hit_test` -> focus ->
  3-phase DOM `click` -> `<a href>` navigation), and DOM/Console/Network
  DevTools inspector tabs, plus `/demo/index.html` (`examples/usable_browser_demo/`).

### 8. Verification

```bash
make -j$(nproc) && make test
./build/litebird examples/usable_browser_demo/index.html --render-png /tmp/demo.png
./build/litebird --gui --port=8080
```

- `make test`: **153/153** unit suites pass after merging `main` (`build-asan/test_suite` verified cleanly).
- Conformance & security: HTML **1781/1792** (`diff-trees` **6,000/6,000** equal),
  JS **907/921** (Everyday **90/90**), DOM **640/640** (Everyday **68/68**;
  out-of-corpus `compare_with_jsdom.py` probe **5/5** equal), URL **1188/1188**,
  XML **1702/1702**, WPT **4658/5192** (0 unlisted), `make security-fetch`
  **3/3**, `make bench-css` **20/20**, `make bench-attributes` **6/6**,
  `make verify-media` **35/35**, `make fuzz-media` **1800/1800**,
  `make diff-strings` **95** (`<= 95`), `make tsan` pass, `make deep`
  **1332/1332** (0 failed), `make asan-deep` **882/882** (0 failed).

### 9. What is still open

- **Frames (`<iframe>` / `<frame>`)**: `iframe.contentDocument` /
  `contentWindow` and nested browsing contexts do not exist yet (495 listed WPT
  subtests).
- **Additional image & font formats**: PNG, JPEG, GIF, SVG, BMP, and PPM are
  decoded through `ImageDecoder`; WebP, AVIF, `@font-face` remote font loading,
  WOFF/WOFF2, and complex script shaping (HarfBuzz/bidi) remain open.
- **Networking & CSSOM corners**: CORS preflight `OPTIONS` requests,
  credentialed cross-origin cookies, HTTP/2, `ArrayBuffer`/binary XHR bodies,
  and CSSOM `sheet.cssRules` remain open.

---

## Phase 34 Architecture: A Function Can Be a Prototype

### 1. What was wrong

A function in this engine is a value, not a heap cell: a `JsValue` whose
payload its copies share. An object is a cell, and a prototype link is a
pointer from one cell to another. So a link could not point at a function,
and on `main` three things stood in for one:

- The prototype of every function was `Function.prototype` by a rule, not by
  a link, and `Function.prototype` was an object.
- A class kept its parent in a hidden property, `%super`, and a static member
  was looked for there: one step and no further. With `class A { static s()
  {} }`, `class B extends A {}` and `class C extends B {}`, `C.s()` was `s is
  not a function`. The DOM's interface objects were tied together the same
  way, so `HTMLElement.ELEMENT_NODE`, a constant of `Node` two steps up, was
  undefined.
- `Object.setPrototypeOf(g, f)` and `Object.create(f)` stored a pointer to
  the cell that holds the properties of `f`. A lookup found those properties
  through it. `Object.getPrototypeOf` made an object out of that cell: not
  `f`, and not something that can be called.

| A script | `main` | Now, and in Node |
| :--- | :--- | :--- |
| `Object.getPrototypeOf(TypeError) === Error` | `false` | `true` |
| `class Derived extends Base {}`; `Object.getPrototypeOf(Derived) === Base`, `Derived.__proto__ === Base`, `Base.isPrototypeOf(Derived)` | `false`, `false`, `false` | `true`, `true`, `true` |
| `typeof Object.getPrototypeOf(function () {})`; `Function.prototype()` | `object`; `TypeError: prototype is not a function` | `function`; `undefined` |
| A static of a grandparent: `C.s()` above | `TypeError: s is not a function` | runs |
| `class B extends A {}` with `static get tag() { return this.name; }` in `A`: `B.tag` | `A` | `B` |
| `var o = Object.create(f)`; `Object.getPrototypeOf(o) === f`; `Object.getPrototypeOf(o)()` | `false`; `TypeError: p is not a function` | `true`; what `f` returns |
| `Reflect.construct(F, [], G) instanceof G` | `false` | `true` |
| `F.prototype.m = …`, then `Object.defineProperty(F, 'prototype', { writable: false })`: how a compiled class ends | `TypeError: Cannot redefine property: prototype` (and with `prototype` never read before, it became `undefined`) | accepted, and `F.prototype = 5` then changes nothing |
| `Object.getPrototypeOf(Derived).call(this, x)`, the first thing a compiled constructor does | `TypeError: Function.prototype.call called on a non-function` | calls `Base` |
| `class C extends A { constructor() { super(); } }`; `Object.setPrototypeOf(C, B)`; `new C()` | runs `A` | runs `B` |
| `class X extends (() => 1) {}`; `Reflect.construct(F, [], () => 1)` | accepted; accepted | `TypeError`; `TypeError` |
| `Object.setPrototypeOf(Object.preventExtensions({}), {})` | accepted | `TypeError`; `Reflect.setPrototypeOf` says `false` |
| `F.prototype = 5`; `({}) instanceof F` | `false` | `TypeError` |
| `Object.getPrototypeOf(HTMLElement) === Element`; `HTMLElement.ELEMENT_NODE` | `false`; `undefined` | `true`; `1` |

Two kinds of program met it. web-platform-tests met it first:
`assert_throws_js(TypeError, …)` checks that what it was given is an error
type by walking up with `Object.getPrototypeOf` until it reaches `Error`, so
every such assertion failed, 18 subtests in the part of the suite that is
vendored (Phase 28). The other kind is most of what pages ship: a class
compiled for older browsers. Babel's helpers (`_inherits`, `_createSuper` or
`_callSuper`, `_wrapNativeSuper`) and TypeScript's `__extends` build a class
out of exactly the calls in the table, and each of the three compiled
programs in the corpus stopped at its first class.

### 2. The cell that stands for a function

A function still is a value. What changed is that it can have a cell that
knows which function it belongs to, and that every kind of function can have
one.

- **One place for the cell.** The handle to a function's cell moved from
  `FunctionData`, which only script functions have, to the payload every
  function has (`JsValue::Extra::cell`). `property_cell(true)` makes a cell
  for a script function when a property is first set on it, as before. A
  native function keeps its few properties in a table, as before, until
  `function_cell()` is called on it: that moves the table into a new cell,
  and from then on the function is stored the way a script function is.
- **`as_prototype()`** is what everything that makes a link calls
  (`Object.create`, `Object.setPrototypeOf`, the `__proto__` setter, a class
  with a parent, `Reflect.construct`). For an object it is the object's
  cell. For a function it is the function's cell, and the cell is given a
  copy of the function (`JsObject::set_function_value`).
- **`JsValue::make_object(cell)`** is what everything that follows a link
  calls. For a cell that has a function it gives back the function. That
  one line is why `Object.getPrototypeOf(Derived)` is `Base`, has `Base`'s
  identity under `===`, and can be called and constructed.

The copy is kept in `JsObject::internal`, the pointer a `Date` keeps its time
in and a `Map` its entries, which the cell of a function had no use for
(`function_value()` reads it when the cell's class is `Function`). No object
and no value grew: `sizeof(JsObject)` is 328 and `sizeof(JsValue)` 88, as on
`main`.

A function has three properties it does not store: `name`, `length`, and
for a constructor `prototype`, an object made the first time it is asked
for. They are answered where the function is the receiver, and now also
where a chain runs through a function's cell (`lookup_chain`, `has_property`
in `src/js/JsValue.cpp`), so `Object.create(f).name` is the name of `f`.
`prototype` is an own property before
anything has read it (`hasOwnProperty`, `getOwnPropertyDescriptor`, `delete`
and `defineProperty` agree: writable, not enumerable, not configurable), and
an assignment to it keeps those flags, so a loop that copies the static
members of a constructor no longer copies `prototype` with them.

### 3. The collector

The cell owns a copy of the function, and the function's payload owns a
handle to the cell. That is a cycle of two reference counts, and reference
counts never free a cycle. It is the collector's to take apart, and it is the
reason a function gets the copy only when it becomes a prototype: most
functions never do, and a closure that nothing was set on still has no cell
at all (100,000 of them: 0 cells, on `main` and now).

The collector already knew one such pair: an object held by a value that is
held by the object's cell (Phase 23, section 5). Before it marks, it counts
for each payload how many of its holders are cells; a payload with no holder
outside the heap is not a root. `JsObject::trace` reports the function copy
with the use count of the pointer that holds it
(`Heap::mark_held_value(*function, internal.use_count())`), and the payload
reports the cell it has a handle to (`PayloadInfo::cell`), so a class and its
cell are found to hold only each other and go together, and with them the
scopes the class closed over.

Two wrong versions of those lines were built and run against the tests, to
see that the tests can tell:

| Changed in `src/js/Heap.cpp` | What happens | Caught by |
| :--- | :--- | :--- |
| The function a cell stands for is not reported | Every class with a child stays for ever | `make test` stops in `gc_roots_are_complete_under_empty_root_set`, a suite of Phase 23; of the new suites `js_classes_and_their_parents_are_collected` and `js_interface_objects_are_collected_with_their_page` fail; five more cases of `prototypes.dat` fail |
| It is counted and not marked | A parent reachable only as a prototype loses what it closed over: `secret is not defined` | `js_classes_and_their_parents_are_collected`; one more case of `prototypes.dat` |

The second was passed by every test until one test called the function it
had reached through `Object.getPrototypeOf` and read a variable the function
closed over. The cell keeps the function's `name` and its properties, so
checks of those cannot see that the closure is gone.

### 4. Classes, `super` and `new`

`%super` is gone. `class Derived extends Base` links
`Derived`'s cell to `Base.as_prototype()` (`define_class`,
`src/js/JsEval.cpp`), and everything that asked `%super` asks the link:

- A static member is an ordinary lookup along the chain, to any depth, and a
  static getter runs with the class it was read from as `this`.
- `super(...)` calls whatever the prototype of the constructor is when it
  runs, as the language says, so `Object.setPrototypeOf(Derived, Other)`
  changes it. `super.name` in a static method starts one step up from the
  class the method lives in (`super_base`, `js_get_from_chain`).
- `js_is_constructor()` is one answer to "can this be constructed", asked by
  `new`, by `extends`, by `super()` and for both function arguments of
  `Reflect.construct`: a function that is not an arrow function, a method,
  an async or a generator function.
- `js_construct(ctor, args, new_target)`: with a third argument the object
  gets `new_target.prototype`, whether `ctor` is a script function or a
  built-in that makes its own object.

**`Function.prototype` is a function.** It takes anything and returns
undefined, its cell is the one every function's chain already ended in
(`function_cell(adopt)`), and it is the one built-in marked as not a
constructor: `new Function.prototype()` throws, and so does `super()` in a
class that extends null, whose parent it is. The mark is a bit of the cell
(`JsObject::not_constructor`, in the padding beside the other flags). It
was a hidden property at first, and every function then answered `true` to
`'%not-a-constructor' in f`.

### 5. `Object`, `Reflect`, `bind`, the error types

- `js_set_prototype()` refuses a link that would close a loop, as before,
  and now also a change to an object that is not extensible; setting the
  prototype an object already has is always allowed. `Object.setPrototypeOf`
  throws on a refusal, `Reflect.setPrototypeOf` returns `false`, and the
  `__proto__` setter ignores what is neither an object nor null.
- `Object.defineProperty` follows the standard's
  ValidateAndApplyPropertyDescriptor (`define_from_descriptor`,
  `src/js/BuiltinsObject.cpp`). What it used to refuse and compiled classes
  do: redefining a property that cannot be configured with the flags and
  the value it has, and taking `writable` away from one. What it used to
  accept: changing such a property's getter, or its kind.
  `Reflect.defineProperty` returns `false` where it used to throw.
- `f.bind(x)` has the prototype of `f`, so a bound class finds the static
  members of the class's parent.
- `EvalError`, `RangeError`, `ReferenceError`, `SyntaxError`, `TypeError`
  and `URIError` have `Error` as their prototype.
- `x instanceof F` throws a `TypeError` when `F.prototype` is there and is
  not an object. It has to now: `Function.prototype` is callable, so
  `x instanceof Function.prototype` no longer stops at "not callable", which
  is where it used to end. With no `prototype` at all the answer is still
  `false` (section 9 says why it does not throw).
- `{ __proto__: function () {} }` sets a prototype and defines no property,
  so the function is not named after the key. It was named `__proto__`, on
  `main` as well, where the function could not be got back to see it.

### 6. The DOM's interface objects, and the bytecode VM

`Realm::proto` (`src/js/DomBindings.cpp`) tied an interface object to its
parent with `%super`. It makes the link now, as Web IDL has it:
`Object.getPrototypeOf(HTMLDivElement)` is `HTMLElement`, then `Element`,
`Node`, `EventTarget`, `Function.prototype`. Constants are inherited to any
depth, and a class that extends `HTMLElement` has it as its prototype.

The VM has an `Object` of its own with three functions (`create`,
`getPrototypeOf`, `setPrototypeOf`, `src/js/VM.cpp`), which read and wrote
`object_val->prototype` directly. They make the same three calls the
interpreter's do.

Both files were added to this phase's pull request only after Phase 33,
whose pull request named them, was merged.

### 7. How this is verified

| Check | Result |
| :--- | :--- |
| JavaScript corpus (`make js-conformance`) | 907 / 921 in 23 files. `prototypes.dat` is new: 76 cases, 67 pass, and the 9 that do not are in section 9. The binary of `main` passes 7 of the 76 |
| The corpus in Node (`--reference "node -e"`) | 921 / 921 |
| test262, the 21 directories this change touches (`tools/conformance/run_test262.py`) | 15,686 tests: 10,835 pass, 10,727 in the binary of `main`. 108 that failed pass; none that passed fails |
| test262, all of `built-ins`, `language` and `annexB` (the same runner with those three names) | 47,967 tests: 26,012 pass, 25,893 in the binary of `main`. 122 that failed pass. Three that passed fail, for a reason that is in section 9 (below) |
| web-platform-tests (`make wpt`) | 4658 / 5192; the 18 `function-as-prototype` subtests pass and are off the list, which has 534 left, 495 of them frames |
| DOM corpus (`make dom-conformance`) | 640 / 640 in 24 files; `interfaces.dat` is new, 11 cases. The binary of `main` passes none of the 11. In jsdom (`--reference`): 640 / 640 |
| Unit tests (`make test`) | 144 / 144; five are new (a function as a prototype, in the interpreter and in the VM; classes and their parents collected; classes compiled to ES5; a class that extends a DOM interface; interface objects collected with their page) |
| Classes compiled to ES5 | Three programs in the corpus: Babel 7's helpers, Babel's newer ones (`_callSuper`), TypeScript's `__extends`. `main` stops at `Cannot redefine property: prototype` in the first two and at `call is not a function` in the third |
| A page that is not in the corpus (`tools/conformance/compare_with_jsdom.py`) | `tests/dom-conformance/open/compiled-custom-elements.js`, custom elements compiled by Babel: 4 of 9 lines as jsdom prints them (1 on `main`). The other five are section 9 |
| html5lib, XML, URL, `make diff-trees`, `make diff-xml` | 1781 / 1792, 1702 / 1702, 1188 / 1188, 0 of 6,000 trees and 0 of 4,000 documents differ: as on `main` |
| `make deep` (when `main` was three merges back: before a fix to a CSS selector, the attribute list of Phase 35 and the media work of Phase 38 came in, with seven suites) | `1332 runs over 465 pages at depth 200000 on a 8 MB stack, 0 failed; slowest 22.5 s (table [dumps])` |
| `make asan` (ASan + UBSan; at that state too, so with 137 suites. The unit tests were run again in the sanitizer build after the first two of those merges, 140 / 140; after the third, CI's sanitizer job is what ran them) | 137 / 137; JavaScript 906 / 921 (one case of deep recursion needs more stack than a sanitizer build's frames leave, as before); DOM 640 / 640; URL 1188 / 1188; XML 1702 / 1702; web-platform-tests 4658 / 5192; 1,000 generated XML documents and 1,000 round trips; `fuzz: 400 mutations, 0 finding(s)`, `pipeline fuzz: 400 runs … 0 finding(s)`, `fuzz xml: 600 mutations … 0 finding(s)`; `882 runs over 315 pages at depth 10000 on a 2 MB stack, 0 failed`. No report from either sanitizer |
| Two pages alive at once, one destroyed and collected while the other runs on | The chain questions get the same answers in each throughout, and the cell count comes back to where it started: a block of the last new unit suite, and the same sequence as a side harness in the sanitizer build, with no report |

**What test262 found.** The corpus here is recorded from Node and was
written with the change; test262 was written by neither. It was run in the
binary of `main` and in this one, for the difference and not for the score.
(69% of these directories pass. Of the 4,851 tests that do not, 1,259 write
an identifier with a Unicode escape, `\u{6F}`, which the lexer does not
read, and 1,027 are scripts that should be refused before they run and are
not.) The first run had two tests that passed on `main` and failed
here, and both were true: the `instanceof` and the `__proto__:` of section
5, each a fault that only showed once a function could be got back from a
link. Both are cases of `prototypes.dat` now.

The rest of the suite was run once after that. Three tests that pass on
`main` fail: `AsyncFunctionPrototype-is-not-callable.js` and the same
question for generator and async generator functions. They ask that the
prototype of an async function is an object that cannot be called. Here it
is `Function.prototype` itself, on `main` as now, and the tests passed for as
long as `Function.prototype` was not callable. They stay failing: what they
ask for is three objects the engine does not have (section 9). Four more
tests end with the process out of memory in both builds, which is two older
faults: `Promise.all`, `any` and `race` read an iterable to its end before
they look at an element, so one that never ends is never left; and an index
near 2^32 makes an array that long, since arrays have no holes.

### 8. What it costs

Release build, each measure in the binary of `main` (at `5e573f5`, the
commit before the URL work of PR #38) and in this one, turn about, three
processes each; the best of five repetitions in a process, with a collection
between them. Milliseconds.

| | `main` | Now |
| :--- | ---: | ---: |
| Read an own property, 2,000,000 times | 462 | 448 |
| Read through 20 prototypes, 300,000 | 82 | 82 |
| Call a method of an instance, 1,000,000 | 591 | 573 |
| Call a method inherited through 3 classes, 1,000,000 | 606 | 597 |
| Call a function, 2,000,000 | 1075 | 1026 |
| `f.call(o, i)`, 1,000,000 | 787 | 802 |
| `f.apply(null, args)`, 500,000 | 414 | 415 |
| `f.bind(o)` and one call of it, 200,000 | 271 | 297 |
| `new F(i)`, 500,000 | 449 | 462 |
| `new D(i)` with three `super()` calls, 200,000 | 751 | 746 |
| A class and a class that extends it, declared 20,000 times | 60 | 59 |
| Call a static method of the class, 1,000,000 | 571 | 564 |
| Call a static method through 3 parents, 500,000 | throws (section 1) | 332 |
| `d instanceof A` through 3 classes, 1,000,000 | 262 | 256 |
| `f.name`, `f.length` and `f.prototype`, 500,000 | 248 | 252 |
| `Object.defineProperty`, 200,000 | 318 | 327 |
| `Object.create(p)` and `Object.getPrototypeOf`, 300,000 | 241 | 248 |
| Throw and catch a `TypeError`, 100,000 | 469 | 491 |
| Make a closure and call it, 500,000 | 390 | 379 |
| Make 50,000 objects with a closure each, then `gc()` | 52 | 53 |

Nothing that was measured got faster or slower by more than the machine
varies from run to run, except `bind`, which now asks for its target's
prototype (about a tenth more for making a bound function; calling one
costs what it did).

The table is from a quiet machine. A repeat on a busy one was thrown away:
there, seven runs of one binary on one loop differed by up to a third in
processor time, more than any row above. With a copy of `main`'s binary run
beside `main`'s as the control, the best run of this build lay between the
two for three million calls (2.14 s; 2.07 and 2.21), below both for a
million closures, and 13% above both for `bind`.

In memory:

- A realm has 11 more cells that stay (55 where a script begins, 44 on
  `main`): the error types have cells of their own, and so has each
  interface object with a parent. One that a page goes on to use costs a
  cell more than it did (`HTMLDivElement`, never used before: 3 cells where
  it was 2).
- A built-in function that becomes a parent or a prototype gets a cell,
  which stays as long as the built-in does: one for `Map` however many
  classes extend it.
- 2,000 pairs of classes: 10,000 cells while they are held, none after, on
  `main` and now.
- A chain of 200,000 classes, each the parent of the next: made in 0.4 to
  0.5 s, 400,002 cells, 0.4 s for a collection while it is held and 0.6 s
  for the one that frees it, all as on `main`. A static member of the first
  class is read from the last in 25 to 44 ms, and the walk up with
  `Object.getPrototypeOf` is 200,001 steps in 0.2 to 0.3 s; on `main` the
  member is `undefined` and the walk is one step.

### 9. What is still open here

Nine cases of `prototypes.dat` failed when this phase ended, each recorded
for one of the first seven of these, and everything in the list was the same
on `main`. Phase 43 has since done the first item and the first half of the
last, and eight cases fail:

- *(Done in Phase 43.)* **`new.target` is the function that is running**, not the one `new` was
  applied to. `if (new.target === Shape) throw …` in a parent's constructor,
  the usual way to write an abstract class, stops `new Circle()`; and with
  `Reflect.construct(F, [], G)` the object has `G.prototype` and `new.target`
  inside `F` is `F`. The DOM needs the same value:
  `Reflect.construct(HTMLElement, [], Derived)` is `Illegal constructor`,
  so a custom element compiled by Babel cannot be made (the five lines of
  `compiled-custom-elements.js`).
- **A built-in does not make the subclass it was called on.**
  `class List extends Array {}`: `List.from([1])` and `new List().map(f)` are
  plain arrays, `class P extends Promise {}`: `P.resolve(1)` is a plain
  promise. There is no `Symbol.species`.
- **`instanceof` does not ask `Symbol.hasInstance`**, and answers `false`
  where it should throw for a right side with no `prototype` at all (an
  arrow function, a method, `Math.max`). It cannot throw there yet: a bound
  function has none either and should be asked for its target's
  (`new F() instanceof F.bind(null)` is `false`), and six of the host's
  constructors have none (`Response`, `Headers`, `Worker`, `WebSocket`,
  `IntersectionObserver`, `ResizeObserver`), where `r instanceof Response`
  must not start to throw.
- **What a function's own properties are.** `Object.getOwnPropertyNames(f)`
  lacks `length`, `name` and a `prototype` nothing has read; `f.name = 'x'`
  takes effect. An async function and a generator function have
  `Function.prototype` itself as their prototype, where the language has an
  object of its own for each kind between the two. `typeof` of that
  prototype is therefore `function` now and was `object` before, and three
  test262 tests that ask for `object` passed on `main` and fail here.
- **Which built-ins are constructors.** Nothing tells `Math.max` from `Map`:
  `new Math.max()` makes an object, and `class X extends f.bind(null)` is
  accepted for any `f`.
- **A property of a prototype that cannot be written does not stop an
  assignment** to the object that inherits it.
- **A function bound to an object that holds it is not collected**:
  `o.b = f.bind(o)` 2,000 times leaves 2,000 cells after `gc()`. The
  collector cannot see into what a native function captured. It is part of
  "no leak audit has been done" in Known gaps, 4.
- *(The first half done in Phase 43.)* **An object that a parent's
  constructor returns does not become `this`** in the child: after `super()`
  the child still has the object `new` made. And `super.x` in a method of a
  class that extends null is `undefined`, not a `TypeError`. (Found by
  test262; no case here yet.)

Three things were found on the way that are not about prototypes. All are
on `main`, and all are rows of the task table now:

- **A page that ends while a worker is starting can end the process.** Four
  `new Worker('data:text/javascript,postMessage(1)')` and nothing else:
  7 of 60 runs of the release build of `main` end in `SIGSEGV`. The
  sanitizer build reports a read of freed memory on the worker's thread, in
  a table that process exit had destroyed.
- **A script can read and write the engine's own slots.** They are
  properties whose names begin with `%`: `el['%classList'] = 5` makes
  `el.classList` a number, and `method['%home']` is the object `super` is
  looked up from.
- **A string built half a surrogate pair at a time costs the square of its
  length**: `s += '\uD83D'; s += '\uDE00'` 20,000 times takes 4.3 s, and
  5 ms a pair at a time. One case of `long-strings.dat` does that and takes
  6 s of the 10 a case is allowed; on a busy machine it times out.

---

## Phase 35 Architecture: Attribute-List End Removals Are Linear

### 1. What was wrong

`AttributeList` kept the attributes in a `std::vector` and, once a list was
long enough to index, kept a parallel `std::vector<std::size_t>` of the
numbers each name was given. The number index avoided rebuilding positions
on every erase, but `erase(iterator)` still erased from both vectors at the
same offset. Removing the first attribute moved every remaining string pair
and every remaining number: draining 50,000 attributes from the front was
quadratic. The recorded script loop took about 6 s (9 s in the older
measurement); the same removals from the back were tens of milliseconds.

### 2. What changed

Only `include/litebird/dom/AttributeList.hpp` changed. The items and
`Index::numbers` remain `std::vector`s, but each has a logical start offset.
The public iterator aliases are still vector iterators, so
`attributes.begin() + index` in the `NamedNodeMap` bindings and
`found - list.begin()` in callers/tests still work. The lazy index is
unchanged in shape: lists shorter than 32 are scanned; longer lists build a
name-to-number map and an ascending number sequence, and `position()`
bisects the live number range. A back erase pops the vector; a front erase
advances the start and leaves a dead prefix. `compact()` drops that prefix
only when it is at least as large as the live tail, which makes repeated
front erases amortized O(1) and a full drain linear, while an empty list
still allocates nothing. A middle erase removes the name from the map,
erases the parallel number, and moves the vector tail. Copy copies only the
live range and drops the index; move resets the source start so a moved-from
list is empty; `clear`, value update in place, insertion order,
`operator[]`, `find/count/erase` and order-insensitive equality keep their
semantics. `AttributeNamespaces` is a separate side table and was not
changed.

### 3. Measured

Whole-process times, best of three, same machine and hour; baseline is the
pre-change binary saved as `build/litebird-attributes-baseline`:

```bash
python3 tools/bench/bench_attributes.py \
  --baseline build/litebird-attributes-baseline --reps 3 --max-ms 3000
```

| Case (50,000 attributes unless noted) | Baseline | Current | Speedup |
| :--- | ---: | ---: | ---: |
| front: remove a0..aN | 7,132 ms | 90 ms | 79.0× |
| back: remove aN..a0 | 115 ms | 114 ms | 1.0× |
| ends: alternate front/back | 3,481 ms | 113 ms | 30.8× |
| front half, then update/reinsert | 5,293 ms | 69 ms | 76.8× |
| namespaces: front removal | 8,465 ms | 197 ms | 43.0× |
| short lists: churn below index | 79 ms | 73 ms | 1.1× |

Every case prints the same expected DOM checksum in both binaries. A
script-only `Date.now()` probe measured the front removal loop at 6,078 ms
before and 51 ms after; the back loop stayed in the tens of milliseconds.
The checked-in guard is `make bench-attributes`: six cases, three
repetitions, expected checksums, 3,000 ms whole-process limit. It fails on a
crash, timeout, wrong checksum or slow case.

### 4. Verification

`make test` passes 135 suites. The two new suites are
`attribute_list_end_removals_keep_order_and_index` (model comparison across
the scan/index boundary, both erase overloads, reinsert/update, copy/move,
moved-from emptiness, late index build after a dead prefix, clear and
random-access iterators) and
`attribute_list_front_removal_is_linear_in_practice` (50,000 direct and
script front removals with generous wall-clock guards). `make
bench-attributes` passes 6 cases with 0 failures. A generated out-of-corpus
probe run through `tools/conformance/compare_with_jsdom.py` printed `4 lines,
4 the same` before and after. The merged release run kept html5lib 1781/1792,
JS 840/845, DOM 629/629, URL 1188/1188, XML 1702/1702, WPT 4640/5192,
diff-trees/diff-xml 0 differing, deep 1332 runs/0 failed (slowest 25.0 s),
bench-css 20 padded runs/0 failures, security-fetch 3 checks/0 failures, and
diff-strings at the 95-difference ceiling. The sanitizer scoreboards are
recorded in AUDIT.md, finding 40.

### 5. What is still open

This fixes end removals, not arbitrary middle removals: a middle erase still
moves the vector tail. The HTML tokenizer's repeated-name search for one
element with 50,000 attributes was fixed in Phase 39 (finding 46). The
`Attr`-is-not-a-`Node`, one-qualified-name-in-two-namespaces and collection
named-property rows remain open. No parser, layout, CSS or JavaScript engine file was
changed.

---

## Phase 38 Architecture: Real-Web JPEG/GIF Decoding, SVG & Canvas 2D

Phase 38 replaces Litebird's toy vector/canvas rasterizers and fills the missing
real-web raster image decoders (JPEG and GIF) with zero-dependency C++20
implementations verified pixel-by-pixel against `libjpeg` and ImageMagick.

### 1. Standalone ITU-T T.81 JPEG Decoder (`JpegDecoder.hpp`, `src/render/JpegDecoder.cpp`)

- **Markers & frame modes:** `SOI`/`EOI`, `DQT` (8-bit and 16-bit), `DHT`
  (9-bit fast lookup table with 16-bit bit-by-bit fallback and over-subscription
  checks), `DRI` / `RST0`–`RST7`, `APP0` (JFIF), `APP14` (Adobe color transform),
  `SOF0` (Baseline DCT), `SOF1` (Extended Sequential DCT), and `SOF2`
  (Progressive DCT).
- **Progressive scans:** Maintains per-component DCT coefficient buffers across
  arbitrary scan scripts: interleaved/non-interleaved DC first (`Ah=0`) and DC
  refinement (`Ah>0`), AC first with EOB runs (`EOB0`–`EOB14`, `ZRL`), and AC
  successive-approximation refinement.
- **IDCT, chroma upsampling & color:** Separable double-precision 8×8 IDCT with
  DC-only row/column fast paths, centered `[3/4, 1/4]` triangle chroma
  upsampling (4:4:4, 4:2:2, 4:2:0, 4:4:0, odd dimensions), and Grayscale,
  YCbCr, RGB, CMYK, and Adobe YCCK conversions.
- **Security bounds:** Rejects streams exceeding `kMaxDecodedPixels` (16M px) or
  `kMaxScans` (64), checks all segment lengths and Huffman trees, and verifies
  against `RasterBudget`.

### 2. Standalone GIF87a / GIF89a Decoder (`GifDecoder.hpp`, `src/render/GifDecoder.cpp`)

- **LZW & palettes:** Full 2..8-bit variable-width LZW (up to 12-bit codes) with
  deferred clear-code support when the 4096-entry table fills, Global and Local
  Color Tables, and 4-pass GIF interlacing.
- **Animation & disposal:** Graphic Control Extensions (transparency index,
  centisecond frame delays, all four disposal methods: `None`, `DoNotDispose`,
  `RestoreBackground`, `RestorePrevious`), `NETSCAPE2.0` loop count, and
  sub-rectangle compositing onto a persistent canvas buffer.
- **Bounds:** Enforces `kMaxDecodedPixels` and `kMaxFrames` (default 256).

### 3. Upgraded Canvas 2D (`Canvas2D.hpp`, `src/render/Canvas2D.cpp`)

- **Transform & state stack:** Full 2D affine Current Transformation Matrix
  (`TransformMatrix2D`) with `save()`/`restore()`, `translate()`, `rotate()`,
  `scale()`, `transform()`, `setTransform()`, `resetTransform()`, `globalAlpha`,
  and an intersectable alpha clip mask (`clip()`).
- **Scanline rasterizer & stroking:** 4× vertical + analytical horizontal
  anti-aliased scanline fill supporting `FillRule::NonZero` and
  `FillRule::EvenOdd`; polygon-ribbon thick stroking with `butt`/`round`/
  `square` caps, `miter`/`bevel`/`round` joins, dash arrays (`setLineDash`/
  `getLineDash`/`lineDashOffset`), and `stroke_gradient`.
- **Paths, gradients, pixels & JS:** `roundRect`, `ellipse`, `arcTo`, `rect`,
  `isPointInPath`, inverse-CTM linear/radial gradients with `Pad`/`Reflect`/
  `Repeat` spread modes, `createImageData`/`getImageData`/`putImageData`,
  CTM-aware `drawImage`, and real JS property accessors via
  `JsValue::define_accessor` (`globalAlpha`, `lineCap`, `lineJoin`,
  `miterLimit`, `lineDashOffset`).

### 4. Real-World SVG Renderer (`SvgRenderer.hpp`, `src/render/SvgRenderer.cpp`)

- **DOM & CSS integration:** Iterative ID indexing (safe on 200,000-deep DOMs),
  `<defs>` exclusion, presentation attributes + inline `style` parsing,
  `#RGBA`/`#RRGGBBAA`/`rgba()` colors, `currentColor` inheritance, `opacity` /
  `fill-opacity` / `stroke-opacity`, `fill-rule`, `display:none` /
  `visibility:hidden`.
- **Geometry & paint servers:** Full SVG path grammar including W3C F.6.5/F.6.6
  `A/a` elliptical arcs, `S/s`/`T/t` smooth curves, compact arc flags, and
  scientific notation; `viewBox` + `preserveAspectRatio` (`meet`/`slice`/`none`);
  full `transform` list stack; `<linearGradient>`/`<radialGradient>` with `href`
  inheritance, `objectBoundingBox`/`userSpaceOnUse`, `gradientTransform`, and
  `spreadMethod`; `<use>` (with recursion/cycle guard) + `<symbol>`;
  `<clipPath>`; rounded `<rect rx ry>`; nested `<svg>`.

### 5. Verification & what remains open

`make verify-media` compares 21 generated JPEGs against `libjpeg` (52–99 dB
PSNR, MAE ≤ 0.39), 9 single/animated GIFs against ImageMagick `-coalesce`
(exact `max_diff = 0` on every frame), and 5 SVG feature suites. `make fuzz-media`
mutates JPEG, GIF, and SVG inputs (1,800 cases in Release, 4,500 under
ASan+UBSan+LSan). Wiring `JpegDecoder` and `GifDecoder` into `ImageDecoder.cpp`
is deferred until PR #39 merges; WebP/AVIF, SVG `<text>`/CSS stylesheets/filters,
and Canvas 2D text/shadows/patterns remain open (see `AUDIT.md`, finding 43).

---

## Phase 39 Architecture: The Tokenizer's Repeated Attribute Names Are Linear

### 1. What was wrong

`HtmlTokenizer::consume_tag` drops a repeated attribute name — the standard's
*duplicate-attribute* parse error, where the first attribute with that name
wins — by comparing the name with every attribute the token has already
accepted (`std::any_of` over `token.attributes` in the `flush_attribute`
lambda). Every attribute of a tag therefore costs as much as the attributes
before it: the square of the count. Phase 28 indexed the element's own
attribute list, which took one element with 50,000 attributes from 10.4 s to
2.9 s in the parser; the tokenizer's own search was what was left. An end tag
paid it too, and its attributes are then thrown away by the tree builder. The
XML parser had already solved the same problem with a set (`XmlParser.cpp`,
the duplicate-name checks).

### 2. What changed

Only `src/html/HtmlTokenizer.cpp` changed. `consume_tag` keeps a
`std::unordered_set<std::string_view>` of the accepted names, built the first
time a tag reaches 32 accepted attributes. Below that the scan stays: for the
handful of attributes an ordinary element has, comparing a few short strings
in a contiguous vector costs less than hashing into a table, and no table is
allocated at all.

The set holds views of the names **inside** `token.attributes`, so no
attribute name is stored twice. That is only safe while those strings stay
put: appending to the vector can reallocate it, and a short name lives inside
its `std::string` object, so it moves with it. The index therefore records the
vector's capacity and is rebuilt whenever it changes; between rebuilds no
accepted name moves. Capacity grows geometrically, so the rebuilds together
cost O(n) over the tag, and the whole check is linear. Nothing else about the
state machine changed: names are ASCII-lower-cased before they reach the
check (which is what makes it the standard's case-insensitive comparison),
the first value wins, order is the order of first occurrence, valueless and
quoted and unquoted forms behave as before, `=` may still start a name, an
end tag's attributes are still tokenized, and EOF inside a tag still drops
the token.

### 3. Measured

Whole-process times (`--dump-dom-webplatform`: parse and tree dump, no layout,
no scripts), best of three, idle machine, same hour. The baseline is this
merged tree built with `main`'s tokenizer, kept as
`build/litebird-final-baseline`, so the only difference is this change:

```bash
python3 tools/bench/bench_tokenizer_attributes.py \
  --baseline build/litebird-final-baseline --reps 3 --max-ms 1000
```

| Case (50,000 attribute occurrences) | Baseline | Current | Speedup |
| :--- | ---: | ---: | ---: |
| distinct names | 3,474 ms | 93 ms | 37.2× |
| each name twice | 1,150 ms | 39 ms | 29.6× |
| uppercase repeats | 1,320 ms | 35 ms | 37.6× |
| long names, each twice | 3,810 ms | 71 ms | 53.5× |
| quoted, bare, unquoted | 772 ms | 27 ms | 28.2× |
| many small tags | 39 ms | 40 ms | 1.0× |
| end tag with attributes | 3,170 ms | 25 ms | 125.5× |

Every dump equals the tree the markup must produce, computed by the bench
from the markup rather than recorded from a binary, and equals the baseline
binary's dump byte for byte. The square is gone: the distinct-names case at
25,000/50,000/100,000/200,000 attributes now takes tens to hundreds of
milliseconds (45/93/183/344 ms measured across runs), against 628 ms at
25,000 and 3,474 ms at 50,000 before: twice the input cost 5.5× before and
about 2× now. Tokenizing alone, 50,000 attributes cost 28 ms in the release
build and 102 ms under ASan/UBSan. The checked-in guard is
`make bench-tokenizer-attributes`: seven cases, three repetitions, expected
trees, 1,000 ms whole-process limit — far
below the 3.5 s the scan costs, and far above what a shared runner needs.

### 4. Verification

`make test` passes 159 suites. The two new suites are
`tokenizer_repeated_attribute_names_keep_the_first` (the fixed shapes:
first-wins, ASCII-only folding, `Ä`/`ä` distinct, `=`-starting names and the
after-attribute-name `=`, entities in values, `/` in a tag, end tags, EOF
dropping the token; every size around the index threshold and around each
vector growth from 1 to 257; 60 random tags against a set-based model; the
same shapes through the parser and the DOM) and
`tokenizer_duplicate_attribute_check_is_linear` (50,000 distinct attributes,
50,000 occurrences of 25,000 long names in both cases, an end tag with 50,000
attributes, and 5,000 ordinary four-attribute tags, each with a 2 s wall-clock
guard). Both were checked against mutants: with the old scan restored the
correctness suite passes and the timing suite fails at `distinct_seconds <
2.0`; with the capacity rebuild removed the correctness suite fails on the
first size past a vector growth, which is the dangling-view bug the rebuild
exists to prevent.

Out-of-corpus pages run through `tools/conformance/compare_with_jsdom.py`
before and after printed the same counts: `16 lines, 16 the same` (duplicates
through `innerHTML`, `DOMParser`, `insertAdjacentHTML`, SVG foreign
attributes, table cells, a 300-attribute element), `20 lines, 20 the same`
(the sizes around the threshold and every growth, whitespace forms, form
controls) and `6 lines, 6 the same` (`=`-starting names). No corpus
expectation was re-recorded.

The release run on the merged tree kept every scoreboard: html5lib
1781/1792, JS 907/921, DOM 640/640, URL 1188/1188, XML 1702/1702, WPT
4658/5192 (534 listed, 0 unlisted); diff-trees 6,000 pages and diff-xml 4,000
documents with 0 differing, XML round trip 3000/3000; deep 1,332 runs over
465 pages at depth 200,000 on an 8 MB stack, 0 failed (slowest 19.3 s);
bench-css 20 padded runs, bench-attributes 6 cases,
bench-tokenizer-attributes 7 cases, security-fetch 3 checks and
security-worker 17 checks, all with 0 failures; verify-media 35 checks and
fuzz-media 1,800 cases clean;
diff-strings at its 95-difference ceiling. Peak RSS for the 50,000-attribute
page was 16.1 MB in both binaries: the index is freed when the tag ends and
the dump reuses its memory.

The sanitizer run on the same tree reported nothing: ASan/UBSan 159/159
suites, JS 906/921 (one fewer than release: a deep-recursion case needs more
stack than a sanitizer build's frames leave, as before), DOM
640/640, URL 1188/1188, XML 1702/1702, WPT 4658/5192; ASan diff-xml 1,000
documents with 0 differing and 1,000/1,000 round trips; fuzz 400 parser
mutations, 400 pipeline runs and 600 XML mutations with 0 findings; ASan deep
882 runs over 315 pages at depth 10,000 on a 2 MB stack, 0 failed (slowest
2.7 s). `make tsan` passed (the string type under ThreadSanitizer, then under
ASan with leak checking).

### 5. What is still open

This is a cost fix in one function, not a parser conformance change: the
tokenizer's other states, the tree builder (including the eleven remaining
html5lib `<select>` cases), and the XML parser were not touched. Finding 28's
other costs — a block inside an inline box, 10,000 sized floats in one box —
and finding 40's middle erases are still open, as are the
`Attr`-is-not-a-`Node`, one-qualified-name-in-two-namespaces and collection
named-property rows. (Since then: the finding-28 layout costs were both
fixed, the block inside an inline box in Phase 36 and the sized floats by the
band-edge sweep; finding 40's middle erases remain.)

---

## Phase 40 Architecture: Worker MIME/Module Policy, EventTarget Dispatch & Lifecycle Safety

Phase 40 completes the remaining Web Worker policy and lifecycle gaps in
`include/litebird/js/Worker.hpp` and `src/js/Worker.cpp` while preserving the
Phase 33 `ResourceLoader::load_for_page` local-file security boundary.

### 1. Strict JavaScript MIME Policy & HTTP Status Checks

- `WorkerObject::is_javascript_mime_type(std::string_view)` implements WHATWG
  MIME Sniffing §4.3: it strips MIME parameters after `;`, trims ASCII HTTP
  whitespace, ASCII-lowercases the essence, and accepts only the 16 standard
  JavaScript MIME types (`text/javascript`, `application/javascript`,
  `application/ecmascript`, `text/javascript1.0`–`1.5`, etc.).
- Both top-level `new Worker(url, options)` fetches and worker-side
  `importScripts(...urls)` route through `load_worker_script()`, requiring
  `ResourceLoader::load_for_page` success, an HTTP `2xx` status code, a
  non-empty `Content-Type`, and a valid JavaScript MIME essence (no MIME
  sniffing). `data:text/plain`, default `data:,...`, HTTP `text/plain`,
  `text/html`, `application/json`, missing `Content-Type`, HTTP `3xx`/`4xx`,
  and local `.txt` files are refused cleanly with an error event.

### 2. `WorkerOptions` Validation, Module Policy & Worker Global Scope

- `new Worker(...)` validates construction (`new` required, at least 1
  argument, `options` nullish or object, `options.type` in `{"classic",
  "module"}` else synchronous `TypeError`).
- `{ type: "module" }` is no longer silently executed as classic JavaScript:
  `start()` refuses execution and queues an `ErrorEvent` delivered to both
  `worker.onerror` and `worker.addEventListener("error", ...)`.
- Inside `DedicatedWorkerGlobalScope`, `self` and global bindings expose
  `self.name` (`options.name`), `self.location` (parsed via `Url::parse`),
  `self.postMessage`, `self.close`, `self.addEventListener`,
  `self.removeEventListener`, `self.importScripts`, `self.fetch`, and
  `self.Worker`.

### 3. Complete `EventTarget` Dispatch & Owner-Thread State Isolation

- Page-side `Worker` (`attach_js_instance`, shared by both `create_constructor`
  and `create_js_instance`) and worker-side `self` share a `ListenerMap`
  supporting function and `{ handleEvent }` listeners, duplicate suppression
  via `strict_equals`, `removeEventListener`, `{ once: true }`,
  `preventDefault()`, `stopPropagation()`, and `stopImmediatePropagation()`.
- Error events populate `type`, `message`, `filename`, `lineno`, `colno`,
  `error`, `cancelable: true`, `defaultPrevented`, `target`, and
  `currentTarget`, and invoke both `onerror` and all `"error"` listeners.
  Uncaught worker errors first notify `self.onerror` and `self`'s `"error"`
  listeners; if not canceled, they forward to the parent worker's `"error"`
  listeners and `onerror`.
- All owner-thread `JsValue` handles (`js_instance`, `onmessage`, `onerror`,
  listener lists) live in `WorkerOwnerJsState` and are only read, mutated, or
  released on the owner thread so non-atomic `JsObject::handle_count` never
  races across threads. Releasing `WorkerOwnerJsState` on `terminate()` or
  after the worker's final exit task breaks the previous `inst` ↔ `WorkerObject`
  reference cycle.

### 4. Concurrent Startup, Nested Workers & Exit-Safe Shutdown

- `worker_thread_main()` serializes `JsInterpreter` construction behind a
  process-wide mutex (skipping startup if already terminated) and attaches each
  parent worker's `EventLoop` via a lifetime-guarded `WorkerOwnerLoopState`
  with a wake callback so nested child workers wake their parent loop without
  polling and are always terminated and joined before the parent's `EventLoop`
  is destroyed.
- Every live worker registers a `std::weak_ptr<WorkerObject>` in an immortal
  `WorkerRegistry`. On first `WorkerObject::start()`, after warming up static
  tables used by workers (`JsLexer`, `JsOps`'s `symbol_registry()`, and
  `DomBindings`'s interface table), `std::atexit` registers
  `atexit_shutdown_workers()`, which wakes and joins every active worker thread
  before C++ tears down static containers.

### 5. Verification & what remains open

- `make test`: **157/157** unit suites pass in both Release (`-O2`) and
  ASan+UBSan (`build-asan/test_suite`).
- `make security-worker`: **17/17** end-to-end checks pass against a live
  loopback HTTP server (including **60/60** concurrent 4-worker startup/exit
  CLI runs in both Release and ASan+UBSan; additionally verified
  **200/200** consecutive runs with 0 crashes, vs 4/30–7/60 `SIGSEGV` before).
- Remaining gaps: ES module worker execution (requires an engine-wide ES module
  loader), structured clone beyond JSON (`ArrayBuffer`/transferables/cyclic
  objects), `SharedWorker`/`MessagePort`, and heap-allocating `symbol_registry()`
  in `src/js/JsOps.cpp` and `s_registry` in `src/js/DomBindings.cpp` as
  defense-in-depth (see `AUDIT.md`, finding 45).

---

## Phase 41 Architecture: A String Built Half a Surrogate Pair at a Time Is Linear

### 1. What was wrong

`JsString` is a handle on a range of a block of UTF-8 bytes that never change
once written (Phase 23, AUDIT.md finding 25). `a + b` writes `b` into the room
after `a` when `a` ends where the block's text ends, and blocks that keep being
extended are given room to grow by half, so `s += x` in a loop costs what it
writes. One join could not do that. When `a` ends in a high surrogate half and
`b` starts with the low half, the two halves are one character: four bytes that
start three bytes *before* the end of `a`. Writing them would change bytes that
another handle — `var kept = s` before the pair was completed — is still
reading, so `merge_halves` copied the whole of both texts into a new block.
The new block had no spare room and no history of moves, so the append after it
copied as well. A string built one half at a time copied itself at every pair:
20,000 pairs took 5.6 s and 40,000 took 21 s, where a pair at a time took 9 and
15 ms. The corpus case that does this 60,000 times took 5.68 s of the 10 s a
case is allowed, so it timed out on a busy machine, and the JavaScript floor in
CI had to sit one below the score for that case alone.

### 2. What changed

Only `src/js/JsString.cpp` and `include/litebird/js/JsString.hpp`. A join does
not write a high half that ends its result; the handle **owes** it.

- The handle carries a fifth 32-bit field, `m_pending`: 0, or the high half
  owed at the end. `m_units` counts the half, `m_bytes` does not, and `size()`
  reports the bytes the text has, half included. `is_ascii()` is false for a
  handle that owes one, and `is_well_formed()` is false without scanning.
- `concat_settled` appends everything but a trailing high half and owes that
  half. `concat_owed` decides what becomes of a half the left side owes: if the
  text arriving starts with a low half, the character they spell is appended
  where the block's text ends (four bytes, two code units, nothing copied);
  otherwise the half is written first, which is an ordinary append too. A join
  of two handles passes the right side's bytes without its owed half and owes
  that half itself, so nothing is written to be joined.
- Anything that reads the text settles the handle first: `view()`, `wide()`,
  `str()`, `substring()`. `unit_at()` does not need to: the half is the last
  unit, and everything before it is in the block. Settling writes the three
  bytes into the room after the block's text when that room is still free,
  claimed by the same compare-and-swap an append uses, and otherwise copies the
  text once into a block of its own. That copy hangs off a list on the handle's
  block, which holds one claim on it, so it lives as long as any handle that
  can still ask for it — the same arrangement as the cached UTF-16 form. The
  list only grows and each node is whole before it is linked, so readers walk
  it without a lock. A settled handle is an ordinary range of a block.
- `merge_halves` stays for a high half that is already in a block (a literal,
  `String.fromCharCode`, a piece): those bytes may be read by another handle,
  so that join still copies.

The handle is 32 bytes where it was 24 (a pointer and five 32-bit fields, one
word of padding), and `JsValue` is 96 where it was 88. The unit test that pins
the size was updated with the reason.

### 3. Measured

Whole-process times, release build, idle machine, `--run-js`. The baseline is
the branch's base commit (`ce37948`), which is `main`'s `JsString`, kept as
`build/litebird-surrogate-baseline`; these scripts run no layout, so the only
difference on their path is this change.

| Pairs appended | Half at a time, before | Half at a time, after | Speedup | A pair at a time, both |
| ---: | ---: | ---: | ---: | ---: |
| 5,000 | 325 ms | 7 ms | 43.5× | — |
| 10,000 | 1,390 ms | 11 ms | 127.1× | — |
| 20,000 | 5,583 ms | 18 ms | 308.0× | 11 ms |
| 40,000 | 23,252 ms | 30 ms | 782.1× | 15 ms |

`make bench-surrogates` is the checked-in guard (three cases, best of three,
results compared with Node.js, one-second limit): 17 ms, 28 ms and 14 ms,
against 5,546 ms, over 8 s and 3,856 ms for `main`'s binary, which fails all
three. Counting work instead of timing it, 20,000 pairs built half at a time
copy 399,370 bytes into 152 blocks for 100,000 bytes of text; a pair at a time
copies 399,220 bytes into 22 blocks. The corpus case went from 5.545 s to
0.035 s, printing the same line.

The wider handle is not free, and the cost is measured: across the 39 string
benchmarks that were already there, 2,521 ms before and 2,640 ms after (1.05×),
median case 1.04×, worst 1.52× (`read: for-of, 200k non-ASCII`, 42 → 64 ms),
best 0.60×. An earlier run of the same two binaries came out at 2,661 → 2,452 ms
(median 0.92×), so the difference is close to this machine's run-to-run spread.
Repro: `python3 tools/bench/bench_strings.py --binary … --reference '' --reps 3`.

### 4. Verification

`make test` passes 162 suites. The two new ones are
`js_string_owes_a_trailing_high_half_until_it_is_read` (what script can see of
the owed half: nothing is written when it is owed, the text reads as it always
did, a handle kept before the completion still ends in a lone half, settling
copies three bytes once and never again, a settle whose room was taken copies
once and outlives the handle that took it, and every way round gives the same
bytes) and `js_string_built_half_a_pair_at_a_time_costs_what_it_writes` (the
work counted, and 6,000 kept prefixes each ending in the half they were given).
With the deferral turned off — `ends_in_high_half` always false, which is the
old behaviour — the first fails at once and the second fails at
`bytes_copied < 2000000`. The random UTF-16 model test in
`tests/js_string_stress.hpp`, which `make tsan` also runs, generates lone
halves in its alphabet and concatenates, slices and appends with reads in
between; it passes unchanged.

The JavaScript corpus scores 907/921 with 0 timeouts, `long-strings.dat`
34/34. The floor in CI is now that score, 907, as the comment above it said to
do when this row was fixed. Out-of-corpus, 40 surrogate-heavy programs run in
Node.js and in both binaries print the same: 0 mismatches; and two pages' worth
of DOM text built half a pair at a time through
`tools/conformance/compare_with_jsdom.py` print `11 lines, 11 the same` and
`12 lines, 12 the same` on both binaries.

The settle path is shared state, so it was run on its own under the sanitizers
too: 200 rounds of seven threads settling copies of one handle that owes a
half, reading one handle from several threads at once and taking pieces, while
another thread appends to the same block so the room is sometimes free and
sometimes taken. No race, no leak, every answer right, under ThreadSanitizer
and under ASan/UBSan with leak checking on.

The full chain on the merged tree (`main` at `e007fa2`, PR #51's float
placement included): release — 162 suites, html5lib 1781/1792, JS
907/921, DOM 640/640, URL 1188/1188, XML 1702/1702, WPT 4658/5192 (534 listed,
0 unlisted), diff-trees and diff-xml 0 differing, diff-strings at its ceiling,
deep 1,332 runs over 465 pages at depth 200,000 (0 failed, slowest 23.4 s),
bench 42 / bench-surrogates 3 / bench-css 20 / bench-attributes 6 /
bench-tokenizer-attributes 7 / security-fetch 3 / security-worker 17 with 0
failures, verify-media 35 checks. Sanitizers — 162 suites, JS 906/921 (one
fewer: deep recursion), XML 1702/1702, WPT 4658/5192, 1,000 generated XML
documents and 1,000 round trips, fuzz 400 / pipeline 400 / XML 600 mutations
with 0 findings, 1,800 media fuzz cases clean, deep 882 runs over 315 pages at
depth 10,000 (0 failed, slowest 3.6 s), and `make tsan` under both sanitizers.

### 5. What is still open

- A high half that is already in a block still copies when the low half
  arrives: a piece taken with `substring` that ends in one, or a literal, is
  settled text, and the four bytes of the pair start inside it. A join owes the
  half instead, which is why `s += String.fromCharCode(0xD83D)` in a loop is
  linear all the same: the half is the last thing in the text joined, so the
  join owes it rather than writing it.
- Reading a handle that owes a half writes the half into its block, so
  completing the pair afterwards copies: `s += '\uD83D'; n += s.indexOf('zz');
  s += '\uDE00'` in a loop is still quadratic (58/242/893 ms at
  5,000/10,000/20,000). Making that linear needs text that can be read without
  being flattened — a rope, or UTF-16 storage — which is a change of
  representation, not of this path. It is a row of the task table.
- The handle is a word wider. Packing the owed half into the spare high bits of
  the four length and offset fields would keep it at 24 bytes, at the cost of
  masking every read of them; that was not done, and the measured cost of not
  doing it is in section 3.

---

## Phase 43 Architecture: `new.target`, and What a Constructor Makes

### 1. What was wrong

`new.target` is the function `new` was applied to. In a constructor that a
subclass reached with `super(...)` it is the subclass; with
`Reflect.construct(F, args, G)` it is `G` inside `F`. On `main` it was the
function that was running. `invoke_function` bound it on entry, to the callee
whenever the call was a construction, and nothing could have told it more:
`JsValue::construct(args, this)` took the arguments and the object, and
`super(...)` and `Reflect.construct` both ended there.

Three more things about what a constructor makes were wrong beside it, each
of them a few lines of `eval_super_call` or `js_construct`:

- `super(...)` ran the parent's constructor on the object `new` had made and
  dropped what came back. A parent that returns an object of its own (a
  function that returns `Object.create(new.target.prototype)`, a class that
  hands out one instance, a compiled class) was lost: the child went on with
  the other object, and that one was what `new` gave. The value of
  `super(...)` itself was `undefined`.
- `this` could be bound twice, or never: a second `super(...)`, a derived
  constructor that returns without one, and one that returns a number were
  all accepted.
- `Reflect.construct(Native, args, NewTarget)` ran the built-in with no
  object and gave what it built the prototype of `NewTarget` afterwards.
  That is right for an array or an error. A DOM interface builds on the
  object `super()` hands it and refuses to run without one, so
  `Reflect.construct(HTMLElement, [], Derived)` was `Illegal constructor`.

| A script | `main` | Now, and in Node |
| :--- | :--- | :--- |
| `class Shape { constructor() { if (new.target === Shape) throw new TypeError('abstract'); } }`, `class Circle extends Shape {}`: `new Circle()` | `TypeError: abstract` | a `Circle` |
| `new.target.name` in `A`, and in `B extends A` after its `super()`: `new C()` for `class C extends B {}`, then `new B()`, then `new A()` | `A,B,A,B,A` | `C,C,B,B,A` |
| `function F() { this.nt = new.target; }`: `Reflect.construct(F, [], G).nt === G` | `false` | `true` |
| `class A { constructor() { this.f = () => new.target; } }`, `class B extends A {}`: `new B().f() === B` | `false` | `true` |
| `class MyErr extends Error { constructor(m) { super(m); this.who = new.target.name; } }`, `class Sub extends MyErr {}`: `new Sub('m').who` | `MyErr` | `Sub` |
| `function Parent() { return custom; }`, `class Child extends Parent { constructor() { super(); this.added = 2; } }`: `new Child() === custom`; `custom.added` | `false`; `undefined` | `true`; `2` |
| `class Child extends Parent { f = 7; }` over the same parent: `custom.f` | `undefined` | `7` |
| `var r = super()`: `r === this` | `false` | `true` |
| `constructor() { super(); super(); }` | accepted | `ReferenceError`, after the parent has run a second time |
| `class B extends A { constructor() {} }`: `new B()` | an object | `ReferenceError` |
| `constructor() { super(); return 1; }`, and the same with `null` | an object | `TypeError` |
| `Reflect.construct(F.bind(null), [], G)`: `new.target` in `F` | `F` | `G` |
| The same when `F` returns an object of its own: that object's prototype afterwards | `G.prototype` | what it was |
| `Reflect.construct(Function, ['return 3'], G)`: the prototype of the function made | `Function.prototype` | `G.prototype` |
| `Reflect.construct(F)`, with no list of arguments | accepted | `TypeError` |
| `function Old() { var self = Reflect.construct(HTMLElement, [], new.target); self.made = true; return self; }`, given a prototype and to `customElements.define`: `new Old()`, `document.createElement('x-old')` | `TypeError: Illegal constructor`; an element that is not an `Old` | an `Old`, both (the last two rows: as in jsdom) |
| An `<x-old>` that stands in the page when that `define` runs: `el.made` | `undefined`, after an uncaught `Illegal constructor` | `true` |

Who met it. The abstract class of the first row is the usual way to write
one. An error class that calls itself after `new.target` (`this.name =
new.target.name`) named every subclass after the parent. A parent that
returns an object is how a singleton and a factory are written, and it is
what every class compiled for older browsers does: Babel turns `super(...)`
into `Reflect.construct(Super, arguments, NewTarget)` and goes on with what
that returns, so a modern class that extends a compiled one lost its
instance. And a custom element compiled that way could neither be made nor
take over an element of the page. Phase 34, which made compiled classes run
as far as their helpers go, listed both as open (its section 9), with a
failing case and five lines of
`tests/dom-conformance/open/compiled-custom-elements.js`.

### 2. `new.target` goes in with the call

A call in this engine has a context beside its arguments (`CallContext` in
`src/js/JsValue.cpp`, one per thread): who `this` is, whether it is a
construction, which function is called. It has one field more now,
`new_target`, and null there means the function that is called, which is
every construction but two kinds:

```cpp
JsValue construct(const std::vector<JsValue>& args, const JsValue* this_val = nullptr,
                  const JsValue* new_target = nullptr) const;
JsValue construct_as_super(const std::vector<JsValue>& args, const JsValue& derived_this,
                           const JsValue* new_target = nullptr, JsValue* in_place = nullptr) const;
```

`super(...)` passes on the `new.target` of the constructor it stands in, and
`Reflect.construct` passes its third argument. A script function reads it
with the rest of the context (`take_call_context`), a native function with
`JsValue::current_new_target()`, which answers null unless another function
than the native itself was named.

Inside a script function `new.target` is a binding of the function's scope,
`%new.target`, as before: made when the function is a class constructor or
its body, or an arrow function in it, says `new.target` (the parser marks the
nearest function that is not an arrow), so an arrow function finds it along
the scope chain. What changed is the value it is given on entry: undefined
in a call, the named function when there is one, the callee otherwise.
`eval_super_call` reads the binding where `super(...)` stands and hands a
copy up.

**The way in is a slot, for now.** The callable that `make_function` gives a
script function reads the context and calls `invoke_function`, and that is
where the value should be an argument. `invoke_function` is declared in
`include/litebird/js/JsInterpreter.hpp`, a file another open pull request
was changing while this was written, so its signature was left alone: the
value goes from the one function to the other through a thread-local pointer
(`NewTargetForEntry` in `src/js/JsEval.cpp`), set right before the call and
taken as the first thing the callee does, with nothing between the two but
the call. `invoke_function` has no other caller. The slot should become an
argument when that header is free.

The bytecode VM is not involved: the compiler leaves a function that says
`new.target` or `super` to the interpreter.

### 3. `super(...)` binds `this`, once

`eval_super_call` constructs the parent with the object `new` made and the
`new.target` of this constructor. Then:

- **What the parent gave back is `this`.** A script parent, or a bound
  function, that returns an object other than the one it was given: that
  object is `this` from here on, as it is, with the prototype it has. The
  binding named `this` that the `super(...)` expression sees is given the
  new value, which is the constructor's also when the expression stands in
  an arrow function. The class's fields are then defined on that object, and
  the value of the expression is `this`.
- **A built-in parent** builds its own object and `this` takes over its
  state, as since Phase 34; a DOM interface is handed `this` and makes it
  the thing it constructs. Both are told `new.target` now. (An interface can
  come back with another object than the one it was handed; that is
  section 5.)
- **What the constructor returns.** `invoke_function` returns the
  constructor's `this` binding when a derived constructor's body returned
  nothing. Whoever called `construct()` (`new`, `Reflect.construct`, a
  subclass's `super(...)`) already preferred an object that comes back to
  the one it had passed in, so the object a parent three classes up returned
  travels down through each `super(...)` to the `new` expression.

`this` is bound when the parent has returned, and only once. The scope of a
derived constructor has a flag for it, a binding named `%this.bound` (not a
name a script can write, and a scope is not an object a script can reach).
`eval_super_call` sets it after the parent has run and throws a
`ReferenceError` if it was set already: the language runs the parent the
second time and refuses the result, and a script can count the runs. The
second run is given a spare object, so that what the first one made is left
as it was (`class M extends Map { constructor() { super([[1, 2]]); try {
super(); } catch (e) {} } }` still has its entry). `invoke_function` looks at
the flag when the body returned nothing, and throws a `ReferenceError` if
`super(...)` never finished; a body that returned something that is neither
an object nor undefined is a `TypeError`. A class that extends null gets
both without a line of its own: its default constructor calls a parent that
is not a constructor, and one written out either returns an object or never
binds `this`.

The flag is a binding and not a field of the scope for the reason the slot
of section 2 exists: `JsEnvironment` is declared in the same header.

What this does not do: reading `this` before `super(...)` has run is still
not an error (section 8).

### 4. `Reflect.construct`, and `new` on a bound function

`js_construct(ctor, args, new_target)` is behind `new`, `Reflect.construct`
and a bound function. Until now its third argument decided one thing, whose
`prototype` the object gets. Now:

| `ctor` is | What happens |
| :--- | :--- |
| a script function or class | The object is made with `new_target.prototype`, read once before the function runs, and the function is constructed on it with that `new.target`. What it returns, if an object, is the result |
| a built-in, and no other `new_target` | As before: it builds its own object |
| a bound function | It is constructed with the `new_target`; its native reads `current_new_target()` and calls `js_construct(target, bound and given arguments, that)`. What comes back is the result as it stands, with no prototype given to it: the target made the object, or returned one of its own. The function carries a mark for this (`Extra::forwards_construct`, set by `bind`), which `super(...)` in a class that extends a bound function reads too |
| any other built-in, with a `new_target` | `JsValue::construct_for` runs it. If it asks for the object it is to build on (`current_derived_this()`, which only the DOM's interface constructors do), one is made then, with the prototype `new_target` has at that moment, and what the built-in makes of it is the result. If it never asks, it builds its own and that is given the prototype afterwards |

The order in the last row is the language's: a built-in looks at its
arguments first and at `NewTarget.prototype` when it makes its object.
`Reflect.construct(Promise, [], NewTarget)` has to complain that there is no
executor without having read the property, and one test262 test asks exactly
that with a getter that throws. (The first version of this change made the
object before running the built-in, and that test, which `main` passes, was
how it was found.) On every path the property is read once; where it is not
an object, a script function's object gets `Object.prototype` and a
built-in's keeps the prototype the built-in gave it.

`Reflect.construct` itself now refuses a list of arguments that is not an
object, after it has found both functions to be constructors.

### 5. The DOM: a custom element compiled for older browsers

No file of the DOM bindings changed (they belonged to another open pull
request). Two things in the engine make such an element work.

**Made new.** The constructor of a DOM interface already asked for the
object a subclass's `super()` hands it and made the element, or the event,
on that object. `Reflect.construct(HTMLElement, [], Old)` now offers it one
the same way (section 4), with `Old.prototype`, and
`construct_custom_element` finds the definition by that prototype as it does
for `new MyElement()`:

```js
function Old() { var self = Reflect.construct(HTMLElement, [], new.target); self.made = true; return self; }
Old.prototype = Object.create(HTMLElement.prototype, { constructor: { value: Old, writable: true, configurable: true } });
Object.setPrototypeOf(Old, HTMLElement);
customElements.define('x-old', Old);
new Old().localName                                 // "x-old"; on main: TypeError: Illegal constructor
document.createElement('x-old') instanceof Old      // true; on main: false, after an uncaught "Illegal constructor"
```

**Upgraded.** Most custom elements are not made by a script. They stand in
the page's markup, come out of `innerHTML`, are cloned, or exist before
`customElements.define` runs, and each of those is an element that exists
and is *upgraded*: `run_upgrade` runs the definition's constructor with the
element's own object as `this`. A class written as a class reaches
`HTMLElement` through `super()` with that object, and the interface sees
that it is an element already. A constructor compiled for older browsers
does not pass `this` on. It calls `Reflect.construct(HTMLElement, [],
new.target)` and goes on with what comes back; or, when its parent is a
class (a library ships classes, the application is compiled), it calls
`Reflect.construct(Parent, arguments, NewTarget)`, the class is constructed
on a new object, and it is the class's `super()` that reaches `HTMLElement`,
with that new object. Either way the only thing that ties the call to the
upgrade it is part of is `new.target`.

So `JsValue::construct()` notes a construction whose `this` is a platform
object already, a *construction in place*, for as long as it runs, under the
function `new` is applied to (a thread-local list in `src/js/JsValue.cpp`;
`super(...)` constructing a script parent on the same object adds nothing to
it). When the constructor of an interface asks for the object it is to build
on (`current_derived_this()`), the list is looked at first. If the call's
`new.target` is being constructed in place, on an element of the interface
that asks, the answer is that element, whatever the call came with, and it
is given once: a second request while the same construction runs is a
`TypeError`. `construct_as_super` tells `eval_super_call` when the interface
built on another object than the one `super(...)` offered, and that object
becomes `this`, the way a script parent's returned object does (section 3).

Only the element's own interface is given it: `HTMLElement` for an element
with a name of its own, the interface of its tag for a customized built-in
(`is_interface_of()`, which asks the element's realm for that constructor).
Any other interface builds on an object of its own, also when `super(...)`
came with the element. So `Reflect.construct(Event, [], new.target)` in the
constructor of a custom element makes an event, and a class that extends
`Event`, or `HTMLElement` where its definition is for a `button`, and is
given to `customElements.define` all the same, no longer has an element
turned into something else under it: its upgrade fails, as the standard has
it.

This is the HTML standard's *construction stack*, which each definition has
there: the element being upgraded is pushed on it, the `HTMLElement`
constructor finds the definition by `new.target`, returns the element on top
and leaves a marker in its place. The consequences are the standard's too,
and jsdom's (all of these are cases of `compiled.dat`):

| | What happens |
| :--- | :--- |
| An element of the page, one from `innerHTML`, a clone, one made before `define` or in a tree given to `customElements.upgrade` | Upgraded by the compiled constructor, and then told its observed attributes and that it is connected, in that order |
| What Babel emits (`_wrapNativeSuper`, `_createSuper`), two classes deep | The same: each level constructs the next with the same `new.target` |
| A subclass compiled for ES5 of a class that extends `HTMLElement` | The compiled constructor constructs the class on a new object; the class's `super()` is given the element, which is `this` in the class from there on, with the class's fields on it |
| A class on top of a compiled constructor | `super()` runs the function on the element with the class as `new.target`; the function gets the element back, and the class's fields go on it |
| A customized built-in (`Reflect.construct(HTMLButtonElement, [], new.target)`, `<button is="fancy-b">`) | The same. One that asks `HTMLElement` instead is refused |
| The constructor asks twice during an upgrade | The second request is a `TypeError`. Under `new`, where nothing is being upgraded, each request makes an element |
| A second instance of the class made while it is being upgraded | Before the constructor reached its interface: the inner instance is the element being upgraded, and the constructor's own request is the one refused. After: the inner one is refused. For a class and for a compiled constructor alike |
| The constructor throws after it got its element | The upgrade fails; later constructions get elements of their own |

**What is left** is in how `run_upgrade` calls the constructor, which is in
`src/js/DomObserverBindings.cpp`. The standard constructs it the ordinary
way: `this` is a new object, the element gets the class's prototype when
`HTMLElement` hands it out, and the upgrade fails unless the constructor
returns the element. Here the element is given the prototype first and is
`this`. A class cannot tell, and neither can a compiled constructor that
does what it is for. Three things can
(`tests/dom-conformance/open/compiled-custom-elements.js`, 5 lines, 4
differ, all four the same on `main`): in a constructor written as a function
`this === self` is `true`; one that never reaches `HTMLElement` (`function
Plain() { this.touched = true; }`) has written on the element and its
upgrade counts as done, where the standard refuses it; and after a
constructor that throws before it reaches `HTMLElement` the element keeps
the class's prototype. The fourth line is the selector's: a customized
built-in whose upgrade failed still matches `:defined`. A row of the task
table.

### 6. How this is verified

@@VERIFY_TABLE@@

**What test262 found.** @@T262_TEXT@@

**What jsdom is not right about here.** Two answers of jsdom 31 differ from
Web IDL, and the unit suite asserts the standard's instead (both are rows of
the table in `tests/dom-conformance/README.md` now): for
`Reflect.construct(Event, ['z'], NewTarget)` jsdom reads
`NewTarget.prototype` twice, and when that is not an object it makes
something that is not an event (`instanceof Event` is `false` and `type` is
undefined), where the standard falls back to `Event.prototype`.

### 7. What it costs

@@COST_TEXT@@

### 8. What is still open here

Found on the way, the same on `main`, and in the task table now, as rows of
their own or added to the row they belong to. The first two have a failing
case each in `construct.dat`.

- **`this` can be used before `super(...)` has run.** In a derived
  constructor `this.x = 1; super();` writes to the object `new` made, where
  the language has a `ReferenceError`; an arrow function that reads `this`
  and `super.m` behave the same. The engine has no binding that exists and
  cannot be read yet: `let` and `const` have none either (`typeof z` before
  `let z` is `undefined` here and a `ReferenceError` in Node, and a `let` in
  a block does not hide an outer variable above its own line).
- **`eval` inside a function is global code.** It sees no local variable, no
  parameter, no `this`, no `new.target` and no `super`: `function f() { var
  local = 5; return eval('local + 1'); }` is a `ReferenceError`, and
  `eval('super()')` in a constructor is a `SyntaxError`.
- **A private name is a property whose name begins with `#`.** `o.#p` for an
  object that has no such field is `undefined`, and assigning to it adds
  one, where both are a `TypeError`; `obj['#p']` reads and writes the
  private field from outside the class; `#p in o` is a syntax error. (Met
  while writing the case for fields on a returned object.)
- **`new Map(iterable)` and `new Set(iterable)` do not call the `set` and
  `add` of the object they fill**, so a subclass that overrides one is not
  asked. (Added to the row on built-ins and subclasses.)
- **An upgrade runs the constructor on the element itself** (section 5):
  `this` in a constructor written as a function is the element, one that
  never reaches `HTMLElement` is accepted, and the element has the class's
  prototype before the constructor has run.
- **A script's own property whose name begins with `#`, `%` or `@@`, or has
  the shape `__x__`, is left out of `Object.keys`, `JSON.stringify`,
  `for…in`, `Object.assign` and spread**, because that is how the engine
  hides its own slots and private names: `var m = {}; m['#/route'] =
  'home'; JSON.stringify(m)` is `{}`. (Met while looking at how a private
  name is stored; added to the row on the engine's slots.)
- **The slot of section 2 and the flag of section 3** should be an argument
  of `invoke_function` and a field of the scope, once
  `include/litebird/js/JsInterpreter.hpp` can be changed.

What Phase 34 listed and this did not touch: a built-in does not make the
subclass it was called on and there is no `Symbol.species`, `instanceof`
does not ask `Symbol.hasInstance`, nothing tells a built-in that is a
constructor from one that is not (so `class X extends f.bind(null)` is
accepted, which is why a class can extend a bound function here at all), a
function's own property names, and a bound function that is never collected.
Eight cases of `prototypes.dat` fail on those.

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
   multicol, bidi, complex font shaping/HarfBuzz, the table's outer border merge in
   the collapsed model, `empty-cells`, `caption-side`, `table-layout: fixed`,
   per-element scrollable `overflow: auto` containers (the viewport scrolls, inner
   boxes clip), and rounded-corner clipping. The DOM is trustworthy; the box tree
   handles everyday layouts but not full CSS2.2/CSS3 edge cases.
2. **JavaScript is interpreted, and one regular-expression limit remains.**
   Scripts run on a tree-walking interpreter: 50 to 100 times slower than V8
   on loops and calls. Long strings are no longer quadratic (Phase 24), and
   neither are deleting many keys from one object or emptying a long array
   with `shift()` (Phase 30). In regular expressions, a group
   repeated more than about 12,000 times (`(?:a|b)*` over a long string) ends
   in a `RangeError` where V8 answers, because the matcher recurses; a single
   character or class under `*` has no such limit (AUDIT.md, finding 26).
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
   dimensions, margins and padding, but percentage `font-size`, `line-height`
   and `border-radius` are still not resolved anywhere.
6. **11 corpus cases still fail, all of them about `<select>`.**
   `webkit02.dat` 43/49 (six: `<selectedcontent>` cloning and the other
   rules of the 2025 customizable-`<select>` work), `tests1.dat` 110/112,
   `tests_innerHTML_1.dat` 79/81 and `tests7.dat` 33/34. The standard
   changed how the content of a `<select>` is parsed and Litebird has the
   older rules. Everything else in the corpus passes. The corpus is not
   the last word: the vendored copy is the last one html5lib-tests
   published before its tree-construction tests moved to
   web-platform-tests (June 2026). That copy has 1,973 cases now and
   Litebird passes 1,868 of them: 88 of the others need processing
   instructions, which the standard's parser now makes and Litebird's does
   not (the node type exists since Phase 28), and 6
   need a script to run while the page is parsed. And a corpus does not
   hold everything: `<br/>` closing its parent had no case in it (Phase 26).
   `make diff-trees` compares random pages with parse5 for that reason.
7. **Conformance is not rendering.** 99.4% of the parser corpus is a DOM
   guarantee, not a promise that real pages look right.
8. **TrueType `.ttf` rendering has no `@font-face` or HarfBuzz shaping.** When a
   system `.ttf` font is present, `FontRenderer` rasterizes anti-aliased UTF-8
   glyphs at the computed `font-size`; when no `.ttf` file is available on the
   host, rendering falls back to the unscaled 8×8 bitmap font. Webfont
   downloads (`@font-face`, WOFF/WOFF2), kerning/ligatures (`GPOS`/`GSUB`), and
   right-to-left bidi shaping are not implemented.
9. **The DOM a script sees is measured against jsdom and a part of
   web-platform-tests, plus targeted end-to-end integration tests.**
   It passes all 663 cases of its corpus (Phases 27, 28, 34 and 43) and 4,658 of
   the 5,192 subtests of `dom/nodes` and `domparsing`; the corpus can
   only ask what jsdom can answer and what somebody thought to ask, and the
   rest of web-platform-tests has not been run. Phase 36 added on-demand layout
   (`getBoundingClientRect()`, `offset*`, `client*`), external classic/deferred
   `<script src>` and `<link rel="stylesheet">` + `@import`, CLI/shell XML & SVG
   document loading, and HTTP/HTTPS + CORS `fetch()` and `XMLHttpRequest`. Still
   missing: `document.write`, style sheet objects (`sheet.cssRules`), frames
   with documents (`iframe.contentDocument`), CORS preflight `OPTIONS`, four text
   encodings only, date and time inputs without their values, `el.style`
   shorthands kept whole. Absent: `TextEncoder`, `crypto`, `indexedDB`,
   `MessageChannel`, XPath, Web Animations, `ElementInternals`, and what needs
   typed arrays (`ArrayBuffer`). The full lists are in Phase 27, section 12,
   Phase 28, section 10, and Phase 36, section 9.
10. **JavaScript features that do not exist:** BigInt, `Proxy`, typed arrays,
   modules, `with`, `Intl`, holes in arrays. Details and the approximate
   corners are in Phase 23, section 10. Around classes: there is
   no `Symbol.species` and `instanceof` does not ask `Symbol.hasInstance`,
   nothing tells a built-in that is a constructor from one that is not
   (Phase 34, section 9), `this` can be used before `super()` has run, `let`
   and `const` have no dead zone, `eval` inside a function is global code,
   and a private name is a property that `obj['#p']` reads (Phase 43,
   section 8). Of @@T262_DEF_TOTAL@@ test262 tests on functions, classes and
   objects, @@T262_DEF_NOW@@ pass (`make test262`); of the @@T262_ALL_TOTAL@@ in all of its
   `built-ins`, `language` and `annexB`, @@T262_ALL_NOW@@.
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
