# 🦅 Litebird Browser Engine

> A lightweight, modular browser and JavaScript engine built from scratch in **C++20**.
> Designed for learning, experimentation, and building custom web engines without massive legacy dependencies.

---

## 📊 Conformance scoreboard

Phase 15 replaced the hand-rolled HTML parser with the WHATWG tree-construction
algorithm and added a way to *measure* the engine instead of eyeballing it.
Phase 17 then spent the scoreboard: it drove the number from 88.0% to 98.8% by
fixing what the corpus actually disagreed with, and audited the harness itself
(see "What the scoreboard measured wrong" below). Phase 18 moved back to
*layout* -- percentage lengths and `overflow` clipping -- which the parser
scoreboard cannot see at all. Phase 26 compared the tree builder with another
parser on pages nobody wrote, which found what the corpus has no case for
(`<br/>` closed the paragraph it stood in) and took the number to 99.4% on
the way (see "Pages nobody wrote"). Phase 27 did for the DOM a script sees
what Phase 23 did for the language: a corpus recorded from another
implementation first, then new bindings written against it (see "The DOM
scoreboard"). Phase 28 wrote the XML parser the DOM lacked, against the W3C's
suite and two other parsers, and brought in the tests browsers share (see
"XML"). Phase 49 implemented customizable `<select>` content and
`<selectedcontent>` cloning, taking the html5lib tree-construction suite from
99.4% to **100.0% (`1792 / 1792`)**.

| Metric | Value | How it is measured |
| :--- | :--- | :--- |
| **DOM tree-construction conformance** | **1792 / 1792 (100.0%)** | html5lib/WPT derivation suite run by `make conformance` (Phase 49 solved the last 11 `<select>` cases) |
| Trees for random pages | 6,000 of 6,000 equal to the ones parse5 builds once nine rules of parse5 are corrected to the standard; 5,951 equal to parse5 as published | `make diff-trees` (needs Node.js) |
| **JavaScript corpus** | **974 / 989 (98.5%)**; ten of the 15 that fail were recorded in Phases 34 and 43 for what classes still lack | `tests/js-conformance`, expected output recorded from Node.js, run by `make js-conformance` |
| **DOM corpus** (what a script sees) | **669 / 669**; 61 of 572 before Phase 27 | `tests/dom-conformance`, expected output recorded from jsdom, run by `make dom-conformance` |
| **web-platform-tests** (`dom/nodes`, `domparsing`, `encoding`) | **7968 / 8009 subtests** across 103 files; the 41 others listed with a reason (0 `frames` failures since Phase 42). 1,127 before Phase 28, 5421 before Phase 47 | `tests/wpt`, the tests browsers share, with their own harness, run by `make wpt` |
| **XML parser** | **1718 / 1718** of the W3C suite's documents that apply; 2,585 run (1702 until Phase 47 read the last 16 encodings) | downloaded by `make xml-conformance` |
| **Legacy encodings** | **40 encodings, 228 labels, every byte of every index table** (including the Big5 two-code-point entries and the `gb18030` ranges), generated from `whatwg/encoding` `@ be17ac5c49881336538acfc7b40dba3c8009144b` | `include/litebird/net/TextEncoding.hpp`, `make test`; pages in these encodings run by `make wpt` |
| XML against Expat and libxml2 | 4,000 generated documents, none read otherwise than both read it; 3,000 serialized trees, all read back the same | `make diff-xml` (needs `pip install lxml`) |
| **URL parsing and setters** | **1188 / 1188 (100%)**; 2 before Phase 27 | `tests/url`, the web-platform-tests data, run by `make url-conformance` |
| Character/entity table | 2231 named references (complete) | generated from the WHATWG table |
| Engine unit tests | 193 suites, all passing | `make test` |
| Worker MIME/module & lifecycle | 17 end-to-end checks (HTTP/data MIME policy, `{type:"module"}` refusal, `importScripts` boundary, `addEventListener("error")`, 60/60 4-worker exit runs) | `make security-worker` |
| CSS Color 4/5 oracle & fuzzing | 180/180 15-space conversions vs exact-rational Python W3C §17 oracle (`max err 5.04e-08`), 13/13 §13.2 OKLCH gamut map, 133/133 valid & 61/61 invalid WPT vectors (4,860/4,860 live upstream WPT `color-computed-*.html`), 5,008 adversarial fuzz inputs (0 crashes) | `make verify-color`, `make fuzz-color` |
| Media & vector oracle | 21 JPEG variants vs libjpeg (52–99 dB PSNR), 9 GIF variants (bit-exact `max_diff=0` vs ImageMagick `-coalesce`), 5 SVG suites; 1,800+ mutated fuzz cases (0 findings) | `make verify-media`, `make fuzz-media` |
| Fuzz robustness | 400 parser mutations, 400 layout/render/image runs, 600 mutated XML documents, 1,927 whole XML roundtrips, and 1,800 mutated JPEG/GIF/SVG inputs: 0 findings | `make asan` (ASan + UBSan) |
| Deep and oversized pages | 165 pages that nest or repeat one thing up to 200,000 times + 300 random pages at the parser's nesting limit: 1,332 runs, 0 failed | `make deep` (8 MB stack, 2 GB, 60 s a run) |

```bash
make conformance     # HTML: per-file pass rate + failing-case diffs
make js-conformance  # JavaScript: per-file pass rate
make test262         # 21 directories of the standard's own suite, downloaded; for comparing two builds, not a score
make dom-conformance # the DOM a script sees: per-file pass rate
make url-conformance # URL parsing and setters (web-platform-tests data)
make xml-conformance # the XML parser: the W3C suite, downloaded the first time
make wpt             # web-platform-tests for dom/nodes and domparsing, with testharness.js
make diff-xml        # generated XML read here, by Expat and by libxml2; serialized trees read back
make test            # 193 engine suites
make bench-floats    # indexed sized-float timing guard: 1k and 10k, mixed sides/clear
python3 tools/bench/bench_sized_floats.py build/litebird --out build/floats-after --sizes 1000 3000 10000
# Compare against the pre-change binary as described in ARCHITECTURE.md Phase 48.
make verify-color    # CSS Color 4/5 15-space rational oracle, §13.2 gamut map, and WPT vectors
make fuzz-color      # adversarial grammar & mutation fuzzing for CSS Color 4/5
make verify-media    # JPEG vs libjpeg, GIF vs ImageMagick -coalesce, and SVG conformance
make fuzz-media      # mutation fuzzing for JPEG, GIF, and SVG decoders/renderer
make deep            # pages nested 200,000 deep, oversized bitmaps, very long runs of one thing
make asan            # sanitizer build, tests, the JavaScript, DOM, URL and XML corpora, web-platform-tests, a fuzz run and the deep pages
make tsan            # the string type's stress tests under ThreadSanitizer, and under ASan with leak checking
make bench           # string-heavy scripts timed against Node; fails if one is wrong or takes over 3 s
make bench-css       # CSS whitespace runs: same paint as compact pages, each under 3 s
make bench-attributes # attribute-list end removals: expected DOM checksums, each under 3 s
make bench-tokenizer-attributes # the HTML tokenizer at 50,000 attributes: expected trees, each under 1 s
make bench-surrogates # six surrogate-pair scripts (including reads and substring slices between halves): results face Node, each under 1 s
make verify-promise-iterators # 34 async cases: each iterator must be closed promptly on a thrown resolve/then
make security-fetch  # page fetch() cannot read arbitrary local files; file: documents keep local access
make security-worker # Worker MIME/module policy, EventTarget dispatch, importScripts boundary, and 60x startup/exit stability
make diff-strings    # 50,580 string results compared with Node's; fails if more than the known 95 differ
make diff-trees      # 6,000 random pages and fragments parsed here and by parse5; fails if a tree differs
```

### The JavaScript scoreboard

`tests/js-conformance/` holds 989 small scripts. Each one records what Node.js
prints for it, and a case passes when `litebird --run-js` prints the same and
exits 0. CI re-runs the whole corpus under Node first, so an expectation
cannot be edited to match what Litebird does.

Read the number for what it is:

- **It is not test262.** The corpus was written alongside the engine. It
  covers the language and library features ordinary scripts use (syntax,
  closures, classes, destructuring, generators, promises and async functions,
  strings, arrays, regular expressions, dates, JSON), not every clause of the
  specification. The official suite scores lower: of the 47,967 tests in
  its `built-ins`, `language` and `annexB` directories that are not modules,
  27,872 pass (58.1%; 26,012 / 54.2% before Phase 44); of the 15,686
  on functions, classes and objects, 12,316 pass (78.5%; 10,835 / 69.1%
  before Phase 44). `make test262` runs the latter. It is used for what a
  change moved between two builds, not as a score (Phase 34: 108 more pass
  and none fewer in those directories; Phase 44: 1,440 more pass and none
  fewer in those 21 directories, plus 374/374 = 100.0% in
  `language/{identifiers,keywords,future-reserved-words,reserved-words}`;
  Phase 43: 43 more in the whole suite and none fewer).
- **Five of the 15 failures are whole features that do not exist**: BigInt
  (2 cases), `Proxy`, typed arrays, and holes in arrays. The other ten were
  recorded to fail. Eight are in `prototypes.dat`: what classes still lack
  after Phase 34 (`Symbol.species`, `Symbol.hasInstance`, and six smaller
  things; ARCHITECTURE.md, Phase 34, section 9). Two are in `construct.dat`:
  `this` used before `super()` has run, and `eval` inside a function, which
  sees nothing of the function (Phase 43, section 8).
- **It measures the language, not the DOM.** The bindings a page script calls
  (`document`, elements, events) are a much thinner layer; see "Known gaps" in
  `ARCHITECTURE.md`.

The engine as it was before Phase 23 passes 140 of the 801 cases the corpus
had when it was replaced, and 25 of the 90 cases in the fixed "everyday" probe
(`everyday-es5.dat` and `everyday-es2015.dat`), which now passes 90/90.

Four other checks sit behind the number. A differential run evaluates 67,545
generated expressions in Litebird and in Node and compares the results: 99
(0.15%) differ, mostly the last digit of `Math.atan2`, `cbrt` and `pow`.
`make diff-strings` does the same for strings (36 subjects through every
string method and 51 regular expressions): 95 of 50,580 results differ, all
of them Unicode data the engine does not have (collation, normalization, some
case mappings, part of `\p{L}`).
63 of the corpus cases (`deep-nesting.dat`) hand the engine input nested
200,000 deep, which used to end the process and must now end in an error the
script can catch. And 34 (`long-strings.dat`) loop over, build or take apart
strings of 100,000 characters and more, work that used to grow with the
square of the length: reading a string copied it. The engine before that fix
passes 12 of the 34 at best and times out on 15 (AUDIT.md, finding 25).

76 cases (`prototypes.dat`, Phase 34) are about a function that stands in a
prototype chain: the parent of a class, the prototype of an object,
`Function.prototype` itself. Three of them are whole programs as Babel and
TypeScript compile classes for older browsers, which stopped at their first
class before. 68 of the 76 pass; the engine before Phase 34 passes 7.

49 cases (`construct.dat`, Phase 43) are about what a constructor makes:
`new.target` in a parent's constructor and under `Reflect.construct`, an
object a parent's constructor returns, when `this` is bound, and three more
programs as Babel and TypeScript compile classes. 47 of the 49 pass; the
engine before Phase 43 passes 13.

Every case in the 1,792-case html5lib tree-construction suite passes (**1792 / 1792**);
`python3 tools/conformance/run_tree_construction.py --filter NAMEFRAGMENT --show 5`
inspects any subset. The final 11 cases (fixed in Phase 49) were all `<select>`:
treating `<select>` as a default scope boundary, keeping arbitrary HTML/SVG/MathML
content inside `<select>` with formatting-element reconstruction, and cloning the
selected `<option>`'s children into `<selectedcontent>`.

### The DOM scoreboard

`tests/dom-conformance/` holds 669 cases: a page, a script, and what the
script prints in [jsdom](https://github.com/jsdom/jsdom) 31.0.0, an
independent implementation of the DOM and HTML standards. A case passes when
`litebird --run-script` prints the same and exits 0. CI runs the whole corpus
in jsdom first (`--reference`), so an expectation cannot be edited to match
what Litebird does.

```bash
make dom-conformance
python3 tools/conformance/run_dom_conformance.py --show 20 --filter forms
python3 tools/conformance/compare_with_jsdom.py page.html script.js   # any page, any script: where do the two differ?
```

| | Passed |
| :--- | ---: |
| Before Phase 27 (`main` at `6f08a70`) | 61 / 572; the everyday probe 13 / 68 |
| Phase 27 | 572 / 572; the everyday probe 68 / 68 |
| Phase 28, with 57 more cases (`xml.dat`, `xhr.dat`) | 629 / 629 |
| Phase 34, with 11 cases (`interfaces.dat`: an interface object has its parent as its prototype) | 640 / 640 |
| Phase 37, with 4 more cases (`forms.dat` date/time inputs, `style.dat` CSSOM `el.style` shorthands) | 644 / 644 |
| Now, with the 25 cases of Phase 43 (`compiled.dat`: custom elements compiled for older browsers, made and upgraded) | 669 / 669 |

`everyday.dat` is the fixed probe: 68 snippets adapted from
[You Might Not Need jQuery](https://github.com/HubSpot/YouMightNotNeedjQuery),
which is what plain DOM code on real pages looks like. It was written before
any of the new code and is never edited. The 20 other files of Phase 27 go through the
DOM by area: nodes, elements, attributes, `classList`, `dataset`, collections,
selectors, markup (`innerHTML` and friends), text, style, ranges, events,
forms, mutation observers, custom elements and shadow trees, the document,
the window, loading, errors, and 16 small programs (a to-do list, a router on
the hash, a form with validation).

The bindings were rewritten for it (`src/js/Dom*Bindings.cpp`, 13,700 lines):
every DOM object has its interface's prototype, so `el instanceof HTMLElement`,
`Object.getPrototypeOf(document.body) === HTMLBodyElement.prototype`,
`class MyEl extends HTMLElement` and patching `Element.prototype` all work;
a node has one object for as long as it lives; collections are live; selectors
are the real grammar; and errors are the `DOMException`s the standards name.
A page's scripts run while it is parsed, so a script sees the page up to
itself. ARCHITECTURE.md, Phase 27, has the design.

Four things about the number:

- **It was 100% before it was right.** At 557 of 557, `getComputedStyle`
  answered `""` for every property nobody had declared and took the last
  rule that matched whatever its specificity, and a `Range` did not follow
  changes to the tree. No case had asked: the corpus was written by the
  hand that wrote the code. Both were found by running pages that were not
  in the corpus through jsdom and Litebird side by side
  (`compare_with_jsdom.py`), and became fifteen cases. A full score says
  that nothing is known to be wrong among what was asked.
- **It is 100% of what the corpus asks, not of the DOM.** jsdom does no layout
  and loads no other page, so nothing here checks `getComputedStyle(el).width`
  layout resolution, scrolling or navigation. What is known to be
  missing or wrong is listed in ARCHITECTURE.md (Phase 27, section 12,
  Phase 28, section 10, and Phase 37): `sheet.cssRules`, `document.write`,
  the SVG DOM (`className.baseVal`, `viewBox`, `getBBox()`), and more.
- **jsdom is the reference, not the authority.** Where it differs from the
  standards, the corpus stays away and a unit test asserts the standard's
  answer (`tests/dom-conformance/README.md` names the places).
- **URLs are measured separately**, with the data browsers use:
  `tests/url/` is `urltestdata.json` and `setters_tests.json` from
  web-platform-tests, 1,188 cases, all of which pass. Host names with
  characters outside ASCII go through Unicode's UTS #46 (`make url-conformance`
  enforces 1,188); `tests/url/README.md` says exactly what is checked.

### XML

Until Phase 28 `new DOMParser().parseFromString(text, 'application/xml')`
gave an HTML document. There was no XML parser: a CDATA section became a
comment, text that was not well formed gave a tree like any other, and
`XMLHttpRequest` failed every request, so `responseXML` was always null.

| A script on a page | Before | Now |
| :--- | :--- | :--- |
| `d = parseFromString('<a xmlns="urn:x"><b id="k">t</b><![CDATA[<c>]]></a>', 'application/xml')`, then `d.documentElement.nodeName`, `.namespaceURI`, `d.contentType` | `HTML`, `http://www.w3.org/1999/xhtml`, `text/html` | `a`, `urn:x`, `application/xml` |
| `new XMLSerializer().serializeToString(d)` | `<html xmlns="http://www.w3.org/1999/xhtml"><head /><body><a xmlns="urn:x"><b id="k">t</b><!--[CDATA[<c-->]]&gt;</a></body></html>` | The text that went in |
| `parseFromString('<a><b></a>', 'application/xml').documentElement.nodeName` | `HTML` | `parsererror` |
| `x.open('GET', 'data:text/xml,<a><b>t</b></a>'); x.send()` | `error`, `loadend`; status 0; `responseXML` is null | `loadstart`, `load`, `loadend`; status 200; `responseXML.querySelector('b').textContent` is `t` |

The parser (`src/xml/XmlParser.cpp`) is XML 1.0, Fifth Edition, with
namespaces, and it is what a browser's is: it does not validate, it reads
nothing outside the text it is given, and it bounds what entities and
attribute defaults may expand to. On top of it: XML documents in the DOM
(namespaces on elements and attributes, CDATA sections, processing
instructions, the `parsererror` document),
the standard's XML serializer for `XMLSerializer` and for `innerHTML` in an
XML document, and `XMLHttpRequest` for `data:` URLs.

It was written against references, and each has a command:

| Reference | Result | Command |
| :--- | :--- | :--- |
| The W3C XML Conformance Test Suite | 1718 / 1718 of the documents that apply to such a parser; all 2,585 are run, none crashes it | `make xml-conformance` |
| Expat 2.5.0 and libxml2 2.14.6, on 4,000 generated documents, half of them damaged | 3,958 read as both read them, 13 on which the two disagree, 29 with a version number outside the grammar (refused), 0 that differ | `make diff-xml` |
| Itself: 3,000 trees made to break a serializer, written and read back | 3,000 come back the same tree, read by Litebird and by Expat | `make diff-xml` |
| web-platform-tests, `dom/nodes`, `domparsing` and `encoding` (103 files, with `testharness.js`) | 7968 / 8009 subtests; each of the 41 others is listed with a reason | `make wpt` |
| jsdom 31.0.0 | 57 recorded cases, all pass | `make dom-conformance` |
| Nothing: the W3C suite's documents with bytes changed, cut short and pasted over, in the sanitizer build | 600 mutants and 1,927 documents as they are: no crash, no sanitizer report, and every document the same tree after it is written and read | `make asan` (`tools/fuzz/fuzz_xml.py`) |

```bash
python3 tools/conformance/run_xml_conformance.py --show 20      # the failures, if there were any
python3 tools/conformance/run_xml_conformance.py --reference    # Expat and libxml2 on the same suite: 1,404 and 1,697 of 1,718
python3 tools/conformance/run_wpt.py --filter XMLSerializer --show 20
python3 tools/fuzz/diff_xml.py --count 20000 --seed 1
./build/litebird --xml-canonical file.xml                       # exit 0 well formed, 1 not, 3 an encoding it cannot read
```

Five things about these numbers:

- **1,718 is what applies, and the rest is said.** Not counted: 310
  documents whose answer changed with the Fifth Edition, 274 for XML 1.1, 247
  that need an external entity read, 27 where a parser may report an error
  or not, 9 that are not namespace-well-formed on purpose
  (`tests/xml/README.md`). The 16 that were skipped until Phase 47 for their
  encoding are read now, and each is still answered as the suite says. The suite is
  downloaded and checked against a pinned SHA-256; it is not in the
  repository, because its licence does not allow passing it on in another
  form.
- **No reference is the truth everywhere.** Expat does not check a version
  number and libxml2 accepts `version="1."`. Five web-platform-tests
  subtests assert a text of the serializer's standard that has since
  changed, or a fault filed against it, and following them writes documents
  that read back as another tree (265 of the 3,000 above, with one of
  them). jsdom reads an XML response as UTF-8 whatever the document in it
  declares. Where two references
  disagree the standard was read, and the documents say which way it went
  (`tests/wpt/README.md`, `tests/dom-conformance/README.md`).
- **41 web-platform-tests subtests fail** across 103 files (down from 534 once
  Phase 42 added nested frame documents and XML/XHTML frame loading, resolving
  all 495 `frames` failures, and from 42 once Phase 47 read every legacy
  encoding the `encoding/` tests write in): 33 fail on what an `Attr` and a
  `NamedNodeMap` are here, 1 on an upstream WPT `.xhtml` test checking the
  parent frame's `DOMException` constructor (`innerhtml-05.xhtml`), and 7 on
  serializer edge cases documented above (`tests/wpt/README.md`).
  (18 more failed on a fault of the JavaScript engine, a function that could
  not be a prototype, until Phase 34.)
- **`XMLHttpRequest` answers `data:` URLs and nothing else.** No request
  leaves the process or reads a file, and a unit test asserts it. Requests
  over the network are open, and so is something found on the way:
  `fetch()`, which is older code, reads files of this machine for any page
  that asks (`AGENTS.md`).
- **The normal build passed everything while reading freed memory.** The
  first commit CI ran failed in the sanitizer job and nowhere else:
  `XMLHttpRequest` read the charset of a response's type through a pointer
  into a temporary. All unit suites and all 629 cases had passed. It is
  fixed, the line no longer compiles, and the mutation fuzzer in the table
  was written because of it. `make asan` is not the slow twin of
  `make test`.

ARCHITECTURE.md, Phase 28, has the design, what a hostile document costs,
and what is still open.

### Pages nobody wrote

The corpus is 1,792 inputs somebody thought of. `make diff-trees`
(`tools/conformance/diff_trees.py`) makes pages nobody did, a few dozen tags
in random order and most of them misnested, and compares the tree Litebird
builds with the one parse5 builds, an independent parser written from the
same standard. A failing corpus case says a bug exists. A random page says
how often a page meets it, and `--reduce` cuts the page down to a few tags
to read the standard against.

The first run of it, in Phase 25, found five bugs in 6,000 pages. Phase 26
fixed those and then widened what the pages contain, and the wider pages
found more than the first ones had:

| What the pages contain | Trees equal before Phase 26 | Now |
| :--- | ---: | ---: |
| Tags, text and comments | 1,489 of 1,500 | 1,500 |
| The same with what else the tree builder reads: `<br/>`, `type=hidden`, CDATA, U+0000, a stray doctype | 1,371 | 1,500 |
| Those parsed as fragments, which is what `innerHTML` does, in 45 kinds of element | 1,371 | 1,500 |
| Those fragments with scripting disabled | 1,361 | 1,500 |

The worst of what they found is not in the corpus at all, because no case
there writes it: **a void element written the XHTML way closed its parent.**
In `<p>a<br/>b</p>` the "b" landed outside the paragraph, and a
`<link .../>` in the head put the rest of the head, and the body, outside
`<html>`. One line was wrong, and 292 of the 408 wrong pages in the table
come right when only that line is changed. One of the example pages in this
repository was parsed wrongly for that reason, and so was any markup read
back from `innerHTML` and assigned again, because Litebird itself writes
`<br />`.

The fragment rows led to the other thing a page would notice. The parser
was right about fragments for a `<tr>` or an `<svg>`, and `innerHTML` never
asked it: a script's `element.innerHTML = ...` was parsed as the content of
a `<body>` whatever the element was. `table.innerHTML =
"<tr><td>1</td><td>2</td></tr>"` left the text "12" in the table and no
row. It is parsed for its element now. The rest is in AUDIT.md, findings 29
and 30.

Two things about the numbers. The comparison is with a copy of parse5 in
which nine rules are corrected, because parse5 8.1 is itself behind the
standard there (a form inside a `<template>`, CDATA in an SVG `<title>` and
seven more; `diff_trees.py` lists them with the correction, a few lines
each). Against parse5 as published 5,951 of the 6,000 trees are equal and
the other 49 differ for those nine reasons. For four of the nine the
web-platform-tests copy of the corpus has cases that say the same, for two
more Blink's source does, and for the last three there is only the
standard's text (AUDIT.md, finding 30). And `<select>` is left out of the
pages: parse5 has the older rules for it, and so does Litebird.

The check needs Node.js and, the first time, the npm registry to fetch
parse5. CI runs it in the conformance job and fails if a tree differs.

### Pages that are hostile by size

A conformance corpus is made of small pages, and so is what a mutation fuzzer
starts from. Neither finds the oldest way to end a browser from a web page:
nest something deeper than the stack is long, or repeat it until work that
grows with the square of its count never finishes. A file of 300,000 nested
`<div>` (3.3 MB, no script) used to end Litebird with a `SIGSEGV`.

`make deep` generates those pages (`tools/fuzz/deep_pages.py`) and runs each
in a child process with an 8 MB stack, 2 GB of memory and 60 seconds:

| | Before Phase 25 | Now |
| :--- | :--- | :--- |
| Runs that fail, of 392 over 145 pages | 264 | 0 |
| 300,000 nested `<div>` | `SIGSEGV` | renders in 4 s |
| A script that nests 200,000 elements with `appendChild`, reads `textContent` and `innerHTML`, and drops the tree | `SIGSEGV` at 20,000 | 0.7 s |
| One paragraph of 100,000 images | 35 s | 1.3 s |
| `<svg width="100000" height="100000">` | abort (`std::bad_alloc`) | blank box |

Three rules do it. The HTML parser attaches nothing deeper than 512 levels,
the limit Blink and WebKit use: an element that would be deeper becomes a
sibling instead, and none is dropped. A tree built by script may be as deep as
the script likes, so nothing that walks the DOM recurses. Layout and painting
still recurse and stop when the stack is nearly used up. What this does not
cover is listed in AUDIT.md, finding 28: among others, 10,000 floats with a
size in one box still take 46 s, and a block inside an inline box is not laid
out.

### What the scoreboard measured wrong

Phase 17 also fixed three harness bugs that were *mis-scoring the engine*, and
they are worth stating because they change what the number means:

1. **Case splitting.** The runner split `.dat` files on blank lines, but a case
   legitimately contains blank lines (`<pre>` data does). Cases whose data
   contained one were truncated and their expected trees came out empty — a
   guaranteed fail. Cases now split where the corpus format says they do: at a
   `#data` line that starts a case.
2. **Lone CR.** Reading the corpus in text mode turned a lone `\r` into `\n`, so
   a case asserting that a `&#x0D;` reference survives could never pass.
3. **Trailing newline.** The expected tree kept a trailing blank line, which made
   comparisons fail for cosmetic reasons.

Fixing these moved the denominator from 1790 to 1792 cases and was worth ~55
cases on its own. The gain is *measurement honesty*, not new engine code.

The corpus is the same tree-construction material WebKit, Blink, Gecko and
Ladybird use as their parser baseline, so the number is comparable across
engines rather than self-referential. It is vendored under
`tests/html5lib/tree-construction/` (BSD-licensed, see the LICENSE there).

The vendored copy is the last one html5lib-tests published: in June 2026 its
tree-construction tests moved into web-platform-tests, and that copy has
grown to 1,973 cases since. Litebird passed 1,868 of them before Phase 49
implemented the `<select>` / `<selectedcontent>` rules (leaving 88 that need
processing instructions (`<?x?>` is a node of its own in the standard now, and a
comment here) and 6 that need a script to run while the page is being parsed).
That copy is not vendored yet, so `make conformance` and CI still score the
1,792.

> **Honest scope:** 100.0% on the vendored parser conformance corpus means the DOM this engine builds
> matches a real browser's across those 1,792 cases. It does **not** mean it renders real pages:
> floats and tables lay out with spans and collapsed borders (Phases 16 and 29),
> percentages resolve (Phase 18) and `overflow` clips (Phase 18), but there is
> no bidi, external stylesheets and scripts are not loaded, and the
> JS engine is a tree-walking interpreter without a JIT.
> See "Known gaps" in `ARCHITECTURE.md`.

---

## 🌟 Features & Architecture

Litebird implements the complete modern browser rendering pipeline:

```text
[Raw HTML] ───► HTML Tokenizer ───► HTML Parser ───► DOM Tree
                                                         │
[Raw CSS]  ───► CSS Tokenizer  ───► CSS Parser  ───► Style Rules (@media, @keyframes)
                                                         │
                                                         ▼
                                                 Style Resolver (Cascade, Viewport & Pseudo-Classes)
                                                         │
                                                         ▼
                                                 Layout Box Tree
                                                 (Block / Flexbox / Grid / IFC / Canvas / Image / SVG)
                                                         │
                                                         ▼
                                                 Display List
                                                 (DrawRect, DrawBorder, DrawText, DrawImage)
                                                         │
                                        ┌────────────────┴────────────────┐
                                        ▼                                 ▼
                                 Terminal Renderer                 Bitmap Renderer
                              (24-bit ANSI Truecolor)             (24-bit .BMP Image)

                                                 ▲
                                                 │
[JavaScript] ───► Lexer ───► AST Parser ───► Bytecode Compiler ───► Stack VM
                                                                         │
                                                 ├──► DOM Bindings (getElementById, textContent)
                                                 ├──► HTML5 Canvas 2D (getContext("2d"))
                                                 ├──► SVG Vector Graphics (<svg>, <path>, <circle>)
                                                 ├──► ES6 Collections (Map, Set) & Destructuring
                                                 ├──► Web Storage & Cookies (localStorage, cookies)
                                                 └──► Async / Promises / EventLoop (fetch, setTimeout, rAF)
```

### 1. DOM Core, W3C EventTarget & Web Components (`include/litebird/dom/`)
- Polymorphic node hierarchy (`Node`, `Element`, `Text`, `Document`, `DocumentFragment`).
- Tree navigation (`parent()`, `children()`, `append_child()`, `remove_child()`).
- DOM query methods: `getElementById()`, `getElementsByTagName()`, `querySelector()`, `querySelectorAll()`.
- Interactive tree printer (`dumpTree()`).
- **No recursion over the tree:** the destructor, `textContent`, `innerHTML`, the selector and tag searches and the dumps are loops (`include/litebird/dom/TreeWalk.hpp`), so a tree a script nested 200,000 levels deep can be built, queried and released.
- **W3C 3-Phase DOM Event Dispatch:** Standard capture, target, and bubbling phases (`Event`, `addEventListener(type, listener, useCapture)`, `dispatchEvent()`, `stopPropagation()`, `preventDefault()`).
- **HTML Form Controls:** Native DOM state and change event dispatch for `<input type="text|checkbox">`, `<textarea>`, `<button>`, and `<select>`.
- **MutationObserver API:** Observation of DOM mutations (`childList`, `attributes`, `attributeOldValue`, `subtree`, `attributeFilter`), microtask batching, and `takeRecords()`.
- **Autonomous Custom Elements:** `customElements.define()`, `whenDefined()`, dynamic element upgrade, and lifecycle callbacks (`connectedCallback`, `disconnectedCallback`, `attributeChangedCallback`).
- **Shadow DOM & Slot Content Projection:** `attachShadow({ mode: 'open' | 'closed' })`, `shadowRoot`, scoped CSS encapsulation with `:host` rules, and `<slot>` content projection (`assignedNodes()`, `assignedElements()`).

#### The DOM a script sees (Phase 27, `src/js/Dom*Bindings.cpp`)
- **Interfaces with prototypes:** about 170 (`Node`, `Element`, `HTMLElement` and the HTML element interfaces, `Document`, `Event` and its kinds, `DOMTokenList`, `HTMLCollection`, `NodeList`, `Range`, `MutationObserver`, `FormData`, `URL`, ...). Members live on the prototype and check what they are called on (`Illegal invocation`), constructors can be subclassed, and one node has one object.
- **Nodes and trees:** `append`/`prepend`/`before`/`after`/`replaceWith`/`replaceChildren`, `insertAdjacentHTML/Element/Text`, `cloneNode`, `contains`, `compareDocumentPosition`, `isEqualNode`, `normalize`, `DocumentFragment`, `<template>.content`, `TreeWalker`, `NodeIterator`, `DOMParser`, `XMLSerializer`, and the hierarchy checks with the `DOMException` each one names.
- **Ranges and the selection** (`include/litebird/js/DomLive.hpp`): `Range` with `extractContents`, `cloneContents`, `deleteContents`, `insertNode`, `surroundContents`, `createContextualFragment` and the comparisons; `getSelection()`. Ranges are live: their boundaries follow nodes that are inserted and removed, text that is edited or split, and `normalize()`.
- **Selectors** (`src/dom/Selector.cpp`): compound and complex selectors with all four combinators, attribute selectors with every operator and the `i` flag, `:is()`, `:where()`, `:not()`, `:has()`, the `:nth-*` family with `of`, and the state pseudo-classes of forms; `querySelector(All)`, `matches`, `closest`. An invalid selector is a `SyntaxError`.
- **Markup:** `innerHTML`/`outerHTML` written by the standard's serializer (escaped text and attributes, void elements, namespaces) and read by the parser for the element it is assigned to.
- **Attributes and their reflections:** `classList`, `dataset`, `attributes`, `style` as a live declaration block, `id`/`className`/`hidden`/`tabIndex`/`href`/... per element type.
- **`getComputedStyle`** (`src/js/DomStyleBindings.cpp`): an answer for every property. The rules of the page ordered by importance, specificity and position, on top of what HTML gives each element; inherited and initial values; colours as `rgb()`; lengths in px (`em`, `rem`, `pt`, `calc()`, font sizes through the chain of ancestors); shorthands and their parts. Computed values, not used ones: `width: auto` stays `auto`, because layout has not happened.
- **Events:** `Event` and 27 subclasses with their init dictionaries, capture/target/bubble with `composedPath()`, `once`/`passive`/`signal`, `on*` properties and attributes, default actions (`click()` on a checkbox, a label, a submit button, a link to a fragment), `AbortController`.
- **Forms** (`src/dom/FormState.cpp`): `value`/`checked`/`selected` with their dirty flags, radio groups, `<select>` and `options`, constraint validation (`validity`, `checkValidity`, `reportValidity`, `setCustomValidity`), `form.elements`, submission and reset with their events, `FormData`, `requestSubmit`.
- **Observers and components:** `MutationObserver` with every option, custom elements (autonomous and customized built-ins, upgrades, all reactions, in the order the standard queues them), shadow trees and slots.
- **The window:** `location` and `history` (`pushState`, `hashchange`, `popstate`), `URL`/`URLSearchParams` on the URL Standard's parser, timers, `localStorage`, cookies, `matchMedia`, `XMLHttpRequest`, `Blob`/`File`/`FileReader`, `window.onerror` and `unhandledrejection`, named access (`window.myId`, `document.forms.search`).
- **Loading:** scripts run as the parser reaches them (`document.currentScript`, `document.readyState`), then `DOMContentLoaded` and `load`.
- **XML** (Phase 28, `src/xml/`): `DOMParser` for `application/xml`, `text/xml`, `application/xhtml+xml` and `image/svg+xml`, with the `parsererror` document for text that is not well formed; `document.implementation.createDocument()`; `createElementNS`, `setAttributeNS` and their relatives, `lookupNamespaceURI`, `lookupPrefix`, `isDefaultNamespace`; `CDATASection` and `ProcessingInstruction`; `XMLSerializer` and `innerHTML` in an XML document by *DOM Parsing and Serialization*; the namespace bar in selectors (`*|a`, `|a`, `[*|href]`). `XMLHttpRequest` for `data:` URLs: `responseXML`, `responseText`, `response` with `responseType` (`text`, `document`, `json`, `blob`), `overrideMimeType`, the states and progress events, synchronous requests, `abort()`.

### 2. HTML5 Parser: spec tree construction (`include/litebird/html/`)
- **Full WHATWG tree-construction algorithm** (`HtmlTreeBuilder`): 23 insertion modes (`initial` through `after after frameset`), including `in table`, `in table body`, `in row`, `in cell`, `in caption`, `in column group`, `in select`, `in template` and `text`.
- **Implied element synthesis:** `<html>`, `<head>`, `<body>`, `<tbody>` and `<colgroup>` are created when the markup omits them; `<p>`, `<li>`, `<dd>`/`<dt>`, `<option>` and heading elements auto-close per the spec's implied-end-tag rules.
- **Active formatting elements + the adoption agency algorithm:** misnested `<b><i></b></i>` repairs produce the same DOM a browser does.
- **Foster parenting:** stray content inside `<table>` is relocated before the table instead of being dropped.
- **Scopes:** default, list-item, button, table and select scoping rules drive every "is this tag in scope?" decision.
- **Foreign content:** SVG and MathML namespaces, tag *and* attribute case adjustment (`<foreignobject>` → `foreignObject`, `viewbox` → `viewBox`), `xlink`/`xml` attribute namespacing, HTML/SVG/MathML integration points, `</p>`/`</br>` breakouts, and `<![CDATA[ ]]>` sections. Scope checks are namespace-aware, so an `<svg td>` never satisfies an HTML table-scope test.
- **`<template>`:** contents live in a separate `DocumentFragment`, exactly as the spec requires.
- **Comments and doctypes are real nodes** (`Comment`, `Document::doctype()`), including quirks/limited-quirks/no-quirks document modes.
- **Spec-complete tokenizer:** all tag, attribute, comment, bogus-comment, doctype and CDATA states, RCDATA/RAWTEXT/plaintext content models, CRLF normalisation, the complete 2231-entry named character reference table, and the full **script-data state machine** including the escaped and double-escaped runs (`<!--<script </script>-->` stays text, as it should).
- **`eof-in-tag`, `eof-before-tag-name` and NUL handling** match the spec: a tag cut off by EOF is discarded, `</` at EOF is text, and a NUL in a comment becomes U+FFFD.
- **Fragment parsing with a context element**, which is what makes `innerHTML` (and `<tbody>` vs `<div>` contexts) behave correctly.
- **A nesting limit of 512** (`HtmlTreeBuilder::kMaxTreeDepth`, the number Blink and WebKit use): an element that would be deeper is attached beside the element it was written in. The stack of open elements is not shortened, so the parser's state is what the standard says at any depth, and every per-token question about that stack is answered from an index.
- Legacy helpers: `HtmlParser::parse()`, `HtmlParser::parse_fragment_with_context()`.

### 3. HTML Parser helpers (`include/litebird/html/`)
- Stateful streaming tokenizer (`HtmlTokenizer`).
- Supports start tags, end tags, attributes (quoted/unquoted, boolean), comments, and DOCTYPEs.
- Automatic handling of void elements (`<img />`, `<br>`, `<input>`, `<hr>`, `<meta>`, `<link>`).
- Raw text mode for `<style>` and `<script>` tags.
- Character entity decoding (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&nbsp;`).

### 4. CSS Engine, Cascade, Animations & Pseudo-Classes (`include/litebird/css/`)
- Selector parser supporting:
  - Universal (`*`)
  - Type / Tag selectors (`div`, `h1`, `p`)
  - Class selectors (`.card`, `.btn`, `.navbar`)
  - ID selectors (`#header`, `#main`)
  - Attribute selectors (`[attr]`, `[attr="value"]`)
  - Descendant combinators (`div .card p`)
  - **Advanced CSS Pseudo-Classes:**
    - Structural: `:first-child`, `:last-child`, `:nth-child(An+B)`, `:nth-of-type(An+B)` (supports `odd`, `even`, and linear index formulas).
    - User Action & State: `:hover`, `:focus`, `:active`, `:disabled`, `:checked`.
    - Negation: `:not(...)` functional selector inversion.
- **CSS Variables & Custom Properties (`var()`):** Lexical custom property cascade (`--custom-name: value`) with inheritance down DOM subtrees and recursive nested fallback evaluation (`var(--color, var(--fallback, #fff))`).
- **CSS Math Expression Engine (`calc()`):** Recursive descent arithmetic evaluator resolving additions, subtractions, multiplications, divisions, percentages, and variable substitutions.
- **CSS 2D Transforms & Affine Matrix Math:** Homogeneous 3x3 transform matrices (`TransformMatrix2D`) implementing `translate()`, `rotate()`, `scale()`, `skew()`, and `transform-origin`, complete with inverse-matrix hit-testing.
- **CSS Transitions & Keyframe Animations (`@keyframes`):**
  - Declarative `@keyframes` rule parsing with percentage offsets (`0%`, `50%`, `100%`) and `from`/`to`.
  - Animation properties: `animation-name`, `animation-duration`, `animation-timing-function`, `animation-delay`, `animation-iteration-count`, `transition-property`, `transition-duration`.
  - Timing function calculation: `linear`, `ease`, `ease-in`, `ease-out`, `ease-in-out`.
  - Continuous property interpolation for `opacity`, `width`, `height`, `color`, and `background-color`.
- **CSS Media Queries (`@media`):** Supports responsive breakpoints (`(max-width: ...)`, `(min-width: ...)`, `(max-height: ...)`, `(min-height: ...)`). Dynamically re-evaluates rule application against active viewport dimensions.
- **Longhand Shorthands (Phase 15):** `margin`, `padding`, `border`, `border-width`, `border-color`, per-side borders, `background`, `font`, `border-radius` and `flex-flow` are expanded into longhands before cascading, so `margin: 10px 20px` and `border: 3px solid #0f0` behave as expected.
- **`!important` and the full cascade order (Phase 15):** author normal → inline normal → author `!important` → inline `!important`, applied in `(importance, specificity, source order)` tiers.
- **Percentage lengths (Phase 18):** `width`, `height`, `min`/`max` dimensions, `margin` and `padding` percentages resolve against the containing block at layout time (`width: 50%` is half the parent, not fifty pixels), and percentage table cells resolve against the table width.
- **Relative and viewport units (Phase 15):** `em`/`ex`/`ch` resolve against the element's own computed font size, `rem` against the root size, and `vw`/`vh`/`vmin`/`vmax` against the viewport; `pt`, `pc`, `in`, `cm`, `mm` and `q` are converted to CSS pixels.
- **Specificity Calculation:** Computes `(ID, Class, Tag)` triples for accurate cascade resolution.
- **Built-in User-Agent Stylesheet:** Default styling for headings, paragraphs, lists, and layout blocks.
- **Style Resolver:** Combines User-Agent styles, Author stylesheets, and inline `style="..."` attributes with property inheritance (`color`, `font-size`, `text-align`).

### 5. Layout Engine: Block, Flexbox, Grid, Positioning & IFC (`include/litebird/layout/`)
- Standard Box Model: `content`, `padding`, `border`, `margin`.
- Box types: `BlockNode`, `FlexNode`, `GridNode`, `InlineNode`, `AnonymousBlock`, `TextNode`, `ImageNode`, `TableNode`, `TableRowNode`, `TableCellNode`.
- **CSS Box Sizing & Constraints:** Full support for `box-sizing: content-box | border-box`, plus `min-width`, `max-width`, `min-height`, and `max-height`.
- **CSS Positioning & Stacking Contexts:** `position: static | relative | absolute | fixed`, coordinate offsets (`top`, `right`, `bottom`, `left`), and hierarchical `z-index` stacking context sorting.
- **CSS Grid Layout (`display: grid`):** Track dimension resolution (`grid-template-columns: 1fr 2fr 1fr`), `repeat()` counts, `minmax()` track constraints, `auto-fit` auto-repetition, cell auto-placement, item alignment, and grid gap spacing (`row-gap`, `column-gap`, `gap`).
- **CSS Flexbox (`display: flex; flex-wrap: wrap`):** Single-line and multi-line flex container line breaking, cross-axis line alignment, space distribution, and flex item `gap` / `row-gap` / `column-gap` spacing without margin distortion.
- **Inline Formatting Context (IFC 2.1):**
  - Line box construction (`LineBox`) and inline fragment generation (`InlineFragment`).
  - True continuous text flowing across mixed inline elements (`<b>`, `<strong>`, `<i>`, `<em>`, `<a>`, `<code>`, `<span>`).
  - Inline element styling: custom colors, background pills, borders, padding, and text-decoration (`underline`).
  - Word wrapping across element boundaries with exact baseline alignment.
- **Floats & Block Formatting Contexts (CSS 2.1 §9.5):**
  - `float: left | right | none` with shrink-to-fit widths, floating out of the normal flow, and floats stacking side by side until a band is full.
  - `clear: left | right | both` pushes a box below the relevant float bottoms.
  - Line boxes shrink around floats per line (`LineBox::left_intrusion` / `right_intrusion`), so text wraps beside a float and reclaims the full width once it has passed.
  - A block formatting context (`overflow != visible`, floats, flex/grid containers, table cells, inline-blocks, the root) grows to contain its floats instead of collapsing.
  - `overflow: hidden | auto | scroll` and `contain: paint` clip painting: the display list carries `PushClipCommand`/`PopClipCommand` and both renderers honour the clip stack.
- **CSS Table Layout (`display: table | table-row | table-cell | table-row-group`):**
  - Anonymous table structure: rows and row groups are flattened into a row grid, and stray cells are collected into rows.
  - Automatic column sizing: each column is at least as wide as its widest cell's min-content width or explicit width; an explicit table width distributes the surplus across the columns.
  - Row heights are the maximum of the row's cell heights; cells carry their own padding, borders and backgrounds.
  - Percentage cell widths resolve against the table width (a `width: 25%` cell in a 300px table is a 75px column).
  - UA stylesheet provides the default `table`/`tr`/`td` display values plus `<th>` centering and bold weight.
- **Incremental Layout & Invalidation:** Granular `dirty` marking (`mark_dirty()`) and subtree-isolated relayout avoiding redundant full-document recalculations.

### 6. Multi-Backend Rendering, SVG Vector Graphics, Canvas 2D & Image Decoding (`include/litebird/render/`)
- **Native SVG Vector Graphics Engine (`SvgRenderer`):**
  - Direct rasterization of `<svg>` DOM trees into `ImageNode` layout boxes with iterative ID indexing and recursion/cycle guards.
  - `viewBox` + `preserveAspectRatio` (`meet`/`slice`/`none`), full `transform` stacks (`matrix`, `translate`, `scale`, `rotate`, `skewX`, `skewY`), and nested `<svg>`.
  - Shape & path rasterization: `<rect>` (including `rx`/`ry`), `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>`, and full SVG `<path>` grammar (`M/m`, `L/l`, `H/h`, `V/v`, `C/c`, `S/s`, `Q/q`, `T/t`, W3C F.6.5/F.6.6 `A/a` elliptical arcs, `Z/z`).
  - Paint servers, styling, reuse & clipping: `<defs>`, `<linearGradient>`/`<radialGradient>` (`href` inheritance, `objectBoundingBox`/`userSpaceOnUse`, `gradientTransform`, `spreadMethod`), `<use>`/`<symbol>`, `<clipPath>`, inline `style`, `rgba()`/`#RGBA`/`#RRGGBBAA`, `currentColor`, `opacity`/`fill-opacity`/`stroke-opacity`, `fill-rule="evenodd"|"nonzero"`, `display:none`/`visibility:hidden`, and dashed/capped/joined strokes.
- **HTML5 `<canvas>` 2D Context API (`CanvasRenderingContext2D`):**
  - 2D CTM stack (`save`/`restore`, `translate`, `rotate`, `scale`, `transform`, `setTransform`, `resetTransform`), `globalAlpha`, and `clip()`.
  - 4× vertical + analytical horizontal anti-aliased scanline rasterization (`nonzero` and `evenodd` winding rules) and polygon-ribbon thick stroking (`lineWidth`, `lineCap`, `lineJoin`, `miterLimit`, `setLineDash`/`getLineDash`/`lineDashOffset`, `strokeGradient`).
  - Geometric & path primitives: `fillRect`, `clearRect`, `strokeRect`, `rect`, `roundRect`, `arc`, `ellipse`, `arcTo`, `quadraticCurveTo`, `bezierCurveTo`, `isPointInPath`.
  - Text (Phase 46): `fillText`, `strokeText`, `measureText` with `font` (CSS font shorthand subset — style, variant, weight, size in px/pt/em/rem/% with an optional `/line-height`, and a family list that is parsed and reflected but rasterised with the engine's system font), `textAlign`, `textBaseline`, `maxWidth` horizontal condensing, solid or gradient paint, CTM-aware sampling with AA coverage, `clip()` and `globalAlpha` support, and the save/restore text state.
  - Inverse-CTM linear/radial gradients (`Pad`/`Reflect`/`Repeat`), `createImageData`/`getImageData`/`putImageData`, CTM-aware `drawImage`, and JS property accessors via `JsValue::define_accessor`.
- **Native Image Decoders (`JpegDecoder`, `GifDecoder`, `ImageDecoder`):**
  - Standalone ITU-T T.81 Baseline/Extended/Progressive JPEG decoder (`JpegDecoder`: 4:4:4/4:2:2/4:2:0/4:4:0, Grayscale/YCbCr/RGB/CMYK/YCCK, restart markers, multi-scan progressive refinement).
  - Standalone GIF87a/GIF89a single-frame & animated multi-frame decoder (`GifDecoder`: 2..8-bit LZW, GCT/LCT, 4-pass interlacing, transparency, all 4 disposal methods, loop count).
  - `ImageDecoder` for PNG (DEFLATE / uncompressed), Netpbm PPM, and BMP (`JpegDecoder`/`GifDecoder` wiring into `ImageDecoder::decode` tracked as follow-up once PR #39 lands).
- **Terminal Renderer:** Renders web pages and images directly inside your terminal using 24-bit ANSI Truecolor half-block characters and ASCII borders.
- **Bitmap Renderer:** Exports 24-bit uncompressed `.bmp` images using an embedded font with anti-aliasing. Zero external GUI or graphics libraries required.

### 7. JavaScript Engine & Web APIs (`include/litebird/js/`, `include/litebird/net/`)

The engine was rebuilt in Phase 23. It is a tree-walking interpreter: a lexer
and a recursive-descent parser produce an AST, and the evaluator runs it
directly. A script with a syntax error does not run at all, and no script runs
twice.

- **Language:** `var`/`let`/`const` with hoisting and the temporal dead zone,
  strict mode, closures, all operators including `?.`, `??` and the logical
  assignments, destructuring, spread and rest, default parameters, template
  literals and tagged templates, labelled statements, `switch`, `try`/`catch`/
  `finally`, getters and setters, computed keys, symbols, iterators,
  `for`...`of` and `for`...`in`, generators, classes (fields, private names,
  statics, accessors, `super`, subclassing built-ins), regular-expression
  literals.
- **Promises and async functions:** promises follow the specification's
  ordering (reactions are FIFO microtasks, thenables are adopted), with `all`,
  `allSettled`, `any`, `race` and `withResolvers`. `await` suspends the
  function and returns to its caller; `for await` and async generators work.
  A rejection nothing handles is reported as `Uncaught (in promise)`.
- **Standard library:** `Object`, `Function`, `Array`, `String`, `Number`,
  `Boolean`, `Symbol`, `Math`, `JSON`, `Date`, `RegExp` (a backtracking matcher
  with lookaround, named groups and the `g`, `i`, `m`, `s`, `u`, `y` flags),
  `Map`, `Set`, `WeakMap`, `WeakSet`, `Reflect`, the `Error` family,
  `structuredClone`, `globalThis`, `eval` and `Function`. Methods live on
  shared prototypes; each interpreter has its own set (a realm).
- **Event loop:** tasks, microtasks and timers, with a microtask checkpoint
  whenever the script stack empties. `setTimeout`/`setInterval` (with extra
  arguments), `queueMicrotask`, `requestAnimationFrame`, `performance.now()`.
  The loop's clock can skip ahead, so `--run-js` and one-shot page rendering
  never sleep waiting for a timer.
- **Strings:** a string value is a 24-byte handle on a shared block of UTF-8
  (`JsString`). Passing, reading and indexing a string copy nothing, `s += x`
  appends in place, and a large slice is a range of its parent, so a loop over
  a long string or a string built piece by piece costs what it reads
  (`ARCHITECTURE.md`, Phase 24).
- **Garbage collector:** mark and sweep, including cycles through closures.
  Every value holds a counted handle to its object, and the collector treats
  an object with more handles than it can find inside the heap as held by
  native code. Nothing has to be registered as a root. `gc()` runs a
  collection and returns its statistics.
- **Page lifecycle:** `document.readyState`, `DOMContentLoaded` and `load`
  fire once the document's scripts have run.
- **Web APIs:** `fetch`, `Response`, `Headers`, `URL`, `URLSearchParams`,
  `localStorage`/`sessionStorage`, `document.cookie`, `AbortController`,
  `WebSocket` (RFC 6455), `Worker` (each worker is a thread with its own
  interpreter and event loop; messages are copied as JSON),
  `canvas.getContext("2d")`, and the DOM: section 1, "The DOM a script
  sees".
- **What is not there:** BigInt, `Proxy`, typed arrays and `ArrayBuffer`,
  modules (`import`/`export`), `with`, holes in arrays, `Intl`. See "Known
  gaps" in `ARCHITECTURE.md` for the limits of what is there.
- **Bytecode VM:** an earlier experiment, kept for `--dump-bytecode`. It
  covers a small subset of the language and scripts do not run on it.

### 8. Modern Browser Capabilities & Advanced Engine Platform (Phase 14) (`include/litebird/dom/`, `include/litebird/render/`, `include/litebird/net/`, `include/litebird/js/`)
- **IntersectionObserver API:** Viewport and root-relative intersection detection, threshold crossing calculations (`thresholds`, `rootMargin`), ratio computation, entry records, and EventLoop microtask delivery.
- **ResizeObserver API:** Element content and border box size change observation (`contentRect`, `borderBoxSize`), target tracking, and reactive microtask dispatch.
- **History API & Client-Side SPA Routing:** Session history stack management (`pushState`, `replaceState`, `back`, `forward`, `go`), navigation listeners, and standard `popstate` window event dispatch.
- **CSS Visual Filters & Backdrop Filters:** Full parsing and software rasterization for CSS `filter` and `backdrop-filter` operations:
  - Separable Gaussian-approximation box blur (`blur()`).
  - Color channel adjustments: `brightness()`, `contrast()`, `grayscale()`, `sepia()`, `invert()`, and `opacity()`.
  - Multi-parameter drop shadows: `drop-shadow(dx dy blur color)`.
- **Canvas 2D Blend Modes (`globalCompositeOperation`):** Standard Porter-Duff and color blending algorithms implemented in software: `source-over`, `copy`, `multiply`, `screen`, `lighter`, `destination-over`.
- **JavaScript Generators & Iteration Protocol:**
  - Generator functions: `function*`, cooperative suspension/resumption via `yield` expressions, and delegated iteration (`yield*`).
  - Generator objects with standard `.next()`, `.return()`, and `.throw()` methods.
  - Well-known Symbol `Symbol.iterator` support on arrays, generators, and custom iterables.
  - `for...of` iteration statement over arrays, strings, and generator sequences.
- **AbortController & Network Cancellation:**
  - Standard `AbortController` and `AbortSignal` APIs (`aborted`, `reason`, `abort()`, `throwIfAborted()`, `AbortSignal.timeout(ms)`).
  - Integration with `fetch(url, { signal })` to cleanly abort inflight network requests and immediately reject pending promises with `AbortError`.
- **CSS Grid Named Areas & Layout Containment:**
  - `grid-template-areas`: Multi-row and multi-column visual ASCII grid template definitions.
  - `grid-area`: Item placement into named template areas or explicit grid line coordinates (`row-start / col-start / row-end / col-end`).
  - CSS Containment (`contain: none | strict | content | layout | paint | size`): Layout isolation stopping dirty bit propagation at containment boundaries and size containment.

### 9. Interactive Browser Shell, EventLoop & Network (`include/litebird/shell/`, `include/litebird/net/`)
- **Interactive TUI Browser Shell (`BrowserShell`):**
  - Terminal User Interface with keyboard-driven browsing: `[Tab]` / `[Shift+Tab]` cycles focus, `[Enter]` activates links and buttons, `[↑/↓/PgUp/PgDn]` scrolls viewport.
  - Navigation history (`back()`, `forward()`, `reload()`, `navigate_to()`).
  - Interactive URL bar (`[o]` key) supporting local files and remote HTTP URLs.
  - Integrated DevTools inspector pane (`[d]` key) with live DOM and Heap memory metrics.
- **Asynchronous EventLoop & Timers (`EventLoop`):**
  - High-resolution timer queue for `setTimeout()` and `setInterval()`.
  - Timer-driven DOM mutations trigger reactive relayout and repaints.
- **Resource Loader & Native HTTP Client (`ResourceLoader`):**
  - Native POSIX socket HTTP/1.1 client supporting GET requests and headers without external dependencies.
  - Rejects unsafe HTTP methods, URL request targets, and caller header names/values; caps request/response headers at 64 KiB and total HTTP responses at 16 MiB.
  - Full relative URL path resolution.

---

## 🚀 Getting Started

### Prerequisites

- `g++` with C++20 support (GCC 11+)
- `make`

### Building

```bash
# Build the main litebird CLI
make

# Run the 193 engine test suites
make test
```

### Running Examples

```bash
# 1. Run the Phase 14 Super-Showcase (Observers + Visual Filters & Blending + Generators + Grid Areas)
./build/litebird examples/phase14_showcase.html

# 2. Run the Phase 9 Super-Showcase (SVG Vector Graphics + Animations + Pseudo-Classes + ES6 Map/Set)
./build/litebird examples/phase9_showcase.html

# 3. Run the Next-Gen Feature Showcase (Canvas 2D + Grid + Storage + ES6)
./build/litebird examples/nextgen_showcase.html

# 4. Launch the Interactive TUI Browser Shell
./build/litebird --shell examples/browser_portal.html

# 5. Export Phase 14 showcase directly to a 24-bit BMP image
./build/litebird --render-bmp phase14_showcase.bmp examples/phase14_showcase.html

# 6. Run the live EventLoop Timer & Interactive Counter application
./build/litebird examples/timer_counter.html

# 6. Run the DOM EventTarget and Mark-and-Sweep GC showcase
./build/litebird examples/events_and_gc.html

# 7. Render an Inline Formatting Context (IFC) page with mixed inline badges
./build/litebird examples/ifc.html

# 8. Render a Flexbox layout with responsive cards and navbar
./build/litebird examples/flexbox.html
```

---

## 🛠️ CLI Options

| Flag | Description |
| :--- | :--- |
| `--shell [file/url]` | Launch interactive TUI Browser Shell |
| `--dump-dom` | Print the hierarchical DOM tree |
| `--dump-css` | Print parsed CSS rules and specificity calculations |
| `--dump-layout` | Print the computed Layout Box tree with geometry |
| `--dump-paint` | Print atomic display list commands |
| `--dump-bytecode` | Disassemble JavaScript code into VM bytecode |
| `--render-terminal` | Render page to ANSI Truecolor terminal (default) |
| `--render-bmp <file>` | Render page to an uncompressed 24-bit BMP image |
| `--render-png <file>` | Render page to a compressed PNG image (with TrueType `.ttf` text when available) |
| `--gui [--port=<N>] [url]` | Launch the interactive browser Web GUI on `0.0.0.0:<N>` (default `8080`) with viewport, navigation, interaction, and DevTools |
| `--run-js "<code>"` | Run a script, then its timers and promise callbacks. Exits 1 if anything was left uncaught |
| `--run-script <file>` | Load the page, run the file as one more script, then the page's load events and timers. Nothing is drawn. Exits 1 if anything was left uncaught |
| `--url=<URL>` | The address the page is said to come from (`location.href`); by default the `file:` URL of the file (or the fetched `http:`/`https:` URL) |
| `--virtual-time-budget=<ms>` | How much timer time a page gets to settle before it is rendered (default 16, one frame) |
| `--xml-canonical <file>` | Read the file as XML and print its canonical form, the one the XML conformance suite compares. Exits 0 for a well-formed document, 1 for one that is not (the reason on standard error), 3 for an encoding this build cannot read |
| `--xml-events <file>` | Read the file as XML and print what the parser reports, one line an event (`tools/fuzz/diff_xml.py` compares these with Expat and libxml2). The same exit statuses |
| `--repl` | Launch the interactive JavaScript & DOM REPL |

---

## 🧪 Running Tests & Conformance

```bash
make test
```

Output (193 suites; the first and last lines shown):
```text
=== Running Litebird Browser Engine Test Suite ===

Running test: color_parsing... PASSED
Running test: css_color_keywords_match_the_css_specification... PASSED
...
All 193 engine test suites passed successfully!
```

```bash
make js-conformance                                                     # the JavaScript scoreboard
python3 tools/conformance/run_js_conformance.py --show 10 --filter async   # failing cases, with diffs
```

```bash
make dom-conformance                                                    # the DOM scoreboard
python3 tools/conformance/run_dom_conformance.py --show 10 --filter forms  # failing cases, with diffs
python3 tools/conformance/run_dom_conformance.py --reference            # the corpus itself, against jsdom (needs Node.js and npm)
make url-conformance                                                    # URL parsing and setters
```


### CSS whitespace runs (Phase 31)

The CSS parser and style resolver used to trim a leading run of spaces by
erasing the first character repeatedly, shifting the whole remainder each
time. `make bench-css` runs ten compact/padded page pairs through parsing,
style, layout and paint at 30,000 and 300,000 spaces per placeholder. The
dumps must match, and each padded run must finish within three seconds.
On the verification machine the 300,000-space `:nth-child` case went from
14.6 s to 111 ms; `repeat(...)`, `minmax(...)` and `translate(...)` went from
9.8–22.5 s to 0.19–0.30 s. The command and full table are in `ARCHITECTURE.md`,
Phase 31. This is a cost fix, not a claim of broader CSS conformance. The
separate `:nth-child(2n + 1)` sign-space parsing bug is fixed as of finding
39: the formula matcher ignores whitespace tokens and parses strictly signed
integers (`AUDIT.md`, finding 39).


### Attribute-list removals (Phase 35)

An element's attributes and the lazy index's ascending numbers used to be
plain vectors. Removing the first attribute shifted both tails, so draining
50,000 attributes from the front took seconds while the back was milliseconds.
They are still vectors, but with a logical start offset: a front erase
advances the offset and compacts only when the dead prefix reaches the live
tail, so end removals are linear and an empty list allocates nothing.
Lookup/update/copy/move and random-access iterator semantics stay the same; a
middle removal still moves the tail. `make bench-attributes` runs six DOM cases at 50,000
attributes (front, back, alternating ends, front-half update/reinsert,
namespaces, short-list churn), checks their checksums, and fails over 3 s.
On the verification machine the front case went from about 6.3 s to 0.09 s
whole-process. This is a cost fix, not the open `Attr`/namespace representation
or named-property work; the tokenizer repeated-name cost is fixed as of
Phase 39. Details and repro are in `ARCHITECTURE.md`,
Phase 35, and `AUDIT.md`, finding 40.


### The tokenizer's repeated attribute names (Phase 39)

The HTML tokenizer drops a repeated attribute name — the first one wins — by
comparing each name with every attribute it has already accepted. That is the
square of the count: one element with 50,000 attributes spent about 2.9 s in
the parser, and an end tag with that many paid the same. A tag with 32 or more
accepted attributes now keeps a hash set of their names; shorter tags keep the
scan, which is cheaper than building a table. The set holds views of the names
inside the token, so no name is stored twice, and it is rebuilt when the
attribute vector grows under it. `make bench-tokenizer-attributes` parses seven
page shapes at 50,000 attribute occurrences (distinct names, every name twice,
uppercase repeats, long names, the four value spellings, many small tags, an
end tag), compares each tree dump with the tree the markup must produce, and
fails over 1 s. On the verification machine the wide cases went from 3.5 s to
0.09 s and the many-small-tags case did not change. First-wins, ASCII case
folding, order and every value spelling are unchanged; this is a cost fix, not
a parser conformance change. Details and repro are in `ARCHITECTURE.md`,
Phase 39, and `AUDIT.md`, finding 46.


### A string built half a surrogate pair at a time (Phase 41)

String text is UTF-8 in blocks that never change once written, and `s += x`
writes into the room after the text so a loop costs what it writes. One join
could not: `s += '\uD83D'; s += '\uDE00'` makes one character out of two halves,
four bytes that start three bytes before the end of what was already written,
so it copied the whole string at every pair — 20,000 pairs took 5.6 s and
40,000 took 21 s, against 9 and 15 ms a pair at a time. A join now owes a
trailing high half instead of writing it: the handle keeps it, and the
character is written where the block's text ends when the low half arrives.
Anything that reads the text settles the handle first, writing three bytes
into the free room or copying once into a block the handle's own block keeps
alive. `make bench-surrogates` runs the two sizes a doubling apart and the
keep-each-prefix shape against Node.js with a one-second limit: 17 ms, 28 ms
and 14 ms, where `main`'s binary fails all three (5.5 s, over 8 s, 3.9 s). The
corpus case that used to take 5.5 s of its 10-second budget takes 0.035 s, so
the JavaScript floor in CI rose to the score at that phase, 907 of 921
(Phase 45 raises it again). The handle is a word
wider, which costs about 5% across the string benchmarks that were already
there. Reading a half-owed string before completing the
pair still copies; that shape is a row of the task table. Details and repro are
in `ARCHITECTURE.md`, Phase 41, and `AUDIT.md`, finding 47.


### UTF-16 storage for non-ASCII strings (Phase 53)

Phase 53 (finding 59) stores non-ASCII `JsString` buffers as two-byte UTF-16
code units instead of UTF-8 while keeping ASCII buffers one byte per unit and
the 24-byte `JsString` handle unchanged (replacing Phase 41's `m_pending`
field with an atomic cached UTF-8 byte length). Reading `length`, indexing,
`charCodeAt`, `indexOf`, slicing, and regex matching all operate directly on
the 1-byte or 2-byte storage without generating UTF-8, while host `.view()` /
`c_str()` callers populate a lazy UTF-8 cache (with `Utf8Slice` fallback when
an unpaired trailing high surrogate is requested so the prefix never freezes
a surrogate stub before the low half arrives). Because a trailing high half in
UTF-16 is an ordinary 2-byte unit rather than a 3-byte UTF-8 surrogate that
would have to be rewritten into a 4-byte pair, appending `'\uDE00'` after
reading, calling `indexOf`, or slicing with `substring` writes the low half in
place (and a sole-thread CAS tail rewind reclaims a dropped temporary tail when
safe).

`make bench-surrogates` now runs six cases (20k, 40k, keep-each-prefix, read
between halves, `indexOf` between halves, and a `substring` piece read then
completed) against Node.js under a one-second guard (`14 / 22 / 11 / 17 / 154 /
17 ms`, versus `58 / 242 / 893 ms` at 5k/10k/20k for read-between-halves
before), `make bench` runs all 45 cases, `make tsan` passes under TSan and
ASan+UBSan, `make test` passes 193/193 suites, and `make diff-strings` prints
`50485 / 50580`. Remaining nuances: in JS, `s += '\uD83D\uDE00'; s =
s.substring(0, s.length - 1); s += '\uDE00'` still keeps `refs == 2` across
the variable slot and operand temporary during `+=` (though single-owner C++
rewinds), and calling `.view()` repeatedly while a buffer ends in an unpaired
high half allocates a slice per call. Details are in `ARCHITECTURE.md`,
Phase 53, and `AUDIT.md`, finding 59.


### Promise combinators must resolve before the next item (Phase 45)

`Promise.all`, `any`, `race` and `allSettled` used to walk an iterable
until it ended without calling `Promise.resolve` on any element. If it did not
end, they never reached the first value. Now each step calls the receiver's
`resolve` (the method is looked up once, before the iterator), reads the
result's `then`, and subscribes before asking for the next value. If any of
those operations throws, the iterator's `return()` runs and the returned
promise rejects. A custom `then` is invoked while the iterator is open.

`make verify-promise-iterators` prints `34/34` async cases with a 3-second
limit; four new cases use hostile iterators and expectations independently
checked with Node. The old binary passes 30/34, with timeouts. The full JS
score is now 925/939 after merging Phase 44's identifiers (911/925 before
that merge); `make test` passes 170 suites (163 on the original base). A page
outside the corpus, through `tools/conformance/compare_with_jsdom.py`, prints
`4 lines, 4 the same` after the fix, versus 0 before. A custom array iterator is still
ignored by `js_iterate`'s array fast path, and subclassed Promises still
make base promises: see `AUDIT.md`, finding 51, and `ARCHITECTURE.md`, Phase 45.
The full release and sanitizer chain passed on the base tree, again after
merging main's frames/table work, and again with Phase 44's Unicode identifiers:
170/170 suites, JS 925/939 (924/939 ASan), WPT 5421/5463, 0 unlisted;
`make fuzz` and `make tsan` passed. The CI JS floor is now 925 and the ASan
floor is 923 (both raised, neither lowered).


### Page resource origin (Phase 33)

`fetch()` and `new Worker()` used to send every non-`data:`/non-`http://`
address to the local filesystem, so a page at `https://example.test/` could
read `/home/user/notes.txt` or `file:///…`. Page-initiated loads now go
through `ResourceLoader::load_for_page` with the document's address: web
documents may not read local files, relative references resolve against the
document/base URL, and a `<base href="file:///…">` does not grant file access.
A document the embedder loaded from `file:` keeps local-file access for the
CLI/TUI; trusted navigation is a separate `load_navigation` entry point.
`make security-fetch` generates a marker file and checks the web, base-smuggled,
and local cases (3 checks, 0 failures). `make test` includes the loader,
`make security-fetch` generates a marker file and checks the web, base-smuggled,
and local cases (3 checks, 0 failures). `make test` includes the loader,
fetch/worker, and relative-base suites. Details and repro are in
`ARCHITECTURE.md`, Phase 33, and `AUDIT.md`, finding 37.


### A function as a prototype (Phase 34)

`Object.getPrototypeOf(Derived)` for `class Derived extends Base` used to be
an object that was not `Base` and could not be called, `Object.getPrototypeOf(TypeError)`
was not `Error`, a static member was found one parent up and no further, and
`Function.prototype` was an object: a function is a value here, a prototype
link points at a heap cell, and nothing led from the cell back to the
function. Now something does. The helpers Babel and TypeScript emit for a
class with a parent are made of exactly those calls, so compiled classes run
(three whole programs in `tests/js-conformance/prototypes.dat`), every
`assert_throws_js` of web-platform-tests passes (18 subtests), and the DOM's
interface objects have their parents as prototypes
(`Object.getPrototypeOf(HTMLElement) === Element`).

```bash
./build/litebird --run-js 'class A { static s() { return "A.s"; } } class B extends A {} class C extends B {}
console.log(Object.getPrototypeOf(C) === B, C.s(), Object.getPrototypeOf(TypeError) === Error, typeof Function.prototype)'
# true A.s true function        (before: Uncaught TypeError: s is not a function)
make js-conformance      # 974 / 989; prototypes.dat 68 / 76
make test262             # the standard's own tests on functions, classes and objects: 12,316 of 15,686
```

What is still missing around classes (`Symbol.species`,
`Symbol.hasInstance`) is in `ARCHITECTURE.md`, Phase 34, section 9, with a
failing case each; the design and what it costs are in the same place, and
`AUDIT.md`, finding 41, says what found what. `new.target` in a parent's
constructor and custom elements compiled by Babel, which were on that list,
came with Phase 43 (below).


### Usable browser pipeline: HTTPS/CORS, subresources, live layout, TrueType & Web GUI (Phase 36)

Phase 36 connects the parsers, layout engine, and script runtime into an
interactive multi-resource browser without adding external build-time link
dependencies:

- **HTTPS/TLS, HTTP/1.1 & CORS**: Runtime `dlopen` of OpenSSL 3 (`libssl.so.3`)
  with SNI and CA hostname verification; HTTP/1.1 `Transfer-Encoding: chunked`,
  pure-C++20 `gzip`/`deflate`/zlib decompression (`Deflate.cpp`), redirect
  chaining, and CORS `Access-Control-Allow-Origin` enforcement across `fetch()`
  and networked `XMLHttpRequest`.
- **External stylesheets, scripts & origin-safe images**: `<link rel="stylesheet">`,
  `<base href>`, `media` attributes, recursive `@import`, classic and `defer`
  `<script src>` execution in HTML order, and PNG/JPEG/GIF/SVG `<img src>`
  subresource decoding gated by `ResourceLoader::load_for_page` (`AUDIT.md`,
  finding 44).
- **Live DOM geometry, TrueType `.ttf` text & `--gui` browser**: On-demand
  reflow for `getBoundingClientRect()`/`offset*`/`client*`, block-in-inline
  splitting (`<a>x<p>y</p></a>`), form-control `InlineBlock` layout, a pure-C++20
  TrueType `.ttf` glyph rasterizer (`FontRenderer.cpp`), CLI XML/SVG document
  loading, `--render-png`, and `--gui [--port=8080]` (`WebBrowserServer`).

```bash
./build/litebird examples/usable_browser_demo/index.html --render-png /tmp/demo.png
./build/litebird https://api.github.com/zen
./build/litebird --gui --port=8080
```

Still open: `<iframe>` nested documents, WebP/AVIF images, `@font-face`/WOFF2
and HarfBuzz/bidi shaping, CORS preflight `OPTIONS`, and `ArrayBuffer`. Full
details are in `ARCHITECTURE.md`, Phase 36, and `AUDIT.md`, finding 44.


### Iterative RegExp VM, Modern CSS Cascade/Units, CSSOM `el.style`, and HTML5 Date/Time Inputs (Phase 37)

Phase 37 closes four cross-subsystem gaps (`AUDIT.md`, finding 42):

- **Iterative bytecode RegExp VM (`src/js/BuiltinsRegExp.cpp`):** Replaces the
  recursive CPS matcher and recursive AST destructor with an iterative bytecode
  VM, explicit heap backtracking stack (`kMaxMatchSteps = 30,000,000`), and
  iterative teardown (`regexp.dat` 39/39; `/(?:a|b)+/` on 120,000 chars and
  250+ nested groups execute cleanly without `RangeError`).
- **Modern CSS cascade, selectors, units & L4 media queries (`CssParser`,
  `StyleSheet`, `StyleResolver`):** Adds child/sibling combinators (`>`, `+`,
  `~`), `:is()` / `:where()` / selector-list `:not()` / `:host`, `@layer`
  statement/block ordering, `@supports` evaluation, Media Queries Level 4
  ranges (`<=`, `<`, `>=`, `>`) and `not`/`and`/`or`, `min()`/`max()`/`clamp()`,
  `dvh`/`dvw`/`vmax`, percentage `border-radius`, `:root` `rem` inheritance, and
  O(1) ancestor-depth pruning + `AllDescendant` DP prefix memoization (depth
  200,000 in `< 0.1 s`).
- **CSSOM `el.style` shorthand expansion & aliases (`DomStyleBindings.cpp`):**
  Expands `flex`, `overflow`, `inset`, `gap`, `borderRadius`, `background`,
  `transition`, `textDecoration`, `listStyle`, `outline`, `margin`/`padding`/
  `border` on inline styles, canonicalizes `cssText`/`length`/`item()`/
  `removeProperty`/`!important`/custom properties `--*`, and maps `-webkit-`/
  `-moz-`/`-ms-` aliases (26/27 checks on `tests/dom-conformance/open/inline-style.js`).
- **HTML5 date/time `<input>` semantics (`FormState`, `DomFormBindings`):**
  Value sanitization, `valueAsDate`, `valueAsNumber`, `stepUp()`/`stepDown()`,
  and `min`/`max`/`step` validity for `date`, `time`, `month`, `week`, and
  `datetime-local` (`6/6` date checks on `tests/dom-conformance/open/dates-and-svg.js`,
  `forms.dat` + `style.dat` bringing `make dom-conformance` to `644/644`).


### Media & Vector Rendering (Phase 38)

Phase 38 adds standalone zero-dependency C++20 decoders for ITU-T T.81
Baseline/Extended/Progressive JPEG (`JpegDecoder`) and single/multi-frame
GIF87a/GIF89a (`GifDecoder`), and upgrades `CanvasRenderingContext2D` and
`SvgRenderer` with CTM stacks, anti-aliased scanline filling (`nonzero`/
`evenodd`), thick capped/joined/dashed stroking, clipping masks, inverse-CTM
gradients, full SVG path/elliptical-arc grammar, `<defs>`/`<use>`/`<symbol>`/
`<clipPath>`, inline `style`, and opacity. `make verify-media` verifies 21 JPEG
variants pixel-by-pixel against `libjpeg` (52–99 dB PSNR), 9 GIF variants
bit-for-bit (`max_diff = 0`) against ImageMagick `-coalesce`, and 5 SVG feature
suites; `make fuzz-media` runs 1,800 mutated JPEG/GIF/SVG inputs through
`build/media_check`. Details and remaining gaps are in `ARCHITECTURE.md`,
Phase 38, and `AUDIT.md`, finding 43.


### `new.target`, and what a constructor makes (Phase 43)

`new.target` was the function that was running, where it is the function
`new` was applied to: an abstract class written the usual way (`if
(new.target === Shape) throw …`) refused its own subclasses, and inside `F`
`Reflect.construct(F, [], G)` did not name `G`. An object that a parent's
constructor returned was dropped by `super()`, so a class on top of a
singleton, a factory or a class compiled for older browsers went on with
another object. Both are as in Node now, also through a bound function, and
`this` is bound once: a second `super()`, a derived constructor that never
calls it and one that returns a number throw.

With the right `new.target` reaching the DOM's constructors, a custom
element compiled for older browsers works: `Reflect.construct(HTMLElement,
[], new.target)` makes the element for `new` and `document.createElement`,
and for an element that exists already (in the page's markup, from
`innerHTML`, a clone) the interface is given that element, as the standard's
construction stack has it. The same holds for a compiled subclass of a class
that a library ships.

```bash
./build/litebird --run-js 'class Shape { constructor() { if (new.target === Shape) throw new TypeError("abstract"); this.kind = new.target.name; } }
class Circle extends Shape {}
var one = {}; function Single() { return one; } class Child extends Single { constructor() { super(); this.added = 2; } }
console.log(new Circle().kind, new Child() === one, one.added)'
# Circle true 2        (before: Uncaught TypeError: abstract)
make js-conformance      # 974 / 989; construct.dat 47 / 49
make dom-conformance     # 669 / 669; compiled.dat 25 / 25
```

What is still missing: an upgrade still runs the constructor on the element
itself, where the standard hands the element over when the constructor asks
for it (a constructor written as a function can tell, and one that never
reaches `HTMLElement` is accepted); `this` can be used before `super()` has
run; `eval` inside a function sees nothing of the function; and a private
name is a property that `obj['#p']` reads. Those and what else was found on the way
are rows of the task table in `AGENTS.md`; the design and what it costs are
in `ARCHITECTURE.md`, Phase 43, and `AUDIT.md`, finding 49, says what found
what.

### The encodings pages are written in (Phase 47)

Every page and XML document used to be read as UTF-8, with UTF-16 and
Latin-1 accepted in an XML declaration: a file in Shift_JIS, EUC-KR or
windows-1252 arrived as `U+FFFD`s, `<meta charset>` was text nobody read,
and `document.characterSet` did not exist. Phase 47 implements the WHATWG
Encoding Standard: **40 encodings and their 228 labels**, generated from
`whatwg/encoding` (pinned in the header), with the index tables every byte
of them (`index-iso-8859-6`'s holes included), the Big5 pointers that stand
for two code points, `gb18030`'s four-byte ranges, BOM sniffing, and the
standard's two decoder modes — replacement (one `U+FFFD` per error, as a
page expects) and fatal (an error stops the decode, as XML expects).

An HTML page picks its encoding the way the standard says: the BOM, then
`Content-Type`, then a `<meta charset>` prescan of the first 1,024 bytes
(HTML §13.2.3.2), and `document.characterSet`, `document.charset` and
`document.inputEncoding` report what it found. XML validates the
declaration's `[81] EncName` before looking it up, refuses `replacement`,
and decodes the rest. The same tables serve `data:` URLs,
`XMLHttpRequest`, `<script src>`, `<iframe>` subdocuments and CSS
`@charset`. `--content-type=<mime>` (and a `<file>.headers` sidecar for
`--map-origin` files) lets the conformance runner hand a page the type a
server would, which is what the `encoding/` tests need.

Measured: the W3C XML suite's **1,718/1,718** (the 16 documents skipped
until now for their encoding are read, and each is still answered as the
suite says); web-platform-tests **7,968/8,009** across 103 files, of which
16 new `encoding/` files pass 2,543/2,543 subtests (2,547 more than before,
with `big5-decode-extra.html` alone at 2,472); unit suites 174 → 177. The
Fuji Xerox XML documents in EUC-JP, Shift_JIS, ISO-2022-JP and UTF-16 now
serialize to the same canonical bytes as their UTF-8 translation.
Details are in `ARCHITECTURE.md`, Phase 47, and `AUDIT.md`, finding 53.
