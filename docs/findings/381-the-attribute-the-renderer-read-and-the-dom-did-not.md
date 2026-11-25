# Finding 381: `preserveAspectRatio` was a string the renderer read and the DOM did not have

## The symptom

```js
svg.getAttribute('preserveAspectRatio');   // "xMinYMax slice"
svg.preserveAspectRatio;                   // undefined
```

The engine parsed that attribute for painting — `SvgRenderer` had handled
`none`, `meet`, `slice` and even the SVG 1.1 `defer` keyword for a while — but
the DOM half of the same feature had never been written. `SVGPreserveAspectRatio`
and `SVGAnimatedPreserveAspectRatio` did not exist at all, so a page could set
the attribute as text and read it back as text, and nothing else.

This is the other half of the finding Phase 374 fixed. `SVGFitToViewBox` has two
attributes (§4.6.9); 374 gave the five elements that include it their `viewBox`,
and recorded `preserveAspectRatio` as still open. Same mixin, same five
elements, the attribute next to it.

## Three things this got wrong on the way, and what caught each

**1. An invented exemption for `none`.** The first version refused to write a
meetOrSlice after `none`, on the reasoning that the attribute grammar ignores it
there, so writing it is redundant. jsdom writes `none meet`. Checking §8.14.4
showed why: the serialization step appends align, a space, then the meetOrSlice
keyword, with **no** branch for `none`. The "redundancy" is in the grammar, not
in the serializer, and a serializer that silently drops part of its own state is
a bug even when the dropped part reads the same.

**2. An off-by-one on `align`.** The setter accepted `0`, which is
`SVG_PRESERVEASPECTRATIO_UNKNOWN`. §8.14.4 rejects any value without a keyword,
and UNKNOWN is precisely the one constant no keyword produces — so `0` cannot be
written, and the check is `number == 0 || number > 10`, not `number > 10`. The
suite's assertion `align = 0` throws is what found it.

**3. An assertion that assumed constants are own properties.** The constants are
defined on the interface object and reach an instance through the prototype, so
`hasOwnProperty` is false and `in` is true. That was my error in the *test*, not
in the engine: jsdom agrees with the engine. Worth recording because a test that
encodes a wrong expectation is worse than no test — it has to be corrected, not
satisfied.

## Where jsdom is not the reference

Two behaviours were settled against the specification rather than jsdom:

| | jsdom | SVG 2 | Engine |
| :--- | :--- | :--- | :--- |
| `animVal.align = 2` | written, and lands in the attribute | §8.14.4 throws `NoModificationAllowedError` | throws |
| `align = 0` | throws `TypeError` | §8.14.4, no keyword for UNKNOWN | throws |

The `animVal` one is the notable disagreement: jsdom's `animVal` is a plain
object that writes straight through to the attribute, so it cannot express
"read-only" at all. The engine implements the rule. It is recorded in
`tests/dom-conformance/README.md` alongside the four `viewBox`/`getBBox` places
where jsdom follows an older draft, so a later agent rerunning the comparison
does not "fix" it back.

Everywhere else the two agree, including on the eight grammar cases — `defer`
falling back, `xMidYMid  slice` with two spaces parsing, `XMIDYMID` rejected as
case-wrong, `xMidYMid MEET` rejected, `none slice` reading as (1, 2). Those are
the lines `compare_with_jsdom.py` does not print, which is the tool's way of
saying they match.

## What is still not there

`animVal` and `baseVal` agree because there is no SVG animation to make them
differ; the difference is that one is writable. `getCTM()`/`getScreenCTM()` and
the other SVG animated attributes (`transform`, the length-valued
`x`/`y`/`width`/`height`, `SVGAnimatedEnumeration`) remain unimplemented, and
are named in the AGENTS.md row so the next phase can pick one up without
re-deriving where the boundary is.
