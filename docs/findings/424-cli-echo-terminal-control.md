# Finding 424 — CLI and REPL echoes carried terminal instructions

A REPL evaluation result could write terminal-control bytes. The CLI also
printed unfiltered invocation names, GUI initial-page arguments and BMP/PNG
output filenames in success and error messages. The latter are invoker-supplied
rather than page-supplied, but can come from untrusted filenames or pasted text.

Phase 424 filters these human-facing fields while preserving JavaScript values,
filesystem paths, GUI navigation and the application's ANSI decorations. The
real-process verifier passes 16/16 checks, versus 7/16 on the old binary.

Redirected messages are filtered too. The filter assumes a UTF-8 terminal;
tabs and newlines remain and can spoof lines. Terminal input echo and other
outputs remain outside this change. In particular, `--dump-layout` still
emits page-controlled IDs and text without filtering.
