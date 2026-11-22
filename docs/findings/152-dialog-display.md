## 152. Closed dialog contents were painted and clickable

A closed dialog had no default hiding its subtree. Reflecting `open` and returning the right value from `close()` did not make the MDN example disappear.

The renderer and script-facing computed style now use the HTML dialog's boolean `open` attribute for the default display. Author declarations still win. `tools/real_pages/dialog-01/check.sh` after a shell build prints **28/28 dialog display checks passed**; this includes close and reopen with actual pixels and hits. See the [frozen H3 replay](../../tools/real_pages/dialog-01/results/H3.log).

Only the closed-display defect is resolved. On H3, the open form remains inline; focus after Cancel remains on the hidden button. Escape and input isolation need separate modal behavior, not a change to this default.
