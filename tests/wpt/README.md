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
248 files, `MANIFEST.txt` has the Git blob id of each as that commit has it,
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
| `encoding/` | 16 test files, 5 scripts and `.headers` sidecars, 14 resources | `bom-handling`, `remove-only-one-bom`, `utf-32` and `utf-32-from-win1252`, and the `legacy-mb-{japanese,korean,tchinese}` error pages: each test iframes a document in a legacy encoding and reads the text it decoded to, which is the byte-level check on the tables of the Encoding Standard |
| `resources/testharness.js`, `common/subset-tests.js` | 2 | the harness, and the subset runner `big5-decode-extra.html` counts its 2,472 subtests with |
| `dom/common.js`, `dom/constants.js`, `common/` | 5 | what the tests include, and three documents (`blank.html`, `dummy.xml`, `dummy.xhtml`) some load into a frame |
| `fonts/` | 3 | Ahem and its stylesheet, used by two grid tests |
| `LICENSE.md` | 1 | the licence of the tests |

They are the tests for what Phase 28, Phase 42, Phase 47 and Phase 51 built
(XML in the DOM, nested `<iframe>` browsing contexts, XHTML page execution,
the WHATWG Encoding Standard, and the attribute model) and for the CSS Grid
and Flexbox property parsers and serializers they exercise. It is not all of
`dom/`, `encoding/` or `css/`: the rest is to be added as it is worked on. In
particular, only the *parsing* of the grid and flexbox suites is here.
Everything layout — `abspos/`, `alignment/`, `layout-algorithm/`,
`grid-lanes/` — is a test of a layout engine this project does not have yet;
those suites arrive with the engine, and the `.tentative.html` grid-lanes
files with them. The `encoding/` pages left out are the ones that call a
browser's `TextDecoder` and `TextEncoder`, which this engine does not
expose to script.

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
and are not listed`, with none fewer passing than the day before. That is
what one failed run said; the same step had failed four times before (twice
on the branch of that change, once on `main`, once on another branch), when
a failed job could not yet say anything (AUDIT.md, finding 49). So the
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

**9,567 of 9,824 subtests pass. 257 are listed:** the 8 below from the DOM
and encoding suites, and these 249 from the CSS parsing suites.

| Reason | Subtests | What it is |
| :--- | ---: | :--- |
| `grid-resolved-tracks` | 185 | The *resolved value* of a track listing (CSS Grid, "Resolved Value of a Track Listing"): `getComputedStyle` reports the tracks after the grid is laid out — implicit tracks made by positioned items show up, `auto-fit` bands with no items collapse to `0px`, `auto`/intrinsic tracks take their content size. Litebird serializes the specified value today; it has no grid placement and no track sizing, so these cannot pass until it does. 65 of them are newer than the rest: until Phase 72 the two files below a `document.fonts.ready` call (`grid-columns-rows-get-set-multiple.html`, `grid-content-sized-columns-resolution.html`) reported one `(harness)` row each and ran no subtest at all — `document.fonts` is in since then, and their subtests are listed one by one with this word |
| `intrinsic-auto-repeat-lanes` | 64 | `repeat(auto-fill, <intrinsic-size>)` — an intrinsic size inside an auto-repetition. The current Grid 3 draft (21 October 2026) makes that a grid-lanes feature only and says the declaration on a non-lanes grid computes to `none`, which is what Litebird does; these `.tentative.html` tests expect the lanes behavior, resolved against a layout, and are expected to move with the spec |

| Reason | Subtests | What it is |
| :--- | ---: | :--- |
| `test-checks-parent-global` | 1 | `domparsing/innerhtml-05.xhtml` calls `assert_throws_dom('SyntaxError', …)` without `window[0].DOMException` when setting `innerHTML` on an element from `window[0]`. The engine throws `window[0]`'s exception, as browsers do: a node's realm is its document's, so the binding's current realm is `window[0]`'s. The same rule is what `dom/nodes/Document-createElement.html` asks of 240 subtests that name the frame's constructor, and no implementation passes both. See [`docs/findings/68-the-realm-of-a-domexception.md`](../../docs/findings/68-the-realm-of-a-domexception.md) |
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
  1226 of the trees that pass hold a name that contradicts a declaration in scope (3315 such names);
  166 were written with other prefixes the second time, and none changed after that
```

Since Phase 51 an element may hold two attributes with one qualified name in
different namespaces — an `xmlns` in the XMLNS namespace and one in no
namespace, say — and no XML text can write that pair: a parser refuses the
repeated name. The tree generator makes one or the other, never both, and the
pair is exercised by the unit suite instead.

Following the two subtests above to the letter makes this fail: with the
standard's "generate a prefix" as written (issue 44) 265 of those 3,000 trees
do not come back, and with a prefix kept for its old namespace after a nearer
element has given it another (issues 45 and 75), 498 do not.
