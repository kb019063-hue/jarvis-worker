# Phase 228 — the Tab key moves the focus

Task row: **Tab advances a ring index without ever calling `focus()`**
(`AGENTS.md`, from the holdout-02 run). Finding 232.

## 1. What was wrong

The shell drew a focus ring over a list of stops and kept its own idea of
which one was focused:

```cpp
void BrowserShell::focus_next() {
    if (m_interactive_elements.empty()) return;
    m_focused_index = (m_focused_index + 1) % m_interactive_elements.size();
    m_status_message = ...;
    m_dirty = true;                 // and that was all
}
```

No `focus()`, no `blur()`, no event. So the only thing a Tab press changed was
what the shell painted. `document.activeElement` stayed whatever the page had
focused (holdout-02 H2: the input that was clicked, after typing and two Tab
presses), no `focus`/`blur`/`focusin`/`focusout` listener ran, and `:focus`
never matched the ring's element. A script that draws its own focus state —
and every page that asks `document.activeElement` — disagreed with what the
user saw.

## 2. What changed

- `BrowserShell::set_document_focus(element)` is the one place the ring moves
  the focus, and it goes through the bindings' own `dom::focus_element`, the
  path `element.focus()` uses: it blurs what held the focus (dispatching
  `blur` and `focusout`), focuses the new stop (`focus`, `focusin`), and
  boxes nothing that cannot take the focus. A document with no browsing realm
  (one a test built, with no window) keeps the old writing of the focus, so
  nothing observable happens there and the ring's index is still observable.
- `focus_next()` and `focus_prev()` call it on the stop they reach.
- `forget_focus_if_gone()` reconciles the index with the document before the
  walk moves: the index is only where the walk stands between two presses, and
  `document.focused_element()` is the truth. A script's `el.focus()`, a stop
  removed from the document, or a modal dialog that made the ring's element
  inert therefore sets where the next Tab starts instead of being stepped
  over.
- With nothing focused, the first Tab takes the first stop and the first
  Shift+Tab the last, as the platform does.

## 3. How it was verified

The suite is a file of its own, `tests/suites/228-tab-moves-the-focus.cpp`
(the form `tests/suites/README.md` asks for; nothing is added to
`tests/test_suite.cpp`). It builds the holdout's page — three stops, four
listeners on each — and checks, in order:

| After | `document.activeElement` | The page's log |
| :--- | :--- | :--- |
| the page loads | `BODY` | — |
| one Tab | `#number1`, and `number1.matches(':focus')` | `focus:number1,focusin:number1` |
| two Tabs | `#number2` | `…,blur:number1,focusout:number1,focus:number2,focusin:number2` |
| three, four | `#go`, then back to `#number1` | `…,blur:go,focusout:go,focus:number1,focusin:number1` |
| Shift+Tab (`focus_prev`) | `#go` | `blur:number1,focusout:number1,focus:go,focusin:go` |

and then that a script's `el.focus()` is where the next Tab starts, that a
stop removed from the document is stepped past from the document's own focus,
and that a page which focused a `tabindex` div before the ring was built still
hears that div's `blur`.

```bash
make test      # All 373 engine test suites passed successfully!
```

Nothing else moved:

| Check | Result |
| :--- | :--- |
| `make js-conformance` | 1149/1158 (99.2%), 0 crashes — one more than `main`'s 1148, from `main`'s own unscopables work, not from this phase; `make metrics` moved the figure and CI's floor with it |
| `make conformance` / `make dom-conformance` | 1973/1973 · 678/678 |
| `make wpt` | 9567/9824, the same 257 listed |
| `make url-conformance` / `make xml-conformance` | 1188/1188 · 1718/1718 |
| `make render-conformance` | 55/64 and 35/41, the same recorded 9 and 6 |
| `make diff-trees` / `make diff-xml` | 1500/1500 trees equal, no crash |
| `make deep` | 1332 runs, 0 failed |
| `make verify-terminal-shell-safety` / `verify-relative-navigation` / `verify-gc-stress` | 1/1 · 50/50 · 11830 |
| `make security-fetch` / `-worker` / `-https` / `-websocket` | 3/3 · 17/17 · 20/20 · 40/40 |
| a probe of the same page under `-fsanitize=address,undefined` | unchanged answers, no sanitizer report |

Negative control: with `BrowserShell.cpp` and its header as `main` has them and
this suite in place, it fails on the first Tab — `document.activeElement.id` is
still `BODY`.

## 4. What is still open

- **The ring's stops are not the platform's tab stops.**
  `scan_interactive_elements` takes `a[href]`, `button` and `input`, so a
  `tabindex` element, `select`, `textarea`, `contenteditable` and `summary` are
  reachable by `element.focus()` — and so by this change's own blur path — but
  not by Tab. A page-focused `tabindex` div is blurred for that reason, which
  the suite pins rather than hides. The bindings already have `is_focusable`;
  the scan is the half that does not use it.
- **The ring does not follow a click.** `click_at` focuses what the page does
  with the point; the index stays where it was until the document's focus and
  the index disagree at the next press, which this change now notices and
  adopts.
- **Shift+Tab is not a key `handle_key` carries.** The terminal loop maps the
  `ESC [ Z` sequence straight to `focus_prev()`, so the back step cannot be
  driven through `handle_key` from a test or from page script.
