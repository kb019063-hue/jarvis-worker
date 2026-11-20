# Phase 101 — `scripted_ark.dat`, and the attributes Noah's Ark compares

Task row: **One tree-construction case is left: `scripted_ark.dat`** (finding
107).

## 1. What was wrong

`make conformance` stood at **1,972 / 1,973**. The one failing case was
`scripted_ark.dat`:

```html
<p><font size=4><font size=4><font size=4><script>
document.getElementsByTagName("font")[2].setAttribute("size", "5");
</script><font size=4><p>X
```

Four `<font size=4>` start tags are pushed onto the list of active formatting
elements. A script between the third and the fourth changes the *third DOM
element's* `size` to `5`. When the fourth `<font size=4>` arrives, the "Noah's
Ark" clause must still count the first three as alike and remove the earliest,
so that reconstructing the formatting elements inside the second `<p>` builds
three nested `<font>`s, each with `size="4"` (the attributes on the tokens, not
the edited DOM node). This engine built four:

```diff
 |           <font>
 |             size="4"
-|             "X"
+|             <font>
+|               size="4"
+|               "X"
```

`push_formatting_element()` compared `entry.element->attributes()` with the new
element's live attributes. After the script ran, the list read `4, 4, 5`, so
only two entries matched the fourth `4`, the earliest was not removed, and the
list kept four entries.

The standard is explicit about this: each element in the list is associated
with the token for which it was created, and the Noah's Ark clause says the
attributes "must be compared as they were on the tokens for which the elements
were created; two elements have the same attributes if all their parsed
attributes can be paired such that the two attributes in each pair have
identical names, namespaces, and values (the order of the attributes does not
matter)" — [HTML Standard §13.2.4.3](https://html.spec.whatwg.org/multipage/parsing.html#the-list-of-active-formatting-elements).
It even notes that attributes a sanitizer removes when creating the element
still count, because the comparison is of the tokens, not the elements.

## 2. What changed

`src/html/HtmlTreeBuilder.cpp`:

- `same_noahs_ark_attributes()` copies the two tokens' `(name, value)` vectors,
  sorts them and compares them. Duplicate attribute names are already removed
  by the tokenizer, so this is the order-independent pairing the standard
  asks for, and it keeps attributes the element-building step might drop.
- `push_formatting_element()` now compares `entry.token.name` (the tag name as
  it was parsed), `entry.element->element_namespace()` and
  `same_noahs_ark_attributes(entry.token, token)` instead of the live
  `Element::attributes()` of the two DOM nodes.

Nothing else changed: `reconstruct_formatting_elements()` and the adoption
agency already create replacement elements from `entry.token`, which is why the
reconstructed copies in the expected tree have `size="4"` while the DOM node
the script edited has `size="5"`.

## 3. How it was verified

- `make conformance`: **1,973 / 1,973 (100.0%)**, up from 1,972 / 1,973; 62
  files, 0 failures. `python3 tools/conformance/run_tree_construction.py
  --filter scripted_ark --show 1` passes, and the diff above is the diff it
  printed before the change.
- `make test`: **260 / 260** engine suites and **114 / 114** cross-origin
  Window/Location and MessageChannel checks.
- `make diff-trees`: **6,000 / 6,000** random pages (four batches of 1,500)
  equal to the trees parse5 8.1.1 builds after its nine non-standard rules are
  corrected; 0 differ.
- `make wpt`: **9,567 / 9,824** subtests across 190 files, 0 unlisted
  failures — unchanged.
- `make js-conformance` **1,137 / 1,156**, `make dom-conformance` **678 / 678**,
  `make url-conformance` **1,188 / 1,188** — unchanged. The everyday probes are
  90 / 90 and 68 / 68.
- `make check-numbers` and `python3 tools/ci/check_docs.py --base origin/main`:
  **0 failures**.
- The conformance floor in `.github/workflows/ci.yml` is raised from 1,972 to
  **1,973**, so a regression in the last case fails CI rather than quietly
  lowering the score.

## 4. What is still open

- The tree-construction corpus is now complete, but it is one corpus. The
  remaining `document.write()` gaps from Phase 95 are still open: a late
  `write()` does not open a new document, `document.close()` does not end the
  parse, a written `<script>` is not parser-blocking, and there is no queue for
  an external script that is still being fetched.
- The list still holds live element pointers for identity and stack membership;
  a script that *moves or removes* a formatting element mid-parse is not
  modelled by any case in this corpus.
- Render conformance is still unmeasured by any scoreboard (ROADMAP Tier 1 #8).
