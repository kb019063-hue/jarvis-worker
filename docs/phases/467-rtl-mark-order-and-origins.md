# Phase 467 — RTL Mark Order, Attachment Offset Propagation, and Unanchored Composite Mark Origins

Resolves [Finding 467](../findings/467-rtl-mark-order-and-origins.md) under the shaping conformance track (`AGENTS.md`: *"Adjacent Latin composition asks the font (Phase 234, finding 238)"*).

## Summary

`TrueTypeFont::shape_glyphs` and `FontRenderer::shape_text` previously emitted RTL mark clusters in `[base, mark_1, ..., mark_k]` order with LTR-style `-(sum_{k in [j, i)} advance_k)` offset compensation instead of HarfBuzz's visual RTL buffer order `[mark_k, ..., mark_1, base]` with `+(sum_{k in (j, i]} advance_k)` attachment offset propagation. For anchored marks with zero advance, the two representations placed ink at the same coordinates while diverging on raw glyph-stream order and offsets (`hebrew-marks`, `arabic-marks`, and every multi-glyph RTL mark probe in `hebrew_diagnostic.py` and `arabic_marks_diagnostic.py`). For unattached zero-advance marks on unanchored RTL bases — notably precomposed `U+0622` (`آ`), `U+0623` (`أ`), and `U+0625` (`إ`) in `DejaVuSansMono.ttf` and `DejaVuSansMono-Bold.ttf`, which carry `1233`-unit advances and no `MarkToBase` base-anchor entry — `[base, mark]` advanced the pen across the base *before* drawing the mark, shifting every unattached mark `+1233` font units (one full monospace cell) to the right of its base.

Phase 467 aligns RTL mark offset propagation, buffer reversal, and `FontRenderer::shape_text` odd-level Bidi shaping with HarfBuzz 12.1.0.

## Design & Implementation

### 1. Direction-Aware Stage 4b Mark Attachment Offset Propagation (`src/render/FontRenderer.cpp`)

In logical buffer order (`0 <= j < i`), `run[i]` attaches to `run[j]` (`j = run[i].mark_parent`) with anchor delta:

$$\Delta_i = (\text{base\_anchor}_x(j) - \text{mark\_anchor}_x(i),\; \text{base\_anchor}_y(j) - \text{mark\_anchor}_y(i))$$

stored initially in `(run[i].x_offset, run[i].y_offset)`.

- **LTR (`!do_rtl`)**: `i` remains *after* `j` in visual order, so the pen advances by $\sum_{k=j}^{i-1} \text{adv}_k$ between drawing `j` and reaching `i`. Subtracting $\sum_{k \in [j, i)} \text{adv}_k$ from `run[i]`'s offset yields visual origin $\text{Origin}(i) = \text{Origin}(j) + \Delta_i$.
- **RTL (`do_rtl`)**: Stage 5 reverses the buffer, placing `i` *before* `j` in visual left-to-right order. Between reaching `i` and reaching `j`, the pen advances over reversed positions `i, i - 1, ..., j + 1`, whose sum is $\sum_{k=j+1}^{i} \text{adv}_k$. Adding `run[j].x_offset`/`y_offset` plus $\sum_{k \in (j, i]} \text{adv}_k$ to `run[i]`'s offset (matching HarfBuzz `OT::Layout::GPOS::propagate_attachment_offsets` in `src/OT/Layout/GPOS/GPOS.hh:92-145`) gives:

$$\text{pen}(j) = \text{pen}(i) + \sum_{k=j+1}^{i} \text{adv}_k \implies \text{Origin}(i) = \text{pen}(i) + \text{off}(i) = \text{Origin}(j) + \Delta_i$$

Because `resolve_mark(i)` recurses on `resolve_mark(j)` before accumulating advances, `MarkToMark` chains (`base <- mark_1 <- mark_2`) and marks attached to `CursivePos`-shifted RTL bases compose transitively in $O(N)$ time (`resolved_mark[i]` memoizes each index).

### 2. Full Stage 5 Visual RTL Buffer Reversal (`src/render/FontRenderer.cpp`)

When `do_rtl` (`visual_rtl`) is true, Stage 5 replaces the cluster-preserving `[base, mark_1, ..., mark_k]` reversal loop with `std::reverse(run.begin(), run.end())`. Logical `[base_1, m_{1,1}, m_{1,2}, base_2, m_{2,1}]` becomes visual `[m_{2,1}, base_2, m_{1,2}, m_{1,1}, base_1]`:

- Every anchored mark lands at the exact same visual coordinates relative to its base.
- Every unattached zero-advance mark (`mark_parent == -1`, `x_advance = 0`, `x_offset = 0`) precedes its base at the same pen position, so `Origin(mark) == Origin(base)` instead of `Origin(base) + x_advance(base)`.

### 3. `FontRenderer::shape_text` `pre_reversed` Bidi Alignment (`src/render/FontRenderer.cpp`)

`LayoutBox` and `BitmapRenderer` pass odd-level UAX #9 Bidi runs to `FontRenderer::shape_text` with `visual_rtl = true, pre_reversed = true` (codepoints already reversed to visual L-to-R order by UAX #9 rule L2). Previously, `shape_text` restored `fr.is_rtl` runs to logical order, shaped with `visual_rtl = false`, and applied a second cluster-preserving reversal to the whole run list. Now:

- `face_visual_rtl = visual_rtl && (!pre_reversed || fr.is_rtl)`:
  - When `pre_reversed && fr.is_rtl`, `fr.cps` is reversed back to logical order and shaped with `face_visual_rtl = true`, producing HarfBuzz-identical `[mark_k, ..., mark_1, base]` glyphs per `FaceRun`.
  - When `pre_reversed && !fr.is_rtl` (for example `<bdo dir="rtl">` overriding Latin characters + combining marks), `fr.cps` is restored to logical order, shaped with `face_visual_rtl = false`, and cluster-reversed so combining marks stay attached to their overridden Latin base.
  - Because `cps` arrived in visual L-to-R order, `runs` are already ordered visually left-to-right (whereas when `!pre_reversed && visual_rtl`, `runs` are in logical R-to-L order and iterated in reverse).

## Differential & Suite Verification

### HarfBuzz 12.1.0 (`uharfbuzz` 0.52.0) Before vs After

| Face (`SHA-256` prefix) | Harness | Raw `match` (Before -> After) | `same-placement` (Before -> After) |
|---|---|---|---|
| `DejaVuSans.ttf` (`abdc775b...`) | `compare.py` (13 supported, 1 `devanagari` unsupported) | `11/13` -> **`13/13`** | `13/13` -> **`13/13`** |
| `DejaVuSans.ttf` (`abdc775b...`) | `hebrew_diagnostic.py` / `--permutations` | `2/5` -> **`5/5`** / `0/12` -> **`12/12`** | `5/5` / `12/12` |
| `DejaVuSans.ttf` (`abdc775b...`) | `arabic_marks_diagnostic.py` / `--ordering` / `--matrix` | `5/23` -> **`23/23`** / `3/16` -> **`16/16`** / `0/486` -> **`486/486`** | `23/23` / `16/16` / `486/486` |
| `DejaVuSans-Bold.ttf` (`0d977336...`) | `compare.py` / `hebrew` (`+perm`) / `arabic` (`+ord`, `+mat`) | `11/13` -> **`13/13`** / **`5/5`**, **`12/12`** / **`23/23`**, **`16/16`**, **`486/486`** | **`100%`** across all supported cases |
| `DejaVuSansMono.ttf` (`0f5db4f1...`) | `compare.py` (11 supported, 3 unsupported) | `10/11` -> **`11/11`** | `11/11` -> **`11/11`** |
| `DejaVuSansMono.ttf` (`0f5db4f1...`) | `arabic_marks_diagnostic.py` / `--ordering` | `5/23` -> **`23/23`** / `3/16` -> **`16/16`** | `23/23` / `13/16` -> **`16/16`** |
| `DejaVuSansMono.ttf` (`0f5db4f1...`) | `arabic_marks_diagnostic.py --matrix` (384 supported, 102 unsupported) | `0/384` -> **`384/384`** | `358/384` -> **`384/384`** (`26 -> 0` diff) |
| `DejaVuSansMono-Bold.ttf` (`2964f6da...`) | `compare.py` / `arabic` / `--ordering` / `--matrix` (384 supported) | `10/11` -> **`11/11`** / **`23/23`** / **`16/16`** / **`384/384`** | **`11/11`** / **`23/23`** / `15/16` -> **`16/16`** / `370/384` -> **`384/384`** (`14 -> 0` diff) |

- Latin diagnostics (`latin_diagnostic.py`, `latin_decomposition_diagnostic.py`, `latin_mark_order_diagnostic.py`, `supported_composite_diagnostic.py`, `leading_marks_diagnostic.py`): `36/36` exact raw `match` on each of the 4 faces (`144/144` total).
- Extended sweeps: `2,554 / 2,554` supported Arabic multi-mark/ligature/phrase cases and `800 / 800` supported Hebrew multi-mark cases match HarfBuzz 12.1.0 raw glyph streams (`0` mismatches); `tools/shaping/ink.py` (`FreeType 2.13.1` / `freetype-py 2.5.1`) reports `0` changed pixels at 16, 32, and 64 px.

### Portable & Host-Font Regression Tests

- `tests/suites/467-rtl-mark-order-and-origins.cpp`:
  - Synthetic `arab`/`hebr`/`latn` SFNT verifying single-mark RTL `[mark, base]` order and visual origin invariance, `MarkToMark` stacking (`[mark2, mark1, base]`), nonzero mark `x_advance`/`y_advance` `(j, i]` offset accumulation, RTL `CursivePos` + `MarkToBase` composition, unanchored `U+0622 + U+064E` Mono composite mark origin (`x = 0`, not `+1200`), `FontRenderer::shape_text` `pre_reversed = false` vs `pre_reversed = true` parity, and `<bdo dir="rtl">` Latin cluster preservation; plus host `DejaVuSans.ttf` and `DejaVuSansMono.ttf` checks when installed.
- `tests/suites/432-arabic-mark-advances.cpp` and `tests/test_suite.cpp` (`test_multiscript_itemization_visual_rtl_and_bidi_mixed_runs`): updated legacy `[base, mark]` RTL glyph-order assertions to `[mark, base]`.
