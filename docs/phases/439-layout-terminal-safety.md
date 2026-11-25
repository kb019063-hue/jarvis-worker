# Phase 439 — Layout dumps are not terminal commands

Task row: terminal viewport clipping and ink; the human layout dump.
Finding 439.

## What was wrong

`LayoutBox::dump` printed an element's ID and either its first wrapped line or
its text directly. A page could put ESC, BEL, carriage return or C1 controls
in these fields, making `--dump-layout` send instructions to the terminal.
For example an ID containing `&#27;]0;OWN&#7;` reached the real CLI intact.
The other human dump paths had been protected, but layout had not.

## What changed

The dump filters the displayed ID, first line and direct text separately with
`terminal_safe` at the stream boundary. The DOM and layout objects are not
mutated; the rest of the dump, including geometry, indentation and the CLI's
own ANSI heading, is unchanged. The IFC summary branch does not print text.
The implementation filters fields rather than buffering the whole tree: a deep
layout dump can be very large. The separately specified webplatform tree dump
remains unfiltered data for its corpus.

The same policy applies to redirected layout output, so it is not a lossless
serialization of control-bearing page data. Normal UTF-8 survives; the filter
assumes a UTF-8 terminal, since an 8-bit terminal may interpret a valid
continuation byte as C1. Tabs and line feeds remain, so log-line spoofing is
not prevented. Other direct output and terminal input are outside this fix.

## Verification

- `make verify-layout-terminal-safety`: 9/9 real-process checks, versus 6/9 on
  the original binary. It captures a page with terminal instructions in both
  its ID and text, checks Unicode and geometry, and verifies that JavaScript
  and the webplatform DOM data still see the original values. Attack bytes
  are captured, never displayed.
- `tests/suites/439-layout-terminal-safety.cpp` checks ID and both text
  branches, one and several wrapped lines, the IFC branch, descendants,
  malformed bytes, allowed whitespace and unchanged source fields. The old
  implementation fails its first filtered-ID assertion.
- `make test`: 492 engine suites and 114 cross-origin checks pass. Normal
  conformance, WPT and render checks completed; `make verify-bytecode` agrees
  on 75/75 cases. Existing terminal-safety verifiers pass. `make diff-xml` has
  0 differential failures and 3,000/3,000 round trips.
- `make fuzz` passes with no findings; `make deep` passes 1,332 runs over
  465 pages. The ASan/UBSan build passes the new CLI verifier (9/9) and
  the new unit regression, its verifier and conformance parts, generated XML,
  fuzz and deep checks (882 runs over 315 pages). The first sanitized fuzz
  run timed out on a layout case while a unit compilation was competing for
  memory; rerunning it alone passed without findings.
- The sanitizer unit binary was compiled with `TEST_OPT=-O0` after the normal
  optimized compilation was killed under this 3.8 GiB sandbox's memory limit.
  Four non-deep stretches pass 488 of 492 engine suites and the cross-origin
  test passes. The remaining four deep-nesting suites were not run locally:
  they have previously exceeded this sandbox's memory; CI runs them on a
  larger machine. Thus this is not a full local `make asan` pass.
