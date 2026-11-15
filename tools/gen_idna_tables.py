#!/usr/bin/env python3
# Regenerates src/net/IdnaTables.cpp: the Unicode tables litebird needs for
# UTS #46 (IDNA compatibility processing) as used by WHATWG URL parsing.
#
# Sources (no network access required):
#   * UTS #46 mapping table   - the `idna` package's uts46data module
#                               (pass its parent directory with --idna-pkg,
#                               or have `idna` importable). Unicode 15.1.0.
#   * Combining marks, combining classes, canonical decompositions,
#     composition pairs, bidirectional classes - the Python standard library's
#     unicodedata (Python 3.11.2 ships Unicode 14.0.0; the mapping table and
#     joining types are Unicode 15.1.0, so the mix is not one uniform version;
#     what drifts is the treatment of code points assigned in Unicode 15+,
#     which are mostly unassigned today and would only miss bidi data). The
#     generated header states both versions.
#   * Joining types -
#     the `idna` package's idnadata module (needed by the contextJ rule of
#     UTS #46; WHATWG's CheckJoiners covers ZWNJ/ZWJ only).
#   * Composition pairs - derived, then verified against
#     unicodedata.normalize("NFC", a + b) so composition exclusions need no
#     separate table.
#
# Usage:  python3 tools/gen_idna_tables.py [--idna-pkg /path/to/pkg-parent] > src/net/IdnaTables.cpp

import argparse
import sys
import unicodedata

parser = argparse.ArgumentParser()
parser.add_argument("--idna-pkg", default=None, help="directory containing the `idna` package")
args = parser.parse_args()

if args.idna_pkg:
    sys.path.insert(0, args.idna_pkg)

from idna import uts46data, idnadata  # noqa: E402

ud_version = unicodedata.unidata_version
uts46_version = getattr(uts46data, "__version__", "unknown")

STATUS = {"V": 0, "M": 1, "D": 2, "3": 3, "X": 4, "I": 5}

# --- helpers -------------------------------------------------------------------

def emit_ranges(out, name, ctype, items, comment):
    # items: list of (lo, hi, payload...) tuples; payload flattened per item
    out.append(f"// {comment}")
    out.append(f"constexpr {ctype} {name}[] = {{")
    line = "    "
    count = 0
    for item in items:
        piece = "{" + ", ".join(str(v) for v in item) + "},"
        if len(line) + len(piece) + 1 > 100:
            out.append(line.rstrip())
            line = "    "
        line += piece + " "
        count += 1
    if line.strip():
        out.append(line.rstrip())
    out.append("};")
    out.append(f"constexpr std::size_t {name}Count = {count};")
    out.append("")
    return count

def ranges_from_bool(predicate, limit=0x110000):
    ranges = []
    start = None
    for cp in range(limit):
        if predicate(cp):
            if start is None:
                start = cp
        else:
            if start is not None:
                ranges.append((start, cp - 1))
                start = None
    if start is not None:
        ranges.append((start, limit - 1))
    return ranges

out = []
out.append("// GENERATED FILE - do not edit by hand.")
out.append("// Regenerate with: python3 tools/gen_idna_tables.py --idna-pkg /path/to/extracted/idna-pkg > src/net/IdnaTables.cpp")
out.append("// Sources:")
out.append(f"//   UTS46 mapping table and joining types: idna package {uts46_version}.")
out.append(f"//   Combining marks, combining classes, decompositions, composition pairs, bidi classes:")
out.append(f"//   Python unicodedata (Unicode {ud_version}). New code points assigned after")
out.append(f"//   Unicode {ud_version.split('.')[0]}.0 that need bidi data or NFC support are")
out.append("//   therefore not covered; today those are all unassigned PVALID code points.")
out.append("// idna package idnadata (joining types).")
out.append("// clang-format off")
out.append('#include "litebird/net/IdnaTables.hpp"')
out.append("")
out.append("namespace litebird {")
out.append("")

# --- UTS #46 mapping table + mapping pool ------------------------------------

mapping_pool = bytearray()
_pool_cache = {}

def pool_append(text):
    global mapping_pool
    offset = len(mapping_pool)
    mapping_pool += text.encode("utf-8")
    return offset

def pool_offset(text):
    if text not in _pool_cache:
        encoded = text.encode("utf-8")
        _pool_cache[text] = (pool_append(text), len(encoded))
    return _pool_cache[text]

uts46_items = []
import bisect as _bisect
_rows = uts46data.uts46data

def _row_for(cp):
    return _rows[_bisect.bisect_left(_rows, (cp, "Z")) - 1]

continue_start = 0
prev = None
for cp in range(0x110000):
    entry = _row_for(cp)
    status_code = STATUS[entry[1]]
    mapping = entry[2] if len(entry) > 2 else None
    if mapping is not None:
        pool_off, map_len = pool_offset(mapping)
    else:
        pool_off, map_len = 0xFFFFFFFF, 0
    key = (status_code, pool_off, map_len)
    if prev is None:
        prev = key
        continue
    if key != prev:
        uts46_items.append((continue_start, cp - 1, prev[0], prev[1], prev[2]))
        continue_start = cp
        prev = key
uts46_items.append((continue_start, 0x10FFFF, prev[0], prev[1], prev[2]))
covered = sum(r[1] - r[0] + 1 for r in uts46_items)
assert covered == 0x110000, f"UTS46 ranges do not cover the whole range: {covered}"

# --- general categories: marks (Mn, Mc, Me) for the leading-mark check --------

mark_ranges = ranges_from_bool(lambda cp: unicodedata.category(chr(cp))[0] == "M")

# --- combining classes (canonical, ccc > 0) -----------------------------------

ccc_ranges = []
start = None
for cp in range(0x110000):
    ccc = unicodedata.combining(chr(cp))
    if ccc != 0:
        if start is None:
            start = (cp, ccc)
        elif start[1] != ccc:
            ccc_ranges.append((start[0], cp - 1, start[1]))
            start = (cp, ccc)
    else:
        if start is not None:
            ccc_ranges.append((start[0], cp - 1, start[1]))
            start = None
if start is not None:
    ccc_ranges.append((start[0], 0x10FFFF, start[1]))

# --- canonical decompositions (recursive, compat excluded) ---------------------

decomp_pool = []          # flattened codepoints
decomp_items = []         # (cp, offset, length)

def canonical_expansion(cp):
    d = unicodedata.decomposition(chr(cp))
    if not d or d.startswith("<"):
        return None
    parts = [int(p, 16) for p in d.split()]
    out = []
    for p in parts:
        sub = canonical_expansion(p)
        if sub is None:
            out.append(p)
        else:
            out.extend(sub)
    return out

for cp in range(0x110000):
    exp = canonical_expansion(cp)
    if exp:
        offset = len(decomp_pool)
        decomp_pool.extend(exp)
        decomp_items.append((cp, offset, len(exp)))

# --- canonical composition pairs, verified via NFC ------------------------------

pair_items = []
for cp in range(0x110000):
    # Derive pairs from the SINGLE-STEP canonical decomposition, not the full
    # recursive expansion: double-accented precomposed letters (e.g. U+01D6 ->
    # 00FC 0304, whose full expansion is 0075 0308 0304) still need their
    # (starter, mark) pair emitted. The NFC round-trip check keeps composition
    # exclusions out.
    d = unicodedata.decomposition(chr(cp))
    if not d or d.startswith("<"):
        continue
    parts = [int(p, 16) for p in d.split()]
    if len(parts) != 2:
        continue
    a, b = parts
    if unicodedata.normalize("NFC", chr(a) + chr(b)) == chr(cp):
        pair_items.append(((a << 21) | b, cp))
pair_items.sort(key=lambda p: p[0])

# --- bidirectional classes (only the ones UTS #46 talks about) ----------------

BIDI = {"L": 1, "R": 2, "AL": 3, "EN": 4, "AN": 5, "ES": 6, "CS": 7, "ET": 8,
        "ON": 9, "BN": 10, "NSM": 11, "WS": 12}

bidi_ranges = []
start = None
start_cls = None
for cp in range(0x110000):
    cls = BIDI.get(unicodedata.bidirectional(chr(cp)))
    if cls == start_cls:
        continue
    if start is not None:
        bidi_ranges.append((start, cp - 1, start_cls))
    start = cp if cls is not None else None
    start_cls = cls
if start is not None:
    bidi_ranges.append((start, 0x10FFFF, start_cls))

# --- joining types (Arabic contextJ rule) ---------------------------------------

JOINING = {ord("L"): 1, ord("R"): 2, ord("D"): 3, ord("T"): 4}

joining_ranges = []
start = None
start_jt = None
for cp in range(0x110000):
    jt = JOINING.get(idnadata.joining_types.get(cp, 0))
    if jt == start_jt:
        continue
    if start is not None:
        joining_ranges.append((start, cp - 1, start_jt))
    start = cp if jt is not None else None
    start_jt = jt
if start is not None:
    joining_ranges.append((start, 0x10FFFF, start_jt))

# --- emit -------------------------------------------------------------------------

out.append("namespace {")
out.append("")

out.append(f"constexpr Uts46Range kUts46Ranges[] = {{")
line = "    "
for item in uts46_items:
    piece = "{0x%x, 0x%x, %d, %uu, %uu}," % item
    if len(line) + len(piece) + 1 > 110:
        out.append(line.rstrip())
        line = "    "
    line += piece + " "
if line.strip():
    out.append(line.rstrip())
out.append("};")
out.append(f"constexpr std::size_t kUts46RangeCount = {len(uts46_items)};")
out.append("")

pool_lines = []
for i in range(0, len(mapping_pool), 32):
    pool_lines.append("    " + ", ".join("0x%02x" % b for b in mapping_pool[i:i + 32]) + ",")
out.append("constexpr unsigned char kMappingPool[] = {")
out.extend(pool_lines)
out.append("};")
out.append(f"constexpr std::size_t kMappingPoolSize = {len(mapping_pool)};")
out.append("")

mark_ranges = [(lo, hi) for lo, hi in mark_ranges]
emit_ranges(out, "kMarkRanges", "Uts46Range2", mark_ranges, "General category M (marks), for the leading-mark check")
emit_ranges(out, "kCombiningClassRanges", "Uts46Range2U8", ccc_ranges, "Canonical combining classes, ccc > 0")

out.append("constexpr uint32_t kDecompPool[] = {")
line = "    "
for cp in decomp_pool:
    piece = "0x%x," % cp
    if len(line) + len(piece) + 1 > 110:
        out.append(line.rstrip())
        line = "    "
    line += piece + " "
if line.strip():
    out.append(line.rstrip())
out.append("};")
out.append(f"constexpr std::size_t kDecompPoolSize = {len(decomp_pool)};")
out.append("")

emit_ranges(out, "kDecompEntries", "Uts46Decomp", decomp_items, "Canonical decompositions (recursive expansion), sorted by cp")
emit_ranges(out, "kCompositionPairs", "Uts46Composition", pair_items, "Canonical composition pairs, verified through NFC; key = (starter << 21) | combining")

bidi_ranges = [(lo, hi, cls) for lo, hi, cls in bidi_ranges]
emit_ranges(out, "kBidiRanges", "Uts46Range2U8", bidi_ranges, "Bidi classes for UTS #46 checkBidi (L=1 R=2 AL=3 EN=4 AN=5 ES=6 CS=7 ET=8 ON=9 BN=10 NSM=11 WS=12)")
emit_ranges(out, "kJoiningTypeRanges", "Uts46Range2U8", joining_ranges, "Joining types for the contextJ rule (L=1 R=2 D=3 T=4)")

out.append("} // namespace")
out.append("")
out.append("const Uts46Range* uts46_ranges() { return kUts46Ranges; }")
out.append("std::size_t uts46_range_count() { return kUts46RangeCount; }")
out.append("const unsigned char* mapping_pool() { return kMappingPool; }")
out.append("std::size_t mapping_pool_size() { return kMappingPoolSize; }")
out.append("const Uts46Range2* mark_ranges() { return kMarkRanges; }")
out.append("std::size_t mark_range_count() { return kMarkRangesCount; }")
out.append("const Uts46Range2U8* combining_class_ranges() { return kCombiningClassRanges; }")
out.append("std::size_t combining_class_range_count() { return kCombiningClassRangesCount; }")
out.append("const uint32_t* decomp_pool() { return kDecompPool; }")
out.append("std::size_t decomp_pool_size() { return kDecompPoolSize; }")
out.append("const Uts46Decomp* decomp_entries() { return kDecompEntries; }")
out.append("std::size_t decomp_entry_count() { return kDecompEntriesCount; }")
out.append("const Uts46Composition* composition_pairs() { return kCompositionPairs; }")
out.append("std::size_t composition_pair_count() { return kCompositionPairsCount; }")
out.append("const Uts46Range2U8* bidi_ranges() { return kBidiRanges; }")
out.append("std::size_t bidi_range_count() { return kBidiRangesCount; }")
out.append("const Uts46Range2U8* joining_type_ranges() { return kJoiningTypeRanges; }")
out.append("std::size_t joining_type_range_count() { return kJoiningTypeRangesCount; }")
_parts = uts46_version.split(".")
utsv = int(_parts[0]) * 10000 + int(_parts[1]) * 100 + (int(_parts[2]) if len(_parts) > 2 else 0)
out.append(f"std::size_t idna_tables_unicode_version() {{ return {utsv}; }} // packed as major*10000+minor*100+patch for {uts46_version}; bidi/decomposition/composition tables derive from unicodedata {ud_version}")
out.append("")
out.append("} // namespace litebird")

print("\n".join(out))
print(f"// marks={len(mark_ranges)} ccc={len(ccc_ranges)} decomp={len(decomp_items)} pairs={len(pair_items)} bidi={len(bidi_ranges)} joining={len(joining_ranges)} pool={len(mapping_pool)}B", file=sys.stderr)
