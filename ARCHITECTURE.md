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
| **Phase 27** | **The DOM a script sees, rebuilt against an outside scoreboard:** a 557-case corpus with expected output recorded from jsdom (`make dom-conformance`, enforced in CI). A DOM node was a special kind of value whose members were a list of names inside `JsValue::get_property`; it is now an object with its interface's prototype, one per node. About 170 interfaces, a selector engine, the standard's serializer, form state and validation, events with default actions, mutation observers, custom elements, `location`/`history`, and the URL Standard's parser (web-platform-tests data, `make url-conformance`). A page's scripts run while it is parsed. | ✅ Completed (DOM corpus 61 → 557 of 557; URL data 2 → 1150 of 1188; 108/108 unit suites) |

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
  - `FetchAPI::fetch(input, init)` creates an unfulfilled Promise, decodes `RequestInit` options (`method`, `headers`, `body`), and dispatches network/data loading through `ResourceLoader`.
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
  - Script loading supports both `data:text/javascript,...` inline payload data and filesystem scripts via `ResourceLoader`.
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
35 ms. That is fixed: see Phase 24 below. Two containers still have the same
shape:

| Script | Litebird | Node |
| :--- | :--- | :--- |
| `delete` of 50,000 keys from one object | over 120 s | 91 ms |
| `shift()` until a 50,000-element array is empty | 5.3 s | 0.2 s |

The property map rebuilds its index on every `delete`, and arrays are plain
vectors. Small objects and short arrays, which is most of what pages handle,
are not affected.

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
- **`delete` and `shift()`** are quadratic in the size of the object or array
  (Phase 23, section 9). They were found by the same timing run and are not
  string problems.
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
  (`<a href="#">x<p>y</p></a>` paints only "x"). This was so before; the
  nesting limit makes it easier to meet.
- **Many floats with a size in one box** cost far more than their number:
  10,000 take 46 s. Past 512 steps down, a float is placed where it stands:
  1,100 floats of 100 by 10 pixels fill 128 rows correctly, and the rest go
  into one row that runs out of the box to the right. Float placement needs
  to be redone, not patched.
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
                                     over that tree. 12,400 lines, 14 files.
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
| `DomStyleBindings.cpp` | `CSSStyleDeclaration`: `el.style` as a live declaration block, and `getComputedStyle` |
| `DomDocumentBindings.cpp` | `Document`, `DOMImplementation`, `DOMParser`, `XMLSerializer`, cookies, named access |
| `DomTraversalBindings.cpp` | `Range`, `Selection`, `TreeWalker`, `NodeIterator`, `NodeFilter` |
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

### 8. What it costs

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

### 9. How this is verified

**The scoreboard.** `tests/dom-conformance/` is 20 files, 557 cases. A case
is a page (`#html`), a script (`#code`) and what the script prints
(`#expect`). `tools/conformance/run_dom_conformance.py` loads the page with
`litebird --run-script`, which runs the script after the page's own, fires
the load events and runs timers to the end on the event loop's virtual
clock, without layout or painting. Every `#expect` was written by
`--record`, which runs the case in jsdom 31.0.0 (`jsdom_run.mjs`), and
`--reference` runs them all in jsdom again: CI does, so an expectation
cannot be bent. `everyday.dat` was written first, from *You Might Not Need
jQuery*, and is not edited.

jsdom is the reference, not the authority. In five places it differs from
the standards (the corpus README names them); those are not in the corpus
and `dom_follows_the_standard_where_jsdom_does_not` asserts the standard's
answer.

| Check | Result |
| :--- | :--- |
| DOM corpus (`make dom-conformance`) | 557 / 557; 61 on `main`. The everyday probe 68 / 68; 13 on `main` |
| The corpus in jsdom (`--reference`) | 557 / 557 |
| URL data of web-platform-tests (`make url-conformance`): 904 parsing cases, 284 setter cases | 873 and 277, 1150 of 1188; 0 and 2 on `main`, where a URL object had no `username`. The 38 left are host names outside ASCII |
| Unit tests (`make test`) | 108 / 108; five are new (the URL parser, URLs in the DOM, page scripts, the five points where jsdom is not the standard, lookups by id), and five old ones asserted what no browser does and were corrected |
| html5lib tree construction (`make conformance`) | 1781 / 1792, as before |
| JavaScript corpus (`make js-conformance`) | 840 / 845, as before |
| `make diff-trees` | 4 × 1,500 random pages against parse5 8.1.1, 0 differ, as before |
| `make fuzz` | @@FUZZ@@ |
| `make deep` | 1,292 runs over 445 pages at 200,000 levels, 0 failed. The slowest page takes as long as on `main` |
| `make asan` (ASan + UBSan): unit tests, the three corpora, both fuzzers, the deep pages | @@ASAN@@ |

The DOM corpus has a job of its own in CI (the reference check, the
scoreboard with a floor of 557, the URL data with a floor of 1150), and
`make asan` runs both corpora in the sanitizer build.

### 10. What is still open here

- **The corpus measures what jsdom does.** jsdom lays nothing out and loads
  no second page, so nothing checks geometry, scrolling or navigation. In
  the CLI the layout is made once, after the scripts, so
  `getBoundingClientRect()` and `offsetWidth` are zero while they run.
  `getComputedStyle` knows what the cascade knows, not used values.
- **Nothing is fetched.** A `<script src>` or a stylesheet link is not
  loaded (a script inserted with a `src` gets an `error` event).
  `document.write` does nothing.
- **Host names outside ASCII** are kept as written: `http://faß.example`
  should become `xn--fa-hia.example` (Unicode's IDNA tables, the 38 URL
  cases).
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

## Known gaps (the honest list)

1. **Layout is still behind parsing.** Floats, block formatting contexts and
   basic table layout landed in Phase 16; Phase 18 added percentage lengths and
   real `overflow` clipping. Still missing: multicol, bidi, font
   shaping/HarfBuzz, `rowspan`/`colspan`, `border-collapse`, scrollable
   `overflow: auto` (it clips but cannot be scrolled), and rounded-corner
   clipping. A block inside an inline box (`<a>x<p>y</p></a>`) is not laid
   out at all: the "y" is missing. The DOM is trustworthy; the box tree is
   plausible.
2. **JavaScript is interpreted, and two containers are quadratic.** Scripts
   run on a tree-walking interpreter: 50 to 100 times slower than V8 on loops
   and calls. Long strings are no longer part of this (Phase 24), but deleting
   many keys from one object and emptying a long array with `shift()` still
   cost the square of the size: 50,000 deletes take over two minutes.
   Measurements are in Phase 23, section 9. In regular expressions, a group
   repeated more than about 12,000 times (`(?:a|b)*` over a long string) ends
   in a `RangeError` where V8 answers, because the matcher recurses; a single
   character or class under `*` has no such limit (AUDIT.md, finding 26).
3. **Single process, no sandbox.** Chrome's original headline feature was
   process-per-tab isolation. Untrusted input is parsed in-process.
4. **Memory is only reclaimed on request.** The collector is sound (Phase 23,
   section 5) but runs only when a script calls `gc()` or the shell's heap
   view is opened; a long-running page grows until then. Leak detection is
   switched off in `make asan` and no leak audit has been done since the
   engine was rebuilt.
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
   instructions, which the standard has now and Litebird does not, and 6
   need a script to run while the page is parsed. And a corpus does not
   hold everything: `<br/>` closing its parent had no case in it (Phase 26).
   `make diff-trees` compares random pages with parse5 for that reason.
7. **Conformance is not rendering.** 99.4% of the parser corpus is a DOM
   guarantee, not a promise that real pages look right.
8. **The bitmap font is 8x8 and unscaled per glyph.** Text advance now matches
   layout, but glyphs are drawn at 8px regardless of `font-size`, so large text
   is under-sized rather than blurry.
9. **The DOM a script sees is measured against jsdom, not against pages.**
   It passes all 557 cases of its corpus (Phase 27), and the corpus can
   only ask what jsdom can answer. No layout while scripts run
   (`getBoundingClientRect()` is zero), no navigation to another page, no
   external scripts or stylesheets, no `document.write`, no style sheet
   objects (`sheet.cssRules`, `insertRule`). Absent: `TextEncoder`,
   `crypto`, `indexedDB`, `MessageChannel`, XPath, Web Animations,
   `ElementInternals`, and what needs typed arrays. A host name outside
   ASCII is not converted (38 of the 1,188 URL cases). The full list is in
   Phase 27, section 10.
10. **JavaScript features that do not exist:** BigInt, `Proxy`, typed arrays,
   modules, `with`, `Intl`, holes in arrays. Details and the approximate
   corners are in Phase 23, section 10.
11. **A page can still be slow by being long.** Depth no longer ends the
   process (Phase 25): the parser stops nesting at 512, nothing that walks
   the DOM recurses, and layout stops when the stack is nearly used up, which
   means a tree a script built deeper than that is laid out only so far.
   Length is bounded for what `make deep` generates, not in general. 10,000
   floats with a size in one box take 46 s, and past 128 rows of them the
   rest are placed in one row that runs out of the box; removing the first
   child of 50,000, one at a time, takes a second; 300,000 spaces inside
   `:nth-child( )` take 9 s. At the nesting limit, misnested formatting tags
   can leave content out of its written order. Measurements are in AUDIT.md,
   finding 28.

---
