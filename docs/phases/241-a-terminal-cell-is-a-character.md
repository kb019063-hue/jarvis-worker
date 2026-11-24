# Phase 241 — a terminal cell is a character

The terminal view is a debugging view: `--render-terminal` lays a page out and
draws it into an 80x30 grid of character cells. Phase 183 made that grid
respect the viewport, the clip stack and the transform stack. This phase makes
a cell hold a character.

The AGENTS.md row had said so for fifty phases:

> Unicode/grapheme and terminal-width rendering (non-ASCII bytes are replaced).

"Non-ASCII bytes are replaced" was the whole design: `safe_cell_character()`
returned `ch` for `0x20..0x7e` and `'?'` for everything else, and `TerminalCell`
held a `char`. A three-byte character was three cells of `?`.

## What changed

**`TerminalCell` holds a cluster and a width.**

```cpp
struct TerminalCell {
    std::string cluster;   // the grapheme cluster this cell draws
    uint32_t codepoint{0};
    int width{1};          // columns: 0 for a joined mark, 1, or 2
    bool continuation{false};  // the second column of a wide character
    // colours, as before
};
```

**The text path walks clusters.** `DrawTextCommand` carries a `std::string`,
and that string is UTF-8. The walk decodes one code point, then takes every
code point that joins it: a combining mark, a variation selector, a zero-width
joiner and what follows it, or a skin-tone modifier. The whole cluster goes
into one cell.

**Widths come from a generated table.** `tools/gen_terminal_width_tables.py`
writes `src/render/TerminalWidthTables.cpp`: 713 wide runs and 336 zero-width
runs, from East_Asian_Width `W`/`F` and the Mn/Me categories plus the
zero-width joiners. The generator cross-checks every assigned code point
against Python's `unicodedata` and reports 282,230 agreeing, 0 differing. The
header it writes says where the data came from, and the tool prints the
exceptions rather than hiding them.

**The second column of a wide character is not written.** This is the part
that took two attempts. A terminal that draws 日 moves the cursor two columns;
the grid also has a cell at the column after it. Writing that cell as a space —
the obvious reading of "the continuation cell is a blank in the same colours" —
puts a third column on the row, and the row came out 83 columns wide. The
continuation cell stays in the grid (it is indexed by column, and the text
after a wide character has to land in the right one) and carries the
character's colours, and `render()` skips it. Both output paths do this, so
`render()` and `to_ansi_string()` cannot disagree.

**Sanitising is now explicit rather than a side effect.** The old `?` kept
control bytes out of the stream because it kept everything but ASCII out. That
is a real guarantee — a page can write `&#27;[2J` in its text, and a terminal
will clear the reader's screen — and dropping it was caught immediately by
`terminal_page_text_cannot_inject_controls`, which is the suite doing its job.
The rule is now written down: C0, DEL, C1, U+2028, U+2029 and malformed UTF-8
become U+FFFD, one per undecodable byte, with the walk advancing by exactly the
bytes consumed.

## Measurement

| check | before | after |
| --- | --- | --- |
| `verify_terminal_clipping.py` | 26/31 | 31/31 |
| engine suites | 375/375 | 386/386 (merged tree) |
| 80-column render, row widths by Python | — | every text row exactly 80 |

The five new conformance cases are hand-specified from the rule, and the
documented command in the verifier's own docstring reproduces the 26/31
baseline against any pre-241 renderer, so the number is checkable rather than
asserted.

The 26 Phase 183 cases still pass unchanged. That is the useful half of the
result: the geometry was never the defect, and a change to how a cell holds a
character did not disturb where the cell goes.

## What is deliberately left

Ambiguous-width characters (East_Asian_Width `A`) take one column, the default
most terminals use. The CJK ambiguous mode that makes them two is a terminal
setting, not a page property, and this renderer does not model terminal
settings. Flag pairs and sequences that need the full UAX #29 class table —
Hangul jamo, Indic conjuncts — are drawn as separate cells rather than merged
on a guess: a wrong merge loses a column, and a missed one only looks less
joined than a terminal might.

The AGENTS.md row's Unicode item is closed; the row keeps its other notes.
