## Phase 131 — From the Git manual to the manual it cites

Five frozen Git pages exited without a frame in the first browsing check. Keeping the style matcher’s traversal cursor alive (finding 136) let all five display. But showing a link and following it were separate problems. The shell found the link’s painted text fragment, then continued into unfragmented backing boxes and replaced the hit with the surrounding paragraph. On `git-clone`, the link is inside a table; hit testing stopped at the row group, whose rows have geometry even when the group itself does not.

Hit testing now follows the painter’s decision to use inline fragments instead of their backing boxes. For a flattened row group it still visits the rows. The probe locates the manifest’s unchanged CSS selectors with `SelectorList`, as a script would; the simpler native query could not find an attribute selector. All actions still require viewport clicks, with no direct navigation to a destination.

Run the commands in `tools/real_pages/README.md`: Chromium **20/20**, Litebird **12/20**, up from 7. All five Git destination headings are readable in the post-click frames. The original report is unchanged; the new screenshots and machine-readable rows are in `tools/real_pages/rerun-01/`.

The five Python articles do not complete. Hextris now produces a frame but its interaction is untested; the two other driver gaps remain. No broader web coverage is claimed.

### Follow-up: disclosure and the article column

The Python template uses `display: table` on a section containing ordinary elements. The table grid had no rows, leaving its descendants at their initial coordinates. The box builder now groups consecutive improper children in anonymous rows and cells without touching the DOM. The table’s intrinsic-width estimate respects definite minimum and maximum widths. The cascade tests the relative selector inside `:has()` instead of treating it as an unconditional match, so a rule intended for the numerical index no longer widens every article. Closed `details` with an explicit `summary` lays out only that summary until activation opens it.

Correct geometry wasn't enough: two post-click frames were blank. A box painting inline fragments returned without unwinding its clip/transform commands. That path now uses the same epilogue as the ordinary box path. The heading probe checks disclosure activation and an on-screen heading box; screenshot review still decides whether the text actually painted.

`python3 tools/real_pages/check_article_layout.py build/litebird` prints **4/4**. Rerunning the unchanged frozen tasks produced **17/20** (reference **20/20**); see `tools/real_pages/rerun-02/`. Sidebar positioning, preformatted whitespace, visual disclosure markers and synthesized summaries are not complete. The two games and contenteditable input remain untested, not passes.

### Follow-up: does the arrow reach 2048?

Unmodified arrows received through the GUI key path (`BrowserShell::handle_key`) now dispatch bubbling, cancelable keyboard events before the default scroll action. The focused element (otherwise the body or document) is the target. Animation-frame callbacks are serviced before repainting. This is arrow **keydown** support, not a complete keyboard/IME implementation; terminal navigation shortcuts are unchanged.

The frozen game task now reads the tile container's displayed tile values/position classes and verifies a legal directional move plus one newly spawned tile. In the scored run a left arrow moved a 4 from column 2 and a 2 from column 3 to column 1; a new 2 appeared in the first row. Both moved tiles are visible in the screenshots. The board alignment is visibly wrong: this is not a visual pass. See `tools/real_pages/rerun-03/` for frames and all rows. Chromium 20/20; Litebird 18/20, retaining the previous 17. The focused arrow check in the benchmark README reports 4/4.

### Follow-up: type a Unicode domain in place

A click on a plain `contenteditable` host sets focus and a real DOM selection. Select All operates on that host; inserting text or backspacing replaces the selected UTF-16 range, with cancelable `beforeinput` and a trusted `input` notification. The GUI serializes click/key requests. This is deliberately limited to empty or single-text-node hosts, not a rich-text editing engine.

The frozen Punycode trace compares both editable strings, focus and permalink to Chromium at every character. A successful trace initially coexisted with visible raw fallback markup. The document now remembers the parser's scripting flag, and layout omits `noscript` only with scripting enabled. An offline paint test covers both values. Interaction is **19/20**, rendering is **not** parity (0 of 18 comparable frames pixel-identical); Hextris remains undriven. See `tools/real_pages/rerun-04/` and the benchmark README for scope and residual defects.
