# Web Platform Tests

A part of [web-platform-tests](https://github.com/web-platform-tests/wpt), the
tests the browsers share and the standards are written against, run in
Litebird with the tests' own harness (`resources/testharness.js`, the real
one).

The other DOM corpus, `tests/dom-conformance/`, records what jsdom answers.
jsdom is a careful implementation and it is not the standard: its XML
serializer, for one, follows a text the standard has since changed. These
tests are the other witness. The first run of them here found things 607
jsdom-recorded cases had not: `getAttribute('xlink:href')` was null on every
inline `<svg>` the HTML parser read, a doctype a script inserted was not the
document's `doctype`, and `lookupNamespaceURI('xml')` answered for a node that
has no namespaces.

```bash
make wpt                                                   # the scoreboard
python3 tools/conformance/run_wpt.py --show 20             # with the failures that are not listed
python3 tools/conformance/run_wpt.py --filter XMLSerializer
python3 tools/conformance/run_wpt.py --list-failures       # every failure, in the form of the list
```

## What is here

Copied from `web-platform-tests/wpt` at commit
`f2b7b7add021b5f40e30c65dd348135058996249` (14 November 2026), unchanged:
210 files, `MANIFEST.txt` has the Git blob id of each as that commit has it,
and the runner checks the files against it before every run. Do not edit a
test. To take a newer copy, replace the files and write the manifest again
from upstream's tree.

| | Files | About |
| :--- | ---: | :--- |
| `domparsing/` | 27 test files, 8 support files | `DOMParser` for XML and HTML (including cross-frame URL and `pushState` provenance), XHTML `innerHTML` / `insertAdjacentHTML` / `createContextualFragment`, the `parsererror` document, CDATA, doctypes, entities, `XMLSerializer`, a round trip |
| `dom/nodes/` | 60 test files, 10 scripts they include | namespaces and names (`createElementNS`, `createDocument`, `createAttribute`, `lookupNamespaceURI`), the attribute methods, CDATA sections and processing instructions, cloning, inserting and replacing children of a document, character data, cross-frame adoption and constructors |
| `css/css-flexbox/parsing/` | 27 test files | the flexbox properties: what values are valid and invalid, what the computed value is, the shorthands and their serialization, `-webkit-` legacy aliases |
| `css/css-grid/parsing/` | 60 test files, 1 metadata file (`WEB_FEATURES.yml`) | the grid track properties, named lines and `grid-template-areas`, the `repeat()` notation including auto-fill/auto-fit, the shorthands and their serialization |
| `css/support/`, `css/css-grid/support/` | 7 support files | what the parsing tests include (`parsing-testcommon.js`, `computed-testcommon.js`, `shorthand-testcommon.js`, `grid-child-utils.js`, three stylesheets) |
| `resources/testharness.js` | 1 | the harness |
| `dom/common.js`, `dom/constants.js`, `common/` | 5 | what the tests include, and three documents (`blank.html`, `dummy.xml`, `dummy.xhtml`) some load into a frame |
| `fonts/` | 3 | Ahem and its stylesheet, used by two grid tests |
| `LICENSE.md` | 1 | the licence of the tests |

They are the tests for what Phase 28 and Phase 42 built (XML in the DOM,
nested `<iframe>` browsing contexts, and XHTML page execution) and for the
code they touched, and for the CSS Grid and Flexbox property parsers and
serializers. It is not all of `dom/` and not all of `css/`: the rest is to be
added as it is worked on. In particular, only the *parsing* of the grid and
flexbox suites is here. Everything layout — `abspos/`, `alignment/`,
`layout-algorithm/`, `grid-lanes/` — is a test of a layout engine this
project does not have yet; those suites arrive with the engine, and the
`.tentative.html` grid-lanes files with them.

Left out of `domparsing/`: streaming and Trusted Types tests, and `.sub.html`
server-substitution tests. Left out of `css/`: the layout suites, `animation/`,
and `.sub.html` tests.

The licence is `LICENSE.md`, the 3-Clause BSD licence of the project.

## How a test is run

The runner puts every `<script src>` of the top-level test into the staged
page inline (wrapped in `<![CDATA[…]]>` when the page is `.xhtml`), puts a
reporter in place of `resources/testharnessreport.js`, and runs the page with
`--run-script` and `--map-origin=https://web-platform.test=tests/wpt` so nested
`<iframe>` navigations and their subresources resolve against the suite root.
The page's address is `https://web-platform.test/<path>`. Nothing else is
changed, and the files in this directory are not touched.

## The failures that are listed

`expected-failures.txt` has every subtest that fails, with a word for why.
The runner fails when a subtest fails that is not listed, and when a listed
one passes, so the list stays what is true today.

**6,961 of 7,198 subtests pass. 237 are listed:** the 42 below from the
DOM suites, and these 195 from the CSS parsing suites.

| Reason | Subtests | What it is |
| :--- | ---: | :--- |
| `grid-resolved-tracks` | 120 | The *resolved value* of a track listing (CSS Grid, "Resolved Value of a Track Listing"): `getComputedStyle` reports the tracks after the grid is laid out — implicit tracks made by positioned items show up, `auto-fit` bands with no items collapse to `0px`, `auto`/intrinsic tracks take their content size. Litebird serializes the specified value today; it has no grid placement and no track sizing, so these cannot pass until it does |
| `intrinsic-auto-repeat-lanes` | 64 | `repeat(auto-fill, <intrinsic-size>)` — an intrinsic size inside an auto-repetition. The current Grid 3 draft (21 October 2026) makes that a grid-lanes feature only and says the declaration on a non-lanes grid computes to `none`, which is what Litebird does; these `.tentative.html` tests expect the lanes behavior, resolved against a layout, and are expected to move with the spec |
| `css-values-4` | 8 | `sign()`, `sibling-index()` (and what they need: the context the value is evaluated in) and the Values 4 term order of a serialized `calc()`. None is implemented; the `sign(…)` cases in `flex-computed.html`/`flex-valid.html` cannot be answered without them |
| `detached-computed-style` | 1 | `grid-template-node-not-connected.html`: after the element is disconnected, the test wants `getComputedStyle().gridTemplate` to be the empty string (it guards a WebKit crash, bug 261421). Litebird answers `none`, the computed value; it does not special-case disconnected elements |
| `document-fonts` | 2 | `document.fonts` (`FontFaceSet`) is not implemented; two tests call `document.fonts.ready.then(…)` and harness-error before any subtest runs |

| Reason | Subtests | What it is |
| :--- | ---: | :--- |
| `one-name-two-namespaces` | 14 | Two attributes of one element with the same qualified name and different namespaces (`setAttributeNS('ab', 'attr', …)` then `setAttributeNS('kl', 'attr', …)`). An element keeps its attributes under their qualified names, so the second replaces the first. Only a script can make such a pair; no parser can |
| `attr-is-not-a-node` | 10 | An `Attr` is an object and not a `Node`: `attr.cloneNode()`, `attr.lookupNamespaceURI()`, `importNode(attr)`, an `Attr` adopted with its element |
| `named-properties` | 8 | `Object.getOwnPropertyNames` and property descriptors of a `NamedNodeMap` and an `HTMLCollection`; assigning to an index of an `HTMLCollection` in strict mode |
| `nodelist-iterator` | 1 | `NodeList.prototype[Symbol.iterator]` is not the same function as `Array.prototype[Symbol.iterator]` |
| `page-encoding` | 1 | `document.characterSet` of the page is always UTF-8 |
| `test-checks-parent-global` | 1 | `domparsing/innerhtml-05.xhtml` calls `assert_throws_dom('SyntaxError', …)` without `window[0].DOMException` when setting `innerHTML` on an element from `window[0]`; WebIDL throws from the setter's Realm (`window[0]`) |
| the four below | 7 | Where the test and the standard, or the test and XML, disagree (including two `xml-serialization.xhtml` doctype subtests expecting unquoted `"""` inside a `PUBLIC` literal) |

### `XMLSerializer`: five subtests Litebird fails on purpose

`domparsing/XMLSerializer-serializeToString.html` was written against the
text of *DOM Parsing and Serialization* as it stood in 2019. Two things have
happened since.

The standard changed on 1 September 2026 (`w3c/DOM-Parsing`, "Preserve an
attribute's namespace prefix when possible", closing issues 29 and 30, which
say that browsers always did):

| Word | Subtest | The test expects | The standard now, and Litebird |
| :--- | :--- | :--- | :--- |
| `serializer-standard-moved` | "…the prefix of an attribute is NOT preserved…" | `xmlns:ns1="uri2" ns1:name="value"` | `xmlns:p="uri2" p:name="value"`: the attribute keeps its prefix |
| `serializer-standard-moved` | "…the same prefix declared in an ancestor element" | `<child xmlns:ns1="uri2" ns1:foobar="v"/>` | `<child xmlns:p="uri2" p:foobar="v"/>`: a prefix an ancestor declared is declared again |

And the standard's algorithm has faults that are filed and open, which three
subtests assert:

| Word | Subtest | The test expects | Litebird |
| :--- | :--- | :--- | :--- |
| `serializer-not-well-formed` | "…"ns1" is generated even if the element already has xmlns:ns1" | `<child xmlns:ns1="uri1" xmlns:ns1="uri3" ns1:attr1="value1"/>`, an element with one attribute twice, which no XML parser reads (issue 44) | `xmlns:ns3="uri3" ns3:attr1="value1"`: a generated prefix that stands for something already is passed over |
| `serializer-another-tree` | "…even if the prefix is assigned to another namespace" | `<el2 xmlns:q="u2" q:name="v"/>` for an attribute in `u1`: read back, the attribute is in `u2` (issue 45) | `p:name="v"`, the prefix that still stands for `u1` |
| `serializer-redundant-xmlns` | "…a prefix bound to an empty namespace URI…" | `<root xmlns="" xmlns:foo=""/>` | `<root xmlns:foo=""/>`: the standard leaves out a default declaration that says what is already so, and the subtest "Check if redundant xmlns="..." is dropped" of the same file asserts exactly that. No implementation passes both |

The serializer is checked by something stronger than either reading. A tree
that is written and read again must be the same tree: every element and
attribute with the namespace and the local name it had. That is a statement
about Litebird that needs no reference to be right about, and
`tools/fuzz/xml_roundtrip.py` (`make diff-xml` runs it) checks it on trees
made to break it: a prefix that stands for one namespace on an element and
for another on its parent, elements whose own `xmlns` attributes contradict
their names, an `xmlns` attribute in no namespace, and the prefixes `ns1`,
`ns2`, `ns3`, which are the ones the serializer makes up. Each tree is
serialized, the text is read by `DOMParser` and by Expat, and both must give
the tree that was written; the tree read back is then serialized and read
once more.

```text
$ python3 tools/fuzz/xml_roundtrip.py --count 3000
XML round trip: 3000 trees, 3000 come back the same from Litebird and from Expat, 0 do not
  1252 of the trees that pass hold a name that contradicts a declaration in scope (3347 such names);
  165 were written with other prefixes the second time, and none changed after that
```

Following the two subtests above to the letter makes this fail: with the
standard's "generate a prefix" as written (issue 44) 265 of those 3,000 trees
do not come back, and with a prefix kept for its old namespace after a nearer
element has given it another (issues 45 and 75), 498 do not.
