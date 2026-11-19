# 🦅 Litebird Browser Engine

> A lightweight, modular browser and JavaScript engine built from scratch in **C++20**.
> Designed for learning, experimentation, and building custom web engines without massive legacy dependencies.

---

## 📊 Conformance scoreboard

Phase 15 replaced the hand-rolled HTML parser with the WHATWG tree-construction
algorithm and added a way to *measure* the engine instead of eyeballing it.
Phase 17 then spent the scoreboard: it drove the number from 88.0% to 98.8% by
fixing what the corpus actually disagreed with, and audited the harness itself
(see "What the scoreboard measured wrong" below). Phase 18 moved back to
*layout* -- percentage lengths and `overflow` clipping -- which the parser
scoreboard cannot see at all. Phase 26 compared the tree builder with another
parser on pages nobody wrote, which found what the corpus has no case for
(`<br/>` closed the paragraph it stood in) and took the number to 99.4% on
the way (see "Pages nobody wrote"). Phase 27 did for the DOM a script sees
what Phase 23 did for the language: a corpus recorded from another
implementation first, then new bindings written against it (see "The DOM
scoreboard"). Phase 28 wrote the XML parser the DOM lacked, against the W3C's
suite and two other parsers, and brought in the tests browsers share (see
"XML"). Phase 49 implemented customizable `<select>` content and
`<selectedcontent>` cloning, taking the html5lib tree-construction suite from
99.4% to **100.0% (`1792 / 1792`)** on the copy the repository held. Phase 76
vendored the copy web-platform-tests actually publishes — 62 files, **1,973
cases**, with `#description` sections and a `processing-instructions.dat` the
old copy never had — and made the parser emit the processing instruction the
standard now makes, which is 1,967 of the 1,973.

| Metric | Value | How it is measured |
| :--- | :--- | :--- |
| **DOM tree-construction conformance** | **1972 / 1973 (99.9%)** | the corpus as web-platform-tests publishes it (`html/syntax/parsing/resources/`, vendored at `37cfee8`, 62 files), run by `make conformance`. The six that fail are in `scripted_*.dat`, where a script must edit the document while the page is parsed |
| Trees for random pages | 6,000 of 6,000 equal to the ones parse5 builds once nine rules of parse5 are corrected to the standard; 5,951 equal to parse5 as published | `make diff-trees` (needs Node.js) |
| **JavaScript corpus** | **1137 / 1156 (98.4%)**; of the 19 that fail, eight are what a function or a class still lacks (Phase 34), seven are iteration gaps (Phases 63 and 94), and one each is typed arrays, `Proxy`, `BigInt` and `typeof` | `tests/js-conformance`, expected output recorded from Node.js, run by `make js-conformance` |
| **DOM corpus** (what a script sees) | **678 / 678**; 61 of 572 before Phase 27 | `tests/dom-conformance`, expected output recorded from jsdom, run by `make dom-conformance` |
| **web-platform-tests** (`dom/nodes`, `domparsing`, `encoding`, `css-flexbox`, `css-grid`) | **9567 / 9824 subtests** across 190 files; the 257 others listed with a reason (0 `frames` failures since Phase 42). 1,127 before Phase 28, 5421 before Phase 47, 7998 before CSS Grid/Flexbox parsing, 9550 before the CSS Object Model of Phase 72 | `tests/wpt`, the tests browsers share, with their own harness, run by `make wpt` |
| **XML parser** | **1718 / 1718** of the W3C suite's documents that apply; 2,585 run (1702 until Phase 47 read the last 16 encodings) | downloaded by `make xml-conformance` |
| **Legacy encodings** | **40 encodings, 228 labels, every byte of every index table** (including the Big5 two-code-point entries and the `gb18030` ranges), generated from `whatwg/encoding` `@ be17ac5c49881336538acfc7b40dba3c8009144b` | `include/litebird/net/TextEncoding.hpp`, `make test`; pages in these encodings run by `make wpt` |
| XML against Expat and libxml2 | 4,000 generated documents, none read otherwise than both read it; 3,000 serialized trees, all read back the same | `make diff-xml` (needs `pip install lxml`) |
| **URL parsing and setters** | **1188 / 1188 (100%)**; 2 before Phase 27 | `tests/url`, the web-platform-tests data, run by `make url-conformance` |
| Character/entity table | 2231 named references (complete) | generated from the WHATWG table |
| Engine unit tests | 260 suites + 114 cross-origin/channel checks, all passing | `make test` |
| **Shared `ComputedStyle` & enum compaction** | `LayoutBox` (1,320 → **344 B**) and `InlineFragment` (1,064 → **88 B**) hold a 16-byte `SharedComputedStyle` deduplicated per layout build (`ComputedStylePool`), with copy-on-write percentage resolution and `uint8_t` enums/bitmasks (Phase 71, finding 77). Peak RSS on 100k `word <b>bold</b> ` drops **1,382.3 → 352.0 MiB** (-74.5%) with identical paint dumps | `make bench-styles`; `make test` |
| **Map/Set iterator cursors** | A completed iterator unregisters its remapping cursor immediately; `new_cursor()` prunes abandoned partial iterators only at geometric registry sizes. Node-recorded cases keep two iterators live across compaction, insertion, `clear()`, and `done`; in two equal 8,192-loop batches in one process with no `gc()`, the later Map and Set batch may take no more than 2.25× the earlier one plus 40 ms, and both batches must sum every value | `make verify-collection-iterators`; `make bench-collections` |
| **Per-glyph font fallback** | A codepoint the primary face does not cover is drawn from the next discovered system face that has it, loaded lazily (Phase 64). The oracle is the measured `cmap` coverage of two faces, not an assumption: `'A'` is in both DejaVuSans and the vendored Ahem, `'Ā'` in DejaVu only, `'一'` (`U+4E00`) in Ahem only | `make test`, suite `per_glyph_font_fallback` |
| **`font-family` selection** | A CSS family list is resolved against the face names the host's own fonts declare, per element, and the winning face is loaded lazily and used to measure and draw (Phase 67). The oracle is absolute rather than a comparison: every glyph in the vendored Ahem is exactly 1em wide, so four characters at 16 px measure 64.0 px in it and in nothing else. `monospace` is checked by measurement too — `'i'` and `'W'` advance the same there and not in a proportional face | `make test`, suites `font_family_selects_a_face`, `font_family_reaches_layout_from_css` |
| **`@font-face`** | A page brings its own typeface: the rule's `font-family` names a family the host may never have heard of, and the `src` list is fetched lazily, in order, until a source parses (Phase 74). The oracle is the vendored Ahem served through a `data:` URL, so the bytes arrive the way a browser's would — every glyph in it is exactly 1em wide, so four characters at 16 px lay out at 64.0 px or the rule did not take. A webfont outranks a host family of the same name, `format()` skips what the engine cannot read, and `local()` answers out of the host's own fonts | `make test`, suites `font_face_declares_the_family_the_page_names`, `font_face_brings_the_family_the_page_names` |
| **HTTP/1.1 cookies, RFC 9111 cache & CORS preflight** | Thread-safe RFC 6265bis `CookieJar` (`Domain`/host-only, `Path`, wall-clock `Max-Age`/`Expires`, `Secure`, `HttpOnly` script isolation, `SameSite`, `__Secure-`/`__Host-`) wired to HTTP `Set-Cookie`/`Cookie` across redirects after CORS checks; bounded LRU HTTP cache (`Cache-Control`, `ETag`/`If-None-Match`, `Last-Modified`/`If-Modified-Since`, `304` merge, `Vary`, unsafe invalidation); WHATWG Fetch CORS `OPTIONS` preflight + `Max-Age` cache, W3C mixed-content blocking, and `HeadersExotic` `fetch()` options (Phase 73) | `make test`, 4 loopback HTTP/1.1 & Fetch suites |
| **HTTP/1.1 keep-alive pooling & Brotli (`br`)** | Thread-safe HTTP/1.1 and HTTPS (`SSL*`) persistent socket pool (`Connection: keep-alive`, `Keep-Alive: timeout/max`, bounded 4 per key / 32 total, incremental chunked/CL framing, `poll` + `SSL_peek` stale eviction, safe idempotent EOF retry) and pure-C++20 RFC 7932 Brotli (`br`) decoder (`Accept-Encoding: gzip, deflate, br`, right-to-left stacked `Content-Encoding`, `max_output_bytes` bomb cap); **53** RFC/Node streams across qualities 0–11 and `lgwin` 10–24, **49** bomb-cap checks, **6** invalid headers, and **2,000** mutation fuzz inputs (`0` discrepancies vs Node `zlib`) (Phase 85, finding 91) | `make verify-brotli`, `make fuzz-brotli`, `make test` |
| **`font-style: italic`** | `em`, `i`, `cite`, `dfn`, `var` and `address` are italic, and so is anything a page slants itself. A family that has a slanted face is asked for it; a family that has none is *sheared*, which is what CSS asks for and what a browser does — 0.25 px of run per px of height, the 14° an `oblique` with no angle means (Phase 78). The oracle is the shape of the ink rather than a font, because the machine has no italic face at all: at 40 px `'H'` leans 8 px from its bottom row to its top row slanted and 0 px upright. The advance does not change, so an italic line measures exactly as wide as the upright one | `make test`, suites `font_style_italic_reaches_the_painter`, `font_style_italic_slants_the_glyphs_it_draws`, `a_family_with_an_italic_face_is_not_sheared` |
| **`font-weight` and the font's own tables** | A face's weight and slant are read from the file — `OS/2.usWeightClass`, `OS/2.fsSelection` bit 0, `head.macStyle` bit 1 — not guessed from the words in its subfamily name, and the weight a page asks for reaches the registry instead of being flattened to `bold`/`not` (Phase 87). Matching follows CSS Fonts 4 §5.2.1's search order rather than nearest distance, and `bolder`/`lighter` are relative to the parent. The oracle is a file that disagrees with itself: the vendored Ahem with its `OS/2` rewritten to 700 while its name still says "Regular", one table field changed per test. A family of two faces at 200 and 500 answers 350 with the 200 and 600 with the 700 — which no bool can express | `make test`, suites `a_face_weight_comes_from_the_font_not_its_name`, `font_weight_matching_follows_the_css_order`, `bolder_and_lighter_are_relative_to_the_parent` |
| **`kern` pair kerning** | The font's own `kern` table is read — version 0, format 0, the horizontal pair list — and its adjustment is counted by `measure_text` and applied by `draw_text` and the canvas painter, so a line measures the width it draws (Phase 91). The oracle is the file, not the engine: the test parses DejaVu Sans's 2,727 pairs itself and requires the engine to report the same unit value scaled by the face's own `units_per_em` (2,048) — `AV` is −131 units, −6.396 px at 100 px. Kerning **opens** pairs as well as closes them (`AA` is +57 units), so nothing assumes it only tightens, and a face with no table is left alone: Ahem's `AV` at 100 px is exactly 200.000 px. Measuring and painting are compared in pixels: the engine's `AV` must be identical to the two glyphs placed by hand at the kerned positions and *not* to the unkerned ones | `make test`, suites `kerning_closes_the_pairs_the_table_names`, `a_face_without_a_kern_table_is_not_kerned`, `the_painter_closes_the_same_pairs_the_measurer_counted` |
| **`GSUB` `liga`/`rlig` ligatures** | The font's own `GSUB` is read — the `latn` script (or `DFLT`), the `liga` and `rlig` features, type-4 ligature lookups, following type-7 extension wrappers — and applied to each run of glyphs of one face, longest match first (Phase 93). `ff`, `fi`, `fl`, `ffi` and `ffl` become one glyph each in DejaVu Sans, and the engine measures and paints that glyph: `ff` at 100 px is 68.896 px where its letters are 70.410. Measurement, the text painter and the canvas painter all walk one shaped list, so a canvas string measures the ligature it paints. The oracle is the file — a `GSUB` reader in the test says what each string becomes — plus ink: `fi` must be the same 2,491 pixels as `U+FB01` and not the 2,404 of `f` + `i` | `make test`, suites `ligatures_join_the_glyphs_the_table_names`, `a_ligature_is_drawn_as_the_glyph_the_table_names`, `the_canvas_measures_the_ligature_it_paints` |
| **`font-kerning`** | The property a page uses to switch the pair adjustments off, and the first text property here whose value changes how wide a run *is* rather than how it looks — so it has to reach layout and the painter together (Phase 97). `none` is carried from the computed style to the text box, to the `DrawText` command, to the pen; `auto` and `normal` both kern, which is what `auto` means. The oracle is the same pair measured four ways: `AV` at 100 px is 130.420 px kerned and 136.816 px with the property off — the plain sum of the two advances — a `<p>` laid out in each style differs by the same 6.40 px, and painted at 100 px the unkerned pair ends 6 columns further right. The `font` shorthand resets it to `auto`, per CSS Fonts 4, which exposed a cascade bug: the shorthand used to be applied in a pass of its own, ahead of the rest of the block, so declaration order did not hold for anything it touched (finding 103) | `make test`, suites `font_kerning_none_reaches_the_box_and_the_painter`, `kerning_none_measures_the_plain_advances`, `the_font_shorthand_resets_font_kerning`, `the_painter_honours_font_kerning_none` |
| RegExp Unicode 17.0 & ES2025 | **1,849 / 1,879 (98.4%)** test262 `built-ins/RegExp` (up from 739/1,879) + **237 / 240 (98.8%, 0 fail)** `language/literals/regexp` + **333 / 339 (98.2%)** `String.prototype/{match,matchAll,replace,replaceAll,search,split}`; **588 / 588** Node.js differential + fuzz (`0` discrepancies) & **112 / 112** ES2025 spec checks; `make diff-strings` **50,509 / 50,580** (`0` RegExp diffs) | `make verify-regexp`, `make diff-strings`, `tools/conformance/run_test262.py` |
| String Unicode 17.0 & CLDR 48 | **50,580 / 50,580 (0 diffs)** in `make diff-strings` (down from 71); **12,255,232** all-code-point (`1,114,112 × 11` ops) + **1,112** curated/fuzz strings + **1,038** collation pairs + **400,680** UCD `NormalizationTest.txt` assertions + **206,297** CLDR `CollationTest_CLDR` pairs = **0 diffs**; test262 String Unicode/locale 9 dirs **152 / 156 (97.4%)** (up from 105/156), full `built-ins/String` **1,199 / 1,223 (98.0%)** | `make diff-strings`, `make verify-string-unicode`, `tools/conformance/run_test262.py` |
| Worker MIME/static-module & lifecycle | 17 end-to-end checks (HTTP/data MIME policy, a `{type:"module"}` static import graph, classic `importScripts` boundary, `addEventListener("error")`, 60/60 4-worker exit runs) | `make security-worker` |
| WebSocket TLS, Fetch policy & RFC 6455 | 29 end-to-end checks against OpenSSL 3 CA/SAN/IP/expired TLS peers, Fetch `Origin` & mixed-content policy across `https:`/`http:`/`file:`/Worker/sandboxed `iframe`, relative URLs, WHATWG validation/close semantics, and 60/60 concurrent 4x `wss://` exit runs (Phase 90, finding 96) | `make security-websocket`; `make test` |
| CSS Color 4/5 oracle & fuzzing | 180/180 15-space conversions vs exact-rational Python W3C §17 oracle (`max err 5.04e-08`), 13/13 §13.2 OKLCH gamut map, 133/133 valid & 61/61 invalid WPT vectors (4,860/4,860 live upstream WPT `color-computed-*.html`), 5,008 adversarial fuzz inputs (0 crashes) | `make verify-color`, `make fuzz-color` |
| Media & vector oracle | 21 JPEG variants vs libjpeg (52–99 dB PSNR), 9 GIF variants (bit-exact `max_diff=0` vs ImageMagick `-coalesce`), 27 lossless WebP checks bit-exact vs libwebp (plus refusals), 26 lossy WebP (`VP8`) checks — every pixel of 11 streams equal to libwebp's, the loop filter and the `ALPH` plane checked separately — 16 animated WebP checks (metadata, four composited frames, an opaque animation against ImageMagick `-coalesce`, a six-frame transparency/blending/disposal animation, the first frame, the frame cap, determinism, six refusals), 7 SVG checks (including positioned text and tspan); 2,400 mutated fuzz cases (0 findings). The compositor itself was compared with libwebp 1.5's animation decoder over 658 files and 2,262 frames, every one byte-identical (Phase 79) | `make verify-media`, `make fuzz-media` |
| Fuzz robustness | 400 parser mutations, 400 layout/render/image runs, 600 mutated XML documents, 1,927 whole XML roundtrips, and 2,400 mutated JPEG/GIF/WebP/SVG inputs: 0 findings | `make asan` (ASan + UBSan) |
| Deep and oversized pages | 165 pages that nest or repeat one thing up to 200,000 times + 300 random pages at the parser's nesting limit: 1,332 runs, 0 failed | `make deep` (8 MB stack, 2 GB, 60 s a run) |

```bash
make conformance     # HTML: per-file pass rate + failing-case diffs
make js-conformance  # JavaScript: per-file pass rate
make test262         # 21 directories of the standard's own suite, downloaded; for comparing two builds, not a score
make dom-conformance # the DOM a script sees: per-file pass rate
make url-conformance # URL parsing and setters (web-platform-tests data)
make xml-conformance # the XML parser: the W3C suite, downloaded the first time
make wpt             # web-platform-tests for dom/nodes and domparsing, with testharness.js
make diff-xml        # generated XML read here, by Expat and by libxml2; serialized trees read back
make test            # 260 engine suites + 114 cross-origin Window/Location/channel checks
make check-numbers   # the numbers above still divide and still count (tools/ci/check_numbers.py)
make verify-regexp   # RegExp Unicode 17.0 / v-mode / d-mode / ES2025 differential & spec verification
make verify-string-unicode # all 1,114,112 code points x 11 ops + curated/fuzz strings & collation + ECMA-402 checks vs Node.js
make bench-floats    # indexed sized-float timing guard: 1k and 10k, mixed sides/clear
python3 tools/bench/bench_sized_floats.py build/litebird --out build/floats-after --sizes 1000 3000 10000
# Compare against the pre-change binary as described in ARCHITECTURE.md Phase 48.
make bench-collections # Map/Set completed-iterator growth guard, two 8,192-loop batches without gc()
make bench-styles    # 100k repeated inline words/links/spans under 500 MiB peak RSS (`word <b>bold</b>` was 1,382 MiB)
make verify-color    # CSS Color 4/5 15-space rational oracle, §13.2 gamut map, and WPT vectors
make fuzz-color      # adversarial grammar & mutation fuzzing for CSS Color 4/5
make verify-brotli   # pure C++20 RFC 7932 Brotli vector, bomb-cap, and Node zlib differential suite
make fuzz-brotli     # 2,000-input truncation, bit-flip, random-byte, and bomb-cap Brotli fuzzer
make verify-media    # JPEG vs libjpeg, GIF vs ImageMagick -coalesce, lossless WebP vs libwebp, and SVG conformance
make fuzz-media      # mutation fuzzing for JPEG, GIF, WebP, and SVG decoders/renderer
make deep            # pages nested 200,000 deep, oversized bitmaps, very long runs of one thing
make asan            # sanitizer build, tests, the JavaScript, DOM, URL and XML corpora, web-platform-tests, a fuzz run and the deep pages
make tsan            # the string type's stress tests under ThreadSanitizer, and under ASan with leak checking
make bench           # string-heavy scripts timed against Node; fails if one is wrong or takes over 3 s
make bench-css       # CSS whitespace runs: same paint as compact pages, each under 3 s
make bench-attributes # attribute-list end removals: expected DOM checksums, each under 3 s
make bench-tokenizer-attributes # the HTML tokenizer at 50,000 attributes: expected trees, each under 1 s
make bench-surrogates # six surrogate-pair scripts (including reads and substring slices between halves): results face Node, each under 1 s
make verify-promise-iterators # 34 async cases: each iterator must be closed promptly on a thrown resolve/then
make verify-collection-iterators # Map/Set compaction, clear, insertion-order, and done-latch cases from Node
make security-fetch  # page fetch() cannot read arbitrary local files; file: documents keep local access
make security-worker # Worker MIME/static-module policy, EventTarget dispatch, classic importScripts boundary, and 60x startup/exit stability
make security-websocket # WebSocket wss:// TLS (CA/SAN/IP), Fetch Origin & mixed-content policy, API validation, and 60x concurrent exit stress
make diff-strings    # 50,580 string results compared with Node's; fails if any differ (ceiling: 0)
make diff-trees      # 6,000 random pages and fragments parsed here and by parse5; fails if a tree differs
```

### The JavaScript scoreboard

`tests/js-conformance/` holds 1,156 small scripts. Each one records what Node.js
prints for it, and a case passes when `litebird --run-js` prints the same and
exits 0. CI re-runs the whole corpus under Node first, so an expectation
cannot be edited to match what Litebird does.

Read the number for what it is:

- **It is not test262.** The corpus was written alongside the engine. It
  covers the language and library features ordinary scripts use (syntax,
  closures, classes, destructuring, generators, promises and async functions,
  strings, arrays, regular expressions, dates, JSON), not every clause of the
  specification. The official suite scores lower: of the 47,967 tests in
  its `built-ins`, `language` and `annexB` directories that are not modules,
  31,161 pass (65.0%; 29,450 / 61.4% last recorded before Phase 88, 27,872 /
  58.1% before Phase 55, 26,012 / 54.2% before Phase 44); of the 15,686
  on functions, classes and objects, 13,654 pass (87.0%; 12,657 / 80.7%
  before Phase 88, 12,359 / 78.8% before Phase 59, 10,835 / 69.1% before
  Phase 44). `make test262` runs the
  latter. It is used for what a change moved between two builds, not as a
  score (Phase 34: 108 more pass and none fewer in those directories; Phase
  44: 1,440 more pass and none fewer in those 21 directories, plus 374/374 =
  100.0% in `language/{identifiers,keywords,future-reserved-words,reserved-words}`;
  Phase 43: 43 more in the whole suite and none fewer; Phase 59: 43 more in
  those 21 directories and 49 more across 13 TDZ/scope directories, none
  fewer; Phase 63: 10 more in the whole suite and 4 fewer, each of the four
  a row of the task table).
- **Five of the 24 failures are whole features that do not exist**: BigInt
  (2 cases), `Proxy`, typed arrays, and holes in arrays. The other 19 were
  recorded to fail. Eight are in `prototypes.dat`: what classes still lack
  after Phase 34 (`Symbol.species`, `Symbol.hasInstance`, and six smaller
  things; ARCHITECTURE.md, Phase 34, section 9). Eleven are in
  `iteration.dat`: the places that still read an array without asking it
  for its iterator (array destructuring, `Array.from`), the default constructor
  spread, and what built-in iterators get wrong (Phase 63, section 6).
  Each of the 19 belongs to a row of the task table in
  `AGENTS.md`. `construct.dat` had one until Phase 65: `eval` inside a
  function saw nothing of the function, and now runs in its caller's scope
  ([`docs/phases/65-direct-eval-scope.md`](docs/phases/65-direct-eval-scope.md);
  `this` before `super()` was fixed in Phase 59).
- **It measures the language, not the DOM.** The bindings a page script calls
  (`document`, elements, events) are a much thinner layer; see "Known gaps" in
  `ARCHITECTURE.md`.

The engine as it was before Phase 23 passes 140 of the 801 cases the corpus
had when it was replaced, and 25 of the 90 cases in the fixed "everyday" probe
(`everyday-es5.dat` and `everyday-es2015.dat`), which now passes 90/90.

Four other checks sit behind the number. A differential run evaluates 67,545
generated expressions in Litebird and in Node and compares the results: 99
(0.15%) differ, mostly the last digit of `Math.atan2`, `cbrt` and `pow`.
`make diff-strings` does the same for strings (36 subjects through every
string method and 51 regular expressions): all **50,580 of 50,580 results**
equal Node's (**0 differ**; 95 differed before Phase 55's Unicode RegExp work,
and 71 before Phase 60's Unicode 17.0 normalization, full/locale case mapping,
and CLDR 48 root collation).
63 of the corpus cases (`deep-nesting.dat`) hand the engine input nested
200,000 deep, which used to end the process and must now end in an error the
script can catch. And 34 (`long-strings.dat`) loop over, build or take apart
strings of 100,000 characters and more, work that used to grow with the
square of the length: reading a string copied it. The engine before that fix
passes 12 of the 34 at best and times out on 15 (AUDIT.md, finding 25).

76 cases (`prototypes.dat`, Phase 34) are about a function that stands in a
prototype chain: the parent of a class, the prototype of an object,
`Function.prototype` itself. Three of them are whole programs as Babel and
TypeScript compile classes for older browsers, which stopped at their first
class before. 68 of the 76 pass; the engine before Phase 34 passes 7.

50 cases (`construct.dat`, Phase 43 / Phase 59) are about what a constructor
makes: `new.target` in a parent's constructor and under `Reflect.construct`, an
object a parent's constructor returns, when `this` is bound (including TDZ
checks before `super()`), and three more programs as Babel and TypeScript
compile classes. 49 of the 50 pass; the engine before Phase 43 passes 13.

78 cases (`iteration.dat`, Phase 63) are about who decides how a value is
iterated: an iterator a script put on an array, on a subclass of `Array`, on
`Array.prototype` or on `String.prototype`, through each construct that
iterates; the order in which the protocol reads and calls; closing an
iterator that is left early; and the plain cases, which must not change. 67
of the 78 pass; the engine before Phase 63 passes 28, and two do not end.

1,972 of the 1,973 cases of the html5lib tree-construction suite pass (**1972 / 1973**); the one that does not, `scripted_ark.dat`, differs by a formatting element the adoption agency reconstructs once too often. The six `scripted_*.dat` cases used to fail because the dump path ran no script; since Phase 95 it runs the page's scripts as the parser reaches them, and `document.write` writes into that parser;
`python3 tools/conformance/run_tree_construction.py --filter NAMEFRAGMENT --show 5`
inspects any subset. The final 11 cases (fixed in Phase 49) were all `<select>`:
treating `<select>` as a default scope boundary, keeping arbitrary HTML/SVG/MathML
content inside `<select>` with formatting-element reconstruction, and cloning the
selected `<option>`'s children into `<selectedcontent>`.

### The DOM scoreboard

`tests/dom-conformance/` holds 678 cases: a page, a script, and what the
script prints in [jsdom](https://github.com/jsdom/jsdom) 31.0.0, an
independent implementation of the DOM and HTML standards. A case passes when
`litebird --run-script` prints the same and exits 0. CI runs the whole corpus
in jsdom first (`--reference`), so an expectation cannot be edited to match
what Litebird does.

```bash
make dom-conformance
python3 tools/conformance/run_dom_conformance.py --show 20 --filter forms
python3 tools/conformance/compare_with_jsdom.py page.html script.js   # any page, any script: where do the two differ?
```

| | Passed |
| :--- | ---: |
| Before Phase 27 (`main` at `6f08a70`) | 61 / 572; the everyday probe 13 / 68 |
| Phase 27 | 572 / 572; the everyday probe 68 / 68 |
| Phase 28, with 57 more cases (`xml.dat`, `xhr.dat`) | 629 / 629 |
| Phase 34, with 11 cases (`interfaces.dat`: an interface object has its parent as its prototype) | 640 / 640 |
| Phase 37, with 4 more cases (`forms.dat` date/time inputs, `style.dat` CSSOM `el.style` shorthands) | 644 / 644 |
| Phase 43, with 25 more cases (`compiled.dat`: custom elements compiled for older browsers, made and upgraded) | 669 / 669 |
| Now, with the 9 cases of Phase 69 (`upgrades.dat`: what an upgrade's construction is) | 678 / 678 |

`everyday.dat` is the fixed probe: 68 snippets adapted from
[You Might Not Need jQuery](https://github.com/HubSpot/YouMightNotNeedjQuery),
which is what plain DOM code on real pages looks like. It was written before
any of the new code and is never edited. The 20 other files of Phase 27 go through the
DOM by area: nodes, elements, attributes, `classList`, `dataset`, collections,
selectors, markup (`innerHTML` and friends), text, style, ranges, events,
forms, mutation observers, custom elements and shadow trees, the document,
the window, loading, errors, and 16 small programs (a to-do list, a router on
the hash, a form with validation).

The bindings were rewritten for it (`src/js/Dom*Bindings.cpp`, 13,700 lines):
every DOM object has its interface's prototype, so `el instanceof HTMLElement`,
`Object.getPrototypeOf(document.body) === HTMLBodyElement.prototype`,
`class MyEl extends HTMLElement` and patching `Element.prototype` all work;
a node has one object for as long as it lives; collections are live; selectors
are the real grammar; and errors are the `DOMException`s the standards name.
A page's scripts run while it is parsed, so a script sees the page up to
itself. ARCHITECTURE.md, Phase 27, has the design.

Four things about the number:

- **It was 100% before it was right.** At 557 of 557, `getComputedStyle`
  answered `""` for every property nobody had declared and took the last
  rule that matched whatever its specificity, and a `Range` did not follow
  changes to the tree. No case had asked: the corpus was written by the
  hand that wrote the code. Both were found by running pages that were not
  in the corpus through jsdom and Litebird side by side
  (`compare_with_jsdom.py`), and became fifteen cases. A full score says
  that nothing is known to be wrong among what was asked.
- **It is 100% of what the corpus asks, not of the DOM.** jsdom does no layout
  and loads no other page, so nothing here checks `getComputedStyle(el).width`
  layout resolution, scrolling or navigation. What is known to be
  missing or wrong is listed in ARCHITECTURE.md (Phase 27, section 12,
  Phase 28, section 10, and Phase 37): `document.write`, the SVG DOM
  (`className.baseVal`, `viewBox`, `getBBox()`), and more. The style sheet
  objects (`sheet.cssRules`, `insertRule`) are in since Phase 72; the corpus
  still cannot record them, because jsdom does not implement them either, so
  `cssom_sheets_rules_insert_amounts_and_fonts` in `tests/test_suite.cpp`
  holds them.
- **jsdom is the reference, not the authority.** Where it differs from the
  standards, the corpus stays away and a unit test asserts the standard's
  answer (`tests/dom-conformance/README.md` names the places).
- **URLs are measured separately**, with the data browsers use:
  `tests/url/` is `urltestdata.json` and `setters_tests.json` from
  web-platform-tests, 1,188 cases, all of which pass. Host names with
  characters outside ASCII go through Unicode's UTS #46 (`make url-conformance`
  enforces 1,188); `tests/url/README.md` says exactly what is checked.

### XML

Until Phase 28 `new DOMParser().parseFromString(text, 'application/xml')`
gave an HTML document. There was no XML parser: a CDATA section became a
comment, text that was not well formed gave a tree like any other, and
`XMLHttpRequest` failed every request, so `responseXML` was always null.

| A script on a page | Before | Now |
| :--- | :--- | :--- |
| `d = parseFromString('<a xmlns="urn:x"><b id="k">t</b><![CDATA[<c>]]></a>', 'application/xml')`, then `d.documentElement.nodeName`, `.namespaceURI`, `d.contentType` | `HTML`, `http://www.w3.org/1999/xhtml`, `text/html` | `a`, `urn:x`, `application/xml` |
| `new XMLSerializer().serializeToString(d)` | `<html xmlns="http://www.w3.org/1999/xhtml"><head /><body><a xmlns="urn:x"><b id="k">t</b><!--[CDATA[<c-->]]&gt;</a></body></html>` | The text that went in |
| `parseFromString('<a><b></a>', 'application/xml').documentElement.nodeName` | `HTML` | `parsererror` |
| `x.open('GET', 'data:text/xml,<a><b>t</b></a>'); x.send()` | `error`, `loadend`; status 0; `responseXML` is null | `loadstart`, `load`, `loadend`; status 200; `responseXML.querySelector('b').textContent` is `t` |

The parser (`src/xml/XmlParser.cpp`) is XML 1.0, Fifth Edition, with
namespaces, and it is what a browser's is: it does not validate, it reads
nothing outside the text it is given, and it bounds what entities and
attribute defaults may expand to. On top of it: XML documents in the DOM
(namespaces on elements and attributes, CDATA sections, processing
instructions, the `parsererror` document),
the standard's XML serializer for `XMLSerializer` and for `innerHTML` in an
XML document, and `XMLHttpRequest` for `data:` URLs.

It was written against references, and each has a command:

| Reference | Result | Command |
| :--- | :--- | :--- |
| The W3C XML Conformance Test Suite | 1718 / 1718 of the documents that apply to such a parser; all 2,585 are run, none crashes it | `make xml-conformance` |
| Expat 2.5.0 and libxml2 2.14.6, on 4,000 generated documents, half of them damaged | 3,958 read as both read them, 13 on which the two disagree, 29 with a version number outside the grammar (refused), 0 that differ | `make diff-xml` |
| Itself: 3,000 trees made to break a serializer, written and read back | 3,000 come back the same tree, read by Litebird and by Expat | `make diff-xml` |
| web-platform-tests, `dom/nodes`, `domparsing` and `encoding` (103 files, with `testharness.js`) | 7968 / 8009 subtests; each of the 41 others is listed with a reason | `make wpt` |
| jsdom 31.0.0 | 57 recorded cases, all pass | `make dom-conformance` |
| Nothing: the W3C suite's documents with bytes changed, cut short and pasted over, in the sanitizer build | 600 mutants and 1,927 documents as they are: no crash, no sanitizer report, and every document the same tree after it is written and read | `make asan` (`tools/fuzz/fuzz_xml.py`) |

```bash
python3 tools/conformance/run_xml_conformance.py --show 20      # the failures, if there were any
python3 tools/conformance/run_xml_conformance.py --reference    # Expat and libxml2 on the same suite: 1,404 and 1,697 of 1,718
python3 tools/conformance/run_wpt.py --filter XMLSerializer --show 20
python3 tools/fuzz/diff_xml.py --count 20000 --seed 1
./build/litebird --xml-canonical file.xml                       # exit 0 well formed, 1 not, 3 an encoding it cannot read
```

Five things about these numbers:

- **1,718 is what applies, and the rest is said.** Not counted: 310
  documents whose answer changed with the Fifth Edition, 274 for XML 1.1, 247
  that need an external entity read, 27 where a parser may report an error
  or not, 9 that are not namespace-well-formed on purpose
  (`tests/xml/README.md`). The 16 that were skipped until Phase 47 for their
  encoding are read now, and each is still answered as the suite says. The suite is
  downloaded and checked against a pinned SHA-256; it is not in the
  repository, because its licence does not allow passing it on in another
  form.
- **No reference is the truth everywhere.** Expat does not check a version
  number and libxml2 accepts `version="1."`. Five web-platform-tests
  subtests assert a text of the serializer's standard that has since
  changed, or a fault filed against it, and following them writes documents
  that read back as another tree (265 of the 3,000 above, with one of
  them). jsdom reads an XML response as UTF-8 whatever the document in it
  declares. Where two references
  disagree the standard was read, and the documents say which way it went
  (`tests/wpt/README.md`, `tests/dom-conformance/README.md`).
- **41 web-platform-tests subtests fail** across 103 files (down from 534 once
  Phase 42 added nested frame documents and XML/XHTML frame loading, resolving
  all 495 `frames` failures, and from 42 once Phase 47 read every legacy
  encoding the `encoding/` tests write in): 33 fail on what an `Attr` and a
  `NamedNodeMap` are here, 1 on an upstream WPT `.xhtml` test checking the
  parent frame's `DOMException` constructor (`innerhtml-05.xhtml`), and 7 on
  serializer edge cases documented above (`tests/wpt/README.md`).
  (18 more failed on a fault of the JavaScript engine, a function that could
  not be a prototype, until Phase 34.)
- **`XMLHttpRequest` answers `data:` URLs and nothing else.** No request
  leaves the process or reads a file, and a unit test asserts it. Requests
  over the network are open, and so is something found on the way:
  `fetch()`, which is older code, reads files of this machine for any page
  that asks (`AGENTS.md`).
- **The normal build passed everything while reading freed memory.** The
  first commit CI ran failed in the sanitizer job and nowhere else:
  `XMLHttpRequest` read the charset of a response's type through a pointer
  into a temporary. All unit suites and all 629 cases had passed. It is
  fixed, the line no longer compiles, and the mutation fuzzer in the table
  was written because of it. `make asan` is not the slow twin of
  `make test`.

ARCHITECTURE.md, Phase 28, has the design, what a hostile document costs,
and what is still open.

### Pages nobody wrote

The corpus is 1,973 inputs somebody thought of. `make diff-trees`
(`tools/conformance/diff_trees.py`) makes pages nobody did, a few dozen tags
in random order and most of them misnested, and compares the tree Litebird
builds with the one parse5 builds, an independent parser written from the
same standard. A failing corpus case says a bug exists. A random page says
how often a page meets it, and `--reduce` cuts the page down to a few tags
to read the standard against.

The first run of it, in Phase 25, found five bugs in 6,000 pages. Phase 26
fixed those and then widened what the pages contain, and the wider pages
found more than the first ones had:

| What the pages contain | Trees equal before Phase 26 | Now |
| :--- | ---: | ---: |
| Tags, text and comments | 1,489 of 1,500 | 1,500 |
| The same with what else the tree builder reads: `<br/>`, `type=hidden`, CDATA, U+0000, a stray doctype | 1,371 | 1,500 |
| Those parsed as fragments, which is what `innerHTML` does, in 45 kinds of element | 1,371 | 1,500 |
| Those fragments with scripting disabled | 1,361 | 1,500 |

The worst of what they found is not in the corpus at all, because no case
there writes it: **a void element written the XHTML way closed its parent.**
In `<p>a<br/>b</p>` the "b" landed outside the paragraph, and a
`<link .../>` in the head put the rest of the head, and the body, outside
`<html>`. One line was wrong, and 292 of the 408 wrong pages in the table
come right when only that line is changed. One of the example pages in this
repository was parsed wrongly for that reason, and so was any markup read
back from `innerHTML` and assigned again, because Litebird itself writes
`<br />`.

The fragment rows led to the other thing a page would notice. The parser
was right about fragments for a `<tr>` or an `<svg>`, and `innerHTML` never
asked it: a script's `element.innerHTML = ...` was parsed as the content of
a `<body>` whatever the element was. `table.innerHTML =
"<tr><td>1</td><td>2</td></tr>"` left the text "12" in the table and no
row. It is parsed for its element now. The rest is in AUDIT.md, findings 29
and 30.

Two things about the numbers. The comparison is with a copy of parse5 in
which nine rules are corrected, because parse5 8.1 is itself behind the
standard there (a form inside a `<template>`, CDATA in an SVG `<title>` and
seven more; `diff_trees.py` lists them with the correction, a few lines
each). Against parse5 as published 5,951 of the 6,000 trees are equal and
the other 49 differ for those nine reasons. For four of the nine the
web-platform-tests copy of the corpus has cases that say the same, for two
more Blink's source does, and for the last three there is only the
standard's text (AUDIT.md, finding 30). And `<select>` is left out of the
pages: parse5 has the older rules for it, and so does Litebird.

The check needs Node.js and, the first time, the npm registry to fetch
parse5. CI runs it in the conformance job and fails if a tree differs.

### Pages that are hostile by size

A conformance corpus is made of small pages, and so is what a mutation fuzzer
starts from. Neither finds the oldest way to end a browser from a web page:
nest something deeper than the stack is long, or repeat it until work that
grows with the square of its count never finishes. A file of 300,000 nested
`<div>` (3.3 MB, no script) used to end Litebird with a `SIGSEGV`.

`make deep` generates those pages (`tools/fuzz/deep_pages.py`) and runs each
in a child process with an 8 MB stack, 2 GB of memory and 60 seconds:

| | Before Phase 25 | Now |
| :--- | :--- | :--- |
| Runs that fail, of 392 over 145 pages | 264 | 0 |
| 300,000 nested `<div>` | `SIGSEGV` | renders in 4 s |
| A script that nests 200,000 elements with `appendChild`, reads `textContent` and `innerHTML`, and drops the tree | `SIGSEGV` at 20,000 | 0.7 s |
| One paragraph of 100,000 images | 35 s | 1.3 s |
| `<svg width="100000" height="100000">` | abort (`std::bad_alloc`) | blank box |

Three rules do it. The HTML parser attaches nothing deeper than 512 levels,
the limit Blink and WebKit use: an element that would be deeper becomes a
sibling instead, and none is dropped. A tree built by script may be as deep as
the script likes, so nothing that walks the DOM recurses. Layout and painting
still recurse and stop when the stack is nearly used up. What this does not
cover is listed in AUDIT.md, finding 28: among others, 10,000 floats with a
size in one box still take 46 s, and a block inside an inline box is not laid
out.

### What the scoreboard measured wrong

Phase 17 also fixed three harness bugs that were *mis-scoring the engine*, and
they are worth stating because they change what the number means:

1. **Case splitting.** The runner split `.dat` files on blank lines, but a case
   legitimately contains blank lines (`<pre>` data does). Cases whose data
   contained one were truncated and their expected trees came out empty — a
   guaranteed fail. Cases now split where the corpus format says they do: at a
   `#data` line that starts a case.
2. **Lone CR.** Reading the corpus in text mode turned a lone `\r` into `\n`, so
   a case asserting that a `&#x0D;` reference survives could never pass.
3. **Trailing newline.** The expected tree kept a trailing blank line, which made
   comparisons fail for cosmetic reasons.

Fixing these moved the denominator from 1790 to 1792 cases and was worth ~55
cases on its own. The gain is *measurement honesty*, not new engine code.

The corpus is the same tree-construction material WebKit, Blink, Gecko and
Ladybird use as their parser baseline, so the number is comparable across
engines rather than self-referential. It is vendored under
`tests/html5lib/tree-construction/` (BSD-licensed, see the LICENSE there).

In June 2026 the tree-construction tests moved out of html5lib-tests into
web-platform-tests. Phase 76 vendored that copy — 62 files, 1,973 cases,
`#description` sections and all — at
`web-platform-tests/wpt@37cfee8549b687f1cb0bc8cf772a81ab86de657d`
(`html/syntax/parsing/resources/`). The 181 cases the old copy did not have
are 88 processing instructions, 6 that need a script to run while the page is
parsed, and 87 others. The first group needed engine work: `<?x?>` is a node
of its own in the standard now, and was a comment here. The six remain, and
`make conformance` scores 1,967 of 1,973.

> **Honest scope:** 100.0% on the vendored parser conformance corpus means the DOM this engine builds
> matches a real browser's across 1,967 of those 1,973 cases. It does **not** mean it renders real pages:
> floats and tables lay out with spans and collapsed borders (Phases 16 and 29),
> percentages resolve (Phase 18) and `overflow` clips (Phase 18), but there is
> no bidi, external stylesheets and scripts are not loaded, and the
> JS engine is a tree-walking interpreter without a JIT.
> See "Known gaps" in `ARCHITECTURE.md`.

---

## 🌟 Features & Architecture

Litebird implements the complete modern browser rendering pipeline:

```text
[Raw HTML] ───► HTML Tokenizer ───► HTML Parser ───► DOM Tree
                                                         │
[Raw CSS]  ───► CSS Tokenizer  ───► CSS Parser  ───► Style Rules (@media, @keyframes)
                                                         │
                                                         ▼
                                                 Style Resolver (Cascade, Viewport & Pseudo-Classes)
                                                         │
                                                         ▼
                                                 Layout Box Tree
                                                 (Block / Flexbox / Grid / IFC / Canvas / Image / SVG)
                                                         │
                                                         ▼
                                                 Display List
                                                 (DrawRect, DrawBorder, DrawText, DrawImage)
                                                         │
                                        ┌────────────────┴────────────────┐
                                        ▼                                 ▼
                                 Terminal Renderer                 Bitmap Renderer
                              (24-bit ANSI Truecolor)             (24-bit .BMP Image)

                                                 ▲
                                                 │
[JavaScript] ───► Lexer ───► AST Parser ───► Bytecode Compiler ───► Stack VM
                                                                         │
                                                 ├──► DOM Bindings (getElementById, textContent)
                                                 ├──► HTML5 Canvas 2D (getContext("2d"))
                                                 ├──► SVG Vector Graphics (<svg>, <path>, <circle>)
                                                 ├──► ES6 Collections (Map, Set) & Destructuring
                                                 ├──► Web Storage & Cookies (localStorage, cookies)
                                                 └──► Async / Promises / EventLoop (fetch, setTimeout, rAF)
```

### 1. DOM Core, W3C EventTarget & Web Components (`include/litebird/dom/`)
- Polymorphic node hierarchy (`Node`, `Element`, `Text`, `Document`, `DocumentFragment`).
- Tree navigation (`parent()`, `children()`, `append_child()`, `remove_child()`).
- DOM query methods: `getElementById()`, `getElementsByTagName()`, `querySelector()`, `querySelectorAll()`.
- Interactive tree printer (`dumpTree()`).
- **No recursion over the tree:** the destructor, `textContent`, `innerHTML`, the selector and tag searches and the dumps are loops (`include/litebird/dom/TreeWalk.hpp`), so a tree a script nested 200,000 levels deep can be built, queried and released.
- **W3C 3-Phase DOM Event Dispatch:** Standard capture, target, and bubbling phases (`Event`, `addEventListener(type, listener, useCapture)`, `dispatchEvent()`, `stopPropagation()`, `preventDefault()`).
- **HTML Form Controls:** Native DOM state and change event dispatch for `<input type="text|checkbox">`, `<textarea>`, `<button>`, and `<select>`.
- **MutationObserver API:** Observation of DOM mutations (`childList`, `attributes`, `attributeOldValue`, `subtree`, `attributeFilter`), microtask batching, and `takeRecords()`.
- **Autonomous Custom Elements:** `customElements.define()`, `whenDefined()`, dynamic element upgrade, and lifecycle callbacks (`connectedCallback`, `disconnectedCallback`, `attributeChangedCallback`).
- **Shadow DOM & Slot Content Projection:** `attachShadow({ mode: 'open' | 'closed' })`, `shadowRoot`, scoped CSS encapsulation with `:host` rules, and `<slot>` content projection (`assignedNodes()`, `assignedElements()`).

#### The DOM a script sees (Phase 27, `src/js/Dom*Bindings.cpp`)
- **Interfaces with prototypes:** about 170 (`Node`, `Element`, `HTMLElement` and the HTML element interfaces, `Document`, `Event` and its kinds, `DOMTokenList`, `HTMLCollection`, `NodeList`, `Range`, `MutationObserver`, `FormData`, `URL`, ...). Members live on the prototype and check what they are called on (`Illegal invocation`), constructors can be subclassed, and one node has one object.
- **Nodes and trees:** `append`/`prepend`/`before`/`after`/`replaceWith`/`replaceChildren`, `insertAdjacentHTML/Element/Text`, `cloneNode`, `contains`, `compareDocumentPosition`, `isEqualNode`, `normalize`, `DocumentFragment`, `<template>.content`, `TreeWalker`, `NodeIterator`, `DOMParser`, `XMLSerializer`, and the hierarchy checks with the `DOMException` each one names.
- **Ranges and the selection** (`include/litebird/js/DomLive.hpp`): `Range` with `extractContents`, `cloneContents`, `deleteContents`, `insertNode`, `surroundContents`, `createContextualFragment` and the comparisons; `getSelection()`. Ranges are live: their boundaries follow nodes that are inserted and removed, text that is edited or split, and `normalize()`.
- **Selectors** (`src/dom/Selector.cpp`): compound and complex selectors with all four combinators, attribute selectors with every operator and the `i` flag, `:is()`, `:where()`, `:not()`, `:has()`, the `:nth-*` family with `of`, and the state pseudo-classes of forms; `querySelector(All)`, `matches`, `closest`. An invalid selector is a `SyntaxError`.
- **Markup:** `innerHTML`/`outerHTML` written by the standard's serializer (escaped text and attributes, void elements, namespaces) and read by the parser for the element it is assigned to.
- **Attributes and their reflections:** `classList`, `dataset`, `attributes`, `style` as a live declaration block, `id`/`className`/`hidden`/`tabIndex`/`href`/... per element type.
- **`getComputedStyle`** (`src/js/DomStyleBindings.cpp`): an answer for every property. The rules of the page ordered by importance, specificity and position, on top of what HTML gives each element; inherited and initial values; colours as `rgb()`; lengths in px (`em`, `rem`, `pt`, `calc()`, font sizes through the chain of ancestors); shorthands and their parts. Computed values, not used ones: `width: auto` stays `auto`, because layout has not happened.
- **Events:** `Event` and 27 subclasses with their init dictionaries, capture/target/bubble with `composedPath()`, `once`/`passive`/`signal`, `on*` properties and attributes, default actions (`click()` on a checkbox, a label, a submit button, a link to a fragment), `AbortController`.
- **Forms** (`src/dom/FormState.cpp`): `value`/`checked`/`selected` with their dirty flags, radio groups, `<select>` and `options`, constraint validation (`validity`, `checkValidity`, `reportValidity`, `setCustomValidity`), `form.elements`, submission and reset with their events, `FormData`, `requestSubmit`.
- **Observers and components:** `MutationObserver` with every option, custom elements (autonomous and customized built-ins, upgrades, all reactions, in the order the standard queues them), shadow trees and slots.
- **The window:** `location` and `history` (`pushState`, `hashchange`, `popstate`), `URL`/`URLSearchParams` on the URL Standard's parser, timers, `localStorage`, cookies, `matchMedia`, `XMLHttpRequest`, `Blob`/`File`/`FileReader`, `window.onerror` and `unhandledrejection`, named access (`window.myId`, `document.forms.search`).
- **Loading:** scripts run as the parser reaches them (`document.currentScript`, `document.readyState`), then `DOMContentLoaded` and `load`.
- **XML** (Phase 28, `src/xml/`): `DOMParser` for `application/xml`, `text/xml`, `application/xhtml+xml` and `image/svg+xml`, with the `parsererror` document for text that is not well formed; `document.implementation.createDocument()`; `createElementNS`, `setAttributeNS` and their relatives, `lookupNamespaceURI`, `lookupPrefix`, `isDefaultNamespace`; `CDATASection` and `ProcessingInstruction`; `XMLSerializer` and `innerHTML` in an XML document by *DOM Parsing and Serialization*; the namespace bar in selectors (`*|a`, `|a`, `[*|href]`). `XMLHttpRequest` for `data:` URLs: `responseXML`, `responseText`, `response` with `responseType` (`text`, `document`, `json`, `blob`), `overrideMimeType`, the states and progress events, synchronous requests, `abort()`.

### 2. HTML5 Parser: spec tree construction (`include/litebird/html/`)
- **Full WHATWG tree-construction algorithm** (`HtmlTreeBuilder`): 23 insertion modes (`initial` through `after after frameset`), including `in table`, `in table body`, `in row`, `in cell`, `in caption`, `in column group`, `in select`, `in template` and `text`.
- **Implied element synthesis:** `<html>`, `<head>`, `<body>`, `<tbody>` and `<colgroup>` are created when the markup omits them; `<p>`, `<li>`, `<dd>`/`<dt>`, `<option>` and heading elements auto-close per the spec's implied-end-tag rules.
- **Active formatting elements + the adoption agency algorithm:** misnested `<b><i></b></i>` repairs produce the same DOM a browser does.
- **Foster parenting:** stray content inside `<table>` is relocated before the table instead of being dropped.
- **Scopes:** default, list-item, button, table and select scoping rules drive every "is this tag in scope?" decision.
- **Foreign content:** SVG and MathML namespaces, tag *and* attribute case adjustment (`<foreignobject>` → `foreignObject`, `viewbox` → `viewBox`), `xlink`/`xml` attribute namespacing, HTML/SVG/MathML integration points, `</p>`/`</br>` breakouts, and `<![CDATA[ ]]>` sections. Scope checks are namespace-aware, so an `<svg td>` never satisfies an HTML table-scope test.
- **`<template>`:** contents live in a separate `DocumentFragment`, exactly as the spec requires.
- **Comments and doctypes are real nodes** (`Comment`, `Document::doctype()`), including quirks/limited-quirks/no-quirks document modes.
- **Spec-complete tokenizer:** all tag, attribute, comment, bogus-comment, doctype and CDATA states, RCDATA/RAWTEXT/plaintext content models, CRLF normalisation, the complete 2231-entry named character reference table, and the full **script-data state machine** including the escaped and double-escaped runs (`<!--<script </script>-->` stays text, as it should).
- **`eof-in-tag`, `eof-before-tag-name` and NUL handling** match the spec: a tag cut off by EOF is discarded, `</` at EOF is text, and a NUL in a comment becomes U+FFFD.
- **Fragment parsing with a context element**, which is what makes `innerHTML` (and `<tbody>` vs `<div>` contexts) behave correctly.
- **A nesting limit of 512** (`HtmlTreeBuilder::kMaxTreeDepth`, the number Blink and WebKit use): an element that would be deeper is attached beside the element it was written in. The stack of open elements is not shortened, so the parser's state is what the standard says at any depth, and every per-token question about that stack is answered from an index.
- Legacy helpers: `HtmlParser::parse()`, `HtmlParser::parse_fragment_with_context()`.

### 3. HTML Parser helpers (`include/litebird/html/`)
- Stateful streaming tokenizer (`HtmlTokenizer`).
- Supports start tags, end tags, attributes (quoted/unquoted, boolean), comments, and DOCTYPEs.
- Automatic handling of void elements (`<img />`, `<br>`, `<input>`, `<hr>`, `<meta>`, `<link>`).
- Raw text mode for `<style>` and `<script>` tags.
- Character entity decoding (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&nbsp;`).

### 4. CSS Engine, Cascade, Animations & Pseudo-Classes (`include/litebird/css/`)
- Selector parser supporting:
  - Universal (`*`)
  - Type / Tag selectors (`div`, `h1`, `p`)
  - Class selectors (`.card`, `.btn`, `.navbar`)
  - ID selectors (`#header`, `#main`)
  - Attribute selectors (`[attr]`, `[attr="value"]`)
  - Descendant combinators (`div .card p`)
  - **Advanced CSS Pseudo-Classes:**
    - Structural: `:first-child`, `:last-child`, `:nth-child(An+B)`, `:nth-of-type(An+B)` (supports `odd`, `even`, and linear index formulas).
    - User Action & State: `:hover`, `:focus`, `:active`, `:disabled`, `:checked`.
    - Negation: `:not(...)` functional selector inversion.
- **CSS Variables & Custom Properties (`var()`):** Lexical custom property cascade (`--custom-name: value`) with inheritance down DOM subtrees and recursive nested fallback evaluation (`var(--color, var(--fallback, #fff))`).
- **CSS Math Expression Engine (`calc()`):** Recursive descent arithmetic evaluator resolving additions, subtractions, multiplications, divisions, percentages, and variable substitutions.
- **CSS 2D Transforms & Affine Matrix Math:** Homogeneous 3x3 transform matrices (`TransformMatrix2D`) implementing `translate()`, `rotate()`, `scale()`, `skew()`, and `transform-origin`, complete with inverse-matrix hit-testing.
- **CSS Transitions & Keyframe Animations (`@keyframes`):**
  - Declarative `@keyframes` rule parsing with percentage offsets (`0%`, `50%`, `100%`) and `from`/`to`.
  - Animation properties: `animation-name`, `animation-duration`, `animation-timing-function`, `animation-delay`, `animation-iteration-count`, `transition-property`, `transition-duration`.
  - Timing function calculation: `linear`, `ease`, `ease-in`, `ease-out`, `ease-in-out`.
  - Continuous property interpolation for `opacity`, `width`, `height`, `color`, and `background-color`.
- **CSS Media Queries (`@media`):** Supports responsive breakpoints (`(max-width: ...)`, `(min-width: ...)`, `(max-height: ...)`, `(min-height: ...)`). Dynamically re-evaluates rule application against active viewport dimensions.
- **Longhand Shorthands (Phase 15):** `margin`, `padding`, `border`, `border-width`, `border-color`, per-side borders, `background`, `font`, `border-radius` and `flex-flow` are expanded into longhands before cascading, so `margin: 10px 20px` and `border: 3px solid #0f0` behave as expected.
- **`!important` and the full cascade order (Phase 15):** author normal → inline normal → author `!important` → inline `!important`, applied in `(importance, specificity, source order)` tiers.
- **Percentage lengths (Phase 18):** `width`, `height`, `min`/`max` dimensions, `margin` and `padding` percentages resolve against the containing block at layout time (`width: 50%` is half the parent, not fifty pixels), and percentage table cells resolve against the table width.
- **Relative and viewport units (Phase 15):** `em`/`ex`/`ch` resolve against the element's own computed font size, `rem` against the root size, and `vw`/`vh`/`vmin`/`vmax` against the viewport; `pt`, `pc`, `in`, `cm`, `mm` and `q` are converted to CSS pixels.
- **Specificity Calculation:** Computes `(ID, Class, Tag)` triples for accurate cascade resolution.
- **Built-in User-Agent Stylesheet:** Default styling for headings, paragraphs, lists, and layout blocks.
- **Style Resolver:** Combines User-Agent styles, Author stylesheets, and inline `style="..."` attributes with property inheritance (`color`, `font-size`, `text-align`).

### 5. Layout Engine: Block, Flexbox, Grid, Positioning & IFC (`include/litebird/layout/`)
- Standard Box Model: `content`, `padding`, `border`, `margin`.
- Box types: `BlockNode`, `FlexNode`, `GridNode`, `InlineNode`, `AnonymousBlock`, `TextNode`, `ImageNode`, `TableNode`, `TableRowNode`, `TableCellNode`.
- **CSS Box Sizing & Constraints:** Full support for `box-sizing: content-box | border-box`, plus `min-width`, `max-width`, `min-height`, and `max-height`.
- **CSS Positioning & Stacking Contexts:** `position: static | relative | absolute | fixed`, coordinate offsets (`top`, `right`, `bottom`, `left`), and hierarchical `z-index` stacking context sorting.
- **CSS Grid Layout (`display: grid`):** Track dimension resolution (`grid-template-columns: 1fr 2fr 1fr`), `repeat()` counts, `minmax()` track constraints, `auto-fit` auto-repetition, cell auto-placement, item alignment, and grid gap spacing (`row-gap`, `column-gap`, `gap`).
- **CSS Flexbox (`display: flex; flex-wrap: wrap`):** Single-line and multi-line flex container line breaking, cross-axis line alignment, space distribution, and flex item `gap` / `row-gap` / `column-gap` spacing without margin distortion.
- **Inline Formatting Context (IFC 2.1):**
  - Line box construction (`LineBox`) and inline fragment generation (`InlineFragment`).
  - True continuous text flowing across mixed inline elements (`<b>`, `<strong>`, `<i>`, `<em>`, `<a>`, `<code>`, `<span>`).
  - Inline element styling: custom colors, background pills, borders, padding, and text-decoration (`underline`).
  - Word wrapping across element boundaries with exact baseline alignment.
- **Floats & Block Formatting Contexts (CSS 2.1 §9.5):**
  - `float: left | right | none` with shrink-to-fit widths, floating out of the normal flow, and floats stacking side by side until a band is full.
  - `clear: left | right | both` pushes a box below the relevant float bottoms.
  - Line boxes shrink around floats per line (`LineBox::left_intrusion` / `right_intrusion`), so text wraps beside a float and reclaims the full width once it has passed.
  - A block formatting context (`overflow != visible`, floats, flex/grid containers, table cells, inline-blocks, the root) grows to contain its floats instead of collapsing.
  - `overflow: hidden | auto | scroll` and `contain: paint` clip painting: the display list carries `PushClipCommand`/`PopClipCommand` and both renderers honour the clip stack.
- **CSS Table Layout (`display: table | table-row | table-cell | table-row-group`):**
  - Anonymous table structure: rows and row groups are flattened into a row grid, and stray cells are collected into rows.
  - Automatic column sizing: each column is at least as wide as its widest cell's min-content width or explicit width; an explicit table width distributes the surplus across the columns.
  - Row heights are the maximum of the row's cell heights; cells carry their own padding, borders and backgrounds.
  - Percentage cell widths resolve against the table width (a `width: 25%` cell in a 300px table is a 75px column).
  - UA stylesheet provides the default `table`/`tr`/`td` display values plus `<th>` centering and bold weight.
- **Incremental Layout & Invalidation:** Granular `dirty` marking (`mark_dirty()`) and subtree-isolated relayout avoiding redundant full-document recalculations.

### 6. Multi-Backend Rendering, SVG Vector Graphics, Canvas 2D & Image Decoding (`include/litebird/render/`)
- **Native SVG Vector Graphics Engine (`SvgRenderer`):**
  - Direct rasterization of `<svg>` DOM trees into `ImageNode` layout boxes with iterative ID indexing and recursion/cycle guards.
  - `viewBox` + `preserveAspectRatio` (`meet`/`slice`/`none`), full `transform` stacks (`matrix`, `translate`, `scale`, `rotate`, `skewX`, `skewY`), and nested `<svg>`.
  - Shape & path rasterization: `<rect>` (including `rx`/`ry`), `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>`, and full SVG `<path>` grammar (`M/m`, `L/l`, `H/h`, `V/v`, `C/c`, `S/s`, `Q/q`, `T/t`, W3C F.6.5/F.6.6 `A/a` elliptical arcs, `Z/z`).
  - Paint servers, styling, reuse & clipping: `<defs>`, `<linearGradient>`/`<radialGradient>` (`href` inheritance, `objectBoundingBox`/`userSpaceOnUse`, `gradientTransform`, `spreadMethod`), `<use>`/`<symbol>`, `<clipPath>`, inline `style`, `rgba()`/`#RGBA`/`#RRGGBBAA`, `currentColor`, `opacity`/`fill-opacity`/`stroke-opacity`, `fill-rule="evenodd"|"nonzero"`, `display:none`/`visibility:hidden`, and dashed/capped/joined strokes.
  - Basic SVG `<text>` / `<tspan>` in Phase 82: inherited font/solid paint, first `x`/`y` and `dx`/`dy`, simple `text-anchor`, whitespace normalization, and Canvas 2D glyph rasterization in SVG user coordinates. Per-character lists, textPath, multi-run anchoring, and text gradients remain open; see `docs/phases/82-svg-text-rendering.md`.
- **HTML5 `<canvas>` 2D Context API (`CanvasRenderingContext2D`):**
  - 2D CTM stack (`save`/`restore`, `translate`, `rotate`, `scale`, `transform`, `setTransform`, `resetTransform`), `globalAlpha`, and `clip()`.
  - 4× vertical + analytical horizontal anti-aliased scanline rasterization (`nonzero` and `evenodd` winding rules) and polygon-ribbon thick stroking (`lineWidth`, `lineCap`, `lineJoin`, `miterLimit`, `setLineDash`/`getLineDash`/`lineDashOffset`, `strokeGradient`).
  - Geometric & path primitives: `fillRect`, `clearRect`, `strokeRect`, `rect`, `roundRect`, `arc`, `ellipse`, `arcTo`, `quadraticCurveTo`, `bezierCurveTo`, `isPointInPath`.
  - Text (Phase 46): `fillText`, `strokeText`, `measureText` with `font` (CSS font shorthand subset — style, variant, weight, size in px/pt/em/rem/% with an optional `/line-height`, and a family list that is parsed and reflected but rasterised with the engine's system font), `textAlign`, `textBaseline`, `maxWidth` horizontal condensing, solid or gradient paint, CTM-aware sampling with AA coverage, `clip()` and `globalAlpha` support, and the save/restore text state.
  - Inverse-CTM linear/radial gradients (`Pad`/`Reflect`/`Repeat`), `createImageData`/`getImageData`/`putImageData`, CTM-aware `drawImage`, and JS property accessors via `JsValue::define_accessor`.
- **Native Image Decoders (`JpegDecoder`, `GifDecoder`, `WebPDecoder`, `Vp8Decoder`, `ImageDecoder`):**
  - Standalone ITU-T T.81 Baseline/Extended/Progressive JPEG decoder (`JpegDecoder`: 4:4:4/4:2:2/4:2:0/4:4:0, Grayscale/YCbCr/RGB/CMYK/YCCK, restart markers, multi-scan progressive refinement).
  - Standalone GIF87a/GIF89a single-frame & animated multi-frame decoder (`GifDecoder`: 2..8-bit LZW, GCT/LCT, 4-pass interlacing, transparency, all 4 disposal methods, loop count).
  - Standalone lossless WebP decoder (`WebPDecoder`: the RIFF container including `VP8X`, `VP8L`'s Huffman-coded LZ77, meta prefix codes, colour cache, and all four transforms) and, since Phase 57, a standalone lossy decoder (`Vp8Decoder`: a VP8 key frame — boolean coder, token partitions, 16x16/8x8/4x4 intra prediction, IDCT and Walsh–Hadamard, the whole-frame loop filter, fancy chroma upsampling — with the `ALPH` plane in both compression methods and all three spatial filters). Animated WebP is composited too, since Phase 79 (`WebPDecoder::decode_frames()`: `VP8X` canvas, `ANIM` loop count and background, one `ANMF` per frame with its own rectangle, duration, blending and disposal method, every frame handed back whole — `<img>` shows the first, as it does for a GIF). AVIF is still reported by status, not drawn.
  - `ImageDecoder` for PNG (DEFLATE / uncompressed), Netpbm PPM, BMP, JPEG, GIF and WebP.
- **Terminal Renderer:** Renders web pages and images directly inside your terminal using 24-bit ANSI Truecolor half-block characters and ASCII borders.
- **Bitmap Renderer:** Exports 24-bit uncompressed `.bmp` images using an embedded font with anti-aliasing. Zero external GUI or graphics libraries required.

### 7. JavaScript Engine & Web APIs (`include/litebird/js/`, `include/litebird/net/`)

The engine was rebuilt in Phase 23. It is a tree-walking interpreter: a lexer
and a recursive-descent parser produce an AST, and the evaluator runs it
directly. A script with a syntax error does not run at all, and no script runs
twice.

- **Language:** `var`/`let`/`const` with hoisting and the temporal dead zone,
  strict mode, closures, all operators including `?.`, `??` and the logical
  assignments, destructuring, spread and rest, default parameters, template
  literals and tagged templates, labelled statements, `switch`, `try`/`catch`/
  `finally`, getters and setters, computed keys, symbols, iterators,
  `for`...`of` and `for`...`in`, generators, classes (fields, private names,
  statics, accessors, `super`, subclassing built-ins), regular-expression
  literals.
- **Promises and async functions:** promises follow the specification's
  ordering (reactions are FIFO microtasks, thenables are adopted), with `all`,
  `allSettled`, `any`, `race` and `withResolvers`. `await` suspends the
  function and returns to its caller; `for await` and async generators work.
  A rejection nothing handles is reported as `Uncaught (in promise)`.
- **Standard library:** `Object`, `Function`, `Array`, `String`, `Number`,
  `Boolean`, `Symbol`, `Math`, `JSON`, `Date`, `RegExp` (an iterative bytecode
  backtracking VM with lookaround, `d`/`g`/`i`/`m`/`s`/`u`/`v`/`y` flags,
  Unicode 17.0 `\p`/`\P` property escapes & Properties of Strings, `v`-mode
  set operations `--`/`&&`/`\q{...}`, `match.indices`, ES2025 `(?ims-ims:...)`
  modifiers, duplicate named groups, `RegExp.escape()`, and `Symbol.*`
  dispatch), `Map`, `Set`, `WeakMap`, `WeakSet`, `Reflect`, the `Error` family,
  `structuredClone`, `globalThis`, `eval` and `Function`. Methods live on
  shared prototypes; each interpreter has its own set (a realm).
- **Event loop:** tasks, microtasks and timers, with a microtask checkpoint
  whenever the script stack empties. `setTimeout`/`setInterval` (with extra
  arguments), `queueMicrotask`, `requestAnimationFrame`, `performance.now()`.
  The loop's clock can skip ahead, so `--run-js` and one-shot page rendering
  never sleep waiting for a timer.
- **Strings:** a string value is a 24-byte handle on a shared block of UTF-8
  (`JsString`). Passing, reading and indexing a string copy nothing, `s += x`
  appends in place, and a large slice is a range of its parent, so a loop over
  a long string or a string built piece by piece costs what it reads
  (`ARCHITECTURE.md`, Phase 24).
- **Garbage collector:** mark and sweep, including cycles through closures.
  Every value holds a counted handle to its object, and the collector treats
  an object with more handles than it can find inside the heap as held by
  native code. Nothing has to be registered as a root. `gc()` runs a
  collection and returns its statistics.
- **Page lifecycle:** `document.readyState`, `DOMContentLoaded` and `load`
  fire once the document's scripts have run.
- **Web APIs:** `fetch`, `Response`, `Headers`, `URL`, `URLSearchParams`,
  `localStorage`/`sessionStorage`, `document.cookie`, `AbortController`,
  `WebSocket` (`ws://` and `wss://` TLS verified via runtime OpenSSL 3, Fetch `Origin` & mixed-content policy, RFC 6455 framing, live accessors; `ArrayBuffer`/`Blob` binary exposure awaits typed arrays), `Worker` (each worker is a thread with its own
  interpreter and event loop; `Worker`, `Window`, and same-agent
  `MessagePort` messages use a neutral structured-clone packet for primitives,
  ordinary objects, and arrays), `canvas.getContext("2d")`, and the DOM:
  section 1, "The DOM a script sees".
- **What is not there:** BigInt, `Proxy`, typed arrays and `ArrayBuffer`, a
  general ES-module language runtime (only module workers have the documented
  static import/export subset), `with`, holes in arrays, `Intl`. See "Known
  gaps" in `ARCHITECTURE.md` for the limits of what is there.
- **Bytecode VM:** an earlier experiment, kept for `--dump-bytecode`. It
  covers a small subset of the language and scripts do not run on it.

### 8. Modern Browser Capabilities & Advanced Engine Platform (Phase 14) (`include/litebird/dom/`, `include/litebird/render/`, `include/litebird/net/`, `include/litebird/js/`)
- **IntersectionObserver API:** Viewport and root-relative intersection detection, threshold crossing calculations (`thresholds`, `rootMargin`), ratio computation, entry records, and EventLoop microtask delivery.
- **ResizeObserver API:** Element content and border box size change observation (`contentRect`, `borderBoxSize`), target tracking, and reactive microtask dispatch.
- **History API & Client-Side SPA Routing:** Session history stack management (`pushState`, `replaceState`, `back`, `forward`, `go`), navigation listeners, and standard `popstate` window event dispatch.
- **CSS Visual Filters & Backdrop Filters:** Full parsing and software rasterization for CSS `filter` and `backdrop-filter` operations:
  - Separable Gaussian-approximation box blur (`blur()`).
  - Color channel adjustments: `brightness()`, `contrast()`, `grayscale()`, `sepia()`, `invert()`, and `opacity()`.
  - Multi-parameter drop shadows: `drop-shadow(dx dy blur color)`.
- **Canvas 2D Blend Modes (`globalCompositeOperation`):** Standard Porter-Duff and color blending algorithms implemented in software: `source-over`, `copy`, `multiply`, `screen`, `lighter`, `destination-over`.
- **JavaScript Generators & Iteration Protocol:**
  - Generator functions: `function*`, cooperative suspension/resumption via `yield` expressions, and delegated iteration (`yield*`).
  - Generator objects with standard `.next()`, `.return()`, and `.throw()` methods.
  - Well-known Symbol `Symbol.iterator` support on arrays, generators, and custom iterables.
  - `for...of` iteration statement over arrays, strings, and generator sequences.
- **AbortController & Network Cancellation:**
  - Standard `AbortController` and `AbortSignal` APIs (`aborted`, `reason`, `abort()`, `throwIfAborted()`, `AbortSignal.timeout(ms)`).
  - Integration with `fetch(url, { signal })` to cleanly abort inflight network requests and immediately reject pending promises with `AbortError`.
- **CSS Grid Named Areas & Layout Containment:**
  - `grid-template-areas`: Multi-row and multi-column visual ASCII grid template definitions.
  - `grid-area`: Item placement into named template areas or explicit grid line coordinates (`row-start / col-start / row-end / col-end`).
  - CSS Containment (`contain: none | strict | content | layout | paint | size`): Layout isolation stopping dirty bit propagation at containment boundaries and size containment.

### 9. Interactive Browser Shell, EventLoop & Network (`include/litebird/shell/`, `include/litebird/net/`)
- **Interactive TUI Browser Shell (`BrowserShell`):**
  - Terminal User Interface with keyboard-driven browsing: `[Tab]` / `[Shift+Tab]` cycles focus, `[Enter]` activates links and buttons, `[↑/↓/PgUp/PgDn]` scrolls viewport.
  - Navigation history (`back()`, `forward()`, `reload()`, `navigate_to()`).
  - Interactive URL bar (`[o]` key) supporting local files and remote HTTP URLs.
  - Integrated DevTools inspector pane (`[d]` key) with live DOM and Heap memory metrics.
- **Asynchronous EventLoop & Timers (`EventLoop`):**
  - High-resolution timer queue for `setTimeout()` and `setInterval()`.
  - Timer-driven DOM mutations trigger reactive relayout and repaints.
- **Resource Loader & Native HTTP Client (`ResourceLoader`):**
  - Native POSIX socket HTTP/1.1 client supporting GET requests and headers without external dependencies.
  - Rejects unsafe HTTP methods, URL request targets, and caller header names/values; caps request/response headers at 64 KiB and total HTTP responses at 16 MiB.
  - Full relative URL path resolution.

---

## 🚀 Getting Started

### Prerequisites

- `g++` with C++20 support (GCC 11+)
- `make`

### Building

```bash
# Build the main litebird CLI
make

# Run the 260 engine test suites
make test
```

### Running Examples

```bash
# 1. Run the Phase 14 Super-Showcase (Observers + Visual Filters & Blending + Generators + Grid Areas)
./build/litebird examples/phase14_showcase.html

# 2. Run the Phase 9 Super-Showcase (SVG Vector Graphics + Animations + Pseudo-Classes + ES6 Map/Set)
./build/litebird examples/phase9_showcase.html

# 3. Run the Next-Gen Feature Showcase (Canvas 2D + Grid + Storage + ES6)
./build/litebird examples/nextgen_showcase.html

# 4. Launch the Interactive TUI Browser Shell
./build/litebird --shell examples/browser_portal.html

# 5. Export Phase 14 showcase directly to a 24-bit BMP image
./build/litebird --render-bmp phase14_showcase.bmp examples/phase14_showcase.html

# 6. Run the live EventLoop Timer & Interactive Counter application
./build/litebird examples/timer_counter.html

# 6. Run the DOM EventTarget and Mark-and-Sweep GC showcase
./build/litebird examples/events_and_gc.html

# 7. Render an Inline Formatting Context (IFC) page with mixed inline badges
./build/litebird examples/ifc.html

# 8. Render a Flexbox layout with responsive cards and navbar
./build/litebird examples/flexbox.html
```

---

## 🛠️ CLI Options

| Flag | Description |
| :--- | :--- |
| `--shell [file/url]` | Launch interactive TUI Browser Shell |
| `--dump-dom` | Print the hierarchical DOM tree |
| `--dump-css` | Print parsed CSS rules and specificity calculations |
| `--dump-layout` | Print the computed Layout Box tree with geometry |
| `--dump-paint` | Print atomic display list commands |
| `--dump-bytecode` | Disassemble JavaScript code into VM bytecode |
| `--render-terminal` | Render page to ANSI Truecolor terminal (default) |
| `--render-bmp <file>` | Render page to an uncompressed 24-bit BMP image |
| `--render-png <file>` | Render page to a compressed PNG image (with TrueType `.ttf` text when available) |
| `--gui [--port=<N>] [url]` | Launch the interactive browser Web GUI on `0.0.0.0:<N>` (default `8080`) with viewport, navigation, interaction, and DevTools |
| `--run-js "<code>"` | Run a script, then its timers and promise callbacks. Exits 1 if anything was left uncaught |
| `--run-script <file>` | Load the page, run the file as one more script, then the page's load events and timers. Nothing is drawn. Exits 1 if anything was left uncaught |
| `--url=<URL>` | The address the page is said to come from (`location.href`); by default the `file:` URL of the file (or the fetched `http:`/`https:` URL) |
| `--virtual-time-budget=<ms>` | How much timer time a page gets to settle before it is rendered (default 16, one frame) |
| `--wall-limit=<seconds>` | How much real time the page's callbacks get once it has loaded (default 10). When that limit ends a run with callbacks still due, one line on standard error says so; a test runner with a timeout of its own passes it here (`run_wpt.py` does) |
| `--xml-canonical <file>` | Read the file as XML and print its canonical form, the one the XML conformance suite compares. Exits 0 for a well-formed document, 1 for one that is not (the reason on standard error), 3 for an encoding this build cannot read |
| `--xml-events <file>` | Read the file as XML and print what the parser reports, one line an event (`tools/fuzz/diff_xml.py` compares these with Expat and libxml2). The same exit statuses |
| `--repl` | Launch the interactive JavaScript & DOM REPL |

---

## 🧪 Running Tests & Conformance

```bash
make test
```

Output (260 suites; the first and last lines shown):
```text
=== Running Litebird Browser Engine Test Suite ===

Running test: color_parsing... PASSED
Running test: css_color_keywords_match_the_css_specification... PASSED
...
All 260 engine test suites passed successfully!
```

```bash
make js-conformance                                                     # the JavaScript scoreboard
python3 tools/conformance/run_js_conformance.py --show 10 --filter async   # failing cases, with diffs
```

```bash
make dom-conformance                                                    # the DOM scoreboard
python3 tools/conformance/run_dom_conformance.py --show 10 --filter forms  # failing cases, with diffs
python3 tools/conformance/run_dom_conformance.py --reference            # the corpus itself, against jsdom (needs Node.js and npm)
make url-conformance                                                    # URL parsing and setters
```


### Live indexed-list iteration follow-up (Phase 58)

PR #77 made NodeList and DOMTokenList methods share the realm's Array iterator
functions and fixed the `Node.childNodes` WPT identity check. This follow-up
pins live mutations and borrowed array-like iteration in a unit suite, clamps
`length` to the ECMAScript `ToLength` maximum before converting it to a C++
index (so `Infinity` does not produce a finished iterator), and reads
`forEach`'s length before checking the callback. `make test` runs 220/220
suites; `make wpt` passes **9550/9744** across 190 files (194 listed,
0 unlisted). The WPT floor now enforces the eleven gains PR #77 left without
a raised floor. See `docs/phases/58-indexed-dom-list-iteration.md` and
`docs/findings/64-indexed-dom-list-iteration.md`. Other Array methods still
snapshot non-array receivers.

### CSS whitespace runs (Phase 31)

The CSS parser and style resolver used to trim a leading run of spaces by
erasing the first character repeatedly, shifting the whole remainder each
time. `make bench-css` runs ten compact/padded page pairs through parsing,
style, layout and paint at 30,000 and 300,000 spaces per placeholder. The
dumps must match, and each padded run must finish within three seconds.
On the verification machine the 300,000-space `:nth-child` case went from
14.6 s to 111 ms; `repeat(...)`, `minmax(...)` and `translate(...)` went from
9.8–22.5 s to 0.19–0.30 s. The command and full table are in `ARCHITECTURE.md`,
Phase 31. This is a cost fix, not a claim of broader CSS conformance. The
separate `:nth-child(2n + 1)` sign-space parsing bug is fixed as of finding
39: the formula matcher ignores whitespace tokens and parses strictly signed
integers (`AUDIT.md`, finding 39).


### Attribute-list removals (Phase 35)

An element's attributes and the lazy index's ascending numbers used to be
plain vectors. Removing the first attribute shifted both tails, so draining
50,000 attributes from the front took seconds while the back was milliseconds.
They are still vectors, but with a logical start offset: a front erase
advances the offset and compacts only when the dead prefix reaches the live
tail, so end removals are linear and an empty list allocates nothing.
Lookup/update/copy/move and random-access iterator semantics stay the same; a
middle removal still moves the tail. `make bench-attributes` runs six DOM cases at 50,000
attributes (front, back, alternating ends, front-half update/reinsert,
namespaces, short-list churn), checks their checksums, and fails over 3 s.
On the verification machine the front case went from about 6.3 s to 0.09 s
whole-process. This is a cost fix, not the open `Attr`/namespace representation
or named-property work; the tokenizer repeated-name cost is fixed as of
Phase 39. Details and repro are in `ARCHITECTURE.md`,
Phase 35, and `AUDIT.md`, finding 40.


### The tokenizer's repeated attribute names (Phase 39)

The HTML tokenizer drops a repeated attribute name — the first one wins — by
comparing each name with every attribute it has already accepted. That is the
square of the count: one element with 50,000 attributes spent about 2.9 s in
the parser, and an end tag with that many paid the same. A tag with 32 or more
accepted attributes now keeps a hash set of their names; shorter tags keep the
scan, which is cheaper than building a table. The set holds views of the names
inside the token, so no name is stored twice, and it is rebuilt when the
attribute vector grows under it. `make bench-tokenizer-attributes` parses seven
page shapes at 50,000 attribute occurrences (distinct names, every name twice,
uppercase repeats, long names, the four value spellings, many small tags, an
end tag), compares each tree dump with the tree the markup must produce, and
fails over 1 s. On the verification machine the wide cases went from 3.5 s to
0.09 s and the many-small-tags case did not change. First-wins, ASCII case
folding, order and every value spelling are unchanged; this is a cost fix, not
a parser conformance change. Details and repro are in `ARCHITECTURE.md`,
Phase 39, and `AUDIT.md`, finding 46.


### A string built half a surrogate pair at a time (Phase 41)

String text is UTF-8 in blocks that never change once written, and `s += x`
writes into the room after the text so a loop costs what it writes. One join
could not: `s += '\uD83D'; s += '\uDE00'` makes one character out of two halves,
four bytes that start three bytes before the end of what was already written,
so it copied the whole string at every pair — 20,000 pairs took 5.6 s and
40,000 took 21 s, against 9 and 15 ms a pair at a time. A join now owes a
trailing high half instead of writing it: the handle keeps it, and the
character is written where the block's text ends when the low half arrives.
Anything that reads the text settles the handle first, writing three bytes
into the free room or copying once into a block the handle's own block keeps
alive. `make bench-surrogates` runs the two sizes a doubling apart and the
keep-each-prefix shape against Node.js with a one-second limit: 17 ms, 28 ms
and 14 ms, where `main`'s binary fails all three (5.5 s, over 8 s, 3.9 s). The
corpus case that used to take 5.5 s of its 10-second budget takes 0.035 s, so
the JavaScript floor in CI rose to the score at that phase, 907 of 921
(Phase 45 raises it again). The handle is a word
wider, which costs about 5% across the string benchmarks that were already
there. Reading a half-owed string before completing the
pair still copies; that shape is a row of the task table. Details and repro are
in `ARCHITECTURE.md`, Phase 41, and `AUDIT.md`, finding 47.


### Temporal dead zone for derived `this` and lexical bindings (Phase 59)

Derived constructor `this` and `let`/`const`/`class` bindings previously lacked
an uninitialized state: reading or writing `this` before `super()` touched the
pre-allocated object instead of throwing a `ReferenceError`, `typeof z` before
`let z` returned `"undefined"`, a block `let` failed to shadow outer bindings
above its declaration line, and `c = 2` before `const c` created an implicit
global.

Phase 59 (finding 65) adds `JsEnvironment::Binding::is_initialized` and
`AssignResult::Uninitialized`, marks derived constructor `this` uninitialized on
entry until `super(...)` completes, and pre-declares lexical bindings in the
parser/evaluator across scripts, functions, async/generator bodies, blocks,
`switch` statements, `for`/`for-of`/`for-in` heads, `try/catch`, class
`extends`/computed keys, and non-simple parameter defaults.

```bash
./build/litebird --run-js 'class Base {} class Derived extends Base { constructor() { try { this.x = 1; } catch (e) { console.log(e.name); } super(); } } new Derived();
try { typeof z; } catch (e) { console.log(e.name); } let z = 1;'
# ReferenceError
# ReferenceError
make test            # 203 / 203 unit suites
make js-conformance  # 1003 / 1017 (construct.dat 49/50, lexical.dat 41/41)
make test262         # 12,359 / 15,686 (+43, 0 regressions)
```

Details are in `ARCHITECTURE.md`, Phase 59, and `AUDIT.md`, finding 65.


### UTF-16 storage for non-ASCII strings (Phase 53)

Phase 53 (finding 59) stores non-ASCII `JsString` buffers as two-byte UTF-16
code units instead of UTF-8 while keeping ASCII buffers one byte per unit and
the 24-byte `JsString` handle unchanged (replacing Phase 41's `m_pending`
field with an atomic cached UTF-8 byte length). Reading `length`, indexing,
`charCodeAt`, `indexOf`, slicing, and regex matching all operate directly on
the 1-byte or 2-byte storage without generating UTF-8, while host `.view()` /
`c_str()` callers populate a lazy UTF-8 cache (with `Utf8Slice` fallback when
an unpaired trailing high surrogate is requested so the prefix never freezes
a surrogate stub before the low half arrives). Because a trailing high half in
UTF-16 is an ordinary 2-byte unit rather than a 3-byte UTF-8 surrogate that
would have to be rewritten into a 4-byte pair, appending `'\uDE00'` after
reading, calling `indexOf`, or slicing with `substring` writes the low half in
place (and a sole-thread CAS tail rewind reclaims a dropped temporary tail when
safe).

`make bench-surrogates` now runs six cases (20k, 40k, keep-each-prefix, read
between halves, `indexOf` between halves, and a `substring` piece read then
completed) against Node.js under a one-second guard (`14 / 22 / 11 / 17 / 154 /
17 ms`, versus `58 / 242 / 893 ms` at 5k/10k/20k for read-between-halves
before), `make bench` runs all 45 cases, `make tsan` passes under TSan and
ASan+UBSan, `make test` passes 260/260 suites, and `make diff-strings` prints
`50485 / 50580`. Remaining nuances: in JS, `s += '\uD83D\uDE00'; s =
s.substring(0, s.length - 1); s += '\uDE00'` still keeps `refs == 2` across
the variable slot and operand temporary during `+=` (though single-owner C++
rewinds), and calling `.view()` repeatedly while a buffer ends in an unpaired
high half allocates a slice per call. Details are in `ARCHITECTURE.md`,
Phase 53, and `AUDIT.md`, finding 59.


### Promise combinators must resolve before the next item (Phase 45)

`Promise.all`, `any`, `race` and `allSettled` used to walk an iterable
until it ended without calling `Promise.resolve` on any element. If it did not
end, they never reached the first value. Now each step calls the receiver's
`resolve` (the method is looked up once, before the iterator), reads the
result's `then`, and subscribes before asking for the next value. If any of
those operations throws, the iterator's `return()` runs and the returned
promise rejects. A custom `then` is invoked while the iterator is open.

`make verify-promise-iterators` prints `34/34` async cases with a 3-second
limit; four new cases use hostile iterators and expectations independently
checked with Node. The old binary passes 30/34, with timeouts. The full JS
score is now 925/939 after merging Phase 44's identifiers (911/925 before
that merge); `make test` passes 170 suites (163 on the original base). A page
outside the corpus, through `tools/conformance/compare_with_jsdom.py`, prints
`4 lines, 4 the same` after the fix, versus 0 before. A custom array iterator was still
ignored by `js_iterate`'s array fast path (until Phase 63, below), and subclassed Promises still
make base promises: see `AUDIT.md`, finding 51, and `ARCHITECTURE.md`, Phase 45.
The full release and sanitizer chain passed on the base tree, again after
merging main's frames/table work, and again with Phase 44's Unicode identifiers:
170/170 suites, JS 925/939 (924/939 ASan), WPT 5421/5463, 0 unlisted;
`make fuzz` and `make tsan` passed. The CI JS floor is now 925 and the ASan
floor is 923 (both raised, neither lowered).


### Page resource origin (Phase 33)

`fetch()` and `new Worker()` used to send every non-`data:`/non-`http://`
address to the local filesystem, so a page at `https://example.test/` could
read `/home/user/notes.txt` or `file:///…`. Page-initiated loads now go
through `ResourceLoader::load_for_page` with the document's address: web
documents may not read local files, relative references resolve against the
document/base URL, and a `<base href="file:///…">` does not grant file access.
A document the embedder loaded from `file:` keeps local-file access for the
CLI/TUI; trusted navigation is a separate `load_navigation` entry point.
`make security-fetch` generates a marker file and checks the web, base-smuggled,
and local cases (3 checks, 0 failures). `make test` includes the loader,
`make security-fetch` generates a marker file and checks the web, base-smuggled,
and local cases (3 checks, 0 failures). `make test` includes the loader,
fetch/worker, and relative-base suites. Details and repro are in
`ARCHITECTURE.md`, Phase 33, and `AUDIT.md`, finding 37.


### A function as a prototype (Phase 34)

`Object.getPrototypeOf(Derived)` for `class Derived extends Base` used to be
an object that was not `Base` and could not be called, `Object.getPrototypeOf(TypeError)`
was not `Error`, a static member was found one parent up and no further, and
`Function.prototype` was an object: a function is a value here, a prototype
link points at a heap cell, and nothing led from the cell back to the
function. Now something does. The helpers Babel and TypeScript emit for a
class with a parent are made of exactly those calls, so compiled classes run
(three whole programs in `tests/js-conformance/prototypes.dat`), every
`assert_throws_js` of web-platform-tests passes (18 subtests), and the DOM's
interface objects have their parents as prototypes
(`Object.getPrototypeOf(HTMLElement) === Element`).

```bash
./build/litebird --run-js 'class A { static s() { return "A.s"; } } class B extends A {} class C extends B {}
console.log(Object.getPrototypeOf(C) === B, C.s(), Object.getPrototypeOf(TypeError) === Error, typeof Function.prototype)'
# true A.s true function        (before: Uncaught TypeError: s is not a function)
make js-conformance      # 974 / 989; prototypes.dat 68 / 76
make test262             # the standard's own tests on functions, classes and objects: 12,316 of 15,686
```

What is still missing around classes (`Symbol.species`,
`Symbol.hasInstance`) is in `ARCHITECTURE.md`, Phase 34, section 9, with a
failing case each; the design and what it costs are in the same place, and
`AUDIT.md`, finding 41, says what found what. `new.target` in a parent's
constructor and custom elements compiled by Babel, which were on that list,
came with Phase 43 (below).


### Usable browser pipeline: HTTPS/CORS, subresources, live layout, TrueType & Web GUI (Phase 36)

Phase 36 connects the parsers, layout engine, and script runtime into an
interactive multi-resource browser without adding external build-time link
dependencies:

- **HTTPS/TLS, HTTP/1.1 & CORS**: Runtime `dlopen` of OpenSSL 3 (`libssl.so.3`)
  with SNI and CA hostname verification; HTTP/1.1 `Transfer-Encoding: chunked`,
  pure-C++20 `gzip`/`deflate`/zlib decompression (`Deflate.cpp`), redirect
  chaining, and CORS `Access-Control-Allow-Origin` enforcement across `fetch()`
  and networked `XMLHttpRequest`.
- **External stylesheets, scripts & origin-safe images**: `<link rel="stylesheet">`,
  `<base href>`, `media` attributes, recursive `@import`, classic and `defer`
  `<script src>` execution in HTML order, and PNG/JPEG/GIF/SVG `<img src>`
  subresource decoding gated by `ResourceLoader::load_for_page` (`AUDIT.md`,
  finding 44). Lossless WebP joined the list in Phase 54 (`AUDIT.md`,
  finding 60).
- **Live DOM geometry, TrueType `.ttf` text & `--gui` browser**: On-demand
  reflow for `getBoundingClientRect()`/`offset*`/`client*`, block-in-inline
  splitting (`<a>x<p>y</p></a>`), form-control `InlineBlock` layout, a pure-C++20
  TrueType `.ttf` glyph rasterizer (`FontRenderer.cpp`), CLI XML/SVG document
  loading, `--render-png`, and `--gui [--port=8080]` (`WebBrowserServer`).

```bash
./build/litebird examples/usable_browser_demo/index.html --render-png /tmp/demo.png
./build/litebird https://api.github.com/zen
./build/litebird --gui --port=8080
```

Still open: `<iframe>` nested documents, lossy `VP8` and AVIF images,
`@font-face`/WOFF2 and HarfBuzz/bidi shaping, CORS preflight `OPTIONS`, and
`ArrayBuffer`. Full details are in `ARCHITECTURE.md`, Phase 36, and `AUDIT.md`,
finding 44.


### Iterative RegExp VM, Modern CSS Cascade/Units, CSSOM `el.style`, and HTML5 Date/Time Inputs (Phase 37)

Phase 37 closes four cross-subsystem gaps (`AUDIT.md`, finding 42):

- **Iterative bytecode RegExp VM (`src/js/BuiltinsRegExp.cpp`):** Replaces the
  recursive CPS matcher and recursive AST destructor with an iterative bytecode
  VM, explicit heap backtracking stack (`kMaxMatchSteps = 30,000,000`), and
  iterative teardown (`regexp.dat` 39/39; `/(?:a|b)+/` on 120,000 chars and
  250+ nested groups execute cleanly without `RangeError`).
- **Modern CSS cascade, selectors, units & L4 media queries (`CssParser`,
  `StyleSheet`, `StyleResolver`):** Adds child/sibling combinators (`>`, `+`,
  `~`), `:is()` / `:where()` / selector-list `:not()` / `:host`, `@layer`
  statement/block ordering, `@supports` evaluation, Media Queries Level 4
  ranges (`<=`, `<`, `>=`, `>`) and `not`/`and`/`or`, `min()`/`max()`/`clamp()`,
  `dvh`/`dvw`/`vmax`, percentage `border-radius`, `:root` `rem` inheritance, and
  O(1) ancestor-depth pruning + `AllDescendant` DP prefix memoization (depth
  200,000 in `< 0.1 s`).
- **CSSOM `el.style` shorthand expansion & aliases (`DomStyleBindings.cpp`):**
  Expands `flex`, `overflow`, `inset`, `gap`, `borderRadius`, `background`,
  `transition`, `textDecoration`, `listStyle`, `outline`, `margin`/`padding`/
  `border` on inline styles, canonicalizes `cssText`/`length`/`item()`/
  `removeProperty`/`!important`/custom properties `--*`, and maps `-webkit-`/
  `-moz-`/`-ms-` aliases (26/27 checks on `tests/dom-conformance/open/inline-style.js`).
- **HTML5 date/time `<input>` semantics (`FormState`, `DomFormBindings`):**
  Value sanitization, `valueAsDate`, `valueAsNumber`, `stepUp()`/`stepDown()`,
  and `min`/`max`/`step` validity for `date`, `time`, `month`, `week`, and
  `datetime-local` (`6/6` date checks on `tests/dom-conformance/open/dates-and-svg.js`,
  `forms.dat` + `style.dat` bringing `make dom-conformance` to `644/644`).


### Media & Vector Rendering (Phase 38)

Phase 38 adds standalone zero-dependency C++20 decoders for ITU-T T.81
Baseline/Extended/Progressive JPEG (`JpegDecoder`) and single/multi-frame
GIF87a/GIF89a (`GifDecoder`), and upgrades `CanvasRenderingContext2D` and
`SvgRenderer` with CTM stacks, anti-aliased scanline filling (`nonzero`/
`evenodd`), thick capped/joined/dashed stroking, clipping masks, inverse-CTM
gradients, full SVG path/elliptical-arc grammar, `<defs>`/`<use>`/`<symbol>`/
`<clipPath>`, inline `style`, and opacity. `make verify-media` verifies 21 JPEG
variants pixel-by-pixel against `libjpeg` (52–99 dB PSNR), 9 GIF variants
bit-for-bit (`max_diff = 0`) against ImageMagick `-coalesce`, 69 WebP checks
(27 lossless in Phase 54, 26 lossy in Phase 57, 16 animated in Phase 79) and 5
SVG feature suites — 103 checks in all; `make fuzz-media` runs 2,400 mutated
JPEG/GIF/WebP/SVG inputs through `build/media_check`. Details and remaining gaps are in `ARCHITECTURE.md`,
Phase 38, and `AUDIT.md`, finding 43.


### `new.target`, and what a constructor makes (Phase 43)

`new.target` was the function that was running, where it is the function
`new` was applied to: an abstract class written the usual way (`if
(new.target === Shape) throw …`) refused its own subclasses, and inside `F`
`Reflect.construct(F, [], G)` did not name `G`. An object that a parent's
constructor returned was dropped by `super()`, so a class on top of a
singleton, a factory or a class compiled for older browsers went on with
another object. Both are as in Node now, also through a bound function, and
`this` is bound once: a second `super()`, a derived constructor that never
calls it and one that returns a number throw.

With the right `new.target` reaching the DOM's constructors, a custom
element compiled for older browsers works: `Reflect.construct(HTMLElement,
[], new.target)` makes the element for `new` and `document.createElement`,
and for an element that exists already (in the page's markup, from
`innerHTML`, a clone) the interface is given that element, as the standard's
construction stack has it. The same holds for a compiled subclass of a class
that a library ships.

```bash
./build/litebird --run-js 'class Shape { constructor() { if (new.target === Shape) throw new TypeError("abstract"); this.kind = new.target.name; } }
class Circle extends Shape {}
var one = {}; function Single() { return one; } class Child extends Single { constructor() { super(); this.added = 2; } }
console.log(new Circle().kind, new Child() === one, one.added)'
# Circle true 2        (before: Uncaught TypeError: abstract)
make js-conformance      # 1137 / 1156; construct.dat 50 / 50
make dom-conformance     # 678 / 678; compiled.dat 25 / 25, upgrades.dat 9 / 9
```

What is still missing: a private name is an identity now (Phase 88), but the
early errors an eval written in a class body must raise are not, and `this` in a
derived constructor is unbound until `super()` runs while a `class` field
initializer can still be reached through `eval`. Those and what
else was found on the way
are rows of the task table in `AGENTS.md`; the design and what it costs are
in `ARCHITECTURE.md`, Phase 43, and `AUDIT.md`, finding 49, says what found
what. The upgrade itself stopped being one of them in Phase 69, below.

### An upgrade is an ordinary construction (Phase 69)

A custom element that is already in the page is upgraded when its class is
defined, and until Phase 69 the engine did that by handing the element's own
object to the constructor as `this`. The standard does not: it constructs the
class the ordinary way, on an object of its own, and the element is what
`HTMLElement` gives out when the constructor asks for it — through
`super(...)`, or through `Reflect.construct(HTMLElement, [], new.target)` —
and the upgrade fails unless the constructor comes back with that element. So
the definition now keeps the standard's construction stack (`run_upgrade`
pushes the element, `construct_custom_element` takes it and marks the entry),
and a constructor can tell what it is:

```bash
cat > up.html <<'HTML'
<!DOCTYPE html><body><x-e id="e"></x-e></body>
HTML
cat > up.js <<'JS'
class E extends HTMLElement {
  field = 1;
  constructor() {
    var early; try { early = this; } catch (err) { early = err.name; }
    super();
    console.log(early, this === document.getElementById('e'), new.target === E);
  }
}
customElements.define('x-e', E);
var e = document.getElementById('e');
console.log(e.field, e instanceof E, e.matches(':defined'));
var Plain = function () { this.touched = true; };
Plain.prototype = Object.create(HTMLElement.prototype, { constructor: { value: Plain } });
Object.setPrototypeOf(Plain, HTMLElement);
document.body.insertAdjacentHTML('beforeend', '<x-p id="p"></x-p>');
customElements.define('x-p', Plain);
var p = document.getElementById('p');
console.log(p.touched === undefined, p.matches(':defined'), Object.getPrototypeOf(p) === HTMLElement.prototype);
JS
./build/litebird --run-script up.js up.html
# ReferenceError true true         `this` is unbound before super(), then the element
# 1 true true                      a field initializer lands on the element, `:defined`
# Uncaught TypeError: The custom element constructor returned a different object.
# true false true                  `this.touched` is on no element; the upgrade failed
```

What follows from it: a constructor written as a plain function is no longer
`this === self`, one that never reaches `HTMLElement` has written on an object
that is not the element and its upgrade fails, `super()` returns the element
and a second `super()` is a `TypeError`, and a constructor that throws or
returns something else leaves the element a plain `HTMLElement` that is not
`:defined` — including a customized built-in, which `Element::is_defined` used
to call defined. The object a script held before the definition existed stays
the element's object; the class's prototype is put on it when the interface
hands it out, and stays there if the upgrade then fails, as it does in a
browser.

```bash
make dom-conformance     # 678 / 678; upgrades.dat 9 / 9
make test                # 220 suites; js_an_upgrade_constructs_the_definition_the_ordinary_way
```

The five lines that measured this before the fix lived in
`tests/dom-conformance/open/compiled-custom-elements.js` (4 of 5 differed);
they are cases in `tests/dom-conformance/upgrades.dat` now.
[`docs/phases/69-custom-element-construction-stack.md`](docs/phases/69-custom-element-construction-stack.md)
says how it is built and
[`docs/findings/75-an-upgrade-ran-the-constructor-on-the-element.md`](docs/findings/75-an-upgrade-ran-the-constructor-on-the-element.md)
what was found.

### Iteration asks the value for its iterator (Phase 63)

`for…of`, spread, `yield*`, `Array.from`, `new Map`, `new Set`,
`Object.fromEntries` and the `Promise` combinators read an array's elements
and a string's characters without asking for `Symbol.iterator`. An iterator
that a script put on an array, on a subclass of `Array` or on
`Array.prototype` was never called, and neither was one on
`String.prototype`. Now the value is asked, once, and the loop is the
language's: `next` is read once, `done` before `value`, and leaving early
calls `return()` and checks what it is and what it gives back. A plain array
or string is still read directly, after a check that runs no script
(it takes between 0.94 and 1.06 of the time `main` takes, measured twice,
where a second copy of `main`'s binary takes between 0.95 and 1.03).

Three smaller things around it are right now too. An object that has `next`
and no `Symbol.iterator` is not iterable: spreading an async generator used
to hang, and throws. A property *named* `Symbol.iterator` is not the method.
A number is asked through its prototype instead of being refused.

```bash
./build/litebird --run-js 'class Rows extends Array { *[Symbol.iterator]() { for (var i = this.length; i--; ) yield this[i]; } }
var r = new Rows(); r.push("a", "b", "c");
var own = [1, 2]; own[Symbol.iterator] = function* () { yield 9; yield 8; };
console.log([...r].join(), Array.from(r).join(), [...own].join(), Math.max(...own))'
# c,b,a c,b,a 9,8 9        (before: a,b,c a,b,c 1,2 2)
make js-conformance      # 1124 / 1150; iteration.dat 65 / 78
```

What got worse, in three cases a test suite meets and a page hardly will:
with the iterator deleted from `Array.prototype`, `Array.from([1, 2])` throws
where it should read the array by index; a class without a constructor calls
a replaced `Array.prototype[Symbol.iterator]` when it is constructed, because
its default constructor is written with a spread; and `yield*` in an async
generator, which never asked for `Symbol.asyncIterator`, lost the one shape
of iterable it handled by accident. Still as it was: array destructuring
does not ask the array, and the `Map` and `Set` constructors do not call
`set` and `add`. All of it is in the 13 failing cases of `iteration.dat` and
in rows of the task table in `AGENTS.md`; `ARCHITECTURE.md`, Phase 63, has
the design, and `AUDIT.md`, finding 69, says what found what. Since that
measurement, Phase 75 fixed Map/Set constructor dispatch and Phase 77 fixed
async-generator delegation; 11 cases remain.

### `eval` runs in the scope that called it (Phase 65)

`eval` was `run(source)`: parse the text, run it against the global
environment. That is the *indirect* eval — what `(0, eval)(x)` and
`window.eval(x)` do — applied to every call, so a function could not read its
own `var` through it, `eval('this')` was the global object, `eval('super()')`
in a derived constructor was a `SyntaxError`, and `eval('var x = 1')` planted
`x` on `window`. Now a call written `eval(...)` whose identifier resolves to
the global `eval` is direct: it runs in the calling function's environment,
shares its `this`, `arguments`, `new.target`, `super` and strictness, its
`let`/`const`/`class` stay inside the eval, and its sloppy `var`s belong to
the function. `(0, eval)`, an alias and a shadowed `eval` are ordinary calls
and stay global.

The bindings a function normally hoists only for names its *body* mentions —
`arguments`, `%fn`, `%new.target`, `%home` — are now hoisted for any function
that calls `eval`, from the parser: the text is a string, and what it names is
not known until it runs.

```bash
./build/litebird --run-js 'function f() { var local = 5; return eval("local + 1"); }
function g(a, b) { return eval("arguments.length"); }
class A { constructor() { this.a = 1; } }
class B extends A { constructor() { eval("super()"); this.b = this.a + 1; } }
console.log(f(), g(7, 8), new B().b)'
# 6 2 2        (before: Uncaught ReferenceError: local is not defined)
make js-conformance      # 1124 / 1150; eval.dat 38 / 38
```

The design and what it costs are in
[`docs/phases/65-direct-eval-scope.md`](docs/phases/65-direct-eval-scope.md);
[`docs/findings/71-direct-eval-scope.md`](docs/findings/71-direct-eval-scope.md)
says what was found, and `AGENTS.md` no longer carries the row.

### The encodings pages are written in (Phase 47)

Every page and XML document used to be read as UTF-8, with UTF-16 and
Latin-1 accepted in an XML declaration: a file in Shift_JIS, EUC-KR or
windows-1252 arrived as `U+FFFD`s, `<meta charset>` was text nobody read,
and `document.characterSet` did not exist. Phase 47 implements the WHATWG
Encoding Standard: **40 encodings and their 228 labels**, generated from
`whatwg/encoding` (pinned in the header), with the index tables every byte
of them (`index-iso-8859-6`'s holes included), the Big5 pointers that stand
for two code points, `gb18030`'s four-byte ranges, BOM sniffing, and the
standard's two decoder modes — replacement (one `U+FFFD` per error, as a
page expects) and fatal (an error stops the decode, as XML expects).

An HTML page picks its encoding the way the standard says: the BOM, then
`Content-Type`, then a `<meta charset>` prescan of the first 1,024 bytes
(HTML §13.2.3.2), and `document.characterSet`, `document.charset` and
`document.inputEncoding` report what it found. XML validates the
declaration's `[81] EncName` before looking it up, refuses `replacement`,
and decodes the rest. The same tables serve `data:` URLs,
`XMLHttpRequest`, `<script src>`, `<iframe>` subdocuments and CSS
`@charset`. `--content-type=<mime>` (and a `<file>.headers` sidecar for
`--map-origin` files) lets the conformance runner hand a page the type a
server would, which is what the `encoding/` tests need.

Measured: the W3C XML suite's **1,718/1,718** (the 16 documents skipped
until now for their encoding are read, and each is still answered as the
suite says); web-platform-tests **7,968/8,009** across 103 files, of which
16 new `encoding/` files pass 2,543/2,543 subtests (2,547 more than before,
with `big5-decode-extra.html` alone at 2,472); unit suites 174 → 177. The
Fuji Xerox XML documents in EUC-JP, Shift_JIS, ISO-2022-JP and UTF-16 now
serialize to the same canonical bytes as their UTF-8 translation.
Details are in `ARCHITECTURE.md`, Phase 47, and `AUDIT.md`, finding 53.

### Lossless WebP (Phase 54)

`WebPDecoder` reads the RIFF container and the lossless `VP8L` bitstream, so
`<img src="x.webp">`, a `data:image/webp` URI and a WebP file on disk reach the
layout and the raster the same way a PNG does. Lossless WebP is a Huffman-coded
LZ77 stream over ARGB pixels preceded by up to four self-similarity transforms
— predictor (14 modes), cross-colour, subtract-green and colour-indexing with
delta-coded palettes and pixel bundling — each described by a sub-image that is
decoded with the same reader that decodes the image, plus meta prefix codes and
a colour cache.

```bash
make verify-media   # 103 checks: JPEG, GIF, lossless/lossy/animated WebP (bit-exact) and SVG
make fuzz-media     # 2,400 mutation cases across four formats
./build/media_check --decode-webp in.webp out.ppm [out.rgba]
./build/media_check --decode-webp-anim anim.webp /tmp/frame [max_frames]
./build/media_check --probe-webp  in.webp     # "ok", "truncated WebP file", ...
```

### Lossy WebP (Phase 57)

The other half of the format — the one libwebp writes by default — is a VP8
video key frame, decoded by `Vp8Decoder`: the boolean (arithmetic) coder, the
mode and residual partitions, 16x16/8x8/4x4 intra prediction, the 4x4 IDCT and
the Walsh–Hadamard transform for Y2, the loop filter over the whole frame, and
fancy chroma upsampling with libwebp's fixed-point BT.601 matrix. The `VP8X`
container and its `ALPH` plane (both compression methods, all three spatial
filters) come with it, so a lossy WebP with transparency also decodes. The
pixels are not "close": 11 generated streams from 1x1 to 129x97, four
qualities and odd sizes decode exactly as libwebp does, and the checks reach the
parts a smooth image never exercises — the loop filter (off and on, strength 0
and 100, simple, sharpness 7), the token partitions, segmentation, and the alpha
plane rebuilt by hand from all three filters.

```bash
./build/media_check --decode-vp8  frame.vp8   out.ppm [out.rgba]   # a bare `VP8 ` payload
./build/media_check --fuzz-vp8    frame.vp8
./build/media_check --decode-webp translucent.webp out.ppm out.rgba
```

`--decode-webp-anim` prints the canvas, the frame count, the loop count, the
background colour and each frame's geometry, duration and methods, then writes
one `.rgba` per composited frame:
[`docs/phases/79-animated-webp-frames-on-a-canvas.md`](docs/phases/79-animated-webp-frames-on-a-canvas.md)
says how the compositor decides what to blend over what, and
[`docs/findings/85-animated-webp-was-refused-by-a-decoder-that-could-have-drawn-it.md`](docs/findings/85-animated-webp-was-refused-by-a-decoder-that-could-have-drawn-it.md)
what it cost to get right. AVIF is still refused with a status rather than
approximated: [`docs/findings/63-lossy-webp-vp8-refused.md`](docs/findings/63-lossy-webp-vp8-refused.md)
says what was found there and
[`docs/phases/57-lossy-webp-vp8-key-frame.md`](docs/phases/57-lossy-webp-vp8-key-frame.md)
how the lossy decoder is built.

### Cross-origin `Window` / `Location`, navigation, module workers and message ports (Phases 56, 61 & 66)

A script embedded in one origin cannot read a cross-origin frame (`SecurityError`
on non-safelisted names), and Phase 56 made its *reflection* surface follow the
cross-origin table of the HTML Standard: the window reports exactly its
child-frame indices and exposed names (`window`, `self`, `location`, `close`,
`closed`, `focus`, `blur`, `frames`, `length`, `top`, `opener`, `parent`,
`postMessage`, then `then`), while `Location` exposes only `href` (writable,
not readable cross-origin) and `replace()`. Define/delete/integrity changes
refuse with `SecurityError`/`TypeError`, and `getPrototypeOf` answers `null`.
Same-origin pages are untouched.

Phase 61 makes the writable entries do useful work: setting `child.location`
or `child.location.href`, and calling `child.location.replace(url)`, schedules
a real nested-frame document navigation without changing the iframe's `src`
markup. The child keeps its `WindowProxy` identity, but receives a new
Document; the existing iframe loader continues to apply sandbox, MIME,
origin-root and load-order rules.

Phase 66 also lets `{ type: "module" }` workers load a MIME-checked,
CORS-aware static module graph. The supported subset has side-effect,
default/named/namespace imports; simple declaration/default/named exports; and
named re-exports. Modules execute in dependency order inside strict,
module-local wrappers; classic `importScripts()` is not exposed there.

`new MessageChannel()` creates two stable `MessagePort` EventTargets.
`postMessage()` queues FIFO `message` events, `onmessage` starts a port,
`start()` releases messages accumulated after `addEventListener`, and `close()`
drops later messages. Worker, Window, and same-agent port delivery now writes a
neutral structured-clone graph before scheduling: `undefined`, `NaN`,
infinities, `-0`, strings, ordinary objects, arrays, aliases, and cycles retain
their supported value semantics without retaining a sender-realm `JsValue`.
Functions, symbols, DOM/host/exotic values, and non-empty transfer lists throw
`DataCloneError`; an empty transfer list is a no-op. Ports themselves are still
not transferable or cross-agent.

`tests/cross_origin_windows.cpp`, run by `make test`, now has **114 checks**:
reflection security, three cross-origin navigation entry points, direct
navigation over both `src` and `srcdoc`, proxy identity, unchanged markup and
continued cross-origin read refusal, plus asynchronous FIFO channel delivery,
GC-safe cloned graphs, ordinary EventTarget target/receiver behavior, delayed
start and close-before/after-delivery behavior. The design and remaining limits
are in
[`docs/phases/61-child-location-navigation-and-message-channel.md`](docs/phases/61-child-location-navigation-and-message-channel.md),
[`docs/phases/66-es-module-workers-and-structured-clone.md`](docs/phases/66-es-module-workers-and-structured-clone.md),
[`docs/findings/67-child-location-navigation-and-message-ports.md`](docs/findings/67-child-location-navigation-and-message-ports.md),
and [`docs/findings/72-es-module-workers-and-structured-clone.md`](docs/findings/72-es-module-workers-and-structured-clone.md).

### String Unicode 17.0 normalization, full/locale case mapping, and CLDR 48 UCA collation (Phase 60)

Before Phase 60, `String.prototype.toLowerCase()` and `toUpperCase()` only
mapped ASCII bytes, `normalize()` returned the input unchanged after checking
the form name, and `localeCompare()` compared raw UTF-16 code units, leaving
all 71 remaining diffs in `make diff-strings` and scoring 105/156 on Test262's
nine String Unicode/locale suites. Phase 60 generates compact static Unicode
17.0 / ICU 78.2 / CLDR 48 tables (`include/litebird/js/StringUnicodeTables.hpp`,
`src/js/StringUnicodeTables.cpp`, built by
`tools/generate_string_unicode_tables.py` — 293 KB `.o`) and implements the
full UAX #15 normalization pipeline (`NFC`/`NFD`/`NFKC`/`NFKD` with Hangul Jamo
and QuickCheck fast paths), full Unicode 17.0 `SpecialCasing` (`Final_Sigma`,
`ß→SS`, `İ`, ligatures) plus locale overrides (`tr`/`az`, `lt`, and the ICU
78.2 `el` `GreekUpper::toUpper` state machine), CLDR 48 / UCA 17.0 root
collation (contiguous and discontiguous contractions, all 7 implicit weight
groups, `sensitivity`, `caseFirst`, `numeric`, `ignorePunctuation`, `-u-kf`/
`-u-kn`), and ECMA-402 BCP-47/options validation in `src/js/BuiltinsString.cpp`
(also fixing a dangling-reference bug in `this_string()`). `make diff-strings`
reaches **50,580 / 50,580 (0 diffs)**, `make verify-string-unicode` verifies
all 1,114,112 code points × 11 operations plus curated/fuzz strings and
collation pairs against Node.js at **0 diffs**, and the 9 Test262 String
Unicode/locale suites rise to **152 / 156 (97.4%)** (`Intl.Collator` and
locale-specific CLDR tailorings remain open). See `ARCHITECTURE.md`, Phase 60,
and `AUDIT.md`, finding 66.


### Async-generator delegation (Phase 77)

An async generator's `yield*` now asks for `Symbol.asyncIterator`, falls back
through an async-from-sync adapter, and forwards return and throw completions.
A direct async `yield` awaits its operand. Synchronous `yield*` passes the
iterator's result object through without eagerly reading its value. The new
Node differential `python3 tools/conformance/verify_async_generator.py` checks
seven cases; the pinned test262 delegation/yield differential gains 178 passing
cases with no newly failing cases. Async request queuing and promise-job timing
remain incomplete; see [Phase 77](docs/phases/77-async-generator-yield-star.md)
and [finding 83](docs/findings/83-async-generator-delegation.md).

### WebSocket handshake security (Phase 83)

`wss://` no longer sends a plaintext handshake: this implementation has no
WebSocket TLS transport and fails the connection instead. For `ws://`, URL
and protocol input cannot inject HTTP headers; the response must be an HTTP
101 upgrade with the correct named accept header, and selected subprotocols
must have been offered. The RFC-based loopback tests run in `make test`.
The client still lacks browser Origin/mixed-content policy, TLS, full API
validation and frame compliance; do not use it as a secure browser boundary.
See [Phase 83](docs/phases/83-websocket-handshake-security.md) and
[finding 89](docs/findings/89-websocket-handshake-security.md).

### HTTPS certificate identity (Phase 89)

HTTPS now checks an IP URL against the certificate's IP SAN, not just its
trust chain. `make security-https` runs eight ephemeral local TLS fixtures
against Python ssl and Litebird: trusted DNS/IP, wrong DNS/IP identities,
DNS SANs used as IP identities, and untrusted certificates. Rejected peers
receive no HTTP request. CI runs these normally and under ASan/UBSan.
See [Phase 89](docs/phases/89-https-certificate-identity.md) and
[finding 95](docs/findings/95-https-ip-identity.md) for scope and remaining gaps.

### WebSocket TLS, Fetch policy, and RFC 6455 framing (Phase 90)

WebSockets now support authenticated `wss://` TLS over runtime-loaded
OpenSSL 3 (system/test CA trust, `SSL_VERIFY_PEER`, SNI, and DNS/IP SAN
matching), Fetch `Origin` and W3C mixed-content enforcement across document,
Worker, `file:`, and sandboxed `iframe` contexts, relative URL resolution,
WHATWG synchronous constructor/`send`/`close` validation and live accessors,
CSPRNG keys/masking, full RFC 6455/3629 framing/UTF-8 validation, non-blocking
`poll()` + wake-pipe I/O, and deterministic `atexit` shutdown joining.
`make security-websocket` runs 29 end-to-end TLS/policy checks (including
60/60 concurrent 4x `wss://` exit runs), and `make test` covers the loopback
unit suite. `ArrayBuffer`/`Blob` binary frame exposure awaits Wave 2 typed
arrays. See [Phase 90](docs/phases/90-websocket-tls-fetch-policy-and-frames.md)
and [finding 96](docs/findings/96-websocket-tls-origin-and-framing.md).
