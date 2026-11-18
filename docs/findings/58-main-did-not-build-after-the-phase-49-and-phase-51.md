# 58. ✅ `main` did not build after the Phase 49 and Phase 51 merges (Phase 52)

> Moved out of `AUDIT.md` — written there as a `## 58. ✅ `main` did not build after the Phase 49 and Phase 51 merges (Phase 52)` section.
> The text below is the section as it stood, unchanged.


Phase 49 (#58) added `clone_node_tree` for `<selectedcontent>` cloning, which
copied an element with `element->m_attributes = from.m_attributes` and then
`element->copy_attribute_namespaces(from)`. Phase 51 (#60) deleted that method
together with the `AttributeNamespaces` side table: an `Attribute` now carries
its own namespace (an interned `const std::string*`), so copying the list copies
the namespaces with it. Both pull requests were green on their own head commit
and each was tested against a `main` that did not contain the other, so the
merge of the two --- `57a94be` --- does not compile:

```
src/html/HtmlTreeBuilder.cpp:1312:26: error: 'using element_type = class litebird::Element'
  {aka 'class litebird::Element'} has no member named 'copy_attribute_namespaces'
```

**What changed.** The call is deleted; `m_attributes` already carries everything.

**Verification.** `make test` (183 suites, the new
`cloning_for_selectedcontent_keeps_attribute_namespaces` among them),
`make conformance` 1792/1792, `make dom-conformance` 644/644, `make
js-conformance` 926/940, `make url-conformance` 1188/1188, `make
xml-conformance` 1718/1718, `make wpt` 7998/8009 (11 listed, 0 unlisted),
`make diff-trees` 0 differ, `make diff-xml` 0 DIFFER and 3000/3000 round
trips, `make deep` 1332 runs 0 failed, `make asan` green.

**The guard.** Neither suite looked at an attribute's *namespace* on a cloned
node, which is the line the two changes crossed on. The new unit suite parses
`<select><button><selectedcontent>` with an `<option>` whose content holds
`<svg xml:lang="en"><use xlink:href="#i"/>` and reads the clone's attributes by
namespace: `xml:lang` in `http://www.w3.org/XML/1998/namespace`,
`xlink:href` in `http://www.w3.org/1999/xlink`, both nodes still in the SVG
namespace, and the clone independent of the option's own subtree.

**Still open.** Nothing, beyond the rule this broke: a pull request is merged
only after `main` has been merged into it and CI has passed on that commit.
