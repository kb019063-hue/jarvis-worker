# Finding 68 — The realm of a `DOMException`, settled by measurement (Phase 62)

(Written on 17 November 2026 as a section of `AUDIT.md` in #63 and moved here, its text unchanged, when #63 was landed on 18 November. The phase is [`docs/phases/62-the-realm-of-a-domexception.md`](../phases/62-the-realm-of-a-domexception.md).)

**What was asked.** `tests/wpt/expected-failures.txt` lists
`domparsing/innerhtml-05.xhtml` as `test-checks-parent-global`: setting
`innerHTML` on a node of `window[0]`'s document from the page made the
exception come from the frame, and the test calls `assert_throws_dom` without
naming `window[0].DOMException`. It was claimed that following Web IDL to the
letter — the exception is made in the caller's realm — would turn the subtest
green with the same change that keeps the other listed failures true.

**What the probe found.** It does turn that subtest green, and it turns 240
others red. `dom/nodes/Document-createElement.html` (743 subtests, 0 listed
today) asks the opposite: for each of 80 invalid names it calls
`assert_throws_dom("InvalidCharacterError", win.DOMException, function() { doc.createElement(arg) })`
in an HTML, an XML and an XHTML document of a frame, and names the *frame's*
`DOMException` as the constructor it expects. Web IDL's own text is on the
same side: `throw_dom` runs inside the binding, whose current realm is the one
the interface object was made in, so for a node of `window[0]`'s document it is
`window[0]`'s realm — not the caller's. (The Web IDL note's `MathUtils` example
is about a method borrowed between realms with `.call()`; a node method with a
node receiver of another realm is the same case, and the exception is the
receiver's.) The two files cannot both pass, and browsers keep the realm of the
node: `createElement` on a frame's document throws the frame's `DOMException`
in Chrome, Firefox and Safari, and `innerhtml-05.xhtml` fails there too — it is
one of the 11 rows the suite lists as the test's, not the engine's.

**What changed.** Nothing in the engine. The candidate change (owner =
`caller_realm_or(realm)` in `make_dom_exception`) was written, run, and
reverted: it moved WPT from **7,998 / 8,009 (11 listed, 0 unlisted)** to
**7,759 / 8,009 (10 listed, 240 unlisted)** — 240 of the 743 subtests of
`dom/nodes/Document-createElement.html`, the failing `createElement` of the
first frame in each of three document kinds for each of 80 invalid names. The
row stays in `expected-failures.txt` with its reason, and `tests/wpt/README.md`
now says which rule the engine keeps and why both tests cannot hold.

**What is now pinned.** `tests/test_suite.cpp`,
`dom_exceptions_belong_to_the_realm_of_the_object_they_are_about`: a frame with
an XML document, and six statements — an invalid name through the frame's
`createElement` throws the frame's exception (name, code, prototype and
`instanceof` all checked); a non-well-formed XML fragment through the frame's
`innerHTML` does too; passing the node to a function of the frame's changes
nothing; and a node of the page's own still throws the page's, so the two
realms stay distinguishable. 202 of 202 unit suites pass.

**Still open.** `strict-write` (2 subtests) and `nodelist-iterator` (1) remain
the engine's, both claimed elsewhere: the first needs `JsExotic::set` to be able
to say "refused" so that `JsEval.cpp` can throw in strict mode, and the second
makes `NodeList.prototype[Symbol.iterator]` be `Array.prototype[Symbol.iterator]`
(PR #73). The seven `serializer-*` rows are the standard's and the tests', as
`tests/wpt/README.md` explains. A browser cannot be asked which realm it throws
in from this sandbox, so the claim that Chrome, Firefox and Safari keep the
node's realm is Web IDL's text and a reading of the two tests, not a run;
whoever has a browser to hand should put
`document.createElement('iframe')` in a page, reach into `contentDocument` and
compare `e.constructor` with both `DOMException`s.
