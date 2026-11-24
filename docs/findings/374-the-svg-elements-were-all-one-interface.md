# Finding 374: the SVG elements were all one interface, and none of them was a graphics element

## The symptom

A page that asked an SVG shape for its box got nothing:

```js
var g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
g.getBBox();            // TypeError: g.getBBox is not a function
g.constructor.name;     // "SVGElement"  -- should be "SVGGElement"
```

`<svg>` answered `"SVGSVGElement"` and everything else answered `"SVGElement"`.
`SVGGraphicsElement` was registered in the interface table and never built: no
element was ever an instance of it.

## Why it mattered beyond the name

`getBBox()` is not on `SVGElement`; SVG 2 puts it on `SVGGraphicsElement`
(§4.4.2). With no element inheriting that interface, the method did not exist on
anything. The same lookup also decided `viewBox`, which shipped on `<svg>` alone
because it was written as a member of `SVGSVGElement` — but SVG 2 does not give
that attribute to `SVGSVGElement`; it gives it to the `SVGFitToViewBox` mixin
(§4.6.9), which five interfaces include.

So one wrong table produced three separate missing answers, which is why they
are one finding rather than three.

## The trap: "give them all a viewBox"

The obvious way to make `<symbol viewBox="...">` work is to move the `viewBox`
getter up to `SVGElement`. That passes a naive test — the symbol now answers —
and it is wrong, because it also answers for `<g>`, `<rect>` and `<defs>`, which
have no such attribute in SVG 2 (§7.7 names the elements that do: those
establishing a viewport, plus `marker`, `pattern` and `view`). Chromium answers
`undefined` for those, and a page that branches on `el.viewBox` would start
taking the wrong branch.

The suite therefore asserts the set in both directions:

```cpp
assert(S("sym.constructor.name") == "SVGSymbolElement");
assert(S("typeof g.viewBox") == "undefined");     // and rect, and defs
assert(S("typeof mk.viewBox") == "object");       // marker does have one
```

A fix that hands `viewBox` to every SVG element fails the second line. That
matters more than the missing feature: the negative assertion is what keeps the
shortcut from being taken again later.

## What the references say, and where they disagree

Two references were available and neither is complete:

| | `symbol` | `marker` | `pattern` | `g` | `getBBox()` |
| :--- | :--- | :--- | :--- | :--- | :--- |
| jsdom 27 | `viewBox` | none | none | none | absent |
| SVG 2 §12.13.1, §13.6.9, §5.13.8 | `viewBox` | `viewBox` | `viewBox` | none | §4.4.2 |

jsdom follows an older draft for `marker` and `pattern`, and has no `getBBox()`
at all, so it cannot be the reference for the half that matters most. The oracle
used is the specification, quoted by section, and this is written down so that a
later agent who reruns the jsdom comparison does not "correct" the marker and
pattern back out.

## What was changed

- `svg_graphics_interface_for_tag` (in `src/js/DomBindings.cpp`): the tag to
  interface table for the rendered graphics elements, every one of them a child
  of `SVGGraphicsElement`.
- `setup_svg_fit_to_view_box`: `viewBox`, on the five interfaces that include
  `SVGFitToViewBox` and on no others.
- `setup_svg_graphics_element`: `getBBox()`, from the laid-out border box.
- `setup_svg_svg_element`: `createSVGRect()`, alongside the `viewBox` that moved.
- `SvgRectState::box` is now optional: a `viewBox` half writes its attribute
  back, whereas a `getBBox()` rectangle owns its four values and is writable.

## What this finding does not claim

`getBBox()` reads the element's laid-out border box, not its own geometry. Object
bounding boxes follow the painted box per SVG 2 §4.4.2, so for an element the
layout has given a box this is the right answer; for a shape whose size comes
from `d`/`r`/`cx` there is no separate SVG box model behind it yet, and the
answer is the box the layout produced. `getCTM()`, `getScreenCTM()` and
`preserveAspectRatio` remain unimplemented, and `getBBox` ignores its
`SVGBoundingBoxOptions` argument — the suite asserts `length === 0`, which is
what the IDL says, rather than pretending the options are read.
