# Phase 424 — CLI and REPL echoes are not terminal commands

Task row: terminal viewport clipping and ink; REPL results and CLI echoes.
Finding 424.

## What was wrong

The REPL printed the JavaScript evaluation result directly. A result carrying
ESC, BEL, CR or C1 could instruct the terminal instead of displaying text.
The CLI also echoed the invoked executable name in help, the GUI's initial
page argument, and BMP/PNG output paths in success and error messages without
filtering. Those arguments are supplied by the invoker, not by a page; they
can nevertheless originate in untrusted names or pasted text.

## What changed

Each affected human-facing field is passed through `terminal_safe` at the
output boundary. REPL values also use the existing JavaScript output conversion
so a lone UTF-16 surrogate becomes one replacement character. The interpreter
keeps the original value, the actual executable and output paths are untouched,
and the GUI navigates to the original page. Application-owned ANSI sequences
remain unchanged.

Redirected output is filtered as well: messages are no longer a lossless copy
of control-bearing arguments. Normal Unicode is retained. The filter assumes a
UTF-8 terminal; in an 8-bit mode a continuation byte could be interpreted as a
control. Tabs and newlines remain, so this does not prevent line spoofing. The
terminal's own input echo and other output paths are outside this fix. In
particular, `--dump-layout` still prints page-controlled element IDs and text
without filtering; an actual page ID containing ESC/BEL reaches that dump.
Machine-readable XML and webplatform output are unchanged.

## Verification

- `make verify-cli-echo-terminal-safety`: 16/16 checks, versus 7/16 on the
  original binary. It captures the real REPL, `--help` invoked through a
  hostile symlink, BMP and PNG success and failure, malformed filename bytes,
  and a GUI launched on an ephemeral local port. It verifies the GUI's state
  via its actual API, confirms output files were created under the original
  names, and checks that JS character codes are unchanged. Attack bytes are
  captured, not sent to the terminal; ordinary Unicode, a lone surrogate,
  retained whitespace and program-owned colors are tested too.
- `make test`: 476 engine suites and 114 cross-origin checks pass. No engine
  unit suite is added: the changed surfaces are CLI entry points, and the
  process-level verifier exercises them in CI.
- Conformance, WPT and render checks completed; `make verify-bytecode` agrees
  on 59/59 cases. The CLI-error, bytecode, console, CSS/paint and DOM terminal
  verifiers pass. `make diff-xml` has 0 differential failures and 3,000/3,000
  round trips. `make deep` completes 1,332 runs without failure; `make fuzz`
  finds no crashes or other findings.
- The verifier passes 16/16 under ASan/UBSan. Sanitizer verification,
  JS/DOM/URL/XML/WPT, generated XML, fuzz and deep parts complete; deep ran
  882 cases without failure. Generated XML in Expat-only mode reports one
  uncounted mismatch: libxml2 agrees with Litebird on that input. The full
  sanitizer unit run was not repeated locally: on the preceding phase the
  existing deep-nesting suite exhausted this 3.8 GiB sandbox. CI runs it on a
  larger runner.
