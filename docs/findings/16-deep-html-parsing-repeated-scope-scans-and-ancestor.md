# 16. ✅ Deep HTML parsing repeated scope scans and ancestor walks

> Moved out of `AUDIT.md` — written there as a `## 16. ✅ Deep HTML parsing repeated scope scans and ancestor walks` section.
> The text below is the section as it stood, unchanged.


The tree builder had two depth-dependent costs:

- Scope queries rescanned `m_open_elements` from the current node to a boundary.
  Deep stacks often asked about tags absent from the stack, or about a target
  below thousands of ordinary elements.
- `Node::append_child` called `is_connected()`, which walked all ancestors for
  every insertion.

The open-element stack now maintains HTML tag positions and scope-boundary
positions incrementally on push/pop. Scope membership is decided from the
nearest target and boundary indexes; the less common adoption-agency edits
rebuild the indexes after changing the middle of the stack. `Node` also caches
connectivity, updating it on parent changes and propagating through light DOM
and shadow roots. Parser insertion therefore does not repeatedly walk the
ancestor chain.

A local `-O2` parser-only harness (read input, call `HtmlParser::parse`, discard
the document) now measures:

| depth | time |
|---:|---:|
| 1,000 | 5 ms |
| 2,000 | 6 ms |
| 3,000 | 8 ms |
| 5,000 | 12 ms |
| 10,000 | 20 ms |
| 20,000 | 38 ms |

The old CLI measurements also serialized the resulting DOM. The web-platform
tree dumper emits indentation proportional to depth for every node, so dumping
a single deeply nested chain still produces O(depth²) output; those end-to-end
numbers should not be confused with parser time.

Conformance remains **1770/1792 (98.8%)**. The cache is tested across attach,
detach, reattach, and shadow-host connectivity transitions.

---
