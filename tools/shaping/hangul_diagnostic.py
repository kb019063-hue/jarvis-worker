#!/usr/bin/env python3
"""OpenType Hangul syllable, jamo, filler, and tone-mark shaping probes; same CLI as compare.py."""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import compare

DEFAULT_CASES = [
    # Covered precomposed negative control ("한글 ")
    ("hangul-precomposed-word", "\ud55c\uae00", "Hang", "ltr"),
    # Algorithmic <L,V> and <L,V,T> composition when precomposed syllable is covered
    ("hangul-compose-lv", "\u1100\u1161", "Hang", "ltr"),
    ("hangul-compose-lvt", "\u1112\u1161\u11ab", "Hang", "ltr"),
    ("hangul-compose-two-syllables", "\u1112\u1161\u11ab\u1100\u1173\u11af", "Hang", "ltr"),
    # Partial <LV,T>: compose when covered, else decompose to L+V+T with ljmo/vjmo/tjmo
    ("hangul-partial-lv-plus-t-compose", "\uac00\u11a8", "Hang", "ltr"),
    ("hangul-partial-lv-plus-missing-t-decomp", "\uac00\u11a9", "Hang", "ltr"),
    ("hangul-partial-lv-plus-old-t-decomp", "\uac00\ud7cb", "Hang", "ltr"),
    ("hangul-lvt-plus-extra-t-no-recompose", "\uac01\u11a8", "Hang", "ltr"),
    # Missing precomposed <LV> and <LVT> fallback decomposition + (!has_glyph && !tindex) + trailing T
    ("hangul-missing-lv-decompose", "\ub2e4", "Hang", "ltr"),
    ("hangul-missing-lvt-decompose", "\ub2e5", "Hang", "ltr"),
    ("hangul-missing-lv-plus-t-edge", "\ub2e4\u11a8", "Hang", "ltr"),
    ("hangul-uncomposable-missing-lvt-jamo", "\u1103\u1161\u11a8", "Hang", "ltr"),
    # Old Hangul (U+11xx and Jamo Extended-A/B) uncomposable syllables -> ljmo/vjmo/tjmo
    ("hangul-old-jamo-11xx-lvt", "\u1140\u119e\u11eb", "Hang", "ltr"),
    ("hangul-ext-ab-jamo-lvt", "\ua960\ud7b0\ud7cb", "Hang", "ltr"),
    # Hangul Choseong/Jungseong/Compatibility/Halfwidth fillers stay visible (not hidden)
    ("hangul-choseong-filler-v", "\u115f\u1161", "Hang", "ltr"),
    ("hangul-l-jungseong-filler", "\u1100\u1160", "Hang", "ltr"),
    # calt suppressed on conjoining jamo (even standalone), preserved on compat jamo & syllables
    ("hangul-standalone-jamo-no-ljmo-no-calt", "\u1100\u11a8", "Hang", "ltr"),
    ("hangul-compat-jamo-and-fillers", "\u3131\u3164\uffa0", "Hang", "ltr"),
    # Middle Korean spacing tone marks (U+302E, U+302F): reorder before syllable or add U+25CC
    ("hangul-tone-after-composed-syllable", "\ud55c\u302e", "Hang", "ltr"),
    ("hangul-tone-after-decomposed-old-syllable", "\u1140\u119e\u11eb\u302f", "Hang", "ltr"),
    ("hangul-tone-standalone-dotted-circle", "\u302e", "Hang", "ltr"),
    ("hangul-tone-double-consecutive", "\ud55c\u302e\u302f", "Hang", "ltr"),
    ("hangul-tone-after-compat-jamo", "\u3131\u302e", "Hang", "ltr"),
    # NORMALIZATION_MODE_NONE + ZERO_WIDTH_MARKS_NONE: CCC mark sorting without GDEF width zeroing
    ("hangul-marks-sort-no-zero-width", "\u0301\uac00\u302e \uac00\u0301\u0323", "Hang", "ltr"),
]

ZERO_WIDTH_TONE_CASES = [
    ("hangul-zw-precomposed-control", "\ud55c\uae00", "Hang", "ltr"),
    ("hangul-zw-tone-after-composed", "\ud55c\u302e", "Hang", "ltr"),
    ("hangul-zw-double-dot-after-composed", "\uac00\u302f", "Hang", "ltr"),
    ("hangul-zw-tone-after-composed-from-jamo", "\u1112\u1161\u11ab\u302e", "Hang", "ltr"),
    ("hangul-zw-tone-after-decomposed-old", "\u1140\u119e\u11eb\u302f", "Hang", "ltr"),
    ("hangul-zw-tone-after-partial-decomp", "\uac00\u11a9\u302e", "Hang", "ltr"),
    ("hangul-zw-tone-standalone-dotted-circle", "\u302e", "Hang", "ltr"),
    ("hangul-zw-double-dot-standalone-dotted-circle", "\u302f", "Hang", "ltr"),
    ("hangul-zw-tone-double-consecutive", "\ud55c\u302e\u302f", "Hang", "ltr"),
    ("hangul-zw-tone-after-standalone-l", "\u1100\u302e", "Hang", "ltr"),
    ("hangul-zw-tone-after-compat-jamo", "\u3131\u302f", "Hang", "ltr"),
    ("hangul-zw-two-syllables-with-tones", "\u1100\u1161\u302e \ud55c\u302f", "Hang", "ltr"),
]

DFLT_NO_DOTTED_CIRCLE_CASES = [
    ("hangul-dflt-compose-lv", "\u1100\u1161", "Hang", "ltr"),
    ("hangul-dflt-missing-lvt-decomp", "\ub2e5", "Hang", "ltr"),
    ("hangul-dflt-old-jamo-lvt", "\ua960\ud7b0\ud7cb", "Hang", "ltr"),
    ("hangul-dflt-standalone-jamo-blocks-calt", "\u1100", "Hang", "ltr"),
    ("hangul-dflt-compat-jamo-keeps-calt", "\u3131", "Hang", "ltr"),
    ("hangul-dflt-tone-after-syllable", "\ud55c\u302e", "Hang", "ltr"),
    ("hangul-dflt-tone-standalone-no-dotted-circle", "\u302e", "Hang", "ltr"),
    ("hangul-dflt-tone-double-no-dotted-circle", "\ud55c\u302e\u302f", "Hang", "ltr"),
]


def _u16(v: int) -> bytes:
    return struct.pack(">H", v & 0xFFFF)


def _i16(v: int) -> bytes:
    return struct.pack(">h", v)


def _u32(v: int) -> bytes:
    return struct.pack(">I", v & 0xFFFFFFFF)


def _box_glyf(x0: int, y0: int, x1: int, y1: int) -> bytes:
    header = struct.pack(">hhhhhH", 1, x0, y0, x1, y1, 3) + _u16(0)
    flags = bytes([0x01, 0x01, 0x01, 0x01])
    xs = struct.pack(">hhhh", x0, x1 - x0, 0, x0 - x1)
    ys = struct.pack(">hhhh", y0, 0, y1 - y0, y0 - y1)
    blob = header + flags + xs + ys
    if len(blob) % 4:
        blob += b"\x00" * (4 - len(blob) % 4)
    return blob


def _coverage_fmt1(gids: list[int]) -> bytes:
    return _u16(1) + _u16(len(gids)) + b"".join(_u16(g) for g in gids)


def _gsub_single_lookup(mapping: dict[int, int]) -> bytes:
    gids = sorted(mapping)
    subs = [mapping[g] for g in gids]
    cov_off = 6 + 2 * len(subs)
    sub = _u16(2) + _u16(cov_off) + _u16(len(subs)) + b"".join(_u16(s) for s in subs) + _coverage_fmt1(gids)
    return _u16(1) + _u16(0) + _u16(1) + _u16(8) + sub


def _gpos_single_lookup(adjustments: dict[int, tuple[int, int, int]]) -> bytes:
    # ValueFormat = XPlacement(0x1) | YPlacement(0x2) | XAdvance(0x4) = 0x0007
    gids = sorted(adjustments)
    vf = 0x0007
    cov_off = 8 + 6 * len(gids)
    vals = b"".join(_i16(dx) + _i16(dy) + _i16(dadv) for dx, dy, dadv in (adjustments[g] for g in gids))
    sub = _u16(2) + _u16(cov_off) + _u16(vf) + _u16(len(gids)) + vals + _coverage_fmt1(gids)
    return _u16(1) + _u16(0) + _u16(1) + _u16(8) + sub


def _gpos_mark_to_base_lookup(mark_anchors: dict[int, tuple[int, int]],
                              base_anchors: dict[int, tuple[int, int]]) -> bytes:
    mgids = sorted(mark_anchors)
    bgids = sorted(base_anchors)
    mark_cov = _coverage_fmt1(mgids)
    base_cov = _coverage_fmt1(bgids)

    # MarkArray: markCount + [class=0, anchorOff]*N + anchors
    ma_hdr_len = 2 + 4 * len(mgids)
    ma_recs = bytearray()
    ma_ancs = bytearray()
    for g in mgids:
        mx, my = mark_anchors[g]
        ma_recs += _u16(0) + _u16(ma_hdr_len + len(ma_ancs))
        ma_ancs += _u16(1) + _i16(mx) + _i16(my)
    mark_array = _u16(len(mgids)) + bytes(ma_recs) + bytes(ma_ancs)

    # BaseArray: baseCount + [anchorOff]*B + anchors
    ba_hdr_len = 2 + 2 * len(bgids)
    ba_recs = bytearray()
    ba_ancs = bytearray()
    for g in bgids:
        bx, by = base_anchors[g]
        ba_recs += _u16(ba_hdr_len + len(ba_ancs))
        ba_ancs += _u16(1) + _i16(bx) + _i16(by)
    base_array = _u16(len(bgids)) + bytes(ba_recs) + bytes(ba_ancs)

    mc_off = 12
    bc_off = mc_off + len(mark_cov)
    ma_off = bc_off + len(base_cov)
    ba_off = ma_off + len(mark_array)
    sub = _u16(1) + _u16(mc_off) + _u16(bc_off) + _u16(1) + _u16(ma_off) + _u16(ba_off)
    sub += mark_cov + base_cov + mark_array + base_array
    return _u16(4) + _u16(0) + _u16(1) + _u16(8) + sub


def _build_otl_table(script_tag: bytes, features: list[tuple[bytes, list[int]]], lookups: list[bytes]) -> bytes:
    ll_offs = []
    ll_body = bytearray()
    ll_base = 2 + 2 * len(lookups)
    for lk in lookups:
        ll_offs.append(ll_base + len(ll_body))
        ll_body += lk
    lookup_list = _u16(len(lookups)) + b"".join(_u16(o) for o in ll_offs) + bytes(ll_body)

    feats_sorted = sorted(features, key=lambda item: item[0])
    fl_recs = bytearray()
    fl_body = bytearray()
    fl_base = 2 + 6 * len(feats_sorted)
    for ftag, lidxs in feats_sorted:
        fl_recs += ftag + _u16(fl_base + len(fl_body))
        fl_body += _u16(0) + _u16(len(lidxs)) + b"".join(_u16(i) for i in lidxs)
    feature_list = _u16(len(feats_sorted)) + bytes(fl_recs) + bytes(fl_body)

    f_indices = list(range(len(feats_sorted)))
    langsys = _u16(0) + _u16(0xFFFF) + _u16(len(f_indices)) + b"".join(_u16(i) for i in f_indices)
    script_tbl = _u16(4) + _u16(0) + langsys
    script_list = _u16(1) + script_tag + _u16(8) + script_tbl

    sl_off = 10
    fl_off = sl_off + len(script_list)
    ll_off = fl_off + len(feature_list)
    return _u16(1) + _u16(0) + _u16(sl_off) + _u16(fl_off) + _u16(ll_off) + script_list + feature_list + lookup_list


def _build_gdef_classdef(classes: dict[int, int]) -> bytes:
    if not classes:
        return _u16(1) + _u16(0) + _u16(0) + _u16(0) + _u16(0) + _u16(0)
    lo = min(classes)
    hi = max(classes)
    arr = [classes.get(g, 0) for g in range(lo, hi + 1)]
    cd = _u16(1) + _u16(lo) + _u16(len(arr)) + b"".join(_u16(c) for c in arr)
    return _u16(1) + _u16(0) + _u16(12) + _u16(0) + _u16(0) + _u16(0) + cd


def build_synthetic_hangul_sfnt(zero_width_tones: bool = False,
                                include_dotted_circle: bool = True,
                                script_tag: bytes = b"hang") -> bytes:
    # Covered codepoints (deliberately omitting U+AC02 갂, U+B2E4 다, U+B2E5 닥
    # so both precomposition and missing-syllable decomposition are exercised).
    cps = [
        0x0020,
        0x0301, 0x0323,
        0x1100, 0x1102, 0x1103, 0x1112, 0x1140, 0x115F,
        0x1160, 0x1161, 0x1169, 0x1173, 0x119E,
        0x11A8, 0x11A9, 0x11AB, 0x11AF, 0x11EB,
        0x302E, 0x302F,
        0x3131, 0x3164, 0x3200,
        0xA960,
        0xAC00, 0xAC01, 0xAE00, 0xB098, 0xD558, 0xD55C,
        0xD7B0, 0xD7CB,
        0xFFA0,
    ]
    if include_dotted_circle:
        cps.append(0x25CC)
    cps = sorted(cps)
    cp_to_gid = {cp: idx + 1 for idx, cp in enumerate(cps)}

    # Extra unmapped GSUB target glyphs for ljmo, vjmo, tjmo, and calt
    next_gid = len(cps) + 1
    def alloc_gid() -> int:
        nonlocal next_gid
        g = next_gid
        next_gid += 1
        return g

    ljmo_map = {cp_to_gid[cp]: alloc_gid() for cp in (0x1100, 0x1102, 0x1103, 0x1112, 0x1140, 0x115F, 0xA960)}
    vjmo_map = {cp_to_gid[cp]: alloc_gid() for cp in (0x1160, 0x1161, 0x1169, 0x1173, 0x119E, 0xD7B0)}
    tjmo_map = {cp_to_gid[cp]: alloc_gid() for cp in (0x11A8, 0x11A9, 0x11AB, 0x11AF, 0x11EB, 0xD7CB)}
    calt_decoy_l = alloc_gid()
    calt_compat_3131 = alloc_gid()
    calt_ac00 = alloc_gid()
    calt_map = {
        cp_to_gid[0x1100]: calt_decoy_l,
        cp_to_gid[0x3131]: calt_compat_3131,
        cp_to_gid[0xAC00]: calt_ac00,
    }

    ng = next_gid
    glyf_parts = [b""]
    loca_offs = [0, 0]
    advances = [500]

    vjmo_targets = set(vjmo_map.values())
    tjmo_targets = set(tjmo_map.values())
    ljmo_targets = set(ljmo_map.values())

    for gid in range(1, ng):
        if gid == cp_to_gid[0x0020]:
            blob = b""
            adv = 300
        elif gid in (cp_to_gid[0x302E], cp_to_gid[0x302F]):
            y_shift = 40 if gid == cp_to_gid[0x302F] else 0
            if zero_width_tones:
                blob = _box_glyf(-220, 420 + y_shift, -90, 560 + y_shift)
                adv = 0
            else:
                blob = _box_glyf(30, 420 + y_shift, 190, 560 + y_shift)
                adv = 260
        elif gid in (cp_to_gid[0x0301], cp_to_gid[0x0323]):
            blob = _box_glyf(20, 620, 160, 740) if gid == cp_to_gid[0x0301] else _box_glyf(20, -140, 160, -20)
            adv = 220
        elif gid in ljmo_targets:
            blob = _box_glyf(40, 380, 340 + (gid % 7) * 18, 720)
            adv = 800
        elif gid in vjmo_targets:
            blob = _box_glyf(420, 260, 740, 720 - (gid % 5) * 20)
            adv = 800
        elif gid in tjmo_targets:
            blob = _box_glyf(120, 20, 680, 220 + (gid % 5) * 16)
            adv = 800
        else:
            w = 520 + (gid % 9) * 24
            h = 560 + (gid % 7) * 22
            blob = _box_glyf(50, 40, w, h)
            adv = 800
        glyf_parts.append(blob)
        loca_offs.append(loca_offs[-1] + len(blob))
        advances.append(adv)

    gsub_lookups = [
        _gsub_single_lookup(calt_map),
        _gsub_single_lookup(ljmo_map),
        _gsub_single_lookup(vjmo_map),
        _gsub_single_lookup(tjmo_map),
    ]
    gsub = _build_otl_table(
        script_tag,
        [(b"calt", [0]), (b"ljmo", [1]), (b"vjmo", [2]), (b"tjmo", [3])],
        gsub_lookups,
    )

    # GPOS:
    # - Lookup 0 (calt): decoy shift on raw 0x1100 (must NOT fire!) + real shift on compat 3131
    # - Lookup 1 (vjmo): compose vowel onto preceding L block (dx=-800, dy=0, dadv=-800)
    # - Lookup 2 (tjmo): compose trailing T under L+V block (dx=-800, dy=-40, dadv=-800)
    # - Lookup 3 (mark): MarkToBase for U+0301/U+0323 (and zero-width U+302E/U+302F when enabled)
    gpos_calt = _gpos_single_lookup({
        cp_to_gid[0x1100]: (150, 0, 150),
        calt_compat_3131: (30, 0, 40),
    })
    gpos_vjmo = _gpos_single_lookup({g: (-800, 0, -800) for g in vjmo_targets})
    gpos_tjmo = _gpos_single_lookup({g: (-800, -40, -800) for g in tjmo_targets})

    mark_anchors = {
        cp_to_gid[0x0301]: (90, 620),
        cp_to_gid[0x0323]: (90, -20),
    }
    if zero_width_tones:
        mark_anchors[cp_to_gid[0x302E]] = (-60, 480)
        mark_anchors[cp_to_gid[0x302F]] = (-60, 500)

    base_gids = [
        cp_to_gid[0xAC00], calt_ac00, cp_to_gid[0xAC01], cp_to_gid[0xAE00],
        cp_to_gid[0xB098], cp_to_gid[0xD558], cp_to_gid[0xD55C],
        *ljmo_targets,
    ]
    if include_dotted_circle:
        base_gids.append(cp_to_gid[0x25CC])
    base_anchors = {g: (360, 740) for g in base_gids}
    gpos_mark = _gpos_mark_to_base_lookup(mark_anchors, base_anchors)

    gpos = _build_otl_table(
        script_tag,
        [(b"calt", [0]), (b"vjmo", [1]), (b"tjmo", [2]), (b"mark", [3])],
        [gpos_calt, gpos_vjmo, gpos_tjmo, gpos_mark],
    )

    gdef_classes = {cp_to_gid[0x0301]: 3, cp_to_gid[0x0323]: 3}
    if zero_width_tones:
        gdef_classes[cp_to_gid[0x302E]] = 3
        gdef_classes[cp_to_gid[0x302F]] = 3
    gdef = _build_gdef_classdef(gdef_classes)

    tables: dict[bytes, bytes] = {}
    head = bytearray(54)
    struct.pack_into(">IIHH", head, 0, 0x00010000, 0x00010000, 0, 0)
    struct.pack_into(">IHH", head, 12, 0x5F0F3CF5, 0x000B, 1000)
    struct.pack_into(">hhhhHHh", head, 36, -300, -250, 950, 880, 0, 8, 1)
    tables[b"head"] = bytes(head)
    hhea = bytearray(36)
    struct.pack_into(">Ihhh", hhea, 0, 0x00010000, 880, -250, 0)
    struct.pack_into(">H", hhea, 4, 800)
    struct.pack_into(">H", hhea, 34, ng)
    tables[b"hhea"] = bytes(hhea)
    maxp = bytearray(32)
    struct.pack_into(">IH", maxp, 0, 0x00010000, ng)
    struct.pack_into(">HHHH", maxp, 6, 8, 2, 0, 0)
    tables[b"maxp"] = bytes(maxp)
    tables[b"hmtx"] = b"".join(struct.pack(">Hh", a, 20) for a in advances)
    tables[b"loca"] = b"".join(_u32(o) for o in loca_offs)
    tables[b"glyf"] = b"".join(glyf_parts) or b"\x00\x00\x00\x00"
    post = bytearray(32)
    struct.pack_into(">I", post, 0, 0x00030000)
    tables[b"post"] = bytes(post)
    os2 = bytearray(78)
    struct.pack_into(">HhH", os2, 0, 2, 600, 400)
    tables[b"OS/2"] = bytes(os2)
    name_tbl = bytearray(18)
    struct.pack_into(">HHH", name_tbl, 0, 0, 0, 6)
    tables[b"name"] = bytes(name_tbl)
    groups = b"".join(struct.pack(">III", cp, cp, cp_to_gid[cp]) for cp in cps)
    sub12 = struct.pack(">HHIII", 12, 0, 16 + len(groups), 0, len(cps)) + groups
    tables[b"cmap"] = struct.pack(">HH", 0, 1) + struct.pack(">HHI", 3, 10, 12) + sub12
    tables[b"GDEF"] = gdef
    tables[b"GSUB"] = gsub
    tables[b"GPOS"] = gpos

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
    parser.add_argument("--zero-width-tones", action="store_true")
    parser.add_argument("--dflt-no-dotted-circle", action="store_true")
    parser.add_argument("--write-synthetic-font")
    args, remaining = parser.parse_known_args()

    if args.zero_width_tones:
        cases = list(ZERO_WIDTH_TONE_CASES)
        sfnt = build_synthetic_hangul_sfnt(zero_width_tones=True, include_dotted_circle=True, script_tag=b"hang")
    elif args.dflt_no_dotted_circle:
        cases = list(DFLT_NO_DOTTED_CIRCLE_CASES)
        sfnt = build_synthetic_hangul_sfnt(zero_width_tones=False, include_dotted_circle=False, script_tag=b"DFLT")
    else:
        cases = list(DEFAULT_CASES)
        sfnt = build_synthetic_hangul_sfnt(zero_width_tones=False, include_dotted_circle=True, script_tag=b"hang")

    tmp_path = None
    if "--font" not in remaining:
        if args.write_synthetic_font:
            font_path = args.write_synthetic_font
            with open(font_path, "wb") as fp:
                fp.write(sfnt)
        else:
            fd, tmp_path = tempfile.mkstemp(suffix=".ttf")
            with os.fdopen(fd, "wb") as fp:
                fp.write(sfnt)
            font_path = tmp_path
        remaining = ["--font", font_path, *remaining]
    elif args.write_synthetic_font:
        with open(args.write_synthetic_font, "wb") as fp:
            fp.write(sfnt)

    try:
        sys.argv = [sys.argv[0], *remaining]
        compare.CASES = cases
        return compare.main()
    finally:
        if tmp_path and os.path.exists(tmp_path):
            os.remove(tmp_path)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError, ImportError) as exc:
        print(f"hangul diagnostic error: {exc}", file=sys.stderr)
        sys.exit(2)
