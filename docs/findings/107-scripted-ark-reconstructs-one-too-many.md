# Finding 107 — `scripted_ark.dat` reconstructed one `<font>` too many

Phase 101. The architecture and the measurements are in
[`docs/phases/101-scripted-ark-and-the-adoption-agency.md`](../phases/101-scripted-ark-and-the-adoption-agency.md).

## 1. What was wrong

The last failing case of the vendored web-platform-tests tree-construction
corpus (`scripted_ark.dat`) differed by one extra `<font size="4">` around the
text `"X"` in the second `<p>`. Four identical `<font size=4>` start tags had
been pushed; a script between the third and fourth set the third DOM element's
`size` to `5`.

`HtmlTreeBuilder::push_formatting_element()` implemented the Noah's Ark clause
by comparing the *live DOM attributes* of the entries already in the list with
those of the new element. The edit made the third entry read `size="5"`, so
only two of the three earlier entries were "alike" when the fourth arrived, the
earliest was not dropped, and reconstruction built four fonts instead of three.
The engine's own comment said comparing the elements compared "what the
standard says to compare"; it does not. §13.2.4.3 requires the attributes as
they were on the tokens for which the elements were created, paired without
regard to order.

## 2. What changed

`push_formatting_element()` now compares the tag name, namespace and sorted
`(name, value)` pairs of the tokens held in each `FormattingEntry` — the token
is already stored there for reconstruction and for the adoption agency — via a
new `same_noahs_ark_attributes()` helper. A script editing the DOM after an
element was pushed can no longer change what the Noah's Ark clause sees.

## 3. How it was verified

- `make conformance`: **1,973 / 1,973 (100.0%)** (from 1,972 / 1,973);
  `--filter scripted_ark --show 1` passes. The CI floor is raised to 1,973.
- `make test` **260 / 260** plus **114 / 114** cross-origin checks;
  `make diff-trees` **6,000 / 6,000**; `make wpt` **9,567 / 9,824**;
  `make js-conformance` **1,137 / 1,156**; `make dom-conformance`
  **678 / 678**; `make url-conformance` **1,188 / 1,188** — no other score
  moved.
- `make check-numbers` and `check_docs.py --base origin/main`: 0 failures.

## 4. What is still open

- `document.write()` after the load, `document.close()` ending the parse,
  parser-blocking written scripts and the external-script queue (Phase 95).
- A script that moves or removes a formatting element while the parser runs is
  not covered by the corpus; the list still uses live element pointers for
  stack membership and identity.
- No scoreboard measures a pixel (ROADMAP Tier 1 #8).
