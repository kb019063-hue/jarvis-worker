# Phase 381: `preserveAspectRatio` had no DOM at all

## What a page saw

Phase 374 put `viewBox` on the five elements that include `SVGFitToViewBox`.
That mixin has **two** attributes (SVG 2 §4.6.9), and the other one was still
missing:

```js
svg.preserveAspectRatio;                 // undefined
typeof SVGAnimatedPreserveAspectRatio;   // undefined
```

The renderer had been reading the attribute as a string the whole time
(`SvgRenderer::compute_viewbox_matrix` parses it, including `defer` and
`slice`), so a page could set it and have it paint, but could not read or edit
it as the object the standard defines.

## The shape of it

```
SVGAnimatedPreserveAspectRatio   baseVal, animVal  -> SVGPreserveAspectRatio
SVGPreserveAspectRatio           align, meetOrSlice, 14 constants
```

The attribute is a keyword pair — `<align> <meetOrSlice>?`, initially
`xMidYMid meet` — and the DOM exposes it as two `unsigned short` constants,
which is why the constants matter: `align` reads `6`, not `"xMidYMid"`.

| Constant | Value | | Constant | Value |
| :--- | ---: | --- | :--- | ---: |
| `SVG_PRESERVEASPECTRATIO_UNKNOWN` | 0 | | `SVG_PRESERVEASPECTRATIO_XMAXYMID` | 7 |
| `SVG_PRESERVEASPECTRATIO_NONE` | 1 | | `SVG_PRESERVEASPECTRATIO_XMINYMAX` | 8 |
| `SVG_PRESERVEASPECTRATIO_XMINYMIN` | 2 | | `SVG_PRESERVEASPECTRATIO_XMIDYMAX` | 9 |
| `SVG_PRESERVEASPECTRATIO_XMIDYMIN` | 3 | | `SVG_PRESERVEASPECTRATIO_XMAXYMAX` | 10 |
| `SVG_PRESERVEASPECTRATIO_XMAXYMIN` | 4 | | `SVG_MEETORSLICE_UNKNOWN` | 0 |
| `SVG_PRESERVEASPECTRATIO_XMINYMID` | 5 | | `SVG_MEETORSLICE_MEET` | 1 |
| `SVG_PRESERVEASPECTRATIO_XMIDYMID` | 6 | | `SVG_MEETORSLICE_SLICE` | 2 |

## Three rules that are easy to get wrong

**1. An invalid attribute reads as the initial pair, not as UNKNOWN.**
§4.3.6: the value is *"the current non-animated value of the attribute (using
the attribute's initial value if it is not present or invalid)"*. So
`preserveAspectRatio="nonsense"` answers `align === 6, meetOrSlice === 1`.
UNKNOWN exists in the IDL and no keyword produces it. This is why the parser
falls back rather than reporting 0.

**2. A write serializes both parts, always — including after `none`.**
§8.14.4's serialization step is "append align, append a space, append the
meetOrSlice keyword", with no exception. So setting `align` to `NONE` on an
`xMidYMid slice` writes `none meet`, not `none`. This looks wrong — the
attribute grammar says `<meetOrSlice>` is *ignored* when align is `none`, so
writing it is redundant — and the first version of this change "helpfully"
dropped it. jsdom writes `none meet`, which is what the algorithm says, and the
suite now pins it:

```cpp
assert(S("svg.preserveAspectRatio.baseVal.align=SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_NONE;"
         "svg.getAttribute('preserveAspectRatio')") == "none meet");
```

**3. `animVal` is read-only; `baseVal` is not.** §8.14.4: *"If the
SVGPreserveAspectRatio is read only, then throw a NoModificationAllowedError."*
jsdom disagrees here — its `animVal` is a plain writable object whose writes
land in the attribute — and the specification is the reference, so the engine
throws. Recorded in `tests/dom-conformance/README.md` with the other four
places jsdom and the specification part ways.

Two smaller ones: a setter with no keyword throws `TypeError` (§8.14.4 says the
setter throws for a value with no entry in the keyword table), which covers both
`0` — UNKNOWN has no keyword, so it cannot be *written* either — and anything
past `XMAXYMAX`; and WebIDL converts to `unsigned short` **before** that check,
so `align = 6.9` truncates to 6 rather than throwing.

## The change

`src/js/DomHtmlBindings.cpp` only:

- `SvgPreserveAspectRatioState` and `SvgAnimatedPreserveAspectRatioState`, both
  holding the element and a read-only flag.
- `parse_svg_preserve_aspect_ratio` — the keyword grammar, sharing
  `svg_space` with the `viewBox` parser next to it. Case-sensitive, tolerant of
  whitespace, rejecting `defer` (SVG 2 no longer lists it) and trailing junk.
- `write_svg_preserve_aspect_ratio` — both parts, always.
- `setup_svg_preserve_aspect_ratio` (the 14 constants and the two accessors)
  and `setup_svg_animated_preserve_aspect_ratio` (`baseVal`/`animVal`, cached).
- The getter lives in `setup_svg_fit_to_view_box`, not in a setup of its own,
  because that is the interface that carries it: the same five elements that
  have `viewBox` have this, and `<g>`/`<rect>` have neither.

The renderer is untouched. It already parsed the attribute correctly, and the
serialization rule above means what a setter writes is still what it parsed
before.

## Verification

- `make test` — **442 suites pass**, including
  `tests/suites/381-preserve-aspect-ratio-is-an-object.cpp`: the interfaces,
  `[SameObject]` on all four members, all 14 constants and that they are
  inherited rather than own properties, the initial value, the grammar
  (including seven invalid forms), both serialization rules, the refusals on
  both setters plus `animVal`, and WebIDL's truncate-before-check.
  **Reverting `src/js/DomHtmlBindings.cpp` fails it at `:48`.**
- DOM conformance **678/678** — unchanged.
- `make wpt` **9570/9824, 254 listed, 0 unlisted** — unchanged.
- `make xml-conformance` **1718/1718** — unchanged.

## Still open

- SVG animation, so `animVal` could differ from `baseVal`; today they agree and
  differ only in being read-only.
- `getCTM()` / `getScreenCTM()`.
- The remaining SVG animated attributes (`transform`, `x`/`y`/`width`/`height`
  as `SVGAnimatedLength`, `SVGAnimatedEnumeration` and the rest).
