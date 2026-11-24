# Finding 365 — a CLI load error could command the terminal

CLI file-open and navigation failures copied their filename, URL and loader
message directly into stderr. A filename could carry terminal controls; an
HTTP server could put an OSC-shaped sequence in an unsupported
`Transfer-Encoding`, which was then emitted in the loader error.

Phase 365 filters only these two CLI output boundaries, retaining printable
ASCII and replacing other bytes with `?`. Paths and loader responses remain
unchanged. `make verify-cli-error-terminal-safety` exercises the real CLI,
including a hostile local HTTP response: 2/8 checks passed before the fix
and 8/8 after. The same verifier covers control-bearing filenames that open
successfully as well as diagnostics from a missing deferred script.

This is a byte-wise ASCII fallback, including for redirected output, not
Unicode terminal rendering or a general terminal-safety audit. Other CLI,
console and prompt/input paths remain open.
