# Phase 27 Architecture: The DOM a Script Sees

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 27 Architecture: The DOM a Script Sees` section.
> The text below is the section as it stood, unchanged.


### 1. Why this phase existed

Phase 23 rebuilt the language against a corpus recorded from Node.js and left
the DOM as it was (Known gaps, 9): `getElementById`, one-word selectors,
`appendChild`, `innerHTML`, events. A script that did
`document.querySelector('ul li')`, `el.classList.add(...)` or
`new CustomEvent(...)` stopped with an error on its first line, and most
scripts on real pages do one of those on their first line.

The reason was structural. A DOM node was a *kind of value*
(`JsType::DomNode`), and what it could do was a chain of name comparisons in
`JsValue::get_property`. Nothing was an object: there was no
`HTMLElement.prototype` to inherit from, extend or test with `instanceof`,
and every new member was another branch in one function. So the phase started the way Phase
23 did, with a scoreboard from outside (section 9), and then replaced the
bindings instead of adding branches.

### 2. Three layers

```
src/dom/, include/litebird/dom/      the tree: Node, Element, Document, attributes,
                                     selectors, form state, the serializer.
                                     Knows nothing about scripts.
src/js/Dom*Bindings.cpp              the interfaces of the DOM and HTML standards,
                                     over that tree. 13,700 lines, 14 files.
src/js/ (Heap, JsValue, JsOps)       the engine: what an object is.
```

**The tree** gained what the interfaces need and the old bindings did
without: an owner document on every node, attributes as an ordered list with
namespaces (`AttributeList.hpp`), a selector engine (`Selector.cpp`), the
values and dirty flags of form controls and constraint validation
(`FormState.cpp`), and the standard's fragment serializer (`Node.cpp`). It
tells whoever is listening what changed through `DomHooks`, a table of
function pointers (`Node.hpp`): children changed, an attribute changed, an
element was connected or disconnected. The bindings fill the table in; the
tree calls nothing in the bindings by name, and a tree with no interpreter
attached (the parser's corpus run, a fragment) pays for a null check.

**The engine** gained one thing: an object can carry native state and answer
for its own properties (`JsExotic` in `Heap.hpp`). That is what a
`NodeList` (indexed), a `DOMStringMap` (named), `document` and `window`
(named access) are. `JsType::DomNode` is gone.

### 3. One object per node (`DomBindings.cpp`)

`wrap(realm, node)` returns the node's object, creating it the first time.
The object's prototype is chosen from the node: `HTMLDivElement.prototype`
for a `<div>`, `HTMLUnknownElement.prototype` for a tag nobody knows, the
class of a custom element. From then on:

- The object owns the node (`JsObject::dom_node`, a `shared_ptr`). The node
  points back with a plain pointer (`Node::script_object`), cleared when the
  collector frees the object.
- **A node that has an object, its parent has one too.** Marking an object
  marks the objects of its parent, its children and its document
  (`trace_wrapper`). So holding any node of a tree keeps every object of the
  tree, and `el.myData = ...` is still there when the script finds `el`
  again by another route. The rule is kept by `wrap` (ancestors first, in a
  loop: a tree can be as deep as memory allows) and by a hook for when
  native code puts a node that has an object under one that has none.
- Objects that are not nodes (events, lists, style declarations, ranges)
  carry their C++ state as the object's `JsExotic`. A binding recognises
  what it was called on by the dynamic type of that state (`this_state<T>`),
  never by a property a script could set, and throws `Illegal invocation`
  otherwise: `Element.prototype.getAttribute.call({})` is a `TypeError`.

### 4. Interfaces (`DomBindings.hpp`)

An interface is a row in a table: its name, its parent, a function that puts
its members on a prototype, and a constructor or none (`InterfaceDef`,
`register_interface`). A `Realm`, one per interpreter, builds a prototype
and its constructor the first time either is asked for, parents first. The
window finds interfaces by name the same way, when a script first says
`HTMLElement`: an interpreter that never touches the DOM builds none of it,
and a page that uses ten interfaces builds ten.

Members are native functions made by `make_function`, defined as accessors
and methods with the attributes WebIDL gives them. `ElementMembers` covers
the bulk: a reflected string, boolean, integer, enumerated keyword or URL
attribute is one line. The constructors can be subclassed
(`class Card extends HTMLElement`), which is what custom elements need:
`construct_as_super` in the engine lets a native constructor return the
object the derived class is building.

### 5. What the files hold

| File | Interfaces |
| :--- | :--- |
| `DomNodeBindings.cpp` | `Node`, `CharacterData`, `Text`, `Comment`, `DocumentFragment`, `DocumentType`; insertion with the hierarchy checks, adoption, `cloneNode` |
| `DomElementBindings.cpp` | `Element`: attributes, `classList`, selectors, `innerHTML`/`outerHTML`/`insertAdjacent*`, shadow roots, `DOMRect` |
| `DomCollectionBindings.cpp` | `NodeList`, `HTMLCollection`, `DOMTokenList`, `NamedNodeMap`, `Attr`, `DOMStringMap`: live, indexed and named |
| `DomHtmlBindings.cpp` | `HTMLElement` and the HTML element interfaces: reflected attributes, links, images, tables, templates, dialogs, canvas |
| `DomFormBindings.cpp` | Form controls, `form.elements`, validation, submission and reset, `FormData`, focus |
| `DomEventBindings.cpp` | `EventTarget`, `Event` and 27 subclasses, dispatch, `on*` handlers, `AbortController` |
| `DomStyleBindings.cpp` | `CSSStyleDeclaration`: `el.style` as a live declaration block, and `getComputedStyle` with a cascade of its own (section 8) |
| `DomDocumentBindings.cpp` | `Document`, `DOMImplementation`, `DOMParser`, `XMLSerializer`, cookies, named access |
| `DomTraversalBindings.cpp` | `Range` and `Selection`, which follow the tree (section 9), `TreeWalker`, `NodeIterator`, `NodeFilter` |
| `DomObserverBindings.cpp` | `MutationObserver`, custom elements and their reactions |
| `DomFileBindings.cpp` | `Blob`, `File`, `FileReader` |
| `DomUrlBindings.cpp` | `URL`, `URLSearchParams` |
| `DomWindowBindings.cpp` | `Window`, `Location`, `History`, `Navigator`, `Storage`, `XMLHttpRequest`, `matchMedia`, error reporting, the load events |

### 6. When things run

**Custom element reactions** wait for the DOM call that caused them to
return, then run before the script goes on, which is what `[CEReactions]`
means in the standards: `el.setAttribute('a', 1); el.setAttribute('a', 2)`
gives two `attributeChangedCallback`s, each after its own call, never one in
the middle of the other. Every native DOM function runs inside a
`ReactionScope` (in `make_function`), which costs a counter, a comparison
and a `try` block when the queue is empty.

**Mutation observers** get their records in a microtask. There is one
microtask queue, the engine's, so a promise callback queued before a
mutation runs before the observer's callback.

**Events** are dispatched by `DomEventBindings.cpp` for scripts and for
native code alike (a hook: the shell's click reaches script listeners).
The path is computed first; listeners are objects or functions, with
`once`, `passive`, `capture` and `signal`; an exception in one is reported
to `window.onerror` and the next still runs. After the dispatch comes the
default action: a click toggles a checkbox (and puts it back if a listener
called `preventDefault()`), follows a label to its control, submits a
form, moves the page to a fragment.

**A page's scripts run while it is parsed.** `HtmlTreeBuilder` reports each
`<script>` when its end tag has been read (`set_script_handler`), and
`JsInterpreter::load_page()` runs it there, as `document.currentScript`,
after a microtask checkpoint. The script sees the page up to itself:
`document.scripts.length` in the first script is 1, and
`scripts[scripts.length - 1]` is the script that asks. What it does to the
tree is there for the rest of the parse; the parser keeps its own
references to the open elements and goes on inserting into them wherever
they now are, as the standard says. The CLI and the shell both load pages
this way. Then the document is `interactive`, `DOMContentLoaded` is fired,
and `load`.

### 7. URLs (`src/net/UrlParser.cpp`)

Addresses were resolved by cutting strings: `?q=1` against
`/app/page.html?tab=2` gave `/app/?q=1`, `//cdn.test/lib.js` stayed on the
page's host, `mailto:` was read as `https:`, and `new URL('nope')` did not
throw. `Url` is the URL Standard's state machine, with the state override
its setters use: special schemes, opaque paths, IPv4 and IPv6 hosts in every
spelling, the percent-encode sets, `file:` URLs, origins. Everything that
has an address goes through it: `a.href` and the parts of a link,
`location`, `form.action`, `baseURI` with `<base href>`, `pushState`, and
the `URL` object, whose `searchParams` is a second view of the same record.
The page's own address is normalised when the window is made.

Navigation is only within the page: assigning to `location.hash`, following
a link to a fragment, `location.href = '#x'`. Those change the address at
once and fire `popstate` and `hashchange` from a task. Anything else would
be loading another page, and does nothing.

### 8. The computed style (`DomStyleBindings.cpp`)

`getComputedStyle` has a cascade of its own. It is not the layout engine's
(`StyleResolver`), which resolves into the typed values layout needs: a
script wants text back, for every property there is, and wants it as the
standards write it.

For one element, `compute_block` collects the declarations that apply and
orders them by weight: what the HTML standard's Rendering section gives the
element (a heading is bold and `2em`, a list has `40px` of padding, a link
is underlined), then the rules of the document's `<style>` elements by
specificity and position (`SelectorList::matching_specificity`: ids,
classes, types; `:is()` and `:not()` count as their most specific argument,
`:where()` as nothing), then the style attribute, then the `!important`
ones of the same three. Shorthands are taken apart on the way, so
`overflow: hidden` answers for `overflowY` and `font: 12px/1.5 Arial` for
`fontSize`.

What a script reads is the computed value of that:

- a property nobody declared has its inherited value or, from a table of
  182, its initial one;
- a colour is `rgb()` or `rgba()`: names, hex, `hsl()`, `currentcolor`;
- a length is in px: `em` and `rem` through the font size, `pt`, `in`,
  `cm`, `mm`, `pc`, `q`, and `calc()` of those. A font size is the chain of
  its ancestors' (`2em` of `150%` of `larger` of `20px`), and so is
  `bolder`;
- a border or outline that is not drawn is `0px` wide, one that is drawn is
  a whole number of pixels;
- a shorthand is put together from the computed values of its parts.

Everything that goes up the tree (inheritance, the font size, the weight) is
a loop, and the root of the tree is found once for each question: asking
about an element 20,000 levels down takes 100 ms and no stack.

What it does not know is layout. `width: auto` and `50%` stay what they
are, where a browser gives the used value in px. Section 12 has the rest.

### 9. Ranges that follow the tree (`DomLive.hpp`)

A `Range` is two boundary points, each a node and an offset. The standard
says where each goes when the tree changes after the range was made: nodes
inserted before a boundary push it on, a boundary inside something that is
removed lands where that was, text that is edited or split takes its
boundaries along. A `NodeIterator` whose reference node is removed steps
out of what is going away. A selection is a range, so it follows too.

The realm keeps a weak list of the ranges and iterators script can still
reach. The places where the tree changes tell it: the `children_changed`
hook of the tree, `replace_data()` behind `insertData`/`deleteData`/
`replaceData`, `splitText()`, `normalize()`. With no range alive each of
those is one lookup. With some, a change costs a walk up from each boundary
(to learn whether it was inside what went away), which is why the list is
cleared of ranges the collector has freed as soon as one is met: a page
that made 100,000 ranges and kept one pays for the one.

`insertNode()` and `surroundContents()` are the standard's steps on top of
that: text is split at the boundary, and a node the range cuts through that
is not text is an `InvalidStateError`.

### 10. What it costs

Measured against the binary of `main` on one machine, a page with an empty
body and a script (milliseconds, the better of two runs):

| | `main` | Phase 27 |
| :--- | ---: | ---: |
| Create 20,000 elements, give each an id and text, append them | 70 | 66 |
| `getElementById` for each of the 20,000 | 5,640 | 16 |
| `textContent` of each of the 20,000 children | 15 | 14 |
| `getAttribute` 200,000 times | 122 | 100 |
| 20,000 clicks on an element with one listener | 102 | 155 |
| Remove the 20,000 children, last first | 97 | 84 |

A click is slower: it now builds an event object with a prototype, computes
the path, and runs the default action. A lookup by id was a walk over the
whole document; these bindings were slower still (7.4 s), which is how it
was noticed. `Document::get_element_by_id` now answers from an index, built
on the second lookup after the tree last changed and thrown away when it
changes again. The first lookup after a change still walks, so a script that
changes the page between lookups pays what it always did. Nothing else was
measured: there is no benchmark of the DOM in the repository, and these six
lines are not one.

`Element` grew by one pointer (`m_rare`, for what few elements have: a
namespace that is neither HTML, SVG nor MathML, a prefix). The binary is
7.9 MB, from 3.7 MB: the bindings are templates over lambdas, one
instantiation for each member.

### 11. How this is verified

**The scoreboard.** `tests/dom-conformance/` is 21 files, 572 cases. A case
is a page (`#html`), a script (`#code`) and what the script prints
(`#expect`). `tools/conformance/run_dom_conformance.py` loads the page with
`litebird --run-script`, which runs the script after the page's own, fires
the load events and runs timers to the end on the event loop's virtual
clock, without layout or painting. Every `#expect` was written by
`--record`, which runs the case in jsdom 31.0.0 (`jsdom_run.mjs`), and
`--reference` runs them all in jsdom again: CI does, so an expectation
cannot be bent. `everyday.dat` was written first, from *You Might Not Need
jQuery*, and is not edited.

jsdom is the reference, not the authority. Where it differs from the
standards (the corpus README names the places), the answer is not in the
corpus, and a unit test asserts the standard's:
`dom_follows_the_standard_where_jsdom_does_not`,
`computed_style_answers_for_every_property`, `ranges_follow_the_tree`.

**What the scoreboard did not see.** The corpus was written by the hand
that wrote the code, and a case nobody thought of protects nothing. At 557
of 557:

- `getComputedStyle(el).position` was `""`, and so was every other property
  nobody had declared: no case read one. `#id { color: red }` lost to a
  later `p { color: blue }`: no case had two rules for one element. A
  heading had no font size.
- A range kept its boundaries where they were put, whatever happened to the
  tree afterwards, and `surroundContents()` inside one text node put the
  wrapper after the text.

Both were found by running pages that were *not* in the corpus through
jsdom and Litebird side by side
(`tools/conformance/compare_with_jsdom.py`), and each became cases: seven
for the computed style, eight for ranges. The same comparison says what is
still different, and that list is section 12. It is the list of what was
looked at, not of what is wrong.

| Check | Result |
| :--- | :--- |
| DOM corpus (`make dom-conformance`) | 572 / 572; 61 on `main`. The everyday probe 68 / 68; 13 on `main` |
| The corpus in jsdom (`--reference`) | 572 / 572 |
| URL data of web-platform-tests (`make url-conformance`): 904 parsing cases, 284 setter cases | 873 and 277, 1150 of 1188; 0 and 2 on `main`, where a URL object had no `username`. All 1,188 since host names outside ASCII go through Unicode's UTS #46 |
| Unit tests (`make test`) | 110 / 110; seven are new (the URL parser, URLs in the DOM, page scripts, where jsdom is not the standard, lookups by id, the computed style, ranges), and five old ones asserted what no browser does and were corrected |
| html5lib tree construction (`make conformance`) | 1781 / 1792, as before |
| JavaScript corpus (`make js-conformance`) | 840 / 845, as before |
| `make diff-trees` | 4 × 1,500 random pages against parse5 8.1.1, 0 differ, as before |
| `make fuzz` (in the sanitizer build) | 400 parser mutations and 400 pipeline runs, 0 findings |
| `make deep` | 1,292 runs over 445 pages at 200,000 levels, 0 failed. The slowest takes 19 s of the 60 allowed; the same page takes 21 s with the binary of `main` |
| `make asan` (ASan + UBSan): unit tests, the three corpora, both fuzzers, the deep pages at 10,000 levels | 110 / 110; JavaScript 839 / 845, as before; DOM 572 / 572; URL 1,188 / 1,188; 400 + 400 runs; 842 runs over 295 pages, 0 failed. No report |

The DOM corpus has a job of its own in CI (the reference check, the
scoreboard with a floor of 572, the URL data with a floor of 1188), and
`make asan` runs both corpora in the sanitizer build.

### 12. What is still open here

- **The corpus measures what jsdom does.** jsdom lays nothing out and loads
  no second page, so nothing checks geometry, scrolling or navigation. In
  the CLI the layout is made once, after the scripts, so
  `getBoundingClientRect()` and `offsetWidth` are zero while they run.
- **The computed style is computed, not used.** `width: auto` and `50%`
  stay; a browser gives px. A value of several parts is left as declared
  (`box-shadow: 0 0 1em red` keeps its `em`), `var()` is not substituted,
  and the shorthands that are not taken apart (`transition`, `animation`,
  `grid`, `background` beyond its colour) leave their parts `""`: not
  known, which is better than an initial value that would be wrong. The
  defaults are the HTML standard's, and the layout engine paints with a
  sheet of its own: a heading is `2em` to a script and 28px on the screen.
- **`el.style` keeps most shorthands whole.** `style.flex = '1'` leaves
  `style.flexGrow` empty; only margin, padding and the border family are
  taken apart. `length` and `item()` count a shorthand once where a browser
  lists its longhands. Values are not rewritten the way CSSOM writes them:
  `url(a.png)` stays unquoted, `translate( 10px , 20px )` keeps its spaces,
  `webkitTransform` is not `transform`.
  `tests/dom-conformance/open/inline-style.js` shows each.
- **`DOMParser` knows HTML only.** `parseFromString(text,
  'application/xml')` or `'image/svg+xml'` parses the text as HTML: there
  is no XML parser, so no XML document, no `parsererror`, no `responseXML`.
  The answer is wrong and nothing says so. (Phase 28 wrote the parser.)
- **Date and time inputs are text inputs.** `type="date"`, `time`,
  `month`, `week` and `datetime-local` keep whatever is assigned, and have
  no `valueAsDate`, `valueAsNumber` or `stepUp()`. `number` and `range`
  have all of it.
- **SVG elements have no SVG DOM.** They are `SVGElement` with the members
  of `Element`: `className` is a string, there is no `viewBox`, no
  `getBBox()`. `tests/dom-conformance/open/dates-and-svg.js` shows the
  last two; until Phase 28 it showed all three.
- **Nothing is fetched.** A `<script src>` or a stylesheet link is not
  loaded (a script inserted with a `src` gets an `error` event).
  `document.write` does nothing.
- **Style sheets are not objects.** `document.styleSheets` lists the
  `<style>` elements and nothing more: no `cssRules`, no `insertRule`. A
  script that writes its rules that way changes nothing; one that appends
  a `<style>` element is fine.
- **Absent**: `TextEncoder`/`TextDecoder`, `crypto`, `indexedDB`,
  `BroadcastChannel`, `MessageChannel`, `Request`, `EventSource`,
  `document.evaluate` (XPath), Web Animations (`el.animate`),
  `ElementInternals`, constructable stylesheets, the clipboard, the ARIA
  reflections (`el.ariaLabel`), and whatever needs typed arrays
  (`Blob.arrayBuffer()`, `FileReader.readAsArrayBuffer`).
- **The parser upgrades a custom element when it is connected** instead of
  constructing it before it is inserted. The element and its callbacks are
  the same; a mutation observer hears of the insertion one callback earlier
  than in a browser.
- **A worker has no DOM**, as in a browser, but its `window`-less global
  still comes from the code that was there before this phase.

---
