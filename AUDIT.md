# Litebird engine audit

Everything below was found by reading and probing the engine, not the corpus.
The conformance scoreboard only measures the tree builder, so it says nothing
about the four things that actually matter for a browser: **memory safety,
script correctness, performance, and memory footprint.**

Commit `8b3aeff` on this branch fixes the four highest-value items (marked ✅).
The rest are documented with enough detail to act on.

**The resolved findings have moved** to one file each in `docs/findings/`, and
are indexed at the bottom of this document. A citation of the form "AUDIT.md,
finding NN" means that file when the finding is closed, and the section still
here when it is open.

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
| 26 | Regular expressions over long input: quadratic lookbehind, limits on repeated groups | **Medium** | ✅ Fixed: lookbehind in Phase 24; bytecode VM on explicit heap stack in Phase 37 (finding 42; 30M catastrophic-backtracking step guard kept) |
| 27 | Pages hostile by size: a crash in an unclosed `calc(`, a 60-byte page that crashed the parser, bitmaps as large as the page asked for, loops that cost the square of their input | **High** | ✅ Fixed (Phase 25) |
| 28 | A block inside an inline box is not laid out; many sized floats in one box and a few other inputs still cost the square of their length | **Medium** | ✅ Fixed: block-in-inline split in Phase 36; float placement rewritten (band-edge sweep, exact landing, no step cap). The remaining inputs are smaller loops |
| 29 | About one random page in 115 got another tree than an independent parser builds: an end tag that meets an open SVG or MathML element, `</button>` with an element still open inside, and three smaller things | **Medium** | ✅ Fixed (Phase 26) |
| 30 | `<br/>` closed the paragraph it stood in: a void element written with a slash closed its parent. `innerHTML` parsed everything as the content of a `<body>`, so a table filled through it came out as loose text. Fourteen smaller things, most of them with no case in the corpus | **High** | ✅ Fixed (Phase 26) |
| 31 | The DOM a script sees: 61 of 572 cases recorded from jsdom passed. `querySelector('ul li')` found nothing, `classList`, `dataset`, `children`, `closest`, `append`, `new CustomEvent`, `location`, `getComputedStyle` did not exist, `innerHTML` read back text unescaped, nothing was an `instanceof` anything. URLs were resolved by cutting strings: `?q=1` on `/app/page.html` gave `/app/?q=1`. Scripts ran after the whole page was parsed | **Critical** | ✅ Fixed (Phase 27/28/37): 644 of 644; URL data 1,188 of 1,188; date inputs & `el.style` shorthands fixed in Phase 37. Open: layout numbers in `getComputedStyle`, `document.write`, SVG DOM (the style sheet objects landed in Phase 72) |
| 32 | Tables could not span: `colspan` and `rowspan` were read by nothing, `rowspan="0"` was ignored, a spanning cell pushed no column widths, `border-collapse` and `border-spacing` did not exist, a cell with a border lost it (cells were laid out directly instead of through `layout()`), and a percentage width inside a cell resolved against the column instead of the table | **Medium** | ✅ Fixed (Phase 29), re-landed from PR #16 |
| 33 | Deleting from a large property map rebuilt the index on every `delete`, and `shift()` moved the whole array tail each call | **High (perf)** | ✅ Fixed (Phase 30) |
| 35 | CSS trim sites erased the first character repeatedly, shifting the whole remainder; 300,000 spaces cost seconds in selectors, grid, filters, durations and transforms | **Medium (perf)** | ✅ Fixed (Phase 31); the separate finding-28 layout costs remain open (its tokenizer cost was fixed in Phase 39) |
| 36 | The renderer's color parser had 19 names, 15 of them not the CSS values, and returned black for the other 129 CSS keywords | **Medium** | ✅ Fixed (Phase 32) |
| 37 | Page `fetch()`/`Worker` sent arbitrary non-data/non-http addresses to the local filesystem; a web page could read local files, and a failed worker load could execute the path as source | **High (security)** | ✅ Fixed (Phase 33); CORS/XHR network/HTTPS landed in Phase 36 (preflight `OPTIONS` open) |
| 38 | Host names outside ASCII were kept as written instead of converted with UTS #46; the 38 remaining URL failures were all such hosts | **Medium** | ✅ Fixed; URL data 1188/1188 |
| 39 | `:nth-child(2n + 1)` read a sign separated from its digits by spaces as offset 0, and invalid formulas silently defaulted | **Medium** | ✅ Fixed; `:nth-of-type()` shares the fix |
| 40 | Removing attributes from the front of a long list shifted both the item vector and the index-number vector; 50,000 front removals took seconds while the back was milliseconds | **Medium (perf)** | ✅ Fixed (Phase 35); middle erases remain (the tokenizer repeated-name cost was fixed in Phase 39) |
| 41 | A function could not be a prototype: `Object.getPrototypeOf(Derived)` was not `Base` and could not be called, `Object.getPrototypeOf(TypeError)` was not `Error`, a static member was found one parent up and no further, `Function.prototype` was an object. Classes compiled by Babel and TypeScript stopped at their first class; every `assert_throws_js` of web-platform-tests failed | **High** | ✅ Fixed (Phase 34). Open: `new.target` in a parent's constructor, `Symbol.species`, `Symbol.hasInstance`, bound functions are never collected |
| 42 | Repeated regex groups overflowed the stack, modern CSS cascade/L4 selectors/units/MQ L4 were missing (and deep CSS blocks/selectors hit stack or quadratic bounds), `el.style` kept shorthands whole, and HTML5 date/time inputs acted as text inputs | **High** | ✅ Fixed (Phase 37) |
| 43 | Real-web raster/vector rendering gaps: no JPEG or GIF decoder, incomplete Canvas 2D CTM/path/stroke/clip/accessors, and missing SVG paths/arcs/gradients/`<use>`/`<clipPath>` | **High** | ✅ Fixed (Phase 38); `ImageDecoder.cpp` dispatch wired in Phase 36; WebP/AVIF & text remain open |
| 44 | `<img src>` bypassed `ResourceLoader::load_for_page` and read arbitrary local files directly from `LayoutTree`, and unloaded images without `width`/`height` laid out as `100×100` boxes instead of `0×0` | **High (security / layout)** | ✅ Fixed (Phase 36) |
| 45 | Worker script loads lacked MIME/module validation, `addEventListener("error")` listeners were never invoked, `Worker` instances leaked via an `inst` ↔ `WorkerObject` cycle, and pages exiting while workers were starting crashed in `SIGSEGV` / `heap-use-after-free` during static teardown of `js_symbol_for_key`'s registry and `DomBindings`'s interface table | **High (crash / security)** | ✅ Fixed (Phase 40); ES module execution, structured clone beyond JSON, and immortalizing `symbol_registry()` / `DomBindings` registry remain open |
| 46 | The HTML tokenizer compared every attribute name with every name it had already accepted: one element with 50,000 attributes cost the square of the count, and so did an end tag with that many | **Medium (perf)** | ✅ Fixed (Phase 39) |
| 48 | `<iframe>` elements had no nested browsing context (`contentDocument` / `contentWindow` / `sandbox` / `srcdoc` / `window.frames` / `postMessage` did not exist), DOM bindings assumed one global realm per thread, `LayoutTree` skipped `<iframe>` subdocuments, and `.xhtml`/XML CLI loads never executed `<script>` elements — blocking 495 WPT subtests and the `DOMParser-parseFromString-url*` / `.xhtml` suites | **High** | ✅ Fixed (Phase 42); cross-origin `WindowProxy`/`Location` reflection landed in Phase 56 (finding 62); `MessageChannel`/`MessagePort` and out-of-process site isolation remain open |
| 49 | `new.target` was the function that was running: an abstract class refused its own subclasses and `Reflect.construct(F, [], G)` did not name `G` inside `F`. `super()` dropped an object the parent's constructor returned, `this` could be bound twice or never, and a custom element compiled for older browsers could neither be made nor upgraded | **High** | ✅ Fixed (Phase 43); `this` before `super()`, `eval` in a function and private names remain |
| 50 | Unicode escapes in JavaScript identifiers (`var \u0061 = 1`, `o.\u2118`, `#\u{1D49C}`) failed with `Invalid or unexpected token`, non-ID Unicode bytes `>= 0x80` (e.g. `U+2E2F`) were accepted as identifiers, and strict/contextual escaped keyword rules were unenforced | **High (spec conformance)** | ✅ Fixed (Phase 44): +1,440 / 0 lost in default 21 Test262 dirs (12,275/15,686); 374/374 (100.0%) in `language/{identifiers,keywords,future-reserved-words,reserved-words}` |
| 51 | `Promise.all`/`any`/`race`/`allSettled` handed every value straight to `to_promise` inside `js_iterate`, bypassing the constructor's overridable `resolve`, and an iterator with no end was consumed forever before its first value could throw | **High** | ✅ Fixed (Phase 45) |
| 52 | A canvas could draw every shape but no text: `fillText`/`strokeText`/`measureText` and the `font`/`textAlign`/`textBaseline` state did not exist, so no canvas text could reach the screen | **Medium** | ✅ Fixed (Phase 46) |
| 54 | Every sized float rebuilt and sorted the top/bottom edges of all prior nonempty bands before finding the first fitting y, even though the prior bands had not changed | **Medium (perf)** | ✅ Fixed (Phase 48): indexed vertical intervals and subtree maximum capacity; 10,000 packed floats 5.244 → 0.096 s and mixed 12.505 → 0.125 s end-to-end, nine out-of-corpus layouts identical. Remaining: updates to a band spanning many distinct intervals still touch each such interval |
| 55 | Customizable `<select>` content and `<selectedcontent>` cloning followed pre-2025 parsing rules, failing the final 11 html5lib tree-construction corpus cases | **High (spec conformance)** | ✅ Fixed (Phase 49): **1792/1792 (100.0%)** across all 57 html5lib tree-construction files (CI conformance floor raised `1781 -> 1792`) |
| 59 | Reading or `substring`-slicing a string ending in a high surrogate half (`0xD800..0xDBFF`) settled/left a 3-byte UTF-8 sequence inside the block so completing the surrogate pair copied the whole string (`58/242/893 ms` at 5k/10k/20k iterations) | **Medium (perf)** | ✅ Fixed (Phase 53): ASCII 1-byte / non-ASCII 2-byte UTF-16 `Buffer` + lazy UTF-8/`Wide` caches + `sole_thread` CAS tail rewind; 20k read-between-halves & slice-complete drop to `17 ms`, `indexOf`-between-halves `154 ms` |
| 62 | Every cross-origin `WindowProxy` / `Location` described itself with the ordinary own-keys walk: `Object.keys`/`getOwnPropertyNames`/`Reflect.ownKeys`/spread/rest/`for…in` leaked the child's globals and element ids, `getOwnPropertyDescriptor` answered with ordinary storage, `Object.defineProperty` wrote into the child's own map, `freeze`/`seal`/`preventExtensions`/`setPrototypeOf` mutated it, `getPrototypeOf` returned the child realm's `Window.prototype`, and cross-origin `Location` leaked `href`/`pathname`/`origin` outright | **High (security)** | ✅ Fixed (Phase 56): one cross-origin predicate (`dom::cross_origin_own_keys`) drives every reflection seam; `Location` gained a security exotic; cross-origin reflection is exactly the spec's index + table names + `then`; define/delete/integrity/`instanceof`/prototype all refuse. 80 targeted checks in `tests/cross_origin_windows.cpp` (`make test`) |
| 65 | Derived constructor `this` before `super(...)` and `let`/`const`/`class` declarations had no Temporal Dead Zone: pre-`super()` `this` read/wrote the allocated object, `typeof z` before `let z` returned `"undefined"`, block `let` failed to shadow above its line, and `c = 2` before `const c` created a global | **High (spec conformance)** | ✅ Fixed (Phase 59): `JsEnvironment::Binding::is_initialized`, `AssignResult::Uninitialized`, derived `this` TDZ enforcement, and parser/evaluator lexical pre-declaration across blocks, functions, `switch`, loops, `try/catch`, classes, and default params (`construct.dat` 49/50, `lexical.dat` 41/41, +43 default / +49 TDZ test262, 0 regressions) |

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

**The float placement was rewritten.** A float is now placed by a sweep of
the band edges: capacity below the flow is constant between two consecutive
band edges, so the smallest position at which the float fits is found by
closing and opening bands in y order, which is also exactly flush with the
bottom edge of the float in the way (CSS 2.1 9.5.1, rule 6), and never past
a fixed step cap. The quarter-height sliding loop and its 512-step limit are
gone. On the audit's input (floats of 100 by 10 pixels in an 800-pixel box):
every float lands inside the box again, in its row of eight, and the
10,000-float parse-plus-layout-plus-dump end to end takes about 5 s on a
two-core machine. A float wider than the containing block is placed at the
top and overflows, the way CSS 2.1 9.5.1 handles it. One pre-existing
expectation codified the old quantization: a 250-pixel float C below floats
that end at 89.5 used to land at 90 (the slide's one-pixel step) and now
lands exactly at 89.5.

Phase 48 subsequently indexed these nonempty bands for placement: the same
10,000-float page takes 0.096 s end-to-end rather than 5.244 s on the
Phase 48 baseline, without changing its positions (finding 54).

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

## Finding 54 — repeated sized-float band sweeps (Phase 48)

The old correctness repair (finding 28) had already removed the 512-step cap
and placed every float at the exact first fitting band edge; its remaining
performance problem was rebuilding and sorting every prior nonempty band's
endpoints for each new sized float. Phase 48 keeps that placement behavior,
but indexes constant-capacity vertical intervals with a treap that can skip
whole subtrees without a fitting edge. See `ARCHITECTURE.md`, Phase 48, for
the generator, exact baseline/new timings, differential command and the
remaining O(number of intersecting intervals) insertion cost. No table-layout,
encoding, Canvas, or JavaScript semantics were changed.


## Resolved findings

One file per resolved finding, in `docs/findings/`. A finding used to stay
in this document after it was fixed, which is how it reached 296 KB with 57
of its findings closed. `AUDIT.md` holds what is **open**; what is fixed
moves here and out. (Everything below is in the repository's history too.)

- **Finding 1** — [1. ✅ Image decoder: crash and out-of-bounds write](docs/findings/01-image-decoder-crash-and-out-of-bounds-write.md)
- **Finding 2** — [2. ✅ Strict equality did not exist](docs/findings/02-strict-equality-did-not-exist.md)
- **Finding 3** — [3. ✅ Assignment operators were missing from the grammar](docs/findings/03-assignment-operators-were-missing-from-the-grammar.md)
- **Finding 4** — [4. ✅ GC roots were incomplete — a live use-after-free](docs/findings/04-gc-roots-were-incomplete-a-live-use-after-free.md)
- **Finding 5** — [5. ✅ Segfault on deeply nested input](docs/findings/05-segfault-on-deeply-nested-input.md)
- **Finding 7** — [7. ✅ `var` has function scope (Phase 19)](docs/findings/07-var-has-function-scope-phase-19.md)
- **Finding 8** — [8. ✅ No recursion guard in the JS engine](docs/findings/08-no-recursion-guard-in-the-js-engine.md)
- **Finding 14** — [14. ✅ `has_class()` built a `stringstream` per call](docs/findings/14-hasclass-built-a-stringstream-per-call.md)
- **Finding 15** — [15. ✅ `text_content()` was O(n²) in depth](docs/findings/15-textcontent-was-o-n2-in-depth.md)
- **Finding 16** — [16. ✅ Deep HTML parsing repeated scope scans and ancestor walks](docs/findings/16-deep-html-parsing-repeated-scope-scans-and-ancestor.md)
- **Finding 17** — [17. ✅ HTTP request injection and unbounded response reads](docs/findings/17-http-request-injection-and-unbounded-response-reads.md)
- **Finding 18** — [18. ✅ A script was cut off at the first unknown character, and could run twice](docs/findings/18-a-script-was-cut-off-at-the-first-unknown-character-and.md)
- **Finding 19** — [19. ✅ `gc()` freed objects that were still in use](docs/findings/19-gc-freed-objects-that-were-still-in-use.md)
- **Finding 21** — [21. ✅ Deeply nested input killed the process](docs/findings/21-deeply-nested-input-killed-the-process.md)
- **Finding 22** — [22. ✅ A template literal dropped substitutions that were literals](docs/findings/22-a-template-literal-dropped-substitutions-that-were.md)
- **Finding 23** — [23. ✅ What the differential run found](docs/findings/23-what-the-differential-run-found.md)
- **Finding 24** — [24. ✅ `node.appendChild(node)` ended the process, and so did a very deep tree](docs/findings/24-node-appendchild-node-ended-the-process-and-so-did-a.md)
- **Finding 25** — [25. ✅ Strings were copied on every read](docs/findings/25-strings-were-copied-on-every-read.md)
- **Finding 26** — [26. ✅ Regular expressions over long input: lookbehind fixed (Phase 24), bytecode VM removes repeated-group recursion (Phase 37)](docs/findings/26-regular-expressions-over-long-input-lookbehind-fixed.md)
- **Finding 27** — [27. ✅ What the deep pages found that was not depth](docs/findings/27-what-the-deep-pages-found-that-was-not-depth.md)
- **Finding 29** — [29. ✅ What the comparison with another parser found](docs/findings/29-what-the-comparison-with-another-parser-found.md)
- **Finding 30** — [30. ✅ What the wider comparison found](docs/findings/30-what-the-wider-comparison-found.md)
- **Finding 31** — [31. ✅ The DOM a script sees](docs/findings/31-the-dom-a-script-sees.md)
- **Finding 32** — [32. ✅ Tables could not span, and had no border model](docs/findings/32-tables-could-not-span-and-had-no-border-model.md)
- **Finding 33** — [33. ✅ Property deletion and array front edits were quadratic](docs/findings/33-property-deletion-and-array-front-edits-were-quadratic.md)
- **Finding 34** — [34. ✅ `DOMParser` gave an HTML document for XML](docs/findings/34-domparser-gave-an-html-document-for-xml.md)
- **Finding 35** — [35. ✅ CSS whitespace trimming was quadratic](docs/findings/35-css-whitespace-trimming-was-quadratic.md)
- **Finding 36** — [36. ✅ The renderer did not speak CSS's color keywords](docs/findings/36-the-renderer-did-not-speak-css-s-color-keywords.md)
- **Finding 37** — [37. ✅ Page fetch could read arbitrary local files](docs/findings/37-page-fetch-could-read-arbitrary-local-files.md)
- **Finding 38** — [38. ✅ Host names outside ASCII are converted with UTS #46](docs/findings/38-host-names-outside-ascii-are-converted-with-uts-46.md)
- **Finding 39** — [39. ✅ `:nth-child( An+B )` reads a sign separated from its digits by spaces](docs/findings/39-nth-child-an-b-reads-a-sign-separated-from-its-digits-by.md)
- **Finding 40** — [40. ✅ Attribute-list front removal was quadratic](docs/findings/40-attribute-list-front-removal-was-quadratic.md)
- **Finding 41** — [41. ✅ A function could not be a prototype](docs/findings/41-a-function-could-not-be-a-prototype.md)
- **Finding 42** — [42. ✅ Iterative RegExp bytecode VM, CSS L4 cascade/selectors/units, CSSOM `el.style` shorthands, and HTML5 date/time inputs](docs/findings/42-iterative-regexp-bytecode-vm-css-l4-cascade-selectors.md)
- **Finding 43** — [43. ✅ Real-Web Media & Vector Rendering: standalone JPEG/GIF decoders, upgraded SVG & Canvas 2D](docs/findings/43-real-web-media-vector-rendering-standalone-jpeg-gif.md)
- **Finding 44** — [44. ✅ `<img src>` bypassed document-origin security, and unloaded images without dimensions laid out as 100×100 boxes](docs/findings/44-img-src-bypassed-document-origin-security-and-unloaded.md)
- **Finding 45** — [45. ✅ Worker MIME/module policy, EventTarget error dispatch, and shutdown `SIGSEGV`](docs/findings/45-worker-mime-module-policy-eventtarget-error-dispatch-and.md)
- **Finding 46** — [46. ✅ The tokenizer's repeated-attribute-name check was quadratic](docs/findings/46-the-tokenizer-s-repeated-attribute-name-check-was.md)
- **Finding 47** — [47. ✅ A string built half a surrogate pair at a time cost the square of its length](docs/findings/47-a-string-built-half-a-surrogate-pair-at-a-time-cost-the.md)
- **Finding 48** — [48. ✅ Nested browsing contexts (`<iframe>`), cross-realm DOM wrappers, cross-origin `WindowProxy` isolation, and `.xhtml` script execution (Phase 42)](docs/findings/48-nested-browsing-contexts-iframe-cross-realm-dom-wrappers.md)
- **Finding 49** — [49. ✅ `new.target` was the function that was running](docs/findings/49-new-target-was-the-function-that-was-running.md)
- **Finding 50** — [50. ✅ Unicode escapes in JavaScript identifiers and Unicode 17.0 identifier validation (Phase 44)](docs/findings/50-unicode-escapes-in-javascript-identifiers-and-unicode-17.md)
- **Finding 51** — [51. ✅ Promise combinators resolved values only after consuming the iterable](docs/findings/51-promise-combinators-resolved-values-only-after-consuming.md)
- **Finding 52** — [52. ✅ Canvas 2D could draw every shape except text](docs/findings/52-canvas-2d-could-draw-every-shape-except-text.md)
- **Finding 53** — [53. ✅ A page was read as UTF-8 whatever it said it was (Phase 47)](docs/findings/53-a-page-was-read-as-utf-8-whatever-it-said-it-was-phase.md)
- **Finding 55** — [55. ✅ `<select>` content and `<selectedcontent>` cloning followed pre-2025 rules (Phase 49)](docs/findings/55-select-content-and-selectedcontent-cloning-followed-pre.md)
- **Finding 56** — [56. ✅ CSS Color 4/5 spaces, gamut mapping, `color-mix()`, relative colors, and `light-dark()` (Phase 50)](docs/findings/56-css-color-4-5-spaces-gamut-mapping-color-mix-relative.md)
- **Finding 57** — [57. ✅ Attributes were keyed by qualified name; `Attr` was not an object; collections had no named own properties (Phase 51)](docs/findings/57-attributes-were-keyed-by-qualified-name-attr-was-not-an.md)
- **Finding 58** — [58. ✅ `main` did not build after the Phase 49 and Phase 51 merges (Phase 52)](docs/findings/58-main-did-not-build-after-the-phase-49-and-phase-51.md)
- **Finding 59** — [59. ✅ Completing a surrogate pair after reading or slicing copied the string (Phase 53)](docs/findings/59-completing-a-surrogate-pair-after-reading-or-slicing.md)
- **Finding 60** — [60. ✅ WebP was a hole in the image pipeline (Phase 54)](docs/findings/60-webp-was-a-hole-in-the-image-pipeline-phase-54.md)
- **Finding 61** — [61. ✅ RegExp lacked Unicode 17.0 property escapes, `v`/`d` flags, ES2025 modifiers / duplicate named groups / `RegExp.escape()`, parse-time literal validation, and `Symbol.*` dispatch (Phase 55)](docs/findings/61-regexp-lacked-unicode-17-0-property-escapes-v-d-flags.md)
- **Finding 62** — [62. ✅ Every cross-origin `Window` and `Location` described itself with the ordinary own-keys walk (Phase 56)](docs/findings/62-every-cross-origin-window-and-location-described-itself.md)
- **Finding 65** — [65. ✅ Derived constructor `this` before `super()` and `let`/`const`/`class` TDZ (Phase 59)](docs/findings/65-derived-constructor-this-before-super-and-let-const.md)
- **Finding 66** — [66. ✅ String lacked Unicode 17.0 normalization, full/locale case mapping, and CLDR 48 UCA collation (Phase 60)](docs/findings/66-string-lacked-unicode-17-0-normalization-full-locale.md)
- **Finding 69** — [69. ✅ An array and a string were not asked for their iterator (Phase 63)](docs/findings/69-an-array-and-a-string-were-not-asked-for-their-iterator.md)
- **Finding 75** — [75. ✅ An upgrade ran the constructor on the element itself (Phase 69)](docs/findings/75-an-upgrade-ran-the-constructor-on-the-element.md)
- **Finding 83** — [Async `yield*` used the sync iterator protocol (Phase 77)](docs/findings/83-async-generator-delegation.md)
- **Finding 85** — [85. ✅ Animated WebP was refused by a decoder that could have drawn it (Phase 79)](docs/findings/85-animated-webp-was-refused-by-a-decoder-that-could-have-drawn-it.md)
- **Finding 89** — [WebSocket secure URLs silently used plaintext](docs/findings/89-websocket-handshake-security.md)
