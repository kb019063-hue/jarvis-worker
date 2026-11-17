# Phase 52 Architecture: Repairing `main` After the Phase 49 and Phase 51 Merges

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 52 Architecture: Repairing `main` After the Phase 49 and Phase 51 Merges` section.
> The text below is the section as it stood, unchanged.


A repair, not a phase of its own: `main` at `57a94be` did not compile. Phase 49
(#58) had added `clone_node_tree` for `<selectedcontent>` cloning, and it copied
an element's attributes and then called `element->copy_attribute_namespaces(from)`.
Phase 51 (#60) deleted that method with the `AttributeNamespaces` side table: an
`Attribute` carries its own namespace now, an interned `const std::string*`, so
`element->m_attributes = from.m_attributes` copies the namespaces too. Both
pull requests were green on their own head commit, each tested against a `main`
that did not yet contain the other, and the merge of the two had never been
compiled.

What changed is one deleted call. What was added is the test that would have
caught it: no suite looked at an attribute's namespace on a *cloned* node, so
the new `cloning_for_selectedcontent_keeps_attribute_namespaces` parses a
`<select><button><selectedcontent>` page whose selected `<option>` holds
`<svg xml:lang="en"><use xlink:href="#i"/>` and reads the clone's attributes by
namespace (`xml:lang` in the XML namespace, `xlink:href` in the XLink one, the
nodes in the SVG one), then checks that changing the original does not change
the clone. The README's suite count went from 181 to 183 (Phase 49's two plus
this one), and the scoreboards are the ones Phase 51 left: web-platform-tests
7998/8009 with 11 listed, html5lib 1792/1792, jsdom 644/644, JS 926/940, URL
1188/1188, the W3C XML suite 1718/1718, 6,000 generated pages with 0
differences, 4,000 XML documents and 3,000 round trips, 1,332 deep-page runs
and the sanitizer build.

The rule it broke is written in `AGENTS.md` and was skipped here: merge `main`
into the branch, re-run the checks, and let CI pass on *that* commit before
merging. A pull request behind `main` says nothing about the merge.

---
