# Phase 220 — Space on a focused checkbox is a toggle

**Finding 224**
(`docs/findings/224-space-on-a-checkbox-was-never-a-toggle.md`).

## What was wrong

`BrowserShell::handle_key` handled arrows, `PageUp`/`PageDown`, `Tab`,
`Backspace` and `Enter`, then fell through to `type_text(key)`. A focused
checkbox is not editable, so there was nothing to type into — but a space
is a printable character, so the call was accepted and `handle_key(" ")`
returned **true**. The key reported itself handled and the control never
changed:

```
handle_key(" ") -> true      checked=false
```

HTML's activation behavior for a checkbox and a radio is reached from the
keyboard too. A completion toggle that a keyboard user can focus and cannot
operate is not a control.

## What was checked and found already right

The row this phase began from named a `<label>` click. Measured on the head it
began from, both label shapes already forward to their control, and the
mouse's `click` already runs the activation behavior (`click_at` →
`send_mouse` → `Element::click()` → `before_click`/`after_click`). A change
to `click_at` was written, shown to make no difference to those cases, and
reverted rather than kept: this phase changes the keyboard only.

## What changed

`src/shell/BrowserShell.cpp`, `include/litebird/shell/BrowserShell.hpp`. A
focused `checkbox`/`radio` activates on `" "`, `"Enter"` or `"Spacebar"`
through a new `BrowserShell::dispatch_click`, which calls `Element::click()`
so that the toggle, the activation behavior and the `input`/`change` events
all come from the one path the mouse already uses.

## How it was verified

`browser_shell_activates_a_checkbox_and_toggles_it_from_the_keyboard`, four
cases in four documents loaded through `navigate_to` as `data:` URLs (the
shell makes its own `Document`, and a suite that builds its own tree would be
dispatching into a different realm):

- the mouse reaches the checkbox and the listener sees `click:true`;
- `onclick` + `preventDefault` takes the toggle back and fires no `change`;
- **Space on the focused checkbox toggles it, twice** — the case this phase
  is for;
- a radio is set and never unset, and its group is exclusive.

Shown red before the change, and red again with only the keyboard branch
removed. `make test` **363 / 363**.

## What is still open

A `<label>` with no control does not forward its click to whatever is under
the pointer. The painted appearance of a checkbox — the shell's overlay draws
every `input` as a bordered text field, and a checkbox's default value paints
as the literal string `on` — is the row this phase came from, and remains.
