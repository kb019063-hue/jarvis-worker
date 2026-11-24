#!/usr/bin/env python3
"""Check terminal cells independently of the renderer's coordinate conversion.

The C++ fixture emits tiny display lists; this script compiles it with the
specified renderer source, decodes the public ANSI stream, and compares whole
cell grids with hand-specified answers. No browser/font rasterization is used:
a terminal intentionally approximates geometry in 10x20 pixel cells.

    python3 tools/conformance/verify_terminal_clipping.py
    python3 tools/conformance/verify_terminal_clipping.py --source build/baseline/TerminalRenderer.cpp

A pre-fix renderer should fail several cases. --self-test checks that malformed
output and a changed expected cell are rejected. g++ (or $CXX) is required.

The five `unicode_*` cases were added in Phase 241, when a cell stopped being a
byte. Against the renderer before that change the whole file scores 26/31: every
one of the five fails, and the 26 Phase 183 cases still pass, because geometry
was never the defect. To reproduce that measurement:

    mkdir -p /tmp/baseline/include/litebird/render
    git show <pre-241-commit>:src/render/TerminalRenderer.cpp > /tmp/baseline/T.cpp
    git show <pre-241-commit>:include/litebird/render/TerminalRenderer.hpp \
        > /tmp/baseline/include/litebird/render/TerminalRenderer.hpp
    python3 tools/conformance/verify_terminal_clipping.py \
        --source /tmp/baseline/T.cpp --include-dir /tmp/baseline/include
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import unicodedata
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
# One cell is one grapheme cluster, and a cluster is not a byte and not a code
# point. The escape prefix is matched here; the characters after it are read one
# code point at a time (below), because only the character data says whether the
# next code point joins this cell or starts the next one.
PREFIX = re.compile(
    rb"\x1b\[38;2;(\d+);(\d+);(\d+)m"
    rb"\x1b\[48;2;(\d+);(\d+);(\d+)m"
    rb"(\x1b\[1m)?"
)


def read_codepoint(row: bytes, pos: int):
    """One UTF-8 code point from `row` at `pos`, as (character, next position).

    A malformed byte is U+FFFD and advances by one, which is the contract the
    renderer has: the grid still accounts for that byte as a cell, so the check
    can see the character the renderer chose to put there.
    """
    lead = row[pos]
    if lead < 0x80:
        return chr(lead), pos + 1
    if 0xC2 <= lead <= 0xDF:
        size = 2
    elif 0xE0 <= lead <= 0xEF:
        size = 3
    elif 0xF0 <= lead <= 0xF4:
        size = 4
    else:
        return "\ufffd", pos + 1
    chunk = row[pos:pos + size]
    try:
        return chunk.decode("utf-8"), pos + size
    except UnicodeDecodeError:
        return "\ufffd", pos + 1


def terminal_columns(text: str) -> int:
    """How many columns a cluster takes: UAX #11, as a terminal counts it.

    The width of the base character, and a cluster of marks that starts with
    something invisible is one column rather than none.
    """
    if not text:
        return 1
    base = text[0]
    if unicodedata.category(base) in ("Mn", "Me", "Cf"):
        return 1
    return 2 if unicodedata.east_asian_width(base) in ("W", "F") else 1


def joins(previous: str, current: str) -> bool:
    """Whether `current` extends the cluster that ends with `previous`.

    The rule this checks is the one UAX #29 gives for a base character and what
    follows it: a combining mark, a variation selector, a zero-width joiner (and
    the character after it) or a skin-tone modifier joins. Everything else
    starts a new cluster.
    """
    if not current:
        return False
    cp = ord(current[0])
    if previous.endswith("\u200d"):
        return True
    if cp in (0xFE0E, 0xFE0F, 0x200D):
        return True
    if 0x1F3FB <= cp <= 0x1F3FF:
        return True
    if unicodedata.combining(current) != 0:
        return True
    if unicodedata.category(current) in ("Mn", "Me"):
        return True
    return False


def read_cluster(row: bytes, pos: int):
    """One cell's text from `row` at `pos`, as (text, next position)."""
    text, pos = read_codepoint(row, pos)
    while pos < len(row) and row[pos] != 0x1B and row[pos] != 0x0A:
        nxt, after = read_codepoint(row, pos)
        if not joins(text[-1:], nxt):
            break
        text += nxt
        pos = after
        if nxt == "\u200d":
            nxt2, after2 = read_codepoint(row, pos)
            text += nxt2
            pos = after2
    return text, pos


WHITE = (255, 255, 255)
DEFAULT = (220, 220, 220)
BLACK = (0, 0, 0)
RED = (255, 0, 0)
GREEN = (0, 255, 0)
BLUE = (0, 0, 255)
YELLOW = (255, 255, 0)
FOCUS = (245, 158, 11)


def decode(stream: bytes, width: int, height: int):
    rows = stream.split(b"\x1b[0m\n")
    if len(rows) != height + 1 or rows[-1] != b"\x1b[0m":
        raise ValueError("wrong number of rows or missing final reset")
    result = []
    for row in rows[:-1]:
        cells = []
        pos = 0
        while pos < len(row):
            m = PREFIX.match(row, pos)
            if not m:
                raise ValueError(f"malformed cell at byte {pos}: {row[pos:pos + 30]!r}")
            fg = tuple(map(int, m.group(1, 2, 3)))
            bg = tuple(map(int, m.group(4, 5, 6)))
            bold = bool(m.group(7))
            pos = m.end()
            text, pos = read_cluster(row, pos)
            if bold:
                if row[pos:pos + 5] != b"\x1b[22m":
                    raise ValueError("bold sequence was not closed")
                pos += 5
            elif row[pos:pos + 5] == b"\x1b[22m":
                raise ValueError("unbalanced bold sequence")
            # A character that takes two columns is one cell that occupies two,
            # like a terminal moves the cursor by two; a cell with no character
            # is the second half of one, and the grid still has to have a place
            # to put the colour, or every cell after it would be out by one.
            cells.append((text, fg, bg, bold))
            # A character that takes two columns occupies two cells of the
            # grid. The ansi stream names it once and the terminal advances by
            # two; the second column is still a place in the row, and the cell
            # there is a blank in the same colours.
            if text and terminal_columns(text) == 2:
                cells.append((" ", fg, bg, bold))
        if len(cells) != width:
            # The width is counted in columns, and the fixture's viewport is
            # built from it: a row of 日本語 in five columns is 3 cells, and
            # gets four with the trailing blank. Counting cells is what matters
            # here, not bytes, so the check is that no cell ran off the end.
            if len(cells) > width:
                raise ValueError(f"row has {len(cells)} cells, more than {width} columns")
        result.append(cells)
    return result


def blank(dark=False):
    """A 5x4 viewport.

    The renderer's own default is a dark terminal (background 30,30,30) and the
    Phase 183 cases were specified against a white one because they draw their
    own backgrounds; `dark=True` is for the cases that leave the background
    alone, which is what a page with no styling does.
    """
    bg = (30, 30, 30) if dark else WHITE
    return [[(" ", DEFAULT, bg, False) for _ in range(5)] for _ in range(4)]


def write(grid, x, y, ch=None, fg=None, bg=None, bold=None):
    old = grid[y][x]
    grid[y][x] = (old[0] if ch is None else ch, old[1] if fg is None else fg,
                  old[2] if bg is None else bg, old[3] if bold is None else bold)


def exact_border():
    g = blank()
    for x, ch in enumerate("+-+"):
        write(g, x, 0, ch=ch, fg=RED)
        write(g, x, 2, ch=ch, fg=BLUE)
    write(g, 0, 1, ch="|", fg=YELLOW)
    write(g, 2, 1, ch="|", fg=GREEN)
    return g


def expectations():
    tests = {name: blank() for name in [
        "border_partial", "border_offscreen", "border_encloses_viewport",
        "border_clipped", "border_sides", "border_clip_visible", "border_transparent",
        "text_clipped", "nested_text_clips", "transformed_clip", "empty_clip", "text_negative",
        "text_scrolled", "transparent_text", "shadow_offset", "shadow_clipped",
        "shadow_offscreen", "rect_clipped_control", "image_scroll_control", "focus_exact",
        "focus_partial", "focus_offscreen", "invalid_geometry",
    ]}
    tests["border_exact"] = exact_border()
    tests["border_scrolled"] = exact_border()
    g = tests["border_partial"]
    write(g, 2, 0, ch="|", fg=GREEN)
    for x, ch in enumerate("--+"):
        write(g, x, 1, ch=ch, fg=BLUE)
    g = tests["border_sides"]
    for x in range(3):
        write(g, x, 0, ch="-", fg=RED)
        write(g, x, 2, ch="-", fg=BLUE)
    g = tests["border_clip_visible"]
    for x in (1, 2):
        write(g, x, 0, ch="-", fg=RED)
        write(g, x, 2, ch="-", fg=BLUE)
    g = tests["border_transparent"]
    write(g, 0, 0, ch="|", fg=YELLOW)
    write(g, 2, 0, ch="|", fg=GREEN)
    write(g, 0, 1, ch="|", fg=YELLOW)
    write(g, 2, 1, ch="|", fg=GREEN)
    for x, ch in enumerate("+-+"):
        write(g, x, 2, ch=ch, fg=BLUE)
    g = tests["text_clipped"]
    for x, ch in ((1, "B"), (2, "C")):
        write(g, x, 1, ch=ch, fg=BLACK)
    g = tests["nested_text_clips"]
    for x, ch in ((2, "C"), (3, "D")):
        write(g, x, 1, ch=ch, fg=BLACK)
    for x, ch in enumerate("1234"):
        write(g, x, 2, ch=ch, fg=BLACK)
    g = tests["transformed_clip"]
    for x, ch in ((1, "A"), (2, "B")):
        write(g, x, 1, ch=ch, fg=BLACK)
    for x, ch in enumerate("XY"):
        write(g, x, 0, ch=ch, fg=BLACK)
    for x, ch in enumerate("OK"):
        write(tests["empty_clip"], x, 0, ch=ch, fg=BLACK)
    for name, word in (("text_negative", "BCDE"), ("text_scrolled", "CDE")):
        for x, ch in enumerate(word):
            write(tests[name], x, 0, ch=ch, fg=BLACK)
    for x, ch in enumerate("RED"):
        write(tests["transparent_text"], x, 1, ch=ch, fg=(255, 127, 127))
    # The bitmap shadow painter uses 0.6 of the specified alpha on its core.
    # The terminal samples the unblurred rectangle using that same core alpha.
    for x in (2, 3):
        write(tests["shadow_offset"], x, 1, bg=(255, 179, 179))
    write(tests["shadow_clipped"], 3, 1, bg=(255, 179, 179))
    for x in (1, 2):
        write(tests["rect_clipped_control"], x, 1, bg=RED)
    write(tests["image_scroll_control"], 0, 0, bg=GREEN)
    for x, y in ((0, 0), (1, 0), (0, 1), (1, 1)):
        write(tests["focus_exact"], x, y, fg=FOCUS, bold=True)
    for x, y in ((2, 0), (0, 1), (1, 1), (2, 1)):
        write(tests["focus_partial"], x, y, fg=FOCUS, bold=True)
    tests["empty_viewport"] = []
    return tests


def unicode_expectations():
    """Hand-specified grids for the Unicode cases (Phase 241).

    These are the cases the byte-at-a-time renderer got wrong. A cell is a
    character: `é` is one cell, not two; 日 is one cell that occupies two
    columns; a combining accent joins the letter before it rather than taking a
    cell of its own; and a control character (which a page can ask for with a
    numeric reference) is U+FFFD, never a byte a terminal would act on.
    """
    tests = {}
    black = BLACK

    g = [[(" ", DEFAULT, (30, 30, 30), False) for _ in range(11)] for _ in range(4)]
    for x, ch in enumerate("café résumé"):
        write(g, x, 0, ch=ch, fg=black)
    tests["unicode_latin"] = g

    g = blank(dark=True)
    # 日本語 needs six columns and the viewport has five, so 日 and 本 are
    # drawn (four columns) and 語 is dropped rather than cut in half. Each wide
    # character is one cell; the column beside it is the blank second half.
    for (col, cluster) in ((0, "日"), (2, "本")):
        write(g, col, 0, ch=cluster, fg=black)
        # The column beside a wide character is a cell of its own: it draws a
        # space, in the character's colours, so a background colour on the text
        # spans both columns instead of stopping halfway through the glyph.
        write(g, col + 1, 0, ch=" ", fg=black)
    tests["unicode_wide"] = g

    g = blank(dark=True)
    # e + U+0301 is one cluster and one cell, so X lands in column 1, not 2.
    # The cell holds the whole cluster; the ansi stream carries its two code
    # points together, and the decoder reads them as one cell.
    write(g, 0, 0, ch="e\u0301", fg=black)
    write(g, 1, 0, ch="X", fg=black)
    tests["unicode_combining"] = g

    g = blank(dark=True)
    for x, ch in enumerate(["A", "\ufffd", "B", "\ufffd", "C"]):
        write(g, x, 0, ch=ch, fg=black)
    tests["unicode_control"] = g

    g = blank(dark=True)
    # "A" <80> "B" <E6> <97>: the stray continuation is one replacement
    # character, and the truncated three-byte sequence is two, because each
    # byte that cannot start or continue a sequence is replaced on its own.
    # A decoder that consumed the pair as one would put B in the wrong column.
    for x, ch in enumerate(["A", "\ufffd", "B", "\ufffd", "\ufffd"]):
        write(g, x, 0, ch=ch, fg=black)
    tests["unicode_malformed"] = g
    return tests


def self_test():
    valid = (b"\x1b[38;2;220;220;220m\x1b[48;2;255;255;255m "
             b"\x1b[0m\n\x1b[0m")
    got = decode(valid, 1, 1)
    assert got == [[(" ", DEFAULT, WHITE, False)]]
    assert got != [[("X", DEFAULT, WHITE, False)]]
    for broken in (valid[:-1], valid.replace(b"220m", b"oops", 1), valid.replace(b"\x1b[0m\n", b"\n")):
        try:
            decode(broken, 1, 1)
        except ValueError:
            pass
        else:
            raise AssertionError("malformed ANSI was accepted")
    print("ANSI verifier self-test: 5/5 (decode, wrong cell, three malformed streams)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", default="src/render/TerminalRenderer.cpp")
    parser.add_argument("--self-test", action="store_true", help="test only the ANSI decoder")
    parser.add_argument("--include-dir", action="append", default=[],
                        help="an extra include directory in front of include/, for measuring a "
                             "pre-fix renderer whose header has a different cell structure")
    args = parser.parse_args()
    self_test()
    if args.self_test:
        return 0
    source = Path(args.source)
    if not source.is_absolute():
        source = ROOT / source
    with tempfile.TemporaryDirectory(prefix="litebird-terminal-") as tmp:
        executable = Path(tmp) / "fixture"
        command = shlex.split(os.environ.get("CXX", "g++")) + [
            "-std=c++20", "-O2",
            *[item for d in args.include_dir for item in ("-I", d)],
            "-I", str(ROOT / "include"),
            str(ROOT / "tools/conformance/terminal_clipping_fixture.cpp"), str(source),
            # The renderer reads its column widths from a generated table and
            # its combining classes from the Unicode tables, so a check that
            # compiles the renderer has to compile those too or the link fails.
            str(ROOT / "src/render/TerminalWidthTables.cpp"),
            "-o", str(executable),
        ]
        subprocess.run(command, check=True, cwd=ROOT)
        failures = 0
        cases = expectations()
        cases.update(unicode_expectations())
        for name, expected in cases.items():
            proc = subprocess.run([str(executable), name], capture_output=True, timeout=10)
            if proc.returncode:
                print(f"FAIL {name}: exited {proc.returncode}: {proc.stderr.decode(errors='replace').strip()}")
                failures += 1
                continue
            try:
                if name == "empty_viewport":
                    actual = decode(proc.stdout, 0, 0)
                elif name == "unicode_latin":
                    actual = decode(proc.stdout, 11, 4)
                else:
                    actual = decode(proc.stdout, 5, 4)
            except (ValueError, UnicodeDecodeError) as exc:
                print(f"FAIL {name}: {exc}")
                failures += 1
                continue
            if actual != expected:
                failures += 1
                mismatches = [(x, y, a, expected[y][x]) for y, row in enumerate(actual)
                              for x, a in enumerate(row) if a != expected[y][x]]
                print(f"FAIL {name}: {len(mismatches)} wrong cells; first: {mismatches[0]}")
            else:
                print(f"PASS {name}")
        print(f"Terminal cell checks: {len(cases) - failures}/{len(cases)} pass")
        return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
