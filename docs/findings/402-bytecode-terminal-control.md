# Finding 402 — Bytecode dumps carried terminal instructions

A page's string literal appeared verbatim in the human-readable bytecode dump,
and a decoded token appeared in a compiler diagnostic. Both could contain ESC,
BEL, carriage return or encoded C1 controls. This affected the page and
`--run-js` forms of `--dump-bytecode`; the disassembler also printed a
caller-supplied title without filtering.

Phase 402 filters titles, displayed operands and compiler diagnostic text at
the output boundary, without changing constants or VM execution. The real-CLI
verifier passes 10/10 checks, versus 3/10 before; the new unit suite fails
against the old disassembler.

Redirected dumps are filtered as well and are not a lossless representation of
control-bearing constants. The filter assumes a UTF-8 terminal; tabs and line
feeds remain and can spoof lines. Other output paths and terminal input remain
outside this change.
