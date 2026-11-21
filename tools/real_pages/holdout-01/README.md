# Holdout 01 · Four unfamiliar browsing tasks

**First exposure only.** On the local `771fb6f` engine build that completed v1's 20 tasks, Chromium completed **4/4** holdout tasks. Litebird passed **0/4 automated gates**. On review, **1/4 user actions visibly completed**, with a measurable API/layout defect, and three did not. Neither figure is a measure of general web compatibility. Do not add this denominator to v1's.

[Side-by-side PDF](results/holdout.pdf) · [frozen manifest](manifest.json) · [native traces](results/litebird.json) · [Chromium traces](results/reference.json) · [frame review](results/review.json).

| Task | Native observation | Strict verdict |
|---|---|---|
| Rust Book: Getting Started → Installation | The source heading rendered. Two Next chapter links were found, with boxes 760×0 and 0×0. No viewport click could be sent to their centers; the page stayed at Getting Started. | Blocked, not a navigation pass. |
| W3C APG: first tab → Carl Andersen with ArrowRight | Tab 2 had focus and selection, tab 1 lost selection, and the biography is readable in the after frame. The JS rectangle reports its top at y≈2061 in a 600px viewport, with `window.scrollY` 0. The screenshot contradicts the failed geometry assertion. The tabs also stack vertically, and the page logs a syntax error at `import`. | User action completed; **not** a clean automated or visual pass. |
| MDN: open and cancel a modal | The open flag and `animalNotChosen` return value changed, but the form was visible before opening and after canceling. No visible modal/backdrop; focus stayed on Cancel instead of returning to Update details. | Incomplete. A return value alone is insufficient. |
| TodoMVC ES5: add and complete Buy mangoes | Enter added one correctly named item and cleared the input. The toggle's center hit a label rather than the checkbox; the driver did not click a different target. One item remained. A script also reported an unsupported `with` statement. | Add passed; complete did not. No full-task pass. |

**Pixels.** Zero tolerance on 800×600 RGBA post-action frames. None of the four raw pairs matches. Only the tabs reached a comparable end state: that pair is a visual failure. The other three pairs document different outcomes; their pixel counts are not rendering scores. The dialog report also shows the open intermediate state.

**What was frozen and when.** Local commit `25374e7` recorded the four upstream SHAs, tasks, criteria, viewport and timeout before either browser ran. The local engine source is `771fb6f8f1eba0b103b0cfa60d565e5a0b3850c2`; measured `build/litebird` SHA-256 `55500140fa511ac89542ea7de54c391357a81249918bce1a1a013eeeea07e13b`. The evaluators share the read-only observations and action sequence; their first versions are in `0363920`, before the native run. No engine fixes, page substitutions, or score-dependent changes to the criteria. A reference replay MIME error was corrected (`.mjs` must be served as JavaScript); all four Chromium outcomes were unchanged, with no missing same-origin requests or page errors in the final reference run. Off-origin requests are blocked in both browsers; this is not a test of the live deployed pages. The task set is now exposed: future fixes on it are regressions/improvements against a known set, **not** another unseen test.

**Reproduce on Linux.** From repository root, use the engine revision above (not an arbitrary newer `main`) and a fresh scratch directory. Follow the v1 [Chromium dependency setup](../README.md#rerun-on-linux-from-repository-root) for `NODE_PATH`, the browser package and `LD_LIBRARY_PATH`. `capture.py` uses `gh api` and codeload for the pinned static files, and verifies TodoMVC's two runtime packages against its upstream lockfile. It never installs/runs upstream package scripts.

```sh
H=tools/real_pages/holdout-01; R=$(mktemp -d)
python3 "$H/capture.py" "$R/sites"
node "$H/oracle.cjs" "$R/sites" "$R/run"
make -j2 build/litebird
g++ -std=c++20 -Iinclude "$H/drive.cpp" $(find build -mindepth 2 -name '*.o') -o "$R/driver"
python3 "$H/run.py" "$R/sites" "$R/run" "$R/driver"
```

Read **all** rows (the runner records nonpasses rather than stopping at the first one). Native: fresh process per task, sockets denied, 45s wall budget, real shell clicks/keys, read-only JS observations. Reference: fresh context per task, same local origin/files, real mouse and keyboard. Initial and final screenshots, plus the open dialog and added todo, are in the run directory. Focus and visible result are separate checks; inspect the screenshots instead of trusting rectangles alone.

**Next investigation:** reduce MDN's dialog lifecycle first. Test default closed rendering, opening above the document with a backdrop, Escape/Cancel behavior and focus restoration, with controls outside the dialog checked too. Separately, isolate the scroll/DOMRect mismatch on the APG page; do not remove the geometry assertion to make the row green.
