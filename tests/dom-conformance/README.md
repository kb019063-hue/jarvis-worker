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

572 cases in 21 files. Litebird passes 572; the engine before the bindings
were rewritten (`main` at `6f08a70`) passes 61.

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
| `errors.dat` | Which exception each misuse throws, and what a page sees of an uncaught one |
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
| `open/dates-and-xml.html`, `.js` | Date and time inputs, `DOMParser` with an XML type, the SVG DOM. 10 lines, all differ |

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
   layout of any kind (every box is 0 by 0).
7. The code runs through `eval` in jsdom and as a script in Litebird. They
   agree except that a top-level `var` can be deleted in one and not in the
   other, and that code which throws at top level is refused by the recorder.
   To test an uncaught exception, throw it from a timer, a listener or a
   `<script>` in the page.
8. Add cases for what pages do, including things Litebird cannot do yet. A
   corpus that only contains passing cases measures nothing.
