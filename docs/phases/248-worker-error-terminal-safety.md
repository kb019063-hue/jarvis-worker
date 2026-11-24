# Phase 248 — a worker error is not a terminal command

Task row: terminal viewport clipping and ink; the unhandled worker-error
output boundary. Finding 252.

## What was wrong

`WorkerObject::dispatch_page_error` printed the message from a worker script
or load error straight to stderr if no page handler consumed it. A worker
throwing `"\x1b]0;worker\x07"` put an OSC sequence in that diagnostic. The
terminal frame and page-cell protections of Phases 203 and 212 do not cover
this output. This is not limited to OSC: a newline, CR or other control byte
in a message also reached the output as data interpreted by the terminal.

## What changed

At this one stderr boundary, printable ASCII bytes (0x20 through 0x7e) pass
unchanged and every other byte becomes `?`. The static prefix and final
newline remain the program's own. The `ErrorEvent.message`, worker-side
handlers and underlying error strings are not changed. As with the shell
frame, this conservative policy renders a UTF-8 message as one question
mark per non-ASCII byte; it is not Unicode-aware terminal rendering.

## Verification

- `make test`: 394 engine suites and 114 cross-origin checks pass. The new
  suite throws all code points U+0000..U+00FF in a real worker and compares
  the entire diagnostic, then checks that a handled error keeps its original
  control characters and does not emit the unhandled diagnostic.
- `make verify-worker-terminal-safety`: 3/3 checks. It runs two real workers
  through the CLI, waits for asynchronous delivery, captures stderr without
  printing attack bytes, and checks both the exact sanitized diagnostic and
  the unmodified ErrorEvent seen by the page. With the original `Worker.cpp`
  it passes 2/3: the unhandled diagnostic contains ESC, BEL, CR and U+009B.
- CI runs the independent verifier in addition to the unit suite.

## What remains open

This protects only the default worker diagnostic, including when stderr is
redirected. Other writes such as console output, exception diagnostics, CLI
errors and the interactive prompt have not been audited or made safe here.
The existing limitations on Unicode, terminal width and input remain. This
is not a claim that the application as a whole is terminal-safe.
