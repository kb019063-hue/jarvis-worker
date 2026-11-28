#!/usr/bin/env python3
"""Thai/Lao OpenType SARA AM and legacy PUA shaping probes; same CLI as compare.py."""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import compare

DEFAULT_CASES = [
    # Basic SARA AM decomposition and explicit <0ECD, 0EB2> control
    ("lao-ko-sara-am", "\u0e81\u0eb3", "Laoo", "ltr"),
    ("lao-ko-niggahita-aa-equiv", "\u0e81\u0ecd\u0eb2", "Laoo", "ltr"),
    ("lao-nam-mai-tho-sara-am", "\u0e99\u0ec9\u0eb3", "Laoo", "ltr"),
    # All four Lao tone marks before SARA AM, plus trailing tone after SARA AM
    ("lao-ko-mai-ek-sara-am", "\u0e81\u0ec8\u0eb3", "Laoo", "ltr"),
    ("lao-ko-mai-tho-sara-am", "\u0e81\u0ec9\u0eb3", "Laoo", "ltr"),
    ("lao-ko-mai-ti-sara-am", "\u0e81\u0eca\u0eb3", "Laoo", "ltr"),
    ("lao-ko-mai-catawa-sara-am", "\u0e81\u0ecb\u0eb3", "Laoo", "ltr"),
    ("lao-sara-am-then-mai-tho", "\u0e81\u0eb3\u0ec9", "Laoo", "ltr"),
    # Ascending consonants (PO PA, FO TAM) and semivowel LO (0EBC is not above-base)
    ("lao-po-pa-mai-tho-sara-am", "\u0e9b\u0ec9\u0eb3", "Laoo", "ltr"),
    ("lao-fo-tam-mai-ek-sara-am", "\u0e9d\u0ec8\u0eb3", "Laoo", "ltr"),
    ("lao-ho-lo-semivowel-sara-am", "\u0eab\u0ebc\u0ec9\u0eb3", "Laoo", "ltr"),
    # Interaction between preprocess_text_thai (before normalization) and below-mark CCC sorting
    ("lao-below-u-mai-tho-sara-am", "\u0e81\u0eb8\u0ec9\u0eb3", "Laoo", "ltr"),
    ("lao-mai-tho-below-u-sara-am", "\u0e81\u0ec9\u0eb8\u0eb3", "Laoo", "ltr"),
    # Multiple above-base marks crossed by SARA AM's NIGGAHITA
    ("lao-above-i-mai-tho-sara-am", "\u0e81\u0eb4\u0ec9\u0eb3", "Laoo", "ltr"),
    ("lao-mai-kan-mai-ek-sara-am", "\u0e81\u0eb1\u0ec8\u0eb3", "Laoo", "ltr"),
    ("lao-mai-kon-mai-tho-sara-am", "\u0e81\u0ebb\u0ec9\u0eb3", "Laoo", "ltr"),
    ("lao-cancellation-sara-am", "\u0e81\u0ecc\u0eb3", "Laoo", "ltr"),
    # Controls: literal NIGGAHITA does not reorder; standalone SARA AM; multi-word phrase
    ("lao-literal-niggahita-control", "\u0e81\u0ec9\u0ecd\u0eb2", "Laoo", "ltr"),
    ("lao-standalone-sara-am", "\u0eb3", "Laoo", "ltr"),
    ("lao-two-words-sara-am", "\u0e99\u0ec9\u0eb3 \u0e84\u0eb3", "Laoo", "ltr"),
]

EXTENDED_CASES = [
    ("lao-ho-no-sara-am", "\u0edc\u0eb3", "Laoo", "ltr"),
    ("lao-ho-no-mai-tho-sara-am", "\u0edc\u0ec9\u0eb3", "Laoo", "ltr"),
    ("lao-ho-mo-mai-tho-sara-am", "\u0edd\u0ec9\u0eb3", "Laoo", "ltr"),
    ("lao-prebase-e-ko-mai-tho-sara-am", "\u0ec0\u0e81\u0ec9\u0eb3", "Laoo", "ltr"),
]

SYNTHETIC_THAI_PUA_CASES = [
    ("thai-ko-sara-am", "\u0e01\u0e33", "Thai", "ltr"),
    ("thai-ko-mai-tho-sara-am", "\u0e01\u0e49\u0e33", "Thai", "ltr"),
    ("thai-nc-mai-ek-sd", "\u0e01\u0e48", "Thai", "ltr"),
    ("thai-nc-av-mai-ek-nop", "\u0e01\u0e34\u0e48", "Thai", "ltr"),
    ("thai-ac-po-pla-mai-ek-sdl", "\u0e1b\u0e48", "Thai", "ltr"),
    ("thai-ac-fo-fa-mai-tho-sdl", "\u0e1d\u0e49", "Thai", "ltr"),
    ("thai-ac-fo-fan-mai-tri-sdl", "\u0e1f\u0e4a", "Thai", "ltr"),
    ("thai-ac-av-mai-ek-sl-sl", "\u0e1b\u0e34\u0e48", "Thai", "ltr"),
    ("thai-ac-mai-tho-sara-am", "\u0e1b\u0e49\u0e33", "Thai", "ltr"),
    ("thai-rc-yo-ying-sara-u-rd", "\u0e0d\u0e38", "Thai", "ltr"),
    ("thai-rc-tho-than-sara-uu-rd", "\u0e10\u0e39", "Thai", "ltr"),
    ("thai-dc-do-chada-sara-u-sd", "\u0e0e\u0e38", "Thai", "ltr"),
    ("thai-dc-to-patak-phinthu-sd", "\u0e0f\u0e3a", "Thai", "ltr"),
    ("thai-reorder-mai-ek-sara-u", "\u0e01\u0e48\u0e38", "Thai", "ltr"),
    ("thai-reorder-dc-phinthu-sara-u", "\u0e0e\u0e3a\u0e38", "Thai", "ltr"),
    ("thai-nonconsonant-sara-aa-u", "\u0e32\u0e38", "Thai", "ltr"),
]


def _box_glyf(x0: int, y0: int, x1: int, y1: int) -> bytes:
    header = struct.pack(">hhhhhH", 1, x0, y0, x1, y1, 3) + struct.pack(">H", 0)
    flags = bytes([0x01, 0x01, 0x01, 0x01])
    xs = struct.pack(">hhhh", x0, x1 - x0, 0, x0 - x1)
    ys = struct.pack(">hhhh", y0, 0, y1 - y0, y0 - y1)
    blob = header + flags + xs + ys
    if len(blob) % 4:
        blob += b"\x00" * (4 - len(blob) % 4)
    return blob


def build_synthetic_thai_pua_sfnt(mac_pua: bool = False) -> bytes:
    pua_range = range(0xF884, 0xF89F) if mac_pua else range(0xF700, 0xF71B)
    rd_pua = {0xF89A, 0xF89E} if mac_pua else {0xF700, 0xF70F}
    cps = sorted([
        0x0020, 0x0E01, 0x0E0D, 0x0E0E, 0x0E0F, 0x0E10, 0x0E1B, 0x0E1D, 0x0E1F,
        0x0E31, 0x0E32, 0x0E33, 0x0E34, 0x0E35, 0x0E36, 0x0E37,
        0x0E38, 0x0E39, 0x0E3A, 0x0E47, 0x0E48, 0x0E49, 0x0E4A, 0x0E4B, 0x0E4C,
        0x0E4D, 0x0E4E, *pua_range,
    ])
    ng = len(cps) + 1
    glyf_parts = [b""]
    loca_offs = [0, 0]
    advances = [500]
    for cp in cps:
        if cp == 0x0020:
            adv, blob = 250, b""
        elif cp == 0x0E32:
            adv, blob = 500, _box_glyf(40, 0, 460, 520)
        elif cp == 0x0E33:
            adv, blob = 900, _box_glyf(40, 0, 860, 680)
        elif (0x0E01 <= cp <= 0x0E2E) or cp in rd_pua:
            adv, blob = 600, _box_glyf(40, -120 if cp in (0x0E0D, 0x0E0E, 0x0E0F, 0x0E10) else 0, 560, 640)
        else:
            shift = -80 if cp >= 0xF700 else 0
            adv, blob = 400, _box_glyf(120 + shift, 560, 320 + shift, 700)
        advances.append(adv)
        glyf_parts.append(blob)
        loca_offs.append(loca_offs[-1] + len(blob))

    tables = {}
    head = bytearray(54)
    struct.pack_into(">IIIIHH", head, 0, 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0, 1000)
    struct.pack_into(">hhhhHHhhh", head, 36, 0, -200, 900, 820, 0, 8, 2, 1, 0)
    tables[b"head"] = bytes(head)
    hhea = bytearray(36)
    struct.pack_into(">IhhhH", hhea, 0, 0x00010000, 820, -200, 0, 900)
    struct.pack_into(">H", hhea, 34, ng)
    tables[b"hhea"] = bytes(hhea)
    maxp = bytearray(32)
    struct.pack_into(">IHHHH", maxp, 0, 0x00010000, ng, 4, 1, 0)
    tables[b"maxp"] = bytes(maxp)
    tables[b"hmtx"] = b"".join(struct.pack(">Hh", a, 0) for a in advances)
    tables[b"loca"] = b"".join(struct.pack(">I", o) for o in loca_offs)
    tables[b"glyf"] = b"".join(glyf_parts) or b"\x00\x00\x00\x00"
    post = bytearray(32)
    struct.pack_into(">I", post, 0, 0x00030000)
    tables[b"post"] = bytes(post)
    os2 = bytearray(78)
    struct.pack_into(">HhH", os2, 0, 2, 500, 400)
    tables[b"OS/2"] = bytes(os2)
    name_tbl = bytearray(18)
    struct.pack_into(">HHH", name_tbl, 0, 0, 0, 6)
    tables[b"name"] = bytes(name_tbl)
    groups = b"".join(struct.pack(">III", cp, cp, idx + 1) for idx, cp in enumerate(cps))
    sub12 = struct.pack(">HHIII", 12, 0, 16 + len(groups), 0, len(cps)) + groups
    tables[b"cmap"] = struct.pack(">HH", 0, 1) + struct.pack(">HHI", 3, 10, 12) + sub12

    num_tables = len(tables)
    sr = 16
    es = 0
    while sr * 2 <= num_tables * 16:
        sr *= 2
        es += 1
    out = bytearray(struct.pack(">IHHHH", 0x00010000, num_tables, sr, es, num_tables * 16 - sr))
    dir_pos = 12
    out.extend(b"\x00" * (16 * num_tables))
    for tag in sorted(tables):
        data = tables[tag]
        off = len(out)
        out.extend(data)
        while len(out) % 4:
            out.append(0)
        struct.pack_into(">4sIII", out, dir_pos, tag, 0, off, len(data))
        dir_pos += 16
    return bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--extended", action="store_true")
    parser.add_argument("--synthetic-thai-pua", action="store_true")
    parser.add_argument("--synthetic-thai-mac-pua", action="store_true")
    parser.add_argument("--write-synthetic-font")
    args, remaining = parser.parse_known_args()

    if args.synthetic_thai_pua or args.synthetic_thai_mac_pua:
        sfnt = build_synthetic_thai_pua_sfnt(mac_pua=args.synthetic_thai_mac_pua)
        tmp_path = None
        if args.write_synthetic_font:
            font_path = args.write_synthetic_font
            with open(font_path, "wb") as fp:
                fp.write(sfnt)
        else:
            fd, tmp_path = tempfile.mkstemp(suffix=".ttf")
            with os.fdopen(fd, "wb") as fp:
                fp.write(sfnt)
            font_path = tmp_path
        try:
            if "--font" not in remaining:
                remaining = ["--font", font_path, *remaining]
            sys.argv = [sys.argv[0], *remaining]
            compare.CASES = list(SYNTHETIC_THAI_PUA_CASES)
            return compare.main()
        finally:
            if tmp_path and os.path.exists(tmp_path):
                os.remove(tmp_path)

    sys.argv = [sys.argv[0], *remaining]
    compare.CASES = [*DEFAULT_CASES, *EXTENDED_CASES] if args.extended else list(DEFAULT_CASES)
    return compare.main()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError, ImportError) as exc:
        print(f"thai/lao diagnostic error: {exc}", file=sys.stderr)
        sys.exit(2)
