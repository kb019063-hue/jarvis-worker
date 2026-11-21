# Litebird handoff: the frozen development set and its first holdout

This is a dated checkpoint for the work in [PR #158](https://github.com/kb019063-hue/jarvis-worker/pull/158), not the project-wide backlog or a live compatibility score. For the current work queue and file claims, read [AGENTS.md](AGENTS.md). For project-wide conformance numbers, see [the metrics ledger](docs/metrics.json).

## Where we started

The first recorded Litebird engine commit, [`5b945b6` (4 November 2026)](https://github.com/kb019063-hue/jarvis-worker/commit/5b945b6a9043ec6968c5e1095005fb69c7549715), already had a C++20 DOM, layout, JavaScript VM and rasterizer. The question for this effort was different: could a person use an actual saved page to finish a specific task?

We froze [20 tasks and their source revisions](tools/real_pages/pages.json) before scoring them: five Python proposals, five Git manuals, eight text tools, and two games. Both browsers received the same saved assets at 800×600. Chromium completed all 20. Litebird initially completed seven text-tool tasks; none of the ten navigation tasks completed. Five Git pages and Hextris exited before drawing a frame.

| Checkpoint | Interactions completed | What changed |
| --- | ---: | --- |
| [Baseline](tools/real_pages/Litebird-browsing-report.pdf) | 7/20 | Ordinary form input worked on seven tools. No credit for loading a page without completing its task. |
| [Git manuals](tools/real_pages/rerun-01/git-manuals.pdf) | 12/20 | Fixed selector traversal and link hit testing. Five clicks reached readable destination headings. |
| [Python articles](tools/real_pages/rerun-02/python-articles.pdf) | 17/20 | Disclosure and table layout, selector matching, and painting fixes made all five linked sections readable. Geometry alone had incorrectly suggested two blank frames passed. |
| [2048](tools/real_pages/rerun-03/2048.pdf) | 18/20 | An arrow produced one legal opening move and a spawn. This was not a full game or a layout pass. |
| [Punycode](tools/real_pages/rerun-04/punycode.pdf) | 19/20 | Select All replaced `mañana.com` with `bücher.de` in the page. Focus, both field values, and the permalink matched Chromium after each character. Final output: `xn--bcher-kva.de`. |
| [Hextris](tools/real_pages/rerun-05/hextris.pdf) | 20/20 | A hit-tested primary press starts the game; the board and pause glyph reach the screen. Not a full playthrough. |

## Where the evidence stops

At build `771fb6f`, all 20 development interactions completed. [None of 19 comparable post-action frames matched exactly](tools/real_pages/rerun-05/strict-frames.json); independent 2048 boards are excluded. Hextris has different type, spacing and pause-button geometry; 2048 still has offset tiles. This set repeats templates and informed the fixes. It is not a percentage of the web.

The [first holdout](tools/real_pages/holdout-01/README.md) froze four new publishers before testing the same build. Chromium passed 4/4. Litebird passed **0/4 strict gates**: one visible user action (the W3C tab switch) completed despite incorrect JS geometry, while Rust chapter navigation, MDN dialog cancellation and TodoMVC completion did not. Keep the sets separate. The scores refer to the recorded runs, not every subsequent merge.

- **Punycode editing is narrow.** Plain, single-text-node hosts support replacement; click placement is at the end, not the clicked glyph. The caret and selection highlight aren't painted. Font sizes, page width, spacing and focus border differ (26,928 of 480,000 post-edit pixels). Rich text and IME are not established. A raw “enable JavaScript” warning that used to appear during successful conversion was fixed; keep testing `noscript` with scripting both on and off.
- **The GUI title can be stale.** A separate smoke test visibly replaced the text, but the wrapper still displayed the initial title after the page changed `document.title`. Verify the rendered result rather than using the title as a proxy for it.

## First steps after merging

1. Check CI on the **merge SHA**, not just the PR. For example, `gh pr view 158 --json mergeCommit --jq .mergeCommit.oid`, then `gh run list --branch main --json databaseId,headSha,status,conclusion` and match `headSha`. A green build is not a new browsing measurement.
2. To measure a changed tree, follow [the reproduction commands](tools/real_pages/README.md#rerun-on-linux-from-repository-root). Start in fresh scratch directories; run the pinned Chromium oracle, then the native probe and `strict_frames.py`. Retain before/after frames. Don't change the manifest to recover a score.
3. Run the focused article-paint, arrow, editable-input, and `noscript` checks in [the benchmark README](tools/real_pages/README.md), plus `make test`. Record regressions against the earlier rows, including crashes, unreadable results and minor rendering differences. Put new evidence in a **new** rerun directory rather than overwriting the reports above.
4. Next engineering task: reduce the MDN dialog failure. Verify closed rendering, the open modal/backdrop, cancellation, and focus restoration. Keep the geometry check on W3C tabs even though the biography was readable. Check open PR claims before editing shared files.
