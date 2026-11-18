# Phase 71 — Shared immutable `ComputedStyle` across layout boxes and inline fragments

## 1. What was wrong

Every `LayoutBox` and every `InlineFragment` carried a full `ComputedStyle` by
value, and `ComputedStyle` itself stored five string fields (`text_align`,
`text_decoration`, `animation_timing_function`, `transition_timing_function`,
and `transform_origin` defaulted to `"50% 50%"`) alongside full-width `int`
enums.

On x86_64 GCC 12 before this change:

- `sizeof(ComputedStyle)` = **992 bytes**
- `sizeof(LayoutBox)` = **1,320 bytes**
- `sizeof(InlineFragment)` = **1,064 bytes**

Every word and space in an inline formatting context allocated its own
1,064-byte `InlineFragment` containing another copy of `ComputedStyle`, and
every `LayoutBox` (including anonymous wrappers and text nodes) carried another
992 bytes. Repeating `word <b>bold</b> ` 100,000 times used **1,382.3 MiB** peak
RSS (AUDIT.md, finding 12).

## 2. What changed

### Compact enums and bitmasks in `ComputedStyle`

In `include/litebird/css/StyleResolver.hpp` and `src/css/StyleResolver.cpp`:

- Converted `text_align` to `TextAlign : uint8_t`, `text_decoration` to
  `TextDecoration : uint8_t` bitmask (`None`, `Underline`, `LineThrough`,
  `Overline` with `has_underline()`, `has_line_through()`, `has_overline()`,
  and `has_text_decoration()` helpers), and animation/transition timing
  functions to `TimingFunction : uint8_t`.
- Gave the existing layout, table, flexbox, and positioning enums an explicit
  `: uint8_t` underlying type.
- Switched `transform_origin`'s default from `"50% 50%"` to empty string
  (meaning `"50% 50%"` everywhere it is read, avoiding a heap/SSO setup on every
  default style).
- Added `BoxShadow::operator==`, `ComputedStyle::GridTrackDef::operator==`,
  `ComputedStyle::GridAreaRect::operator==`, `ComputedStyle::has_percent_lengths()`,
  `ComputedStyle::operator==`, and `ComputedStyle::hash()`.

This shrank `sizeof(ComputedStyle)` from **992 B to 832 B**.

### Process-lifetime default style and per-build `ComputedStylePool`

- `SharedComputedStyle` (`std::shared_ptr<const ComputedStyle>`, **16 bytes**)
  is now the style handle stored on `LayoutBox` (`include/litebird/layout/LayoutBox.hpp`)
  and `InlineFragment`.
- `default_computed_style()` returns a process-lifetime heap-allocated
  `SharedComputedStyle` (never destroyed at process exit, preserving static
  teardown safety).
- `ComputedStylePool` deduplicates resolved element styles per `LayoutTree::build()`
  pass via `hash()` and `operator==`, and caches `inherited_only(parent, display)`
  per parent pointer for anonymous blocks and text nodes.

### Copy-on-write percentage resolution and inline override caching

- `LayoutBox::resolve_percentages(float containing_width, float containing_height)`
  (`src/layout/LayoutBox.cpp`) first checks `style->has_percent_lengths()`.
  When percentages are present and resolve to different pixel values than the
  current style holds, it clones into a local `ComputedStyle`, resolves the
  pixel fields while leaving `*_percent` intact for incremental relayout, and
  replaces only `this->style` with a new `SharedComputedStyle` — never mutating
  sibling boxes that share the unresolved template style.
- `LayoutBox::collect_items()` (`src/layout/LayoutBox.cpp`) propagates
  `SharedComputedStyle` handles instead of copying `ComputedStyle`, using a
  per-IFC `InlineOverrideCache` keyed by `(inline_box_style*, item_style*)`
  when an ancestor inline element applies `background_color`, `border_radius`,
  `text_decoration`, or `border`.
- `resolve_element_style()` (`src/layout/LayoutTree.cpp`) remains a separate
  non-recursive helper so the temporary `ComputedStyle` is freed before
  `build_boxes()` recurses into child nodes, preserving deep-nesting stack
  headroom.

### Resulting struct sizes (x86_64 GCC 12 Release)

| Type | Before (`ee88b1c`) | After | Reduction |
| :--- | ---: | ---: | ---: |
| `ComputedStyle` | 992 B | **832 B** (interned/shared) | -16.1% |
| `LayoutBox` | 1,320 B | **344 B** | **-73.9%** |
| `InlineFragment` | 1,064 B | **88 B** | **-91.7%** (12.1× smaller) |
| `LineBox` | 56 B | 56 B | — |
| `SharedComputedStyle` | — | **16 B** | — |

`tests/test_suite.cpp` pins `sizeof(LayoutBox) < 400` and
`sizeof(InlineFragment) < 128` via `static_assert`.

## 3. How it was verified

Three new engine test suites were added to `tests/test_suite.cpp`:

- `shared_computed_style_deduplicates_boxes_and_fragments` — builds a document
  with 200 repetitions of `word <b>bold</b> <span class="hl">mark</span> ` (>1,000
  boxes and fragments) and asserts all `style` pointers collapse to `<= 8`
  distinct addresses, plus `ComputedStylePool::intern` and `inherited_only`
  checks.
- `shared_percentage_styles_do_not_clobber_siblings` — lays out two boxes
  sharing `.half { width: 50%; margin-left: 10%; height: 20px; }` inside
  200 px and 400 px parents, then re-lays out one box at 300 px without
  affecting the sibling.
- `text_align_and_text_decoration_enums_parse_cleanly` — checks case-insensitive
  `text-align` and combined `text-decoration: underline line-through` bitmask
  parsing and inheritance.

`tools/bench/bench_shared_style.py` (`make bench-styles`) benchmarks 100,000
repetitions of `word <b>bold</b> `, `<a href="#">link</a>, `, and styled `<span>`
elements through `--dump-paint` via `os.wait4`:

```bash
python3 tools/bench/bench_shared_style.py build/litebird --baseline .cache/litebird-main --max-rss-mb 600 --max-seconds 10
# case             count       rss     time   base_rss   base_t  rss_chg  result
# word_100k       100000  352.0 MB    1.40s  1382.3 MB    2.67s   -74.5%  paint 6d38689f2dd7
# link_100k       100000  354.3 MB    2.02s  1162.2 MB    3.40s   -69.5%  paint 649dcb46730f
# span_bg_100k    100000  239.7 MB    1.30s   601.6 MB    1.79s   -60.2%  paint 06e8ed97dca3
#
# 3 case(s); 0 failure(s).
```

Against `ee88b1c`, all three benchmarks had byte-identical `--dump-paint`
output and cut peak RSS by **60.2% to 74.5%**. All 16 `examples/*.html` pages
and `examples/usable_browser_demo/index.html` also produced byte-identical
`--dump-layout` and `--dump-paint` dumps vs `ee88b1c`.

Full suite verification on the merged tree:

```bash
make -j2 test
# All 226 engine test suites passed successfully!
# Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

make bench-styles
# 3 case(s); 0 failure(s). (351.8 MB / 354.1 MB / 239.7 MB under 500 MiB guard)

make conformance        # 1792 / 1792 (100.0%)
make js-conformance     # 1126 / 1152 (97.7%)
make dom-conformance    # 669 / 669 (100.0%)
make url-conformance    # 1188 / 1188 (100.0%)
make xml-conformance    # 1718 / 1718 (100.0%)
make wpt                # 9550 / 9744 (194 listed, 0 unlisted)
make diff-trees         # 4 x 1500 / 1500 equal to corrected parse5 (0 differ)
make diff-strings       # 50580 / 50580 equal to Node.js (0 differ)
make verify-color verify-media verify-regexp verify-string-unicode
make security-fetch security-worker bench bench-css bench-floats bench-collections bench-attributes bench-tokenizer-attributes
make asan               # ASan+UBSan core corpora + fuzz (0 findings) + 882/882 deep pages (depth 10000, 2 MB stack)
```

One behavioral improvement revealed by synthetic differential testing: parsing
`text-decoration: underline line-through` into `TextDecoration` flags causes
`has_underline()` to emit the underline rectangle, whereas the previous exact
check `text_decoration == "underline"` omitted underlines on multi-keyword
declarations.

## 4. What is still open

- **Rare-data sub-structs inside `ComputedStyle`**: grid track definitions,
  filters, animations, transitions, transforms, and `custom_properties` still
  live inline inside the 832-byte struct; moving cold sub-groups behind
  copy-on-write shared pointers would shrink `ComputedStyle` further for pages
  with many distinct styles.
- **Matched-properties cascade cache**: `StyleResolver::resolve_style()` still
  constructs a temporary `ComputedStyle` on the stack per element before
  `ComputedStylePool::intern()` deduplicates it.
- **`line-through` and `overline` painting**: `TextDecoration` records both
  flags, while `DisplayList` currently paints only underlines (`has_underline()`).
- **Per-layout-tree pool scope**: `ComputedStylePool` lives for one
  `LayoutTree::build()` pass; `default_computed_style()` persists for the process.
