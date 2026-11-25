# Phase 409 — the ring is the tab order

Task row: **Tab advances a ring index without ever calling `focus()`** — the
half Phase 228 left open (`AGENTS.md`). Finding 409.

## 1. What was wrong

Phase 228 (finding 232) made a Tab press move the *document's* focus rather
than only the shell's idea of it. What it did not change is which elements
there were to move between. `BrowserShell::scan_interactive_elements` built the
ring from three tag names and a heuristic:

```cpp
bool is_link = (tag == "a" && el->has_attribute("href"));
bool is_btn = (tag == "button" || cls.find("btn") != std::string::npos ||
               id.find("btn") != std::string::npos || el->has_listeners("click"));
bool is_inp = (tag == "input");
if (is_link || is_btn || is_inp) candidates.push_back(el);
```

The engine already knew better, in one place, and the two disagreed.
`dom::is_focusable` (`src/js/DomFormBindings.cpp`) is the predicate
`element.focus()` obeys — the gate `dom::focus_element` returns early on — and
it accepts `input` (not `hidden`, not disabled), `button`, `select`,
`textarea`, `a`/`area` with `href` *or* `tabindex`, `iframe`, a `summary`
inside a `details`, a non-`false` `[contenteditable]`, and any element at all
with a `tabindex` attribute.

So a page could focus an element with `focus()` and not reach it with Tab. A
`select`, a `textarea`, a `contenteditable` region and a `tabindex` div were
each a real control, a real editable surface, or explicitly put in the tab
order by its author, and each was invisible to the key whose whole purpose is
to reach them. This is the defect Phase 228's row recorded as still open, and
it is visible in Phase 228's own suite: that suite asserted, in a comment, that
a `tabindex` div "is not a ring stop (the scan takes `a`, `button` and
`input`)" — the bug stated as the contract.

## 2. What changed

`scan_interactive_elements` asks `dom::is_focusable`, in tree order, so the
ring and `focus()` can no longer disagree about what a stop is. Nothing else
about which elements are focusable moved: the predicate already existed and the
shell now reads it instead of keeping a second opinion.

Three elements the old scan took are **not** focusable and are kept, because
they are what the ring was built for: a `div` with a `btn`-class, a `div` with
a `btn`-ish id, and any element with a click listener. They are collected in a
second tree-order pass and appended *after* the focusable ones, so no page
loses a stop it had and no stop a page already had changes its place. The list
is a tab order now, not a list of click targets — clicks never used it;
`BrowserShell::element_at` does the hit testing.

The modal-dialog rule is unchanged and still applies to the wider list: an
element `Document::is_inert` reports as behind an open dialog is dropped.

Phase 228's own suite changed with it. Its last block had asserted the defect
(`sink` is focused, the first Tab jumps over it to `go`); it now asserts what
the fix makes true — `sink` is a stop, so the first Tab leaves it for `go` and
the back step returns to it. The comment there says which assertion moved and
why.

## 3. Evidence

`make test` **464/464** (463 before this suite; `main` is at 463).

`tests/suites/409-tab-reaches-every-focusable.cpp` is the oracle. It puts one
of each kind on a page — `input`, `select`, `textarea`, `tabindex` div,
`contenteditable` span, `summary` in a `details`, `button`, `a[href]` — plus
three that must *not* be stops (a hidden input, a disabled input, a plain div),
and requires:

- the ring, read back by id, is exactly `a,b,c,d,e,f,g,h` in tree order;
- every one of the eight answers `focus()`, and each of the three absent ones
  dispatches nothing when `focus()` is called on it;
- eight Tab presses walk exactly those eight, in that order, each matching
  `:focus`, and the ninth wraps to the first;
- the `blur`/`focus` pair still reaches the page across the wider ring.

Both directions were measured. On the fixed build the isolated suite passes
(`LITEBIRD_SUITE_SHARD=0/2 LITEBIRD_SUITE_CUTS=…`: "its 81 of the 464 engine
test suites passed"). On `main`'s `BrowserShell.cpp`, with the suite in place,
it fails at its first assertion:

```
test_suite: ./tests/suites/409-tab-reaches-every-focusable.cpp:59:
Assertion `ring == "a,b,c,d,e,g,h"` failed.
```

(`a,b,c,d,e,g,h` is `select`, `textarea`, the `tabindex` div, the
`contenteditable` span and the `summary` missing; the old build also ordered
`button` and `a` last, because it collected them in a second pass.)

Nothing else moved: `make js-conformance` **1152/1158** and `make
dom-conformance` **678/678**, both unchanged.

## 4. What is still open

- **`tabindex` order.** The standard puts positive `tabindex` values first, in
  ascending order, then the rest in tree order. The ring is tree order and does
  not read the attribute's value at all. Nothing on the measured pages used a
  positive value.
- **A click does not move the ring index**, and Shift+Tab is the terminal's own
  escape sequence rather than a key `handle_key` carries — both recorded by
  Phase 228 and untouched here.
- `select` now takes the focus but still paints nothing: the native control
  painter is the separate row this table keeps for `<select>`.
