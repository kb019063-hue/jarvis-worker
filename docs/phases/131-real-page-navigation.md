## Phase 131 — From the Git manual to the manual it cites

Five frozen Git pages exited without a frame in the first browsing check. Keeping the style matcher’s traversal cursor alive (finding 136) let all five display. But showing a link and following it were separate problems. The shell found the link’s painted text fragment, then continued into unfragmented backing boxes and replaced the hit with the surrounding paragraph. On `git-clone`, the link is inside a table; hit testing stopped at the row group, whose rows have geometry even when the group itself does not.

Hit testing now follows the painter’s decision to use inline fragments instead of their backing boxes. For a flattened row group it still visits the rows. The probe locates the manifest’s unchanged CSS selectors with `SelectorList`, as a script would; the simpler native query could not find an attribute selector. All actions still require viewport clicks, with no direct navigation to a destination.

Run the commands in `tools/real_pages/README.md`: Chromium **20/20**, Litebird **12/20**, up from 7. All five Git destination headings are readable in the post-click frames. The original report is unchanged; the new screenshots and machine-readable rows are in `tools/real_pages/rerun-01/`.

The five Python articles do not complete. Hextris now produces a frame but its interaction is untested; the two other driver gaps remain. No broader web coverage is claimed.
