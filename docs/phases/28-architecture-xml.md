# Phase 28 Architecture: XML

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 28 Architecture: XML` section.
> The text below is the section as it stood, unchanged.


### 1. Why this phase existed

`new DOMParser().parseFromString(text, 'application/xml')` gave back an HTML
document: `documentElement.nodeName` was `HTML` whatever the text held, a
CDATA section became a comment, and text that was not well formed gave a tree
like any other. There was no XML parser. `XMLHttpRequest` was a stub whose
every request ended in `error`, so `responseXML` could only be null. Phase 27
listed the first under "known to be wrong" (AUDIT.md, finding 31): a wrong
answer, not a missing one, and nothing told the script.

XML is a small language with a hard edge. A parser must refuse what is not
well formed, with no recovery, so every one of its rules can be tested from
outside: a document is accepted or it is not. That made it possible to start
with the references and write the parser against them:

| Reference | What it judges |
| :--- | :--- |
| The W3C XML Conformance Test Suite (2,585 documents) | Accepting and refusing, and the canonical form of what is accepted |
| Expat 2.5.0 and libxml2 2.14.6, on documents nobody wrote | The same, where the suite has no document |
| web-platform-tests, `domparsing/` and `dom/nodes/` (76 files) | The DOM an XML document has, and the serializer |
| jsdom 31.0.0 (57 recorded cases) | The same, and `XMLHttpRequest` |

No two of them agree everywhere, and section 7 says where each is not the
truth.

### 2. The parser (`src/xml/XmlParser.cpp`)

XML 1.0, Fifth Edition, with Namespaces in XML 1.0: what a browser's parser
is. It does not validate, and it reads nothing but the text it was given.

- **It knows nothing of the DOM.** `parse_xml(text, handler, error, options)`
  reports to an `XmlHandler`: `doctype`, `start_element` (the name and the
  attributes with their namespaces resolved), `end_element`, `characters`,
  `cdata_section`, `comment`, `processing_instruction`. One handler builds a
  tree (section 4); two others write the canonical form the conformance
  suite compares and the event dump the differential run compares
  (`XmlTools.cpp`, `--xml-canonical` and `--xml-events` on the command
  line).
- **The internal subset is read, and used.** Entities declared there are
  expanded; an attribute's declared type decides how its value is normalized
  (3.3.3: anything but `CDATA` is trimmed and collapsed) and a declared
  default is added to an element that does not have the attribute, before
  namespaces are resolved, because a default can be an `xmlns`. Element
  types and notations are checked for form and then not used: nothing is
  validated.
- **Nothing outside the document is read.** An external entity, an external
  subset and a parameter entity that points at a file are never fetched: no
  code path opens anything. What the standard asks of a parser that does
  not read them follows from that: declarations that come after a reference
  to an external parameter entity are not used (what was not read could
  have changed them), and in a document that has an external subset or
  refers to a parameter entity, an entity that is not declared is no error
  of form (4.1); it stands for nothing. `standalone="yes"` takes both back.
- **`&nbsp;` in XHTML.** The HTML standard asks a browser to treat eleven
  public identifiers (the XHTML and MathML DTDs) as declaring the entity
  names of HTML. A DOCTYPE with one of them does; any other document has the
  five entities of XML and its own.
- **Errors end the parse.** The first one is reported with its line and
  column, and the handler's tree is thrown away by the caller. There is one
  place where "not well formed" is a judgement: a version number.
  `version="1.7"` is accepted, as the Fifth Edition says; `version="2.0"`
  and `version="1."` are not (the grammar is `'1.' [0-9]+`).
- **The encoding** of bytes is found by `decode_xml()`: a byte order mark,
  then the charset of the transport, then the first bytes and the encoding
  declaration, then UTF-8 (appendix F). It reads UTF-8, UTF-16 in both
  orders and windows-1252 under its names (`ISO-8859-1`, `US-ASCII`); a
  document that declares another encoding is not read at all. `DOMParser`
  never gets here: it is handed a string.

### 3. What a hostile document costs

A browser's XML parser is handed text by a page, so the page chooses the
worst case.

| Document | What bounds it | Time |
| :--- | :--- | ---: |
| Nested 200,000 deep | Nothing recurses: open elements are a vector, the namespace scope is a stack of the declarations made (not a copy a level), an entity inside an entity value is expanded from a vector of sources, and a content model's parentheses are counted | 130 ms |
| Entities that expand to entities (a "billion laughs"), in text or in an attribute value | Expansion is counted. Past 8 MB a document may expand to 100 times its own size and no more; then it is not well formed. The figures are Expat's | refused in 83 ms |
| An entity that contains itself, directly or through 50,000 others | Refused where it is used (4.1, "No Recursion"); the chain without the ring is followed in a loop | 0.1 s |
| One attribute default and 100,000 empty tags that get it | A default is text the document did not write where it appears: one of a megabyte made 100 GB of attribute values from 1.4 MB. Counted with what entities add, under the same limit | refused in 0.1 s |
| One element with 50,000 attributes | A name given twice is found with a set, and the element's own list has an index (section 4) | 40 ms; 7.1 s before the index |
| 50,000 namespace declarations on one element, each used by one child | A prefix is looked up in a map of stacks, not by walking the declarations | 148 ms; over two minutes as first written |
| 50,000 attributes declared for one element in the DOCTYPE | The first declaration of a name counts, so each is looked up: in a set | 0.1 s; 4.5 s as first written |

The attribute and namespace rows were found by pages
`tools/fuzz/deep_pages.py` generates, the two about the DOCTYPE by asking the
same questions of declarations while this section was written. All of them
are pages there now (20 for XML), and the unit suite
`xml_parser_bounds_what_a_hostile_document_costs` has each with a time limit.

### 4. XML in the DOM

**A document knows what it is.** `Document` has a content type and is an HTML
document or it is not; a `DOMParser` document, `createDocument()`,
`new Document()` and a response document are not. In such a document names
keep their case (`createElement('P').nodeName` is `P`), `createElement` makes
an element in no namespace (the XHTML one when the type is
`application/xhtml+xml`), `createCDATASection` works and `document.write`
throws, and `innerHTML` is XML in both directions: assigning text that is
not well formed is a `SyntaxError`, where HTML would make something of it.

**Every element has a namespace, a prefix and a local name**, and so has
every attribute. An element in the XHTML namespace gets its HTML interface
wherever it was made (`<p xmlns="http://www.w3.org/1999/xhtml">` in a feed is
an `HTMLParagraphElement`); one in another namespace, or none, is an
`Element`. Selectors learned the namespace bar (`*|b`, `|b`) and match a type
by local name, case kept outside HTML.

**Two new kinds of node**: `ProcessingInstruction` and `CDATASection` (a
`Text`). Both are in the node bindings, the cloning, the comparisons and both
serializers; the HTML one writes `<?target data>`.

**A document keeps to its shape.** One element, one doctype, the doctype
first: `appendChild`, `insertBefore`, `replaceChild`, `append`, `before` and
their relatives check "ensure pre-insert validity" for a document as the
parent, which they did not, and `document.doctype` is the doctype that is
there now, not the one the parser saw.

**Names are checked by the DOM standard's own rules**, which since 2025 are
not XML's: a name is what the HTML parser could have made. So
`createElement('a<b')` gives an element and `createElement('a b')` throws;
`setAttribute('<', '')` is allowed and `setAttribute('a=b', '')` is not. The
target of a processing instruction is still an XML name. `createElementNS`
and `createDocument` validate and split a qualified name and refuse a prefix
without a namespace, `xml` and `xmlns` with the wrong one.

**Not well formed** gives what browsers give: a document whose root is
`parsererror` in the namespace
`http://www.mozilla.org/newlayout/xml/parsererror.xml`, holding the message.
A script tests for it with `getElementsByTagName('parsererror')`.

**The attribute list** (`include/litebird/dom/AttributeList.hpp`). An
element's attributes are a vector with a logical start offset in the order they were added (Phase 27),
and every lookup was a scan of it. That is right for the handful an element
has and the square of the count for one that has many: 50,000 `setAttribute`
calls on one element took 6.3 s. The list now builds an index the first time
an element with many attributes is searched. The items and the index's
ascending numbers are vectors with a logical start offset; a front erase
advances the offset and compacts only when the dead prefix reaches the live
tail, so end removals are linear and an empty list allocates nothing (Phase
35). A middle removal still moves the tail. Namespaces
are kept beside the list, for the few attributes that have one, with indexes
of their own. The
HTML parser now gives the attributes of inline SVG and MathML their
namespaces ("adjust foreign attributes") and keeps their names:
`getAttribute('xlink:href')` and `getAttributeNS('http://www.w3.org/1999/xlink',
'href')` were both null on every `<use xlink:href>` a page held.

### 5. The serializer (`src/xml/XmlDom.cpp`)

`XMLSerializer.serializeToString()` and `innerHTML` of an XML document are
the algorithm of *DOM Parsing and Serialization*, which decides for every
element and attribute whether its prefix can be written as it is, whether a
declaration must be added, and when a prefix has to be made up (`ns1`). It
is a loop with a stack of scopes, each holding only what its element
declared.

The algorithm as published has faults that are filed against it, and tests
that assert them. Litebird follows the text except where following it writes
a document that reads back as another tree, or as none:

- a made-up `nsN` passes over a prefix that is already declared on the
  element (issue 44: the text writes `xmlns:ns1` twice);
- a prefix that a nearer element has given another namespace is not used for
  the old one (issues 45 and 75);
- an attribute named `xmlns` in no namespace is written as an attribute and
  never taken for a declaration (issue 83).

And it follows the change of 1 September 2026: an attribute keeps its prefix
where it can. `tests/wpt/README.md` has each case, what the test expects and
what Litebird writes.

What decides is not a reading of the text. **A tree that is written and read
again must be the same tree**, and that can be checked with no reference at
all: `tools/fuzz/xml_roundtrip.py` builds trees made to break it (one prefix
for two namespaces on an element and its parent, `xmlns` attributes that
contradict the names beside them, the prefixes the serializer makes up),
writes each, reads the text with `DOMParser` and with Expat, and compares.
With the published algorithm 265 of 3,000 such trees do not come back; with
issue 45 left in, 498.

"The same tree" means every node, and every element and attribute with its
namespace and local name. Two things are not kept, by the standard's choice,
and Litebird and jsdom both follow it there:

- A namespace declaration that says what is so already is not written: one
  for the `xml` prefix, or one that repeats an ancestor's. The names it was
  for are unchanged; the `xmlns:` attribute is gone from the element.
- `innerHTML` writes each child without its surroundings. A child in no
  namespace is written `<c/>`, and when that text is assigned to an element
  with a default namespace the child lands in it. So `el.innerHTML =
  el.innerHTML` is not always the identity in an XML document. Done a second
  time it changes nothing more, and the mutation fuzzer (section 8) checks
  that.

### 6. `XMLHttpRequest`

A request is answered when its address is a `data:` URL, and only then. That
is enough for `responseXML` to exist and be tested, and it is all that could
be done without deciding what a request over a network may reach
(section 10).

What is there follows the standards step by step, and four of them meet in
it:

| Standard | What | Where |
| :--- | :--- | :--- |
| Fetch | The `data:` URL processor: the type before the comma, percent escapes, `;base64` and what counts as base64 | `src/net/DataUrl.cpp` |
| MIME Sniffing | Reading and writing a MIME type; which types are XML (`text/xml`, `application/xml`, anything `+xml`) | the same |
| Encoding | Bytes into text: a byte order mark first, then the label, errors as U+FFFD | the same |
| XMLHttpRequest | `open`, `send`, `abort`, the states and events, `responseType`, `overrideMimeType`, the final MIME type and encoding, the text, document, JSON and blob responses | `src/js/DomWindowBindings.cpp` |

`responseXML` is a document when the final type is an XML one and the bytes
are a well-formed document, and null otherwise; there is no `parsererror`
document here. The document has the response's type and address, and no
script in it runs. An HTML response is a document only when the script asks
for one (`responseType = 'document'`), and is parsed with scripting off, as
`DOMParser` parses. `send()` fires `loadstart` and returns; the response
arrives in a task after that (the states, `progress`, `load`, `loadend`). A
synchronous request has it before `send()` returns, and fires no `loadstart`
and no `progress`.

`DataUrl.cpp` does no input or output: it turns a string into a type and
bytes. The request code calls nothing that opens a file or a socket, and the
unit suite asserts it from outside: a `file:` address, a path, a relative
address that climbs to one, `http:`, `https:`, `blob:`, `about:` and
`javascript:` all end in `error` with status 0 and an empty response. For
the first three the file is there to be read.

### 7. Four references, and where each is not the truth

**The W3C suite** judges 1,702 of its 2,585 documents for this parser. The
rest ask for something a browser's parser does not do (validate, read an
external entity, XML 1.1) or were answered differently before the Fifth
Edition; `tests/xml/README.md` has the count for each reason. All 2,585 are
run, counted or not, so that none can crash the parser. The suite is
downloaded and checked against a pinned SHA-256, not kept in the repository:
the archive has no licence for the whole, and one of its parts forbids
passing it on in any other form than its own.

**Expat and libxml2** read documents `tools/fuzz/diff_xml.py` generates, half
of them damaged in one place. The rule is: what the two read alike, Litebird
must read alike. They disagree with each other on 13 documents in 4,000,
and both are wrong about one thing: Expat does not check a version number
and libxml2 accepts `version="1."`, so a document whose version is outside
the grammar is judged by the grammar. On the W3C suite Expat gives the
expected answer for 1,404 of 1,718 documents and libxml2 for 1,697, which is
why neither is the reference alone.

**web-platform-tests** are the tests browsers share, run with their own
harness (`tests/wpt/`, 76 test files, 5,192 subtests). Five subtests of the
serializer's file assert a text the standard has since changed, or a fault
that is filed against it (section 5). 495 load a document into a frame,
which Litebird does not have. Every failing subtest is listed with a word
for its reason in `tests/wpt/expected-failures.txt`; the runner fails when
one fails that is not listed and when a listed one passes.

**jsdom** recorded 57 cases (`xml.dat`, `xhr.dat`). Its XML serializer
follows the text of before September 2026 and makes up a prefix where an
attribute has one; its `XMLHttpRequest` reads an XML response as UTF-8
whatever the document in it declares, puts U+FFFD where the bytes are not
text and gives a document, where XML says there is none (4.3.3); it calls every
response document `application/xml`, keeps the fragment in `responseURL`,
fires a `progress` event on a synchronous request and one after `abort()`,
and parses an HTML response with scripting on. The corpus stays away from
each of these and the unit suites
`xml_dom_follows_the_standard_where_jsdom_does_not` and
`xml_http_request_answers_data_urls_and_nothing_else` assert the standard;
`tests/dom-conformance/README.md` has the table.

### 8. How this is verified

| Check | Result |
| :--- | :--- |
| W3C XML suite (`make xml-conformance`) | 1702 / 1702 counted, 261 canonical forms the same, 2,585 documents run, 0 crashes, 0 timeouts. `main` has no XML parser |
| Generated documents against Expat and libxml2 (`make diff-xml`) | 4,000 documents, half of them damaged: 3,958 read as both read them, 13 where the two disagree, 29 with a version number outside the grammar and all refused, 0 differ |
| Round trip (`make diff-xml`) | 3,000 trees written by `XMLSerializer`: 3,000 come back the same from Litebird and from Expat |
| web-platform-tests (`make wpt`) | 4640 / 5192 subtests in 76 files; the 552 others are listed with a reason, 495 of them frames. `main` passes 1,127 of the 2,257 it gets as far as reporting |
| DOM corpus (`make dom-conformance`) | 629 / 629 in 23 files: the 572 of Phase 27, 38 in `xml.dat`, 19 in `xhr.dat`. `main` passes none of the 57 |
| The corpus in jsdom (`--reference`) | 629 / 629 |
| Unit tests (`make test`) | 129 / 129; twelve are new (the parser, what a hostile document costs, XML documents in the DOM, the serializer, `innerHTML` in XML, names, the shape of a document, foreign attributes, where jsdom is not the standard, selectors with namespaces, the attribute list, `XMLHttpRequest`) |
| html5lib tree construction (`make conformance`) | 1781 / 1792, as before |
| `make diff-trees` | 4 × 1,500 random pages against parse5, 0 differ, as before: the HTML parser now stores foreign attributes with their namespaces |
| JavaScript corpus, URL data | 840 / 845 and 1150 / 1188, as before |
| `make deep` | 1,332 runs over 465 pages at 200,000 levels, 0 failed; 20 of the pages are XML, the slowest of those 1.6 s. The slowest of all takes 22.8 s of the 60 allowed (a table 200,000 deep, which this phase did not touch) |
| `make asan` (ASan + UBSan): unit tests, the corpora, the XML suite, web-platform-tests, generated documents, the fuzzers, the deep pages at 10,000 levels | 129 / 129; JavaScript 839 / 845, as before; DOM 629 / 629; URL 1150 / 1188; XML 1702 / 1702; web-platform-tests 4640 / 5192; 1,000 generated documents and 1,000 round trips, none differ; 400 + 400 fuzz runs, 0 findings; 882 runs over 315 deep pages, 0 failed. No report from either sanitizer |
| Mutated documents (`tools/fuzz/fuzz_xml.py`, in `make fuzz` and so in `make asan`) | 600 mutants of the W3C suite's documents, 38 of them still documents, and 1,927 of its documents as they are, 1,551 of them documents to this parser: no signal, no report, no timeout, and each document the same tree after it is written and read. Three other seeds by hand, 1,800 mutants: the same |

CI has a job for the XML suite, the differential run and the
web-platform-tests, and the sanitizer job runs all three in the sanitizer
build.

**What the sanitizer build found.** The first commit of this phase that CI
ran failed there, and only there. `XMLHttpRequest`'s "get a final encoding"
read the charset through `response_mime(state).parameter("charset")`: a
pointer into a `MimeType` that was a temporary, used one statement after the
temporary was gone. Every request for a type with a charset read freed
memory; the normal build passed every suite and every case, because the
bytes were still there. The fix is a name for the temporary, and the rvalue
overload of `MimeType::parameter()` is deleted, so the same line no longer
compiles.

Two things followed. The sandbox the checks are run in could not show the
failure, because the unit tests of the sanitizer build were killed there for
lack of memory: nothing in the engine collects unasked, AddressSanitizer
keeps what is freed, and 125 suites in one process had reached 3.8 GB of
the machine's 3.9. After each suite the sanitizer build now collects and
hands the allocator's pages back (`give_memory_back()` in
`tests/test_suite.cpp`); the run peaks at 2.6 GB and takes two minutes. And
a parser of hostile text that had only ever been given documents somebody
understood now has a mutation fuzzer: `fuzz_xml.py` takes the W3C suite's
documents, changes bytes, cuts them short and pastes over them, and gives
each to the parser as bytes and to `XMLHttpRequest`, `DOMParser`,
`XMLSerializer` and `innerHTML` through a page.

### 9. What it costs

Release build, on the machine the checks ran on; "before" is `main`
(`db705e6`) built and run there the same hour.

| | Before | Now |
| :--- | ---: | ---: |
| `setAttribute` 50,000 times on one element | 6.3 s | 49 ms |
| `setAttributeNS` 20,000 times on one element | 0.7 s | 41 ms |
| `removeAttribute` of 25,000 of 50,000, from the back | 2.5 s | 22 ms |
| `removeAttribute` of all 50,000, from the front | 8.2 s | 9.0 s at Phase 28: each one moved the rest of a vector; fixed in Phase 35 (0.09 s whole-process) |
| The HTML parser, one element with 50,000 attributes | 10.4 s | 2.9 s at Phase 28: the tokenizer compared every name with every accepted one; fixed in Phase 39 (0.09 s whole-process) |
| An XML document 200,000 deep: parsed, serialized | | 130 ms, 37 ms |
| An XML element with 50,000 attributes: parsed, serialized | | 40 ms, 4 ms |
| The unit tests in the sanitizer build, highest memory | killed at 3.8 GB (this branch before `give_memory_back()`) | 2.6 GB |

### 10. What is still open here

- **`XMLHttpRequest` answers `data:` only.** `http:`, `https:`, `file:` and
  relative addresses end in `error`. `fetch()` is older code with a loader
  of its own (`src/net/ResourceLoader.cpp`), and that loader takes whatever
  is not `data:` or `http://` for a path on this machine: a page at
  `https://example.test/` that calls `fetch('/home/user/notes.txt')` gets
  the file. Requests were not put on that loader, and the hole is a row in
  AGENTS.md.
- **Four encodings**: UTF-8, UTF-16 in both orders, windows-1252. A label
  for any other is taken for no label; an XML document that declares another
  is not read. A page is read as UTF-8 whatever it declares, `<meta
  charset>` is not looked for (in a page or in an HTML response), and
  `document.characterSet` is `UTF-8` for every document.
- **The command line loads every file as HTML.** XML documents exist
  through `DOMParser`, `createDocument()` and `XMLHttpRequest`, not by
  opening `feed.xml` or `drawing.svg`. That, and frames with documents in
  them, is what 495 of the 552 failing web-platform-tests subtests need.
- **A refused write is refused in silence.** Writing to an index of an
  `HTMLCollection` that is out of range, or to any computed property, is
  ignored where a browser throws a `TypeError` in strict mode (2 subtests).
  The write path is `JsValue::set_property` and `JsEval.cpp`, which is
  claimed by another change.
- **An `Attr` is not a `Node`** was true to Phase 50: `attr.cloneNode()`,
  `importNode(attr)` and an `Attr` adopted with its element all work since
  Phase 51, and so do two attributes of one element with one qualified name
  in different namespaces and the named own properties of a `NamedNodeMap`
  and an `HTMLCollection`.
- **A function cannot have a function as its prototype** in the JavaScript
  engine: `Object.getPrototypeOf(TypeError) !== Error`, and for a class,
  `Object.getPrototypeOf(Derived) !== Base`. 18 subtests fail on it, each an
  `assert_throws_js`. Not a DOM fault. (Fixed in Phase 34.)
- **Removing attributes from the front** of a very long list was quadratic
  at Phase 28; fixed in Phase 35 (vectors with a logical start offset).
- **`responseType = 'arraybuffer'`** gives null: the engine has no
  `ArrayBuffer`.
- **SVG elements still have no SVG DOM** (`className.baseVal`, `viewBox`),
  in an XML document as in an HTML one.
- **The HTML parser does not make processing instructions yet.** The
  standard now does and the node type is here; the tokenizer and the tree
  builder are not changed.
- **Not planned, as in browsers:** XML 1.1, validation, external entities.
  **Absent:** XPath (`document.evaluate`), XSLT.

---
