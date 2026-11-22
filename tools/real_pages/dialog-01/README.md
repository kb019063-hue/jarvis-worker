# Dialog display · known-task replay

[Before, open, and after Cancel (PDF)](dialog-display.pdf) · [H3 trace](results/H3.log) · [all four native rows](results/litebird.json).

On the `22232cf` engine, the MDN example no longer shows its form before the user clicks **Update details**. Clicking **Cancel** returns `animalNotChosen` and hides the form again. In the original [first exposure](../holdout-01/README.md) both closed frames showed the form. This is an improvement on a known task, **not a task pass**.

* In the open frame, the form is in the normal flow without a backdrop. The frozen check named “modal open” checks only `open` and on-screen geometry. It cannot prove modality.
* The [outside-click diagnostic](results/outside.log) found focus still on the opener after `showModal()`. A second real click hit that outside button and ran its handler again. The [Escape diagnostic](results/escape.log) stopped because the shell did not deliver Escape; it did not test a browser-generated cancel event. In the normal Cancel-button run, focus ended on the now-hidden `cancel` button, not `updateDetails`.
* Same pinned pages, steps, 800×600 viewport and timeout: native **0/4** strict, reference **4/4**. H1 still had no clickable Next target, H2 still failed the biography geometry check, and H4 still had no clickable toggle center. Chromium 131's fresh trace matched the committed reference. The original 20 development tasks were not rescored here.

## Reproduce

After `make -j2 build/litebird`, run `tools/real_pages/dialog-01/check.sh`: **28/28** checks. This nonmodal control goes closed → `show()` → `close()` → `show()` via an outside button. It checks computed display, dialog and descendant boxes, real hits, an exact colored rectangle in the open framebuffer and zero marker pixels when closed, `open="false"` (present), and author CSS overriding either default. The test fixture separates the marker from the outside button's focus outline.

For H3 use the [frozen holdout reproduction](../holdout-01/README.md#holdout-01--four-unfamiliar-browsing-tasks) on this engine, not the first-exposure engine. In that recipe `$R` is scratch. Then:

```sh
T=tools/real_pages/dialog-01
"$T/inspect-modal.sh" "$R/driver" "$R/sites" "$R/extra"
python3 "$T/report.py" "$R/run" "$R/run" "$R/replay.pdf"  # ReportLab and Pillow
```

Unmodified local snapshot; real clicks and shell key input, not calls to page handlers. The supplemental actions run in separate sessions and do not affect the frozen score.
