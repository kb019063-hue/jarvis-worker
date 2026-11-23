# Phase 183 — terminal ink stays in the viewport

*Finding 187. Task: terminal clipping and viewport edges after Phase 170.*

## 1. What was wrong

Phase 170 made the terminal's canvas, rectangles and images respect viewport
scrolling and clips. Other commands still used different rules. A border or
shadow wholly above the viewport was clamped onto its first row; a partially
visible border acquired fictitious edges at the viewport boundary. Borders
also drew all four edges regardless of width or transparency. Text ignored
`overflow` clips and rounded negative coordinates toward zero, so text above or
left of the viewport leaked into its first row or column. Shadows ignored their
color, alpha and enclosing clips. Focus-ring coordinates were clamped too, and
a zero-sized terminal could index an empty grid.

## 2. What changed

`TerminalRenderer` computes the visible intersection with exclusive right and
bottom edges before converting to integer cells. Border and focus outlines keep
the *original* edge positions: an edge outside the viewport is not redrawn on
the edge of the viewport. Borders paint only nonzero, nontransparent sides,
retain their individual colors, and honor every active clip. Shadows honor
scroll, clips, the specified color and alpha; their unblurred core uses the
bitmap renderer's 0.6 alpha factor. Text checks clips per cell, uses floor for
negative positions, skips fully transparent ink, composites translucent
foregrounds over the cell background, and avoids iterating through text that
is wholly offscreen. Empty or negative-size outlines, shadows and clips, and
non-finite geometry in these paths, are refused before integer conversion; a
zero-sized terminal is safe.

The clipping policy stays the terminal's existing one: an enclosing clip asks
whether a cell's **top-left** point is inside it. This is an approximation, not
a claim that a character cell matches the bitmap painter's pixels.

## 3. How it was verified

`tools/conformance/verify_terminal_clipping.py` builds a small driver with only
the renderer and reads its **public ANSI output** with an independent decoder.
Its 26 hand-specified whole-grid cases include exact and partially visible
borders, absent/transparent sides, nested and transformed clips, negative and
scrolled text, alpha, shadows, focus, empty viewports and malformed geometry.
Two already-passing rectangle/image cases are controls. The decoder's self-test
rejects a changed cell and malformed streams.

```sh
# Save the previous source before changing it, then compare the same fixture:
git show d51ed33:src/render/TerminalRenderer.cpp > build/terminal-before.cpp
python3 tools/conformance/verify_terminal_clipping.py --source build/terminal-before.cpp  # 2/26, exits 1
python3 tools/conformance/verify_terminal_clipping.py                                      # 26/26
CXX='g++ -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all -g' \
  python3 tools/conformance/verify_terminal_clipping.py                                   # 26/26
```

The old renderer also segfaulted on the empty-viewport fixture. Two C++ unit
suites check the same public output and the actual HTML → layout → display-list
path; `make test` runs them along with the engine suite. Their expected cells
are not derived from the renderer's coordinate helper.

## 4. What is still open

- Terminal clips are rectangular and sampled at cell origins; rounded clips,
  pixel coverage, shadow blur and border thickness remain approximations.
- Text is still byte-oriented rather than grapheme- or terminal-width-aware;
  terminal control characters in page text are not sanitized here. This phase
  does not claim general Unicode or terminal-input safety.
- Focus is currently marked before the display list is painted, by changing
  foreground/bold on the outline's existing cells; later ink can overwrite the
  mark, empty cells do not show a foreground color, and the focused element's
  ancestor overflow clips are not available at that point. This phase fixes
  viewport geometry, not the focus overlay's visual design.
- The separate HTML `display:contents` canvas regression was fixed by Phase
  190. Nested translucent inline backgrounds, background images and full root
  layout remain open as recorded in Phase 170. This phase does not change
  CSS/layout or bitmap paint behavior.
