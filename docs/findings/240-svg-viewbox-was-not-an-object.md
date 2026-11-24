# 240. An SVG viewBox was not an object

## What was wrong

The renderer could read an SVG `viewBox` attribute, but `SVGSVGElement.viewBox`
was undefined. Scripts could not read or edit its four coordinates through the
`SVGAnimatedRect` and `SVGRect` interfaces.

## What changed

Phase 236 adds stable live base and animated rectangle views for `svg` elements.
The base reads and writes the attribute; the animated view follows it without
animation and rejects writes. Attribute mutation, even setting the same text,
invalidates the parsed value. Detached retained views trace their node wrapper.

## Evidence

The original independent Chromium comparison was **0/20 before, 20/20
after**; the expanded probe is **34/34 after**:
`python3 tools/conformance/verify_svg_viewbox.py`. The setup and limits are in
[Phase 236](../phases/236-svg-viewbox-is-a-live-rectangle.md). The dedicated
suite `make test` includes a garbage-collection check.

## Still open

SVG viewBox interfaces on non-`svg` elements, `createSVGRect()`, animation,
geometry and repaint conformance remain separate work. The comparison does not
cover every numeric syntax or all SVG DOM APIs.
