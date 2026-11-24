# Finding 283 — an uncaught error could command the terminal

An uncaught JavaScript value or unhandled promise rejection was printed to
stderr without filtering. A page-controlled ESC, BEL, CR or C1 character could
therefore be interpreted as a terminal instruction. The rendered-cell, shell
frame and worker-error safeguards are separate and did not protect it.

Phase 269 filters only the uncaught and rejection diagnostic bytes in
`JsOps.cpp`: printable ASCII is preserved and every other byte is replaced by
`?`. Event handlers and promise reactions still receive the original value.
`make verify-js-error-terminal-safety` checks six real CLI cases: 2/6 passed
before and 6/6 after. A unit suite additionally covers all U+0000..U+00FF and
native/syntax exception bytes.

This is an ASCII fallback, including for redirected stderr, not Unicode
terminal rendering or a general audit. Console output, other diagnostic
paths and terminal input remain open.
