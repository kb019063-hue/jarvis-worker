# Finding 467 — RTL Mark Order, Attachment Offset Propagation, and Unanchored Composite Mark Origins

- **Severity**: Medium — OpenType RTL glyph-stream conformance and monospaced Arabic/Hebrew mark placement
- **Status**: Resolved in Phase 467 (`src/render/FontRenderer.cpp`)
- **Affected Files**: `src/render/FontRenderer.cpp` (`TrueTypeFont::shape_glyphs` Stage 4b/Stage 5 and `FontRenderer::shape_text`), `tools/shaping/README.md`, `tests/test_suite.cpp`, `tests/suites/432-arabic-mark-advances.cpp`, `tests/suites/467-rtl-mark-order-and-origins.cpp`

## Problem

After Phases 289, 292, 432, and 442 aligned Hebrew and Arabic pre-`GSUB` decomposition, OpenType modified combining-class ordering (`CCC10..26`, `CCC27..33`, UTR #53 MCM `220 -> 25`, `230 -> 26`), and `arab` late `GDEF` class-3 mark-advance zeroing, two coupled RTL gaps remained in `TrueTypeFont::shape_glyphs` and `FontRenderer::shape_text`:

1. **Cluster-preserving RTL output order and LTR-only mark offset propagation (`src/render/FontRenderer.cpp:3122-3159`)**:
   - Stage 4b subtracted intermediate glyph advances over `[j, i)` (`dx -= run[k].x_advance`) for both LTR and RTL runs, compensating each attached mark as if it would remain *after* its base in output order.
   - Stage 5 then reversed RTL cluster order while keeping `[base, mark_1, ..., mark_k]` order inside each cluster instead of reversing the shaped buffer like HarfBuzz (`hb_buffer_reverse()`), where logical `[base, mark_1, ..., mark_k]` becomes visual `[mark_k, ..., mark_1, base]` after `GPOS::propagate_attachment_offsets` adds advances over `(j, i]`.
   - Consequently, every multi-glyph RTL mark cluster mismatched HarfBuzz's raw glyph stream (`compare.py` `hebrew-marks` and `arabic-marks`, `3/5` `hebrew_diagnostic.py`, `12/12` `hebrew_diagnostic.py --permutations`, `18/23` `arabic_marks_diagnostic.py`, `13/16` `arabic_marks_diagnostic.py --ordering`, `486/486` `DejaVuSans` `--matrix`, and `384/384` supported `DejaVuSansMono` `--matrix`).

2. **Misplaced unattached zero-advance marks on unanchored RTL bases (`26` `DejaVuSansMono.ttf` and `14` `DejaVuSansMono-Bold.ttf` `--matrix` cases, plus `3` `DejaVuSansMono.ttf` and `1` `DejaVuSansMono-Bold.ttf` `--ordering` cases)**:
   - `DejaVuSansMono.ttf` defines no `MarkToBase` base-anchor entry on precomposed Arabic Alef variants `U+0622` (`gid 1317`, `x_advance = 1233`), `U+0623` (`gid 1319`), or `U+0625` (`gid 1323`), so following marks remain unattached (`mark_parent == -1`, `x_offset = 0`, `x_advance = 0`).
   - Under `[base, mark]`, `base` advanced the pen by `+1233` *before* the unattached zero-advance mark was drawn, placing the mark at `x = 1233` (one full cell to the right of the glyph's cell) instead of `x = 0` (`HarfBuzz: [[mark, 0, 0, 0, 0], [base, 1233, 0, 0, 0]]`, where both glyphs share origin `x = 0`).

3. **Asymmetric `pre_reversed` odd-level Bidi handling in `FontRenderer::shape_text` (`src/render/FontRenderer.cpp:4377-4435`)**:
   - When `LayoutBox` or `BitmapRenderer` passed an odd-level UAX #9 Bidi run with `pre_reversed = true`, `shape_text` re-reversed `fr.is_rtl` codepoints back to logical order and invoked `shape_glyphs(..., /*visual_rtl=*/false)`, then performed its own cluster-preserving reversal (`[base, mark_1, ..., mark_k]`), diverging from `shape_glyphs(..., /*visual_rtl=*/true)` on direct `pre_reversed = false` calls.

## Root Cause & Fix

1. **Direction-aware Stage 4b mark attachment propagation + full Stage 5 RTL buffer reversal (`src/render/FontRenderer.cpp`)**:
   - Matching HarfBuzz 12.1.0 `OT::Layout::GPOS::propagate_attachment_offsets` (`src/OT/Layout/GPOS/GPOS.hh:92-145`), `resolve_mark(i)` resolves `resolve_mark(j)` (`j = run[i].mark_parent`, `0 <= j < i`) and:
     - when `!do_rtl`: subtracts `run[k].x_advance` and `run[k].y_advance` over `k in [j, i)`, placing `i` after `j` in LTR visual order;
     - when `do_rtl`: adds `run[k].x_advance` and `run[k].y_advance` over `k in (j, i]` (including `i`'s own advance when nonzero), placing `i` before `j` in RTL visual order at the identical visual origin relative to `j`.
   - Stage 5 replaces the cluster-preserving RTL reversal loop with `std::reverse(run.begin(), run.end())`, emitting `[mark_k, ..., mark_1, base]` in visual left-to-right order and placing unattached zero-advance marks immediately before their base so both draw at the base's visual pen origin.

2. **Unified `FontRenderer::shape_text` `pre_reversed` RTL shaping (`src/render/FontRenderer.cpp`)**:
   - `shape_text` computes `face_visual_rtl = visual_rtl && (!pre_reversed || fr.is_rtl)`: for `pre_reversed` RTL face runs (`fr.is_rtl`), codepoints are restored to logical order and shaped with `visual_rtl = true`, while `runs` (already ordered visually right-to-left because `cps` arrived pre-reversed) are emitted in reverse `FaceRun` order without a second per-glyph reversal; non-RTL face runs (`!fr.is_rtl`, e.g. `<bdo dir="rtl">` on Latin text) continue to shape logically with `face_visual_rtl = false` and reverse clusters intact so marks remain over their reversed Latin bases.

## Verification

- **HarfBuzz 12.1.0 (`uharfbuzz` 0.52.0) Differential Suites across 4 DejaVu Faces (`build/shaping-probe`)**:
  - `DejaVuSans.ttf` (`abdc775b21b1bc470d50c97e790d276f2054b7504e56e5bd3e64f48d68582322`) & `DejaVuSans-Bold.ttf` (`0d977336a6d5fba34eab8e3199eb218327161b5143749f802982c2bc34df0c96`):
    - `compare.py`: exact raw `match` **11/13 -> 13/13** (`hebrew-marks`, `arabic-marks` now exact `match`; `1` unsupported `devanagari`).
    - `hebrew_diagnostic.py`: exact raw `match` **2/5 -> 5/5** (`exit 0`); `--permutations`: **0/12 -> 12/12** (`exit 0`).
    - `arabic_marks_diagnostic.py`: default **5/23 -> 23/23** exact raw `match` (`exit 0`); `--ordering` **3/16 -> 16/16** exact raw `match` (`exit 0`); `--matrix` **0/486 -> 486/486** exact raw `match` (`exit 0`).
  - `DejaVuSansMono.ttf` (`0f5db4f1749979d961019838b160bec74abdf7f9eca69553fe1aa856bbff49a4`):
    - `compare.py`: exact raw `match` **10/11 -> 11/11** (`3` unsupported).
    - `arabic_marks_diagnostic.py`: default **5/23 -> 23/23** exact raw `match` (`exit 0`); `--ordering` `same-placement` **13/16 -> 16/16** and exact raw `match` **3/16 -> 16/16** (`exit 0`); supported `--matrix` `same-placement` **358/384 -> 384/384** (`26 -> 0` `different-placement`) and exact raw `match` **0/384 -> 384/384** (`102` unsupported lacking `U+0670`).
  - `DejaVuSansMono-Bold.ttf` (`2964f6dac8e6e9d71613928340f17bf868e9ea51692cca333c79e74962f02233`):
    - `compare.py`: exact raw `match` **10/11 -> 11/11** (`3` unsupported).
    - `arabic_marks_diagnostic.py`: default **5/23 -> 23/23** exact raw `match` (`exit 0`); `--ordering` `same-placement` **15/16 -> 16/16** and exact raw `match` **3/16 -> 16/16** (`exit 0`); supported `--matrix` `same-placement` **370/384 -> 384/384** (`14 -> 0` `different-placement`) and exact raw `match` **0/384 -> 384/384** (`102` unsupported).
  - Extended RTL sweeps: `2,554 / 2,554` supported Arabic multi-mark/ligature/phrase cases and `800 / 800` supported Hebrew multi-mark cases achieve `100%` exact raw `match`; `tools/shaping/ink.py` reports `0` changed pixels at 16/32/64 px across `compare.py`, `arabic_marks_diagnostic.py --ordering` (`DejaVuSansMono.ttf`), and `hebrew_diagnostic.py --permutations` (`DejaVuSans.ttf`).
- **Regression Suite**: `tests/suites/467-rtl-mark-order-and-origins.cpp` (plus updated RTL glyph-order expectations in `tests/suites/432-arabic-mark-advances.cpp` and `tests/test_suite.cpp`).
