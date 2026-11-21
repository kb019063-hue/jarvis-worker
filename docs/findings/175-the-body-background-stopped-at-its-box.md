# Finding 175 — the body background stopped at its box

**Partly resolved by Phase 170** (`docs/phases/170-canvas-background-from-the-root.md`).

## 1. What was found

The document's layout starts at its body and skips the HTML root. Backgrounds
were painted only as box chrome, so a red body with an 8px margin left `(0,0)`
and `(799,599)` white where Chromium paints the whole viewport red. A root
background was missed; the shell's separate body-color clear also got root
precedence and alpha wrong, and iframe bitmaps were flattened on white.

## 2. What was done

A whole HTML document now selects a solid canvas color from the root, or from
its first HTML body child when the root is transparent and the body has a box.
A viewport-space display-list command paints it before the document boxes; the
source box's used background is suppressed without altering computed style.
Transparent embedded-document pixels remain transparent and source-over alpha
is preserved. The user-agent body's opaque white is normalized to transparent
at UA priority, so explicit author white still works. Non-document subtrees and
non-HTML roots do not acquire the HTML canvas rule.

## 3. Evidence

`make test` passes 329 engine suites and 114 cross-origin checks.
`make render-conformance` reports 36/38 original pages, 2 known, and 34/40
body-origin pages, 6 known. Chromium 153 independently matches all 128
body-origin reference assertions and all 12 scroll-fixture assertions; the
commands and optional dependencies are in the phase write-up. With all known
markers removed, the identical current 40-page corpus scores 7/40 on the base
binary and 34/40 now: 28 pages gained and one lost.

## 4. What is still open

That lost page is a real visible regression: `display:contents` is not
implemented, so the body now incorrectly supplies a red canvas where Chromium
has white; previously the missing propagation happened to hide it. Root
background images, root opacity groups, hidden descendants, inline-body default
margin placement and general empty/sibling margin collapse remain open and
have their own known-failure pages. The UA normalizer should be removed along
with the original UA white declaration once the claimed resolver source file is
free. Solid-color propagation is not full CSS Backgrounds or full root layout.
