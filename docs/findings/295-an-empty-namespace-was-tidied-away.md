# Finding 294 — the empty namespace was tidied away

**Severity:** correctness, and quiet. The XML serializer removed a default
namespace declaration whenever the element was already in the default namespace
in force. That rule is the standard's redundant-`xmlns` rule and it is right
almost always — but `xmlns=""` is the one value where "already in the namespace
in force" and "says nothing" come apart, because the empty namespace is what an
element *leaves* a namespace to enter. Dropping it silently puts the element
back in the ancestor's namespace, or leaves a prefix bound with no statement of
why.

**Found by:** taking `serializer-redundant-xmlns` off the WPT reason list at
random, then reading the assertion instead of guessing at it. The assertion is
one line:

```
root.setAttributeNS(XMLNS_URI, 'xmlns:foo', '');
assert_equals(serialize(root), '<root xmlns="" xmlns:foo=""/>');
```

The element has two declarations and the engine wrote one.

## The two shapes

```
<root xmlns="" xmlns:foo=""/>          before: <root xmlns:foo="urn:bar"/>
                                        after: <root xmlns="" xmlns:foo=""/>
```

and, the case the *standard* cares about, which already worked:

```
<root xmlns="urn:a"><child xmlns=""/></root>   both: <root xmlns="urn:a"><child xmlns=""/></root>
```

The second already wrote its declaration because the default *in force* was
non-empty; the first did not, because the empty namespace "matched" the empty
one in force. That is the shape of the bug: a condition that compares values
when the question is about statements.

## The trap in the neighbourhood

The obvious repair — teach the parser that `xmlns:p=""` is a prefix unbinding,
which Namespaces in XML **1.1** allows — is wrong here, and the repository
already contains the document that proves it:

```xml
<!-- Illegal use of 1.1-style prefix unbinding in 1.0 document -->
<a:foo xmlns:a="http://example.org/namespace">
 <a:foo xmlns:a=""/>
</a:foo>
```

`eduni/namespaces/1.0/023.xml`, from the W3C XML suite this project scores
1718/1718 on. This engine reads 1.0. The change was written, measured to work on
the WPT assertion, and then reverted in full when `make test` and the suite
comment showed what it cost. The phase doc records it because the next person
will have the same idea.

## The lesson

Three of the remaining `domparsing` failures expect output that is **not
well-formed XML** — `xmlns:ns1` written twice on one element, a DOCTYPE whose
identifiers are `"""`. The reason column already says so
(`serializer-not-well-formed`). A serializer can be made to pass any single
assertion; the question is always whether the bytes it writes afterwards are
still something a parser reads back. Here the answer was yes for one assertion
and no for the neighbours, which is exactly the line this phase drew.
