# Finding 216 — shell frame text was terminal code

Phase 212. Status: **shell frame labels protected; other terminal paths open**.

## 1. What was wrong

The shell inserted untrusted navigation and page strings into its ANSI frame
without escaping. A URL or focused element ID containing ESC and BEL could
send a terminal OSC sequence despite the cell-output protection of Phase 203.

## 2. What changed

The frame replaces every non-printable-ASCII byte in URLs, focused tags and
IDs, and status messages with `?`; trusted ANSI remains intact. Stored URLs,
DOM and status messages are unchanged. Non-ASCII text is also replaced byte by
byte, matching the existing cell renderer's conservative fallback.

## 3. Evidence

`make test` passes 355 engine suites, including a test of all 256 bytes and
both successful and failed navigation. `make verify-terminal-shell-safety`
passes 1/1 real CLI frames and rejects 4/4 injected controls; the same
verifier fails against a binary linked with the pre-fix shell. Details and
commands are in `docs/phases/212-shell-frame-text-cannot-command-the-terminal.md`.

## 4. What remains wrong

Other terminal output and input paths, including diagnostic dumps and prompt
echo, have not been audited. This fix does not implement Unicode cell widths,
grapheme rendering or a general application-wide terminal-safety guarantee.
