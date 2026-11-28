# Rerun 07 — frozen real-page interactions on Litebird

Measured 2026-11-21 against the production browser shell built from `main` at
`698aa75cdb3d6febb87d33736bdab00a15823dae` (Phase 469 merge), on the
`arena/a5797bd9-jarvis-worker` work branch. No engine sources were changed for
this run.

The five source revisions in `pages.json` were captured and checked before the
probe ran. The independent Chromium 131.0.6778.0 oracle validated all 20 tasks;
Litebird completed all 20 as well. There were no blocked reference tasks,
missing frames, timeouts, or premature process exits. Task 16 is one legal 2048
arrow-key move with its expected single spawn, not a full game. Task 17 checks
only the Hextris start transition and visible board/pause ink, not gameplay.
The per-task evidence is in `reference.json`, `litebird.json`, and `lite-1.log`
through `lite-20.log`.

## Rendering is not at parity

The strict comparator allows **zero** different RGBA pixels at 800×600. Of the
19 tasks with comparable completed post-action frames, **0 were exact**. Task
16 (2048) is excluded because the two boards start randomly; this is not a
20-row visual score. Exact per-task changed-pixel counts are in
`strict-frames.json`. In the Hextris start frame (task 17), 102,333 of 480,000
pixels differ; this does not assess later gameplay.

Visual review of the frame pairs confirms the pixel differences are visible,
not just antialias noise: the Python article and utility pages have substantial
layout/color differences; the 2048 tile grid is offset; and the game/editor
pages differ in their control styling, spacing and text layout. Interaction
success must not be read as visual fidelity.

For reference stability, the new Chromium after-frames were compared with the
committed fresh reference frames from rerun-06. Tasks 1–15 and 18–20 were
pixel-identical; only the randomized 2048 and animated Hextris frames changed.
The task definitions and five pinned source revisions did not change.

Full-resolution PNG captures, the captured source trees, browser bundle and
runtime libraries stay in the disposable scratch directory, not Git. The JSON
summaries and concise probe logs are retained here so the interaction and
visual counts can be inspected without committing generated images.
