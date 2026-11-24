# Phase 236 — an SVG viewBox is a live rectangle

## What was wrong

`SVGSVGElement.viewBox` was absent. A script could set the `viewBox`
attribute, and SVG rendering already had its own reader, but code that uses
`svg.viewBox.baseVal.width` had no object to read or edit. This is one of the
SVG DOM questions the jsdom-based DOM corpus does not measure.

## What changed

An SVG `svg` element exposes a cached `SVGAnimatedRect`; its `baseVal` and
`animVal` are distinct, stable `SVGRect` objects. The base view reads the four
numbers from the no-namespace `viewBox` attribute and writes the whole
attribute on a component assignment. Invalid or absent text reads as four
zeroes. The parser accepts SVG's adjacent-sign and decimal syntax, negative
dimensions, float narrowing and the observed float decimal accumulation; the
serializer writes the observed six significant digits. JavaScript number
conversion runs before a write to the animated view is refused with
`NoModificationAllowedError`. `animVal` follows the base because viewBox
animation is not implemented. Retaining either rectangle keeps its detached
node and wrapper alive.

The DOM attribute-mutation hook invalidates an existing view on *every*
no-namespace `viewBox` write or removal, even a write of the same text. That
matters when a script writes a float with more precision than the serialized
attribute: the live float stays precise until the attribute is written again.
An unrelated or namespaced attribute change does not invalidate it.

## Evidence

`tools/conformance/verify_svg_viewbox.py` runs identical named probes against
pinned headless Chromium and Litebird. The original **20** probes matched
**0/20 before, 20/20 after**; the expanded set matches **34/34 after**. They
cover object identity and brand, parsing and serialization, invalid text,
attribute and component mutation, precision, same-text writes, conversion
order and reentrancy, read-only animated values, constructors, receiver checks,
namespace isolation and detached elements. `tests/suites/236-svg-viewbox.cpp`
adds a detached-view GC check and an independent ordinary suite.

```sh
make -j2 all
npm install --prefix build/svg-oracle @sparticuz/chromium@131.0.1 puppeteer-core@23.11.1
mkdir -p build/svg-oracle/runtime
node -e "require('fs').createReadStream(process.argv[1]).pipe(require('zlib').createBrotliDecompress()).pipe(process.stdout)" \
  build/svg-oracle/node_modules/@sparticuz/chromium/bin/al2023.tar.br | tar x -C build/svg-oracle/runtime
python3 tools/conformance/verify_svg_viewbox.py
make test
```

Minimal Linux systems may need NSS libraries on `LD_LIBRARY_PATH`; the
verifier includes the package's runtime directory and preserves that variable.

## Still open

Only `SVGSVGElement.viewBox` is connected: other viewBox-bearing SVG interfaces
such as `symbol`, `view`, `marker` and `pattern`, and `createSVGRect()`, remain
open. `animVal` does not implement SMIL or CSS animation. The parser and float
serialization were checked on the named probes, not every SVG number or
locale. The work tests DOM reflection, not whether viewBox edits repaint or
whether geometry APIs such as `getBBox()` conform.
