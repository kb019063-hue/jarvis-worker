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
| 4 | GC roots are incomplete and never registered → latent use-after-free | **Critical** | Open |
| 5 | Segfault on deeply nested input (depth ≈ 3,000) | **High** | Open |
| 6 | Two incompatible object models in `JsValue` | **High** | Open |
| 7 | `var` has block scope instead of function scope | **High** | Open (partial) |
| 8 | No recursion guard in the JS engine | **High** | Open |
| 9 | `get_property()` allocates on every property read | **High (perf)** | Open |
| 10 | `JsValue` is 296 bytes and most of it is dead | **High (perf)** | Open |
| 11 | HTTP header injection + no response size cap | **Medium** | Open |
| 12 | `ComputedStyle` is 920 bytes; `LayoutBox` is 1,200 | **Medium (perf)** | Open |
| 13 | Build uses `-O2` only (no LTO / `-march=native`) | Low | Open |
| 14 | `has_class()` allocated a `stringstream` per call | **High (perf)** | ✅ Fixed |
| 15 | `text_content()` was O(n²) in depth | **Medium (perf)** | ✅ Fixed |

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

**Why the fuzzer missed it.** `tools/fuzz/fuzz_parser.py` feeds every mutation
to `--dump-dom-webplatform`, which never runs layout and therefore never calls
`ImageDecoder`. 900 mutations of the *parser* says nothing about a *decoder*
that parses a different, binary format with different invariants.

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

Still missing from the probe (not fixed): `typeof`, `switch`, `**` and `??`
all fail to parse, and `"a".repeat(3)` returns `undefined` instead of `"aaa"`.

---

## 4. GC roots are incomplete — latent use-after-free

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

I could not make it fire from the CLI (nothing currently triggers an automatic
collection — `collect_garbage()` is only reachable via the explicit `gc()`
builtin), which is the only reason this has not shown up as a crash. It is a
tripwire waiting for the first automatic GC trigger or long-lived page.

**Fix direction:** either make `JsValue` hold `shared_ptr<JsObject>` and drop
the custom collector, or register roots properly at every boundary
(`JsEnvironment`, native captures, listeners) and clear `is_marked` in one pass.
Also note `Heap::default_heap()` is a process-wide singleton with a mutex —
worker threads allocate from the *same* heap as the main thread, so a
collection on one thread can free objects the other is using.

---

## 5. Segfault on deeply nested input

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

## 6. Two incompatible object models in one struct

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

## 7. `var` has block scope

```js
var i = 99; for (var i = 0; i < 3; i = i + 1) {} console.log(i);  // 99, should be 3
if (true) { var z = 5; } console.log(z);                          // undefined, should be 5
```

`var` is function-scoped in JS. Scripts that declare a loop counter with `var`
and read it afterwards — extremely common — get `undefined`.

I attempted this fix and **reverted it**: hoisting to the nearest function scope
also changed how an inner `var` shadows an outer one. `var g = 1; function
h() { var g = 9; return g; }` regressed from `g = 1` to `g = 9`, i.e. the inner
declaration leaked to the global scope. The cause is that the function body's
block environment does not always sit directly under the function environment
in the chain, which needs untangling before `var` hoisting can be made correct.
The finding stands; the fix needs more care than it looked like.

---

## 8. No recursion guard in the JS engine

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

## 10. `JsValue` is 296 bytes

Measured:

```
sizeof(JsValue)    = 296
sizeof(JsObject)   = 120    <- the actual object is smaller than a handle to it
sizeof(Node)       = 136
```

296 bytes come from carrying *every* payload inline: `std::string`,
`shared_ptr<JsObject>`, a `std::vector<std::string>`, three `shared_ptr<AstNode>`,
`std::function`, and five more shared_ptrs for canvas/promise/generator/DOM.
Only one is ever used. Every `push`, `pop`, `pop()`, and by-value return copies
all of it.

This is also why 20,000 `<div>`s peaks at **98 MB**:
`ru_maxrss = 98,196 KB` for a 1 MB dump — roughly **5 KB per element**.

A tagged union (or `std::variant` with the union'd payloads out of line) would
put this at 16–24 bytes and is the single largest memory win available.

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

## Measured impact of the two perf fixes

Cascade, N rules × N elements (`--dump-layout`):

| N | before | after | |
|---|---|---|---|
| 200 | 64.3 ms | 2.9 ms | 22× |
| 400 | 180.9 ms | 2.9 ms | 62× |
| 800 | 758.6 ms | 2.6 ms | 292× |
| 1600 | 2144.4 ms | 2.4 ms | **894×** |

Before, 8× the input cost 33× the time — textbook quadratic. Now it is flat.
Note that this was **not** an algorithmic fix to the cascade: the resolver is
still O(elements × rules) with no id/class/tag indexing, and will go quadratic
again as soon as stylesheets get big. The win came from removing the constant
factor. The next step is bucketing rules by rightmost simple selector.

Nested-depth parse (`--dump-dom`):

| depth | before | after | |
|---|---|---|---|
| 400 | 49.1 ms | 2.3 ms | 21× |
| 800 | 96.7 ms | 2.1 ms | 46× |
| 1600 | 379.8 ms | 2.1 ms | **180×** |

---

## Suggested order of work

1. **~~Assignment operators~~** (#3) — done; `typeof` / `switch` / `**` / `??`
   are the remaining grammar gaps.
2. **GC roots** (#4) — the latent UAF is the thing that will bite hardest later.
3. **Depth guard** (#5) — 9 KB of input currently kills the process.
4. **`JsValue` slimming** (#10) — unlocks memory *and* interpreter speed.
5. **`var` scoping** (#7), then `typeof` / `switch` / `??`.
6. **Cascade indexing** — make the now-flat curve actually stay flat.
7. **Fuzzer coverage** — feed `--dump-layout` and `--render-bmp` too, and seed
   it with malformed image headers, not just HTML.

## One meta-point

The conformance harness is genuinely good — it is wired into CI with a floor,
and Phase 17's audit that *fixed the measurement* rather than the engine is
senior-level work. But 98.8% on tree construction coexists with all of the
above, because the scoreboard measures one stage of a five-stage pipeline and
says nothing about memory safety, script semantics, or speed. A second
scoreboard — even a tiny one: "no crash on N hostile images", "these 20 JS
expressions produce these 20 results" — would catch every Critical finding here.
