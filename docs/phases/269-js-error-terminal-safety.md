# Phase 269 — uncaught JavaScript errors are not terminal commands

Task row: terminal viewport clipping and ink; uncaught JavaScript exceptions
and unhandled rejection diagnostics. Finding 283.

## What was wrong

`js_report_uncaught` and `js_report_unhandled_rejection` printed the thrown
value or native exception text directly to stderr. A page can throw an
OSC-shaped string such as `"\x1b]0;bad\x07"` or reject a promise with it,
causing ESC and BEL to reach a terminal. The existing terminal-cell, shell
frame and worker-error protections are at different output boundaries.

## What changed

At the stderr boundary in `JsOps.cpp`, printable ASCII bytes 0x20..0x7e
are retained and every other byte becomes `?`. This applies to the normal
thrown-value path, the promise-rejection path, and both native and syntax
exception diagnostic paths. Static prefixes and the terminating newline
are unchanged. Error handlers still receive the original thrown value;
exceptions canceled by an error event still produce no diagnostic, and a
caught rejection still receives the original reason. Reporting and exit-status
semantics are unchanged.

The same deliberately conservative byte policy is used by the worker-error
and shell-label diagnostics. It renders non-ASCII UTF-8 as question marks,
one per byte; it is not correct Unicode terminal rendering. The output is
filtered even when stderr is redirected.

## Verification

- `make test`: 414 engine suites and 114 cross-origin checks pass. The new
  suite compares complete outputs for every U+0000..U+00FF thrown value and
  rejection, the `JsThrow` exception path, and bytes 0x01..0xff in native
  and syntax exceptions; it also checks the original value is unchanged.
- `make verify-js-error-terminal-safety`: 6/6 real CLI checks, up from 2/6
  with the old implementation. They cover a thrown string, Error object,
  unhandled rejection, uncanceled and canceled error events, and a caught
  rejection, including the original values and exit statuses.
- The CLI verifier is also part of CI.

## What remains open

This covers only these `JsOps.cpp` diagnostic paths. Console output, other
CLI and parser diagnostics, terminal prompt echo and input are not protected
here. It is not a claim that the whole application is terminal-safe; the
existing Unicode and terminal-width limitations remain.
