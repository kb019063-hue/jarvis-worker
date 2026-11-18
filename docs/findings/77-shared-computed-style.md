# Finding 77 — Every `LayoutBox` and `InlineFragment` carried its own 992-byte `ComputedStyle`

**Severity:** medium (perf / memory footprint — resolving AUDIT.md finding 12
and the `Every box carries its own copy of a 920-byte computed style` task row).

**Found by:** structural audit (`AUDIT.md`, finding 12) and measurement against
`ee88b1c`: `ComputedStyle` had grown to 992 bytes, `LayoutBox` to 1,320 bytes,
and `InlineFragment` to 1,064 bytes.

## 1. What was wrong

`LayoutBox` and `InlineFragment` embedded `ComputedStyle` by value, and
`ComputedStyle` stored five `std::string` properties (`text_align`,
`text_decoration`, `animation_timing_function`, `transition_timing_function`,
and default-initialized `transform_origin = "50% 50%"`) plus `int`-sized
enums. Every word or space split into an `InlineFragment` copied 992 bytes of
style, so `word <b>bold</b> ` repeated 100,000 times required **1,382.3 MiB** of
resident memory.

## 2. What changed

Phase 71 converts `text_align`, `text_decoration`, and timing functions to
`uint8_t` enums/bitmasks (shrinking `ComputedStyle` to 832 B), replaces inline
`ComputedStyle` members in `LayoutBox` and `InlineFragment` with a 16-byte
`SharedComputedStyle` handle backed by `default_computed_style()` and a
per-layout `ComputedStylePool`, uses copy-on-write `resolve_percentages()` so
siblings sharing percentage styles never overwrite each other, and caches
inline ancestor overrides per `(inline_box_style*, item_style*)`.

- `sizeof(LayoutBox)`: `1,320 B -> 344 B` (`-73.9%`)
- `sizeof(InlineFragment)`: `1,064 B -> 88 B` (`-91.7%` / `12.1x` smaller)

## 3. How it was verified

```bash
python3 tools/bench/bench_shared_style.py build/litebird --baseline .cache/litebird-main --max-rss-mb 600 --max-seconds 10
# word_100k       100000  352.0 MB    1.40s  1382.3 MB    2.67s   -74.5%  paint 6d38689f2dd7
# link_100k       100000  354.3 MB    2.02s  1162.2 MB    3.40s   -69.5%  paint 649dcb46730f
# span_bg_100k    100000  239.7 MB    1.30s   601.6 MB    1.79s   -60.2%  paint 06e8ed97dca3

make bench-styles
# 3 case(s); 0 failure(s).

make -j2 test
# All 220 engine test suites passed successfully! (including static_asserts on sizes and 3 new suites)
# Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).
```

Release conformance, fuzzing, differential dumps, and ASan+UBSan verification
all passed with zero regressions.

## 4. What is still open

- Rare CSS property sub-groups (`grid_*`, `filters`, `custom_properties`,
  transforms, animations/transitions) remain inline inside `ComputedStyle`.
- `DisplayList` still paints only `has_underline()`, not `has_line_through()` or
  `has_overline()`.
- `StyleResolver` still builds a temporary `ComputedStyle` on the stack per
  element before `ComputedStylePool::intern()`.
