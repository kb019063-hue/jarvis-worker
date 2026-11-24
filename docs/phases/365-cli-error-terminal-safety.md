# Phase 365 — CLI load errors are not terminal commands

Task row: terminal viewport clipping and ink; the CLI's file-open and
navigation error diagnostics. Finding 365.

## What was wrong

The CLI's `read_file` wrote an unsuccessful filename directly to stderr,
and its navigation error printed both the original URL and the loader's error
message unfiltered. A filename could contain ESC, BEL or CR. More importantly,
an HTTP server could send an unsupported `Transfer-Encoding` containing an
OSC sequence; the loader retained that header value in its error message and
the CLI emitted it directly to the terminal. The earlier terminal-cell,
shell, worker and JavaScript-diagnostic boundaries do not protect these writes.

## What changed

Only the CLI's file-open and navigation-error output boundaries change:
printable ASCII bytes 0x20..0x7e pass through and every other byte becomes
`?`. Static labels and the terminating newline stay intact. The path sent to
`ifstream`, the navigation URL and `ResourceResponse::error_message` are
not changed; return statuses are unchanged. Like the shell-label policy this
is a conservative byte-wise ASCII fallback, including when redirected: it
replaces each byte of non-ASCII UTF-8 and is not Unicode terminal rendering.

## Verification

- `make verify-cli-error-terminal-safety`: 8/8 independent real-CLI checks,
  up from 2/8 with the old `main.cpp`. It captures all output without
  displaying the attack, and checks missing filename bytes 0x01..0xff except
  the path separator, a missing deferred script, a decoded `file:` error,
  a raw URL, a benign path, an existing filename containing ESC/BEL, and an
  HTTP server whose unsupported transfer-encoding contains ESC and BEL.
  The existing control-bearing file opens and its content is read unchanged.
- `make test`: 426 engine suites and 114 cross-origin checks pass. The CLI
  regression is a separate process-level verifier because `main.cpp` is not
  linked into the engine unit-test binary; CI runs the verifier explicitly.

## What remains open

This is not an audit of the CLI as a whole. XML/parser and bytecode
errors, image-output paths, console and REPL output, terminal input and
prompt echo have their own output boundaries and are not changed here.
The existing Unicode and terminal-width limitations remain.
