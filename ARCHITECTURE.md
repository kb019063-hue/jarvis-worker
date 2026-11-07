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

- `make asan` builds with AddressSanitizer + UndefinedBehaviorSanitizer, runs
  the 58 suites, then fuzzes the parser.
- `tools/fuzz/fuzz_parser.py` mutates the conformance corpus (byte flips,
  token splicing, truncation, duplication, doubling) and feeds each mutant to
  the sanitizer build. It found a real dangling-index bug in the adoption
  agency during this phase; that is now fixed.
- LeakSanitizer is deliberately disabled: the JS engine has pre-existing leaks
  that deserve their own pass (see "Known gaps").

---

## Known gaps (the honest list)

1. **Layout is behind parsing.** No `float`, no table layout (`display: table`),
   no multicol, no bidi, no font shaping/HarfBuzz. The DOM is now trustworthy;
   the box tree is not yet.
2. **No JIT.** The JS engine is a bytecode stack VM. Expect 10-100x slower than
   V8 on script-heavy pages.
3. **Single process, no sandbox.** Chrome's original headline feature was
   process-per-tab isolation. Untrusted input is parsed in-process.
4. **Leaks.** The JS engine leaks in the ASan run; a leak audit is outstanding.
5. **Percentages** are passed to layout rather than resolved in the cascade.
6. **Script data escaped states** (`<!--` inside `<script>`) are not implemented;
   those cases fail in `scriptdata01.dat`.
7. **Conformance is not rendering.** 87.8% of the parser corpus is a DOM
   guarantee, not a promise that real pages look right.

---
