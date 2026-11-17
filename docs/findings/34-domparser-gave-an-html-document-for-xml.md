# 34. ✅ `DOMParser` gave an HTML document for XML

> Moved out of `AUDIT.md` — written there as a `## 34. ✅ `DOMParser` gave an HTML document for XML` section.
> The text below is the section as it stood, unchanged.


**The hole.** There was no XML parser. `new DOMParser().parseFromString(text,
'application/xml')` ran the HTML parser whatever type it was given, so a
script that read a feed, a configuration file or an SVG image got a tree, and
not the one in the text: the root was `HTML`, a CDATA section was a comment,
names were upper case, and text that was not well formed gave a tree like any
other. `XMLHttpRequest` was a stub that failed every request, so
`responseXML` was null on every path. Finding 31 listed the first as known
and not fixed.

| A script on a page | Before | Now |
| :--- | :--- | :--- |
| `d = parseFromString('<a xmlns="urn:x"><b id="k">t</b><![CDATA[<c>]]></a>', 'application/xml')`; `d.documentElement.nodeName`, `.namespaceURI`, `d.contentType` | `HTML`, `http://www.w3.org/1999/xhtml`, `text/html` | `a`, `urn:x`, `application/xml` |
| `new XMLSerializer().serializeToString(d)` | `<html xmlns="http://www.w3.org/1999/xhtml"><head /><body><a xmlns="urn:x"><b id="k">t</b><!--[CDATA[<c-->]]&gt;</a></body></html>` | The text that went in |
| `parseFromString('<a><b></a>', 'application/xml').documentElement.nodeName` | `HTML` | `parsererror` |
| `parseFromString('<svg xmlns="http://www.w3.org/2000/svg"><path d="M0 0"/></svg>', 'image/svg+xml')`: root, its first child, the type | `HTML`, `HEAD`, `text/html` | `svg`, `path`, `image/svg+xml` |
| `x.open('GET', 'data:text/xml,<a><b>t</b></a>'); x.send()` | `error`, `loadend`; status 0; `responseXML` null | `loadstart`, `load`, `loadend`; status 200; `responseXML.querySelector('b').textContent` is `t` |
| `el.getAttribute('xlink:href')` and `el.getAttributeNS('http://www.w3.org/1999/xlink', 'href')` on a `<use xlink:href="#a">` in a page | `null`, `null` | `#a`, `#a` |
| `for (i = 0; i < 50000; i++) el.setAttribute('a' + i, i)` | 6.3 s: every call searched the list | 49 ms |

**The fix.** An XML 1.0 (Fifth Edition) parser with namespaces
(`src/xml/XmlParser.cpp`), which does not validate and reads nothing outside
the text it is given; documents that know whether they are HTML; namespaces
on elements and attributes, and an index for an attribute list that is long;
CDATA sections and processing instructions as nodes; the XML serializer of
*DOM Parsing and Serialization* for `XMLSerializer` and for `innerHTML` in an
XML document; and `XMLHttpRequest` for `data:` URLs. ARCHITECTURE.md, Phase
28, has the design.

### What each reference found

The parser was written against the W3C's suite, and passed it, before any of
the following was known. Each was found by a different instrument, and none
of them by reading the code.

| Found by | What |
| :--- | :--- |
| Pages that are large, not wrong (`tools/fuzz/deep_pages.py`) | An element with 50,000 attributes took 7.1 s to parse and 4 s to serialize: the DOM searched its attribute list for every attribute it added, as it does on `main`. 50,000 namespace declarations with a child each took over two minutes: the scope was copied a level and searched a name. Removing every second of 50,000 attributes took 88 s with the first index written for the list, which held positions. All but the last are 0.2 s or less now; removal from the front was still the square then (fixed in Phase 35, finding 40) |
| The same question asked of the DOCTYPE | 50,000 attributes declared for one element took 4.5 s. And one attribute default of a megabyte with 100,000 empty tags made 100 GB of attribute values from a 1.4 MB document: entity expansion was bounded and defaults were not. Both bounded now, and both are deep pages |
| Expat and libxml2 on generated documents (`tools/fuzz/diff_xml.py`) | No fault of Litebird's in 64,000 documents, and one in the references: three runs of 20,000 reported 10 differences, and all ten were `version="1."`, which Expat and libxml2 accept and the grammar does not (`'1.' [0-9]+`). A document whose version is outside the grammar is now judged by the grammar |
| web-platform-tests, on their first run | Three faults that were not about XML: attributes of inline `<svg>` had no namespace (the row with `xlink:href` above), a doctype a script inserted was not `document.doctype`, and `lookupNamespaceURI('xml')` answered for a node that has no namespaces. 607 jsdom-recorded cases had passed over all three |
| Writing a tree and reading it back (`tools/fuzz/xml_roundtrip.py`) | The standard's serializer, followed to the letter, writes documents that read back as another tree: 265 of 3,000 trees with its "generate a prefix", 498 with a prefix kept for a namespace a nearer element took from it. Four issues are open against the standard for these; Litebird leaves the text there and says where (`tests/wpt/README.md`) |
| A page of 200,000 `x` in UTF-16, written for the request code | A response of type `text/xml;charset=utf-16le` with no byte order mark gave no document: the charset of the transport was checked against what the first bytes looked like, as if it were the document's own declaration |
| CI's sanitizer job, on the first commit it ran | A use-after-free in `XMLHttpRequest`: the charset of the response's type was read through a pointer into a `MimeType` that was a temporary, one statement after it was gone. Every request for a type with a charset did it, and the normal build passed all 124 suites and all 629 cases: the bytes were still there. `MimeType::parameter()` cannot be called on a temporary any more |
| The sanitizer build of the unit tests, on a machine with 4 GB | It was killed at 3.8 GB. Nothing in the engine collects unasked, AddressSanitizer keeps what is freed, and 125 suites add up in one process. After each suite the sanitizer build now collects and returns the allocator's pages: 2.6 GB at the highest |
| Mutated documents (`tools/fuzz/fuzz_xml.py`, written after the use-after-free) | Nothing in the parser: 2,400 mutants and 1,927 whole documents of the W3C suite in the sanitizer build, no report. It did report two things the standard's serializer does on purpose, which are now written down: a declaration of the `xml` prefix is not written, and `innerHTML` writes a child without its surroundings, so `el.innerHTML = el.innerHTML` can move a child that is in no namespace into the default namespace of `el`. jsdom does both |

### What is known to be wrong and is not fixed

- **`fetch()` reads files of this machine for any page.** Found while
  deciding what `XMLHttpRequest` may load. The loader behind `fetch()`
  (`ResourceLoader::load`) takes whatever is not `data:` or `http://` for a
  path: a page at `https://example.test/` that calls
  `fetch('/home/user/notes.txt')` gets the file. Nothing checks an origin.
  It is older than this phase and was not touched; `XMLHttpRequest` was kept
  off that loader, answers `data:` URLs only, and a unit test asserts that a
  request reads no file. This is the first row of the task table now.
- **`XMLHttpRequest` sends nothing over a network.** `http:`, `https:` and
  relative addresses end in `error`.
- **Four encodings.** UTF-8, UTF-16 in both orders, windows-1252. A response
  labelled `shift_jis` is read as if it had no label; an XML document that
  declares `EUC-JP` is not read; a page is read as UTF-8 whatever its
  `<meta charset>` says.
- **A page cannot be XML.** The command line parses every file as HTML, and
  there are no frames with documents: 495 web-platform-tests subtests need
  one or the other and fail.
- **In the JavaScript engine, a function cannot have a function as its
  prototype**: `Object.getPrototypeOf(TypeError) !== Error`. 18 subtests.
  (Fixed in Phase 34, finding 41.)
- **An `Attr` is not a `Node`**, two attributes of one element could not
  share a qualified name in two namespaces, and collections did not list
  their names as own properties: 33 subtests. Fixed in Phase 51 (finding
  57); what is left of that block is that a write to an index of an
  `HTMLCollection` is refused in silence in strict mode (2 subtests).
- **Removing all 50,000 attributes of an element from the front took 9 s**
  at Phase 28; from the back, 22 ms of 25,000. Fixed in Phase 35 (finding 40).
- **The HTML tokenizer searched for a repeated attribute name** by comparing
  it with every accepted one: one element with 50,000 attributes took 2.9 s in
  the HTML parser (10.4 s before the attribute list had an index). Fixed in
  Phase 39 (finding 46): 0.09 s whole-process.

### What the numbers do not say

**1702 of 1702** is every document of the W3C suite that applies to a parser
that does not validate, reads no external entity and follows the Fifth
Edition. 883 documents are not counted, for six reasons
(`tests/xml/README.md`), and all 2,585 are run so that none can crash it.

**4640 of 5192** web-platform-tests subtests: of the 552 that fail, 495 were
never really asked, because the document they are about is loaded into a
frame. They are listed as failing, not as skipped.

**629 of 629** is what jsdom answers, and for `XMLHttpRequest` jsdom is wrong
in ten places that were found (`tests/dom-conformance/README.md`); the
corpus stays away from them. Places that were not found are in the corpus as
jsdom's answer.

**The parser reads hostile text in the process it runs in**, as everything
here does. What bounds it is listed in ARCHITECTURE.md, Phase 28, section 3,
and each bound was added after a page was found that needed it; the list is
of what was thought of. The mutation fuzzer starts from the W3C's documents
and is not guided by coverage.

**How verified.** Every line is a command and what it printed on the
merged tree, in the sandbox.

- `make xml-conformance`: `XML conformance (W3C suite): 1702/1702 (100.0%)
  [261 canonical forms compared, 2585 documents run, 0 crashes, 0
  timeouts]`. With `--reference`: Expat 1404 and libxml2 1697 of the 1718
  they can be asked.
- `make diff-xml`: `4000 documents (seed 20261114, half of them damaged),
  Litebird accepts 2245; 3958 agree with both, 0 DIFFER, 13 where Expat and
  libxml2 disagree (Litebird is with Expat on 12, with libxml2 on 1, with
  neither on 0); 29 with a version number outside the grammar, all refused`,
  and `XML round trip: 3000 trees, 3000 come back the same from Litebird and
  from Expat, 0 do not`.
- `make wpt`: `Web Platform Tests: 4640/5192 subtests pass in 76 files; 552
  fail as listed, 0 fail and are not listed`. The binary of `main` passes
  1,127 of the 2,257 subtests it gets as far as reporting.
- `make dom-conformance`: `629/629` in 23 files, and the same from jsdom
  with `--reference`. The binary of `main` passes 572: none of the 57 new
  cases.
- `make test`: `All 129 engine test suites passed successfully!`, twelve of
  them new.
- `make conformance` 1781 of 1792, `make js-conformance` 840 of 845,
  `make url-conformance` 1150 of 1188, `make diff-trees` 0 of 6,000 trees
  differ: all as on `main`, with the HTML parser now storing foreign
  attributes under their names and namespaces.
- `make deep`: `1332 runs over 465 pages at depth 200000 on a 8 MB stack, 0
  failed; slowest 22.8 s (table [dumps])`; 20 of the pages are XML.
- `make asan`: 129 of 129 suites; JavaScript 839 of 845 as before; DOM 629
  of 629; URL 1150 of 1188; XML 1702 of 1702; web-platform-tests 4640 of
  5192; 1,000 generated documents and 1,000 round trips; `fuzz: 400
  mutations, 0 finding(s)`, `pipeline fuzz: 400 runs … 0 finding(s)`, `fuzz
  xml: 600 mutations of 3074 documents (3061 of the W3C suite), 38 of them
  well formed, 0 finding(s)` and `1927 documents as they are, 1551 of them
  documents to this parser: each the same tree after it is written and
  read`; `882 runs over 315 pages at depth 10000 on a 2 MB stack, 0
  failed`. No report from either sanitizer. It takes 23 minutes here.

---
