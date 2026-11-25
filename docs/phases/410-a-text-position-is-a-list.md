# Phase 410: a text position is a list, not a length

## What a page saw

Phase 395 gave every `<length>` geometry attribute an `SVGAnimatedLength`. That
is right for `rect.x`, `circle.r` and `svg.width`. It is wrong for four
attributes on the SVG text elements, and the error is not cosmetic:

```js
var t = document.createElementNS(SVG_NS, 'text');
t.setAttribute('x', '10 20 30');   // three glyph-run positions
t.x.constructor.name;              // "SVGAnimatedLength"  — should be the list
t.x.baseVal.numberOfItems;         // undefined
t.x.baseVal.value;                 // 0                    — should not exist
```

SVG 2 §4.5.3 and §4.6.3 give `x`, `y`, `dx` and `dy` on `text` and `tspan` an
`SVGAnimatedLengthList` whose `baseVal`/`animVal` are `SVGLengthList`s, because
one attribute holds a list — one entry per glyph run. A single-length model
cannot represent one: the list read back as **0**, and writing to it replaced
all three positions with one.

Measured with `tools/conformance/verify_svg_length_lists.py`, which runs the
same page script in pinned headless Chromium and in Litebird:

| query | Chromium | before | after |
| :--- | :--- | :--- | :--- |
| `text.x.constructor.name` | `SVGAnimatedLengthList` | `SVGAnimatedLength` | `SVGAnimatedLengthList` |
| `text.x.baseVal.numberOfItems` | `3` | `undefined` | `3` |
| `text.x.baseVal[0].value` | `10` | `TypeError` | `10` |
| `getItem(1).value = 99` → attribute | `10 99 30` | — | `10 99 30` |
| `typeof text.dx` | `SVGAnimatedLengthList` | `undefined` | `SVGAnimatedLengthList` |

**3/28 probes matched Chromium before; 28/28 after.**

## The list is a live view, so a write-back is the whole attribute

The list is not a copy of the attribute and not a cache of parsed values. Every
member reads the attribute, changes what it read, and writes the whole attribute
back:

```cpp
struct SvgLengthListState : JsExotic {
    SvgLengthListValue read() const;              // parse the attribute
    void write(const SvgLengthListValue&) const;  // serialize it all
};
```

That is what makes `getItem(1)` an *item* of the list rather than a snapshot:
it is an `SVGLength` whose state names the same element, the same attribute and
its own index, so `text.x.baseVal.getItem(1).value = 99` re-serializes
`10 20 30` as `10 99 30`. An empty list removes the attribute rather than
setting it to `""`, which is what a browser does — `clear()` leaves
`hasAttribute('x')` false.

`x`, `y`, `dx` and `dy` are only on `text` and `tspan`. `textPath` is an
`SVGTextContentElement` and **not** an `SVGTextPositioningElement`: its
positions come from the path, and Chromium answers all four as `undefined`. The
interface chain was built to match — `SVGTextPositioningElement` sits between
`SVGTextContentElement` and `SVGTextElement`/`SVGTSpanElement`, and
`SVGTextPathElement` hangs off `SVGTextContentElement` directly.

## The second bug: `SVGLength.value` is in user units

The lists exposed it; it was not introduced here. §4.5.2 makes `value` the
member that **converts** and `valueInSpecifiedUnits` the member that does not.
Phase 395's `value` setter wrote its argument straight into the number:

```js
rect.setAttribute('x', '1cm');
rect.x.baseVal.value = 100;    // asks for 100 user units
rect.getAttribute('x');        // was "100cm"  — 37.795x too long
                               // now "2.64583cm", as Chromium answers
```

The conversion now goes through user units in both directions: `value` converts
in, and `valueInSpecifiedUnits` does not — which is exactly the distinction
`convertToSpecifiedUnits` already walked.

## What the list members do

| member | argument | out of range |
| :--- | :--- | :--- |
| `getItem`, `removeItem` | index | `IndexSizeError` |
| `insertItemBefore` | item, index | clamped to the end |
| `replaceItem` | item, index | `IndexSizeError` |
| `initialize`, `appendItem` | item | — |
| `clear` | — | — |
| `length`, `numberOfItems` | — | the same number |

An **indexed write takes an `SVGLength`**: `list[0] = other` puts `other`'s
value in place 0 and keeps the count, and `list[0] = 5` throws a `TypeError`
in sloppy code too, because the setter checks its argument type before the
range. That was measured, not assumed — the first version of this phase refused
indexed writes outright, which a browser only does inside strict-mode code, and
the differential caught it (26/27 at that point).

## Verification

- `make test` **463/463**, `tests/suites/410-text-positions-are-length-lists.cpp`
  passing on its first run; reverting `src/js/DomHtmlBindings.cpp` fails it at
  `:48` on `typeof SVGLengthList`
- the Chromium differential **28/28**, up from **3/28** on the base tree
- DOM conformance **678/678** (probe 68/68) — unchanged
- `make wpt` **9570/9824**, 254 listed, 0 unlisted — unchanged
- `make xml-conformance` **1718/1718** — unchanged
- `check_docs`, `check_numbers`, `check_suites` clean

## Still open

This phase builds the length lists. The rest of the SVG list types are still
absent, and each is a row of its own:

- `SVGAnimatedNumberList` — `text`'s `rotate`
- `SVGAnimatedPointList`/`SVGPointList` — `polygon`/`polyline` `points`
- `SVGAnimatedTransformList` — `transform`
- `SVGAnimatedLengthList`'s sibling members on the other text attributes:
  `textLength` (an `SVGAnimatedLength`) and `lengthAdjust` are not built
- `SVGTextContentElement`'s measurement members — `getComputedTextLength`,
  `getSubStringLength`, `getStartPositionOfChar` and the rest — are `undefined`
- and the four lists are read-only against **animation**: `animVal` answers the
  same entries as `baseVal`, as everywhere else in this engine, because nothing
  animates.
