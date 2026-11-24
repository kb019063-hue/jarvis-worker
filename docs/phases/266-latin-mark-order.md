# Phase 266: Latin mark order and canonical blocking

Agent: Rowan

## What was wrong

The previous font-aware composition pass only considered adjacent pairs. It
shaped `a + acute + dot below` differently from HarfBuzz, and did not compose
`a + cedilla + acute` across a lower-class cedilla. The eight-case differential
had 5 exact matches, 3 differences (one an order-only difference).

## What changed

For nonzero-class marks immediately following a Latin starter in its Latin
script run, stable-sort by the generated Unicode 17 canonical combining class.
Equal classes keep their order. Do not cross class-zero characters or script
boundaries; leading mark sequences and other scripts are not reordered.

Track the starter in the output and the last unconsumed mark's class. A
canonical pair may compose across lower-class marks, but an equal or greater
class blocks it. A consumed mark does not become a blocker. The font must
still cover the result, and intervening unconsumed marks are preserved. The
existing canonical-pair and combining-class tables are reused unchanged.

## Verification

```
make test
make build/shaping-probe
build/shaping-venv/bin/python tools/shaping/mark_order_diagnostic.py \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf --output build/mark-order.json
```

Setup is the same as `tools/shaping/README.md`. On the recorded DejaVu Sans
hash with HarfBuzz 12.1.0: **5/8 → 8/8 exact raw and placement matches**.
DejaVu Serif also gives 8/8 after the change. The standard 14-case corpus is
unchanged (9 raw matches, 4 RTL representation differences, 1 unsupported;
13 supported placement matches). No tolerance or expected failure was added.

`make test`: **400 suites plus 114 cross-origin checks pass**. The new synthetic
suite checks sorting of three classes, equal-class stability and repetition,
crossing an uncomposable lower-class mark, equal-class blocking, font coverage,
space/control/script boundaries, leading marks and input immutability.

The Phase 234 test that asserted the old limitation (never crossing an
intervening mark) now checks the composite glyph and preserved dot explicitly.
The existing MarkFilteringSet synthetic fixture uses a grave-codepoint mark
with the same class as its neighbours, rather than a lower-class dot. That
keeps the excluded glyph between them after canonical ordering, so the same
anchor assertions still test that lookup filtering skips it. No test floor
or README suite count changed.

## Still open

This is not full NFC or complete shaping normalization. Existing supported
precomposed glyphs are still retained: for example a following lower-class
mark can require decomposing a supported composite before reordering, which
this pass does not do. Leading mark reordering, non-Latin normalization,
script-specific feature plans, source clusters and cross-font fallback remain
separate. Matching these eight cases is not broad-script or browser pixel
conformance.
