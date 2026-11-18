# Phase 51 Architecture: The Attribute Model — Namespaces, `Attr` Objects, and Named Properties

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 51 Architecture: The Attribute Model — Namespaces, `Attr` Objects, and Named Properties` section.
> The text below is the section as it stood, unchanged.


### 1. The hole

An element kept its attributes in a `std::vector<std::pair<std::string,
std::string>>`: the qualified name and the value. The namespace was in a side
table, `AttributeNamespaces`, keyed by the qualified name — so an element could
hold only one attribute per qualified name, whatever namespace it stood in, and
the second `setAttributeNS('ab', 'attr', …)` replaced the first. The standard
keys an attribute list by *expanded name*, (namespace, local name), and the
qualified name is only how a script finds one. That one wrong key cost 33
web-platform-tests subtests in four shapes: two attributes with one qualified
name in different namespaces (14), an `Attr` that must be an object and not a
`Node` — `attr.cloneNode()`, `attr.lookupNamespaceURI()`, `importNode(attr)`,
an `Attr` adopted with its element (10), the named own properties of a
`NamedNodeMap` and an `HTMLCollection` (8), and the strict-mode write to an
index of a collection (1, still open).

### 2. One attribute, one record

`include/litebird/dom/AttributeList.hpp` now stores

```cpp
struct Attribute {
    std::string name;            // the qualified name, as written
    std::string value;
    const std::string* ns;       // an interned namespace name, or null
    std::uint32_t local_at;      // where the local name starts in `name`
};
```

with `has_namespace()`, `namespace_uri()`, `local_name()` and
`is_expanded()`. The namespace name is interned once per program
(`intern_namespace_name`, a function-local set behind a mutex), so an SVG
document with 100,000 `xlink:href` attributes holds `"http://www.w3.org/1999/
xlink"` once and every attribute points at it. `local_at` is the offset after
the prefix colon, or 0 when there is no namespace: in no namespace the whole
name *is* the local name, which is what `setAttribute('a:b', …)` and
`setAttributeNS(null, 'a:b', …)` both mean. `AttributeNamespaces` is gone.

The index that a long list builds (Phase 35 keeps the vectors and the logical
start offset) now holds two maps: the qualified name to the number of the first
attribute with it, and the expanded name to the one attribute that has it. A
qualified name that appears more than once — only a script can make that —
records `kRepeated`, and looking it up takes the scan it always did. Everything
else is unchanged: numbers only grow, `position()` bisects them, a front erase
advances the start, copy drops the index, and an empty list allocates nothing.
`rename(iterator, name, local_at)` is new: `setAttributeNodeNS` replacing an
attribute keeps its place and its namespace and takes the new qualified name,
which the tests of `Element-setAttributeNodeNS.html` check because the *order*
of the lookups changes with it.

`Element` lost `copy_attribute_namespaces` and gained the expanded-name API:
`find_attribute(name)`, `find_attribute_ns(ns, local)`, `set_attribute_ns`,
`add_attribute`, `remove_attribute_ns`, `attribute_namespace`,
`set_attribute_value(const Attribute&, value)` and `replace_attribute`. The
by-name methods (`getAttribute`, `hasAttribute`, `removeAttribute`,
`toggleAttribute`, `setAttribute`, `getAttributeNode`) find the **first**
attribute whose qualified name matches and act on that one, in its own
namespace — `setAttribute` changes its value and neither adds an attribute nor
moves it.

### 3. What a hook is told, and what an `Attr` is

`DomHooks::attribute_changed` is now
`void (*)(Element&, const AttributeChange&)` with

```cpp
struct AttributeChange {
    const std::string* name;          // qualified
    const std::string* namespace_uri; // null when in no namespace
    const std::string* local_name;
    const std::string* old_value;     // null when it was added
    const std::string* new_value;     // null when it was removed
};
```

`Node.cpp` copies every string before the hook runs, because the hook may
re-enter and change the element again. Both consumers now see the namespace:
`attributeChangedCallback(name, oldValue, newValue, namespace)` gets its fourth
argument (it was always `null`), and a `MutationRecord`'s `attributeNamespace`
reports it (it was always `null` too).

`AttrState` is (owner element, namespace, local name) — the old cache slot
`"attr:" + qualified_name` on the element's object could not hold two
attributes. `getAttributeNode` and `getAttributeNodeNS` therefore return one
object per attribute, `isSameNode` compares identity, `cloneNode()` makes a
detached copy with the same names and value, `lookupNamespaceURI` asks the
element the attribute is on, `importNode` copies an `Attr` into the document,
`adoptNode` moves a detached one, and adopting an element carries its
attributes along because an attribute's document is its element's.
`removeAttributeNS` leaves the `Attr` object detached with its last value and a
null `ownerElement`.

### 4. Named properties

`JsExotic` gained two hooks: `own_keys` (every computed name, enumerable or
not) and `computed_property` (whether a name enumerates and can be written).
`Object.getOwnPropertyNames` asks for the first, `Object.getOwnPropertyDescriptor`
for the second, and `Object.keys`/`for…in` still ask `keys`, which reports only
the enumerable ones. A `NamedNodeMap` lists its indices and then its qualified
names — one entry per name, in insertion order, and on an HTML element of an
HTML document only the names that are already lower case; an `HTMLCollection`
lists its indices and then the `id`s and `name`s of its elements. Both
properties are `[LegacyUnenumerableNamedProperties]`: `enumerable: false`,
`configurable: true`, so `for…in` skips them and `hasOwnProperty` says yes.

### 5. What it moved

| Scoreboard | Before | After |
| :--- | ---: | ---: |
| web-platform-tests | 7968/8009, 41 listed | **7998/8009 in 103 files**, 11 listed, 0 unlisted |
| — `dom/nodes/attributes.html` | 60/67 | **77/77** |
| — `Document-`/`Element-getElementsByTagName.html` | 2 listed each | **1 listed each** (the strict-mode write) |
| Unit suites | 177 | **179** |

`tests/wpt/expected-failures.txt` went from 41 rows to 11. The words
`one-name-two-namespaces`, `attr-is-not-a-node` and `named-properties` are gone
from it; the two subtests that are left of that block are listed as
`strict-write`. The new unit suite is
`attributes_are_kept_by_namespace_and_local_name`: the same qualified name in
two namespaces (get, set, remove and toggle on the first), an attribute's own
namespace/prefix/local name, the named own properties and their descriptors of
a `NamedNodeMap` and of an `HTMLCollection`, `Attr` identity and detachment,
`setAttributeNodeNS` reordering, and the namespace in a custom element's
callback and in a `MutationRecord`. The randomized model test in
`attribute_lists_keep_their_order_and_stay_fast` now compares an `Element`'s
attributes against a vector of (namespace, local name, value) rows instead of
the deleted side table.

### 6. What is still open

A write that a property refuses is refused in silence: assigning to an
out-of-range index of an `HTMLCollection` does not throw in strict mode (2
subtests, `strict-write`), and the same is true of a write to a non-writable
property anywhere else in the engine. `JsExotic::set` returns "handled" and has
no way to say "refused", and the strictness of the assignment is known only in
`JsEval.cpp` — a file claimed by another change. An `Attr`'s `childNodes` is
empty rather than a `Text` node holding its value: this engine keeps an
attribute's value in one place, and `value`/`textContent` read it.
`getAttributeNames` and a `NamedNodeMap`'s names still store the qualified name
the script wrote rather than the name in the namespace's canonical form, which
no test asks for yet. Two attributes of one element with one qualified name in
different namespaces cannot be written as XML — a parser refuses the repeated
name — so `tools/fuzz/xml_roundtrip.py` now makes one `xmlns` or the other and
never both; before this phase the second replaced the first, so the corpus
never met the case. The 3,000-tree round trip still comes back 3,000/3,000.
