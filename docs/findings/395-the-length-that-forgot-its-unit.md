# Finding 395: the length attributes were undefined, and the one type that remembered the unit did not exist

## The symptom

```js
var r = document.createElementNS(SVG_NS, 'rect');
r.setAttribute('x', '10');
r.x;                       // undefined
```

Every length-valued geometry attribute in the SVG DOM was absent:
`x`/`y`/`width`/`height` on a rect, `cx`/`cy`/`r` on a circle, `x1`…`y2` on a
line, `refX`/`refY`/`markerWidth`/`markerHeight` on a marker, and `x`/`y`/
`width`/`height` on the `<svg>` element itself. `SVGLength` and
`SVGAnimatedLength` did not exist, so there was nothing for them to be.

This is the third and largest piece of the same row. Phase 374 built the SVG
element interfaces, Phase 381 built the second `SVGFitToViewBox` attribute, and
each recorded the rest as open. The length attributes are the biggest of what
was left, because they are what a page reads to find out where anything is.

## Why the obvious reuse was wrong

The renderer already had a length parser, and using it would have been the
small change. It resolves to pixels on the way in — correctly, because that is
what painting needs — and `SVGLength` needs exactly the opposite. Three
observations of one attribute:

| `height="5mm"` | Reads | Why |
| :--- | :--- | :--- |
| `unitType` | `7` (`MM`) | the unit as written |
| `valueInSpecifiedUnits` | `5` | the number as written |
| `value` | `18.898…` | the same length in user units |

A type that parsed to pixels could answer the third and invent the first two. So
the unit had to survive parsing, and the new parser sits beside the `viewBox`
and `preserveAspectRatio` parsers, sharing their number scanner, while the
renderer keeps its own.

## Two bugs this made, and what caught each

**The non-scalar was erasable.** `unitType` UNKNOWN and `valueInSpecifiedUnits`
0 are how §4.5.2 describes a value the constants cannot name — `3ch`, a
`calc()`. The first version modelled that faithfully and then wrote `""` back
when serializing, so:

```js
r.setAttribute('x', '3ch');
r.x.baseVal.unitType;   // 0, correct
r.getAttribute('x');    // "", destroyed — and the next read answered 0 NUMBER
```

The standard says such a value reads as UNKNOWN; it does not say writing it back
may delete it. `SvgLengthValue` now carries the source text and serializes that,
so a value the DOM can read but not express survives the round trip.

**`e` ate `em`.** The number scanner treated a trailing `e` as the start of an
exponent and then required digits, so `7em` failed to parse and threw
`SyntaxError` — while `1cm`, `2mm`, `3in`, `4pt`, `5pc`, `6px` and `50%` all
worked. A unit test on any one of those would have passed. Running all eleven
as one sequence is what showed it:

```
BAD 7em -> SyntaxError        OK 1cm -> 1cm type=6
BAD 7ex -> SyntaxError        OK 2mm -> 2mm type=7
```

The scanner now looks ahead before committing to an exponent, and the suite
asserts the whole unit set in one line so a regression cannot hide behind a
barely-covered table again.

## There is no reference to check this against

jsdom has **no `SVGAnimatedLength` and no `SVGLength`**. Its
`rect.x` is `undefined`, and `typeof SVGAnimatedLength` throws `ReferenceError`.
So unlike the `viewBox` and `preserveAspectRatio` work — where jsdom could be
consulted and disagreed in four or five instructive places — there is nothing
to compare against here at all, and the specification is the only oracle.

That is worth stating plainly rather than leaving implicit: this phase's
correctness rests on §4.5.2 and §4.6.5, quoted in the suite's header, and on
the negative assertions (which element has which attribute) that a wrong
reading would fail. `tests/dom-conformance/README.md` now records that the
jsdom comparison covers three fewer lines here, so a later agent does not read
"jsdom disagrees" as a bug list.

## What is still not there

`createSVGLength()`, the length/point/transform *lists* (text `x`/`y` and
`dx`/`dy` are lists in SVG 2), `getCTM()`/`getScreenCTM()`, SVG animation, and
the other animated types beyond this one. `em` and `ex` resolve at the initial
16px/8px rather than the element's computed font size, so a length written in
`em` inside a 10px element reports the wrong user-unit value — a real limitation
of the conversion, recorded in the phase doc rather than papered over.
