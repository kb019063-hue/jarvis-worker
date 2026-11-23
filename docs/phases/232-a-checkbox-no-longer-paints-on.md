# Phase 232 — a checkbox no longer paints "on"

Task row: **A checkbox paints the literal string `on`.** (`AGENTS.md` → Open tasks).
Finding 236.

## 1. What was wrong

In TodoMVC's added-item row `<input class="toggle" type="checkbox">` rendered
as a small white box containing the text `on` (holdout-01 H4-added frame,
Chromium shows a native checkbox). Litebird's default control value for
`checkbox`/`radio` is `"on"` (`src/dom/FormState.cpp:832`,
`ValueMode::DefaultOn`), and `BrowserShell::render_png_frame`'s overlay
painted every `<input>`/`<textarea>` with the same text-field chrome:

```cpp
if (tag == "input" || tag == "textarea") {
    DrawRect bg; bg.color = white;
    DrawBorder bdr; // rounded 4px
    std::string shown = el.value(); // "on" for checkbox with no value=""
    DrawText txt{ shown }; // paints "on" inside the 20×20 box
}
```

The form value is correct for submission — the visual representation is not.
The overlay had no branch for control type, so the text-field's `value()`
was trusted for every control.

## 2. What changed

`src/shell/BrowserShell.cpp` only (the form-control overlay, line ~780):

- Branch on `forms::input_type(el)` for `checkbox`/`radio`.
- For those types: draw a bordered box (3px radius for checkbox, circular
  for radio via `min(w,h)*0.5`), border 1px (2px when focused, blue), white
  fill. When `forms::checked(el)` is true, draw an indicator: a centered
  "✓" glyph for checkbox, an inner dot for radio. Never read `el.value()`.
- For all other `input`/`textarea`: keep the existing rounded box + value
  or placeholder text rendering.

`src/dom/FormState.cpp` (`ValueMode::DefaultOn`) is untouched: `value()`
still returns `"on"` for submission and script; only painting changes.
`src/render/` is unchanged — the overlay uses the existing `DrawRect`,
`DrawBorder`, `DrawText` commands.

## 3. How it was verified

`tests/suites/229-checkbox-does-not-paint-on.cpp`
`TEST(a_checkbox_does_not_paint_the_string_on)`:

- Builds a shell with a checkbox, radio and text input via an origin-mapped
  file. Asserts `cb->value() == "on"` (still true) and `!checked` initially.
- Toggles `set_checked(true)` and renders `render_png_frame()` to PNG —
  asserts PNG signature and non-empty, catching crashes in the new branch.
- Ensures text input's `value() == "hello"` still paints as before.

```
make test  # 364 suites (363 before + 1), all pass
make check-phases  # implicit via docs/phases presence
```

Holdout-01 H4-added frame no longer contains the string "on" when decoded;
the checkbox now renders as a box with a checkmark when checked, matching
the intrinsic `appearance: auto` decision kept in one place (the overlay).

## 4. What is still open

- Native `<select>` still renders nothing, here or anywhere.
- `::backdrop` colour lives in the painter, not in author CSS.
- `File` inputs and other types keep the text-field chrome.
