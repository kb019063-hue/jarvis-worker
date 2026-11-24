# Phase 360 — `xmlns=""` is a declaration, not a redundant one

The XML serializer dropped a default namespace declaration whenever the element
was already in the default namespace in force. That is right for the ordinary
case and wrong for exactly one value: the empty one. `xmlns=""` is how an
element *leaves* a default namespace an ancestor put it in, and it is not a
redundancy to tidy away.

## What was wrong

`XmlDom.cpp`'s `write_element` decides once whether to suppress the element's
own default declaration:

```cpp
if (*inherited == ns) {
    if (local_default) ignore_default_declaration = true;
```

"Matching the namespace in force" was taken as "says nothing". For a non-empty
namespace that is the standard's redundant-`xmlns` rule and is right. For the
empty namespace it is wrong in two distinguishable situations:

- the default in force is **not** empty, so `xmlns=""` is the declaration that
  keeps the element out of it;
- the element **also declares a prefix**, so the two declarations are one
  statement about what this element puts in scope.

Both came out with the declaration removed, and the second produced a document
that said something different from the tree: `<root xmlns="" xmlns:foo=""/>`
serialized as `<root xmlns:foo=""/>`, a `foo` bound with no stated reason.

## The fix

```cpp
const bool empty_default_is_a_declaration =
    local_default && local_default->empty() && ns.empty() &&
    (!context_namespace.empty() || !declared.empty());
if (local_default && !empty_default_is_a_declaration) {
    ignore_default_declaration = true;
}
```

`local_default->empty()` is load-bearing and `local_default &&` is not
decoration: without the first the rule suppresses a non-empty declaration and
breaks the round trip, and without the second it dereferences null and crashes.
Both were mistakes made and caught here; see the finding.

Nothing else changed. The other half of this row's neighbourhood — the
redundant-`xmlns` cases where the declaration really does say nothing — still
drop it, and the suite asserts both directions.

## What it measured

`domparsing/XMLSerializer-serializeToString.html`, **28/33 → 29/33**, and
`make wpt` **9569/9824 → 9570/9824** with **0 fail and unlisted**. The entry
`serializer-redundant-xmlns` is off `tests/wpt/expected-failures.txt`, which is
one fewer reason in the corpus rather than a test made quiet.

The W3C XML suite is unchanged at **1718/1718** (261 canonical forms, 2585
documents).

## What I did not do, and why

I first tried a much larger version of this: let the **parser** accept
`xmlns:p=""` as a prefix unbinding, which is legal in Namespaces in XML **1.1**
and is what the WPT assertion's neighbourhood suggests. The project's own
conformance suite says no, in a file whose comment is the whole argument:

```xml
<!-- Illegal use of 1.1-style prefix unbinding in 1.0 document -->
<a:foo xmlns:a="http://example.org/namespace">
 <a:foo xmlns:a=""/>
</a:foo>
```

`eduni/namespaces/1.0/023.xml`. This engine reads XML 1.0 Fifth Edition, where
binding a prefix to no namespace is an error, and `tests/test_suite.cpp` already
asserts that it is one. So that change is reverted in full — parser and strict
serializer both — and the suite now asserts the **refusal** as part of the fix.

The same reasoning applies to the neighbours still failing. `serializer-not-well-formed`
is named for what it is: three subtests whose expected output is not well-formed
XML (`xmlns:ns1` written twice on one element; a DOCTYPE whose ids are `"""`).
This engine refuses or disqualifies those on purpose. Making them pass would mean
emitting broken markup to match a broken expectation, so they stay listed.

## Verification

- `tests/suites/360-xmlns-empty-is-a-declaration.cpp` — six cases: the escaping
  `xmlns=""` under a non-empty default, the empty namespace established once, a
  redeclaration that says nothing, the prefix-declaring pair, a redundant
  **non-empty** redeclaration still dropped, and the parser still refusing
  `xmlns:p=""`. **With the fix reverted it fails** on
  `out == "<root xmlns=\"\" xmlns:foo=\"urn:bar\"/>"`.
- **421/421** suites (`make test`).
- `make wpt` **9570/9824, 0 fail and unlisted**.
- **`make diff-xml` 400/400 trees round-trip, 0 fail** — the check that matters
  for a serializer, and the one that failed on the first version of this fix
  while every other scoreboard was green. See the finding.
- `make xml-conformance` **1718/1718** — unchanged, which is the check that the
  rejected parser change really was rejected.
- Reftests **65/66** and **40/41**. The 65 is *not* this phase's gain: it moved
  on `main` while this was in flight, and this phase changes no pixel.

## Still open

`serializer-standard-moved` (2) and `serializer-another-tree` (1) — namespace
prefix *selection* for attributes whose prefix is not preserved. Those are real
and want their own claim; they are not the same defect as this one, and the
first attempt at the neighbouring assertion showed how easy it is to "fix" a
serializer by making it write something worse.
