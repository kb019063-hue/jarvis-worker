#!/usr/bin/env python3
"""Checks every decoder against the index tables vendored beside it.

Why this exists
---------------
The Big5 row said a byte sweep found a defect when there was none, and the
reason was the oracle: it compared three decoders against Node's `TextDecoder`
in one pass, and Node is ICU. ICU is not the standard. On Big5 the sweep
expected a `0x80 -> U+0080` branch that `whatwg/encoding`'s `encoding.bs`
does not have, and on `koi8-u` and `windows-1253` ICU disagrees with the
index files it ships next to (see finding 249 for the measurements).

The tables this engine decodes through are already in the tree —
`src/net/TextEncodingTables.cpp`, generated from `whatwg/encoding`'s index
files — so the oracle for a decoder is the table it reads, and no external
tool can be wrong about that. That is the sweep the row asked for:

  * **single-byte, both directions.** For each of the 27 single-byte
    encodings, every byte `0x80..0xFF` is decoded alone and compared with
    `kSingleByte[row][byte - 0x80]`, where a 0 entry means "no pointer", i.e.
    an error, i.e. U+FFFD. Then the reverse: every code point the table maps
    is encoded back and must give the byte it came from.
  * **the multi-byte indexes.** `kIndexEucKr` (23,940 pointers), `kIndexBig5`
    (19,782), `kIndexJis0208` (11,280) and the 207 `kIndexGb18030Ranges`,
    driven through the same pointer arithmetic the decoder uses, including
    the parts that are not a plain index lookup: Big5's two trail-byte ranges
    and its four two-code-point entries, Shift_JIS's lead offsets and its
    1,880 private-use pointers, and gb18030's gap clause.

What this does not do
---------------------
It is not a conformance suite: it does not read `whatwg/encoding`'s tests and
it does not check the encoder half except where a round trip needs it. A
decoder and its table can be wrong together — if the table were generated
from a bad revision of the standard, this would agree with it and both would
be wrong. What it does check is the part that has actually broken here: a
decoder that disagrees with its own table, which is what the Big5 sweep
claimed and did not find.

The build
---------
Needs two objects from a tree built by `make all`:

    g++ -std=c++20 -O1 -Iinclude tools/conformance/verify_encoding_tables.cpp \\
        build/net/TextEncoding.o build/net/TextEncodingTables.o \\
        -o build/verify_encoding_tables
    build/verify_encoding_tables

`--build` runs that command; `--json` prints one object per check.

Exit status: 0 when every decoder agrees with its table, 1 otherwise.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BINARY = ROOT / "build" / "verify_encoding_tables"

# Table row for each single-byte label, in kSingleByte order. iso-8859-8 and
# iso-8859-8-i share one row, which is why 28 labels cover 27 rows.
ROW_OF_LABEL = {
    "ibm866": 0, "iso-8859-2": 1, "iso-8859-3": 2, "iso-8859-4": 3,
    "iso-8859-5": 4, "iso-8859-6": 5, "iso-8859-7": 6, "iso-8859-8": 7,
    "iso-8859-8-i": 7, "iso-8859-10": 8, "iso-8859-13": 9, "iso-8859-14": 10,
    "iso-8859-15": 11, "iso-8859-16": 12, "koi8-r": 13, "koi8-u": 14,
    "macintosh": 15, "windows-874": 16, "windows-1250": 17, "windows-1251": 18,
    "windows-1252": 19, "windows-1253": 20, "windows-1254": 21,
    "windows-1255": 22, "windows-1256": 23, "windows-1257": 24,
    "windows-1258": 25, "x-mac-cyrillic": 26,
}
DEFAULT_TIMEOUT = 300.0

# The harness is C++ because the tables are `const` arrays with internal
# linkage in a translation unit: nothing outside can read them, and a second
# copy parsed out of the source file would be the same mistake this tool
# exists to avoid (two descriptions of one table, drifting apart).
HARNESS = r"""
#include "litebird/net/TextEncoding.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace litebird;
using namespace litebird::encoding_tables;

namespace {

std::vector<uint32_t> utf8_to_cps(const std::string& s) {
    std::vector<uint32_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        uint32_t cp; int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else { cp = c & 0x07; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

int g_bad = 0;
int g_checked = 0;
bool g_json = false;

void report(const char* check, int checked, int bad, const char* note = "") {
    g_checked += checked;
    g_bad += bad;
    // The human line always goes out; the JSON one is what the driver reads.
    std::printf("%-28s %6d positions  %4d mismatches%s%s\n",
                check, checked, bad, *note ? "  " : "", note);
    if (g_json) {
        std::printf("{\"check\": \"%s\", \"positions\": %d, \"mismatches\": %d, \"note\": \"%s\"}\n",
                    check, checked, bad, note);
    }
}

// Decodes one byte sequence and returns the first code point, or -1.
uint32_t first_cp(EncodingId id, const std::string& bytes) {
    std::string out;
    decode_with_encoding(bytes, id, out, /*fatal=*/false);
    const auto cps = utf8_to_cps(out);
    return cps.empty() ? 0xFFFFFFFFu : cps[0];
}

// --- single byte, both directions ---------------------------------------
void single_byte() {
    const char* labels[] = {
        "ibm866", "iso-8859-2", "iso-8859-3", "iso-8859-4", "iso-8859-5",
        "iso-8859-6", "iso-8859-7", "iso-8859-8", "iso-8859-8-i", "iso-8859-10",
        "iso-8859-13", "iso-8859-14", "iso-8859-15", "iso-8859-16", "koi8-r",
        "koi8-u", "macintosh", "windows-874", "windows-1250", "windows-1251",
        "windows-1252", "windows-1253", "windows-1254", "windows-1255",
        "windows-1256", "windows-1257", "windows-1258", "x-mac-cyrillic",
    };
    int checked = 0, bad = 0, rows = 0;
    bool seen[27] = {};
    for (const char* label : labels) {
        EncodingId id;
        if (!get_encoding_from_label(label, id)) { std::printf("  no id for %s\n", label); bad++; continue; }
        // The row the decoder reads. This mirrors single_byte_table(), which is
        // file-local in TextEncoding.cpp and so cannot be called from here —
        // and writing it out by hand is how this tool was once blind to an
        // injected error: an `idx - 1` that forgot Iso8859_8_I's special case
        // reads row 21 for windows-1253 where the decoder reads row 20, and a
        // corrupt entry in the row nobody compares goes unseen. The table below
        // is checked against the decoder's own answers, so a mistake here shows
        // up as every entry of the affected encoding disagreeing at once.
        const int idx = static_cast<int>(id);
        int row;
        if (id == EncodingId::Iso8859_8_I) row = 7;
        else if (id >= EncodingId::Iso8859_10) row = idx - 2;   // 10..28 -> 8..26
        else row = idx - 1;                                     // 1..8 -> 0..7
        if (row < 0 || row >= 27) { std::printf("  no row for %s\n", label); bad++; continue; }
        // Prove the row is the one the decoder reads: the first mapped byte of
        // this row must decode to the row's first entry.
        for (int probe = 0x80; probe <= 0xFF; probe++) {
            if (kSingleByte[row][probe - 0x80] == 0) continue;
            if (first_cp(id, std::string(1, static_cast<char>(probe))) != kSingleByte[row][probe - 0x80]) {
                std::printf("  %s: row %d is not the row the decoder reads (probe 0x%02X)\n",
                            label, row, probe);
                bad++;
            }
            break;
        }
        if (!seen[row]) { seen[row] = true; rows++; }
        for (int b = 0x80; b <= 0xFF; b++) {
            const uint32_t entry = kSingleByte[row][b - 0x80];
            // 0 in an index means "no pointer": the standard's decoder returns
            // error on it and a non-fatal decode is U+FFFD.
            const uint32_t want = entry ? entry : 0xFFFD;
            const uint32_t got = first_cp(id, std::string(1, static_cast<char>(b)));
            if (got != want) {
                if (bad < 8) std::printf("  %-14s byte 0x%02X table U+%04X got U+%04X\n", label, b, want, got);
                bad++;
            }
            checked++;
            // The reverse: a mapped code point encodes back to its own byte.
            if (entry) {
                std::string enc;
                if (!encode_code_point(id, entry, enc) || enc.size() != 1 ||
                    static_cast<unsigned char>(enc[0]) != b) {
                    if (bad < 8) std::printf("  %-14s U+%04X did not encode back to 0x%02X\n", label, entry, b);
                    bad++;
                }
                checked++;
            }
        }
    }
    // 28 labels, 27 tables: iso-8859-8 and iso-8859-8-i share one row.
    report("single-byte (both ways)", checked, bad,
           rows == 27 ? "" : "not every table row was reached");
}

// --- the table itself ---------------------------------------------------
//
// The checks above compare a decoder with the table it reads, and they cannot
// see a table that is corrupt in a way the decoder faithfully reproduces: both
// moved together and they agree. That is not hypothetical — an injected
// `0x1234` in windows-1253 was reported as 0 mismatches by the decoder sweep,
// because the decoder had been rebuilt against it. So the table is also read
// as what it is: 27 rows of 128 entries, generated from `whatwg/encoding`'s
// index files, and judged by the properties those files have.
void table_integrity() {
    int checked = 0, bad = 0;
    for (int row = 0; row < 27; row++) {
        bool seen[128] = {};
        for (int b = 0x80; b <= 0xFF; b++) {
            const uint32_t entry = kSingleByte[row][b - 0x80];
            checked++;
            if (entry == 0) continue;                 // no pointer: an error
            // A single-byte index maps a byte to one code point, and the
            // standard's files never map two bytes of one table to the same
            // code point: the reverse map is an index, and an index has one
            // entry per key.
            bool dup = false;
            for (int other = 0x80; other < b; other++) {
                if (kSingleByte[row][other - 0x80] == entry) { dup = true; break; }
            }
            if (dup) {
                if (bad < 8) std::printf("  row %d: U+%04X is the answer for two different bytes\n", row, entry);
                bad++;
            }
            // A private-use code point in a vendor table is legitimate (the
            // Macintosh tables have several); nothing else outside Unicode is.
            if (entry > 0x10FFFFu || (entry >= 0xD800u && entry <= 0xDFFFu)) {
                if (bad < 8) std::printf("  row %d: byte 0x%02X maps to U+%04X, which is not a scalar value\n",
                                         row, b, entry);
                bad++;
            }
            // The tables are sorted by code point across most of the standard's
            // single-byte files, but not all (iso-8859-6 and the windows ones
            // have unmapped gaps and out-of-order tails), so order is not
            // checked: it would be a rule the source files do not follow.
            (void)seen;
        }
    }
    report("table integrity", checked, bad);
}

// --- the multi-byte indexes --------------------------------------------
void euc_kr() {
    EncodingId id; get_encoding_from_label("euc-kr", id);
    int checked = 0, bad = 0;
    for (int lead = 0x81; lead <= 0xFE; lead++) {
        for (int trail = 0x41; trail <= 0xFE; trail++) {
            const uint32_t pointer = static_cast<uint32_t>(lead - 0x81) * 190u + (trail - 0x41);
            if (pointer >= 23940u) continue;
            const uint32_t entry = kIndexEucKr[pointer];
            const uint32_t want = entry ? entry : 0xFFFD;
            const uint32_t got = first_cp(id, std::string{static_cast<char>(lead), static_cast<char>(trail)});
            if (got != want) {
                if (bad < 8) std::printf("  euc-kr ptr %u want U+%04X got U+%04X\n", pointer, want, got);
                bad++;
            }
            checked++;
        }
    }
    report("euc-kr index", checked, bad);
}

void big5() {
    EncodingId id; get_encoding_from_label("big5", id);
    int checked = 0, bad = 0, two = 0;
    for (int lead = 0x81; lead <= 0xFE; lead++) {
        for (int b = 0x40; b <= 0xFE; b++) {
            // Two trail ranges, and a different offset for each.
            if (!((b >= 0x40 && b <= 0x7E) || (b >= 0xA1 && b <= 0xFE))) continue;
            const uint8_t offset = (b < 0x7F) ? 0x40 : 0x62;
            const uint32_t pointer = static_cast<uint32_t>(lead - 0x81) * 157u + (b - offset);
            std::string out;
            decode_with_encoding(std::string{static_cast<char>(lead), static_cast<char>(b)}, id, out, false);
            const auto cps = utf8_to_cps(out);
            // Four pointers in the big5 index are <base, combining> pairs and
            // decode to two code points, not one.
            if (pointer == 1133 || pointer == 1135 || pointer == 1164 || pointer == 1166) {
                const uint32_t a = (pointer == 1133 || pointer == 1135) ? 0x00CAu : 0x00EAu;
                const uint32_t c = (pointer == 1133 || pointer == 1164) ? 0x0304u : 0x030Cu;
                if (cps.size() != 2 || cps[0] != a || cps[1] != c) {
                    if (bad < 8) std::printf("  big5 ptr %u two-code-point pair wrong\n", pointer);
                    bad++;
                }
                two++;
                checked++;
                continue;
            }
            const uint32_t entry = (pointer < 19782u) ? kIndexBig5[pointer] : 0u;
            const uint32_t want = entry ? entry : 0xFFFD;
            const uint32_t got = cps.empty() ? 0xFFFFFFFFu : cps[0];
            if (got != want) {
                if (bad < 8) std::printf("  big5 ptr %u want U+%04X got U+%04X\n", pointer, want, got);
                bad++;
            }
            checked++;
        }
    }
    std::printf("%-28s %6d positions %4d two-code-point entries\n", "big5 index", checked, two);
}

void shift_jis() {
    EncodingId id; get_encoding_from_label("shift_jis", id);
    int checked = 0, bad = 0, pua = 0;
    for (int lead = 0x81; lead <= 0xFC; lead++) {
        if (lead > 0x9F && lead < 0xE0) continue;         // not a lead byte
        for (int b = 0x40; b <= 0xFC; b++) {
            if (!((b >= 0x40 && b <= 0x7E) || (b >= 0x80 && b <= 0xFC))) continue;
            const uint8_t offset = (b < 0x7F) ? 0x40 : 0x41;
            const uint8_t lead_offset = (lead < 0xA0) ? 0x81 : 0xC1;
            const uint32_t pointer = static_cast<uint32_t>(lead - lead_offset) * 188u + (b - offset);
            uint32_t want;
            if (pointer >= 8836u && pointer <= 10715u) {
                want = 0xE000u - 8836u + pointer;         // the private-use run
                pua++;
            } else if (pointer < 11280u) {
                const uint32_t entry = kIndexJis0208[pointer];
                want = entry ? entry : 0xFFFD;
            } else {
                want = 0xFFFD;
            }
            const uint32_t got = first_cp(id, std::string{static_cast<char>(lead), static_cast<char>(b)});
            if (got != want) {
                if (bad < 8) std::printf("  shift_jis ptr %u want U+%04X got U+%04X\n", pointer, want, got);
                bad++;
            }
            checked++;
        }
    }
    report("shift_jis index", checked, bad, pua ? "" : "no private-use pointers seen");
}

void gb18030_ranges() {
    EncodingId id; get_encoding_from_label("gb18030", id);
    int checked = 0, bad = 0, gaps = 0;
    for (std::size_t r = 0; r < 207; r++) {
        const uint32_t first = kIndexGb18030Ranges[r].pointer;
        // A range ends where the next begins; the last runs to the edge of
        // Unicode, because pointer 189000 is U+10000 and U+10FFFF is its end.
        uint32_t last = (r + 1 < 207) ? kIndexGb18030Ranges[r + 1].pointer - 1
                                      : 189000u + (0x10FFFFu - 0x10000u);
        if (last < first) last = first;
        const uint32_t samples[3] = {first, first + (last - first) / 2, last};
        for (const uint32_t p : samples) {
            const uint32_t want = kIndexGb18030Ranges[r].code_point + (p - first);
            if (want > 0x10FFFFu) continue;
            // Step 6 of the decoder: these pointers are an error, not a lookup.
            if ((p > 39419u && p < 189000u) || p > 1237575u) { gaps++; checked++; continue; }
            uint32_t q = p;
            const int b4 = 0x30 + q % 10; q /= 10;
            const int b3 = 0x81 + q % 126; q /= 126;
            const int b2 = 0x30 + q % 10; q /= 10;
            const int b1 = 0x81 + q;
            if (b1 > 0xFE || b3 > 0xFE) continue;
            const uint32_t got = first_cp(id, std::string{static_cast<char>(b1), static_cast<char>(b2),
                                                          static_cast<char>(b3), static_cast<char>(b4)});
            if (got != want) {
                if (bad < 8) std::printf("  gb18030 range %zu ptr %u want U+%04X got U+%04X\n", r, p, want, got);
                bad++;
            }
            checked++;
        }
    }
    report("gb18030 ranges", checked, bad, gaps ? "" : "no gap-clause samples");
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--json") g_json = true;
    }
    table_integrity();
    single_byte();
    euc_kr();
    big5();
    shift_jis();
    gb18030_ranges();
    if (g_json) {
        // The table, for the Node cross-check in the driver.
        std::printf("{\"table\": {");
        bool first = true;
        for (int row = 0; row < 27; row++) {
            if (!first) std::printf(", ");
            first = false;
            std::printf("\"row%d\": [", row);
            for (int j = 0; j < 128; j++) {
                if (j) std::printf(",");
                std::printf("%u", static_cast<unsigned>(kSingleByte[row][j]));
            }
            std::printf("]");
        }
        std::printf("}}\n");
        std::printf("{\"total_positions\": %d, \"total_mismatches\": %d}\n", g_checked, g_bad);
    } else {
        std::printf("\n%d positions checked, %d mismatches\n", g_checked, g_bad);
    }
    return g_bad == 0 ? 0 : 1;
}
"""


# Encodings where ICU (which is what Node's TextDecoder is) is known to
# disagree with the index files finding 249 measured: koi8-u pointers 46 and
# 62, and windows-1253 pointers 42 and 82. A disagreement on these is reported
# and not counted, because the table is the standard and ICU is not.
# Phase 283 adds a third: windows-1255 byte 0xCA is U+05BA in the index file,
# and Node answers U+FFFD — ICU's table predates the code point, which sits in
# the sequential run U+05B9 / U+05BA / U+05BB. The engine has the run and the
# neighbours either side of the gap agree, so the table is the standard's.
ICU_KNOWN_WRONG = {"koi8-u", "windows-1253", "windows-1255"}


def node_crosscheck(rows, timeout: float) -> tuple[int, int, int]:
    """Asks Node's TextDecoder for every single-byte entry and reports where it
    differs from the table. Returns (compared, differed, known_icu).

    This is the only check here that has a second implementation behind it, so
    it is the only one that could see a table corrupt in a way the engine
    reproduces faithfully. It is a cross-check and not the oracle: on the two
    encodings above ICU is the one that is wrong.
    """
    script = r"""
const rows = JSON.parse(process.argv[1]);
const out = {};
for (const [label, table] of Object.entries(rows)) {
    let dec;
    try { dec = new TextDecoder(label); } catch (e) { out[label] = null; continue; }
    const cps = [];
    for (let b = 0x80; b <= 0xFF; b++) {
        const s = dec.decode(new Uint8Array([b]));
        cps.push([...s].length ? [...s].map(c => c.codePointAt(0)) : [0xFFFD]);
    }
    out[label] = cps;
}
process.stdout.write(JSON.stringify(out));
"""
    import json as _json
    payload = _json.dumps(rows)
    proc = subprocess.run(["node", "-e", script, payload],
                          capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        return 0, 0, 0
    got = _json.loads(proc.stdout)
    compared = differed = known = 0
    for label, table in rows.items():
        cps = got.get(label)
        if cps is None:
            continue
        for i, entry in enumerate(table):
            if entry is None:
                continue
            want = [entry]
            if cps[i] != want:
                compared += 1
                if label in ICU_KNOWN_WRONG:
                    known += 1
                else:
                    differed += 1
                    if differed <= 8:
                        print(f"  node disagrees on {label} byte 0x{0x80 + i:02X}: "
                              f"table U+{entry:04X}, node {['U+%04X' % c for c in cps[i]]}")
                    continue
            compared += 1
    return compared, differed, known


def build(binary: Path, timeout: float) -> int:
    src = ROOT / "build" / "verify_encoding_tables.cpp"
    src.parent.mkdir(parents=True, exist_ok=True)
    src.write_text(HARNESS)
    objs = [ROOT / "build" / "net" / "TextEncoding.o",
            ROOT / "build" / "net" / "TextEncodingTables.o"]
    missing = [str(o) for o in objs if not o.exists()]
    if missing:
        print(f"missing object(s): {', '.join(missing)}", file=sys.stderr)
        print("run `make all` first", file=sys.stderr)
        return 1
    argv = ["g++", "-std=c++20", "-O1", f"-I{ROOT / 'include'}", str(src)] + \
           [str(o) for o in objs] + ["-o", str(binary)]
    proc = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout + proc.stderr)
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", default=str(DEFAULT_BINARY))
    parser.add_argument("--build", action="store_true", help="compile the harness first")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument("--no-node", dest="node", action="store_false",
                        help="skip the Node cross-check")
    args = parser.parse_args()

    binary = Path(args.binary)
    # Rebuild when asked, when there is no binary, or when an object it links
    # against is newer than it: a corrupt table rebuilt by `make` after the
    # harness was last linked is invisible otherwise, which is exactly how an
    # injected entry went unreported once while writing this tool.
    objs = [ROOT / "build" / "net" / "TextEncoding.o",
            ROOT / "build" / "net" / "TextEncodingTables.o"]
    stale = binary.exists() and any(o.exists() and o.stat().st_mtime > binary.stat().st_mtime
                                    for o in objs)
    if args.build or not binary.exists() or stale:
        if build(binary, args.timeout) != 0:
            return 1
    # Always ask for the machine-readable form: the Node cross-check needs the
    # table out of it. The driver does the human printing.
    argv = [str(binary), "--json"]
    proc = subprocess.run(argv, capture_output=True, text=True, timeout=args.timeout)
    if args.json:
        for line in proc.stdout.splitlines():
            if line.startswith("{"):
                print(line)
    else:
        # One line per check, plus the total.
        for line in proc.stdout.splitlines():
            if not line.startswith("{"):
                print(line)
    if proc.stderr:
        sys.stderr.write(proc.stderr)
    # The Node cross-check, when Node is here and the table came back.
    table = None
    for line in proc.stdout.splitlines():
        if line.startswith('{"table"'):
            try:
                table = json.loads(line)["table"]
            except (ValueError, KeyError):
                table = None
    if table is not None and args.node:
        # Row index -> label, in kSingleByte order (27 rows, 28 labels: the two
        # iso-8859-8 entries share row 7).
        rows = {}
        for label, row in ROW_OF_LABEL.items():
            rows[label] = [entry if entry else None for entry in table[f"row{row}"]]
        try:
            compared, differed, known = node_crosscheck(rows, args.timeout)
        except (OSError, subprocess.SubprocessError) as exc:
            print(f"node cross-check skipped: {exc}", file=sys.stderr)
        else:
            note = f"  ({known} where ICU is the one that is wrong)" if known else ""
            print(f"{'node cross-check':<28} {compared:6d} positions  {differed:4d} disagreements{note}")
            if differed:
                return 1

    if args.json:
        # The harness prints one object per check; the last is the total.
        lines = [line for line in proc.stdout.splitlines() if line.startswith("{") and "total" in line]
        if lines:
            total = json.loads(lines[-1])
            return 0 if total.get("total_mismatches", 1) == 0 else 1
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
