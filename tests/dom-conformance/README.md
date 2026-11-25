# DOM conformance corpus

Pages and scripts with the exact output a reference DOM gives. This is the
scoreboard for the DOM a script sees, beside `tests/js-conformance/` (the
language) and `tests/html5lib/tree-construction/` (the parser): it measures
the engine against something other than its own tests.

```bash
make dom-conformance                                           # per-file pass rate
python3 tools/conformance/run_dom_conformance.py --show 20 --filter forms
python3 tools/conformance/run_dom_conformance.py --case "radio" --show 5
python3 tools/conformance/run_dom_conformance.py --reference   # check the corpus itself
python3 tools/conformance/compare_with_jsdom.py page.html script.js   # outside the corpus: where do jsdom and Litebird differ?
```

678 cases in 26 files, and Litebird passes all of them. Of the 572 that
Phase 27 wrote, the engine before it (`main` at `6f08a70`) passes 61; of the
57 that Phase 28 wrote (`xml.dat`, `xhr.dat`), the engine before it passes
none; of the 11 that Phase 34 wrote (`interfaces.dat`), the engine before it
passes none; of the 25 that Phase 43 wrote (`compiled.dat`), the engine
before it passes 2; of the 9 that Phase 69 wrote (`upgrades.dat`), the engine
before it passes 2.

## Where the expectations come from

Every `#expect` section was written by `--record`, which runs the case in
[jsdom](https://github.com/jsdom/jsdom) 31.0.0 and stores what it prints.
jsdom is an independent implementation of the DOM and HTML standards in
JavaScript; none of its code is in Litebird. `--reference` runs the whole
corpus in jsdom again and fails on any difference, so an expectation cannot
be bent to match what Litebird happens to print. CI runs that check.

The version is pinned in the runner (`JSDOM_VERSION`), because expectations
are only comparable when everybody records with the same one. Both commands
install it into `build/jsdom` from the npm registry the first time.

## Where jsdom is not the standard

jsdom is the reference, not the authority. Where it differs from the HTML and
DOM standards (and from browsers), the corpus does not record its answer: the
case is written so that it does not depend on the point, and the standard's
behaviour is asserted in `tests/test_suite.cpp` instead. Known so far:

| Point | The standard | jsdom 31 |
| :--- | :--- | :--- |
| `tabIndex` of a `div` whose `tabindex` attribute is not a number | `-1` (the element's default) | `0` |
| A submit button with `formnovalidate` on an invalid form | The form is submitted without validation | Validation still blocks it |
| `form.requestSubmit()` on a form that is not in a document | Nothing happens | `submit` is fired |
| `<p>`, `<div>` and other content inside `<select>` | Kept, since the "in select" parsing mode was removed (html5lib's tests agree) | Dropped |
| A promise callback queued before a mutation is observed | Runs before the observer's callback (one microtask queue, in order) | Runs after it |
| `getComputedStyle(document.body).marginTop` | `8px` | `0px` |
| `line-height: 200%`, computed | A length, here and where it is inherited | `200%` |
| `outline-width` of an outline that is not drawn; `word-spacing` | `0px` | `3px`; `normal` |
| Computed shorthands: `border`, `outline`, `text-decoration`, `list-style`, `flex-flow`, `border-spacing`, `box-shadow` | Whole: `0px none rgb(0, 0, 0)`, `none solid rgb(0, 0, 0)`, `outside none disc`, `row nowrap`, `0px 0px`, `none` | Parts left out, or `currentcolor none` |
| `display` of a `<textarea>` | `inline-block` | `inline` |
| A range boundary in a text node that `normalize()` merges away | Moves into the merged node, at the same character | Left on the parent |
| `selection.containsNode(node, true)` for a node before the selection | `false` | `true` |

The first five rows are asserted in `dom_follows_the_standard_where_jsdom_does_not`,
the computed style in `computed_style_answers_for_every_property`, the last
two in `ranges_follow_the_tree`.

XML documents and the serializer (`xml_dom_follows_the_standard_where_jsdom_does_not`):

| Point | The standard | jsdom 31 |
| :--- | :--- | :--- |
| `innerHTML` of an HTML element that holds a processing instruction | `<?target data>` | Left out |
| `document.title = 'T'` when the root is an empty `<svg>` | A `<title>` as the child of the root | Put after the root: a document with two elements, and `document.title` is still empty |
| An XHTML DOCTYPE with one of the public identifiers the HTML standard lists | Declares the entity names of HTML: `&nbsp;` is a character | `parsererror` |
| The clone of a document `DOMParser` made from XML | A `Document` | An `XMLDocument` |
| `document.dir`, `designMode` of a document `DOMParser` made from XML | `""` and `off`, as on every `Document` | `undefined`, and no `designMode` |
| `activeElement` of a document with no body | The document element | `null` |
| `visibilityState` of a document a script made | `hidden` | `prerender` |
| An attribute with a prefix, serialized | Keeps its prefix when nothing else has it (the standard since September 2026) | A made-up `ns1` |
| A tab in an attribute value, serialized | `&#9;` | `&#x9;` |
| A DOCTYPE identifier that holds a double quote, serialized | In single quotes | In double quotes: not well formed |

One more difference is not the standard's side against jsdom's. Where an
attribute needs a made-up prefix and the element already declares `ns1`, the
standard's text writes `xmlns:ns1` twice (its issue 44, open), and so does
jsdom; no XML parser reads that. Litebird takes the next free prefix.
`tests/wpt/README.md` has this and the other places where the serializer
leaves the text so that what it writes can be read back.

`XMLHttpRequest` (`xml_http_request_answers_data_urls_and_nothing_else`):

| Point | The standard | jsdom 31 |
| :--- | :--- | :--- |
| `responseText` of an XML response with no charset on its type | In the encoding the document declares (`encoding="ISO-8859-1"`) | UTF-8 always |
| An XML response whose bytes are not text in its encoding, or whose declaration contradicts them | `responseXML` is null: XML calls it a fatal error (4.3.3) | U+FFFD in their place, and a document |
| A charset nobody knows on the type of an XML response | No charset: the declaration decides | UTF-8 |
| `overrideMimeType('text/xml;charset=bogus')` on a response with a charset | The override's charset replaces the response's and then names nothing | The response's charset is used |
| `responseXML.contentType` | The type of the response (`text/xml`, `image/svg+xml`) | `application/xml` always |
| `responseURL` of an address with a fragment | Without the fragment | With it |
| `getAllResponseHeaders()` | Every line ends in CR LF | Not the last |
| An HTML response, `responseType = 'document'` | Parsed with scripting off: a `<noscript>` in the head holds elements. An empty one is a document too | Parsed with scripting on; an empty one is null |
| A synchronous request | `readystatechange`, `load`, `loadend`; the total is 0 with no `Content-Length` | A `progress` event as well, and the total is what was loaded |
| `abort()` while the response is arriving | `readystatechange`, `abort`, `loadend`, and nothing after | One more `progress` |
| `Object.getPrototypeOf(EventTarget) === Function.prototype`, `HTMLDivElement instanceof Function`, `Object.getPrototypeOf(DOMException) === Function.prototype` | `true`, all three: an interface object is a function of the realm its script runs in | `false`, all three: jsdom's interface objects are made in Node's realm and the script runs in another, so they are functions whose `Function.prototype` is not the script's. `interfaces.dat` asks for `typeof` and for the walk from one interface object to the next, which are the same in both |
| `getComputedStyle(el).width` and `height` of a box with a width of its own | The **used** value (CSSOM "resolved values"): the box the layout made, 784px for `width:auto` in a 784px block and the wrapped height for `height:auto` | The declared value or `auto`, since jsdom lays nothing out (every box is 0 by 0). The cases here ask these two only where the answer is the declaration — `display:none`, a non-replaced inline box, and a box with no padding or border — and the used value itself is asserted in `tests/test_suite.cpp` (`computed_width_and_height_are_used_values`) |

Interfaces constructed with a `new.target` (`js_compiled_custom_elements_are_constructed`):

| Point | The standard | jsdom 31 |
| :--- | :--- | :--- |
| `Reflect.construct(Event, ['z'], NewTarget)`: how often `NewTarget.prototype` is read | Once, when the object is made (Web IDL) | Twice |
| The same when `NewTarget.prototype` is not an object | An event with `Event.prototype`: the interface's own prototype is the fallback | Something that is not an event: `instanceof Event` is `false` and `type` is `undefined` |

Do not "fix" a failing case by recording such an answer. If Litebird and jsdom
disagree, read the standard first.

## How a case runs

A case is a page and a script.

- The page is the case's `#html`, or else the file's `#fixture`, or else an
  empty page with a title. A doctype is added when the page has none. It is
  loaded from `https://example.test/app/page.html?tab=2#top`.
- The page's own `<script>` elements run first.
- The `#code` then runs the way a deferred script does: the tree is complete,
  `document.readyState` is `"interactive"`, and `DOMContentLoaded` and `load`
  are still to come.
- The page finishes loading, and runs until it has no timer left.

In Litebird that is `litebird --run-script script.js --url=URL page.html`,
with timers on a virtual clock. In jsdom it is
`tools/conformance/jsdom_run.mjs`, with real timers.

A case passes when Litebird prints exactly the expected output and does not
crash, run out of time or print a sanitizer report. An uncaught exception is
not a failure in itself (some cases are about what a page sees after one): a
case that dies early fails because its output is cut short.

## Files

| File | What it covers |
| :--- | :--- |
| `everyday.dat` | The fixed probe the headline number is reported on: the snippets of [You Might Not Need jQuery](https://github.com/HubSpot/YouMightNotNeedjQuery) (MIT), one case per entry that touches the DOM, each made to print what it did. **Do not edit this file**; add cases elsewhere. |
| `node.dat`, `text.dat`, `element.dat` | The tree: navigation, insertion and removal, cloning, comparison, character data, which interface an element gets |
| `attributes.dat`, `classlist.dat`, `dataset.dat`, `style.dat` | Attributes and the objects that are views of them; `getComputedStyle` |
| `ranges.dat` | `Range` and the selection, and how they follow a tree that changes; `NodeIterator` under removal |
| `selectors.dat`, `collections.dat` | `querySelector`, `matches`, `closest`; live and static lists |
| `markup.dat`, `mutation.dat` | `innerHTML` and its relatives in both directions; `MutationObserver` |
| `document.dat`, `window.dat`, `loading.dat` | The document, the global object, location, history, storage, timers, the load events |
| `events.dat`, `forms.dat` | Dispatch, the event classes, handlers; form controls, their state, validation and submission |
| `components.dat` | Custom elements and shadow trees |
| `compiled.dat` | Custom elements and other interfaces built without `class`: a constructor written for ES5 that gets its element from `Reflect.construct(HTMLElement, [], new.target)`, what Babel emits (`_wrapNativeSuper`, `_createSuper`) two classes deep, a class on top of a compiled constructor, a compiled subclass of a class that extends `HTMLElement`, a customized built-in. Made by `new` and `createElement`, and upgraded where they stand: in the page's markup, from `innerHTML`, by `cloneNode`, by `customElements.upgrade`. The element is given out once: what happens when a constructor asks twice, when a second instance of the class is made while it is being upgraded, and when the interface that asks is not the element's (a customized built-in that asks `HTMLElement`, a class that extends `Event`, and classes that extend `Map`, `Error`, `Array`, `Text` and `DocumentFragment`: the element's object is not made an event, a map or a text node). A constructor that reaches its interface from the body of a generator or of an async function, which run on threads of their own here. `Reflect.construct` of `Event`, `CustomEvent` and `EventTarget` with a `new.target`, and `new.target` in the parent class of a custom element |
| `upgrades.dat` | What an upgrade's construction is: the constructor runs on an object of its own, `this` is unbound until `super()`, `super()` hands back the element and a second `super()` is a `TypeError`, a field initializer lands on the element, the element's object is the one a script held before the definition existed, and a constructor that throws, never reaches the interface, or returns something else leaves the element not `:defined` (a customized built-in included) with the prototype the interface gave it still on it |
| `interfaces.dat` | The interface objects as functions with prototypes of their own: `Object.getPrototypeOf(HTMLDivElement)` is `HTMLElement`, then `Element`, `Node`, `EventTarget`; the same for events and for nodes that are not elements; constants inherited along the way (`HTMLElement.ELEMENT_NODE`); a class and an ES5 constructor that extend an interface |
| `errors.dat` | Which exception each misuse throws, and what a page sees of an uncaught one |
| `xml.dat` | `DOMParser` with the XML types and what it gives when the text is not well formed, namespaces, CDATA sections and processing instructions, `createDocument`, `XMLSerializer`, `innerHTML` in an XML document, which names are names |
| `xhr.dat` | `XMLHttpRequest` for `data:` URLs: states and events, `responseXML`, `responseType`, `overrideMimeType`, encodings, what may be called when, `abort()` |
| `programs.dat` | Small pages that do a whole job: a todo list, tabs, a sortable table, a router |

## Outside the corpus

A full score says that nothing is known to be wrong among what the cases
ask. It says nothing about what no case asks, and the cases were written by
whoever wrote the code. At 557 of 557, `getComputedStyle` answered `""` for
every property nobody had declared and a range did not follow the tree.

`tools/conformance/compare_with_jsdom.py` runs any page and any script in
both engines and prints the lines that differ. Use it on things the corpus
does not have: a page from the web cut down to what a script does to it, an
interface member by member, a library's own example. A difference is a
question (read the standard: jsdom is wrong too), and when Litebird is the
one that is wrong the script becomes cases here.

`open/` holds pages and scripts for differences that are known and not
fixed. They are not cases and the runner does not read them:

| Files | What they show |
| :--- | :--- |
| `open/inline-style.html`, `.js` | `el.style`: shorthands kept whole, `length` and `item()`, values not rewritten in CSSOM's form. 27 lines, 15 differ; a few of those are jsdom's (it drops the quotes of a font family) |
| `open/dates-and-svg.html`, `.js` | Date and time inputs, the SVG DOM. 20 lines, 11 the same (Phase 374 added the five SVG-interface lines, Phase 381 the four `preserveAspectRatio` ones, Phase 395 the three length ones and Phase 410 the length-list one; of the nine that still differ, four are jsdom following an older SVG 2 draft for `marker`/`pattern`/`view` `viewBox` and having no `getBBox()`, one is jsdom's `animVal` being a writable object where §8.14.4 makes it read-only, and four are the SVG length types jsdom does not have at all — the specification and Chromium, not jsdom, are the reference for all nine). **jsdom has no `SVGAnimatedLength`, `SVGLength` or any of the SVG list types** (Phase 395's three lines throw `ReferenceError` there and answer `undefined` for every length-valued attribute, and Phase 410's line throws `TypeError`), so those have no counterpart to compare against; the reference for them is pinned headless Chromium, in `tools/conformance/verify_svg_length_lists.py`.) (Until Phase 28 the file was `dates-and-xml` and had three lines more, for `DOMParser` with an XML type; those are cases in `xml.dat` now) |

```bash
python3 tools/conformance/compare_with_jsdom.py tests/dom-conformance/open/inline-style.html tests/dom-conformance/open/inline-style.js
```

## Format

```
#fixture
<the page of every case in this file that has no #html>

#name <short title, unique within the file>
#html
<the page of this case>
#code
<one or more lines of JavaScript>
#expect
<exact standard output>
```

`#fixture` is optional and comes once, before the first case. `#html` is
optional. A case starts at `#name`. Blank lines inside a section belong to it;
cases may be separated by a blank line. A line of markup or code cannot start
with `#name `, and cannot be exactly `#html`, `#code` or `#expect`.

## Rules for new cases

1. Never type an expectation. Write the case with an empty `#expect` (or none)
   and run `--record`; then read what was recorded. If it is not what a
   browser does, the case does not belong here (rule 6).
2. Print only primitives, with `console.log`. An object or a node is formatted
   differently by every engine; the recorder refuses such a case. Print
   `el.id`, `list.length`, `nodes.map(...).join()`, `JSON.stringify(data)`.
3. Never print the message of an error the engine made. Print `e.name`, or
   test with `indexOf`; the wording is not standardised. For the same reason
   do not print the `message` an `error` event carries for an uncaught
   exception: browsers put `Uncaught Error: ` in front, jsdom does not.
4. Let every timer end. A case with an interval nobody clears is refused, and
   so is one that prints from two timers whose order depends on how busy the
   machine is. `--reference` must give the same answer every time: after
   adding timing cases, run it ten times.
5. Do not print sizes of the window or the screen, or anything else that is a
   setting of the environment and not behaviour.
6. jsdom is the reference, not the standard. Where it is known to differ from
   browsers, leave the subject out instead of recording the difference. Known
   so far: `innerText`, named access on a form (`form.fieldName`),
   `history.state` is not a copy, `document.domain`, transient observers on a
   removed subtree, the `unhandledrejection` event, `structuredClone`,
   `scrollTo`, `formAction`, when a link click changes `location.hash`, and
   layout of any kind (every box is 0 by 0). And a request for anything
   but a `data:` URL: jsdom sends it over the network, and the recorder
   then records nothing for the whole case.
7. The code runs through `eval` in jsdom and as a script in Litebird. They
   agree except that a top-level `var` can be deleted in one and not in the
   other, and that code which throws at top level is refused by the recorder.
   To test an uncaught exception, throw it from a timer, a listener or a
   `<script>` in the page.
8. Add cases for what pages do, including things Litebird cannot do yet. A
   corpus that only contains passing cases measures nothing.
