#!/usr/bin/env python3
"""OpenType source-cluster (`HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES`) diagnostic."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import compare

DEJAVU_CASES = [
    ("empty", "", "Latn", "ltr"),
    ("latin-words", "Hello world", "Latn", "ltr"),
    ("latin-ligatures", "office affinity ffi fl ffl", "Latn", "ltr"),
    ("latin-lig-trailing-mark", "fl\u0330 ffl\u0330", "Latn", "ltr"),
    ("latin-lig-intervening-mark", "f\u0301i f\u0330l", "Latn", "ltr"),
    ("latin-lig-zwnj-blocks", "f\u200ci fl\u200c", "Latn", "ltr"),
    ("latin-lig-zwj-merges", "f\u200di", "Latn", "ltr"),
    ("latin-lig-cgj-unhidden", "f\u034fi", "Latn", "ltr"),
    ("latin-combining-marks", "a\u0301 e\u0323\u0301 o\u0302\u0323", "Latn", "ltr"),
    ("latin-leading-marks", "\u0301\u0323a \u0301\u0323", "Latn", "ltr"),
    ("greek-polytonic-recompose", "Ελληνικά \u1f04\u0345 \u03b1\u0313\u0301\u0345", "Grek", "ltr"),
    ("cyrillic-marks", "Привет й\u0301 ё", "Cyrl", "ltr"),
    ("hebrew-rtl-marks", "שָׁלוֹם בְּרֵאשִׁית", "Hebr", "rtl"),
    ("hebrew-cgj-barrier", "\u05dc\u05b7\u034f\u05b4 \u05e9\u05c1\u05bc\u05b8", "Hebr", "rtl"),
    ("arabic-rtl-joining", "سلام مرحبا", "Arab", "rtl"),
    ("arabic-lam-alef-marks", "السَّلَامُ لَا لآ", "Arab", "rtl"),
    ("arabic-zwnj-separate", "ب\u200cب مي\u200cروم", "Arab", "rtl"),
    ("arabic-zwj-continuation", "ب\u200dب \u200dب\u200d", "Arab", "rtl"),
    ("arabic-mcm-reorder", "\u0628\u0651\u064e\u0654 \u064a\u064e\u0654", "Arab", "rtl"),
    ("lao-sara-am-clusters", "\u0e99\u0eb3 \u0e99\u0ec9\u0eb3 \u0ec9\u0eb3 \u0e99\u0eb3\u0e99\u0ec9\u0eb3", "Laoo", "ltr"),
]

SYNTHETIC_CASES = [
    ("gsub-multiple-expand", "aXb", "Latn", "ltr"),
    ("gsub-multiple-delete-mid", "aDb", "Latn", "ltr"),
    ("gsub-multiple-delete-start", "DaX", "Latn", "ltr"),
    ("gsub-multiple-delete-shared", "aD\u0301b", "Latn", "ltr"),
    ("gsub-alternate-keep-cluster", "aAb", "Latn", "ltr"),
    ("gsub-ligature-across-mark-and-trailing", "f\u0300i\u0323x", "Latn", "ltr"),
    ("grapheme-enclosing-mark", "a\u20ddb", "Latn", "ltr"),
    ("emoji-skin-tone-modifier", "\U0001f44b\U0001f3fd\U0001f44b", "Latn", "ltr"),
    ("emoji-regional-indicator-pairs", "\U0001f1fa\U0001f1f8\U0001f1ec\U0001f1e7\U0001f1fa", "Latn", "ltr"),
    ("emoji-zwj-pictographic-family", "\U0001f468\u200d\U0001f469\u200d\U0001f467\U0001f44b", "Latn", "ltr"),
    ("emoji-zwj-non-pictographic-splits", "\U0001f468\u200da", "Latn", "ltr"),
    ("katakana-halfwidth-sound-marks", "\uff76\uff9e\uff77\uff9f", "Kana", "ltr"),
    ("emoji-tag-sequence", "\U0001f3f4\U000e0067\U000e0062\U000e007f\U0001f44b", "Latn", "ltr"),
    ("hangul-tone-cross-cluster-swap", "\u0301\uac00\u302e \uac00\u0301\u0323", "Hang", "ltr"),
    ("hangul-tone-no-swap-distinct", "\u0323\uac00\u302e", "Hang", "ltr"),
    ("hangul-jamo-compose-trailing-mark", "\u1100\u1161\u0301 \u1100\u1161\u11a8\u0301 \uac00\u11a8\u0301", "Hang", "ltr"),
]


def _u16(v):
    return struct.pack(">H", v)


def _i16(v):
    return struct.pack(">h", v)


def _u32(v):
    return struct.pack(">I", v)


def _glyph_rect(x0, y0, x1, y1):
    blob = bytearray()
    blob += _i16(1) + _i16(x0) + _i16(y0) + _i16(x1) + _i16(y1)
    blob += _u16(3) + _u16(0) + bytes([1, 1, 1, 1])
    for d in (x0, 0, x1 - x0, 0):
        blob += _i16(d)
    for d in (y0, y1 - y0, 0, y0 - y1):
        blob += _i16(d)
    if len(blob) % 4:
        blob += b"\x00" * (4 - len(blob) % 4)
    return bytes(blob)


def _cov_fmt1(gids):
    return _u16(1) + _u16(len(gids)) + b"".join(_u16(g) for g in gids)


def build_synthetic_cluster_sfnt():
    cps = [
        0x0020, 0x0041, 0x0044, 0x0058, 0x0061, 0x0062, 0x0066, 0x0069, 0x0078,
        0x0300, 0x0301, 0x0323, 0x200C, 0x200D, 0x20DD, 0x25CC, 0x302E,
        0x1100, 0x1161, 0x11A8, 0xAC00, 0xAC01,
        0xFF76, 0xFF77, 0xFF9E, 0xFF9F,
        0x1F1E7, 0x1F1EC, 0x1F1F8, 0x1F1FA,
        0x1F3F4, 0x1F3FD, 0x1F44B, 0x1F467, 0x1F468, 0x1F469,
        0xE0062, 0xE0067, 0xE007F,
    ]
    cps_sorted = sorted(cps)
    cmap_map = {cp: idx + 1 for idx, cp in enumerate(cps_sorted)}
    next_gid = len(cps_sorted) + 1
    # Synthetic target glyphs: Multiple X -> (m1, m2, m3), Alternate A -> altA, Ligature f+i -> fi
    g_m1, g_m2, g_m3, g_alt_a, g_fi = range(next_gid, next_gid + 5)
    num_glyphs = next_gid + 5

    mark_cps = {0x0300, 0x0301, 0x0323, 0x20DD}
    mark_gids = {cmap_map[c] for c in mark_cps}

    # GDEF ClassDef Format 1: 1=base, 2=ligature, 3=mark
    cls_arr = [1] * (num_glyphs - 1)
    for mg in mark_gids:
        cls_arr[mg - 1] = 3
    cls_arr[g_fi - 1] = 2
    gdef_cd = _u16(1) + _u16(1) + _u16(len(cls_arr)) + b"".join(_u16(c) for c in cls_arr)
    gdef = _u32(0x00010000) + _u16(12) + _u16(0) + _u16(0) + _u16(0) + gdef_cd

    # Lookup 0 (`ccmp`, Type 2 MultipleSubstFormat1): D -> () [deletion], X -> (g_m1, g_m2, g_m3)
    mult_pairs = sorted([(cmap_map[0x0044], ()), (cmap_map[0x0058], (g_m1, g_m2, g_m3))])
    seq_hdr = 6 + 2 * len(mult_pairs)
    cov0 = _cov_fmt1([g for g, _ in mult_pairs])
    seq_offs = []
    seq_blob = bytearray()
    for _, seq in mult_pairs:
        seq_offs.append(seq_hdr + len(cov0) + len(seq_blob))
        seq_blob += _u16(len(seq)) + b"".join(_u16(x) for x in seq)
    sub_mult = _u16(1) + _u16(seq_hdr) + _u16(len(mult_pairs)) + b"".join(_u16(o) for o in seq_offs) + cov0 + bytes(seq_blob)

    # Lookup 1 (`calt`, Type 3 AlternateSubstFormat1): A -> [g_alt_a]
    cov1 = _cov_fmt1([cmap_map[0x0041]])
    alt_set_off = 8 + len(cov1)
    sub_alt = _u16(1) + _u16(8) + _u16(1) + _u16(alt_set_off) + cov1 + _u16(1) + _u16(g_alt_a)

    # Lookup 2 (`liga`, Type 4 LigatureSubstFormat1, IgnoreMarks=0x0008): f + i -> g_fi
    cov2 = _cov_fmt1([cmap_map[0x0066]])
    lig_set_off = 8 + len(cov2)
    lig_tbl = _u16(g_fi) + _u16(2) + _u16(cmap_map[0x0069])
    lig_set = _u16(1) + _u16(4) + lig_tbl
    sub_liga = _u16(1) + _u16(8) + _u16(1) + _u16(lig_set_off) + cov2 + lig_set

    lookups = [(2, 0x0000, sub_mult), (3, 0x0000, sub_alt), (4, 0x0008, sub_liga)]
    ll_offs = []
    ll_body = bytearray()
    ll_base = 2 + 2 * len(lookups)
    for ltype, lflag, sub in lookups:
        ll_offs.append(ll_base + len(ll_body))
        ll_body += _u16(ltype) + _u16(lflag) + _u16(1) + _u16(8) + sub
    lookup_list = _u16(len(lookups)) + b"".join(_u16(o) for o in ll_offs) + bytes(ll_body)

    feats = [(b"calt", [1]), (b"ccmp", [0]), (b"liga", [2])]
    fl_recs = bytearray()
    fl_body = bytearray()
    fl_base = 2 + 6 * len(feats)
    for ftag, idxs in feats:
        fl_recs += ftag + _u16(fl_base + len(fl_body))
        fl_body += _u16(0) + _u16(len(idxs)) + b"".join(_u16(i) for i in idxs)
    feature_list = _u16(len(feats)) + bytes(fl_recs) + bytes(fl_body)

    langsys = _u16(0) + _u16(0xFFFF) + _u16(len(feats)) + _u16(0) + _u16(1) + _u16(2)
    script_tags = [b"DFLT", b"hang", b"kana", b"latn"]
    sl_recs = bytearray()
    sl_body = bytearray()
    sl_base = 2 + 6 * len(script_tags)
    for stag in script_tags:
        sl_recs += stag + _u16(sl_base + len(sl_body))
        sl_body += _u16(4) + _u16(0) + langsys
    script_list = _u16(len(script_tags)) + bytes(sl_recs) + bytes(sl_body)
    gsub = _u32(0x00010000) + _u16(10) + _u16(10 + len(script_list)) + _u16(10 + len(script_list) + len(feature_list)) + script_list + feature_list + lookup_list

    # Minimal GPOS (`mark` SinglePos no-op) so HarfBuzz uses OpenType GPOS rather
    # than its GPOS-less fallback mark heuristic on synthetic `Latn` marks.
    gpos_sub = _u16(1) + _u16(6) + _u16(0) + _cov_fmt1([])
    gpos_ll = _u16(1) + _u16(4) + _u16(1) + _u16(0) + _u16(1) + _u16(8) + gpos_sub
    gpos_fl = _u16(1) + b"mark" + _u16(8) + _u16(0) + _u16(1) + _u16(0)
    gpos_ls = _u16(0) + _u16(0xFFFF) + _u16(1) + _u16(0)
    gpos_sl_recs = bytearray()
    gpos_sl_body = bytearray()
    for stag in script_tags:
        gpos_sl_recs += stag + _u16(sl_base + len(gpos_sl_body))
        gpos_sl_body += _u16(4) + _u16(0) + gpos_ls
    gpos_sl = _u16(len(script_tags)) + bytes(gpos_sl_recs) + bytes(gpos_sl_body)
    gpos = _u32(0x00010000) + _u16(10) + _u16(10 + len(gpos_sl)) + _u16(10 + len(gpos_sl) + len(gpos_fl)) + gpos_sl + gpos_fl + gpos_ll

    glyf_parts = []
    loca_offs = [0]
    hmtx = bytearray()
    for gid in range(num_glyphs):
        adv = 0 if gid in mark_gids or gid in (cmap_map[0x200C], cmap_map[0x200D]) else 600
        blob = b"" if gid in (cmap_map[0x0020], cmap_map[0x200C], cmap_map[0x200D]) else _glyph_rect(40, 0, 520, 700)
        glyf_parts.append(blob)
        loca_offs.append(loca_offs[-1] + len(blob))
        hmtx += _u16(adv) + _i16(40 if blob else 0)
    glyf = b"".join(glyf_parts)
    loca = b"".join(_u32(o) for o in loca_offs)

    groups = b"".join(struct.pack(">III", cp, cp, cmap_map[cp]) for cp in cps_sorted)
    sub12 = struct.pack(">HHIII", 12, 0, 16 + len(groups), 0, len(cps_sorted)) + groups
    cmap = struct.pack(">HH", 0, 1) + struct.pack(">HH", 3, 10) + _u32(12) + sub12

    head = struct.pack(">IIIIHHQQhhhhHHhhh", 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0, 1000,
                       0, 0, 0, 0, 1000, 700, 0, 0, 0, 1, 0)
    hhea = struct.pack(">IhhhHhhhhhhhhhhhH",
                       0x00010000, 800, -200, 0, 600, 0, 0, 600, 1, 0, 0, 0, 0, 0, 0, 0, num_glyphs)
    maxp = struct.pack(">IH", 0x00005000, num_glyphs) + b"\x00" * 26
    post = struct.pack(">IIhhIIIII", 0x00030000, 0, -100, 50, 0, 0, 0, 0, 0)
    name = struct.pack(">HHH", 0, 0, 6)

    tables = sorted([(b"GDEF", gdef), (b"GPOS", gpos), (b"GSUB", gsub), (b"cmap", cmap),
                     (b"glyf", glyf), (b"head", head), (b"hhea", hhea), (b"hmtx", bytes(hmtx)),
                     (b"loca", loca), (b"maxp", maxp), (b"name", name), (b"post", post)])
    nt = len(tables)
    sr = 1 << (nt.bit_length() - 1)
    out = bytearray(struct.pack(">IHHHH", 0x00010000, nt, sr * 16, nt.bit_length() - 1, nt * 16 - sr * 16))
    out += b"\x00" * (16 * nt)
    for idx, (tag, data) in enumerate(tables):
        off = len(out)
        out += data
        if len(out) % 4:
            out += b"\x00" * (4 - len(out) % 4)
        struct.pack_into(">4sIII", out, 12 + 16 * idx, tag, 0, off, len(data))
    return bytes(out)


def main():
    args = sys.argv[1:]
    if "--synthetic" in args:
        args.remove("--synthetic")
        compare.CASES = SYNTHETIC_CASES
        with tempfile.NamedTemporaryFile(suffix=".ttf") as tmp:
            Path(tmp.name).write_bytes(build_synthetic_cluster_sfnt())
            sys.argv = [sys.argv[0], "--font", tmp.name, *args]
            return compare.main()
    compare.CASES = DEJAVU_CASES
    return compare.main()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError, ImportError) as exc:
        print(f"cluster diagnostic error: {exc}", file=sys.stderr)
        sys.exit(2)
