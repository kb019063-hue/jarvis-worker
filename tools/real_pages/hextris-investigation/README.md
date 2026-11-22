# Hextris: why a mouse press alone is not a fix

Exploratory run on `760498a`, **not a change to the merged or queued PR**, using task 17's unchanged `hextris/hextris` revision `3f4847dc8fd7dab3d1c87e6324b9159d92fbd396`. Both engines used `http://snapshot.test/index.html`, the same saved assets, external network blocked, 800×600. Read [the frozen task](../pages.json) and [the benchmark protocol](../README.md).

Chromium 131 hit `#startBtn` at its center (420,303), and the game changed from state 0 to 1. After one second, the start control was hidden and `#pauseBtn` was visible at (717,521), size 72×72. The frame shows the game and its pause control. Litebird initialized jQuery, the game and the 800×600 canvas, but showed neither the title nor the play glyph. `#startBtn` had bounds (40,38,200,4), not (320,228,200,150). A real shell click at its own center (140,40) hit `#currentHighScore`. After one second the state was still 0, pause still had zero area and the displayed frame was pixel-identical to the initial frame.

A scratch shell build added a trusted primary `mousedown` **on the element returned by the ordinary viewport hit test**. It did not call a game handler. In fresh processes it still hit the score at (140,40), and no element at (420,303). Neither started the game. That experiment isn't a production adapter or a benchmark pass. Canvas backing-store samples changed between observations in the scratch run, while the corresponding displayed frames did not: investigate painting/composition as well as input. This does not prove which element, if any, covers the canvas.

**Verdict:** start action not completed; no visual pass. The published total remains 19/20. Do not fix this by clicking a known DOM node directly, raising a game-specific z-index, firing an untrusted script event, or accepting `gameState` without checking the frame.

## Independent offline failures

From the repository root on Linux, with the engine built as in the [benchmark instructions](../README.md):

```sh
g++ -std=c++20 -Iinclude tools/real_pages/hextris-investigation/controls.cpp $(find build -mindepth 2 -name '*.o') -o /tmp/hextris-controls
/tmp/hextris-controls   # currently exits 1, not a passing test
```

| Control | Chromium 131 | Litebird |
| --- | --- | --- |
| `left:40%;top:38%;width:25%;height:25%`, initial containing block | (320,228,200,150) | (40,38,200,0) in the reduced page |
| Same explicit *pixel* rectangle; earlier sibling with z-index 10 above later sibling with z-index 1 | Hits `start` | Hits `under` |
| Unobstructed button; primary click at (20,20) | Trusted `mousedown`, `mouseup`, `click`; `buttons` 1,0,0 | Only `click`; `isTrusted` false |

The three fixtures separate layout, hit order, and event delivery. Coordinates are CSS pixels. Use the first to repair percentage insets and the initial containing block, then check hit order against paint order and implement the primary mouse sequence in the shell. All three passing is necessary evidence for these fixes, not sufficient evidence for the game. Rerun Chromium and the unmodified game, inspect before/after frames, then rerun the other 19 tasks before publishing any new total.

## Local geometry follow-up (after the investigation above)

The local layout change retains signed percentage insets in computed style, resolves them against the containing block on the correct axis, and uses the viewport when no positioned ancestor establishes a containing block. Normal-flow descendants move with the positioned box. No change to the pending PR head or the frozen inputs.

At 800×600, `#startBtn` now matches Chromium exactly: `(320,228,200,150)`. The real shell hit at `(420,303)` now returns **startBtn**, not the score. Clicking still leaves `gameState` at 0, pause hidden and the rendered frames identical. The first reduced control passes; the isolated z-order and trusted mouse sequence controls still fail. On this particular page the center hit is now right despite that general z-order defect.

`check_position.cpp` compares seven boxes to Chromium 131 at each of two viewport sizes, before and after resizing the same shell. Initial containing block, fixed bottom/right, negative percentage, px/auto overrides, positioning inside a padded ancestor, opposing insets and descendant translation are exercised. Strict result **13/14**: 7/7 at 800×600; at 640×480 the percentage top is 182.399993896 in Litebird versus 182.390625 in Chromium. No tolerance applied. The previous investigation's 0/3 results are historical.

```sh
D=tools/real_pages/hextris-investigation
g++ -std=c++20 -Iinclude "$D/check_position.cpp" $(find build -mindepth 2 -name '*.o') -o /tmp/hextris-position
/tmp/hextris-position "$PWD/$D/position.html"   # exits 1 on the fractional mismatch
```

**Not scored:** repeating the scratch `mousedown` experiment with the corrected geometry did move the real game to state 1 and hid the start target. It gave `#pauseBtn` a 60×60 DOM box at (729,533) rather than Chromium's 72×72 at (717,521). The board and pause button still didn't paint: the canvas backing store's center was `(44,62,80,255)` but the displayed center stayed `(236,240,241)`. The only differing displayed pixels fit within (22,6) to (61,16), exclusive end. Keep mouse delivery and painting as separate gates. The 19 other interaction checks held; there is **no 20th pass**.

Regression checks: render reftests 29/34 exact, 5 recorded failures. `make test` initially aborted in the large XML/XHR result-and-time-budget assertion while other jobs were running. A repeat without the other benchmarks passed all 297 engine suites and 114 cross-origin checks. The first run is a failure, not evidence of two clean runs.

## Normal shell input and viewport paint (rerun 05)

The normal primary-click path now sends the trusted press/release pair (including focus cancellation and retargeting), and point lookup visits overlapping siblings in paint order and descendants that protrude from an unclipped parent. The body’s propagated viewport overflow was clipping output to its short in-flow height. The used overflow now leaves the body box unclipped when the HTML root supplies no clip; containment and ordinary element clips still apply. The canvas board and pause glyph are visible, not merely present in the DOM. All six `controls.cpp` checks now pass. `check_viewport_clip.py build/litebird` checks eight actual pixels across four clipping cases. The earlier `check_position.cpp` subpixel discrepancy is unchanged.

See [the new report and both screenshots](../rerun-05/hextris.pdf). A fresh native run and Chromium oracle each completed 20/20. Production GUI scope is a completed primary click at one viewport point, not drag, capture or a full pointer input implementation. This local work was not added to the queued merge of PR #158.
