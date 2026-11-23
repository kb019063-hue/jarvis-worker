# Finding 207 — terminal text was terminal control

## 1. What was wrong

Both terminal renderer APIs wrote untrusted page text bytes directly into an
ANSI stream. ESC, C0, DEL and C1 bytes could be interpreted as terminal
control. HTML character references are one route from a page to such bytes.

## 2. What changed

The two ANSI output paths pass every grid character through a shared
printable-ASCII boundary: 0x20–0x7e stays, all other bytes become `?`. This
preserves cell positions and prevents page bytes from creating control
sequences. It deliberately replaces non-ASCII bytes; the byte-based renderer
has no correct Unicode/grapheme handling yet.

## 3. Evidence

`python3 tools/conformance/verify_terminal_text_safety.py`: both APIs pass all
256 byte values and exact generated ANSI framing; the old renderer fails both.
The parser self-test rejects injected ESC, C1, a changed printable and a
truncated stream. A unit suite also covers both APIs and a real HTML numeric
reference path; `python3 tools/conformance/verify_terminal_clipping.py` remains
26/26.

## 4. What is still open

This is not an audit of shell titles, URLs, logs, prompts or terminal input.
Unicode output is replaced rather than implemented. Phase 203 explains the
trade-off and limits; bitmap and browser rendering are not changed.
