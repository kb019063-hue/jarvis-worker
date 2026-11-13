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
| 31 | The DOM a script sees: 61 of 572 cases recorded from jsdom passed. `querySelector('ul li')` found nothing, `classList`, `dataset`, `children`, `closest`, `append`, `new CustomEvent`, `location`, `getComputedStyle` did not exist, `innerHTML` read back text unescaped, nothing was an `instanceof` anything. URLs were resolved by cutting strings: `?q=1` on `/app/page.html` gave `/app/?q=1`. Scripts ran after the whole page was parsed | **Critical** | ✅ Fixed (Phase 27): 572 of 572; URL data 1150 of 1188. Open: XML in `DOMParser`, date inputs, `el.style` shorthands, layout numbers |

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
shape in other containers and are still open: `delete` of 50,000 keys from one
object takes over 120 s (the property map rebuilds its index on every
`delete`) and `shift()` until a 50,000-element array is empty takes 5.3 s
(arrays are plain vectors). Node takes 91 ms and 0.2 s.

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
| 300,000 spaces inside `:nth-child( )` | 8.8 s |
| 300,000 spaces inside an attribute selector | 6.3 s |
| one element with 50,000 attributes | 4.5 s |
| 300,000 spaces inside `repeat(`, `minmax(` or `translate(` | 2.1 s each |
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
vector. The spaces are trimmed one character at a time from the front of a
string, in about twenty places in the CSS code. The tokenizer drops a
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
| URL parsing and setters, the web-platform-tests data (1,188 cases) | 2 | 1,150 |

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
  tree, and not the one it asked for.
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
