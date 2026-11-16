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
150 files, `MANIFEST.txt` has the Git blob id of each as that commit has it,
and the runner checks the files against it before every run. Do not edit a
test. To take a newer copy, replace the files and write the manifest again
from upstream's tree.

| | Files | About |
| :--- | ---: | :--- |
| `domparsing/` | 27 test files, 8 support files | `DOMParser` for XML and HTML (including cross-frame URL and `pushState` provenance), XHTML `innerHTML` / `insertAdjacentHTML` / `createContextualFragment`, the `parsererror` document, CDATA, doctypes, entities, `XMLSerializer`, a round trip |
| `dom/nodes/` | 60 test files, 10 scripts they include | namespaces and names (`createElementNS`, `createDocument`, `createAttribute`, `lookupNamespaceURI`), the attribute methods, CDATA sections and processing instructions, cloning, inserting and replacing children of a document, character data, cross-frame adoption and constructors |
| `encoding/` | 16 test files, 5 scripts and `.headers` sidecars, 14 resources | `bom-handling`, `remove-only-one-bom`, `utf-32` and `utf-32-from-win1252`, and the `legacy-mb-{japanese,korean,tchinese}` error pages: each test iframes a document in a legacy encoding and reads the text it decoded to, which is the byte-level check on the tables of the Encoding Standard |
| `resources/testharness.js`, `common/subset-tests.js` | 2 | the harness, and the subset runner `big5-decode-extra.html` counts its 2,472 subtests with |
| `dom/common.js`, `dom/constants.js`, `common/` | 5 | what the tests include, and three documents (`blank.html`, `dummy.xml`, `dummy.xhtml`) some load into a frame |
| `LICENSE.md` | 1 | the licence of the tests |

They are the tests for what Phase 28, Phase 42 and Phase 47 built (XML in the
DOM, nested `<iframe>` browsing contexts, XHTML page execution, and the
WHATWG Encoding Standard) and for the code they touched. It is not all of
`dom/` or `encoding/`: the rest is to be added as it is worked on — the
`encoding/` pages left out are the ones that call a browser's `TextDecoder`
and `TextEncoder`, which this engine does not expose to script.

Left out of `domparsing/`: streaming and Trusted Types tests, and `.sub.html`
server-substitution tests.

The licence is `LICENSE.md`, the 3-Clause BSD licence of the project.

## How a test is run

The runner puts every `<script src>` of the top-level test into the staged
page inline (wrapped in `<![CDATA[…]]>` when the page is `.xhtml`), puts a
reporter in place of `resources/testharnessreport.js`, and runs the page with
`--run-script` and `--map-origin=https://web-platform.test=tests/wpt` so nested
`<iframe>` navigations and their subresources resolve against the suite root.
The page's address is `https://web-platform.test/<path>`. Nothing else is
changed, and the files in this directory are not touched.

**Two time limits.** `--timeout SECONDS` is the runner's, for one file (60
by default; `make asan` gives a sanitizer build 600). The harness in the
page has one of its own, 10 seconds, and reports `TIMEOUT` for the file when
it is passed, as one more row, `(harness)`. That happens to a page which
needs more than 10 seconds to reach its load event even when every subtest
has passed by then: the harness finishes from a zero-delay timer it sets in
the load event, and its 10-second timer, set when `testharness.js` was read,
is due first. A page with one subtest that takes 11 seconds prints `PASS`
for the subtest and `TIMEOUT` for the file. In a sanitizer build on a busy
machine the long files come close (under ASan here `big5-decode-extra.html`
needs 6 seconds to its load event and `Node-compareDocumentPosition.html`
3.5, each alone on a core), and CI's sanitizer job failed that way: `1 fail
and are not listed`, with all 7,968 passing. That is what one failed run
said; the same step had failed three times before, when a failed job could
not yet say anything (AUDIT.md, finding 49). So the
reporter scales the harness's limit with the runner's: it calls
`setup({timeout_multiplier: SECONDS / 60})`, which is what wptrunner does
for slow builds, and calls nothing when that is 1. No test here uses
`step_timeout` or `step_wait`, the other things that multiplier lengthens.

A run that fails names the first ten failures that are not listed, also
with `--quiet`.

## The failures that are listed

`expected-failures.txt` has every subtest that fails, with a word for why.
The runner fails when a subtest fails that is not listed, and when a listed
one passes, so the list stays what is true today.

**7,968 of 8,009 subtests pass. 41 are listed:**

| Reason | Subtests | What it is |
| :--- | ---: | :--- |
| `one-name-two-namespaces` | 14 | Two attributes of one element with the same qualified name and different namespaces (`setAttributeNS('ab', 'attr', …)` then `setAttributeNS('kl', 'attr', …)`). An element keeps its attributes under their qualified names, so the second replaces the first. Only a script can make such a pair; no parser can |
| `attr-is-not-a-node` | 10 | An `Attr` is an object and not a `Node`: `attr.cloneNode()`, `attr.lookupNamespaceURI()`, `importNode(attr)`, an `Attr` adopted with its element |
| `named-properties` | 8 | `Object.getOwnPropertyNames` and property descriptors of a `NamedNodeMap` and an `HTMLCollection`; assigning to an index of an `HTMLCollection` in strict mode |
| `nodelist-iterator` | 1 | `NodeList.prototype[Symbol.iterator]` is not the same function as `Array.prototype[Symbol.iterator]` |
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
