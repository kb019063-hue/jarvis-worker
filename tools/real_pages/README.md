# Twenty frozen browsing tasks • v1

[First run: results and screenshot pairs](baseline.html) · [Fixed pages, actions and source revisions](pages.json)

Five Python proposals, five Git manuals, eight Mothereff text tools and two games. This is deliberately a **clustered diagnostic sample**, not 20 independent sites or an estimate of web coverage. Repeated templates make shared regressions easy to see. The source archives are the publishers’ public HTML/assets, not rewritten toy pages. Third-party fonts, ads and analytics aren't replayed. Fonts may differ between engines. Both see an 800×600 viewport at 1× and the same source tree for a page under `http://snapshot.test`. Games have nondeterministic starting boards; don't score them by pixel equality.

## Rerun 01: Git manuals

[PDF: results and five screenshot pairs](rerun-01/git-manuals.pdf) · [All 20 outcomes](rerun-01/litebird.json). 12 completed (previously 7); all five manual links reached readable destination headings. The five PEP tasks remain incomplete; three input tasks are untested. No process exited prematurely. Same source revisions, human tasks, selectors and expectations; no changes to the original report.

## Rerun 02: Python articles

[PDF: five post-click comparisons](rerun-02/python-articles.pdf) · [Litebird outcomes](rerun-02/litebird.json) · [Chromium outcomes](rerun-02/reference.json). **17/20** completed, up from 12. Each PEP task opens the contents with a real click, follows the requested link and displays the heading. We reviewed the post-click frames, not just the scroll offsets. The article column is readable, but the sticky sidebar, code whitespace and typography still differ. All twelve previous passes remain; 2048, Hextris and Punycode are still untested. The manifest and saved pages did not change.

A small offline regression checks anonymous CSS table content, a bounded column, `:has()` matching and painting a sibling after a clipped text box: `python3 tools/real_pages/check_article_layout.py build/litebird` (4 pixel checks).

## Rerun 03: an arrow key for 2048

[PDF: the move, before and after](rerun-03/2048.pdf) · [20 Litebird outcomes](rerun-03/litebird.json) · [Chromium outcomes](rerun-03/reference.json). **18/20 tasks completed**, previously 17. Fresh random game, unchanged task. The probe sends the same unmodified arrow keydowns as the GUI, never calls the game's handlers, and checks the tile values and coordinates against a legal slide/merge plus exactly one spawn. It ignores purely cosmetic class changes. Screenshots show movement **and** a visual defect: the tiles/grid are offset from the board. This is an interaction pass, not visual parity or a full playthrough. Hextris and Punycode are the remaining untested tasks.

With the engine built, the focused offline check needs no game snapshot:

```sh
g++ -std=c++20 -Iinclude tools/real_pages/check_arrows.cpp $(find build -mindepth 2 -name '*.o') -o build/check-arrows
build/check-arrows    # 4/4: all four arrow codes, focused target, bubbling, cancellation and scroll default
```

## Rerun 04: Punycode text replacement

[PDF: before and after, with both engines](rerun-04/punycode.pdf) · [Litebird rows and keystroke trace](rerun-04/litebird.json) · [Chromium reference](rerun-04/reference.json) · [strict visual results](rerun-04/strict-frames.json).

**Interaction: 19/20** (Chromium 20/20). Select All on the clicked host selected `mañana.com`. All nine Unicode key inputs matched Chromium's focus, decoded text, encoded text and permalink after each edit. The final fields fit the viewport, hit-test to their centers, and show `bücher.de` / `xn--bcher-kva.de`. The first candidate showed a spurious “enable JavaScript” message. The final run excludes layout for `noscript` when scripting is enabled and checks for both the fallback and the invalid-input warning. Hextris remains untested, not passed.

**Visual: 0 exact matches in 18 comparable post-action frames.** Zero tolerance, full RGBA, same 800×600 viewport. Do not call that 0/20: independently randomized 2048 boards and the undriven Hextris task are not comparable. For Punycode, 26,928 pixels differ: the page is narrower and shifted left, text and spacing differ, and the blue focus treatment is not the reference outline. The DOM has a selection/caret, but the editor does not paint a selection highlight or insertion caret. Clicks place the caret at the end rather than the clicked glyph. No rich markup, IME or general keyboard fidelity claim.

Additional GUI smoke check (not a benchmark row): a click, Ctrl+A and nine keys on an offline single-field page produced a readable `bücher.de` in the GUI frame. The wrapper's title display remained at its initial value after the page changed `document.title`; that separate assertion **failed**. Do not use the wrapper title as evidence of successful editing.

```sh
g++ -std=c++20 -Iinclude tools/real_pages/check_editable.cpp $(find build -mindepth 2 -name '*.o') -o build/check-editable
build/check-editable    # 8 checks: focus, select, Unicode replacement/deletion, empty field, cancellation, host boundaries, markup
python3 tools/real_pages/check_noscript.py build/litebird   # enabled AND disabled
python3 tools/real_pages/strict_frames.py "$R/results"     # after the full rerun; requires Pillow
```

## Rerun on Linux (from repository root)

Use an empty or trusted local scratch directory. Pinned snapshots, browsers, logs and full-resolution PNGs belong outside Git. Use an isolated disposable environment, not a machine with personal browser data. The network restriction in the probe is for repeatability, not a general-purpose security sandbox.

```sh
R=$(mktemp -d)
T=tools/real_pages
python3 "$T/capture.py" "$R/sites"
npm install --prefix "$R/node" @sparticuz/chromium@131.0.1 puppeteer-core@23.11.1
export NODE_PATH="$R/node/node_modules"
# Minimal Linux images without NSS: extract the browser package's bundled libraries.
mkdir "$R/runtime"
node -e "require('fs').createReadStream(process.argv[1]).pipe(require('zlib').createBrotliDecompress()).pipe(process.stdout)" \
  "$NODE_PATH/@sparticuz/chromium/bin/al2023.tar.br" | tar x -C "$R/runtime"
LD_LIBRARY_PATH="$R/runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" node "$T/reference.cjs" "$R/sites" "$R/results"
make -j2 build/litebird
# Link the production shell and engine; no second input implementation.
g++ -std=c++20 -Iinclude "$T/probe.cpp" $(find build -mindepth 2 -name '*.o') -o "$R/probe"
python3 "$T/run.py" "$R/sites" "$R/results" "$R/probe"
```

Reference: fresh browser context per task, real mouse/keyboard, intercept requests to replay only the pinned tree. `reference.json` must validate the task before the probe attempts it. The initial oracle was calibrated **in Chromium**: open the collapsed PEP contents before following its link, allow scrolling to settle, and preserve the binary converter’s trailing separator. The human tasks did not change.

Litebird: fresh native process per task, origin mapped to the same tree, new sockets disabled before navigation. Scroll/hit-test/click/type through `BrowserShell`; inspect DOM only to locate a control or read the result. The probe locates targets using Litebird’s script-facing selector engine, not the native simple-name helper. It does not navigate directly to the expected result. The shell delivers arrow keydowns and edits plain single-text-node contenteditable hosts, but has no page mousedown adapter; do not substitute calls to a page’s handlers. Treat an exit before the first frame separately from an untestable input. Each task has a 45-second wall budget (not a speed benchmark).

`litebird.json` contains **candidate** interaction outcomes. Review the before/after PNGs for readability and visible results before publishing the count. A readable output with broken layout can pass interaction but not visual fidelity. Record missing frames, incomplete actions, crashes and blocked reference cases separately. Never drop them from the 20-row table. Keep v1 fixed when tuning the engine; use fresh holdout sites for broader claims.
