# Phase 491: CSS `:valid` and `:invalid` match validation controls

The CSS cascade had a different selector path from the DOM selector API. In
`StyleResolver::matches_simple_selector`, recognized dynamic pseudo-classes
checked the live element state, but `:valid` and `:invalid` had no cases. An
unrecognized pseudo-class therefore fell through as if it matched. The Mothereff
pages use `.invalid, :invalid { background: #ffb6c1; }`; the absent cases made
that rule tint the document canvas and ordinary page content, even when the
actual textarea values were valid. The script-facing selector and
`getComputedStyle` path already produced the standards-correct answers, which
is why a test using only that API falsely appeared to cover the paint path.

The layout matcher now asks `forms::validity_for_selector` for both pseudo-classes.
It only returns `:valid` or `:invalid` for elements that participate in
constraint validation and whose current validity matches. Non-controls match
neither. This reuses the same validity state as the DOM selector API rather than
adding a second definition of a valid control.

`tests/suites/491-valid-invalid-css-pseudo-classes.cpp` uses the production
`BrowserShell` paint path and decodes its PNG. Before the fix, the ordinary white
box at `(10, 10)` was not white; after the fix it is `0xFFFFFFFF`. The test also
checks that the body matches neither pseudo-class, a valid textarea matches
`:valid`, and an invalid email input matches `:invalid`. The test was first run
red, then passed after the matcher change. `make test` passes **525/525** suites
and the cross-origin test reports **114 checks, 0 failures**. `make render-conformance` reports **66/67** reftest pages matching (the sole recorded failure remains the known AVIF decoder limitation), **41/41** body-origin pages, and **45,016/45,016** canvas `toDataURL` pixel checks.

## Strict real-page measurement

The full frozen corpus was rerun with the Chromium reference revalidated (20/20)
and the Litebird task driver (20/20). The strict 800×600 full-RGBA oracle still
has **0 exact matches in 19 comparable frames**; randomized 2048 is not scored.
This change is a partial visual correction, not page parity. Compared with the
pre-fix Rerun 07, the four pages using the affected rule improved by:

| Task | Page | Different pixels before | After | Fewer |
| :--- | :--- | ---: | ---: | ---: |
| 13 | Mothereff Base64 | 476,809 | 345,492 | 131,317 |
| 14 | Mothereff Binary ASCII | 477,445 | 344,825 | 132,620 |
| 15 | Mothereff UTF-8 | 466,016 | 332,549 | 133,467 |
| 19 | Mothereff HTML entities | 465,800 | 330,348 | 135,452 |
| **Total** | | **1,886,070** | **1,353,214** | **532,856** |

Visual review confirms the erroneous pink canvas is gone. The remaining pixels
are still a large mismatch: Litebird's controls, page width, spacing, typography
and other layout details do not yet match Chromium. The saved task results and
per-page visual counts are in [`tools/real_pages/rerun-08/`](../../tools/real_pages/rerun-08/).
