# Phase 374: every SVG element was an SVGElement

## What a page saw

The engine knew four SVG element interfaces and built two of them:

```
svg_interface_for_tag:  "svg" -> SVGSVGElement, "title" -> SVGTitleElement,
                        everything else -> SVGElement
```

So `<g>`, `<rect>`, `<symbol>`, `<marker>` and `<pattern>` were the same
interface to a script. Three consequences, each of them something a page asks
for and got `undefined`:

| Asked | Before | SVG 2 |
| :--- | :--- | :--- |
| `document.createElementNS(SVG_NS, 'g').getBBox()` | `TypeError: not a function` | an `SVGRect` (§4.4.2) |
| `svg.createSVGRect()` | `TypeError: not a function` | an empty `SVGRect` (§5.13.2) |
| `<symbol viewBox="1 2 3 4">`.viewBox | `undefined` | an `SVGAnimatedRect` |

And the interface names were wrong in a way `instanceof` shows:

```
before:  g.constructor.name === "SVGElement"
after:   g.constructor.name === "SVGGElement"
         g instanceof SVGGraphicsElement === true
```

## Why `viewBox` is on five elements and not on every one

The tempting fix — "give every SVG element a `viewBox`" — is wrong, and the
negative assertions in the suite exist to keep it from coming back. SVG 2 §7.7:

> All elements that establish a new viewport, plus the `marker`, `pattern` and
> `view` elements have attribute `viewBox`.

The DOM side says the same thing from the other end: `SVGSVGElement`,
`SVGSymbolElement`, `SVGMarkerElement`, `SVGPatternElement` and `SVGViewElement`
each *include* `SVGFitToViewBox` (§4.6.9), the mixin whose single attribute is
`viewBox`. A `<g>`, a `<rect>`, a `<defs>` include nothing, and asking them
answers `undefined` — which is what a browser answers and what the suite
asserts, so a `viewBox` handed to them would fail the test rather than pass it.

Two references were consulted and they disagree, which is worth recording:

- **jsdom** gives `symbol` a `viewBox` and gives `marker` and `pattern` none.
  It is following an older draft; SVG 2 §12.13.1 and §13.6.9 both include
  `SVGFitToViewBox` on those interfaces today.
- **jsdom** has no `getBBox()` at all, so it is no reference for that half.

So the oracle here is the specification, quoted by section, and the suite's job
is to assert exactly the set the specification names — no more (a `<g>` has no
`viewBox`) and no less (a `<marker>` has one).

## The rest of the names, which jsdom has and the engine did not

Once the lookup was a table it was worth asking what else had an interface of
its own. `defs`, `desc`, `metadata`, `style`, `script`, `stop`, the gradients,
`clipPath`, `mask`, `filter`, the `fe*` filter primitives and the animation
elements all have one in SVG 2 §5.13, and all inherit `SVGElement` directly —
so none of them is a graphics element and none has `getBBox()`. jsdom reports
`SVGDefsElement` where this engine said `SVGElement`; it does now too.

They are a second table (`svg_plain_interface_for_tag`) rather than more
entries in the graphics one precisely because of the `getBBox()` consequence:
the two lists differ in *what they inherit*, and `defs instanceof
SVGGraphicsElement === false` is asserted next to `g instanceof
SVGGraphicsElement === true`.

## The change

`src/js/DomBindings.cpp` — the interface lookup now asks a tag table
(`svg_graphics_interface_for_tag`) after the five fit-to-viewBox elements and
`title`, so `g`/`rect`/`text`/`path`/… are their own interfaces, all of them
children of `SVGGraphicsElement`.

`src/js/DomHtmlBindings.cpp`:

- `svg_fits_to_view_box` / `setup_svg_fit_to_view_box` — the `viewBox` getter,
  moved off `SVGSVGElement` alone and put on all five interfaces that include
  `SVGFitToViewBox`. It was already generic over the element.
- `setup_svg_graphics_element` — `getBBox()`, returning an `SVGRect` built from
  the element's laid-out border box. With no transforms implemented, current
  user space *is* the painted box, so that is a correct reading of "in current
  user space" rather than an approximation of it.
- `setup_svg_svg_element` — `createSVGRect()`, plus the `viewBox` it already
  had, so moving the getter did not take it away from `<svg>`.
- `SvgRectState` gained a null-optional `box`: a `viewBox` half reflects an
  attribute and writes it back, while a `getBBox()` or `createSVGRect()`
  rectangle stands on its own and its members are writable. Before this the
  state assumed an element behind every rectangle.

## Verification

- `make test` — **433 suites pass**, including
  `tests/suites/374-the-svg-interfaces-that-were-all-one.cpp` (the oracle:
  interface names, `instanceof`, the prototype chain, `viewBox` on five and
  `undefined` on the rest, `getBBox()` present on graphics elements and absent
  elsewhere, `createSVGRect()`, non-constructibility, and the parent interface
  objects). **Reverting `src/js/DomBindings.cpp` and `DomHtmlBindings.cpp`
  fails it at `:54`** on `g.constructor.name`.
- DOM conformance **678/678** — unchanged, which is the point: this adds
  interfaces without disturbing the 26 files that were already passing.
- `make wpt` **9570/9824, 254 listed, 0 unlisted** — unchanged.
- `make xml-conformance` **1718/1718** — unchanged (nothing here touches XML
  parsing or serialization).
- `make diff-xml` **200/200 trees round-trip** — unchanged.

## Still open

- `preserveAspectRatio` is the other half of `SVGFitToViewBox` and is not
  exposed on any of these five elements.
- `SVGGraphicsElement.getCTM()` and `getScreenCTM()` are still absent; there is
  no transform list to report.
- `getBBox()` answers the laid-out border box; a sub-element's own geometry
  (`d` on a path, `r` on a circle) is not what it reads, because the layout has
  no separate SVG box model for them.
- `getBBox()` ignores its `SVGBoundingBoxOptions` argument (fill/stroke/markers/
  clipped); its `length` is 0, as the standard's IDL has it.
