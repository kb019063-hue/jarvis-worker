# 57. ✅ Attributes were keyed by qualified name; `Attr` was not an object; collections had no named own properties (Phase 51)

> Moved out of `AUDIT.md` — written there as a `## 57. ✅ Attributes were keyed by qualified name; `Attr` was not an object; collections had no named own properties (Phase 51)` section.
> The text below is the section as it stood, unchanged.


An element kept its attributes in a `std::vector<std::pair<std::string,
std::string>>` — qualified name to value — with the namespace in a side table,
`AttributeNamespaces`, keyed by that name. So an element could hold only one
attribute per qualified name, whatever namespace it stood in: the second
`setAttributeNS('ab', 'attr', …)` replaced the first. The DOM Standard keys an
attribute list by *expanded name*, (namespace, local name), and the qualified
name is only how a script finds one. Measured: 33 listed web-platform-tests
subtests in four shapes — two attributes with one qualified name in different
namespaces (14), an `Attr` that must be an object and not a `Node`
(`cloneNode`, `lookupNamespaceURI`, `importNode(attr)`, an `Attr` adopted with
its element: 10), the named own properties of a `NamedNodeMap` and an
`HTMLCollection` (8), and the strict-mode write to an index of a collection (1,
still open as `strict-write`).

**What changed.** `Attribute` carries `name`, `value`, an interned
`const std::string*` namespace and `local_at` (where the local name starts), and
`AttributeList` indexes both the qualified name (first match, `kRepeated` when a
script made a duplicate) and the expanded name; `rename()` gives an attribute a
new qualified name in place, which is what `setAttributeNodeNS` does.
`Element` gained the expanded-name API, and the by-name methods act on the
*first* attribute whose qualified name matches, in its own namespace.
`DomHooks::attribute_changed` became `(Element&, const AttributeChange&)`, so a
custom element's callback gets its namespace argument and a `MutationRecord` its
`attributeNamespace` (both were always `null`). `Attr` is one object per
(element, namespace, local name). `JsExotic` gained `own_keys` and
`computed_property`, so `Object.getOwnPropertyNames` lists a `NamedNodeMap`'s and
an `HTMLCollection`'s names after their indices, non-enumerable and
configurable.

**Verification.** `make wpt` **7998/8009** in 103 files (41 listed before,
11 after, 0 unlisted; `dom/nodes/attributes.html` 60/67 → **77/77**),
`make test` **179 → 182** suites at the time (the new suite
`attributes_are_kept_by_namespace_and_local_name`, and the randomized
attribute-list model rewritten against expanded names), `make conformance`
1792/1792, `make dom-conformance` 644/644, `make js-conformance` 926/940,
`make url-conformance` 1188/1188, `make xml-conformance` 1718/1718,
`make diff-trees` 0 differ, `make diff-xml` 0 DIFFER and 3000/3000 round trips,
`make deep` 1332 runs 0 failed, `make asan` green.

**Still open.** A refused property write is refused in silence (2 subtests,
listed as `strict-write`): the write path (`JsValue::set_property`,
`JsEval.cpp`) had to report "refused" for a strict caller to throw, and it was
claimed by another change. An `Attr`'s `childNodes` is empty rather than a
`Text` node holding its value.

---
