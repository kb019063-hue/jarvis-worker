# Phase 402 — Bytecode dumps are not terminal commands

Task row: terminal viewport clipping and ink; bytecode disassembly and its
compiler diagnostics. Finding 402.

## What was wrong

`--dump-bytecode` wrote a compiled string constant directly into a human-readable
disassembly. A page script could put ESC and BEL into that constant and send a
terminal instruction to whoever ran the dump. The same path is available for
`--run-js`. In addition, a syntax error can quote a token's decoded text, and
both CLI compilation-error handlers printed the exception without filtering.
`Chunk::disassemble` also accepts a caller-supplied title.

## What changed

The disassembler filters its title and all displayed constant operands with
`terminal_safe`, including those rendered by `disassemble_instruction` alone.
It first applies the existing JavaScript output conversion to constants so an
unpaired UTF-16 surrogate is one replacement character. Both CLI compiler-error
handlers filter their exception text. Filtering is at the output boundary:
compiled constants and VM execution are unchanged, as are the program's own
heading and color sequences.

The disassembler is a human-readable output, not a lossless serialization: the
same filtering is applied when its stream is redirected or supplied by another
caller. Ordinary UTF-8 survives. It assumes a UTF-8 terminal; in an 8-bit mode
valid continuation bytes could be interpreted as controls. Tabs and newlines
remain, so line spoofing is not prevented. Other output paths and terminal
input are not covered by this change.

## Verification

- `make verify-bytecode-terminal-safety`: 10/10 checks, versus 3/10 on the
  original binary. The verifier captures real `--run-js` and page output,
  including constants and decoded-token compiler diagnostics, without printing
  attack bytes. It checks ordinary Unicode, C0 and encoded C1 controls,
  program-owned ANSI, and a lone surrogate.
- `make test`: 461 engine suites and 114 cross-origin checks. The new suite
  checks all five constant-bearing opcodes, the public title and instruction
  printer, malformed title bytes, lone surrogates, unchanged constants, and
  the actual VM result. Linking the old disassembler into the same suite makes
  its first filtered-title assertion fail.
- `make verify-bytecode`: 59/59 interpreter/VM agreement cases; existing DOM,
  CSS/paint and console terminal verifiers pass. `make deep`: 1,332 runs,
  0 failed; `make diff-xml`: 0 differential failures and 3,000/3,000 round trips.
- `make fuzz` and sanitizer verification, JS/DOM/URL/XML/WPT, generated XML,
  fuzz and deep parts completed; sanitizer deep ran 882 cases without failure.
  The sanitizer-generated XML run without lxml reported one Expat-only
  difference, which is not counted in that mode: libxml2 agrees with Litebird
  on the same input. The new suite and verifier pass under ASan/UBSan. The full
  sanitizer unit run was stopped when an existing deep-nesting suite consumed
  nearly all of this 3.8 GiB sandbox; CI's larger runner covers the full run.
