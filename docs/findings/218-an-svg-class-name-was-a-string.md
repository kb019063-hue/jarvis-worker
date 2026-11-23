# Finding 218 — an SVG class name was a string

## 1. What was wrong

An SVG element inherited the HTML-style `Element.className` string, so its
`className.baseVal` was undefined and assigning it could not update `class`.

## 2. What changed

SVG elements now expose a cached, live, branded `SVGAnimatedString`: `baseVal`
reflects the attribute in both directions, `animVal` is a read-only view of
that value, and a retained view keeps its element alive. HTML's `className`
remains a string. Invalid receivers and direct construction throw.

## 3. Evidence

`python3 tools/conformance/verify_svg_class_name.py` compares 20 fresh answers
against Chromium: **1/20 before, 20/20 after**. The engine unit suite also
checks retention through an explicit GC. The phase write-up gives the pinned
browser installation and reproduction commands.

## 4. What is still open

SVG animation is not applied to `animVal`; other animated attributes,
`viewBox`, `getBBox()`, geometry and full SVG DOM/WebIDL coverage remain
separate work. This comparison does not claim rendering parity.
