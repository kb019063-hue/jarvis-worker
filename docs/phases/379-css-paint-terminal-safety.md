# Phase 379 — CSS and paint dumps are not terminal commands

Task row: terminal viewport clipping and ink; the human `--dump-css` and
`--dump-paint` output paths. Finding 379.

## What was wrong

CSS declaration names and values and `DrawText` strings were printed straight
to the terminal. A page could include an OSC sequence in a CSS string or its
text and put ESC and BEL into either human dump. Phase 368 protected the DOM
dump, not these outputs.

The shared `terminal_safe` helper also accepted any UTF-8-shaped byte
sequence as valid. It passed UTF-8-encoded U+0080..U+009F C1 controls through
and accepted overlong sequences, surrogate encodings and out-of-range code
points. Simply applying that helper would leave a hole in both these dumps
and the DOM dump already using it.

## What changed

The CSS dump filters both declaration names and values and the paint dump
filters text with `terminal_safe` at the output boundary; stylesheet and
paint data are not mutated. The helper now validates UTF-8 scalar values,
replaces malformed bytes one at a time, and replaces all UTF-8 C1 controls
as well as raw invalid bytes. It retains ordinary Unicode, including valid
sequences with 0x9b as a continuation byte. This strengthens every existing
use of the shared helper, including the human DOM dump. The separately
specified `--dump-dom-webplatform` machine-readable corpus format is not
filtered.

This policy assumes a UTF-8 terminal: a terminal interpreting individual
bytes as an 8-bit encoding could treat a valid UTF-8 continuation byte as a
C1 control. For compatibility the helper also retains tabs and line feeds;
they can change the visual layout of a dump. This is not a defense against
log-line spoofing. Other output, notably console logging and terminal input,
is not covered, and this is not a claim of general terminal safety.

## Verification

- `make verify-css-paint-terminal-safety`: 13/13 independent checks, up from
  6/13 using the old `main.cpp` and `TerminalText.cpp` linked into the same
  engine. The verifier captures a real page's CSS and paint dumps, checks
  three injected fields and unchanged webplatform data, and uses a small
  probe for all ASCII controls, raw and encoded C1, invalid UTF-8, valid
  Unicode, and 64 deterministic byte strings against Python's strict UTF-8
  decoder. Attack bytes are captured, not printed.
- `make verify-page-text-terminal-safety`: 21/21 existing DOM-dump checks.
- `make test`: 441 engine suites and 114 cross-origin checks pass. The
  process-level and byte-level verifier runs in CI; no engine unit suite is
  added for the standalone CLI output.
