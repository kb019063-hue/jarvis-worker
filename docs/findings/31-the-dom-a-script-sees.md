# 31. ✅ The DOM a script sees

> Moved out of `AUDIT.md` — written there as a `## 31. ✅ The DOM a script sees` section.
> The text below is the section as it stood, unchanged.


The language had a scoreboard since Phase 23 and passed 840 of 845 cases.
The DOM had none, and Known gap 9 listed what was missing from memory. This
finding is what a scoreboard said: `tests/dom-conformance/`, 572 pages with
a script each and what the script prints in jsdom 31, an independent
implementation (ARCHITECTURE.md, Phase 27, section 11).

| | `main` before | Now |
| :--- | ---: | ---: |
| All 572 cases | 61 | 572 |
| `everyday.dat`: 68 snippets from *You Might Not Need jQuery*, written first and never edited | 13 | 68 |
| URL parsing and setters, the web-platform-tests data (1,188 cases) | 2 | 1,188 |

### What a script met

Each line is a script on a small page, run by `litebird --run-script`.

| Script | Before | Now |
| :--- | :--- | :--- |
| `document.querySelectorAll('div.card p').length` | `0`; `querySelector('p:nth-child(2)')` was `null` and the next line threw | `2` |
| `a.classList.add('on')` | `TypeError`: `classList` is undefined | the class is added |
| `p.innerHTML` for `<p>a &lt; b &amp; c</p>` | `a < b & c`: assigned back, it is other markup | `a &lt; b &amp; c` |
| `u.children`, `u.firstElementChild` | undefined | a live `HTMLCollection`; the element |
| `a.dataset.role = 'admin'`, `a.matches(...)`, `a.closest(...)` | `TypeError` | work |
| `new CustomEvent('x', {detail: 1})`, `e instanceof MouseEvent` | `ReferenceError` | work |
| `form.elements`, `new FormData(form)`, `input.validity` | undefined | work |
| `getComputedStyle(a)`, `a.style.length` | `ReferenceError` | work; `style` is a live declaration block |
| `document.querySelector('div[')` | no error, no result | `SyntaxError`, a `DOMException` |
| `document.body.appendChild(document)` | no error | `HierarchyRequestError` |
| `Object.getPrototypeOf(document.body) === HTMLBodyElement.prototype` | `ReferenceError`: there were no interfaces | `true` |
| `a.append('x')`, `a.remove()` | `TypeError`: not a function | work |
| `location.href` on a page loaded by the CLI | `ReferenceError` | the page's address |
| `template.content.cloneNode(true)` | `TypeError`: `content` is undefined | works |

The cause was one design decision: a node was a kind of value, and its
members were names compared in one function. Phase 27 replaced that; the
old code is gone, not extended.

### What the work found on the way

- **Addresses were cut out of strings.** On the page
  `https://example.test/app/page.html?tab=2#top`: `a.href = '?q=1'` gave
  `.../app/?q=1` (the file name lost), `'#x'` gave `.../app/#x`,
  `'//cdn.test/lib.js'` stayed on `example.test`, `a.protocol` of
  `mailto:ann@x.test` was `https:`, and `new URL('nope')` did not throw. A
  click on `<a href="#frag">` therefore looked like a move to another page
  and did nothing. There is one parser now, the URL Standard's
  (`src/net/UrlParser.cpp`).
- **Scripts ran after the whole page had been parsed.** The first script of
  a page saw all of it: `document.scripts.length` was the number of scripts
  on the page, so "the last script is me"
  (`scripts[scripts.length - 1]`), which embedded widgets use to find their
  own tag, found the wrong one. Scripts run where the parser stands now.
- **The shell ran every `<script>` whatever its type** (JSON data, a
  template, a module), four times over in four copies of the page-load
  code, and its pages had no address of their own to resolve a link
  against.
- **Five unit tests asserted what no browser does** and passed: a method
  taken off its object and called (`var f = el.focus; f()`),
  `dispatchEvent('click')` with a string, `focus()` on a detached or
  disabled control, `popstate` fired inside `history.back()` instead of
  after it, `observedAttributes` as a method. They were corrected, not
  kept.
- **jsdom is wrong in five places**, by the standards' text: `tabIndex` for
  a `tabindex` that is not a number, `formnovalidate`, `requestSubmit()` on
  a form outside a document, content inside `<select>`, and the order of a
  promise callback and a mutation observer's. The corpus does not record
  those answers, and a unit test asserts the standard's
  (`tests/dom-conformance/README.md`).

### What 557 of 557 hid

The corpus first had 557 cases, and Litebird passed all of them while two
things a page meets early were wrong. Nothing in the corpus asked, because
the cases were written by the hand that wrote the code:

| | At 557 of 557 | Now |
| :--- | :--- | :--- |
| `getComputedStyle(div).position`, `.opacity`, `.marginTop`, `.fontSize`, any property nobody declared | `""` | `static`, `1`, `0px`, `16px`: the inherited or initial value, from a table of 182 |
| `#p { color: red } p { color: blue }` on `<p id=p>` | blue: the last rule that matched | red: specificity, then position |
| `getComputedStyle(h1).fontSize`, `(b).fontWeight`, `(a).cursor` | `""` | `32px`, `700`, `pointer`: what HTML gives each element |
| `color: red`, `margin: 1em`, `width: calc(1px + 2em)` read back | `red`, `1em`, the text | `rgb(255, 0, 0)`, `16px`, `33px` |
| A range from 1 to 3 in a parent whose first child is removed | Still 1 to 3: one child too far | 0 to 2 |
| `surroundContents()` on `bcd` of the text `abcdef` | `aef<em>bcd</em>` | `a<em>bcd</em>ef` |
| A `NodeIterator` whose reference node is removed | Went on inside the removed subtree | Steps out of it |

They were found by running pages that were not in the corpus through jsdom
and Litebird side by side, which is now a tool:
`python3 tools/conformance/compare_with_jsdom.py page.html script.js`. Of
166 properties of a `<div>` nobody styled, three had an answer (`display`,
`visibility`, `pointer-events`); 151 agree with jsdom now. Of the 15
others, 12 are places where Litebird writes what browsers write and jsdom
does not (`border` is `0px none rgb(0, 0, 0)`), two are the default font,
where each engine has its own, and for `transform-origin` Litebird has no
answer. Fifteen cases were added (`style.dat`, `ranges.dat`), which is how
557 became 572.

The lesson is about the method. A corpus recorded from another
implementation cannot be bent, and it still only knows the questions its
author thought of. The comparison tool asks without knowing the answer,
and the next person to work on the DOM should start there, not at the
score.

### What is known to be wrong and is not fixed

The same comparison, run over forms, ranges, inline style and a few more
areas, found these. Each gives an answer, and the answer is wrong:

- **`DOMParser` with an XML type parses HTML.** `parseFromString('<a><b/>
  </a>', 'application/xml').documentElement.nodeName` is `HTML`. There is
  no XML parser. A script that reads a feed or an SVG file this way gets a
  tree, and not the one it asked for. (Fixed since: finding 34.)
- **Date and time inputs are text inputs.** `<input type="date"
  value="2026-02-30">` keeps that value; `valueAsDate` is null and setting
  it throws; `stepUp()` throws.
- **`el.style.flex = '1'` leaves `el.style.flexGrow` empty.** Only margin,
  padding and the border family are taken apart in `el.style` (the
  computed style takes apart ten more). `url(a.png)` is not rewritten as
  `url("a.png")`, nor `translate( 10px , 20px )` as `translate(10px,
  20px)`.
- **SVG elements have no SVG DOM**: `className` is a string, no `viewBox`.

`tests/dom-conformance/open/` has a page and a script for each, to run
through the tool.

### What the number does not say

572 of 572 is everything the corpus asks, and the corpus asks what jsdom
can answer. It has no layout, so no case reads a size or a position, and
in the CLI those are zero while scripts run. It loads one page, so no case
navigates. Nothing external is fetched. Style sheets are not objects
(`sheet.cssRules`). The list of what is known to be absent is in
ARCHITECTURE.md, Phase 27, section 12; it was made by asking for about
170 names and by the comparisons above, not by reading pages, so it is not
complete.

The bindings are 13,700 new lines that take raw pointers into a tree a
script can take apart while they run. `make asan` runs the whole corpus in
the sanitizer build for that reason: 572 of 572 there too, the 110 unit
suites, the URL data, both fuzzers and the deep pages, with no report from
either sanitizer. That is evidence about the paths the cases take. A page
that takes another path has not been run.

---
