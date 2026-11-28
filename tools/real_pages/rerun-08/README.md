# Rerun 08 — strict real-page visual check after Phase 491

Measured 2026-11-21 on the current work branch, based on `main` at
`5149a41fa57ffdfb1e14afa2583c9680eb369de4`, with the Phase 491 CSS selector
matcher change. All five frozen source revisions matched `pages.json`. The
HeadlessChrome/131.0.6778.0 oracle validated **20/20** tasks and Litebird
completed **20/20**. Per-task logs and JSON are beside this report; full PNGs,
captured sites and browser/runtime files stay in the disposable `/tmp` tree.

## Exact visual score

The comparator is unchanged: post-action RGBA at 800×600, zero different pixels
allowed, no alignment or tolerance. **0 of 19 comparable tasks are exact.**
Task 16 (2048) is excluded only because its initial board is randomized. Task
17's small pixel movement includes its animated start frame. This is not a claim
of page parity.

The defect fixed here was one concrete false-positive style match. The CSS
layout matcher ignored `:valid` and `:invalid`, so the pinned Mothereff rule
`.invalid, :invalid { background: #ffb6c1; }` tinted non-controls and the canvas
pink. That visibly wrong canvas is now gray/white. Compared with the pre-fix
Rerun 07, the task scores are:

| Task | Page | Changed pixels before | After | Fewer |
| :--- | :--- | ---: | ---: | ---: |
| 13 | Base64 | 476,809 | 345,492 | 131,317 |
| 14 | Binary ASCII | 477,445 | 344,825 | 132,620 |
| 15 | UTF-8 | 466,016 | 332,549 | 133,467 |
| 19 | HTML entities | 465,800 | 330,348 | 135,452 |
| **Total** | | **1,886,070** | **1,353,214** | **532,856** |

The rest remains unequal: in the reviewed tool-page frames the Litebird textareas
are shorter/white rather than Chromium's tall green boxes; the page width,
spacing and text metrics also differ. Tasks 1–12, 18 and 20 had identical pixel
counts before/after. Task 17 changed by 8 pixels between sampled frames; its
animation makes that tiny movement non-diagnostic. Do not attribute that to the
CSS change.

The red/green engine regression is
`tests/suites/491-valid-invalid-css-pseudo-classes.cpp`: before the fix, an
ordinary white box's painted PNG pixel was not white; after the fix it is. It
also keeps non-controls out of both pseudo-classes and checks valid/invalid
controls through `Element.matches()`. `make test` passed **525/525** suites and
the cross-origin suite passed **114/114**.
