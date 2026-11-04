# 🦅 Litebird Browser Engine

> A lightweight, modular browser and JavaScript engine built from scratch in **C++20**.
> Designed for learning, experimentation, and building custom web engines without massive legacy dependencies.

---

## 🌟 Features & Architecture

Litebird implements the complete modern browser rendering pipeline:

```text
[Raw HTML] ───► HTML Tokenizer ───► HTML Parser ───► DOM Tree
                                                         │
[Raw CSS]  ───► CSS Tokenizer  ───► CSS Parser  ───► Style Rules
                                                         │
                                                         ▼
                                                 Style Resolver (Cascade)
                                                         │
                                                         ▼
                                                 Layout Box Tree
                                                 (Block / Flexbox / Inline / Text)
                                                         │
                                                         ▼
                                                 Display List
                                                 (DrawRect, DrawBorder, DrawText)
                                                         │
                                        ┌────────────────┴────────────────┐
                                        ▼                                 ▼
                                 Terminal Renderer                 Bitmap Renderer
                              (24-bit ANSI Truecolor)             (24-bit .BMP Image)

                                                 ▲
                                                 │
[JavaScript] ───► Lexer ───► AST Parser ───► Bytecode Compiler ───► Stack VM
                                                                         │
                                                 └──► DOM Bindings (getElementById, textContent)
```

### 1. DOM Core (`include/litebird/dom/`)
- Polymorphic node hierarchy (`Node`, `Element`, `Text`, `Document`).
- Tree navigation (`parent()`, `children()`, `append_child()`, `remove_child()`).
- DOM query methods: `getElementById()`, `getElementsByTagName()`, `querySelector()`, `querySelectorAll()`.
- Interactive tree printer (`dumpTree()`).

### 2. HTML5 Parser (`include/litebird/html/`)
- Stateful streaming tokenizer (`HtmlTokenizer`).
- Supports start tags, end tags, attributes (quoted/unquoted), comments, and DOCTYPEs.
- Automatic handling of void elements (`<img />`, `<br>`, `<input>`, `<hr>`, `<meta>`, `<link>`).
- Raw text mode for `<style>` and `<script>` tags.
- Character entity decoding (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&nbsp;`).

### 3. CSS Engine & Cascade (`include/litebird/css/`)
- Selector parser supporting:
  - Universal (`*`)
  - Type / Tag selectors (`div`, `h1`, `p`)
  - Class selectors (`.card`, `.btn`, `.navbar`)
  - ID selectors (`#header`, `#main`)
  - Descendant combinators (`div .card p`)
- **Specificity Calculation:** Computes `(ID, Class, Tag)` triples for accurate cascade resolution.
- **Built-in User-Agent Stylesheet:** Default styling for headings, paragraphs, lists, and layout blocks.
- **Style Resolver:** Combines User-Agent styles, Author stylesheets, and inline `style="..."` attributes with property inheritance (`color`, `font-size`, `text-align`).

### 4. Layout Engine: Block + Flexbox + Inline Formatting Context (IFC) (`include/litebird/layout/`)
- Standard Box Model: `content`, `padding`, `border`, `margin`.
- Box types: `BlockNode`, `FlexNode`, `InlineNode`, `AnonymousBlock`, `TextNode`.
- **Inline Formatting Context (IFC 2.1):**
  - Line box construction (`LineBox`) and inline fragment generation (`InlineFragment`).
  - True continuous text flowing across mixed inline elements (`<b>`, `<strong>`, `<i>`, `<em>`, `<a>`, `<code>`, `<span>`).
  - Inline element styling: custom colors, background pills, borders, padding, and text-decoration (`underline`).
  - Word wrapping across element boundaries with exact baseline alignment.
  - Text alignment: `text-align: left`, `center`, `right`.
  - Automatic `AnonymousBlock` synthesis when block containers contain mixed block and inline children.
- **CSS Flexbox (`display: flex`):**
  - Main Axis & Cross Axis resolution (`flex-direction: row | column`).
  - Space distribution via `flex-grow`, `flex-shrink`, and `flex-basis`.
  - Main-axis alignment via `justify-content` (`flex-start`, `flex-end`, `center`, `space-between`, `space-around`, `space-evenly`).
  - Cross-axis alignment via `align-items` (`stretch`, `flex-start`, `flex-end`, `center`).
  - Item spacing via `gap`.
- Visual box tree inspector (`--dump-layout`).

### 5. Multi-Backend Rendering (`include/litebird/render/`)
- Intermediate **Display List** with atomic commands (`DrawRect`, `DrawBorder`, `DrawText`).
- **Terminal Renderer:** Renders web pages directly inside your terminal using 24-bit ANSI Truecolor and border characters.
- **Bitmap Renderer:** Exports an 800x600 uncompressed 24-bit `.bmp` image using an embedded bitmap font. Zero external GUI or graphics libraries required.

### 6. High-Speed Bytecode Virtual Machine (`include/litebird/js/`)
- **Instruction Set:** Compact 8-bit opcodes (`OpConstant`, `OpGetLocal`, `OpSetLocal`, `OpGetGlobal`, `OpSetGlobal`, `OpAdd`, `OpSubtract`, `OpCall`, `OpReturn`, `OpJumpIfFalse`, `OpLoop`, etc.).
- **Bytecode Compiler:** Converts the parsed AST into a linear instruction `Chunk` with constant pooling, local slot allocation, and jump target patching.
- **Stack-based VM:** High-speed opcode dispatch loop with call-frame stack management.
- **Disassembler:** Inspect the compiled bytecode instructions using `--dump-bytecode`.
- **Global Objects & DOM Bindings:**
  - `console.log(...)`, `console.warn(...)`, `console.error(...)`
  - `Math.floor`, `Math.ceil`, `Math.round`, `Math.abs`, `Math.PI`
  - `document.title`, `document.getElementById(...)`
  - Real-time DOM mutation: `element.textContent = "..."` updates the live document before layout and rasterization.

---

## 🚀 Getting Started

### Prerequisites

- `g++` with C++20 support (GCC 11+)
- `make`

### Building

```bash
# Build the main litebird CLI
make

# Run the full test suite
make test
```

### Running Examples

```bash
# 1. Render a Flexbox layout with responsive cards and navbar
./build/litebird examples/flexbox.html

# 2. Inspect the computed Flexbox layout tree
./build/litebird --dump-layout examples/flexbox.html

# 3. Disassemble JavaScript into bytecode instructions
./build/litebird --dump-bytecode --run-js "let x = 10; function square(n) { return n * n; } console.log(square(x));"

# 4. Render directly to a 24-bit BMP image
./build/litebird --render-bmp page.bmp examples/flexbox.html
```

---

## 🛠️ CLI Options

| Flag | Description |
| :--- | :--- |
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

All 11 engine test suites passed successfully!
```
