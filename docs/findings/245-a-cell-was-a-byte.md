# Finding 245 — a cell was a byte, and a byte is not a character

**Severity:** correctness, and a regression risk in the sanitising that made the
byte model look safe. A terminal cell held one `char`. Text was placed by
walking the bytes of the string and writing each one into a cell, so a UTF-8
sequence became as many cells as it had bytes, and `safe_cell_character()`
replaced every byte outside `0x20..0x7e` with `?`. The terminal view could not
show a page in any language that is not written in ASCII.

**Found by:** reading the AGENTS.md row instead of trusting it. The row
"Terminal viewport clipping and ink" (Phase 183, finding 187) carried one open
item — "Unicode/grapheme and terminal-width rendering (non-ASCII bytes are
replaced)" — and that sentence was exactly true.

## What it looked like

A page of three paragraphs, rendered into an 80x30 terminal by
`./build/litebird --render-terminal`:

```
<p>café résumé</p>    ->  cafr??sum??
<p>日本語</p>          ->  ?????????     (nine: three characters, three bytes each)
<p>naïve €5</p>        ->  na?????5
```

Two separate defects, and the second hides behind the first:

1. **Every non-ASCII byte was `?`.** Nothing outside ASCII could be drawn.
2. **A cell was a byte**, so a character of three bytes occupied three
   columns. Even if the bytes had been decoded for display, the columns after
   it would have been three to the right of where the layout put them, and a
   wide character (which a terminal draws two columns wide) would have been
   wrong twice over.

## The fix

A cell now holds a grapheme cluster and its column width:

- **The text path walks clusters, not bytes.** A combining mark, a variation
  selector, a zero-width joiner and the character after it, and a skin-tone
  modifier join the cell before them (UAX #29, in the part this renderer
  needs). `e` + U+0301 is one cell, and the `X` after it is in column 1.
- **A wide character takes two columns.** East_Asian_Width `W` and `F` take
  two, from a generated table (`tools/gen_terminal_width_tables.py`), and the
  column after it is not written at all: a terminal advances two columns when
  it draws the character, so writing a space there would put the rest of the
  row one column to the right. This was measured, not assumed — the first
  version of the fix wrote the space and the row came out 83 columns wide.
- **Zero-width characters take none.** A mark's own width is 0; joined to its
  base it contributes nothing, which is the point of joining it.

The generated table has 713 wide runs and 336 zero-width runs, and the
generator cross-checks all 282,230 assigned code points against Python's own
`unicodedata`: **282,230 agree, 0 differ.**

## The sanitising had to be made explicit

The old `?` did two jobs. It made non-ASCII visible, and it kept control bytes
out of the ANSI stream: a page that can get `\x1b[2J` into its text can clear a
reader's terminal, and `\x1b]0;…\x07` can rewrite its title. When the byte
model was replaced, that second job was lost, and the existing suite
`terminal_page_text_cannot_inject_controls` failed at once — which is what a
suite is for.

The rule is now explicit and separate from the encoding:

- C0 (which includes ESC, BEL and CR), DEL, C1 (which an 8-bit terminal reads
  as CSI), and U+2028/U+2029 (which split the grid a row at a time) become
  **U+FFFD**.
- Malformed UTF-8 becomes one U+FFFD per byte that could not start or continue
  a sequence, and the walk advances by exactly that byte, so it cannot loop or
  run past the end.

U+FFFD is the character the Unicode standard names for "this was not text". It
prints as a replacement glyph and commands nothing.

## How it was measured

`tools/conformance/verify_terminal_clipping.py` gained five hand-specified
`unicode_*` grids whose expected cells were written from the rule, not from the
renderer's output. Against the renderer before this change the whole file
scores **26/31** — all five Unicode cases fail and the 26 Phase 183 cases still
pass, because geometry was never the defect. After: **31/31**.

The end-to-end check is the column count, by an oracle the renderer does not
share: every text row of an 80-column render is exactly 80 columns when Python
counts it with `unicodedata.east_asian_width`. The `日本語` row is 77
characters and 80 columns.

Engine suites: 386/386 on the merged tree, including the new
`tests/suites/241-terminal-cells-are-characters.cpp`.

## What this does not do

- **Ambiguous-width characters take one column.** `·`, `α` and the box-drawing
  set are East_Asian_Width `A`; a terminal with CJK ambiguous-width mode on
  draws them two. The renderer picks one, which is what most terminals do by
  default, and does not offer the other.
- **A flag pair is two cells, not one.** 🇬🇭 is one grapheme cluster in
  `Intl.Segmenter`, and terminals disagree about how wide it is. This renderer
  draws the two regional indicators as separate cells, which is what a terminal
  that does not join them does, rather than claiming a width it cannot keep.
- **Hangul jamo and Indic conjuncts are not joined by their syllabic
  category.** The marks a cell can see in its character data are joined; a
  sequence that needs the full UAX #29 class table is drawn as separate cells
  rather than merged wrongly. No width is lost for the precomposed forms.
