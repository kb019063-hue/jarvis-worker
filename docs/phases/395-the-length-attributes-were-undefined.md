# Phase 395: the length-valued SVG attributes were `undefined`

## What a page saw

After Phases 374 and 381, the SVG elements had their interfaces, `viewBox` and
`preserveAspectRatio`. The largest remaining block was the geometry: every
attribute that takes a `<length>` was simply not there.

```js
var r = document.createElementNS(SVG_NS, 'rect');
r.setAttribute('x', '10');
r.x;            // undefined
typeof SVGLength;          // undefined
typeof SVGAnimatedLength;  // undefined
```

SVG 2 gives each of these an `SVGAnimatedLength` whose `baseVal`/`animVal` are
`SVGLength` objects. A page could read the attribute as a string and nothing
else — no `unitType`, no conversion, no way to change a value without
rewriting the whole attribute.

| Element | Length attributes |
| :--- | :--- |
| `rect` | `x`, `y`, `width`, `height`, `rx`, `ry` |
| `circle` | `cx`, `cy`, `r` |
| `ellipse` | `cx`, `cy`, `rx`, `ry` |
| `line` | `x1`, `y1`, `x2`, `y2` |
| `svg` | `x`, `y`, `width`, `height` |
| `text`, `tspan` | `x`, `y` |
| `marker` | `refX`, `refY`, `markerWidth`, `markerHeight` |
| `image`, `use`, `foreignObject`, `pattern`, `filter`, `mask`, `feImage` | `x`, `y`, `width`, `height` |

The negative half is asserted too: `rect.cx`, `circle.rx`, `line.cx`, `g.x`,
`path.x` and `text.width` answer `undefined`, because SVG 2 gives those
elements no such attribute.

## Why the renderer's parser could not be reused

`SvgRenderer::parse_float_value` already parses lengths — and resolves them to
pixels immediately, because that is what painting needs. `SVGLength` must not do
that. `unitType` and `valueInSpecifiedUnits` are observations of what was
**written**, and only `value` converts:

```js
r.setAttribute('height', '5mm');
r.height.baseVal.unitType;               // 7  — SVG_LENGTHTYPE_MM
r.height.baseVal.valueInSpecifiedUnits;  // 5  — the number as written
r.height.baseVal.value;                  // 18.898… — the same length in user units
```

A type that converted on the way in could answer only the third. So the phase
adds a unit-preserving parser (`parse_svg_length`) next to the `viewBox` and
`preserveAspectRatio` parsers, sharing `svg_space`/`svg_digit`/
`svg_decimal_float` with them, and leaves the renderer untouched.

## Two bugs made and caught here

**1. A non-scalar value was flattened to empty.** A unit the constants do not
name — `3ch`, or a `calc()` — reads `unitType` UNKNOWN and
`valueInSpecifiedUnits` 0 (§4.5.2), which the value type modelled. But writing
it back serialized `""`, so `setAttribute('x', '3ch')` destroyed the attribute
and the next read answered `0` NUMBER. §4.5.2 says a non-scalar is *readable*
this way, not that it is erasable. `SvgLengthValue` now keeps the source text
for a non-scalar and `serialize_svg_length` returns it, so a value the DOM can
read but not express survives the round trip.

**2. `e` was taken as an exponent marker inside `em`/`ex`.** The number scanner
accepted a trailing `e` and then required digits, so `7em` failed to parse and
`valueAsString = '7em'` threw `SyntaxError` — while `1cm`, `2mm`, `3in` and
every other unit worked, which is why it looked fine at first glance. The
scanner now looks ahead: `e` starts an exponent only when digits (after an
optional sign) follow it. All eleven units then reach their constant, which the
suite asserts as one sequence:

```cpp
assert(S("var seq=[];['7em','7ex','3ch','1cm','2mm','3in','4pt','5pc','6px','50%','12'].forEach(function(t){"
         "rect.setAttribute('x',t);seq.push(rect.x.baseVal.unitType);});seq.join(',')") == "3,4,0,6,7,8,9,10,5,2,1");
```

## The change

`src/js/DomHtmlBindings.cpp` only:

- `SvgLengthUnit`, `SvgLengthValue`, `svg_length_unit_in_px` (the CSS table the
  renderer uses), `svg_length_unit_suffix`, `parse_svg_length`,
  `serialize_svg_length`, `svg_length_in_user_units`.
- `SvgLengthState` — either reflects an element attribute or owns its value, the
  same optional-backing shape `SvgRectState` got in Phase 374. `animVal` is the
  same state with `animated` set, which is what makes its setters throw
  `NoModificationAllowedError`.
- `setup_svg_length` — the 11 constants, `unitType`, `value`,
  `valueInSpecifiedUnits`, `valueAsString`, `newValueSpecifiedUnits`,
  `convertToSpecifiedUnits`.
- `setup_svg_animated_length` — `baseVal`/`animVal`, cached.
- `setup_svg_length_attributes` — the getters, installed on each interface and
  gated by `svg_length_attribute(tag, name)`, so the element decides.
  `SVGSVGElement` and `SVGMarkerElement` call it from their own setups.

`convertToSpecifiedUnits` converts through user units: `5mm` → `0.5cm`, `1in` →
`72pt`. A percentage has no absolute length, so it converts through the
unassociated basis of 100 user units that §4.5.2 gives an object with no
element.

## Verification

- `make test` — **451 suites pass**, including
  `tests/suites/395-svg-lengths-are-objects-not-numbers.cpp`: the interfaces
  and `[SameObject]`, the positive and negative attribute sets, all 11
  constants, the initial value, unit preservation with conversion, all eleven
  units, the non-scalar case both ways, every setter, both methods with their
  `NotSupportedError`, `animVal`'s five refusals, and independence between two
  elements. **Reverting `src/js/DomHtmlBindings.cpp` fails it at `:57`.**
- DOM conformance **678/678** — unchanged.
- `make wpt` **9570/9824, 254 listed, 0 unlisted** — unchanged.
- `make xml-conformance` **1718/1718** — unchanged.

## Still open

- `SVGSVGElement.createSVGLength()`.
- Length *lists* (`x`, `y`, `dx`, `dy` on text are lists in SVG 2, and
  `SVGLengthList`/`SVGPointList`/`SVGTransformList` do not exist here).
- `getCTM()`/`getScreenCTM()`, SVG animation, and the remaining animated types
  (`SVGAnimatedNumber`, `SVGAnimatedEnumeration`, `SVGAnimatedString` beyond
  `className`).
- `em`/`ex` resolve at the initial 16px/8px font size rather than the element's
  computed font, so a length written in `em` inside a 10px element reports the
  wrong user-unit value.
