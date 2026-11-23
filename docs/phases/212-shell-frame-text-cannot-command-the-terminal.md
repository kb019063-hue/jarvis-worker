# Phase 212 — shell frame text cannot command the terminal

Task row: terminal viewport clipping and ink, specifically the shell's
untrusted frame text. Finding 216.

## 1. What was wrong

Phase 203 protected the terminal renderer's page-text cells, but the shell
built its ANSI frame separately. It inserted the current URL, focused tag and
ID, and status message directly into that stream. A local path containing
ESC and BEL, or an element ID containing numeric character references for
them, could inject an OSC title-setting sequence into a terminal. Status
messages can also contain navigation errors, URLs or focused page text. The
page-cell safeguard did not protect these strings.

## 2. What changed

`BrowserShell::render_terminal_frame` converts each untrusted label at the
output boundary: printable ASCII bytes 0x20–0x7e are retained, all others
become `?`, one per byte. This matches Phase 203's deliberately conservative
terminal cell policy. URL, focused tag and ID, and status message are all
covered; the shell's own ANSI sequences and static UI labels remain unchanged.
The current URL, history, DOM, and status accessor keep their original bytes,
so this does not change navigation, graphical output or script-visible data.
`print_terminal_frame` and the interactive shell use the same frame.

This is an ASCII fallback, not correct Unicode terminal rendering: a multibyte
UTF-8 label displays one question mark per byte. No claim is made about terminal
input, prompt echo or other output paths such as CLI diagnostic dumps.

## 3. Verification

```
make test
# 355 engine suites pass, including shell_frame_labels_cannot_inject_terminal_controls;
# cross-origin suite: 114 checks, 0 failures.
make verify-terminal-shell-safety
# Shell frame safety: 1/1 real shell frames pass; 4/4 injected controls rejected
python3 tools/conformance/verify_terminal_text_safety.py
# Terminal safety: 2/2 APIs pass
python3 tools/conformance/verify_terminal_clipping.py
# Terminal cell checks: 26/26 pass
```

The unit test covers all 256 bytes in both focused ID and status text, the
original data retained in memory, successful and failed navigation, ESC,
BEL and CR in a filename, DevTools, and the streaming API. The separate
verifier loads an actual HTML file with an OSC-shaped filename and control
references in a focused button's ID via the public `--shell` CLI, captures
its static frame without displaying attack bytes, checks the raw fields and
ANSI structure, and challenges itself with OSC, CR, C1 and injected SGR.
Compiling the old `BrowserShell.cpp` from the base and linking it into the
same engine makes the verifier fail with `unexpected control byte in frame`;
the corrected build passes. CI runs both the unit test and the real-shell
verifier.

## 4. What remains open

This does not audit all terminal writes (including diagnostics, URL prompt
echo and REPL output) or terminal input handling. It is not a claim that the
whole application is terminal-safe. Unicode/grapheme/terminal-cell-width
rendering remains unimplemented, as do the prior terminal clipping and paint
approximations. Bitmap and graphical rendering are not affected.
