# Phase 432: Arabic combining-mark advances (`ZERO_WIDTH_MARKS_BY_GDEF_LATE`)

Agent: Curlew

## Finding the problem

Phase 292 brought `DejaVuSans.ttf` and `DejaVuSans-Bold.ttf` to 23/23
same-placement matches on `tools/shaping/arabic_marks_diagnostic.py`, while
`DejaVuSansMono.ttf` remained at **5/23** same-placement matches (`18/23`
different-placement) and **0/384** supported same-placement matches on
`--matrix`. Only the five precomposed/composed cases with no surviving mark
glyph in the shaped run (`alef-hamza`, `alef-hamza-precomposed`, `alef-madda`,
`alef-hamza-below`, `lam-alef-hamza`) matched HarfBuzz.

Inspecting `DejaVuSansMono.ttf`'s `hmtx`, `GDEF`, and `GPOS` tables shows why:

- As a fixed-pitch font, `DejaVuSansMono.ttf` gives every glyph in `hmtx` —
  including combining marks (`GDEF` `GlyphClassDef == 3`) — the full cell
  advance width of `1233` font units.
- Under `latn`, `grek`, and `cyrl`, its `GPOS` `mark` feature runs Lookup 7
  (`SinglePos`, `x_advance = -1233`) alongside `MarkToBase` Lookup 6.
- Under `arab`, its `GPOS` scripts run `'mark': [2, 3, 4, 5]`
  (`MarkToLigature` and `MarkToBase`) and `'mkmk': [0, 1]` (`MarkToMark`), with
  **no `SinglePos` lookup**. Like other OpenType Arabic fonts, it relies on the
  shaping engine's late `GDEF` mark-zeroing pass
  (`HB_OT_SHAPE_ZERO_WIDTH_MARKS_BY_GDEF_LATE` in HarfBuzz's Arabic shaper) to
  zero advances on `GDEF` class-3 marks after `GPOS` positioning and before
  attachment offset propagation.

Without that pass, `TrueTypeFont::shape_glyphs` kept `x_advance = 1233` on every
surviving Arabic mark in `DejaVuSansMono`: `"بَ"` (`beh-fatha`) advanced the
terminal pen to `2466` instead of `1233`, `"السَّلَامُ"` (`greeting`) advanced
to `11097` (`9 * 1233`) instead of `6165` (`5 * 1233`) and shifted every base
cluster after a mark right by one monospace cell per mark, and Stage 4b's
`sum_adv_x` across multi-mark clusters subtracted nonzero intermediate mark
advances.

## Changes

In `TrueTypeFont::shape_glyphs` (`src/render/FontRenderer.cpp`), after `GPOS`
lookups and the legacy `kern` fallback have executed and before Stage 4a/4b
attachment offset resolution:

- When `script_tag == kTagArab && m_has_gdef_glyph_class`, every glyph in `run`
  whose `m_gdef_glyph_class.class_of(sg.glyph) == 3` has `sg.x_advance = 0` and
  `sg.y_advance = 0`.

Running this pass *after* `GPOS` (`BY_GDEF_LATE`, matching HarfBuzz 12.1.0)
rather than *before* `GPOS` ensures that Arabic fonts which *do* carry an
explicit `SinglePos` `x_advance = -width` zeroing lookup (such as
`build_test_arabic_latin_sfnt()` in `tests/test_suite.cpp`) still end at `0`
instead of `-width`, while preserving `SinglePos` `(x_placement, y_placement)`
offsets. Running it *before* Stage 4b ensures `sum_adv_x` and `sum_adv_y` across
`[mark_base, i)` do not accumulate zeroed intermediate mark advances when a
second mark attaches directly to the base. Non-mark `GDEF` classes (`!= 3`),
fonts without `GDEF` `GlyphClassDef`, and non-Arabic scripts are untouched.

## Verification

```sh
python3 -m venv /tmp/shaping-venv
/tmp/shaping-venv/bin/pip install -r tools/shaping/requirements.txt
make build/shaping-probe
/tmp/shaping-venv/bin/python tools/shaping/compare.py \
  --probe build/shaping-probe \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
/tmp/shaping-venv/bin/python tools/shaping/arabic_marks_diagnostic.py \
  --probe build/shaping-probe \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
/tmp/shaping-venv/bin/python tools/shaping/arabic_marks_diagnostic.py --matrix \
  --probe build/shaping-probe \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
```

HarfBuzz `12.1.0` / `uharfbuzz` `0.52.0` differential results (diagnostics exit
`1` while raw RTL cluster-order mismatches remain):

- `DejaVuSansMono.ttf` (`0f5db4f1749979d961019838b160bec74abdf7f9eca69553fe1aa856bbff49a4`):
  - `compare.py` (14 cases): same-placement **10 → 11** (`1 → 0`
    different-placement, `3` unsupported; `10` exact raw).
  - `arabic_marks_diagnostic.py` (23 cases): same-placement **5 → 23** (`18 → 0`
    different-placement; `5` exact raw, `18` RTL within-cluster order
    differences). `beh-fatha` terminal pen `2466 → 1233`, `joined-marks`
    `7398 → 3699`, `greeting` `11097 → 6165`.
  - `arabic_marks_diagnostic.py --matrix` (486 cases; 384 supported, 102
    unsupported lacking `U+0670`): same-placement **0 → 358** (`384 → 26`
    different-placement).
- `DejaVuSansMono-Bold.ttf` (`2964f6dac8e6e9d71613928340f17bf868e9ea51692cca333c79e74962f02233`):
  - `compare.py` same-placement **10 → 11** (`0` different-placement); 23-case
    diagnostic **5 → 23**; `--matrix` **0 → 368** (`16` different-placement,
    `102` unsupported).
- `DejaVuSans.ttf` (`abdc775b21b1bc470d50c97e790d276f2054b7504e56e5bd3e64f48d68582322`),
  `DejaVuSans-Bold.ttf`, and `DejaVuSerif.ttf`: unchanged (`23/23` and
  `484/486` same-placement on Sans; `7/7` and `8/8` exact across the Latin and
  decomposition diagnostics; `5/5` same-placement on Hebrew).
- An ASan+UBSan `build-asan/shaping-probe` reproduces the same `23/23`,
  `358/384`, and `11/11` `DejaVuSansMono.ttf` results with `0` sanitizer
  findings.

Unit, conformance, fuzz, and sanitizer suites:

- `tests/suites/432-arabic-mark-advances.cpp` (`arabic_gdef_marks_zero_advances_after_gpos`):
  synthetic monospaced Arabic `MarkToBase`, two-mark Stage 4b base attachment,
  `MarkToMark` stacking, visual RTL multi-cluster positioning, `font-kerning: none`,
  late-vs-early `SinglePos` timing (`dx_adv = -1200` and `+90/+40` preserving
  `(35, -15)` placement offsets), negative controls (`GDEF` class `1`, missing
  `GDEF`, and non-Arabic `latn`), plus host `DejaVu Sans Mono` shaping and
  `CanvasRenderingContext2D::measure_text`. Reverting `src/render/FontRenderer.cpp`
  fails at line 90 (`beh_fatha[1].x_advance == 0`).
- Release: `make test` (**479 / 479** suites, **114 / 114** cross-origin checks),
  `make conformance` (**1973 / 1973**), `make js-conformance` (**1153 / 1158**,
  everyday **90 / 90**), `make dom-conformance` (**678 / 678**, everyday
  **68 / 68**), `make url-conformance` (**1188 / 1188**),
  `make xml-conformance` (**1718 / 1718**), `make render-conformance`
  (**65 / 66** + **41 / 41**, `toDataURL` **45016 / 45016**),
  `make bidi-conformance` (**861948 / 861948**), `make wpt` (**9570 / 9824**,
  `254` listed, `0` unlisted), `make diff-trees` (`4 × 1500`, `0` differ),
  `make diff-xml` (`4000` differential + `3000` round-trip, `0` differ),
  `make fuzz` (`0` findings across HTML, pipeline, XML, media, color, and Brotli),
  and `make deep` (**1332** runs over **465** pages at depth `200000` on an
  `8 MB` stack, `0` failed).
- Sanitizers: ASan+UBSan (`build-asan/`) passed stretches `0, 1, 3, 4, 5` of
  `test_suite` (**477 / 479** suites; stretch `2`'s two `200000`-deep DOM suites
  exceed the sandbox's `3.8 GiB` RAM, per `Makefile` `asan-unit` note),
  `cross_origin_test` (**114 / 114**), `asan-verify`, `asan-js` (**1152 / 1158**,
  **90 / 90** everyday), `asan-dom` (**678 / 678** + **1188 / 1188** URL),
  `asan-xml` (**1718 / 1718**), `asan-wpt` (**9570 / 9824**, `0` unlisted),
  `asan-xml-generated` (`1000` differential + `1000` round-trip), `asan-fuzz`
  (`0` findings), and `asan-deep` (**882** runs over **315** pages at depth
  `10000` on a `2 MB` stack, `0` failed); `make tsan` passed both ThreadSanitizer
  and ASan+LSan+UBSan `JsString` stress checks.

## Limits

- The 26 remaining `different-placement` cases on `DejaVuSansMono.ttf --matrix`
  all have zeroed mark advances (`terminal_pen = [1233, 0]`): 2 are
  `0627-0653-0655` and `0627-0654-0655` (Arabic modified combining-class
  reordering before canonical composition, shared with `DejaVuSans.ttf`), and 24
  involve `U+0627` composed to `U+0622`/`U+0623`/`U+0625` followed by a second
  mark that has no `MarkToBase` anchor on that composite in `DejaVuSansMono.ttf`
  (an unattached mark in Litebird's `[base, mark]` RTL cluster sits after the
  base's `1233` advance rather than before it in HarfBuzz's reversed RTL stream).
- Raw RTL glyph-stream ordering, fallback mark classification when `GDEF` is
  absent, late `GDEF` mark zeroing for scripts outside `arab`, source clusters,
  and full Unicode normalization remain open.
