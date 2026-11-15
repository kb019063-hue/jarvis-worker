# Litebird engine audit

Everything below was found by reading and probing the engine, not the corpus.
The conformance scoreboard only measures the tree builder, so it says nothing
about the four things that actually matter for a browser: **memory safety,
script correctness, performance, and memory footprint.**

Commit `8b3aeff` on this branch fixes the four highest-value items (marked ✅).
The rest are documented with enough detail to act on.

---

## Severity summary

| # | Finding | Severity | Status |
|---|---|---|---|
| 1 | Image decoder: uncaught crash + OOB heap write from a 154-byte file | **Critical** | ✅ Fixed |
| 2 | `===` does not exist; strict equality silently behaves as `==` | **Critical** | ✅ Fixed |
| 3 | `+=`, `-=`, `*=`, `/=`, `%=`, `++`, `--` are not tokenised | **Critical** | ✅ Fixed |
| 4 | GC roots are incomplete and never registered → latent use-after-free | **Critical** | ✅ Fixed |
| 5 | Segfault on deeply nested input (depth ≈ 3,000) | **High** | ✅ Fixed |
| 6 | Two incompatible object models in `JsValue` | **High** | 🟡 Mostly resolved (Phase 23): every object is a heap cell |
| 7 | `var` has block scope instead of function scope | **High** | ✅ Fixed (Phase 19) |
| 8 | No recursion guard in the JS engine | **High** | ✅ Fixed |
| 9 | `get_property()` allocates on every property read | **High (perf)** | Open |
| 10 | `JsValue` is 296 bytes and most of it is dead | **High (perf)** | 🟡 Improved: 88-byte value; payloads tagged/out-of-line |
| 11 | HTTP header injection + no response size cap | **Medium** | Open |
| 12 | `ComputedStyle` is 920 bytes; `LayoutBox` is 1,200 | **Medium (perf)** | Open |
| 13 | Build uses `-O2` only (no LTO / `-march=native`) | Low | Open |
| 14 | `has_class()` allocated a `stringstream` per call | **High (perf)** | ✅ Fixed |
| 15 | `text_content()` was O(n²) in depth | **Medium (perf)** | ✅ Fixed |
| 16 | Deep HTML parsing repeated scope scans and ancestor walks | **Medium (perf)** | ✅ Fixed |
| 17 | HTTP request injection and unbounded response reads | **High** | ✅ Fixed |
| 18 | A script was cut off at the first character the lexer did not know, and could run twice | **Critical** | ✅ Fixed (Phase 23) |
| 19 | `gc()` freed objects held only by native code or the native stack | **Critical** | ✅ Fixed (Phase 23) |
| 20 | A collection on one thread examined and freed a worker's objects | **High** | 🟡 Collections are per agent; the cell list is still shared |
| 21 | Script nested 200,000 deep ended the process (28 of 46 stress pages) | **Critical** | ✅ Fixed (Phase 23) |
| 22 | A template literal dropped substitutions that were literals | **High** | ✅ Fixed (Phase 23) |
| 23 | 1.2% of 67,545 generated expressions disagreed with Node | **High** | ✅ Fixed where pages meet it (Phase 23); 0.15% left |
| 24 | `node.appendChild(node)` ended the process, and so did any tree deeper than the stack: 300,000 nested `<div>` were a `SIGSEGV` | **High** | ✅ Fixed (Phase 25): cycles are refused, the parser stops nesting at 512, nothing that walks the DOM recurses |
| 25 | A string was copied on every read: loops over long strings were quadratic | **High (perf)** | ✅ Fixed (Phase 24) |
| 26 | Regular expressions over long input: quadratic lookbehind, limits on repeated groups | **Medium** | 🟡 Lookbehind fixed (Phase 24); repeated groups still limited |
| 27 | Pages hostile by size: a crash in an unclosed `calc(`, a 60-byte page that crashed the parser, bitmaps as large as the page asked for, loops that cost the square of their input | **High** | ✅ Fixed (Phase 25) |
| 28 | A block inside an inline box is not laid out; many sized floats in one box and a few other inputs still cost the square of their length | **Medium** | Open |
| 29 | About one random page in 115 got another tree than an independent parser builds: an end tag that meets an open SVG or MathML element, `</button>` with an element still open inside, and three smaller things | **Medium** | ✅ Fixed (Phase 26) |
| 30 | `<br/>` closed the paragraph it stood in: a void element written with a slash closed its parent. `innerHTML` parsed everything as the content of a `<body>`, so a table filled through it came out as loose text. Fourteen smaller things, most of them with no case in the corpus | **High** | ✅ Fixed (Phase 26) |
| 31 | The DOM a script sees: 61 of 572 cases recorded from jsdom passed. `querySelector('ul li')` found nothing, `classList`, `dataset`, `children`, `closest`, `append`, `new CustomEvent`, `location`, `getComputedStyle` did not exist, `innerHTML` read back text unescaped, nothing was an `instanceof` anything. URLs were resolved by cutting strings: `?q=1` on `/app/page.html` gave `/app/?q=1`. Scripts ran after the whole page was parsed | **Critical** | ✅ Fixed (Phase 27): 572 of 572; URL data 1,188 of 1,188. Open: XML in `DOMParser`, date inputs, `el.style` shorthands, layout numbers |
| 32 | Tables could not span: `colspan` and `rowspan` were read by nothing, `rowspan="0"` was ignored, a spanning cell pushed no column widths, `border-collapse` and `border-spacing` did not exist, a cell with a border lost it (cells were laid out directly instead of through `layout()`), and a percentage width inside a cell resolved against the column instead of the table | **Medium** | ✅ Fixed (Phase 29), re-landed from PR #16 |
| 33 | Deleting from a large property map rebuilt the index on every `delete`, and `shift()` moved the whole array tail each call | **High (perf)** | ✅ Fixed (Phase 30) |
| 35 | CSS trim sites erased the first character repeatedly, shifting the whole remainder; 300,000 spaces cost seconds in selectors, grid, filters, durations and transforms | **Medium (perf)** | ✅ Fixed (Phase 31); the separate finding-28 layout costs remain open (its tokenizer cost was fixed in Phase 39) |
| 36 | The renderer's color parser had 19 names, 15 of them not the CSS values, and returned black for the other 129 CSS keywords | **Medium** | ✅ Fixed (Phase 32) |
| 37 | Page `fetch()`/`Worker` sent arbitrary non-data/non-http addresses to the local filesystem; a web page could read local files, and a failed worker load could execute the path as source | **High (security)** | ✅ Fixed (Phase 33); CORS/XHR network/HTTPS landed in Phase 36 (preflight `OPTIONS` open) |
| 38 | Host names outside ASCII were kept as written instead of converted with UTS #46; the 38 remaining URL failures were all such hosts | **Medium** | ✅ Fixed; URL data 1188/1188 |
| 39 | `:nth-child(2n + 1)` read a sign separated from its digits by spaces as offset 0, and invalid formulas silently defaulted | **Medium** | ✅ Fixed; `:nth-of-type()` shares the fix |
| 40 | Removing attributes from the front of a long list shifted both the item vector and the index-number vector; 50,000 front removals took seconds while the back was milliseconds | **Medium (perf)** | ✅ Fixed (Phase 35); middle erases remain (the tokenizer repeated-name cost was fixed in Phase 39) |
| 41 | A function could not be a prototype: `Object.getPrototypeOf(Derived)` was not `Base` and could not be called, `Object.getPrototypeOf(TypeError)` was not `Error`, a static member was found one parent up and no further, `Function.prototype` was an object. Classes compiled by Babel and TypeScript stopped at their first class; every `assert_throws_js` of web-platform-tests failed | **High** | ✅ Fixed (Phase 34). Open: `new.target` in a parent's constructor, `Symbol.species`, `Symbol.hasInstance`, bound functions are never collected |
| 43 | Real-web raster/vector rendering gaps: no JPEG or GIF decoder, incomplete Canvas 2D CTM/path/stroke/clip/accessors, and missing SVG paths/arcs/gradients/`<use>`/`<clipPath>` | **High** | ✅ Fixed (Phase 38); `ImageDecoder.cpp` dispatch wired in Phase 36; WebP/AVIF & text remain open |
| 44 | `<img src>` bypassed `ResourceLoader::load_for_page` and read arbitrary local files directly from `LayoutTree`, and unloaded images without `width`/`height` laid out as `100×100` boxes instead of `0×0` | **High (security / layout)** | ✅ Fixed (Phase 36) |
| 45 | Worker script loads lacked MIME/module validation, `addEventListener("error")` listeners were never invoked, `Worker` instances leaked via an `inst` ↔ `WorkerObject` cycle, and pages exiting while workers were starting crashed in `SIGSEGV` / `heap-use-after-free` during static teardown of `js_symbol_for_key`'s registry and `DomBindings`'s interface table | **High (crash / security)** | ✅ Fixed (Phase 40); ES module execution, structured clone beyond JSON, and immortalizing `symbol_registry()` / `DomBindings` registry remain open |
| 46 | The HTML tokenizer compared every attribute name with every name it had already accepted: one element with 50,000 attributes cost the square of the count, and so did an end tag with that many | **Medium (perf)** | ✅ Fixed (Phase 39) |

---

## 1. ✅ Image decoder: crash and out-of-bounds write

**The hole.** Both `decode_bmp` and `decode_ppm` computed `width * height` as
`int`:

```cpp
img->pixels.resize(width * height, 0xFF000000);   // int * int
```

A BMP header is 54 bytes and fully attacker-controlled. Two shapes break it:

| Header claims | `w * h` as `int` | Effect |
|---|---|---|
| 65536 × 65535 | **−65536** | `resize(negative)` → `std::length_error` → `std::terminate` |
| 65537 × 65537 | **131073** | buffer holds 131,073 entries but `set_pixel` trusts 65537 × 65537 → **OOB heap write** |
| 65536 × 65536 | 0 | zero-length buffer, silent |

Reproduced against the real binary, no sanitizer needed:

```
$ cat page.html
<html><body><img src="evil3.bmp"></body></html>
$ ./build/litebird --render-bmp out.bmp page.html
terminate called after throwing an instance of 'std::length_error'
  what():  vector::_M_fill_insert
```

154 bytes of untrusted input, no `<script>` involved, and the process dies.
There is no `try`/`catch` anywhere in `main.cpp`.

**Why the original fuzzer missed it.** `tools/fuzz/fuzz_parser.py` fed every
mutation to `--dump-dom-webplatform`, which returns before layout and never
calls `ImageDecoder`. Phase 22 adds a pipeline fuzzer that runs corpus mutations
through layout and bitmap rendering, and seeds image cases with malformed BMP
and PPM headers. That pass also exposed unchecked decimal overflow in PPM
`read_int`; the parser now consumes overlong fields safely and rejects them.

**The fix.** Dimensions are validated before any multiplication, using division
so the check cannot itself overflow, capped at 64 megapixels. Also fixed:
`std::abs(INT32_MIN)` is UB and stays negative; and
`row_offset + row_width > size` could overflow, so it is now
`row_width > size - row_offset`. Regression test:
`image_decoder_rejects_hostile_dimensions`.

**Still open in the same file:** PPM `read_int()` consumes an unbounded run of
digits, so a long numeric token wraps `int` (UB) before the range check sees it.

---

## 2. ✅ Strict equality did not exist

The lexer scanned `===` and then threw the distinction away:

```cpp
t.type = JsTokenType::EqualEqual;
t.text = "==";
if (match('=')) t.text = "===";   // text updated, type is not
```

`text` is never consulted when evaluating, and there was no `StrictEqual`
token, opcode, or comparison anywhere in the codebase. Consequences:

```js
1 === "1"     // true   ≤ should be false
0 === false   // true   ≤ should be false
null === undefined  // true  ≤ should be false
```

This is the worst class of bug: **silent wrong answers**, not errors. Code that
*relies* on `===` to exclude coerced matches gets the wrong branch, quietly.

It also masked a second bug. `test_shadow_dom_and_slot_projection` asserts
`widget.shadowRoot === shadow`, passing only because everything compared equal.
With real `===`, it failed — revealing that `make_dom_node()` mints a **fresh
`JsObject` wrapper on every property access**, so `el === el` was false for any
two accesses. Both are now fixed: `strict_equals()` implements ES 7.2.16 and
compares DOM values by **node identity** rather than wrapper address.
Regression test: `strict_equality_does_not_coerce`.

---

## 3. ✅ Assignment operators were missing from the grammar

> **Fixed in `18be77f`.** All five compound forms plus `++`/`--` now work on both
> the bytecode VM and the AST interpreter, with a regression test that asserts
> the two paths agree. Details below are the original finding.


```js
var s = 0; s += 1;   // JS Runtime Error: Unexpected token in JS expression: =
var s = 0; s++;      // JS Runtime Error: Unexpected token in JS expression: +
var s = 5; s -= 2;   // same
var s = 5; s *= 2;   // same
```

`JsTokenType` has `Plus`, `Minus`, `Star`… but no `PlusEqual`, `MinusEqual`,
`PlusPlus`, `MinusMinus`. The lexer has no branch that would produce them, so
the parser sees `Plus` followed by `Equal` and fails.

**This was the single biggest obstacle to running real scripts.** `+=` and `++`
are in essentially every nontrivial script ever written; the engine's own
example pages hand-unroll them (`seconds = seconds + 1`) to work around it.
Fixing it means: 6 token types, a lexer branch each, a `parse_assignment()`
level in the parser, and an interpreter case that reads-modifies-writes the
lvalue. Roughly a day's work, and it unlocks more real-world compatibility than
any other single item on this list.

Phase 19 adds `typeof`, `switch`, right-associative `**`, and short-circuit `??`
to the parser and AST interpreter. These constructs use the interpreter fallback
when bytecode compilation does not support them. `"a".repeat(3)` remains outside
this phase and still returns `undefined`.

---

## 4. ✅ GC roots were incomplete — a live use-after-free

> **Fixed in `f78d201`.** The heap now has a root-provider registry; the
> interpreter and the event loop both contribute their live `JsValue`s. Verified
> with 200 consecutive empty-root-set collections under ASan. The original
> finding follows.
>
> **Superseded in Phase 23.** Registering roots at every boundary turned out
> not to be completable; finding 19 is what was still missed. The collector
> now derives its roots from reference counts and nothing is registered.


`JsObject` lives in a mark-and-sweep heap holding **raw `JsObject*`**. A
`JsValue` is a `shared_ptr`-managed value that can point at one. Nothing keeps
a `JsObject` alive except being reachable from the root set at collection time.

The root set is only:

```cpp
for (auto& v : m_stack)   roots.push_back(&v);   // VM operand stack
for (auto& [k, v] : m_globals) roots.push_back(&v);
```

`register_root()` exists in the API and **is never called anywhere**. So
`JsObject*` reachable only through:

- a closure environment (`JsEnvironment` holds `JsValue`s in `shared_ptr`s),
- a `NativeFn` lambda capture,
- a registered event listener,
- `Element::m_custom_element_instance`,

…is invisible to the collector and can be freed while live. `Heap::sweep()`
deletes the cell outright; every surviving `JsObject*` then dangles.

**It does fire, and it is not latent.** I originally wrote here that nothing
triggered a collection — that was wrong. `BrowserShell::render_terminal_frame`
collects on **every DevTools render** with an empty root list:

```cpp
if (devtools) {
    auto heap_stats = Heap::default_heap().collect_garbage();   // no roots
```

Against `examples/interactive.html` that prints `alive 23 -> 0` and ASan reports
the next property read as a `heap-use-after-free` in `Heap::sweep()`. Values
captured by pending promise handlers are hit the same way, and silently return
`undefined` instead of their data.

**Fix direction:** either make `JsValue` hold `shared_ptr<JsObject>` and drop
the custom collector, or register roots properly at every boundary
(`JsEnvironment`, native captures, listeners) and clear `is_marked` in one pass.
Also note `Heap::default_heap()` is a process-wide singleton with a mutex —
worker threads allocate from the *same* heap as the main thread, so a
collection on one thread can free objects the other is using.

---

## 5. ✅ Segfault on deeply nested input

> **Fixed.** The recursion was in `LayoutTree::build_box`, not the parser — see
> below. The per-frame cost was cut by moving the 920-byte style off the stack,
> and the recursion is now bounded by the *remaining native stack* rather than a
> fixed depth. Depth 50,000 parses and renders.
>
> A fixed depth constant was tried first, and the sanitizer CI job rejected it:
> a `build_box` level costs ~2.8 KB in an `-O2` build but ~34 KB under
> AddressSanitizer, so a nominal 512 levels exhausted an 8 MB stack at level 243
> in the ASan job while leaving plenty of room in the normal one. No single
> constant is correct for both. `include/litebird/common/StackLimit.hpp` instead
> compares the current stack pointer against the thread's stack bounds and stops
> when one more level would eat into a 1 MB reserve — the approach V8's
> `StackGuard` and SpiderMonkey's `CheckRecursionLimit` take. It adapts to build
> flags, platform and thread automatically.
>
> Consequence worth knowing: the *reachable* depth now depends on the build.
> Normal builds get thousands of levels; an ASan build gets ~1,000 for layout and
> ~130 for JS, because its frames are ~20-55x larger. That is a measurement of
> the sanitizer, not a regression.
>
> The limit on nesting in the parser that the text below asks for arrived in
> Phase 25 (finding 24). The HTML standard has no such rule; Blink and WebKit
> do, and what goes past the limit is attached as a sibling, not turned into
> text.


```
depth 1,000: ok
depth 3,000: SEGFAULT
depth 5,000: SEGFAULT
```

Deterministic, reproducible, ~9 KB of input. Nothing in the codebase tracks
recursion depth: the tree builder, `LayoutTree::build_box`, `Node::dump`,
`text_content()` and the JS expression evaluator are all unguarded recursion.
The crash happens *during parsing* (zero bytes reach stdout).

Browsers cap this at ~512 nesting depth for exactly this reason. The cheapest
honest fix is a depth counter in `HtmlTreeBuilder` that flushes the token as
text past the limit, which is what the HTML spec already tells you to do.

---

## 6. 🟡 Two incompatible object models in one struct

> **Mostly resolved in Phase 23.** Every object, array and wrapper is a heap
> cell now, and a script function keeps its own properties in one. The
> `properties` map survives on `JsValue` for symbols (their description) and for
> native functions' own properties, which live in the function's shared payload.
> The read sites that had to check both are gone. The original finding follows.

`JsValue` carries both a GC pointer and a `shared_ptr` to a property map:

```cpp
JsObject* object_val;                                            // GC'd
std::shared_ptr<std::unordered_map<std::string, JsValue>> properties;  // refcounted
```

`set_property()` uses the first when `object_val` is set and the second
otherwise, and read sites have to check both:

```cpp
if (arg0.object_val) { for (auto& [k,v] : arg0.object_val->properties) ... }
else if (arg0.properties) { for (auto& [k,v] : *arg0.properties) ... }
```

Some objects therefore **do not participate in GC at all**, and `Fetch.cpp`
needing a manual `else if` is the symptom. Pick one model. This is also the
main source of the "known leaks" in `ARCHITECTURE.md`.

---

## 7. ✅ `var` has function scope (Phase 19)

`var` declarations are hoisted to the nearest function or script scope,
including declarations nested in blocks and `for`/`for...of` headers. The
interpreter predeclares them as `undefined`; assignment during execution
updates the function-scoped binding. `let` and `const` remain block-scoped.
Regression coverage includes block escape, function isolation, use before
initialization (`typeof`), and loop-header declarations. Script-level `var`
bindings are synchronized with the VM global table for interoperability with
the existing bytecode execution path.

---

## 8. ✅ No recursion guard in the JS engine

> **Fixed.** Guard lives in `eval_expr` (guarding only call sites was not
> enough — the recursion is per operator level). Runaway recursion now reports
> `RangeError: Maximum call stack size exceeded` and the interpreter survives.
>
> Like finding 5, the guard is a stack-headroom check rather than a fixed call
> count, because one JS call costs ~1 KB of native stack in an `-O2` build but
> ~55 KB under AddressSanitizer. Measured: the guard trips at depth 133 under
> ASan and around 7,000 uninstrumented, both with the same 1 MB reserve intact.
> A fixed counter tuned for one build is either too shallow or unsafe in the
> other.
>
> One wrinkle found while testing: `JsInterpreter::eval()` runs the bytecode VM
> first and falls through to the AST interpreter on *any* exception, so a
> VM-level `RangeError` silently re-runs the whole script in the AST engine
> before reporting. Harmless here (both bounded), but it is surprising behaviour
> in a fallback path and is noted under finding 6.


There is no call-depth counter and no `RangeError` path. The VM has an explicit
`m_stack` (so bytecode frames are heap-allocated and bounded), but the
**AST interpreter** (`JsInterpreter::eval_expr`) recurses on the C++ stack for
nested expressions and calls. A deeply nested expression or unbounded JS
recursion is a straightforward stack-overflow crash.

---

## 9. `get_property()` allocates on every property read

```cpp
auto it_getter = object_val->properties.find("__get_" + name + "__");
```

That is a heap-allocating string **per property access**, plus a second lookup,
before the real lookup happens — and it runs on every `obj.x` in every loop
iteration. Add `Element::get_attribute()` returning `std::string` **by value**
(an allocation per attribute read) and `Element::classes()` building a vector of
strings, and the interpreter is allocation-bound rather than compute-bound.

The `__get_<name>__` convention is a hack standing in for accessors; property
access should not need string concatenation to discover that a getter exists.

---

## 10. 🟡 `JsValue` payload footprint improved

The original inline representation measured 296 bytes. Phase 20 moves cold,
type-specific state into a copy-on-write `std::variant` payload and exposes
accessors instead of carrying function metadata, callbacks, DOM handles, and
canvas/promise/generator handles in every value. On the current 64-bit GCC
build:

```text
sizeof(JsValue): 296 bytes -> 96 bytes (67.6% smaller)
```

Primitive values and ordinary objects do not allocate the extra payload.
Function/DOM/canvas/promise/generator values allocate it on demand; the variant
stores only the active payload alternative. Copy-on-write setters preserve
value-copy behavior when a payload-bearing `JsValue` is copied and then edited.
The regression suite caps `sizeof(JsValue)` at 128 bytes and checks copy
isolation.

A paired local measurement compared the clean `afad827` build with the current
worktree, using the same 700,026-byte document containing 20,000
`<div class="item">Hello world</div>` nodes. Peak RSS (one run per mode, Linux
`wait4` `ru_maxrss`) changed from **28,452 to 24,888 KB** for
`--dump-dom-webplatform` (-12.5%), and **162,296 to 158,336 KB** for
`--render-terminal` (-2.4%); layout dominates the latter. A bytecode-path
microbenchmark (`let sum = 0; for (let i = 0; i < 200000; i++) { sum += i; }`)
ran five times per build: median wall time fell from **177.16 ms to 71.99 ms**
(-59.4%). These are indicative local measurements, not CI performance
thresholds; the RSS comparison used one run per mode.

The common representation still includes a string, an object pointer, and a
property-map pointer, so further tagged scalar/string compaction remains
possible. Since Phase 24 the string is a 24-byte handle on shared text
(finding 25), not a 32-byte `std::string`, and the value is 88 bytes. The previous 98 MB figure used a different page/workload and should
not be compared directly to these measurements.

---

## 11. HTTP header injection, no response cap

```cpp
std::string request = req_method + " " + path + " HTTP/1.1\r\n" + ...;
for (const auto& [hk, hv] : headers) request += hk + ": " + hv + "\r\n";
```

Neither the method, the header names, nor the header values are stripped of
`\r`/`\n`. A script that calls `fetch(url, {headers: {"X": "a\r\nEvil: 1"}})`
injects arbitrary headers and can smuggle a whole second request. The response
read loop appends until `recv` returns 0 with no size limit, so a hostile or
misconfigured server can exhaust memory.

---

## 12. `ComputedStyle` is 920 bytes and copied per fragment

`LayoutBox` holds one inline (1,200 bytes total) and every `InlineFragment`
holds another copy. A paragraph of 40 words therefore carries ~37 KB of style.
Two easy wins: drop `text_align` / `text_decoration` from `std::string` to an
enum (they allocate), and have fragments reference the box's style instead of
copying it.

---

## 13. Build configuration

`-O2 -Wall -Wextra`, no LTO, no `-march=native`, no PGO. `make asan` does not
pass `-j`, so CI's sanitizer job builds serially — it took >10 minutes for
~40 of 52 objects here on 2 cores. CI does exercise ASan/UBSan/fuzzing, which
is genuinely better than most hobby projects, but the fuzzer's coverage is
parser-only (see #1).

---

## 14. ✅ `has_class()` built a `stringstream` per call

```cpp
bool has_class(const std::string& c) const {
    auto list = classes();              // stringstream + vector<string> + copies
    return std::find(list.begin(), list.end(), c) != list.end();
}
```

Measured: **1,100 ns/call** — a microsecond to answer a substring question. It
is called once per class selector per element during the cascade, so it
dominated style resolution. Now an in-place scan of the `class` attribute.

## 15. ✅ `text_content()` was O(n²) in depth

```cpp
std::string Node::text_content() const {
    std::string res;
    for (const auto& ch : m_children) res += ch->text_content();  // fresh string per subtree
    return res;
}
```

Every level copied all of its descendants' text again. Now appends into one
buffer.

---

## 16. ✅ Deep HTML parsing repeated scope scans and ancestor walks

The tree builder had two depth-dependent costs:

- Scope queries rescanned `m_open_elements` from the current node to a boundary.
  Deep stacks often asked about tags absent from the stack, or about a target
  below thousands of ordinary elements.
- `Node::append_child` called `is_connected()`, which walked all ancestors for
  every insertion.

The open-element stack now maintains HTML tag positions and scope-boundary
positions incrementally on push/pop. Scope membership is decided from the
nearest target and boundary indexes; the less common adoption-agency edits
rebuild the indexes after changing the middle of the stack. `Node` also caches
connectivity, updating it on parent changes and propagating through light DOM
and shadow roots. Parser insertion therefore does not repeatedly walk the
ancestor chain.

A local `-O2` parser-only harness (read input, call `HtmlParser::parse`, discard
the document) now measures:

| depth | time |
|---:|---:|
| 1,000 | 5 ms |
| 2,000 | 6 ms |
| 3,000 | 8 ms |
| 5,000 | 12 ms |
| 10,000 | 20 ms |
| 20,000 | 38 ms |

The old CLI measurements also serialized the resulting DOM. The web-platform
tree dumper emits indentation proportional to depth for every node, so dumping
a single deeply nested chain still produces O(depth²) output; those end-to-end
numbers should not be confused with parser time.

Conformance remains **1770/1792 (98.8%)**. The cache is tested across attach,
detach, reattach, and shadow-host connectivity transitions.

---

## Measured impact of cascade indexing and parser improvements

Cascade benchmark (`--dump-layout`): for each N, generate N rules of the form
`div.c{i} { color: #... }` and N matching `<div class="c{i}">x</div>` elements.
Report median wall time from three process runs. This compares the clean
`afad827` build with the current indexed worktree:

| N rules and elements | afad827 | indexed | speedup |
|---:|---:|---:|---:|
| 200 | 6.18 ms | 5.28 ms | 1.2× |
| 400 | 11.99 ms | 7.91 ms | 1.5× |
| 800 | 30.49 ms | 14.46 ms | 2.1× |
| 1600 | 88.25 ms | 30.35 ms | **2.9×** |

The index buckets author selectors by one necessary rightmost key (ID, one
class token, tag, or a universal fallback), gathers only candidate selectors
for each element, sorts candidates by source order, then confirms them with the
full matcher. More than one possible index key is deliberately not recorded
per selector, preventing duplicate matches. UA and shadow-root rules retain
their existing scan paths; universal/attribute/pseudo-only rules also remain
in the fallback bucket, so those workloads can still approach O(elements ×
rules).

Nested-depth parse (`--dump-dom`):

| depth | before | after | |
|---|---|---|---|
| 400 | 49.1 ms | 2.3 ms | 21× |
| 800 | 96.7 ms | 2.1 ms | 46× |
| 1600 | 379.8 ms | 2.1 ms | **180×** |

---

## Suggested order of work

1. **~~Assignment operators~~** (#3) and **~~JavaScript compatibility subset~~**
   (#7, plus `typeof` / `switch` / `**` / `??`) — implemented in Phases 19.
2. **~~GC roots~~** (#4) — done.
3. **~~Depth guard~~** (#5) and **~~JS recursion guard~~** (#8) — done.
4. **~~Initial `JsValue` footprint/throughput measurements~~** (#10) — done;
   further tagged scalar/string compaction should be driven by profiling.
5. **~~Cascade indexing~~** — initial rightmost-selector buckets implemented in
   Phase 21; shadow-root/UA indexing and universal-rule heavy workloads remain
   possible follow-ups.
6. **~~Fuzzer coverage~~** — Phase 22 now fuzzes parser, layout, bitmap render,
   and malformed BMP/PPM headers through the actual image-loading path. The
   existing work does not yet cover standalone CSS or arbitrary network input.

## One meta-point

The conformance harness is genuinely good — it is wired into CI with a floor,
and Phase 17's audit that *fixed the measurement* rather than the engine is
senior-level work. But 98.8% on tree construction coexists with all of the
above, because the scoreboard measures one stage of a five-stage pipeline and
says nothing about memory safety, script semantics, or speed. A second
scoreboard — even a tiny one: "no crash on N hostile images", "these 20 JS
expressions produce these 20 results" — would catch every Critical finding here.


## 17. ✅ HTTP request injection and unbounded response reads

The POSIX HTTP client previously concatenated caller-supplied method/header
strings and URL-derived request targets directly into a raw HTTP/1.1 request.
CR/LF in those values could inject request lines or headers. It also accumulated
the peer's response until disconnect without a size bound.

`ResourceLoader::load_http()` now accepts only HTTP token methods/header names,
rejects control bytes (including CR/LF) in header values and request targets,
validates URL authority/port parsing, and manages framing headers (accepting a
caller-supplied `Content-Length` only when it exactly matches the body). Complete
request headers are capped at 64 KiB before DNS/network work.
Incoming headers and the full response stream are capped at 64 KiB and 16 MiB
respectively; malformed/incomplete response headers and invalid or oversized
`Content-Length` values are rejected. Socket sends handle partial writes and
interrupted system calls.

Regression coverage exercises method, header-value, and request-target
injection attempts, a successful local loopback response, and a response that
exceeds the 16 MiB cap. Tests use a loopback socket only and do not require
external networking.

## 18. ✅ A script was cut off at the first unknown character, and could run twice

Found in Phase 23 by running the engine against an outside corpus
(`tests/js-conformance/`, expected output recorded from Node.js). The engine
passed its own unit tests and 25 of 90 everyday scripts.

- The lexer's default case turned any character it did not recognise into the
  end-of-file token. `?`, `&`, `|`, `^` and `~` were among them, so a script
  using a ternary or a bitwise operator stopped there, without an error, and
  everything after it never ran.
- `eval()` ran the bytecode VM and, on any exception, ran the whole source
  again in the tree-walker. `console.log("side effect"); undefinedFunction()`
  printed twice. Any script that touched the DOM or the network before failing
  did so twice.

Both are gone with the rewrite: the lexer reports what it cannot tokenise as a
`SyntaxError`, a script that does not parse does not run, and there is one
execution path.

## 19. ✅ `gc()` freed objects that were still in use

Finding 4 was fixed by registering roots. After Phase 23 every object is a
heap cell, which made every unregistered holder a use-after-free the next time
script called `gc()`:

```js
function inner() { gc(); }
function outer() { var o = {a: [1, 2, 3]}; inner(); return o.a.length; }   // o freed inside inner()

for (let o = {a: 1}, i = 0; i < 2; i++) { gc(); o.a; }                     // the loop's scope was not a root

button.addEventListener("click", function () { state.count++; });        // `state` lives in a std::function
gc(); button.click();
```

AddressSanitizer reported each as `heap-use-after-free`. The same shape exists
wherever native code keeps a value: observers, timers, promise reactions, a
half-evaluated argument list, the stack of a suspended generator.

**Fix.** The collector no longer depends on registration. Each `JsValue` holds
a counted handle to its cell; a collection counts the references it can find
inside the heap and treats anything with more references than that as held by
native code, hence a root (`Heap::collect_garbage`, described in
`ARCHITECTURE.md`, Phase 23).

**Tests.** `tests/js-conformance/gc-safety.dat` has 32 cases that collect in
the middle of something (the first two above among them) and must still print
Node's answer. The listener case needs a DOM, so it is a unit test,
`gc_keeps_state_reachable_only_from_listeners_and_frames`. `make asan` runs
both under the sanitizers, and the corpus runner now fails the run on any
sanitizer report in a case's stderr, whatever the pass count: UBSan prints its
report and carries on, so a case could otherwise print the right answer after
undefined behaviour and count as a pass.

## 20. 🟡 A collection on one thread reached into a worker's objects

A worker runs its own interpreter on its own thread but allocates from the
process-wide heap. A collection started by the page marked through, and could
free, cells the worker thread was using at that moment.

Cells and scopes are now tagged with the agent (page or worker) that created
them. A collection examines and frees only its own agent's; other agents'
cells are kept and never looked into. Workers also have their own event loop,
so their timers and promise callbacks no longer run on the page's thread.

Still open: all agents share one cell list behind one mutex, so a collection
blocks other agents' allocations while it marks.


## 21. ✅ Deeply nested input killed the process

Found by generating stress pages rather than by reading code: one script per
page, each nesting one construct 200,000 deep, followed by a second script that
reports whether it ran. 28 of 46 pages ended in `SIGSEGV`. A page could take the
browser down with one long line such as `((((...1...))))`.

The engine recurses over what a script gives it, and only function calls were
guarded (finding 6). Four separate recursions were not:

- **The parser.** Every nested expression, statement, pattern or function body
  is a level of recursive descent.
- **The evaluator**, on nested statements and patterns, and on long operator
  chains: `a+b+c+...` parses into a tree as deep as the chain is long.
- **Library code**: `join` calling `toString` calling `join`, `flat`,
  `structuredClone`, `JSON.stringify` and the JSON parser.
- **Destructors.** Freeing a syntax tree, a chain of closures (each scope owns
  its parent), or a chain of promises ran one destructor inside another, once
  per link. This one needs no deep syntax at all: a loop that builds a linked
  list of scopes is enough, and it crashes when the list is dropped.

Fixes:

- `js_check_stack()` measures the native stack that is left
  (`common/StackLimit.hpp`) and throws a `RangeError` the script can catch. It
  is called at the recursive entry points of the parser, the evaluator and the
  library functions above. Depth counters were not used: how much stack one
  level costs differs by a factor of 50 between an optimised and a sanitizer
  build.
- Operator chains of 8 or more are evaluated in a loop
  (`eval_binary_chain`), so a long `a+b+c+...` is not an error.
- Destruction is iterative. `common/ReleaseQueue.hpp` collects what a destructor
  would otherwise destroy recursively and drains it in a loop; syntax tree nodes
  are deleted through it (`ast_delete`), scopes release their parent chain in a
  loop, and so do promises and function payloads.

After the fix no stress page crashes, in the normal build or under
AddressSanitizer. Deep syntax reports `Uncaught RangeError: Maximum call stack
size exceeded` and the next script on the page runs, which is what V8 does.

**Tests:** `tests/js-conformance/deep-nesting.dat` (63 cases, expectations from
Node) and the unit test `deep_nesting_never_exhausts_the_native_stack`.

Generators and async functions recurse without using the stack: a frame that
resumes another frame waits for it on its own thread. `function* g() { yield*
g(); }` took threads until the system refused one, 15,724 on the test machine,
and under AddressSanitizer ran out of memory first. Frames may now nest 2,000
deep; beyond that the script gets the same `RangeError`.

**Still open.** One stress page does not crash but is killed for memory:
100,000 chained `bind` calls (the name `bound bound ...` grows quadratically;
20,000 take 1.3 s and 1.2 GB). The other one that was, a string built from two
million `+` terms, gave no answer in two minutes; since finding 25 was fixed
it finishes in 3.7 s (Node: 0.5 s), though the syntax tree of that 12 MB
script takes 1 GB. And the DOM is not covered by any of this: see finding 24.

## 22. ✅ A template literal dropped substitutions that were literals

`` `${200000}` `` evaluated to an empty string, and `` `a${1}b` `` to `ab`. The
parser stores a template as a list of parts, text and substitutions alternating,
and the evaluator told the two apart by node type: a string literal node was
taken to be text. A substitution that was itself a literal (`${1}`, `${'x'}`,
`${null}`) was therefore treated as text, and number literals were skipped.

Parts are now told apart by position. Found by a differential run: 67,545
generated expressions evaluated by this engine and by Node, results compared.
The 671-case corpus had not caught it because every template in it substituted
a variable.

## 23. ✅ What the differential run found

The same run (finding 22) left 1.2% of expressions disagreeing with Node. The
causes that real pages meet were fixed, each with corpus cases:

- **`undefined` was a keyword.** `function (window, undefined) { ... }`, the
  opening line of jQuery-era libraries, was a syntax error. It is now an
  ordinary identifier bound to a read-only global, as are `NaN` and `Infinity`:
  assigning to them does nothing in sloppy code and throws in strict code.
- **Every function printed as `function f() { [native code] }`.** Libraries
  test for built-ins with `/native code/.test(fn)`, so every user function
  passed as a built-in. Functions and classes now keep their extent in the
  source and print it.
- **Lookups stopped 10,000 objects up a prototype chain** and answered "not
  found". The limit existed because nothing stopped a script from making a
  chain circular. `Object.setPrototypeOf`, `Reflect.setPrototypeOf` and
  `__proto__` now refuse to close a loop, and the limit is gone.
- `new String('abc')[1]` was `undefined`, and `Object.keys` of a String object
  was empty.
- `-2 ** 2` was accepted; it is a syntax error.
- `escape` and `unescape` were missing.
- `Math.hypot(1e300, 3)` overflowed to `Infinity`; `parseInt` of a long digit
  string was off in the last digits.
- `Date.prototype.toString` named the zone `(UTC)` where browsers print
  `(Coordinated Universal Time)`.

Left as they are, and listed in ARCHITECTURE.md: arrays are dense, last-digit
differences in `Math.atan2`, `cbrt` and `pow`, and date strings such as
`new Date('1')`.

## 24. ✅ `node.appendChild(node)` ended the process, and so did a very deep tree

```js
var d = document.createElement('div');
d.appendChild(d);
```

The DOM accepted this. The node became its own child and its own parent, and
the next thing to walk the tree (`textContent`, serialisation, layout, the
destructor) recursed until the stack was gone. Appending a node to one of its
own descendants did the same in two steps. The DOM standard forbids both.

`Node::append_child` and `Node::insert_before` are the only two places a tree
grows, and both now refuse an insertion that would make a node its own
ancestor, counting a shadow tree as part of its host. The script bindings throw
`HierarchyRequestError` (code 3), as a browser does. The check costs nothing
for a node with no children, which is every node the HTML parser appends.
Test: `dom_insertion_cannot_make_a_node_its_own_ancestor`.

### Depth (fixed in Phase 25)

A tree does not need a cycle to be too deep to walk. The DOM and the code
that walked it were recursive throughout (`textContent`, `innerHTML`, the
id, tag and selector searches, the dumps, the destructor), and only the
layout tree builder checked the stack. The mutation fuzzers start from the
html5lib inputs, a few lines each, and change them a few bytes at a time.
They never come near the tens of thousands of levels this takes, which is
why they reported nothing.

`tools/fuzz/deep_pages.py` generates the pages they cannot. Each of its
145 pages nests or repeats one construct, most of them 200,000 times, and is
run in a child process with an 8 MB stack, 2 GB of memory and 60 seconds: 49
nest one kind of markup, 29 a CSS value or rule, 43 build their tree from
script, 14 leave 200,000 elements open and then send 100,000 tokens, 5 put
100,000 or 200,000 children in one paragraph, and 5 ask for bitmaps larger
than memory.

| | Before | Now |
| :--- | :--- | :--- |
| 300,000 nested `<div>`, a 3.3 MB file, in each of five output modes | `SIGSEGV` after 0.4 s | exit 0 in 3.7 to 5.0 s |
| `appendChild` in a loop, 20,000 levels | 3.2 s | 0.07 s |
| the same, 40,000 levels | 13.2 s | 0.12 s |
| the same, 200,000 levels | not finished after 120 s | 0.42 s |
| 20,000 levels, then `textContent`, `innerHTML` and `querySelectorAll` on the top, attached to the document, removed, dropped | `SIGSEGV` | 0.12 s |
| the same, 200,000 levels | not finished after 120 s | 0.68 s |
| Runs of the deep pages that fail, of 392 | 264, on 110 of the 145 pages: 139 `SIGSEGV`, 106 out of time, 8 aborted, 7 out of memory, 4 others | 0 |

Three things were done, because a tree gets deep in two ways and is walked by
two kinds of code.

**1. The parser stops nesting at 512.** Nothing the HTML parser attaches is
deeper than `HtmlTreeBuilder::kMaxTreeDepth`, counting `<html>` as 1. An
element that would be deeper is attached to the nearest ancestor that can
still take it, which makes it a sibling of the element it was written inside.
Of 6,000 nested `<div>` elements all 6,000 are in the tree: the first 510
inside one another, the other 5,490 beside the innermost of those. The text
is in the last one, and a paragraph after the closing tags is a child of
`<body>`, where it would be without the limit.

The HTML standard has no such limit. Blink and WebKit both have one, and both
set it to 512. (Gecko was not looked at.) They differ in the rule, and the
difference matters:

- WebKit closes the current node when the limit is reached
  (`HTMLConstructionSite::attachLater`). This was implemented first. It
  leaves the insertion mode describing an element that is no longer open:
  "in select" with no `<select>` on the stack. Of 1,500 random pages written
  to sit on the limit, 12 then crashed, one of them by a road that needs no
  depth at all (finding 27).
- Blink changes only where the node is attached
  (`HTMLConstructionSite::Attach`). The stack of open elements, the insertion
  mode and the list of formatting elements stay what the standard says they
  are, at any depth. This is what Litebird does now. Of 4,500 random pages at
  the limit, none crashes and none has an element deeper than 512; 300 more
  run in CI.

Two things go beyond Blink's rule. The depth is measured in the tree and not
as the length of the stack, because the two differ: `</form>` takes the form
off the stack and leaves what is open inside it, so `<form><div></form>`
repeated nests two levels for each element the stack gains. And the adoption
agency algorithm, which moves a block with everything in it under a new
element, is held to the same limit: it used to carry content past it.

With the stack no longer capped, every question the standard answers by
walking down it would cost the depth of the page for every token. Finding 16
gave the scope queries an index. The walks that were left ("which `<li>` does
this `<li>` close", "what does this end tag close", "is anything on the stack
foreign", resetting the insertion mode) now have one too. 20,000 elements
left open, followed by 20,000 times the same token, parsed and printed as a
tree:

| Token | Before | Now |
| :--- | :--- | :--- |
| `<table></table>` | 63.1 s | 0.79 s |
| `<li>x</li>` | out of memory: killed by the system at 3.6 GB, after about 40 s | 1.19 s |
| `<b>x</b>` | out of memory: killed at 3.6 GB, after about 15 s | 1.16 s |
| `</x>`, an end tag that closes nothing, after `<span>` | 14.6 s | 0.35 s |

The dump of such a tree used to grow with the square of its depth as well,
because each line is indented by how deep its node is (finding 16). With the
limit a line is indented by at most 1 KB.

What the limit does not always keep is the order of content. An element
written inside another is attached beside it, and misnested formatting tags
can then move the outer one: `<section>x<p>y</p>` inside 300 open `<b>` at
the limit, followed by their end tags, reads "yx". Everything written is in
the tree. This was not compared with a browser. Below the limit nothing
changes: the html5lib corpus, whose deepest tree has 15 levels, went from
1770 to 1771 of 1792, the one being a parser bug found on the way (finding
27).

**2. A tree built by script has no limit, so nothing that walks the DOM
recurses.** A browser lets a script `appendChild` 200,000 levels deep, and so
does Litebird. The destructor releases a subtree with a loop and a list of
what is left to release, and `textContent`, `innerHTML`, `querySelector(All)`,
`getElementById`, the tag and class searches, the propagation of "is
connected" and the tree dumps are loops over
`include/litebird/dom/TreeWalk.hpp`. A stack check would not have done here:
a `textContent` that stops halfway returns a wrong answer, where layout that
stops halfway draws less.

The check that an insertion makes no cycle (the first half of this finding)
walked from the new parent to the root, which made building a chain cost the
square of its depth: the 3.2 s and 13.2 s above. It now walks up from the
parent and down from the node in turn, and stops when either ends. For a node
with few descendants that is at once.

**3. Layout, painting, SVG rendering and hit testing still recurse, and ask
`stack_is_exhausted()` at every level** (finding 5). What lies deeper than
the stack reaches is not laid out. For a parsed page in a normal build that
cannot happen: 512 levels of `build_box` are 1.4 MB. A tree that a script
built deeper than that is in the DOM, answers every query, and is laid out as
far down as the stack goes. `--dump-dom` prints 1,024 levels and then says
how many nodes it left out and how deep they go.

**How it stays fixed.** `make deep` runs the deep pages and 300 random pages
at the nesting limit, and is a CI job. At 200,000 levels on an 8 MB stack a
walk has 42 bytes of stack for each level, which stops every recursion but
one with a very small frame. The unit test
`dom_trees_of_any_depth_can_be_walked_and_released` closes that gap for the
DOM: it walks and releases a chain of a million nodes, which leaves 8 bytes a
level, and a function that calls itself uses at least 16. It runs in the
normal build and in the sanitizer build. From script, a chain of 2,000,000
(4 bytes a level) is built, read through `textContent`, `innerHTML` and
`querySelectorAll`, attached, removed and dropped in 10 s; it takes 2 GB,
about 1 KB a level.

`make asan` runs the deep pages in the sanitizer build as well, for memory
errors and not for recursion: a twentieth as deep, which keeps a build that
is several times slower to a few minutes, on a quarter of the stack. The
other unit tests are
`html_parser_nesting_limit_holds_for_every_kind_of_element` and
`deeply_nested_markup_does_not_overflow_the_stack`, which used to require
6,000 nested levels to survive and now requires 6,000 elements, none deeper
than 512.

## 25. ✅ Strings were copied on every read

Not a crash, but it was the largest defect left in the JavaScript engine,
found by timing ordinary scripts against Node. A string lived inside the value
that held it. Reading a string variable copied the string; `length` and
indexing scanned it to find out whether it was ASCII. Each of those was
proportional to the length of the string, so a loop over a long string was
quadratic, and so was building one with `+=`. A script that decoded, hashed or
built a large string one character at a time appeared to hang. One of this
audit's own corpus cases did: it built a 900 KB script with `+=` and took 8 s,
until it was rewritten to use `join`.

**The fix.** A string value now holds a `JsString`
(`include/litebird/js/JsString.hpp`): a 24-byte handle on a range of a shared,
reference-counted block of UTF-8.

- Copying a value copies the handle. `length` is a field.
- An ASCII string is indexed by reading a byte. Any other string gets a UTF-16
  copy the first time it is indexed; the copy stays with the block and is
  extended when the text grows, never rebuilt.
- `a + b` writes `b` into the free room after `a` when `a` is the newest text
  in its block, and copies nothing of `a`. Text that keeps moving to a bigger
  block is given half again as much room, so n appends cost n.
- A slice that is a quarter of its block or more is another range of the same
  block. Smaller slices are copies, which bounds how much memory a slice can
  keep alive.
- Bytes that have been written are never written again, so no string can see
  what was appended to another.

Milliseconds for the whole process on one machine. "Before" is the engine at
`54b1986`, one run with a 120 s limit. "After" and Node are the best of three.
The scripts are in `tools/bench/bench_strings.py` (`make bench`), which also
checks that both engines print the same result.

| Script | Before | After | Node 22 |
| :--- | ---: | ---: | ---: |
| `s.charCodeAt(i)` over 200,000 characters | 77,082 | 91 | 31 |
| `s[i]` over 200,000 characters | 55,912 | 63 | 31 |
| `s.charCodeAt(i)` over 200,000 characters that are not ASCII | over 120 s | 94 | 35 |
| `i < s.length` as the bound of that loop | 84,863 | 46 | 29 |
| one long string passed to a function 200,000 times | 46,722 | 199 | 29 |
| `s.substring(i, i + 5)` at each of 100,000 positions | 17,640 | 62 | 33 |
| the same in text that is not ASCII | 72,321 | 66 | 33 |
| `indexOf` from hit to hit, 20,000 hits | 4,000 | 13 | 30 |
| `s += x`, 100,000 times | 9,811 | 49 | 45 |
| `obj.s += x`, 100,000 times | 4,154 | 35 | 41 |
| 30,000 table rows appended to one string | 5,440 | 26 | 44 |
| append, then read the last character, 50,000 times | 4,071 | 33 | 191 |
| `s += x` keeping every intermediate string, 20,000 times | 1,815 | 18 | 40 |
| tokenizer that eats its input: `s = s.slice(k)`, 20,000 tokens | 2,556 | 18 | 31 |
| the same in text that is not ASCII | 13,747 | 20 | 31 |
| `re.exec(s)` in a loop, 20,000 matches in 200,000 characters | killed at 4.6 s | 44 | 33 |
| sticky-flag tokenizer over 150,000 characters | killed at 5.8 s | 115 | 35 |
| `/^x/.test(s)` on a 200,000-character string, 20,000 times | 5,060 | 31 | 31 |

The two "killed" rows ran out of memory, not time: every match array held its
own copy of the subject in `input` (190 KB a match), and nothing is collected
until `gc()` is called. `input` now shares the subject, and the same loop peaks
at 10 MB. The rows that are still several times Node's do a function call or
a regular-expression step per character; that is the interpreter, not the
string.

**What it cost.** A string of a few characters used to live inside its value
and now has a block of its own, so the worry was short strings, which is
nearly every string a page handles.

In time, nothing measurable. Best of five, before and after: `'k' + i`
200,000 times 87 and 84 ms; `===` on short strings 91 and 82; `typeof x ===
'string'` 103 and 93; 200,000 writes to string-keyed properties 94 and 91;
sorting 50,000 strings 66 and 64. Fourteen ordinary scripts (loops, calls,
objects, JSON, template literals, `Map`, short regular expressions) are each
within 1% of their old time or faster. One was not at first: filling a
dictionary with computed keys ran 16% slower, until `'key' + i` stopped going
through the general conversion path.

In memory, about 20 bytes for each distinct short string: 200,000 of them in
an array peak at 33.6 MB where they took 29.7. Against that, a value is 88
bytes, down from 96; strings that are copies of one another are one block
(200,000 references to one string: 27.8 MB, was 35.7); and slices of a long
string share it (a 5 MB string with 1,000 slices of 2 MB each: 15 MB, was
1.9 GB). A slice can keep at most four times its own size alive.

**What else it changed.** Six things that were wrong are right. The first
three follow from storing text one way; the rest were found by the same
tests.

- Two halves of a surrogate pair that meet are the character they spell:
  `"\uD83D" + "\uDE00" === "😀"` was false, and so were the same halves
  brought together by `join`, `repeat` or `padEnd`; as a property key the
  two were different keys, and `for`...`of` saw two characters. A corpus case
  now checks 63 ways for two halves to meet.
- `<` and `sort()` order by UTF-16 code unit. They compared UTF-8 bytes, which
  puts `"😀"` above `"\uFFFF"`; JavaScript puts it below.
- A string too long to exist is a `RangeError`. Doubling a string in a loop
  used to end with the system killing the process at 3.8 GB. The limit is
  2^29 − 24 bytes of UTF-8, which for ASCII is V8's limit exactly and for
  other text is reached sooner than V8's.
- `String.prototype.isWellFormed` and `toWellFormed` exist.
- Text that leaves the engine is UTF-8. `console.log("\uD83D")` wrote the
  three bytes of a lone half, which no reader accepts; it writes U+FFFD, as
  Node does.
- `decodeURIComponent` accepts only UTF-8. `"%ED%A0%BD"` (a surrogate) and
  `"%C0%80"` (an overlong form) decoded to something; both are a `URIError`.
  And `decodeURI("%00")` is the NUL character; it was left as `%00`.

`tools/conformance/diff_strings.py` (`make diff-strings`, and a step in CI)
evaluates a grid in Litebird and in Node and compares every result: 36 subjects (ASCII, accented, CJK, characters
outside the BMP, lone halves in every arrangement, text that crosses the
256-unit marks) through every `String.prototype` method, the operators, the
URI and JSON functions and 51 regular expressions. That is 1,133 expressions
and 50,580 results. 95 differ, and each of the 95 is Unicode data the engine
does not have:

| Results | What is missing |
| ---: | :--- |
| 46 | collation: `localeCompare` folds case, then compares code units |
| 24 | the letter table behind `\p{L}` is a short list of ranges; fullwidth letters and most letters outside the BMP are not in it |
| 17 | normalization: `normalize()` returns its string unchanged |
| 8 | case mapping for `ǅ`, `İ` and fullwidth letters |

The engine before differs on 1,103. 720 of those are the two methods it did
not have. Of the other 383, 290 are fixed (order, halves that meet, splitting
and matching inside a pair, what a lookbehind captures, `%00`) and 93 are
among the 95 above. The other two are new: `localeCompare` of a character
outside the BMP with a lone half, which comparing bytes got right by accident
and comparing code units gets wrong. Code units agree with Node's collation on
ten more of these results than bytes do, so they stay.

It also takes the engine before about 7 minutes to get through the grid. This
one takes 3 seconds and Node half a second: the function that prints a result
reads it one character at a time, and some results are 90,000 characters.

**Tests.**

- `tests/js-conformance/long-strings.dat`: 34 scripts with Node's output, most
  of them the loops in the table at a size where the old cost is a timeout.
  The engine before passes 12 of them at best: 15 time out, 2 end the
  process, and one of the 12 (100,000 appends) takes 9.9 s of the 10 s limit,
  so on a busy machine it is 11.
- `js_string_matches_a_utf16_model_under_random_edits`: 60,000 random
  operations on a pool of strings, each compared with a `std::u16string` after
  every step and the whole pool every 50, then every pair of 585 short strings
  ordered. It ends by counting work (`JsString::stats()`): 20,000 appends may
  copy five times the final text and no more, a second pass over indexed text
  converts nothing, a tokenizer copies less than its input once.
- `js_strings_are_shared_and_read_in_constant_time` asserts the same counts
  through the interpreter, where a regression would actually come from: a
  builtin that calls `str()` on its receiver, an operator that rebuilds its
  operand.
- `js_strings_can_be_used_from_several_threads`, and `make tsan`, which builds
  the two stress tests alone under ThreadSanitizer and under AddressSanitizer
  with leak checking (the engine-wide `make asan` has leak checking off).
- How much these tests can see was measured by planting bugs in `JsString`
  one at a time. Of 47 (a wrong offset, a unit miscounted, a pair not joined,
  room claimed that is not free, a count not kept, a lone half not found, a
  copy where there should be none), the two stress tests catch 46. The other
  one changes no result and no counted work: it makes a small slice of
  non-ASCII text walk up to 256 bytes further. The first run caught 32 of 38,
  which is how the tests got their boundary characters, their search checks
  and their work counts. Of
  four data races planted the same way, ThreadSanitizer reports all four and
  the assertions alone catch two, which is why `make tsan` runs in CI.

**Not part of this.** The audit's first table had two rows that are the same
shape in other containers, and both are fixed in finding 33 (Phase 30).

## 26. 🟡 Regular expressions over long input: lookbehind fixed, repeated groups still limited

Found while testing finding 25. With the string no longer the cost, the
matcher's own limits showed.

**Fixed: lookbehind.** `(?<=x)` was implemented by trying its body forward
from every earlier position in the subject, so each lookbehind that failed
cost the length of everything before it. `/(?<=,)\w+/g` over 120,000
characters took 50.6 s where Node takes 37 ms, and `/(?<!,)c/g` over the same
text threw `RangeError: Regular expression too complex`. The body is now
matched right to left from the current position, which is how the language
defines it: the last term first, each character read from before the
position, and a lookahead inside it left to right again. The two expressions
take 40 ms and 9 ms.

That also changes results. A greedy group inside a lookbehind takes its text
from the right: `/(?<=(\d+)(\d+))$/.exec("1053")` captures `"1"` and `"053"`
in every engine, and captured `"105"` and `"3"` here. And in unicode mode a
match can no longer begin between the halves of a surrogate pair:
`/\uDE00/u.test("😀")` was true. Of 1,500 results from 75 lookbehind patterns
over 20 subjects, all agree with Node; the engine before agreed on 1,411.

**Open: a group repeated many times.** The matcher is a recursive
backtracker. Each pass through `(?:...)*` nests several native frames, and
none of them returns until the whole match is decided. It checks the stack
and gives up cleanly, but early:

| Expression | Repetitions before `RangeError` | Node |
| :--- | ---: | :--- |
| `/^(?:a\|b)*$/` | about 21,800 | 200,000 in 2 ms |
| `/^(ab)+$/` | about 12,000 | 200,000 in 2 ms |
| `/^(?:\w+\s)+$/` | about 12,700 | 200,000 in 1 ms |
| `/^(?:[^"\\]\|\\.)*$/`, the body of a string literal | about 12,700 | not measured |

A single character or class under `*` or `+` (`[^"]*`, `.*`, `\w+`) is a loop
and has no such limit; it is groups that recurse. The fix is to keep the
backtracking state on the heap instead of the native stack, which means
compiling the expression and running it on an explicit stack: a rewrite of
the matcher, not a patch.

**Open: the step limit.** A match is abandoned after 30 million steps with
the same `RangeError`. `/.*y/` on a line of 20,000 characters with no `y`
needs 200 million; it throws after 0.2 s here and answers `false` after 0.46 s
in Node. (Node is quadratic there too: 2 s at 40,000 characters, 39 s at
200,000.) The limit is deliberate, so that one expression cannot hang a page,
but it is lower than what a real engine will sit through.

---

## 27. ✅ What the deep pages found that was not depth

`deep_pages.py` (finding 24) was written to find recursion. Much of what it
found was something else: a page that is hostile by its size, or an old bug
that a generated page happened to walk into. Everything below was on `main`.

**Crashes**

| Input | Before | Now |
| :--- | :--- | :--- |
| `<div style="width: calc(1px">` | `SIGSEGV` | 1px: the `calc(` is closed where the value ends |
| `<template><th><a><button><select><button><tr a=0><button><a>`, 60 bytes | `SIGSEGV`: a read one element before the start of the stack of open elements | parses |
| `<svg width="1e9" height="1e9">` | abort, `std::length_error` | the element keeps its box and is blank |
| `<svg width="100000" height="100000">`, and the same on `<canvas>` | abort, `std::bad_alloc` | the same |
| 20,000 `<svg></svg>`, 180 KB of bitmap each | stopped at the 2 GB the deep pages allow | renders; past the budget they are blank |

- **`calc(`.** The evaluator read past the end of a value that never closed
  its parenthesis. A `calc()` inside a `calc()` also ended the expression
  early: `calc(10px + calc(20px * 2) + (5px + 5px))` was 10px and is 60px.
  Nesting is limited to 100 levels (`kMaxCalcDepth`; Blink's
  `kMaxExpressionDepth` is the same number), and a value nested deeper is
  invalid.
- **The 60 bytes.** No depth is involved. The page gets the parser into the
  "in select" insertion mode with no `<select>` on the stack of open
  elements; "pop until a `<select>` has been popped" then emptied the stack,
  and the adoption agency algorithm read from it. Popping until a given
  element now pops nothing when no such element is open, and the adoption
  agency checks what it indexes. Random pages at the nesting limit found it;
  it reduces to these 60 bytes.
- **Bitmaps.** Every `<svg>` and `<canvas>` was drawn into a bitmap of the
  size the element asked for. One bitmap is now at most 16,384 pixels a side
  and 16 million pixels, and all that are alive at one time 64 million
  (`include/litebird/render/RasterBudget.hpp`). Over that the element keeps
  its size in layout and draws nothing; `canvas.width` reads back what was
  set.

**Wrong results**

- **`<a>` inside `<a>`.** A second `<a>` start tag runs the adoption agency
  for the first and then takes the first out of the list of formatting
  elements. The handler took out whatever stood at the first one's old place
  in the list, which after the adoption agency could be another element.
  `<a><em><div><a>x</div>y` put the last `<a>`, with the "y" in it, outside
  the `<em>` it belongs inside, and `<a X>0<b>1<a Y>2` lost the `<b>` around
  its second `<a>`. The second is an html5lib case, the one gained
  (`tests1.dat`, 108 to 109 of 112). Both trees now equal the ones parse5
  builds.
- **`el.textContent = 'x'`** emptied the element's list of children and did
  nothing else. Each removed child went on naming `el` as its parent:
  `parentNode` was wrong, `isConnected` was wrong, appending `el` to one of
  them was refused as a cycle, and a custom element among them never got its
  `disconnectedCallback`.
- **`textContent`** included the text of comments:
  `<div>one<!--hidden-->two</div>` read "onehiddentwo".
- **Class names with a newline or a tab between them** missed every rule
  whose last compound selector has a class and no id (`.b`, `p.b`,
  `div .b`). The index that picks the candidate rules for an element split
  its class list on spaces only: the tests for the other separators were
  written `'\\t'`, `'\\n'` and so on, constants of two characters that no
  character equals. `class="a` newline `b"` was looked up as one name, so no
  rule for `.a` or for `.b` was a candidate. Markup formatted by a tool
  breaks long class lists this way. No deep page found this one: the
  compiler had been warning about the line on every build.

**Work that grew with the square of the input**

| Page | Before | Now |
| :--- | ---: | ---: |
| `color: var(--x, var(--x, ...`, 20,000 times, never closed | 1.8 s | 0.01 s |
| the same, 200,000 times | not finished after 120 s | 0.12 s |
| `innerHTML` assigned 100,000 elements side by side | 8.8 s | 0.2 s |
| that element emptied again with `innerHTML = ''` | 4.1 s | 0.02 s |
| a fragment with 50,000 children appended | 1.1 s | 0.12 s |
| one paragraph of 100,000 `<img>` | 35 s | 1.3 s |
| one paragraph, `a<br>` 100,000 times | 33 s | 1.3 s |
| one paragraph, `a <b>b</b> ` 100,000 times | 130 s | 3.6 s |
| 200,000 floats of no size, side by side in one box | 20.3 s | 1.2 s |

One cause each:

- `var(`: every unclosed `var(` scanned the rest of the value again. The
  value is now resolved in one pass (the limit of 12 references inside one
  another is unchanged).
- `innerHTML`: the parser moved its nodes from its root element to a
  fragment, and the fragment gave them to the caller, by taking them off the
  front of a list one at a time. Emptying an element did the same. Both hand
  over the whole list (`Node::take_children`).
- The paragraph: after breaking the lines, each text and image child searched
  every fragment of every line for its own pieces. One pass over the
  fragments now notes what each child has on each line.
- The floats: placing a float looks at the band of every float before it,
  and a float of no height left a band that no position can fall inside.
  Such a float still counts for `clear` and for the height of a box that
  contains its floats; it is no longer kept as a band.

The parser's walks down the stack of open elements and the cycle check of
`appendChild` belong here too; they are in finding 24.

**Behaviour that changed**, besides crashes that no longer happen:

- `textContent` leaves out comments, and `textContent = ''` leaves the
  element with no children (it had one empty text node).
- Children removed by `textContent`, `innerHTML` or a replaced fragment have
  no parent afterwards, custom elements among them get `disconnectedCallback`
  when the element was in the document, and a `MutationObserver` gets one
  record that lists them all.
- Appending a fragment that contains the node it is appended to throws
  `HierarchyRequestError` and leaves the fragment as it was.
- An unclosed `calc(` is a value, not a crash; nested `calc()` is evaluated.
- A `<canvas>` or `<svg>` larger than the bitmap budget is blank.
- Rules for a class apply when the class list is separated by newlines or
  tabs.

**Tests.** The pages are part of `deep_pages.py`. Unit tests:
`css_values_survive_hostile_nesting`, `canvas_and_svg_bitmaps_are_bounded`,
`replacing_children_detaches_the_old_ones`,
`inline_children_know_their_lines_in_a_paragraph_of_any_length`,
`floats_of_no_height_cost_nothing_and_still_count`; the 60 bytes and the
`<a>` case are in `html_parser_nesting_limit_holds_for_every_kind_of_element`,
and the class lists in `style_resolver_indexed_candidates_preserve_cascade`.
Six bugs planted in the float change, one at a time, are each caught by a
unit test. With the two layout changes, 1,746 pages (the examples and every
distinct html5lib input) give the same output as before in five modes, 8,730
runs, and so do 305 pages of random floats in three.

---

## 28. What the deep pages found that is still open

**Layout drops a block inside an inline box.** `<a href="#">x<p>y</p></a>`
paints "x" and not "y", at any depth, and did on `main`. The tree is right;
layout has no rule for a block-level box inside an inline one (CSS 2.1,
9.2.1.1: the inline box is broken around the block). The nesting limit makes
it easier to meet, because a block attached beside its parent can land inside
an inline element. The deep pages put their closing paragraph before an
unclosed inline nest for this reason.

**Content order at the nesting limit**, as described in finding 24.

**A tree a script built deeper than the stack reaches** is laid out only as
far as the stack goes (finding 24, point 3).

**Input that still costs the square of its length**, measured on this branch:

| Input | Time |
| :--- | ---: |
| 5,000 floats that have a size, in one box | 10 s |
| 10,000 | 46 s |
| 20,000 | not finished after 60 s |
| `while (el.firstChild) el.removeChild(el.firstChild)`, 50,000 children | 1.0 s |
| 300,000 spaces inside `:nth-child( )` | 8.8 s (fixed in Phase 31, finding 35) |
| 300,000 spaces inside an attribute selector | 6.3 s (fixed in Phase 31, finding 35) |
| one element with 50,000 attributes | 4.5 s |
| 300,000 spaces inside `repeat(`, `minmax(` or `translate(` | 2.1 s each (fixed in Phase 31, finding 35) |
| `at.innerHTML = "<div><div><form></form></div></div>"`, then the same on the inner `<div>`, 5,000 levels down (new in Phase 26) | 1.0 s |

The floats are the one that matters. A float that does not fit beside the
ones before it is moved down a quarter of its own height at a time, starting
each time from where the flow stands (the top of the box, when the box holds
only floats), and every step looks at every band. After 512 steps it is
placed where it stands, which is wrong as well as slow: with floats of 100 by
10 pixels in a box 800 wide, the first 1,024 fill 128 rows of eight, and
every float after them goes into one more row that runs out of the box to the
right (float 1,100 lands at x = 7,500). It needs a placement that goes
straight to the bottom edge of the float in the way. That can move a float
(today it lands on a multiple of a quarter of its height) and is a change of
its own.

Removing the first child moves every other child up, because children are a
vector. The spaces were trimmed one character at a time from the front of a
string, in about twenty places in the CSS code; Phase 31 replaced those loops
with bounded scans (finding 35). The tokenizer drops a
duplicate attribute by comparing each one with every attribute already on
the tag. Markup assigned to `innerHTML` that holds a `<form>` has to know
whether a form is above the element, which is a walk to the root at every
assignment (finding 30); markup without one does not make the walk.

**Memory of inline content.** Every box, line item and line fragment carries
its own copy of a 920-byte computed style (finding 12). One paragraph of
100,000 times `word <b>bold</b>` takes 1.3 GB, and 200,000 pass the 2 GB the
deep pages allow, so those two pages run at 100,000.

---

## 29. ✅ What the comparison with another parser found

The corpus is 1,792 inputs somebody wrote. The work on finding 24 needed to
know more than a corpus score: that a rewritten stack of open elements still
builds the same trees for pages nobody wrote. So
`tools/conformance/diff_trees.py` makes such pages (3 to 39 start tags, end
tags, comments and pieces of text in random order, from the elements the
tree builder has rules of its own for, which leaves most pages misnested)
and compares the tree Litebird builds with the one parse5 builds. parse5 is
an independent parser written from the same standard. On the corpus it gives
the expected tree for 1,764 of the 1,792 cases, and the other 28 all have a
`<select>` in them, whose parsing the standard changed and parse5 8.1 has
not, so the pages leave `<select>` out.

Of 6,000 pages (four seeds), 5,948 got the same tree and 52 did not. The 52
came down to five things. Phase 25 found them and left them alone; **Phase
26 fixed all five**, and the same 6,000 pages now get the tree the standard
asks for (finding 30 says what that is measured against).

| Reduced page | The standard, and parse5 | Litebird before |
| :--- | :--- | :--- |
| `<a><svg></a>` | the `<svg>` stays inside the `<a>`, and both are closed | the `<svg>` was moved out beside the `<a>` and got an `<a>` of its own |
| `<svg><title><em></title><tr>` | `</title>` is ignored: with an HTML element open it may only close an HTML `<title>`. The `<tr>` is ignored as well | the SVG `<title>` was closed, and the `<tr>` became an SVG element |
| `<math><mtext><i></math><mtext>` | `</math>` is ignored, the second `<mtext>` goes inside the `<i>` | the `<math>` was closed |
| `<button><div></button>x` | the button is closed, the "x" follows it | the "x" was inside the `<div>` |
| `<i><i><div><i><i></div>x` | two `<i>` are opened again around the "x" | one |
| `<table><em><table><div>`, then a newline | the newline opens the `<em>` again, inside the `<div>` | the newline was in the `<div>` with no `<em>` |
| `<h2><noscript></noscript><frameset>` | the `<frameset>` replaces the body | the `<frameset>` was ignored |

What each one was:

1. **The first three rows are one family**: an end tag reached an element of
   another namespace than the rules handling it expect. Two things were
   wrong. Every SVG and MathML element counted as "special", where the
   standard names nine (`foreignObject`, `desc`, `title`, `mi`, `mo`, `mn`,
   `ms`, `mtext`, `annotation-xml`), so an `<svg>` stopped the adoption
   agency algorithm like a `<div>` does. And the rules matched tag names
   without asking for the namespace, so `</title>` closed an SVG `<title>`.
   Now the special category goes by namespace and name, and every rule that
   names an HTML element asks `html_name()`, which is empty for an element
   of another namespace.
2. **`</button>`** belongs to the end tags that close their element whatever
   is still open inside it, with `</div>` and `</ul>`. It was missing from
   that list and fell through to "any other end tag", which gives up at the
   first special element. `<button><p>a</button>x` put the "x" into the
   paragraph inside the button.
3. **The fourth identical formatting element.** The list of active
   formatting elements keeps three of a kind, and a fourth pushes out the
   earliest of them ("Noah's Ark"). `push_formatting_element()` searched from
   the end and removed the latest.
4. **Text in a table.** Text directly in a table is collected first, to see
   whether it is only whitespace. That rule was applied whenever the
   insertion mode was "in table". It applies when the current node is a
   table, a row group, a row or a template; here the current node is the
   `<div>`, so the newline is ordinary text and the `<em>` is opened again
   around it.
5. **`<noscript>`** set the frameset-ok flag to "not ok". With scripting
   enabled the standard leaves the flag alone.

Three of the five were in the corpus all along, as one failing case each
among the 21: `<a><svg><tr><input></a>` in `adoption01.dat`,
`<button><p></button>x` in `tests20.dat`, and the first case of
`tests23.dat`, which is the `<i>` row with `<font>`. A failing case says a
bug exists. The random pages say how often a page meets it, and `--reduce`
turns it into a few tags to read the standard against.

**How to see them.** `python3 tools/conformance/diff_trees.py --binary OLD
--reduce` with a binary built from before Phase 26, and `--seed 1`, `2` or
`3` for the other pages. It needs Node.js, and the first time npm and the
network to install parse5 into `build/parse5`.

---

## 30. ✅ What the wider comparison found

The pages of finding 29 were tags, text and comments. With those five bugs
fixed, the pages were given the rest of what a tree builder reads
(`--extras`: a start tag that closes itself, `type=hidden` and the other
attributes that change a rule, `</br>` and `</p>`, CDATA, U+0000, a doctype
out of place). They were also parsed as fragments, which is what `innerHTML`
does, in 45 kinds of element (`--fragments`), and with scripting disabled
(`--no-scripting`).

| 1,500 pages of each kind | Trees the standard asks for, before | Now |
| :--- | ---: | ---: |
| Tags, text and comments (finding 29) | 1,489 | 1,500 |
| With the extras | 1,371 | 1,500 |
| With the extras, as fragments | 1,371 | 1,500 |
| With the extras, as fragments, scripting disabled | 1,361 | 1,500 |

So 408 of 6,000 pages were wrong, where the plain pages alone had said 11 of
1,500. Most of the difference is the first four rows of the next table,
which are one bug.

### What was wrong

| Page | The standard | Litebird before |
| :--- | :--- | :--- |
| `<p>a<br/>b</p>c` | "a", the break and "b" in the paragraph, "c" after it | the "b" was outside the paragraph, followed by an empty `<p>` |
| `<head><meta charset="utf-8"/><link rel="x"/><title>t</title></head><body>b` | a head with three elements, and a body | the head ended after the `<meta>`, and the `<link>`, the `<title>` and the `<body>` were outside `<html>` |
| `<table><col/><tr><td>x` | a column and a row with a cell | the row and the cell were lost |
| `<ul><li><input type="text" /> label</li></ul>` | the label in the list item | the label after the list item |
| `<nobr><table><marquee></table><nobr>x` | the second `<nobr>` beside the first | inside the first |
| `<template><table><form></table></template>` | the form is kept: there is no form pointer inside a template | dropped |
| `<head><template></template></head><p><frameset>` | a frameset page: the body made for the `<p>` sets the frameset-ok flag back to "ok" | the `<frameset>` was ignored |
| `<head><head>`, then a newline | the second `<head>` is ignored and the newline is in the head | the second `<head>` closed the head |
| `<table>`, a space, U+0000, a space, `<tr>` | U+0000 is dropped and the spaces stay in the table | the spaces were moved out in front of the table |
| `<span><svg><wbr></span>x` | the "x" after the `<span>`: an SVG `<wbr>` is an ordinary element | the "x" inside the SVG `<wbr>` |
| `<table><svg><template><desc><tr>` | the row goes into the table | into the SVG `<template>` |
| `&#X` | the text `&#X` | `&#x` |
| `<!--x--` at the end of the page | a comment "x" | a comment "x--" |
| `<a =>` | an `<a>` with an attribute named `=` | no attribute |
| `</script>` in a fragment for a `<script>`, `</textarea>` in one for a `<textarea>` | text: no start tag was seen, so no end tag ends the text | the end of the text; what followed was parsed as markup |
| `<![CDATA[x]]>` at the top of a fragment for an `<svg>` | the text "x" | a comment |
| Any fragment for a `<template>` | parsed in the "in template" mode | the process ended with `SIGSEGV` |

**A void element written with a slash closed its parent.** `<br/>`,
`<img ... />` and `<meta ... />` are how XHTML wrote an element that has no
end tag, and a large part of the web still writes them so. In HTML the slash
means nothing. The tree builder "acknowledged the self-closing flag" by
popping the current node, but a void element had been popped already, so the
element around it went: the paragraph, the list item, the head. Of the 408
wrong pages, 292 come right when this one line is changed in the old tree
builder and nothing else is. The corpus has no case for it, because no case
there writes a void element with a slash: the same change leaves the old
score at 1,771 of 1,792. `examples/nextgen_showcase.html` in
this repository has an `<input ... />` and was parsed wrongly. And Litebird's
own `innerHTML` writes `<br />`, so markup read from one element and
assigned to another came out different from what went in.

**A fragment for a `<template>`** read the last entry of an empty list of
template insertion modes. This could be reached from the command line
(`--fragment-context=template`) and through
`HtmlParser::parse_fragment_with_context`, not from a page, for the reason
in the next paragraph.

**`innerHTML` did not use any of it.** The fragment parser takes the element
the markup is for, and the corpus runner passes one, which is how the 192
fragment cases of the corpus are scored. `Element::set_inner_html`, which is
what a script's `element.innerHTML = ...` calls, passed `<body>` for every
element. So the cases passed and a page still got:

| Script | Before | Now |
| :--- | :--- | :--- |
| `table.innerHTML = "<tr><td>1</td><td>2</td></tr>"` | the text "12" in the table, no row | a `<tbody>` with a row of two cells |
| `tr.innerHTML = "<td>x<th>y"` | the text "xy" | two cells |
| `svg.innerHTML = '<circle r="4"/><g>...</g>'` | HTML elements, and the `<g>` inside the `<circle>` | SVG elements side by side |
| `style.innerHTML = "a > b { } /* <b> */"` | two nodes, the `<b>` an element | one text node |
| `div.innerHTML = "<form>..."` with the `<div>` in a form | a form inside a form | the inner `<form>` is ignored |

It now parses for the element it is called on (`HtmlParser::parse_inner_html`):
its name and namespace, the encoding of a MathML `annotation-xml`, and
whether it stands inside a form.

That last one cost something the first time. Whether a form is above the
element is a walk to the root, and made at every assignment it turned a
script that nests 30,000 levels through `innerHTML` from 0.2 s into 44 s.
`make deep` has that page and it still passed, at 48 of the 60 seconds a run
may take; the line that names the slowest run is what showed it. The walk is
now made only when the markup has the letters "form" in it, since only a
`<form>` or `</form>` asks, and a unit test bounds the loop.

The rest are smaller. An SVG element that has the name of an HTML one (`rt`,
`template`, `td`, `option`, `wbr`) took the HTML element's rules: implied end
tags, the place a table's rows go, select scope. A second `<nobr>` behind a
marker went through an adoption agency algorithm that returned before doing
what "any other end tag" does. The doctype was printed before the comments
that precede it, which is a fault of the tree dump and not of the tree. An
empty file exited with status 1 and no tree.

### A reference that is itself behind

After these fixes 49 of the 6,000 pages still get another tree than parse5
builds. Each of them is one of nine places where parse5 8.1 does not do what
the standard says:

| Rule | The standard | parse5 8.1 |
| :--- | :--- | :--- |
| A `<form>` in the contents of a `<template>` | kept; no form pointer there | ignored in a table, and never nested (the rule until September 2026) |
| The body made for the first thing after the head | sets frameset-ok to "ok" | leaves it |
| CDATA where the current node is an SVG `<title>`, `<desc>`, `<foreignObject>` or a MathML `<mi>` and the like | text | a comment |
| `<search>` | a special element | an ordinary one |
| Table scope | ends at a `<template>` too | at `<table>` and `<html>` |
| `</tbody>`, `</thead>`, `</tfoot>` in a row | ignored unless that element is open | closes the row if that element or a `<tr>` is open |
| Whitespace directly in a `<template>` that is in a table mode | collected like text in a table | ordinary text |
| A run of U+0000 in SVG or MathML | one U+FFFD each | one U+FFFD for the run |
| A fragment for a `<noscript>`, scripting disabled | markup | text |

A ceiling of "49 known differences" would have hidden a new bug behind an
old excuse, so `diff_trees.py --corrected` compares with a copy of parse5 in
which these nine rules are changed to the standard's (13 edits of a few
lines, listed in the script and applied to a copy in `build/parse5/corrected`
when it runs). Against that copy all 6,000 trees are equal, and `make
diff-trees` fails if one is not.

What vouches for the corrections, since a reference corrected by the party
being checked is worth less than one that was not:

- For the first four rows the web-platform-tests copy of the corpus has
  cases, 15 of them. parse5 as published fails them and the corrected copy
  passes them; on the 1,792 cases vendored here the two give the same trees.
- For table scope and for `</tbody>` in a row, Blink's source does what
  the standard says. For U+0000 in SVG and for the `<noscript>` fragment
  there is the standard's text and no second witness.
- For the whitespace in a template, Blink does what parse5 does, so this is
  the one place where Litebird follows the text against two
  implementations. A unit test pins it, and the difference is a text node's
  position inside a template.
- A wider run: 180,000 pages from five lists of elements (the default one,
  every special element, tables, formatting elements, SVG and MathML), with
  and without the extras, as documents and as fragments, with scripting on
  and off, three seeds. Against parse5 as published 1,548 of them differ;
  against the corrected copy none does.

### What the comparison does not reach

- **`<select>`.** The standard changed how its content is parsed, parse5 has
  the older rules and so does Litebird: the 11 corpus cases that still fail.
  Two parsers that are both behind cannot check each other.
- **The corpus has moved.** html5lib-tests handed its tree-construction
  tests to web-platform-tests in June 2026, and the vendored copy is the
  last one from before. The web-platform-tests copy has 1,973 cases;
  Litebird passes 1,868 of them (1,851 before Phase 26). Of the other 105,
  88 need processing instructions (`<?x?>` is a node of its own in the
  standard now and a comment here), 6 need a script to run while the page
  is being parsed, and 11 are the `<select>` cases. Vendoring that copy is
  the next step for the scoreboard; the runner does not read its
  `#description` sections yet.
- **Reading `innerHTML`.** The markup Litebird writes does not escape `<`
  and `&` in text, and writes `<br />` where the standard writes `<br>`. So
  text that holds `<b>` reads back as an element. Parsing is this finding;
  writing is not fixed.

---

## 31. ✅ The DOM a script sees

The language had a scoreboard since Phase 23 and passed 840 of 845 cases.
The DOM had none, and Known gap 9 listed what was missing from memory. This
finding is what a scoreboard said: `tests/dom-conformance/`, 572 pages with
a script each and what the script prints in jsdom 31, an independent
implementation (ARCHITECTURE.md, Phase 27, section 11).

| | `main` before | Now |
| :--- | ---: | ---: |
| All 572 cases | 61 | 572 |
| `everyday.dat`: 68 snippets from *You Might Not Need jQuery*, written first and never edited | 13 | 68 |
| URL parsing and setters, the web-platform-tests data (1,188 cases) | 2 | 1,188 |

### What a script met

Each line is a script on a small page, run by `litebird --run-script`.

| Script | Before | Now |
| :--- | :--- | :--- |
| `document.querySelectorAll('div.card p').length` | `0`; `querySelector('p:nth-child(2)')` was `null` and the next line threw | `2` |
| `a.classList.add('on')` | `TypeError`: `classList` is undefined | the class is added |
| `p.innerHTML` for `<p>a &lt; b &amp; c</p>` | `a < b & c`: assigned back, it is other markup | `a &lt; b &amp; c` |
| `u.children`, `u.firstElementChild` | undefined | a live `HTMLCollection`; the element |
| `a.dataset.role = 'admin'`, `a.matches(...)`, `a.closest(...)` | `TypeError` | work |
| `new CustomEvent('x', {detail: 1})`, `e instanceof MouseEvent` | `ReferenceError` | work |
| `form.elements`, `new FormData(form)`, `input.validity` | undefined | work |
| `getComputedStyle(a)`, `a.style.length` | `ReferenceError` | work; `style` is a live declaration block |
| `document.querySelector('div[')` | no error, no result | `SyntaxError`, a `DOMException` |
| `document.body.appendChild(document)` | no error | `HierarchyRequestError` |
| `Object.getPrototypeOf(document.body) === HTMLBodyElement.prototype` | `ReferenceError`: there were no interfaces | `true` |
| `a.append('x')`, `a.remove()` | `TypeError`: not a function | work |
| `location.href` on a page loaded by the CLI | `ReferenceError` | the page's address |
| `template.content.cloneNode(true)` | `TypeError`: `content` is undefined | works |

The cause was one design decision: a node was a kind of value, and its
members were names compared in one function. Phase 27 replaced that; the
old code is gone, not extended.

### What the work found on the way

- **Addresses were cut out of strings.** On the page
  `https://example.test/app/page.html?tab=2#top`: `a.href = '?q=1'` gave
  `.../app/?q=1` (the file name lost), `'#x'` gave `.../app/#x`,
  `'//cdn.test/lib.js'` stayed on `example.test`, `a.protocol` of
  `mailto:ann@x.test` was `https:`, and `new URL('nope')` did not throw. A
  click on `<a href="#frag">` therefore looked like a move to another page
  and did nothing. There is one parser now, the URL Standard's
  (`src/net/UrlParser.cpp`).
- **Scripts ran after the whole page had been parsed.** The first script of
  a page saw all of it: `document.scripts.length` was the number of scripts
  on the page, so "the last script is me"
  (`scripts[scripts.length - 1]`), which embedded widgets use to find their
  own tag, found the wrong one. Scripts run where the parser stands now.
- **The shell ran every `<script>` whatever its type** (JSON data, a
  template, a module), four times over in four copies of the page-load
  code, and its pages had no address of their own to resolve a link
  against.
- **Five unit tests asserted what no browser does** and passed: a method
  taken off its object and called (`var f = el.focus; f()`),
  `dispatchEvent('click')` with a string, `focus()` on a detached or
  disabled control, `popstate` fired inside `history.back()` instead of
  after it, `observedAttributes` as a method. They were corrected, not
  kept.
- **jsdom is wrong in five places**, by the standards' text: `tabIndex` for
  a `tabindex` that is not a number, `formnovalidate`, `requestSubmit()` on
  a form outside a document, content inside `<select>`, and the order of a
  promise callback and a mutation observer's. The corpus does not record
  those answers, and a unit test asserts the standard's
  (`tests/dom-conformance/README.md`).

### What 557 of 557 hid

The corpus first had 557 cases, and Litebird passed all of them while two
things a page meets early were wrong. Nothing in the corpus asked, because
the cases were written by the hand that wrote the code:

| | At 557 of 557 | Now |
| :--- | :--- | :--- |
| `getComputedStyle(div).position`, `.opacity`, `.marginTop`, `.fontSize`, any property nobody declared | `""` | `static`, `1`, `0px`, `16px`: the inherited or initial value, from a table of 182 |
| `#p { color: red } p { color: blue }` on `<p id=p>` | blue: the last rule that matched | red: specificity, then position |
| `getComputedStyle(h1).fontSize`, `(b).fontWeight`, `(a).cursor` | `""` | `32px`, `700`, `pointer`: what HTML gives each element |
| `color: red`, `margin: 1em`, `width: calc(1px + 2em)` read back | `red`, `1em`, the text | `rgb(255, 0, 0)`, `16px`, `33px` |
| A range from 1 to 3 in a parent whose first child is removed | Still 1 to 3: one child too far | 0 to 2 |
| `surroundContents()` on `bcd` of the text `abcdef` | `aef<em>bcd</em>` | `a<em>bcd</em>ef` |
| A `NodeIterator` whose reference node is removed | Went on inside the removed subtree | Steps out of it |

They were found by running pages that were not in the corpus through jsdom
and Litebird side by side, which is now a tool:
`python3 tools/conformance/compare_with_jsdom.py page.html script.js`. Of
166 properties of a `<div>` nobody styled, three had an answer (`display`,
`visibility`, `pointer-events`); 151 agree with jsdom now. Of the 15
others, 12 are places where Litebird writes what browsers write and jsdom
does not (`border` is `0px none rgb(0, 0, 0)`), two are the default font,
where each engine has its own, and for `transform-origin` Litebird has no
answer. Fifteen cases were added (`style.dat`, `ranges.dat`), which is how
557 became 572.

The lesson is about the method. A corpus recorded from another
implementation cannot be bent, and it still only knows the questions its
author thought of. The comparison tool asks without knowing the answer,
and the next person to work on the DOM should start there, not at the
score.

### What is known to be wrong and is not fixed

The same comparison, run over forms, ranges, inline style and a few more
areas, found these. Each gives an answer, and the answer is wrong:

- **`DOMParser` with an XML type parses HTML.** `parseFromString('<a><b/>
  </a>', 'application/xml').documentElement.nodeName` is `HTML`. There is
  no XML parser. A script that reads a feed or an SVG file this way gets a
  tree, and not the one it asked for. (Fixed since: finding 34.)
- **Date and time inputs are text inputs.** `<input type="date"
  value="2026-02-30">` keeps that value; `valueAsDate` is null and setting
  it throws; `stepUp()` throws.
- **`el.style.flex = '1'` leaves `el.style.flexGrow` empty.** Only margin,
  padding and the border family are taken apart in `el.style` (the
  computed style takes apart ten more). `url(a.png)` is not rewritten as
  `url("a.png")`, nor `translate( 10px , 20px )` as `translate(10px,
  20px)`.
- **SVG elements have no SVG DOM**: `className` is a string, no `viewBox`.

`tests/dom-conformance/open/` has a page and a script for each, to run
through the tool.

### What the number does not say

572 of 572 is everything the corpus asks, and the corpus asks what jsdom
can answer. It has no layout, so no case reads a size or a position, and
in the CLI those are zero while scripts run. It loads one page, so no case
navigates. Nothing external is fetched. Style sheets are not objects
(`sheet.cssRules`). The list of what is known to be absent is in
ARCHITECTURE.md, Phase 27, section 12; it was made by asking for about
170 names and by the comparisons above, not by reading pages, so it is not
complete.

The bindings are 13,700 new lines that take raw pointers into a tree a
script can take apart while they run. `make asan` runs the whole corpus in
the sanitizer build for that reason: 572 of 572 there too, the 110 unit
suites, the URL data, both fuzzers and the deep pages, with no report from
either sanitizer. That is evidence about the paths the cases take. A page
that takes another path has not been run.

---

## 32. ✅ Tables could not span, and had no border model

**The hole.** Phase 16's table algorithm placed one cell per column of the
row it stood in. `colspan` and `rowspan` were parsed into the DOM and read
by nothing, so a header cell meant to cover two columns covered the first
and left the second empty; `rowspan="0"` was an ordinary rowspan of one; a
spanning cell demanded a column wide enough for itself instead of spreading
its requirement; and `border-collapse` / `border-spacing` were unknown
properties, so every cell painted its own full border and adjacent borders
doubled. Two latent faults surfaced with it: table cells and floats were
laid out by calling `layout_block()` directly, and only `layout()` copies
margin/padding/border from the computed style, so a cell with a border lost
it and a float with padding measured as if it had none; and a percentage
width inside a cell resolved against the column it happened to receive
(25% of 75px is 18.75px) instead of the table it belonged to.

**The fix.** Cells are placed into an occupancy grid before sizing, so a
spanning cell reserves its slots and the following cells skip them. A
spanning cell distributes its width requirement over the columns it covers.
Row heights come from single-row cells first; a rowspan cell that needs
more room grows the last row it covers and is itself stretched to fill it.
`border-collapse: collapse` makes each cell paint half of a shared border
(never by mutating the style, so relayout cannot halve twice) and forces
`border-spacing` to zero; `border-spacing` insets and separates cells.
Percentage lengths inside a cell resolve against the table width
(`width_basis_override`). The edge copy lives in `LayoutBox::apply_style_edges()`,
shared by `layout()` and the float path. The full account is in
ARCHITECTURE.md, Phase 29.

**What it still does wrong, on purpose and documented.** Collapsed borders
are halved rather than resolved per edge by CSS 2.1's "widest wins" rule; the
table's outer border is not merged with edge cells; `empty-cells`,
`caption-side` and `table-layout: fixed` are unimplemented; a rowspan's
extra height lands on the last covered row; `rowspan="0"` spans to the end
of the table rather than the row group.

**How verified.** `make test` prints `All 112 engine test suites passed
successfully!` — two of them new (`table_colspan_and_rowspan_geometry`,
`table_border_collapse_and_spacing`). `make conformance` is unchanged at
1781 of 1792: the parser was not touched. The work landed originally as PR
#16 (8 November) on a tree that was subsequently rebuilt through Phases
23–27, and was re-landed by hand for that reason.

---

## 33. ✅ Property deletion and array front edits were quadratic

**The hole.** A large property map rebuilt its entire hash index on every
`delete`: an object with 50,000 keys performed 50,000 linear-index erases,
then 50,000 rebuilds that hashed every remaining key, so deleting the last
key was an operation on all the keys before it. Small property maps avoided
the hash cost by staying linear, but the rebuild ran for the large ones.
Arrays were plain `std::vector<JsValue>` instances, so `shift()` and two-at-a-time
front edits copied (and destroyed) the whole tail each call.

**The fix.** Property entries are now an insertion-ordered `std::list<Entry>`,
and the hash index points into that list instead of into offsets. One index is
built the first time a map grows past twelve entries and is maintained on
insertion and removal; `erase()` removes the list node and the hash entry in
O(1), and no iterator to any surviving property changes. Small maps still use
the linear scan exactly as before, so the common case does not pay for a
hash-map lookup it cannot use. A map that falls
back under half the threshold drops its index, so the retired hash table is
not left behind while the object lives. Array elements
are now `std::deque<JsValue>`, making `shift()` and `unshift()` operations on
the front of the deque; code that indexes, copies or otherwise round-trips an
array re-reads bounded ranges and is unchanged. The full account is in
ARCHITECTURE.md, Phase 30.

**Measured.** The Phase 23 scripts, run on the same machine before and after:

| Script | Before | After |
| :--- | ---: | ---: |
| `delete` of the 50,000 keys of one object | 131,423 ms | 25–29 ms |
| `shift()` until a 50,000-element array is empty | 8,228 ms | 9–10 ms |

`tools/bench/bench_delete_shift.py` keeps the two cases and the adjacent
`unshift()`, front-`splice()` and enumeration-after-delete paths, compares
against Node by default, and exits nonzero if a case times out, errors or
prints a different result. One run printed:

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

**What is still approximate.** Property maps below twelve entries have not
changed their lookup algorithm; taking the middle out of an array with
`splice()` is still linear in the number of elements that move; and a large
map keeps its bucket array while it stays above half the threshold, releasing
it only when it drops lower or the object is destroyed. `delete a[i]` still
leaves holes in an array, as it did before.

**How verified.** `make test` prints `All 113 engine test suites passed
successfully!`. The new suite
`js_property_maps_and_arrays_keep_their_order_through_front_edits` covers
insertion order after `delete` and reassignment, enumerated order after a
2,000-property workload, identity through `shift()`/`unshift()`, the state of
`splice(0, n)`, and a full array consumed one element at a time.
`make js-conformance` is 840 of 845 (everyday probe 90 of 90; 839 of 845 in
the ASan build, which cannot fit the deep-nesting case). `make dom-conformance`
is 572 of 572, `make url-conformance` is 1,188 of 1,188 and `make conformance`
is 1781 of 1792, unchanged; the parser was not touched. `make fuzz` reports
0 findings in 400 parser mutations and 400 layout/render and image cases;
`make deep` reports 0 failures in 1,292 runs over 445 pages; and `make asan`
reports no sanitizer finding in the 113 suites, the corpora, the fuzzers and
the ASan deep run (842 executions). The bench command above passed against
Node 22 on the same machine.


## 34. ✅ `DOMParser` gave an HTML document for XML

**The hole.** There was no XML parser. `new DOMParser().parseFromString(text,
'application/xml')` ran the HTML parser whatever type it was given, so a
script that read a feed, a configuration file or an SVG image got a tree, and
not the one in the text: the root was `HTML`, a CDATA section was a comment,
names were upper case, and text that was not well formed gave a tree like any
other. `XMLHttpRequest` was a stub that failed every request, so
`responseXML` was null on every path. Finding 31 listed the first as known
and not fixed.

| A script on a page | Before | Now |
| :--- | :--- | :--- |
| `d = parseFromString('<a xmlns="urn:x"><b id="k">t</b><![CDATA[<c>]]></a>', 'application/xml')`; `d.documentElement.nodeName`, `.namespaceURI`, `d.contentType` | `HTML`, `http://www.w3.org/1999/xhtml`, `text/html` | `a`, `urn:x`, `application/xml` |
| `new XMLSerializer().serializeToString(d)` | `<html xmlns="http://www.w3.org/1999/xhtml"><head /><body><a xmlns="urn:x"><b id="k">t</b><!--[CDATA[<c-->]]&gt;</a></body></html>` | The text that went in |
| `parseFromString('<a><b></a>', 'application/xml').documentElement.nodeName` | `HTML` | `parsererror` |
| `parseFromString('<svg xmlns="http://www.w3.org/2000/svg"><path d="M0 0"/></svg>', 'image/svg+xml')`: root, its first child, the type | `HTML`, `HEAD`, `text/html` | `svg`, `path`, `image/svg+xml` |
| `x.open('GET', 'data:text/xml,<a><b>t</b></a>'); x.send()` | `error`, `loadend`; status 0; `responseXML` null | `loadstart`, `load`, `loadend`; status 200; `responseXML.querySelector('b').textContent` is `t` |
| `el.getAttribute('xlink:href')` and `el.getAttributeNS('http://www.w3.org/1999/xlink', 'href')` on a `<use xlink:href="#a">` in a page | `null`, `null` | `#a`, `#a` |
| `for (i = 0; i < 50000; i++) el.setAttribute('a' + i, i)` | 6.3 s: every call searched the list | 49 ms |

**The fix.** An XML 1.0 (Fifth Edition) parser with namespaces
(`src/xml/XmlParser.cpp`), which does not validate and reads nothing outside
the text it is given; documents that know whether they are HTML; namespaces
on elements and attributes, and an index for an attribute list that is long;
CDATA sections and processing instructions as nodes; the XML serializer of
*DOM Parsing and Serialization* for `XMLSerializer` and for `innerHTML` in an
XML document; and `XMLHttpRequest` for `data:` URLs. ARCHITECTURE.md, Phase
28, has the design.

### What each reference found

The parser was written against the W3C's suite, and passed it, before any of
the following was known. Each was found by a different instrument, and none
of them by reading the code.

| Found by | What |
| :--- | :--- |
| Pages that are large, not wrong (`tools/fuzz/deep_pages.py`) | An element with 50,000 attributes took 7.1 s to parse and 4 s to serialize: the DOM searched its attribute list for every attribute it added, as it does on `main`. 50,000 namespace declarations with a child each took over two minutes: the scope was copied a level and searched a name. Removing every second of 50,000 attributes took 88 s with the first index written for the list, which held positions. All but the last are 0.2 s or less now; removal from the front was still the square then (fixed in Phase 35, finding 40) |
| The same question asked of the DOCTYPE | 50,000 attributes declared for one element took 4.5 s. And one attribute default of a megabyte with 100,000 empty tags made 100 GB of attribute values from a 1.4 MB document: entity expansion was bounded and defaults were not. Both bounded now, and both are deep pages |
| Expat and libxml2 on generated documents (`tools/fuzz/diff_xml.py`) | No fault of Litebird's in 64,000 documents, and one in the references: three runs of 20,000 reported 10 differences, and all ten were `version="1."`, which Expat and libxml2 accept and the grammar does not (`'1.' [0-9]+`). A document whose version is outside the grammar is now judged by the grammar |
| web-platform-tests, on their first run | Three faults that were not about XML: attributes of inline `<svg>` had no namespace (the row with `xlink:href` above), a doctype a script inserted was not `document.doctype`, and `lookupNamespaceURI('xml')` answered for a node that has no namespaces. 607 jsdom-recorded cases had passed over all three |
| Writing a tree and reading it back (`tools/fuzz/xml_roundtrip.py`) | The standard's serializer, followed to the letter, writes documents that read back as another tree: 265 of 3,000 trees with its "generate a prefix", 498 with a prefix kept for a namespace a nearer element took from it. Four issues are open against the standard for these; Litebird leaves the text there and says where (`tests/wpt/README.md`) |
| A page of 200,000 `x` in UTF-16, written for the request code | A response of type `text/xml;charset=utf-16le` with no byte order mark gave no document: the charset of the transport was checked against what the first bytes looked like, as if it were the document's own declaration |
| CI's sanitizer job, on the first commit it ran | A use-after-free in `XMLHttpRequest`: the charset of the response's type was read through a pointer into a `MimeType` that was a temporary, one statement after it was gone. Every request for a type with a charset did it, and the normal build passed all 124 suites and all 629 cases: the bytes were still there. `MimeType::parameter()` cannot be called on a temporary any more |
| The sanitizer build of the unit tests, on a machine with 4 GB | It was killed at 3.8 GB. Nothing in the engine collects unasked, AddressSanitizer keeps what is freed, and 125 suites add up in one process. After each suite the sanitizer build now collects and returns the allocator's pages: 2.6 GB at the highest |
| Mutated documents (`tools/fuzz/fuzz_xml.py`, written after the use-after-free) | Nothing in the parser: 2,400 mutants and 1,927 whole documents of the W3C suite in the sanitizer build, no report. It did report two things the standard's serializer does on purpose, which are now written down: a declaration of the `xml` prefix is not written, and `innerHTML` writes a child without its surroundings, so `el.innerHTML = el.innerHTML` can move a child that is in no namespace into the default namespace of `el`. jsdom does both |

### What is known to be wrong and is not fixed

- **`fetch()` reads files of this machine for any page.** Found while
  deciding what `XMLHttpRequest` may load. The loader behind `fetch()`
  (`ResourceLoader::load`) takes whatever is not `data:` or `http://` for a
  path: a page at `https://example.test/` that calls
  `fetch('/home/user/notes.txt')` gets the file. Nothing checks an origin.
  It is older than this phase and was not touched; `XMLHttpRequest` was kept
  off that loader, answers `data:` URLs only, and a unit test asserts that a
  request reads no file. This is the first row of the task table now.
- **`XMLHttpRequest` sends nothing over a network.** `http:`, `https:` and
  relative addresses end in `error`.
- **Four encodings.** UTF-8, UTF-16 in both orders, windows-1252. A response
  labelled `shift_jis` is read as if it had no label; an XML document that
  declares `EUC-JP` is not read; a page is read as UTF-8 whatever its
  `<meta charset>` says.
- **A page cannot be XML.** The command line parses every file as HTML, and
  there are no frames with documents: 495 web-platform-tests subtests need
  one or the other and fail.
- **In the JavaScript engine, a function cannot have a function as its
  prototype**: `Object.getPrototypeOf(TypeError) !== Error`. 18 subtests.
  (Fixed in Phase 34, finding 41.)
- **An `Attr` is not a `Node`**, two attributes of one element cannot share
  a qualified name in two namespaces, and collections do not list their
  names as own properties: 33 subtests.
- **Removing all 50,000 attributes of an element from the front took 9 s**
  at Phase 28; from the back, 22 ms of 25,000. Fixed in Phase 35 (finding 40).
- **The HTML tokenizer searched for a repeated attribute name** by comparing
  it with every accepted one: one element with 50,000 attributes took 2.9 s in
  the HTML parser (10.4 s before the attribute list had an index). Fixed in
  Phase 39 (finding 46): 0.09 s whole-process.

### What the numbers do not say

**1702 of 1702** is every document of the W3C suite that applies to a parser
that does not validate, reads no external entity and follows the Fifth
Edition. 883 documents are not counted, for six reasons
(`tests/xml/README.md`), and all 2,585 are run so that none can crash it.

**4640 of 5192** web-platform-tests subtests: of the 552 that fail, 495 were
never really asked, because the document they are about is loaded into a
frame. They are listed as failing, not as skipped.

**629 of 629** is what jsdom answers, and for `XMLHttpRequest` jsdom is wrong
in ten places that were found (`tests/dom-conformance/README.md`); the
corpus stays away from them. Places that were not found are in the corpus as
jsdom's answer.

**The parser reads hostile text in the process it runs in**, as everything
here does. What bounds it is listed in ARCHITECTURE.md, Phase 28, section 3,
and each bound was added after a page was found that needed it; the list is
of what was thought of. The mutation fuzzer starts from the W3C's documents
and is not guided by coverage.

**How verified.** Every line is a command and what it printed on the
merged tree, in the sandbox.

- `make xml-conformance`: `XML conformance (W3C suite): 1702/1702 (100.0%)
  [261 canonical forms compared, 2585 documents run, 0 crashes, 0
  timeouts]`. With `--reference`: Expat 1404 and libxml2 1697 of the 1718
  they can be asked.
- `make diff-xml`: `4000 documents (seed 20261114, half of them damaged),
  Litebird accepts 2245; 3958 agree with both, 0 DIFFER, 13 where Expat and
  libxml2 disagree (Litebird is with Expat on 12, with libxml2 on 1, with
  neither on 0); 29 with a version number outside the grammar, all refused`,
  and `XML round trip: 3000 trees, 3000 come back the same from Litebird and
  from Expat, 0 do not`.
- `make wpt`: `Web Platform Tests: 4640/5192 subtests pass in 76 files; 552
  fail as listed, 0 fail and are not listed`. The binary of `main` passes
  1,127 of the 2,257 subtests it gets as far as reporting.
- `make dom-conformance`: `629/629` in 23 files, and the same from jsdom
  with `--reference`. The binary of `main` passes 572: none of the 57 new
  cases.
- `make test`: `All 129 engine test suites passed successfully!`, twelve of
  them new.
- `make conformance` 1781 of 1792, `make js-conformance` 840 of 845,
  `make url-conformance` 1150 of 1188, `make diff-trees` 0 of 6,000 trees
  differ: all as on `main`, with the HTML parser now storing foreign
  attributes under their names and namespaces.
- `make deep`: `1332 runs over 465 pages at depth 200000 on a 8 MB stack, 0
  failed; slowest 22.8 s (table [dumps])`; 20 of the pages are XML.
- `make asan`: 129 of 129 suites; JavaScript 839 of 845 as before; DOM 629
  of 629; URL 1150 of 1188; XML 1702 of 1702; web-platform-tests 4640 of
  5192; 1,000 generated documents and 1,000 round trips; `fuzz: 400
  mutations, 0 finding(s)`, `pipeline fuzz: 400 runs … 0 finding(s)`, `fuzz
  xml: 600 mutations of 3074 documents (3061 of the W3C suite), 38 of them
  well formed, 0 finding(s)` and `1927 documents as they are, 1551 of them
  documents to this parser: each the same tree after it is written and
  read`; `882 runs over 315 pages at depth 10000 on a 2 MB stack, 0
  failed`. No report from either sanitizer. It takes 23 minutes here.

---

## 35. ✅ CSS whitespace trimming was quadratic

**The hole.** The CSS parser and resolver trimmed leading whitespace with
`erase(begin())` in a loop. Each space shifted the entire remaining value.
Finding 28 measured 300,000 spaces inside `:nth-child( )` at 8.8 s; the
reproducible page-pipeline baseline for this phase measured 14.6 s on its
machine. The same shape appeared in attribute selectors, grid track
arguments, filters, shorthands, durations, grid areas and transforms.

**The fix.** `CssText.hpp` scans the outer runs once and edits an owned
string at most once at the front. Callers that only need a token keep a
string view. The nth matcher trims before copying/lowercasing and no longer
copies the padding for every child. The existing whitespace and quote sets
are preserved; this is not a CSS tokenizer conformance change.

**Measured.** `python3 tools/bench/bench_css_whitespace.py --sizes
30000,300000 --reps 3 --max-ms 3000 --baseline BUILD` compares every padded
page with its compact form and with the baseline binary. At 300,000 spaces:
nth-child 14,627 → 111 ms; attribute value 4,687 → 90 ms; grid minmax
9,786 → 199 ms; grid repeat 22,545 → 188 ms; filters 7,191 → 156 ms;
durations/grid-area 5,791 → 283 ms; transforms 11,694 → 304 ms. The full
table and protocol are in ARCHITECTURE.md, Phase 31. Keyframe and shorthand
cases were already cheap in this pipeline and stayed cheap.

**What is still approximate.** The whitespace set is still the C locale's
`std::isspace`, not the CSS whitespace production; VT is still trimmed.
The separate `:nth-child(2n + 1)` sign-space parsing bug that this phase
surfaced **is** fixed now — see finding 39. The finding-28 float,
computed-style memory and 50,000-attribute tokenizer costs are also not
fixed here and remain open tasks.

**How verified.** `make test` passes 117 suites, including the helper model,
selector/value equivalence and long-run suites. `make bench-css` passes 20
padded runs with identical compact/paged layout and paint. The scoreboards are
unchanged: HTML 1781/1792, JS 840/845, DOM 572/572, URL 1150/1188.
`make diff-trees` has 0 differing pages; `make deep` has 0 failures in 1,292
runs; `make fuzz` has 0 findings in 800 runs; `make asan` reports no sanitizer
finding (ASan JS 839/845, ASan deep 842 runs, 0 failed); `make tsan`,
`make bench` and `make diff-strings` pass.

---

## 36. ✅ The renderer did not speak CSS's color keywords

**The hole.** The renderer's common color parser had its own table of 19
names, 15 of them not the CSS Color 4 values (`red` was the Bootstrap-looking
`rgb(220, 53, 69)`, `green` was `rgb(40, 167, 69)`), and every other CSS
keyword fell through to opaque black. The DOM side knew the standard's 148
keyword values, so `getComputedStyle()` answered `rgb(255, 0, 0)` for a `red`
that canvas, SVG and layout painted as a different red.

**The fix.** `Color::named_color_value(lower, out)` is now the single 148-name
table (`include/litebird/common/Color.hpp`). `Color::parse()` uses it for
`red`, `green`, `tomato`, `rebeccapurple`, the gray/grey aliases and the rest;
`transparent` returns alpha 0; `Color::parse()` with an unknown keyword still
returns opaque black. The computed-style color path and the declaration
normalizer use the same table instead of keeping their own copies, so the
bytes computed for `getComputedStyle()` and the bytes painted by canvas, SVG
and layout cannot drift again.

**How verified.** The unit suite `css_color_keywords_match_the_css_specification`
checks all 148 keywords through both `Color::named_color_value()` and
`Color::parse()` plus case-folded/whitespace-surrounded names, several aliases,
`transparent`, `tomato`, `silver` and an unknown keyword. `color_parsing` now
expects CSS's `blue` (`rgb(0, 0, 255)`). `make test` prints `All 114 engine
test suites passed successfully!`. The refactor initially made the DOM corpus
571/572 because the declaration normalizer no longer accepted `transparent` and
`currentcolor`; both values are accepted explicitly in the declaration block and
572/572 was restored. The rerun is 572/572. `make conformance` is 1781/1792,
`make js-conformance` is 840/845, `make url-conformance` is 1,188/1,188,
`make diff-trees` has 0 differing pages, `make fuzz` is 0/800 findings,
`make deep` is 0/1,292 failures, and `make asan` reports no sanitizer finding
(ASan JS corpus 839/845; ASan deep 842 executions). The HTML and JS floors are
unchanged; the parser was not touched.

**What is still approximate.** `Color::parse()` remains a keyword, hex and
integer-`rgb()` parser: no `rgba()`, `hsl()`/`hsla()`, `hwb()` or laboratory
color syntax; `currentcolor` outside the computed-style cascade has no inherited
color to resolve against and remains opaque black; and the legacy
`dark_gray()`/`light_gray()` helpers keep their previous call-site constants.

## 37. ✅ Page fetch could read arbitrary local files

**The hole.** `ResourceLoader::load` took any string that did not start with
`data:` or `http://` as a filesystem path. `fetch()` passed the script's URL
straight to it without the document's address, so a page at
`https://example.test/` could read `/home/user/notes.txt` or `file:///…`.
`new Worker()` used the same loader and, on failure, returned the path itself
as inline script. The loader was not told where the page came from.

**The fix.** Page loads now require a `ResourceRequestContext`
(`document_url` for capability, `base_url` for resolution) and go through
`ResourceLoader::load_for_page`. URLs are parsed before dispatch. Web
documents cannot read `file:` URLs or local paths; a `<base href="file:///…">`
does not grant file access. Opaque/empty contexts are `data:` only. Documents
the embedder loaded from `file:` keep local-file access for the CLI/TUI.
Trusted navigation is a separate `load_navigation` entry point used by the
shell. `data:` uses the standard processor already shared with XHR. Worker
loads inherit the creator's context; a failed load is an error event, not
source. `Response.url` is the resolved address without a fragment.

**How verified.** `make test` passes 132 suites, including
`resource_loader_page_requests_are_origin_aware`,
`fetch_and_workers_use_the_document_origin`, and
`fetch_relative_requests_use_the_document_base_url`. `make security-fetch`
generates a marker file and prints `3 checks; 0 failure(s).` Repro:
`printf 'LOCAL_SECRET\n' > /tmp/secret.txt; printf '<script>fetch("/tmp/secret.txt").then(r=>r.text()).then(console.log, e=>console.log("ERR:"+e));</script>' > /tmp/page.html; ./build/litebird /tmp/page.html --url=https://example.test/page.html`.
Before: `LOCAL_SECRET`. After: no marker; the request is refused or resolved
as a web URL. The conformance scoreboards are unchanged (html5lib 1781/1792,
JS 840/845, DOM 629/629, URL 1150/1188, XML 1702/1702, WPT 4640/5192);
diff-trees/diff-xml have 0 differing, deep has 1332 runs/0 failed, ASan has
132/132 suites and 882 deep runs/0 failed. A generated base/history probe run
through `compare_with_jsdom.py` printed `4 lines, 4 the same` before/after.

**What is still open.** CORS/same-origin network policy and
`XMLHttpRequest` over the network are the next row; `https:` is not
implemented; `blob:`/`about:`/custom schemes are not fetched; cross-origin
HTTP `fetch()` is not CORS-checked. A `file:` document is a local embedder
context and can read local files, so opening untrusted local HTML remains a
trust decision. Worker MIME checks, module workers, and external
`<script src>`/`<link>` loading are unchanged.
---

## 38. ✅ Host names outside ASCII are converted with UTS #46

**The hole.** The URL parser kept host names with characters outside ASCII
as written: `http://faß.example` stayed `faß.example` instead of becoming
`xn--fa-hia.example`. The web-platform-tests URL corpus said so
(`make url-conformance` was 1,150 of 1,188; every one of the 38 failures
was such a host), and so did every browser the data was recorded from.

**The fix.** The host parser now runs the URL Standard's domain parser:
percent-decode, UTF-8 decode (no BOM), then Unicode's UTS #46
(Nontransitional Processing — the flags the standard mandates:
`CheckBidi=true`, `CheckJoiners=true`, `Transitional=false`,
`VerifyDnsLength=false`), then the standard's forbidden-domain-code-point
check, then the usual `ends in a number` → IPv4 fallback. The UTS #46
machinery (mapping table, recursive canonical decompositions,
composition pairs derived from single-step decompositions and verified
through NFC, bidi classes, joining types) is table-driven and lives in
`src/net/Idna.cpp`; the mapping table and joining types are Unicode 15.1.0
(the `idna` package), while combining marks, combining classes,
decompositions, composition pairs and bidi classes come from the generator
host's `unicodedata` (14.0.0 here) — the generated header states both. Two bugs in the first cut were caught by
diffing against an outside oracle: the Punycode encoder started from code
point 0 instead of 0x80 (RFC 3492 `initial_n`), and the recomposition step
kept the consumed combining mark's class instead of resetting it to 0,
which broke every double-accented precomposed letter (U+01D6 `ǖ`, U+0390
`ΐ`, U+1E08 `Ḉ`); the composition pair table also had to be built from
single-step (not recursive) decompositions. `check_label` no longer
enforces the IDNA2008 CONTEXTO rules (middle dot only between `l`s and so
on): UTS #46 and the URL Standard never had them, and Node's
`domainToASCII` accepts those code points.

**How verified.** `make url-conformance` is 1,188 of 1,188 with a floor of
1,188. Beyond the corpus: every Unicode code point (1,112,063 of them) was
run through `domain_to_ascii` and compared against Node's `domainToASCII` —
99.9% identical, and every divergence accounted for (see
`tests/url/README.md`: IPv4 handling lives one layer up in the host parser
and is checked there, Node is lenient where RFC 5893 forbids mixing
left-to-right and right-to-left, and Node rejects the code points that are
PVALID only because they are unassigned). A 2,263-case sweep of
multi-code-point domains against both Node and Python's `idna` found no
other disagreement, and an exhaustive code point differential against a
Python UTS #46 mirror prints zero real divergences.

**What is still approximate.**

- ẞ (U+1E9E) converts to `xn--zca` (UTS #46 Nontransitional). Node's ICU
  answers `ss`. The WHATWG mapping and the corpus win; the divergence is
  deliberate and documented, never to be "fixed".
- ß (U+00DF) in a domain maps to itself, not `ss` — Nontransitional, as
  required.
- Bidi: litebird implements RFC 5893 §2 exactly (CheckBidi), which rejects
  labels that mix left-to-right and right-to-left characters and labels
  that start with an AN-class digit or a combining mark, where Node accepts
  some of them. The URL Standard's CheckBidi is the authority; if the
  web-platform-tests data ever gains a mixed-direction host case, this is
  the behaviour it must have.
- Unassigned code points that are PVALID (e.g. U+11F00) are accepted; both
  Node and Python's `idna` reject them (they have no bidi data for them).
  UTS #46 accepts them, so does litebird.
- The ASCII fast path (all-ASCII domain returned lowercased, regardless of
  what ToASCII would say) is the standard's web-compatibility rule, not a
  shortcut: an all-ASCII domain with, say, an interior underscore parses
  even though UseSTD3ASCIIRules would call it invalid.
- Combining marks, combining classes, decompositions, composition pairs and
  bidi classes come from the generator host's `unicodedata` (Unicode 14.0.0,
  Python 3.11); the mapping and joining-type tables are Unicode 15.1.0. The
  drift only affects code points assigned after Unicode 14 — currently all
  unassigned PVALID code points, which need no bidi data anyway. The
  generated header states both versions.

---

## 39. ✅ `:nth-child( An+B )` reads a sign separated from its digits by spaces

**The hole.** The formula matcher (`src/css/StyleResolver.cpp`,
`match_nth_formula`) parsed the offset part of an `<an+b>` formula with
`std::stoi`. `"+ 1"` is not parseable by `stoi` — the sign must be attached
to the digits — so the `catch` branch silently defaulted the offset to `0`.
`li:nth-child(2n + 1)` matched only the even items; with three items, only
`b` turned red. Any formula written with a space between the sign and the
number (`2n + 1`, `-n + 3`, `+ n + 1`, the common style in real pages) was
read as `b = 0`, and `:nth-of-type()` shared the matcher and the bug.

**The fix.** The `<an+b>` micro-syntax ignores whitespace tokens, exactly as
the CSS parser does, so the matcher now erases CSS whitespace (Tab, LF, FF,
CR, Space) between tokens before parsing: `2n + 1`, `2n+1` and
`2n\t+\n1` are the same formula, and a sign may sit next to or away from the
digits it signs (`+ 2n + 1` is `+2n+1`). The coefficient and the offset are
parsed as strictly signed integers (`parse_b_integer`): an empty coefficient
or a bare sign means `1`, and anything that is not a signed integer (`1.5n`,
`xn`, `n++1`, `1+1`, `++1`) makes the formula **invalid**, so it matches
nothing — as an invalid selector must — instead of falling back to `a = 1`
or `b = 0` (both silent-default bugs found while fixing this one, both dead
now). `:nth-of-type()` is fixed by the same change.

**How verified.** The finding-35 repro now paints items 1 and 3 red and item
2 dark:
`printf '<ul><li>a</li><li>b</li><li>c</li></ul><style>li:nth-child(2n + 1){color:rgb(255,0,0)}</style>' > /tmp/nth.html && ./build/litebird /tmp/nth.html --dump-paint`.
`make test` passes 133 suites including the new `nth_child_formula_parsing`
(the sign-space matrix `2n + 1`, `2n+ 1`, `2n +1`, tab/newline/FF/CR between
tokens, `2N+1`, `+ 2n + 1`, plus `odd`/`even`/exact/`0n+2`/negative steps,
and an invalid list that must match nothing and never crash). The benchmark
keeps its glued `+1` so it still measures the trimming cost, untouched.
`make bench-css` passes 20/20 with dumps identical to the compact pages;
`make conformance` is 1781/1792, `make dom-conformance` is 629/629,
`make js-conformance` is 840/845 and `make url-conformance` is 1188/1188 —
the corpora are unchanged.

**What is still approximate.** The whitespace set is the five code points
above, not the CSS whitespace production in general (finding 35 documents
the same limit in the trim path); an empty token span such as `:nth-child()`
or a comment inside the formula (`2n/*x*/+1`) matches nothing, as an invalid
argument does. The grammar covered is the `<an+b>` subset of the micro-syntax
(`odd`, `even`, an optional signed coefficient, `n`, an optional signed
offset, a plain integer, and arbitrary whitespace between the tokens); it is
deliberately not a general CSS tokenizer. A sign separated from its
coefficient by whitespace (e.g. `- 2n + 1`) is accepted, which token-boundary
purists may call generous; no valid formula is rejected.

---

## 40. ✅ Attribute-list front removal was quadratic

**The hole.** `AttributeList` kept the attributes in a `std::vector` and,
once a list was long enough to index, kept a parallel vector of the numbers
each name was given. The number index avoided rebuilding positions on every
erase, but `erase(iterator)` still erased from both vectors at the same
offset. Removing the first attribute moved every remaining string pair and
every remaining number: draining 50,000 attributes from the front was
quadratic. The recorded script loop took about 6 s (9 s in the older
measurement); the same removals from the back were tens of milliseconds.

**The fix.** Only `include/litebird/dom/AttributeList.hpp` changed. The
items and `Index::numbers` remain vectors, but each has a logical start
offset. A back erase pops the back. A front erase advances the start and
leaves a dead prefix; `compact()` drops that prefix only when it is at least
as large as the live tail, so repeated front erases are amortized O(1), a
full drain is linear, and an empty list still allocates nothing. A
`std::deque` was tried first and removed: libstdc++ allocates for an empty
deque, which made every element pay even with no attributes and failed the
deep-page memory limits. The public iterators are still random-access vector
iterators (`attributes.begin() + index`, `found - list.begin()`). The lazy
index still scans below 32 entries, then maps names to numbers and bisects
the live ascending number range. Copy copies only the live range and drops
the index; move resets the source start so a moved-from list is empty;
`clear`, value update in place, insertion order, `operator[]`,
`find/count/erase` and order-insensitive equality keep their semantics.
`AttributeNamespaces` was not changed.

**Measured.** Whole-process times, best of three, same machine and hour;
baseline is the pre-change binary:

```bash
python3 tools/bench/bench_attributes.py \
  --baseline build/litebird-attributes-baseline --reps 3 --max-ms 3000
make bench-attributes
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
repetitions, expected checksums, 3,000 ms whole-process limit.

**How verified.** `make test` passes 135 suites, including
`attribute_list_end_removals_keep_order_and_index` and
`attribute_list_front_removal_is_linear_in_practice`. `make bench-attributes`
passes 6 cases with 0 failures. The release run kept html5lib 1781/1792, JS
840/845, DOM 629/629, URL 1188/1188, XML 1702/1702, WPT 4640/5192;
diff-trees/diff-xml have 0 differing; deep has 1332 runs/0 failed (slowest
25.0 s); bench-css has 20 padded runs/0 failures; security-fetch has 3
checks/0 failures; diff-strings is at the 95-difference ceiling. A generated
out-of-corpus probe run through `tools/conformance/compare_with_jsdom.py`
printed `4 lines, 4 the same` before and after. `make tsan` passed (JsString model/thread under ThreadSanitizer and under ASan+UBSan). `make asan` reported no sanitizer finding: 135/135 suites, JS 839/845, DOM 629/629, URL 1188/1188, XML 1702/1702, WPT 4640/5192; ASan diff-xml 1000 documents/0 differ and round trip 1000/1000; fuzz 400 parser mutations, 400 pipeline runs and 600 XML mutations with 0 findings; ASan deep 882 runs/0 failed (slowest 4.3 s).

**What is still open.** This fixes end removals, not arbitrary middle
removals: a middle erase still moves the vector tail. The HTML tokenizer's
repeated-name search for one element with 50,000 attributes was fixed in
Phase 39 (finding 46). The `Attr`-is-not-a-`Node`, one-qualified-name-in-two-
namespaces and collection named-property rows remain open. No parser, layout,
CSS or JavaScript engine file was changed.

---

## 41. ✅ A function could not be a prototype

**The hole.** A function is a value in this engine and an object is a heap
cell, and a prototype link is a pointer to a cell. Nothing led from a cell
back to a function, so nothing could have a function as its prototype and
give it back. `Object.getPrototypeOf(Derived)` for `class Derived extends
Base` was an object that was not `Base` and could not be called;
`Object.getPrototypeOf(TypeError)` was not `Error`; `Function.prototype` was
an object. A class kept its parent in a hidden property and a static member
was looked for one step up and no further. Finding 34 listed it as known and
not fixed, for the 18 web-platform-tests subtests that failed on it: every
`assert_throws_js` walks up from the error type it was given with
`Object.getPrototypeOf` until it reaches `Error`.

What it broke outside a test suite is the code most pages ship: a class
compiled for older browsers. The helpers Babel and TypeScript emit are made
of the calls in this table.

| A script | Before | Now, and in Node |
| :--- | :--- | :--- |
| `Object.getPrototypeOf(TypeError) === Error` | `false` | `true` |
| `class Derived extends Base {}`; `Object.getPrototypeOf(Derived) === Base` | `false` | `true` |
| `class C extends B {}`, `class B extends A {}`, `A` has `static s()`: `C.s()` | `TypeError: s is not a function` | runs |
| `typeof Function.prototype`; `Function.prototype()` | `object`; `TypeError` | `function`; `undefined` |
| `Object.getPrototypeOf(Object.create(f))()` | `TypeError: p is not a function` | what `f` returns |
| `Reflect.construct(F, [], G) instanceof G` | `false` | `true` |
| `F.prototype.m = …`, then `Object.defineProperty(F, 'prototype', { writable: false })`: how a compiled class ends | `TypeError: Cannot redefine property: prototype` | accepted |
| `Object.getPrototypeOf(Derived).call(this, x)` in a compiled constructor | `TypeError: Function.prototype.call called on a non-function` | calls `Base` |
| A class with a parent, compiled by Babel 7, by Babel's newer helpers, by TypeScript | stops at its first class, all three | prints what Node prints, all three |
| `Object.getPrototypeOf(HTMLElement) === Element`; `HTMLElement.ELEMENT_NODE` | `false`; `undefined` | `true`; `1` |

**The fix.** A function's cell can know the function it belongs to
(`JsValue::as_prototype()` gives it a copy; `JsValue::make_object(cell)`
gives the function back), every kind of function can have a cell, and the
collector takes the pair apart, since the cell holds the function and the
function holds the cell. The copy lives in a pointer the cell of a function
had no use for, so no object and no value grew. Classes are linked to their
parents, `super()` calls whatever the prototype is when it runs,
`Function.prototype` is a function, `Reflect.construct` honours its third
argument, `Object.defineProperty` follows the standard's
ValidateAndApplyPropertyDescriptor, and the DOM's interface objects have
their parents as prototypes. ARCHITECTURE.md, Phase 34, has the design.

### What each instrument found

The change was written with a new file of the JavaScript corpus beside it,
`prototypes.dat`, whose expected output is what Node prints. What follows was
found by something else.

| Found by | What |
| :--- | :--- |
| Two wrong collectors, built on purpose | With the function a cell stands for counted and not marked, a parent class reachable only as a prototype lost the variables it closed over, and every test passed: the cell keeps the function's name and properties, so checks of those see nothing. A test has to call the function it got back and read a variable it closed over. One suite and one corpus case do now |
| test262, run in the binary of `main` and in this one | Two tests that passed on `main` and failed here, of 15,686. `[] instanceof Function.prototype` with a `prototype` that is a string threw on `main` only because `Function.prototype` was not callable; `instanceof` never looked at what kind of thing `prototype` is. And `{ __proto__: function () {} }` named the function `__proto__`, on `main` as well, where the function could not be got back to see its name. Both fixed, both corpus cases now |
| Reading the whole diff once more before the documents | The mark that says `Function.prototype` is not a constructor was a hidden property, and `'%not-a-constructor' in f` was `true` for every function. It is a bit of the cell now. Looking for what else is stored that way found the row below |
| The same reading | A script can read and write the engine's own slots, which are properties whose names begin with `%`: `el['%classList'] = 5` makes `el.classList` a number. Older than this change, not fixed, a row of the task table |
| The sanitizer build, on a probe for classes in workers | A read of freed memory when a page ends while a worker is starting: 7 of 60 runs of `main`'s release build end in `SIGSEGV` on four `new Worker(...)` and nothing else. Not this change, not fixed, a row of the task table |
| The corpus run on a busy machine | One case of `long-strings.dat` timed out. It builds a string half a surrogate pair at a time, which costs the square of the length (20,000 pairs: 4.3 s, and 5 ms a pair at a time), and takes 6 of the 10 seconds a case is allowed. Same on `main`. The CI floor is one below the score until it is fixed; a row of the task table |
| jsdom, on the interface objects | Nothing wrong here, and three answers of jsdom's that are not the standard's, because its window is another realm than the one its script runs in: `Object.getPrototypeOf(EventTarget) === Function.prototype` is `false` there. `interfaces.dat` stays away from them |

### What is known to be wrong and is not fixed

All of it is the same on `main`. The first seven have a failing case each in
`prototypes.dat` (nine cases), recorded from Node.

- **`new.target` is the function that is running**, not the one `new` was
  applied to. `if (new.target === Shape) throw …` in a parent's constructor
  stops `new Circle()`. The DOM needs the same value:
  `Reflect.construct(HTMLElement, [], Derived)` is `Illegal constructor`, so
  a custom element compiled by Babel cannot be made
  (`tests/dom-conformance/open/compiled-custom-elements.js`: 4 of 9 lines as
  jsdom prints them).
- **A built-in does not make the subclass it was called on**: `List.from`,
  `new List().map(f)` and `P.resolve(1)` give a plain array and a plain
  promise. No `Symbol.species`.
- **`instanceof` does not ask `Symbol.hasInstance`**, answers `false` for a
  right side with no `prototype` where it should throw, and does not ask a
  bound function for its target.
- **A function's own property names lack `length` and `name`**, `f.name =
  'x'` takes effect, and async and generator functions have
  `Function.prototype` itself as their prototype.
- **Nothing tells a built-in that is a constructor from one that is not**:
  `new Math.max()` makes an object, `class X extends f.bind(null)` is
  accepted.
- **A property of a prototype that cannot be written does not stop an
  assignment** to an object that inherits it.
- **A function bound to an object that holds it is never collected**, nor
  what a pending timer holds: the collector cannot see into what a native
  function captured.
- **An object a parent's constructor returns does not become `this`** in the
  child, and `super.x` in a class that extends null is `undefined`.
- **Not about functions, found by running all of test262:** `Promise.all`,
  `any` and `race` read an iterable to its end before they look at an
  element, so with one that never ends the process grows until it is killed.
  Three tests end that way in both builds, and a fourth on an array index
  near 2^32 (arrays have no holes). A row of the task table.

### What the numbers do not say

**67 of 76** in `prototypes.dat`: the nine that fail were written to fail,
one for each thing above that a case could be recorded for. The 67 are what
somebody thought to ask.

**10,835 of 15,686** test262 tests in the directories about functions,
classes and objects, 10,727 before. That is 69%, and of the whole suite 54%
pass (26,012 of 47,967; 25,893 before). The number that matters is the
difference: 108 more and none fewer in those directories. In the whole
suite it is 122 more and three fewer, and the three are not noise. They ask
that the prototype of an async function, of a generator function and of an
async generator function is an object that cannot be called. It is
`Function.prototype` itself here, before and now, and the tests passed for
as long as `Function.prototype` was not callable. The fault they are about
is the fourth of the list above; the tests stay failing until it is fixed.

**640 of 640** DOM cases is what jsdom answers. The 11 new ones ask what the
prototype of an interface object is and whether its constants are inherited,
and nothing about what an interface object does when it is called.

**Every measure within a tenth of `main`** is 20 loops on one machine
(ARCHITECTURE.md, Phase 34, section 8). The one that moved is `bind`.

**How verified.** Every line is a command and what it printed on the merged
tree, in the sandbox.

- `make test`: `All 144 engine test suites passed successfully!`, five of
  them new. Linked against the objects of `main`, with the lines that use
  the new C++ interface cut, each of the five fails at least half of its
  checks.
- `make js-conformance`: `JS conformance, full corpus: 907/921 (98.5%) [23
  files, 0 crashes, 0 timeouts]`; `prototypes.dat` 67 of 76. With
  `--reference "node -e"`: 921 of 921. The binary of `main` passes 7 of the
  76.
- `python3 tools/conformance/run_test262.py --binary main=… --binary
  now=build/litebird`: 15,686 tests, `main` 10,727 pass, now 10,835; 108
  pass now and not on `main`, 0 the other way. With `--timeout 4 built-ins
  language annexB` added: 47,967 tests, `main` 25,893, now 26,012; 122 pass
  now and not on `main`, 3 the other way (named above).
- `make wpt`: `Web Platform Tests: 4658/5192 subtests pass in 76 files; 534
  fail as listed, 0 fail and are not listed`. On `main`: 4640, 552 listed.
- `make dom-conformance`: `640/640` in 24 files, and the same from jsdom
  with `--reference`. The binary of `main` passes 629: none of the 11 new
  cases.
- `make conformance` 1781 of 1792, `make url-conformance` 1188 of 1188,
  `make xml-conformance` 1702 of 1702, `make diff-trees` 0 of 6,000 trees
  differ, `make diff-xml` 0 of 4,000 documents and 0 of 3,000 round trips,
  `make diff-strings` 95 of 50,580 results differ, `make bench`, `make
  bench-css` 20 runs and 0 failures, `make security-fetch` 3 checks and 0
  failures: all as on `main`.
- `make deep` and `make asan` were run when `main` was three merges back:
  before a fix to a CSS selector, the attribute list of Phase 35 and the
  media work of Phase 38 came in, with seven suites. The unit tests were run
  again in the sanitizer build after the first two of those merges, 140 of
  140; after the third, CI's sanitizer job is what ran them.
- `make deep`: `1332 runs over 465 pages at depth 200000 on a 8 MB stack, 0 failed; slowest 22.5 s (table [dumps])`.
- `make asan`: 137 of 137 suites; JavaScript 906 of 921 (the one more is a
  case of deep recursion that needs more stack than a sanitizer build's
  frames leave, as before); DOM 640 of 640; URL 1188 of 1188; XML 1702 of
  1702; web-platform-tests 4658 of 5192; 1,000 generated documents and 1,000
  round trips; `fuzz: 400 mutations, 0 finding(s)`, `pipeline fuzz: 400 runs
  … 0 finding(s)`, `fuzz xml: 600 mutations … 0 finding(s)`; `882 runs over
  315 pages at depth 10000 on a 2 MB stack, 0 failed`. No report from either
  sanitizer. It takes 19 minutes here.

---

## 43. ✅ Real-Web Media & Vector Rendering: standalone JPEG/GIF decoders, upgraded SVG & Canvas 2D

**What was missing.**
- **JPEG & GIF:** `ImageDecoder` had no GIF decoder at all, and its JPEG path
  (`ImageDecoder.cpp`, owned in parallel by PR #39) was a minimal stub that
  could not decode real-world Baseline/Extended/Progressive JPEGs, chroma
  subsampling (4:2:0 / 4:2:2 / 4:4:0), restart markers (`DRI`/`RST0`–`RST7`),
  or CMYK/YCCK color spaces.
- **SVG (`SvgRenderer`):** `<path>` ignored curves (`C/c`, `S/s`, `Q/q`, `T/t`)
  and elliptical arcs (`A/a`), `<defs>` children were painted directly,
  `transform` matrices, `preserveAspectRatio`, inline `style` declarations,
  `opacity`/`fill-opacity`/`stroke-opacity`, `fill-rule="evenodd"`,
  `currentColor`, `display:none`/`visibility:hidden`, `<linearGradient>` /
  `<radialGradient>`, `<use>` / `<symbol>`, `<clipPath>`, and rounded
  `<rect rx ry>` were missing.
- **Canvas 2D (`CanvasRenderingContext2D`):** No Current Transformation Matrix
  (`save`/`restore`/`translate`/`rotate`/`scale`/`transform`/`setTransform`),
  no `clip()`, no `globalAlpha`, no `NonZero`/`EvenOdd` winding rules or
  anti-aliased scanline fill, thin Bresenham-only stroking (no real stroke
  width, caps, joins, dashes, or `strokeGradient`), no `roundRect`/`ellipse`/
  `arcTo`/`isPointInPath`, and no `getImageData`/`putImageData`.

**What changed (Phase 38, strictly disjoint from open PRs #36–#40).**
- **Standalone ITU-T T.81 JPEG decoder (`JpegDecoder.hpp/.cpp`):** Zero-dependency
  C++20 decoder for Baseline (`SOF0`), Extended Sequential (`SOF1`), and
  Progressive (`SOF2`) DCT JPEGs. Supports 8/16-bit `DQT`, 9-bit fast-lookup +
  16-bit canonical `DHT`, `DRI`/`RST0`–`RST7`, interleaved and non-interleaved
  scans, progressive DC/AC spectral selection + successive approximation + EOB
  runs, separable double-precision 8×8 IDCT with DC-only fast paths, centered
  triangle `[3/4, 1/4]` chroma upsampling (4:4:4, 4:2:2, 4:2:0, 4:4:0, odd
  dimensions), Grayscale / YCbCr / RGB / CMYK / Adobe YCCK, and strict
  `kMaxDecodedPixels` / `kMaxScans` / `RasterBudget` bounds.
- **Standalone GIF87a/GIF89a decoder (`GifDecoder.hpp/.cpp`):** Zero-dependency
  single-frame and animated multi-frame decoder. Supports 2..8-bit LZW with
  deferred clear-code handling, Global/Local Color Tables, 4-pass interlacing,
  Graphic Control Extension transparency, delays, all 4 disposal methods
  (`None`, `DoNotDispose`, `RestoreBackground`, `RestorePrevious`), `NETSCAPE2.0`
  loop counts, sub-rectangle frame compositing, and strict frame/pixel bounds.
- **Upgraded Canvas 2D (`Canvas2D.hpp/.cpp`):** Full 2D CTM stack (`save`/
  `restore`, `translate`/`rotate`/`scale`/`transform`/`setTransform`/
  `resetTransform`), 4× vertical + analytical horizontal anti-aliased scanline
  rasterizer with `NonZero` and `EvenOdd` winding rules, CW-wound polygon-ribbon
  thick stroking with `butt`/`round`/`square` caps, `miter`/`bevel`/`round`
  joins, `setLineDash`/`getLineDash`/`lineDashOffset`, `stroke_gradient`,
  inverse-CTM linear/radial gradients with `Pad`/`Reflect`/`Repeat` spread modes,
  `globalAlpha` + alpha clip mask (`clip()`), `roundRect`/`ellipse`/`arcTo`/
  `rect`/`isPointInPath`, `getImageData`/`putImageData`/`createImageData`,
  CTM-aware `drawImage`, and JS property accessors via `JsValue::define_accessor`.
- **Upgraded SVG renderer (`SvgRenderer.hpp/.cpp`):** Iterative ID index (no
  native recursion on deep DOMs), `<defs>` skipping, full SVG path grammar
  including W3C F.6.5/F.6.6 `A/a` elliptical arcs and compact flag syntax,
  `viewBox` + `preserveAspectRatio` (`meet`/`slice`/`none`), `transform` stack,
  presentation attributes + inline `style` + `#RGBA`/`#RRGGBBAA`/`rgba()` +
  `currentColor` + `opacity`/`fill-opacity`/`stroke-opacity` + `display:none`/
  `visibility:hidden`, `<linearGradient>`/`<radialGradient>` with `href`
  inheritance / `objectBoundingBox` / `userSpaceOnUse` / `gradientTransform` /
  `spreadMethod`, `<use>` with depth/cycle guard + `<symbol>`, `<clipPath>`,
  rounded `<rect rx ry>`, nested `<svg>`, and recursion/budget caps.
- **Verification & fuzzing tools:** `tools/media_check.cpp`,
  `tools/conformance/verify_media_render.py`, `tools/fuzz/fuzz_media.py`, and
  `Makefile` targets `verify-media` and `fuzz-media` (wired into CI and `asan-core`).

**How verified.**
- `make test`: **139/139** engine unit test suites pass (4 new suites:
  `jpeg_decoder_baseline_progressive_and_guards`,
  `gif_decoder_87a_89a_interlaced_and_animation`,
  `canvas2d_ctm_scanline_thick_stroke_clip_and_js_bindings`,
  `svg_real_world_paths_arcs_gradients_use_clip_and_guards`), both in Release
  (`-O2`) and under strict ASan+UBSan (`-fsanitize=address,undefined -fno-sanitize-recover=all`).
- `make verify-media`: **35/35** checks pass (both Release and ASan+UBSan+LSan
  `detect_leaks=1`): 21 JPEG variants vs ImageMagick/`libjpeg` achieve
  **52.06–99.00 dB PSNR** (`MAE 0.000–0.389`, `max_diff 0–3`), 9 GIF variants
  (including 4-pass interlaced and animated Dispose None/Background/Previous/Optimize)
  achieve **bit-for-bit exact `max_diff = 0`** against ImageMagick `-coalesce`,
  and 5 SVG feature suites pass deterministic pixel assertions.
- `make fuzz-media`: **1,800** mutated JPEG/GIF/SVG cases in Release and
  **4,500** cases under ASan+UBSan+LSan (`detect_leaks=1`) — 0 crashes, 0 UB,
  0 leaks, 0 timeouts.
- Deep-page stress checks (`tools/fuzz/deep_pages.py --only svg/canvas/images`):
  39/39 Release runs at depth 200,000 (8 MB stack) and 36/36 ASan+UBSan runs
  at depth 10,000 (2 MB stack) pass with 0 failures.
- Existing conformance corpora unchanged: `make conformance` 1781/1792,
  `make js-conformance` 840/845, `make dom-conformance` 629/629,
  `make url-conformance` 1188/1188, `make wpt` 4640/5192, `make bench-css` 20/20,
  `make security-fetch` 3/3.

**What is still open.**
- **`ImageDecoder::decode()` dispatch wiring:** `include/litebird/render/ImageDecoder.hpp`
  and `src/render/ImageDecoder.cpp` were left untouched because PR #39 owns them
  in parallel; once PR #39 merges, wire `JpegDecoder::decode()` and
  `GifDecoder::decode()` into `ImageDecoder::decode()` (and optionally expose
  animated GIF frames to the shell).
- **Other image formats:** WebP, AVIF, and animated APNG/ICO are not implemented.
  JPEG arithmetic coding (`SOF9`/`SOF10`), lossless JPEG (`SOF3`), and 12-bit
  precision (`P=12`) are rejected cleanly as unsupported.
- **SVG features still missing:** `<text>`/`<tspan>`, `<style>` CSS stylesheet
  rules targeting SVG elements, `<pattern>`, `<mask>`, `<filter>` (`feGaussianBlur`,
  etc.), `<marker>`, `<image>` inside `<svg>`, and SMIL/CSS animations on SVG.
- **Canvas 2D features still missing:** `fillText`/`strokeText`/`measureText`,
  shadows (`shadowBlur`/`shadowColor`), `createPattern`, `Path2D`,
  `drawImage(HTMLImageElement)` (only `ImageData` / canvas-to-canvas is wired),
  and remaining Porter-Duff / blend modes beyond the existing set.

---

## 44. ✅ `<img src>` bypassed document-origin security, and unloaded images without dimensions laid out as 100×100 boxes

**The hole.** Phase 33 moved `fetch()` and `Worker` behind
`ResourceLoader::load_for_page`, but `LayoutTree::build_element` (`src/layout/LayoutTree.cpp`)
still passed an `<img>` element's raw `src` attribute straight to
`ImageDecoder::decode_file(src)`. `decode_file` opened any non-`data:` string
with `std::ifstream(path, std::ios::binary)`. Consequently, a remote web
document (`https://attacker.example/page.html`, or one smuggling
`<base href="file:///...">`) could embed `<img src="/path/to/local.png">` or
`<img src="file:///path/to/local.bmp">`, read local disk images in-process, and
observe their decoded intrinsic pixel dimensions via layout or
`getBoundingClientRect()`/`offsetWidth`/`offsetHeight` side channels.

In addition, when an `<img>` without explicit `width`/`height` attributes or
CSS dimensions failed to decode (or was blocked by origin policy),
`LayoutBox::layout_image` (`src/layout/LayoutBox.cpp`) sized the box to a
hardcoded `100×100` placeholder, pushing surrounding content down by 100 px,
and inline-formatted `<img>` boxes (`InlineFormattingContext`) never wrote their
laid-out fragment coordinates back to the DOM element (`set_layout_box`), so
`img.getBoundingClientRect()` returned `0×0` even for valid inline images.

**The fix.**
1. `LayoutTree` now resolves and loads every `<img src>` through
   `ResourceLoader::load_for_page(src, doc.base_uri(), doc.url(), /*enforce_cors=*/false)`
   and caches decoded bitmaps per resolved URL on `Document`
   (`lookup_decoded_image` / `cache_decoded_image`). Web documents (`http:`,
   `https:`, `data:`) cannot read `file:` URLs or local filesystem paths, even
   when `<base href="file:///...">` is present; trusted local `file:` documents
   keep relative local image loading.
2. `ImageDecoder::decode_bytes` now decodes PNG (`decode_png` / `encode_png`,
   backed by the pure-C++20 RFC 1950/1951/1952 `Deflate` engine with 64 MiB
   decompression and 16384×16384 / 64M-pixel bounds), wires Phase 38's
   `JpegDecoder` and `GifDecoder`, and rasterizes SVG subresources alongside
   BMP, PPM, and `data:image/*;base64,...` URLs.
3. Unloaded or blocked `<img>` elements without explicit `width`/`height` now
   lay out as `0×0` instead of `100×100` (explicit HTML `width`/`height`
   attributes or CSS `width`/`height` are still honoured), and
   `InlineFormattingContext` writes laid-out `ImageNode` border-box coordinates
   back to the `Element` so `getBoundingClientRect()`/`offsetWidth` report live
   inline image geometry.

**How verified.**
```bash
./build/test_suite
make security-fetch
```
`make test` passes **153/153** suites (and **153/153** under ASan+UBSan),
including `img_subresources_enforce_document_origin_security` (proving a web
page at `https://app.example/index.html` — with and without
`<base href="file:///...">` — is denied local PNG access and lays out the
blocked `<img>` at `0×0`, while a local `file:` document decodes the same PNG
at its `24×18` intrinsic size and reports `24×18` from
`img.getBoundingClientRect()`). `make security-fetch` passes `3 checks; 0 failure(s)`.

**What is still approximate.**
- WebP, AVIF, and ICO `<img>` payloads are not decoded yet (PNG, JPEG, GIF, SVG,
  BMP, and PPM are decoded).
- `HTMLImageElement` script properties and events (`naturalWidth`,
  `naturalHeight`, `complete`, `decode()`, `load`/`error` events on `<img>`,
  `srcset`, and `<picture>`) are not implemented yet.
- Unloaded `<img>` elements with non-empty `alt` text and no explicit
  dimensions collapse to `0×0` rather than synthesizing an inline `alt`-text
  fallback box.
- Cross-origin `<img>` loads follow the standard default `no-cors` subresource
  model, but `<canvas>` taint tracking (`crossorigin="anonymous"`) is not
  enforced yet because `drawImage` does not yet take `HTMLImageElement` bitmaps.

---

## 45. ✅ Worker MIME/module policy, EventTarget error dispatch, and shutdown `SIGSEGV`

**What was wrong.**
1. **No MIME or HTTP-status check on worker scripts:** `src/js/Worker.cpp` ran
   any response returned by `ResourceLoader::load_for_page` as classic
   JavaScript regardless of its `Content-Type` (`text/plain`, `text/html`,
   `application/json`, default `data:,...`, missing `Content-Type`, or HTTP
   `3xx` responses where `resp.success` was true).
2. **`WorkerOptions` ignored:** `new Worker(url, { type: "module" })` silently
   ignored the options argument and executed the script as classic JavaScript;
   invalid `type` values did not throw `TypeError`; `options.name` was dropped;
   calling `Worker(...)` without `new` did not throw `TypeError`.
3. **Broken `EventTarget` error dispatch:** `WorkerObject::add_event_listener`
   stored `"error"` listeners in `m_error_listeners`, but `post_error()` only
   ever called `m_onerror`, silently dropping all `addEventListener("error")`
   listeners. There was no `removeEventListener`, deduplication, `{ once }`,
   `handleEvent`, `preventDefault()`, `stopImmediatePropagation()`,
   `self.onerror`/`self.location`/`self.name`, or `importScripts(...)`, and
   `WorkerObject::create_js_instance()` did not wire `onmessage`/`onerror`.
4. **Exit-time `SIGSEGV` (4/30 runs on `main`) & reference-cycle leak:**
   `create_constructor()` stored `[inst, worker]` inside `worker->m_onmessage`
   while `inst` stored `worker` inside its method closures, creating a strong
   reference cycle. Because `Heap::default_heap()` is intentionally never
   destroyed at exit, active `WorkerObject` threads were never terminated or
   joined before `main()` returned. Reproducing with
   `for (var i = 0; i < 4; i++) new Worker("data:text/javascript,postMessage(1)");`
   crashed with `SIGSEGV` (`rc = -11`) in **4 of 30 runs** on `main`; a
   captured backtrace showed worker threads still inside
   `WorkerObject::worker_thread_main` → `JsInterpreter::JsInterpreter` →
   `install_object_builtins` → `js_symbol_for_key` dereferencing
   `symbol_registry()`'s `static std::unordered_map` after `__run_exit_handlers`
   had already destroyed it. Nested workers also had no parent-loop wake
   mechanism or child teardown before the parent's `EventLoop` was destroyed.

**What changed.**
- `include/litebird/js/Worker.hpp` and `src/js/Worker.cpp`:
  - Added `WorkerObject::is_javascript_mime_type()` implementing WHATWG MIME
    Sniffing §4.3 (the 16 JavaScript MIME essences, parameter stripping, ASCII
    HTTP whitespace trimming, case-insensitive matching, no MIME sniffing) and
    required HTTP `2xx` + a JavaScript MIME type in `load_worker_script()` for
    both `new Worker(...)` and `importScripts(...)`.
  - Added `WorkerType` / `WorkerOptions` parsing (`new` check, `type` enum
    validation, `{ type: "module" }` rejected cleanly via an `ErrorEvent`
    instead of running classic JS, `name` exposed on `self.name` and global
    `name`, plus `self.location` and `importScripts(...)`).
  - Replaced ad-hoc callbacks with unified `ListenerMap` / `EventControl`
    dispatch on both `Worker` and `self` (supporting `onerror`/`onmessage` +
    `addEventListener` + `removeEventListener` + deduplication + `{ once }` +
    `{ handleEvent }` + `preventDefault()` + `stopImmediatePropagation()` +
    `ErrorEvent` metadata, and shared across `create_constructor` and
    `create_js_instance`).
  - Isolated owner-thread `JsValue` state into `WorkerOwnerJsState` (released
    on `terminate()` and on the worker's final exit task to eliminate both
    cross-thread non-atomic `handle_count` races and the `inst` ↔ `WorkerObject`
    cycle).
  - Added a lifetime-guarded `WorkerOwnerLoopState` with parent-wake support
    for nested workers and guaranteed child teardown before parent `EventLoop`
    destruction.
  - Serialized worker `JsInterpreter` initialization and registered an immortal
    `WorkerRegistry` with a `std::atexit` hook (installed after warming up
    `JsLexer`'s keyword table, `JsOps`'s `symbol_registry()`, and
    `DomBindings`'s interface table) so all active worker threads are woken
    and joined cleanly before static destructors run.
- Added 4 unit test suites in `tests/test_suite.cpp` (suites 154–157) and
  `tools/security/verify_worker_policy.py` (`make security-worker`, wired into
  CI `build-and-test` and `make asan-core`).

**How it was verified.**
- `make test`: **157/157** unit suites pass in Release (`-O2`) and
  **157/157** under ASan+UBSan (`build-asan/test_suite`, `ASAN_OPTIONS=detect_leaks=0`).
- `make security-worker`: **17/17** checks pass in Release (loopback HTTP
  `text/javascript`, `text/plain`, `text/html`, `application/json`, missing
  `Content-Type`, HTTP `404`, `data:` MIME checks, `{type:"module"}` refusal,
  `{type:"classic", name}`, invalid `type` `TypeError`, `importScripts` MIME
  and Phase 33 local-file boundary, and **60/60** concurrent 4-worker
  startup/exit runs) and **17/17** under ASan+UBSan (**60/60** stress runs).
- Extended startup/exit stress check: **200/200** consecutive CLI runs of
  4 concurrent `new Worker(...)` with immediate exit pass (**0 crashes**,
  down from 4/30 `SIGSEGV` before; also resolves the 7/60 `SIGSEGV` /
  `heap-use-after-free` worker startup exit bug noted in finding 41).
- Existing conformance suites unchanged on latest `main`: `make conformance`
  1781/1792, `make js-conformance` 906/921, `make dom-conformance` 640/640,
  `make url-conformance` 1188/1188, `make wpt` 4658/5192, `make security-fetch`
  3/3, `make verify-media` 35/35, `make bench-attributes` 6/6, `make bench-css`
  20/20.

**What is still open.**
- **ES module workers:** `{ type: "module" }` is now explicitly refused with an
  error event instead of silently running classic JS; actual module worker
  execution waits for an ES module loader in the JS engine.
- **Structured clone & messaging ports:** `postMessage` still serializes via
  JSON (no `ArrayBuffer`, transferables, cyclic object graphs, `MessagePort`,
  or `SharedWorker`).
- **Defense-in-depth for process-wide registries:** `symbol_registry()` in
  `src/js/JsOps.cpp` and `s_registry` in `src/js/DomBindings.cpp` are still
  destroyed at static teardown; Phase 40 warms them before registering
  `atexit_shutdown_workers()` so worker threads are always joined first, and a
  small follow-up can heap-allocate both like `Heap::default_heap()`.

---

## 46. ✅ The tokenizer's repeated-attribute-name check was quadratic

**The hole.** `HtmlTokenizer::consume_tag` drops a repeated attribute name —
the standard's *duplicate-attribute* parse error, where the first attribute
with that name wins — by comparing the name with every attribute the token
has already accepted (`std::any_of` over `token.attributes` in the
`flush_attribute` lambda). Each attribute costs as much as the ones before
it, so a tag with n attributes costs n² comparisons. One element with 50,000
attributes took about 2.9 s in the HTML parser (10.4 s before Phase 28 indexed
the element's own attribute list); 25,000 took about 0.5 s. An end tag with
that many attributes paid the same and its attributes were then thrown away
by the tree builder. This was the tokenizer half of finding 28's cost row;
the XML parser already did the check with a set.

**The fix.** Only `src/html/HtmlTokenizer.cpp` changed. A tag that reaches 32
accepted attributes gets a `std::unordered_set<std::string_view>` of their
names; below that the scan stays, because comparing a handful of short
strings in a contiguous vector is cheaper than hashing into a table, and an
ordinary tag allocates nothing extra. The set holds views of the names inside
`token.attributes`, so no name is stored twice — which is only safe while
those strings stay put. Appending can reallocate the vector, and a short name
lives inside its `std::string` object and moves with it, so the index records
the vector's capacity and is rebuilt whenever it changes; between rebuilds no
accepted name moves. Capacity grows geometrically, so all the rebuilds
together are O(n) and the check is linear. Nothing else changed: names are
ASCII-lower-cased before the check, the first value wins, order is the order
of first occurrence, valueless/quoted/unquoted forms behave as before, `=`
may start a name, end-tag attributes are still tokenized, and EOF inside a
tag still drops the token.

**Measured.** Whole-process times (`--dump-dom-webplatform`: parse and tree
dump, no layout, no scripts), best of three, idle machine, same hour. The
baseline is this merged tree built with `main`'s tokenizer, kept as
`build/litebird-final-baseline`, so the only difference is this change:

```bash
python3 tools/bench/bench_tokenizer_attributes.py \
  --baseline build/litebird-final-baseline --reps 3 --max-ms 1000
```

| Case (50,000 attribute occurrences) | Baseline | Current | Speedup |
| :--- | ---: | ---: | ---: |
| distinct names | 3,868 ms | 95 ms | 40.7× |
| each name twice | 1,166 ms | 34 ms | 34.3× |
| uppercase repeats | 1,091 ms | 42 ms | 26.1× |
| long names, each twice | 3,260 ms | 71 ms | 45.9× |
| quoted, bare, unquoted | 545 ms | 18 ms | 30.9× |
| many small tags | 33 ms | 34 ms | 1.0× |
| end tag with attributes | 3,153 ms | 21 ms | 149.7× |

Every dump equals the tree the markup must produce — computed by the bench
from the markup, not recorded from a binary — and equals the baseline
binary's dump byte for byte. The square is gone: distinct names at
25,000/50,000/100,000/200,000 attributes take 45/95/183/344 ms (before: 628 ms
at 25,000, 3,868 ms at 50,000). Tokenizing alone, 50,000 attributes cost 28 ms
in the release build and 102 ms under ASan/UBSan. `make
bench-tokenizer-attributes` is the checked-in guard: seven cases, three
repetitions, expected trees, 1,000 ms limit, run by CI (the scan cost 3.9 s).

**How it was verified.** `make test` passes 159 suites; the two new ones are
`tokenizer_repeated_attribute_names_keep_the_first` and
`tokenizer_duplicate_attribute_check_is_linear`. Both were checked against
mutants: with the old scan restored the correctness suite still passes and
the timing suite fails at `distinct_seconds < 2.0`; with the capacity rebuild
removed the correctness suite fails at the first size past a vector growth,
which is exactly the dangling-view bug the rebuild prevents. Out-of-corpus
pages through `tools/conformance/compare_with_jsdom.py` printed `16 lines, 16
the same`, `20 lines, 20 the same` and `6 lines, 6 the same` before and after
(duplicates through `innerHTML`/`DOMParser`/`insertAdjacentHTML`/SVG/table
cells, the sizes around the index threshold and every vector growth,
whitespace forms, form controls, and `=`-starting names). No expectation was
re-recorded and no floor was lowered.

The release run on the merged tree kept every scoreboard: html5lib
1781/1792, JS 907/921, DOM 640/640, URL 1188/1188, XML 1702/1702, WPT
4658/5192 (534 listed, 0 unlisted); diff-trees 6,000 pages and diff-xml 4,000
documents with 0 differing, XML round trip 3000/3000; deep 1,332 runs over
465 pages at depth 200,000 on an 8 MB stack, 0 failed (slowest 26.4 s);
bench-css 20 padded runs, bench-attributes 6 cases,
bench-tokenizer-attributes 7 cases and security-fetch 3 checks, all with 0
failures; verify-media 35 checks and fuzz-media 1,800 cases clean;
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
3.7 s). `make tsan` passed (the string type under ThreadSanitizer, then under
ASan with leak checking).

**What is still open.** A cost fix in one function, not a parser conformance
change: the tokenizer's other states, the tree builder (the eleven html5lib
`<select>` cases) and the XML parser were not touched. Finding 28's layout
costs (a block inside an inline box, 10,000 sized floats) and finding 40's
middle erases remain, as do the `Attr`-is-not-a-`Node`,
one-qualified-name-in-two-namespaces and collection named-property rows.
