#!/usr/bin/env python3
# Regenerates src/text/BidiTables.cpp: the Unicode tables the UAX #9 pass needs.
#
# Sources (no network access required; pass --ucd-dir with the files, or let it
# fetch them through the GitHub contents API as tools/generate_string_unicode_tables.py
# does):
#   * Bidi_Class            - DerivedBidiClass.txt, Unicode 17.0.0 (unicode-org/unicodetools,
#                             unicodetools/data/ucd/17.0.0/extracted/DerivedBidiClass.txt).
#                             Its @missing lines give the classes of unassigned code points,
#                             which UAX #9's defaults require; they are written into the
#                             table as explicit ranges so lookups need no second pass.
#   * Bidi_Mirrored pairs   - BidiMirroring.txt, Unicode 17.0.0 (same tag).
#   * Bidi_Paired_Bracket   - BidiBrackets.txt, Unicode 17.0.0 (same tag), used by rule N0.
#
# The engine's other Unicode tables are Unicode 17.0 as well (tools/
# generate_string_unicode_tables.py pulls the same version from unicode-org/icu
# release-78.2), so this keeps one version across the tree.
#
# Usage:  python3 tools/gen_bidi_tables.py [--ucd-dir DIR] > src/text/BidiTables.cpp

import argparse
import base64
import json
import os
import sys
import urllib.request

FILES = [
    ("unicode-org/unicodetools", None,
     "unicodetools/data/ucd/17.0.0/extracted/DerivedBidiClass.txt", "DerivedBidiClass.txt"),
    ("unicode-org/unicodetools", None,
     "unicodetools/data/ucd/17.0.0/BidiMirroring.txt", "BidiMirroring.txt"),
    ("unicode-org/unicodetools", None,
     "unicodetools/data/ucd/17.0.0/BidiBrackets.txt", "BidiBrackets.txt"),
]

# The Bidi_Class values, in the order of litebird::BidiClass in
# include/litebird/text/Bidi.hpp. The generated table stores these numbers.
CLASSES = [
    "L", "R", "AL", "EN", "ES", "ET", "AN", "CS", "NSM", "BN", "B", "S", "WS", "ON",
    "LRE", "RLE", "LRO", "RLO", "PDF", "LRI", "RLI", "FSI", "PDI",
]
ALIASES = {
    "Left_To_Right": "L", "Right_To_Left": "R", "Arabic_Letter": "AL",
    "European_Number": "EN", "European_Separator": "ES", "European_Terminator": "ET",
    "Arabic_Number": "AN", "Common_Separator": "CS", "Nonspacing_Mark": "NSM",
    "Boundary_Neutral": "BN", "Paragraph_Separator": "B", "Segment_Separator": "S",
    "White_Space": "WS", "Other_Neutral": "ON", "Left_To_Right_Embedding": "LRE",
    "Right_To_Left_Embedding": "RLE", "Left_To_Right_Override": "LRO",
    "Right_To_Left_Override": "RLO", "Pop_Directional_Format": "PDF",
    "Left_To_Right_Isolate": "LRI", "Right_To_Left_Isolate": "RLI",
    "First_Strong_Isolate": "FSI", "Pop_Directional_Isolate": "PDI",
}


def fetch_gh(repo, ref, path, dest):
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    url = f"https://api.github.com/repos/{repo}/contents/{path}" + (f"?ref={ref}" if ref else "")
    request = urllib.request.Request(url, headers={"Accept": "application/vnd.github.raw"})
    with urllib.request.urlopen(request, timeout=180) as response:
        data = response.read()
    if data.startswith(b"{"):  # a JSON error body, not the file
        raise SystemExit(f"{path}: {data[:200]!r}")
    open(dest, "wb").write(data)


def parse_class(name):
    name = name.strip()
    if name in CLASSES:
        return name
    if name in ALIASES:
        return ALIASES[name]
    raise SystemExit(f"unknown Bidi_Class value: {name!r}")


def read_bidi_classes(path):
    """[(first, last, class)] over all of Unicode, defaults filled in."""
    entries = []   # explicit assignments
    defaults = []  # the @missing lines
    for raw in open(path, encoding="utf-8"):
        # A whole-line comment, but one kind of them is data: the defaults for
        # every code point the file does not assign. Read those first, before
        # the "nothing left after the comment" test throws the line away.
        if raw.lstrip().startswith("# @missing:"):
            body = raw.split("@missing:", 1)[1]
            span, value = [p.strip() for p in body.split(";")[:2]]
            defaults.append((span, parse_class(value)))
            continue
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        span, value = [p.strip() for p in line.split(";")[:2]]
        entries.append((span, parse_class(value)))

    def span_range(span):
        if ".." in span:
            a, b = span.split("..")
            return int(a, 16), int(b, 16)
        return int(span, 16), int(span, 16)

    ranges = []
    for span, cls in entries:
        a, b = span_range(span)
        ranges.append((a, b, cls))
    # Fill every code point the file does not name from the @missing defaults.
    named = set()
    for a, b, _ in ranges:
        named.update(range(a, b + 1))
    # The @missing lines run from the broadest default (all of Unicode is
    # Left_To_Right) to the most specific (a block that defaults to
    # Arabic_Letter), so the most specific one wins and the order here is
    # reversed.
    for span, cls in reversed(defaults):
        a, b = span_range(span)
        for cp in range(a, b + 1):
            if cp not in named:
                ranges.append((cp, cp, cls))
                named.add(cp)
    ranges.sort()
    merged = []
    for a, b, cls in ranges:
        if merged and merged[-1][2] == cls and merged[-1][1] + 1 == a:
            merged[-1] = (merged[-1][0], b, cls)
        else:
            merged.append((a, b, cls))
    return merged


def read_pairs(path, columns):
    """[(a, b, extra)] from a '# comment' + 'a; b; extra' file."""
    out = []
    for raw in open(path, encoding="utf-8"):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = [p.strip() for p in line.split(";")]
        out.append(tuple(int(parts[i], 16) for i in range(columns)))
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ucd-dir", default=None, help="directory holding the three UCD files")
    args = parser.parse_args()

    ucd = args.ucd_dir or os.path.join(os.path.expanduser("~"), ".cache", "litebird-ucd", "17.0.0")
    for repo, ref, path, name in FILES:
        fetch_gh(repo, ref, path, os.path.join(ucd, name))

    classes = read_bidi_classes(os.path.join(ucd, "DerivedBidiClass.txt"))
    brackets = read_pairs(os.path.join(ucd, "BidiBrackets.txt"), 2)
    bracket_type = {}
    for raw in open(os.path.join(ucd, "BidiBrackets.txt"), encoding="utf-8"):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = [p.strip() for p in line.split(";")]
        bracket_type[int(parts[0], 16)] = parts[2]
    mirrors = read_pairs(os.path.join(ucd, "BidiMirroring.txt"), 2)

    out = sys.stdout
    out.write(
        "// GENERATED FILE - do not edit by hand.\n"
        "// Regenerate with: python3 tools/gen_bidi_tables.py > src/text/BidiTables.cpp\n"
        "// Sources, all Unicode 17.0.0 (unicode-org/unicodetools, unicodetools/data/ucd/17.0.0/):\n"
        "//   extracted/DerivedBidiClass.txt - Bidi_Class, the @missing defaults written out.\n"
        "//   BidiMirroring.txt              - Bidi_Mirrored glyph pairs (rule L4).\n"
        "//   BidiBrackets.txt               - Bidi_Paired_Bracket and its opening/closing kind (rule N0).\n"
        "// clang-format off\n"
        '#include "litebird/text/BidiTables.hpp"\n'
        "\n"
        "namespace litebird {\n"
        "\n"
        "namespace {\n"
        "\n"
        f"constexpr BidiClassRange kClassRanges[] = {{\n"
    )
    for a, b, cls in classes:
        out.write(f"    {{0x{a:X}, 0x{b:X}, {CLASSES.index(cls)}}},\n")
    out.write("};\n\nconstexpr BidiBracketPair kBracketPairs[] = {\n")
    for cp, paired in brackets:
        opening = "true" if bracket_type[cp] == "o" else "false"
        out.write(f"    {{0x{cp:X}, 0x{paired:X}, {opening}}},\n")
    out.write("};\n\nconstexpr BidiMirrorPair kMirrorPairs[] = {\n")
    for cp, mirror in mirrors:
        out.write(f"    {{0x{cp:X}, 0x{mirror:X}}},\n")
    out.write(
        "};\n\n"
        "}  // namespace\n\n"
        "const BidiClassRange* bidi_class_ranges(std::size_t& count) {\n"
        "    count = sizeof(kClassRanges) / sizeof(kClassRanges[0]);\n"
        "    return kClassRanges;\n"
        "}\n\n"
        "const BidiBracketPair* bidi_bracket_pairs(std::size_t& count) {\n"
        "    count = sizeof(kBracketPairs) / sizeof(kBracketPairs[0]);\n"
        "    return kBracketPairs;\n"
        "}\n\n"
        "const BidiMirrorPair* bidi_mirror_pairs(std::size_t& count) {\n"
        "    count = sizeof(kMirrorPairs) / sizeof(kMirrorPairs[0]);\n"
        "    return kMirrorPairs;\n"
        "}\n\n"
        "}  // namespace litebird\n"
    )
    sys.stderr.write(
        f"{len(classes)} class ranges, {len(brackets)} bracket pairs, {len(mirrors)} mirror pairs\n"
    )


if __name__ == "__main__":
    main()
