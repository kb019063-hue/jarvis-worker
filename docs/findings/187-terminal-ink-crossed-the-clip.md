# Finding 187 — terminal ink crossed the clip

## 1. What was wrong

Terminal rectangles and images had viewport-aware clipping, but borders,
shadows and text did not. An offscreen outline was clamped to the viewport
edge, text crossed an `overflow:hidden` clip, and a fully transparent text
color was displayed as opaque. A zero-sized viewport could access an empty
grid while drawing a border or focus ring.

## 2. What changed

Terminal borders and focus outlines use real, unclamped edges; borders honor
individual side widths, colors and clips. Text honors nested/transformed clips
and negative scroll coordinates, and composes its foreground alpha. The shadow
core uses its own color and alpha and is clipped. Invalid/empty outline, text,
shadow and clip geometry no longer reaches unsafe integer casts or grid access.

## 3. Evidence

`python3 tools/conformance/verify_terminal_clipping.py` compares complete ANSI
cell grids with 26 independently specified answers: **2/26 before, 26/26
now**. The old empty-viewport case crashes. Two unit suites also exercise
scrolling, colors, clipping and the real HTML/layout/display-list pipeline;
the phase write-up gives the commands and the UBSan check.

## 4. What is still open

Cell-origin clipping, byte-oriented text, unsanitized terminal control
characters, unblurred shadows, rounded clips and the focus overlay's limited
visibility are still approximations or gaps; see the phase. The separate CSS
`display:contents` canvas regression is not changed.
