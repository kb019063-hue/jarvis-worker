## Phase 147 — Default display of an HTML dialog

The MDN holdout painted the form before opening and after Cancel. Layout had no dialog display default; script-facing computed style always said `block`.

Both style paths now use `none` for a closed HTML dialog and `block` when `open` is present. This is a default before author CSS, not a layout-tree veto. The existing `display:none` layout path already drops descendants and clears their boxes on rebuild. No dialog-specific painter or hit-test exceptions were needed.

`make -j2 build/litebird && tools/real_pages/dialog-01/check.sh`: **28/28** on the nonmodal fixture. The [MDN replay and frames](../../tools/real_pages/dialog-01/) show that Cancel now hides the form as well as returning `animalNotChosen`.

`showModal()` still lacks top-layer rendering, backdrop, outside-input isolation and focus management. The strict holdout score remains **0/4**. A future change should distinguish modal state from merely having the `open` attribute; ordinary `show()` must leave the outside control usable.
