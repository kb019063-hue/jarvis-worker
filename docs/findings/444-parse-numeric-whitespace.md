# 444. Numeric parsers missed three Unicode spaces

`parseInt` and `parseFloat` failed to trim U+1680, U+205F and U+3000 at the
start of a string. Phase 444 recognizes their complete UTF-8 sequences.
Node comparisons improved **400/444 → 444/444**. See
[Phase 444](../phases/444-parse-numeric-whitespace.md). Other numeric
conversion behavior was not exhaustively audited.
