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
  - Zero-dependency POSIX socket HTTP/1.1 client.
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

- `make asan` builds the CLI and the tests with AddressSanitizer +
  UndefinedBehaviorSanitizer, runs the full engine suite, then fuzzes the parser
  (the sanitizer build has to produce `build-asan/litebird` first, or the fuzzer
  has nothing to run).
- `tools/fuzz/fuzz_parser.py` mutates the conformance corpus (byte flips,
  token splicing, truncation, duplication, doubling) and feeds each mutant to
  the sanitizer build. It found a real dangling-index bug in the adoption
  agency during this phase; that is now fixed.
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

## Known gaps (the honest list)

1. **Layout is still behind parsing.** Floats, block formatting contexts and
   basic table layout landed in Phase 16; Phase 18 added percentage lengths and
   real `overflow` clipping. Still missing: multicol, bidi, font
   shaping/HarfBuzz, `rowspan`/`colspan`, `border-collapse`, scrollable
   `overflow: auto` (it clips but cannot be scrolled), and rounded-corner
   clipping. The DOM is trustworthy; the box tree is plausible.
2. **No JIT.** The JS engine is a bytecode stack VM. Expect 10-100x slower than
   V8 on script-heavy pages.
3. **Single process, no sandbox.** Chrome's original headline feature was
   process-per-tab isolation. Untrusted input is parsed in-process.
4. **Leaks.** The JS engine leaks in the ASan run; a leak audit is outstanding.
5. **Percentage lengths are resolved in layout, not the cascade** (the cascade
   has no containing block to resolve against) -- that is now correct for box
   dimensions, margins and padding, but percentage `font-size`, `line-height`
   and `border-radius` are still not resolved anywhere.
6. **22 corpus cases still fail**, and they are known rather than mysterious.
   All of them are in 11 files, and the breakdown is:
   `webkit02.dat` 43/49 (six, all `<selectedcontent>` cloning from the 2025
   customizable-`<select>` work, which is not implemented),
   `tests1.dat` 108/112, `tests_innerHTML_1.dat` 78/81, `tests2.dat` 61/63,
   and one each in `adoption01.dat`, `adoption02.dat`, `tests4.dat`,
   `tests7.dat`, `tests20.dat`, `tests23.dat` and `entities01.dat` —
   adoption-agency corner cases, `innerHTML` fragment oddities and
   character-reference edges. Everything else in the corpus, including
   `tests16.dat` (197/197), `scriptdata01.dat` (26/26), `svg.dat` (8/8) and
   `math.dat` (8/8), now passes.
7. **Conformance is not rendering.** 98.8% of the parser corpus is a DOM
   guarantee, not a promise that real pages look right.
8. **The bitmap font is 8x8 and unscaled per glyph.** Text advance now matches
   layout, but glyphs are drawn at 8px regardless of `font-size`, so large text
   is under-sized rather than blurry.

---
