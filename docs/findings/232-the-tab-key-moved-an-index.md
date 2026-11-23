# Finding 232 — the Tab key moved an index

**Resolved by Phase 228** (`docs/phases/228-the-tab-key-moves-the-focus.md`).

## 1. What was found

The row was true as written. `focus_next()`, `focus_prev()` and
`focus_element()` in `src/shell/BrowserShell.cpp` wrote `m_focused_index` over
`m_interactive_elements` and set `m_dirty`; nothing called `focus()` or
`blur()`, and nothing dispatched an event. The holdout-02 H2 recording is what
that looks like from a page: after clicking `#number1`, typing, and pressing
Tab twice, `document.activeElement` was still `#number1`, no focus listener had
run, and `:focus` did not match the ring's element.

The mechanism to do it properly already existed — `dom::focus_element`, which
`element.focus()` uses, dispatches `blur`/`focusout`/`focus`/`focusin` and
boxes nothing that cannot take the focus. The shell never asked for it.

## 2. What was done

`set_document_focus` is now the one place the ring moves the focus, and it
calls `dom::focus_element`. `forget_focus_if_gone` reconciles the ring's index
with `document.focused_element()` before each press, so a script's
`el.focus()`, a removed stop, or an inert one sets where the walk starts from
rather than being stepped over. With nothing focused, the first Tab takes the
first stop and the first Shift+Tab the last.

A document with no browsing realm keeps the old writing of the focus, so a test
with no window sees no events and the ring's index is still observable — the
one behaviour the change keeps on purpose.

## 3. Evidence

`make test` **384/384**. `tests/suites/228-tab-moves-the-focus.cpp` walks three
stops forwards and back, checks `document.activeElement` and the page's own log
of `focus`/`blur`/`focusin`/`focusout` after each press, checks `:focus`, and
covers the two reconciliations. On `main`'s `BrowserShell.cpp` the same suite
fails at the first Tab.

## 4. What is still open

The ring's stop list is still not `is_focusable`: `tabindex` elements,
`select`, `textarea`, `contenteditable` and `summary` are not Tab stops. A
click does not move the index, and Shift+Tab is not reachable through
`handle_key`. The phase write-up's section 4 says what each would take.
