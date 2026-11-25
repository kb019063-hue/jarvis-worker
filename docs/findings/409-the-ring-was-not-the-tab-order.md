# Finding 409 — the ring held what a page could click, not what Tab could reach

**Resolved by Phase 409** (`docs/phases/409-tab-reaches-every-focusable.md`).

## 1. What was found

The row's own words were right: "the ring's stops are not `is_focusable` —
`tabindex` elements, `select`, `textarea`, `contenteditable` and `summary` are
reachable by `focus()` but not by Tab".

Two lists answered the same question and only one of them was asked.
`dom::is_focusable` (`src/js/DomFormBindings.cpp`) is the predicate the order
of focus really obeys — `dom::focus_element` returns early on it, so
`element.focus()` on an element it refuses does nothing. `BrowserShell::
scan_interactive_elements` did not call it. It took `a[href]`, `button` (or a
`btn`-ish class or id, or any element with a click listener) and `input`, and
nothing else.

The classes of element that fell in the gap are not exotic. A `<select>` is a
form control as much as an `<input>` is; a `<textarea>` is where a page's long
text is typed; `contenteditable` is how a page makes any element an editing
surface; a `summary` is the handle of a `<details>`; and a `tabindex`
attribute exists for no other reason than to put an element in the tab order.

Nothing had noticed, because the thing that was supposed to notice had the bug
written into its assertion. `tests/suites/228-tab-moves-the-focus.cpp` ended
with a comment explaining that the `tabindex` div it had just focused "is not a
ring stop (the scan takes `a`, `button` and `input`)" and then asserted the
next Tab jumped over it. A suite that states the defect as the contract passes
forever.

## 2. What was done

The scan asks `dom::is_focusable`, in tree order. The legacy clickable-but-not-
focusable elements (a `btn`-classed `div`, a `btn`-ish id, an element with a
click listener) are collected afterwards and appended, so the ring only ever
grows at its end and a page that had stops keeps them in the order it had. The
pointer path is untouched: clicking goes through `element_at`, which hit-tests
the layout tree and never consulted this list.

## 3. Evidence

`make test` **464/464** (463 on `main`). The new suite
`tests/suites/409-tab-reaches-every-focusable.cpp` requires the ring and
`focus()` to give the same answer for eight focusable kinds and three that must
stay out, and walks all eight with Tab, asserting `:focus` and the page's own
`focus`/`blur` log at each step. With `main`'s `BrowserShell.cpp` the suite
fails on its first assertion — the ring it reads back is missing five of the
eight — so the fix is what the suite is measuring, not something it was written
around.

## 4. What this finding does not say

It does not say the ring is a correct tab order, only that it is no longer a
list of click targets. Positive `tabindex` values still do not sort the stops,
a click still does not move the ring index, and Shift+Tab still does not reach
`handle_key`. Those were open before this phase and are open after it; the
phase write-up lists them.
