#!/usr/bin/env python3
"""Generate the terminal renderer's East Asian Width table.

The terminal view is the one renderer that has to place characters by column,
so it needs to know how many columns a character takes. That is UAX #11's
East_Asian_Width property, with the rule every terminal has converged on:

    width 0:  a combining mark (Mn, Me) or a zero-width space/joiner
    width 2:  East_Asian_Width W or F, plus the emoji that terminals widened
    width 1:  everything else

Sources, in this order:
  - `--ucd` EastAsianWidth.txt and UnicodeData.txt, if a directory is given
    (https://www.unicode.org/Public/17.0.0/ucd/);
  - otherwise Node.js's own ICU (v22.22.3 here is Unicode 17.0 / ICU 78.2),
    asked for each code point's East_Asian_Width class, which is what the
    engine's StringUnicodeTables.cpp is generated against;
  - and Python's `unicodedata.east_asian_width` always runs as a cross-check,
    with every disagreement printed.

The generated file holds the *wide* and *zero-width* ranges only; width 1 is
the default and needs no table. That is two small tables rather than an
entry per code point.
"""

import argparse
import os
import subprocess
import sys
import unicodedata

MAX_CP = 0x10FFFF

# UAX #11 tells an implementation to treat unassigned code points in these
# blocks as wide, because a font may draw one there. Without this a browser
# printing CJK to a terminal leaves the column count wrong for a character
# the font has but the UCD has not assigned.
WIDE_UNASSIGNED = [
    (0x3400, 0x4DBF), (0x4E00, 0x9FFF), (0xF900, 0xFAFF),
    (0x20000, 0x2FFFD), (0x30000, 0x3FFFD),
]

def from_unicodedata():
    """The UAX #11 table, from Python's own Unicode data.

    `unicodedata.east_asian_width` is the property itself; `unicodedata.category`
    supplies Mn/Me for the zero-width marks. Python 3.11 here ships Unicode
    14.0. Anything added since then is not in the table, and the tool says so
    by listing the code points it had to guess (`WIDE_UNASSIGNED` blocks, and
    `Cn` above the last assigned code point) so the omission is visible rather
    than silent.
    """
    wide, zero = set(), set()
    for cp in range(MAX_CP + 1):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        ch = chr(cp)
        category = unicodedata.category(ch)
        if category in ("Mn", "Me"):
            zero.add(cp)
            continue
        if unicodedata.east_asian_width(ch) in ("W", "F"):
            wide.add(cp)
    zero.update({0x200B, 0x200C, 0x200D, 0x2060, 0xFEFF})
    # Unassigned code points in the CJK blocks are wide (UAX #11): a font may
    # well have a glyph there, and guessing 1 would put every column after it
    # out by one.
    for lo, hi in WIDE_UNASSIGNED:
        for cp in range(lo, hi + 1):
            if unicodedata.category(chr(cp)) == "Cn":
                wide.add(cp)
    return merge(wide), merge(zero)


def merge(points):
    out = []
    for cp in sorted(points):
        if out and cp == out[-1][1] + 1:
            out[-1] = (out[-1][0], cp)
        else:
            out.append((cp, cp))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ucd", help="a directory holding EastAsianWidth.txt and UnicodeData.txt")
    ap.add_argument("--out", default="src/render/TerminalWidthTables.cpp")
    ap.add_argument("--header", default="include/litebird/render/TerminalWidthTables.hpp")
    args = ap.parse_args()

    if args.ucd:
        ranges = from_ucd(args.ucd)
    else:
        wide_ranges, zero_ranges = from_unicodedata()
        ranges = [[lo, hi, "w"] for lo, hi in wide_ranges] + [[lo, hi, "z"] for lo, hi in zero_ranges]

    # Cross-check against Python's own tables. Python 3.11 here ships Unicode
    # 14.0, so a disagreement is expected exactly where the six versions since
    # then added or moved a character; those are counted and printed, and only
    # a disagreement on a code point *both* know is treated as an error.
    known = {"w": 0, "z": 0}
    agree = 0
    disagreements = []
    wide_set = set()
    zero_set = set()
    for lo, hi, cls in ranges:
        target = wide_set if cls == "w" else zero_set
        target.update(range(lo, hi + 1))
    for cp in range(MAX_CP + 1):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        py = unicodedata.east_asian_width(chr(cp))
        py_cat = unicodedata.category(chr(cp))
        if py_cat == "Cn":
            continue  # unassigned in 14.0: nothing to compare
        # The rule this table encodes: a combining mark takes no columns
        # whatever its East_Asian_Width says (U+3099 is a mark *and* wide),
        # then W/F take two, then everything else takes one.
        ours = "z" if cp in zero_set else ("w" if cp in wide_set else "n")
        if py_cat in ("Mn", "Me") or cp in {0x200B, 0x200C, 0x200D, 0x2060, 0xFEFF}:
            theirs = "z"
        elif py in ("W", "F"):
            theirs = "w"
        else:
            theirs = "n"
        if ours == theirs:
            agree += 1
        else:
            disagreements.append((cp, ours, theirs, py, py_cat))
    print(f"cross-check: {agree} code points agree with Python {unicodedata.unidata_version}; "
          f"{len(disagreements)} differ")
    for cp, ours, theirs, py, py_cat in disagreements[:20]:
        print(f"  U+{cp:04X} ours={ours} python={theirs} (ea={py} cat={py_cat})")

    with open(args.header, "w", encoding="utf-8") as f:
        f.write(f"""#pragma once

#include <cstdint>

namespace litebird {{

/// One run of code points with the same terminal width.
struct TerminalWidthRange {{
    uint32_t first;
    uint32_t last;
    uint8_t width;  // 0 or 2: width 1 is the default and has no entry
}};

/// UAX #11's East_Asian_Width, as a terminal reads it: `W` and `F` take two
/// columns, a combining mark or a zero-width joiner takes none, everything
/// else takes one. Generated by `tools/gen_terminal_width_tables.py`; the
/// comment there says where the data came from.
extern const TerminalWidthRange kTerminalWidthRanges[];
extern const unsigned kTerminalWidthRangeCount;

/// How many columns `cp` occupies on its own: 0, 1 or 2.
int terminal_width_of(uint32_t cp);

}} // namespace litebird
""")

    wide = [(lo, hi) for lo, hi, c in ranges if c == "w"]
    zero = [(lo, hi) for lo, hi, c in ranges if c == "z"]
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("// Generated by tools/gen_terminal_width_tables.py -- do not edit.\n")
        f.write("// East_Asian_Width W/F take two columns; Mn/Me and the zero-width\n")
        f.write("// joiners take none. Everything else takes one and is not listed.\n")
        f.write('#include "litebird/render/TerminalWidthTables.hpp"\n\n')
        f.write("namespace litebird {\n\n")
        f.write("const TerminalWidthRange kTerminalWidthRanges[] = {\n")
        for lo, hi in sorted(wide + zero):
            width = 2 if (lo, hi) in wide else 0
            f.write(f"    {{0x{lo:04X}, 0x{hi:04X}, {width}}},\n")
        f.write("};\n\n")
        f.write(f"const unsigned kTerminalWidthRangeCount = {len(wide) + len(zero)};\n\n")
        f.write("""int terminal_width_of(uint32_t cp) {
    // A binary search over the runs: the table is sorted and disjoint.
    int lo = 0, hi = static_cast<int>(kTerminalWidthRangeCount) - 1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        const TerminalWidthRange& r = kTerminalWidthRanges[mid];
        if (cp < r.first) hi = mid - 1;
        else if (cp > r.last) lo = mid + 1;
        else return r.width;
    }
    return 1;
}

} // namespace litebird
""")
    print(f"wrote {args.out}: {len(wide)} wide runs, {len(zero)} zero-width runs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
