# Phase 203 — page text cannot command the terminal

## 1. What was wrong

The terminal grid stores each character as one byte. Both public output APIs
wrote that byte directly between their own ANSI color sequences. Page text
containing ESC, BEL, carriage return, a C1 byte, or another control could
therefore reach a terminal as an instruction rather than as page content. The
HTML numeric character reference `&#27;` reaches the display list as ESC; a
page need not contain the byte literally. Clipping does not make visible text
safe. Phase 183 documented the issue but did not fix it.

## 2. What changed

At the output boundary, both `render()` and `to_ansi_string()` now emit a
cell's byte only if it is ASCII 0x20–0x7e; every other byte is rendered as
`?`. Renderer-generated SGR sequences and line resets are unchanged. The
boundary applies equally to all grid cells and both APIs, including bytes
introduced through direct display lists. A cell stays a cell: substituting a
byte rather than removing it preserves the existing byte-based clipping and
positioning. This is intentionally conservative: **non-ASCII text is now
shown as question marks, one per UTF-8 byte**, not as Unicode characters.
Previously the renderer inserted ANSI escapes between UTF-8 bytes, so it was
not a correct Unicode renderer either, but the output is nevertheless a visible
change. Proper Unicode code-point/grapheme and terminal-cell-width rendering
needs its own design; do not silently remove this safeguard to add it.

## 3. Evidence

`python3 tools/conformance/verify_terminal_text_safety.py` compiles a small
driver and independently validates the complete byte stream from both APIs.
It checks all 256 possible input bytes, exact color sequences, one safe output
byte per cell, and final resets; its self-test rejects ESC, C1, a wrong
printable byte and truncation. The old renderer fails **0/2 APIs**, the new
renderer passes **2/2**. Reproduce the baseline without printing attack bytes:

```sh
git show 3a0b600:src/render/TerminalRenderer.cpp > build/terminal-before-safety.cpp
python3 tools/conformance/verify_terminal_text_safety.py --source build/terminal-before-safety.cpp  # exits 1
python3 tools/conformance/verify_terminal_text_safety.py                                         # 2/2
python3 tools/conformance/verify_terminal_clipping.py                                            # 26/26
```

A unit suite exercises both APIs and a real HTML → layout → display-list path
with numeric control references. The fixture does not send malicious output to
a real terminal. This checks the renderer's generated stream, not a terminal
emulator's interpretation.

## 4. What is still open

- This protects `TerminalRenderer` **cell output**, not arbitrary terminal
  output elsewhere: shell titles, URLs, prompts, logs and other direct writes
  need their own trust-boundary audit. It is not a general terminal-safety
  guarantee for the application.
- Unicode, grapheme clusters, wide glyphs, bidi controls, combining marks and
  printable-text fidelity are not implemented by this ASCII fallback. The
  preceding phase's clipping, shadow, and focus approximations remain.
- Control-sequence handling in terminal *input*, and browser/bitmap rendering,
  are outside this change.
