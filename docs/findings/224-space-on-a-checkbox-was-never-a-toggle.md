## 224. Space on a focused checkbox said it was handled, and toggled nothing

The interactive shell's keyboard went to `BrowserShell::handle_key`. It knew
about arrows, `PageUp`/`PageDown`, `Tab`, `Backspace` and `Enter`, and then
fell through to `type_text(key)`. A focused checkbox is not editable, so
`type_text` had nowhere to put the character — but the fall-through reached
the condition that accepts a printable character, and `handle_key(" ")`
returned **true**. The caller was told the key was handled, and the control
never moved:

```
focused: input[type=checkbox]
handle_key(" ") -> true     checked=false      no click, no change
```

HTML's activation behavior for a checkbox and a radio is reached from the
keyboard as well as from a point: Space, and Enter, and Space on a checkbox
toggles it. A todo list's completion toggle is a checkbox inside a `<label>`;
a person who tabs to it and presses Space is doing the ordinary thing.

## What was wrong before that, and what was not

The row this phase was taken from says a click on a `<label>` does not reach
its control. Measured, that is not true at the head this phase began on —
`click_at` on a label whose control is a descendant, and on a `label[for]`
beside it, both run the control's activation:

```
click on `label[for=c1]`'s text     -> l1-click, c1-click, c1-change:true
click on a label containing c1      -> l1-click, c1-click, c1-change:true
```

A label with **no** control of its own does not forward the click to the
element under the pointer, which is a separate and smaller thing.

The click path itself was suspected and cleared the same way. `click_at`
builds its pointer events with `send_mouse`, whose `click` is
`Element::click()`, and `Element::click()` runs the dispatch that performs
`before_click`/`after_click` in `DomFormBindings.cpp`. That was verified by
reverting a change to `click_at` and watching the mouse cases below still
pass: the behaviour was already right, and this phase does not touch it.

## What changed

`src/shell/BrowserShell.cpp` and its header. A focused `input` whose type is
`checkbox` or `radio` activates on `" "`, `"Enter"` or `"Spacebar"`, through
`BrowserShell::dispatch_click`, which calls `Element::click()` — the same door
the mouse uses, so the activation behavior, the toggle, and the `input` and
`change` events all follow from one place rather than a second copy of them.

## How it was verified

`browser_shell_activates_a_checkbox_and_toggles_it_from_the_keyboard` in
`tests/test_suite.cpp`, four cases in four documents:

| case | before | after |
| :--- | :--- | :--- |
| mouse on the checkbox: `checked` true, listener sees `click:true` | pass | pass |
| `onclick` + `preventDefault`: the toggle is taken back | pass | pass |
| **Space on the focused checkbox toggles, twice** | **fail** | **pass** |
| a radio is set, never unset, and its group is exclusive | pass | pass |

The suite was shown red on the engine before the change, and red again with
the keyboard branch removed and everything else in place — so the third case
is what the suite is actually testing. `make test` prints **363 / 363**.

The page is loaded through `BrowserShell::navigate_to` as a `data:` URL
rather than handed to the shell: the shell makes its own `Document`, and
that document is the one whose realm the events run in, so a suite that
builds its own tree is testing a different realm from the one the shell
dispatches into.

## What is still open

A `<label>` whose click target is not the labeled control — a label with no
control, or a click on a label while a script suppresses the forwarding —
still receives the click itself. The CSSOM and painted appearance of a
checkbox (`appearance: auto`, the browser's own drawing instead of the
text-field overlay that paints it as a rounded box) are a separate row, as is
the fact that an `input` in the shell's overlay is painted with a border and
a background whether it is a text field or not.
