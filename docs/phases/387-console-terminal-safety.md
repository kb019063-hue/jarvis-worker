# Phase 387 — Console text is not a terminal command

Task row: terminal viewport clipping and ink; page-controlled console output.
Finding 387.

## What was wrong

The tree-walker console printed inspected page values directly to stdout and
stderr, including the labels of `console.count`. A page could emit ESC and
BEL to run terminal control sequences, carriage returns to overwrite text,
and encoded C1 controls. The independent bytecode VM console had the same
problem. Prior terminal-output work did not cover these paths.

## What changed

Both implementations pass displayed console arguments through the shared
UTF-8 `terminal_safe` filter after their existing output conversion. The
interpreter's log aliases share the fixed `log` function; assertion arguments
and count labels are filtered too. The VM has its own log, warn and error
functions. Only the displayed text changes: JavaScript values and the original
counter keys are preserved, even when distinct keys look alike after filtering.
Program-owned warning and error color sequences are not filtered.

The policy applies even when stdout or stderr is redirected, so logs that
previously contained control characters now contain replacement characters;
this is a deliberate compatibility change for safe output independent of the
consumer. Normal UTF-8 text is retained. The filter assumes a UTF-8 terminal;
an 8-bit terminal might interpret a valid continuation byte as a C1 control.
Tabs and newlines remain, so line spoofing is not prevented. Other direct
output paths and terminal input are outside this change.

## Verification

- `make verify-console-terminal-safety`: 7/7 checks pass, versus 2/7 with
  the old interpreter and VM linked into the same binary. It captures a real
  `--run-js` invocation and an HTML page, testing OSC, BEL, CR and encoded
  C1, ordinary Unicode, error output, counters and unchanged script values.
  Captured attack bytes are never printed.
- `make test`: 451 engine suites and 114 cross-origin checks pass. The new
  suite tests all log aliases, assertions, counter reset and key identity,
  script-visible values, and the separate VM console. With the old console
  implementations linked into the test binary, its output assertion fails.
- `make verify-css-paint-terminal-safety`: 13/13; `make verify-bytecode`:
  52/52 interpreter/VM agreement cases and 4/4 existing refusals.
- `make deep`: 1,332 runs, 0 failures; `make fuzz`: 0 findings;
  `make diff-xml`: 0 differential failures and 3,000/3,000 round trips.
- The console verifier and the new unit suite each pass when built with
  address and undefined-behavior sanitizers (the suite was linked into a
  small standalone harness). Sanitizer verification, JS, DOM, URL, XML, WPT,
  generated XML, fuzz and deep checks also pass; sanitizer deep ran 882
  cases without failure. The full sanitizer unit build exceeds this sandbox's
  memory limit; CI runs the complete suite.
