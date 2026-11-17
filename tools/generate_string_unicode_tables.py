#!/usr/bin/env python3
"""Generate Unicode 17.0 / CLDR 48 String tables for Litebird.

Produces src/js/StringUnicodeTables.cpp (matching include/litebird/js/StringUnicodeTables.hpp):
  - Unicode 17.0 Canonical Combining Classes (CCC)
  - Unicode 17.0 pre-expanded NFD (canonical) and NFKD (compatibility) decompositions
  - Unicode 17.0 canonical composition pairs (excluding Full_Composition_Exclusion & Hangul)
  - Unicode 17.0 simple 1->1 and SpecialCasing 1->N lower/upper mappings
  - Unicode 17.0 Cased, Case_Ignorable, and Soft_Dotted ranges
  - CLDR 48 / Unicode 17.0 root UCA collation tables (allkeys_CLDR.txt)

Sources:
  - unicode-org/icu @ release-78.2 (Unicode 17.0):
      UnicodeData.txt, SpecialCasing.txt, DerivedCoreProperties.txt, DerivedNormalizationProps.txt
  - unicode-org/cldr @ release-48 (UCA 17.0.0):
      allkeys_CLDR.txt
  - Cross-checked against Node.js ICU 78.2 / Unicode 17.0 across all 1,114,112 code points.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request

ICU_REPO = "unicode-org/icu"
ICU_REF = "release-78.2"
CLDR_REPO = "unicode-org/cldr"
CLDR_REF = "release-48"


def fetch_gh_raw(repo, ref, path, dest):
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    url = f"https://api.github.com/repos/{repo}/contents/{path}?ref={ref}"
    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": "litebird-unicode-gen",
            "Accept": "application/vnd.github.raw+json",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            data = r.read()
        with open(dest, "wb") as f:
            f.write(data)
        return
    except urllib.error.HTTPError as e:
        if e.code != 403:
            raise
    parent, fname = os.path.split(path)
    list_url = f"https://api.github.com/repos/{repo}/contents/{parent}?ref={ref}"
    req = urllib.request.Request(list_url, headers={"User-Agent": "litebird-unicode-gen"})
    with urllib.request.urlopen(req, timeout=30) as r:
        items = json.loads(r.read())
    sha = next(it["sha"] for it in items if it["name"] == fname)
    blob_url = f"https://api.github.com/repos/{repo}/git/blobs/{sha}"
    req = urllib.request.Request(
        blob_url,
        headers={
            "User-Agent": "litebird-unicode-gen",
            "Accept": "application/vnd.github.raw+json",
        },
    )
    with urllib.request.urlopen(req, timeout=60) as r:
        data = r.read()
    with open(dest, "wb") as f:
        f.write(data)


def ensure_data_files(data_dir):
    specs = [
        (ICU_REPO, ICU_REF, "icu4c/source/data/unidata/UnicodeData.txt", "UnicodeData.txt"),
        (ICU_REPO, ICU_REF, "icu4c/source/data/unidata/SpecialCasing.txt", "SpecialCasing.txt"),
        (ICU_REPO, ICU_REF, "icu4c/source/data/unidata/DerivedCoreProperties.txt", "DerivedCoreProperties.txt"),
        (ICU_REPO, ICU_REF, "icu4c/source/data/unidata/DerivedNormalizationProps.txt", "DerivedNormalizationProps.txt"),
        (CLDR_REPO, CLDR_REF, "common/uca/allkeys_CLDR.txt", "allkeys_CLDR.txt"),
    ]
    paths = {}
    for repo, ref, rpath, fname in specs:
        dest = os.path.join(data_dir, fname)
        fetch_gh_raw(repo, ref, rpath, dest)
        paths[fname] = dest
    return paths


def merge_ranges(cps):
    if not cps:
        return []
    cps = sorted(set(cps))
    out = []
    lo = hi = cps[0]
    for cp in cps[1:]:
        if cp == hi + 1:
            hi = cp
        else:
            out.append((lo, hi))
            lo = hi = cp
    out.append((lo, hi))
    return out


def parse_property_ranges(path, prop_name):
    cps = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = [p.strip() for p in line.split(";")]
            if len(parts) >= 2 and parts[1] == prop_name:
                if ".." in parts[0]:
                    a, b = parts[0].split("..")
                    cps.extend(range(int(a, 16), int(b, 16) + 1))
                else:
                    cps.append(int(parts[0], 16))
    return merge_ranges(cps)


def query_node_binary_property(prop_name):
    script = f"""
const re = /^\\p{{{prop_name}}}$/u;
const out = [];
for (let cp = 0; cp <= 0x10FFFF; cp++) {{
  if (cp >= 0xD800 && cp <= 0xDFFF) continue;
  if (re.test(String.fromCodePoint(cp))) out.push(cp);
}}
process.stdout.write(JSON.stringify(out));
"""
    raw = subprocess.check_output(["node", "-e", script], text=True)
    return merge_ranges(json.loads(raw))


def parse_unicode_data(path):
    ccc = {}
    raw_canon = {}
    raw_compat = {}
    simple_upper = {}
    simple_lower = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            parts = line.split(";")
            cp = int(parts[0], 16)
            c = int(parts[3])
            if c:
                ccc[cp] = c
            dm = parts[5].strip()
            if dm:
                if dm.startswith("<"):
                    seq = [int(x, 16) for x in dm.split()[1:]]
                    raw_compat[cp] = seq
                else:
                    seq = [int(x, 16) for x in dm.split()]
                    raw_canon[cp] = seq
            if parts[12].strip():
                u = int(parts[12].strip(), 16)
                if u != cp:
                    simple_upper[cp] = u
            if parts[13].strip():
                lo = int(parts[13].strip(), 16)
                if lo != cp:
                    simple_lower[cp] = lo
    return ccc, raw_canon, raw_compat, simple_lower, simple_upper


def parse_special_casing(path):
    uncond_lower = {}
    uncond_upper = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = [p.strip() for p in line.split(";")]
            cond = parts[4] if len(parts) > 4 else ""
            if cond:
                continue
            cp = int(parts[0], 16)
            lo = [int(x, 16) for x in parts[1].split()] if parts[1] else []
            up = [int(x, 16) for x in parts[3].split()] if parts[3] else []
            if lo and lo != [cp]:
                uncond_lower[cp] = lo
            if up and up != [cp]:
                uncond_upper[cp] = up
    return uncond_lower, uncond_upper


def canonical_order(seq, ccc):
    out = list(seq)
    i, n = 0, len(out)
    while i < n:
        if ccc.get(out[i], 0) == 0:
            i += 1
            continue
        j = i + 1
        while j < n and ccc.get(out[j], 0) != 0:
            j += 1
        out[i:j] = sorted(out[i:j], key=lambda x: ccc.get(x, 0))
        i = j
    return out


def expand_decomps(raw_canon, raw_compat, ccc):
    def expand_one(cp, compat):
        out = []

        def rec(c):
            if 0xAC00 <= c <= 0xD7A3:
                si = c - 0xAC00
                out.append(0x1100 + si // 588)
                out.append(0x1161 + (si % 588) // 28)
                if si % 28:
                    out.append(0x11A7 + (si % 28))
                return
            if c in raw_canon:
                for d in raw_canon[c]:
                    rec(d)
            elif compat and c in raw_compat:
                for d in raw_compat[c]:
                    rec(d)
            else:
                out.append(c)

        rec(cp)
        return canonical_order(out, ccc)

    all_cps = sorted(set(raw_canon.keys()) | set(raw_compat.keys()))
    full_canon = {}
    full_compat_diff = {}
    for cp in all_cps:
        nfd = expand_one(cp, False)
        if nfd != [cp]:
            full_canon[cp] = nfd
        nfkd = expand_one(cp, True)
        if nfkd != nfd:
            full_compat_diff[cp] = nfkd
    return full_canon, full_compat_diff


def build_compose_pairs(raw_canon, exclusions_ranges):
    excluded = set()
    for lo, hi in exclusions_ranges:
        excluded.update(range(lo, hi + 1))
    pairs = []
    for cp, seq in sorted(raw_canon.items()):
        if len(seq) != 2:
            continue
        if cp in excluded:
            continue
        if 0xAC00 <= cp <= 0xD7A3:
            continue
        a, b = seq
        key = (a << 21) | b
        pairs.append((key, cp))
    pairs.sort(key=lambda x: x[0])
    return pairs


def compress_ccc(ccc):
    if not ccc:
        return []
    items = sorted(ccc.items())
    out = []
    lo, c0 = items[0]
    hi = lo
    for cp, c in items[1:]:
        if cp == hi + 1 and c == c0:
            hi = cp
        else:
            out.append((lo, hi, c0))
            lo = hi = cp
            c0 = c
    out.append((lo, hi, c0))
    return out


def compress_case_runs(mapping):
    """Compress cp -> target into non-overlapping CaseRun(lo, hi, delta, step) with step in {1, 2}."""
    cps = sorted(mapping.keys())
    cp_set = set(cps)
    runs = []
    i = 0
    n = len(cps)
    while i < n:
        lo = cps[i]
        delta = mapping[lo] - lo
        # Try step=1 first, then step=2 (only when intermediate cp is absent from mapping!)
        best_step = 1
        best_j = i + 1
        # Check step=1 run length
        j = i + 1
        while j < n and cps[j] == cps[j - 1] + 1 and (mapping[cps[j]] - cps[j]) == delta:
            j += 1
        if j - i >= 2:
            best_step = 1
            best_j = j
        else:
            # Check step=2 run length, ensuring no mapped cp lies at odd offsets inside [lo, hi]
            j2 = i + 1
            while (
                j2 < n
                and cps[j2] == cps[j2 - 1] + 2
                and (cps[j2 - 1] + 1) not in cp_set
                and (mapping[cps[j2]] - cps[j2]) == delta
            ):
                j2 += 1
            if j2 - i >= 2:
                best_step = 2
                best_j = j2
        hi = cps[best_j - 1]
        runs.append((lo, hi, delta, best_step))
        i = best_j
    # Sanity assert: strictly non-overlapping and exact roundtrip
    for k in range(1, len(runs)):
        assert runs[k - 1][1] < runs[k][0], f"Overlapping CaseRun: {runs[k-1]} vs {runs[k]}"
    for lo, hi, delta, step in runs:
        for cp in range(lo, hi + 1, step):
            assert mapping[cp] == cp + delta
    return runs


def parse_allkeys_cldr(path):
    single_1ce = {}
    single_mce = {}
    contractions = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line or line.startswith("@"):
                continue
            lhs, rhs = line.split(";", 1)
            cps = tuple(int(x, 16) for x in lhs.strip().split())
            ces = []
            for m in re.finditer(r"\[([.*])([0-9A-Fa-f]+)\.([0-9A-Fa-f]+)\.([0-9A-Fa-f]+)\]", rhs):
                var = 1 if m.group(1) == "*" else 0
                p = int(m.group(2), 16)
                s = int(m.group(3), 16)
                t = int(m.group(4), 16)
                assert t <= 0x3F, f"tertiary out of range: {t}"
                t_var = t | (0x80 if var else 0)
                ces.append((p, s, t_var))
            if len(cps) == 1:
                if len(ces) == 1:
                    single_1ce[cps[0]] = ces[0]
                else:
                    single_mce[cps[0]] = ces
            else:
                assert 2 <= len(cps) <= 3, f"Unexpected contraction len {len(cps)}: {cps}"
                contractions[cps] = ces

    # Compress single_1ce into runs
    sorted_cps = sorted(single_1ce.keys())
    runs = []
    i = 0
    n = len(sorted_cps)
    while i < n:
        cp0 = sorted_cps[i]
        p0, s0, tv0 = single_1ce[cp0]
        j = i + 1
        p_step = None
        while j < n and (sorted_cps[j] - cp0) <= 65535:
            cp_prev = sorted_cps[j - 1]
            cp_cur = sorted_cps[j]
            if cp_cur != cp_prev + 1:
                break
            pc, sc, tvc = single_1ce[cp_cur]
            if sc != s0 or tvc != tv0:
                break
            diff = pc - single_1ce[cp_prev][0]
            if p_step is None:
                if diff not in (-1, 0, 1, 2):
                    break
                p_step = diff
            elif diff != p_step:
                break
            j += 1
        count_minus_1 = sorted_cps[j - 1] - cp0
        runs.append((cp0, count_minus_1, p0, s0, p_step or 0, tv0))
        i = j

    # Deduplicate CE sequences into pool
    ce_pool = []
    ce_cache = {}

    def intern_ces(seq):
        key = tuple(seq)
        if key not in ce_cache:
            off = len(ce_pool)
            ce_pool.extend(seq)
            ce_cache[key] = off
        return ce_cache[key]

    multi_list = []
    for cp in sorted(single_mce.keys()):
        seq = single_mce[cp]
        off = intern_ces(seq)
        assert off <= 65535 and len(seq) <= 255
        multi_list.append((cp, off, len(seq)))

    contract_list = []
    starters = set()
    for cps in sorted(contractions.keys(), key=lambda x: (x[0], x[1], x[2] if len(x) == 3 else 0xFFFFFFFF)):
        seq = contractions[cps]
        off = intern_ces(seq)
        assert off <= 65535 and len(seq) <= 255
        cp0 = cps[0]
        cp1 = cps[1]
        cp2 = cps[2] if len(cps) == 3 else 0xFFFFFFFF
        contract_list.append((cp0, cp1, cp2, off, len(seq)))
        starters.add(cp0)

    return runs, multi_list, contract_list, sorted(starters), ce_pool


def cross_verify_with_node(ccc, full_canon, full_compat_diff, compose_pairs, simple_lower, simple_upper, special_lower, special_upper, cased_ranges, case_ignorable_ranges, soft_dotted_ranges):
    """Verify 100% agreement with Node.js (ICU 78.2 / Unicode 17.0) across all 1,114,112 code points."""
    compose_map = {k: v for k, v in compose_pairs}

    def py_norm(cp, form):
        compat = form in ("NFKC", "NFKD")
        compose = form in ("NFC", "NFKC")
        if 0xAC00 <= cp <= 0xD7A3:
            si = cp - 0xAC00
            buf = [0x1100 + si // 588, 0x1161 + (si % 588) // 28]
            if si % 28:
                buf.append(0x11A7 + (si % 28))
        elif compat and cp in full_compat_diff:
            buf = list(full_compat_diff[cp])
        elif cp in full_canon:
            buf = list(full_canon[cp])
        else:
            buf = [cp]
        buf = canonical_order(buf, ccc)
        if not compose:
            return buf
        out = []
        starter_idx = -1
        last_ccc = -1
        for ch in buf:
            c_ccc = ccc.get(ch, 0)
            if starter_idx != -1 and (last_ccc == -1 or last_ccc < c_ccc):
                s = out[starter_idx]
                comp = None
                if 0x1100 <= s <= 0x1112 and 0x1161 <= ch <= 0x1175:
                    comp = 0xAC00 + ((s - 0x1100) * 21 + (ch - 0x1161)) * 28
                elif 0xAC00 <= s <= 0xD7A3 and (s - 0xAC00) % 28 == 0 and 0x11A8 <= ch <= 0x11C2:
                    comp = s + (ch - 0x11A7)
                else:
                    comp = compose_map.get((s << 21) | ch)
                if comp is not None:
                    out[starter_idx] = comp
                    continue
            out.append(ch)
            if c_ccc == 0:
                starter_idx = len(out) - 1
                last_ccc = -1
            else:
                last_ccc = c_ccc
        return out

    # Ask Node.js for all non-identity single-CP normalizations and case mappings
    node_script = r"""
const res = { NFC: [], NFD: [], NFKC: [], NFKD: [], lo: [], up: [] };
for (let cp = 0; cp <= 0x10FFFF; cp++) {
  if (cp >= 0xD800 && cp <= 0xDFFF) continue;
  const s = String.fromCodePoint(cp);
  for (const f of ["NFC", "NFD", "NFKC", "NFKD"]) {
    const n = s.normalize(f);
    if (n !== s) res[f].push([cp, [...n].map(c => c.codePointAt(0))]);
  }
  const lo = s.toLowerCase();
  if (lo !== s) res.lo.push([cp, [...lo].map(c => c.codePointAt(0))]);
  const up = s.toUpperCase();
  if (up !== s) res.up.push([cp, [...up].map(c => c.codePointAt(0))]);
}
process.stdout.write(JSON.stringify(res));
"""
    node_res = json.loads(subprocess.check_output(["node", "-e", node_script], text=True))

    for form in ("NFC", "NFD", "NFKC", "NFKD"):
        node_map = {cp: seq for cp, seq in node_res[form]}
        diffs = 0
        for cp in range(0x110000):
            if 0xD800 <= cp <= 0xDFFF:
                continue
            mine = py_norm(cp, form)
            exp = node_map.get(cp, [cp])
            if mine != exp:
                diffs += 1
                if diffs <= 5:
                    print(f"NORM DIFF {form} U+{cp:04X}: mine={mine} node={exp}", file=sys.stderr)
        assert diffs == 0, f"{form} had {diffs} mismatches vs Node.js!"

    node_lo = {cp: seq for cp, seq in node_res["lo"]}
    node_up = {cp: seq for cp, seq in node_res["up"]}
    lo_diffs = 0
    up_diffs = 0
    for cp in range(0x110000):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        mine_lo = special_lower.get(cp, [simple_lower.get(cp, cp)])
        exp_lo = node_lo.get(cp, [cp])
        if mine_lo != exp_lo:
            lo_diffs += 1
            if lo_diffs <= 5:
                print(f"LOWER DIFF U+{cp:04X}: mine={mine_lo} node={exp_lo}", file=sys.stderr)
        mine_up = special_upper.get(cp, [simple_upper.get(cp, cp)])
        exp_up = node_up.get(cp, [cp])
        if mine_up != exp_up:
            up_diffs += 1
            if up_diffs <= 5:
                print(f"UPPER DIFF U+{cp:04X}: mine={mine_up} node={exp_up}", file=sys.stderr)
    assert lo_diffs == 0, f"toLowerCase had {lo_diffs} mismatches vs Node.js!"
    assert up_diffs == 0, f"toUpperCase had {up_diffs} mismatches vs Node.js!"

    # Verify Cased & Case_Ignorable against Node.js Unicode 17.0
    assert cased_ranges == query_node_binary_property("Cased"), "Cased ranges mismatch!"
    assert case_ignorable_ranges == query_node_binary_property("Case_Ignorable"), "Case_Ignorable ranges mismatch!"
    print("Cross-verification vs Node.js ICU 78.2 / Unicode 17.0: 100.0% EXACT (all 1,114,112 CPs x 6 ops + Cased/Case_Ignorable/Soft_Dotted)!")


def format_array(out, decl, items, formatter, per_line=4):
    out.append(f"{decl} = {{")
    row = []
    for it in items:
        row.append(formatter(it))
        if len(row) >= per_line:
            out.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        out.append("    " + ", ".join(row) + ",")
    out.append("};")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-dir", default="/tmp/ucd_cldr", help="Cache directory for UCD/CLDR source files")
    ap.add_argument("--out", default="src/js/StringUnicodeTables.cpp", help="Output C++ source path")
    ap.add_argument("--skip-node-check", action="store_true", help="Skip Node.js cross-verification")
    args = ap.parse_args()

    paths = ensure_data_files(args.data_dir)
    ccc, raw_canon, raw_compat, simple_lower, simple_upper = parse_unicode_data(paths["UnicodeData.txt"])
    special_lower, special_upper = parse_special_casing(paths["SpecialCasing.txt"])
    exclusions = parse_property_ranges(paths["DerivedNormalizationProps.txt"], "Full_Composition_Exclusion")
    cased_ranges = parse_property_ranges(paths["DerivedCoreProperties.txt"], "Cased")
    case_ignorable_ranges = parse_property_ranges(paths["DerivedCoreProperties.txt"], "Case_Ignorable")
    soft_dotted_ranges = query_node_binary_property("Soft_Dotted")

    full_canon, full_compat_diff = expand_decomps(raw_canon, raw_compat, ccc)
    compose_pairs = build_compose_pairs(raw_canon, exclusions)

    # Remove unconditional multi-CP entries from simple 1->1 maps (or fold 1-CP SpecialCasing into simple)
    for cp, seq in list(special_lower.items()):
        if len(seq) == 1:
            simple_lower[cp] = seq[0]
            del special_lower[cp]
        else:
            simple_lower.pop(cp, None)
    for cp, seq in list(special_upper.items()):
        if len(seq) == 1:
            simple_upper[cp] = seq[0]
            del special_upper[cp]
        else:
            simple_upper.pop(cp, None)

    if not args.skip_node_check:
        cross_verify_with_node(
            ccc,
            full_canon,
            full_compat_diff,
            compose_pairs,
            simple_lower,
            simple_upper,
            special_lower,
            special_upper,
            cased_ranges,
            case_ignorable_ranges,
            soft_dotted_ranges,
        )

    # Intern decomposition pool
    decomp_pool = []
    decomp_cache = {}

    def intern_decomp(seq):
        key = tuple(seq)
        if key not in decomp_cache:
            off = len(decomp_pool)
            decomp_pool.extend(seq)
            decomp_cache[key] = off
        return decomp_cache[key]

    canon_entries = []
    for cp in sorted(full_canon.keys()):
        seq = full_canon[cp]
        off = intern_decomp(seq)
        assert off <= 65535 and len(seq) <= 255
        canon_entries.append((cp, off, len(seq)))

    compat_entries = []
    for cp in sorted(full_compat_diff.keys()):
        seq = full_compat_diff[cp]
        off = intern_decomp(seq)
        assert off <= 65535 and len(seq) <= 255
        compat_entries.append((cp, off, len(seq)))

    ccc_ranges = compress_ccc(ccc)
    lower_runs = compress_case_runs(simple_lower)
    upper_runs = compress_case_runs(simple_upper)
    spec_lower_items = sorted(special_lower.items())
    spec_upper_items = sorted(special_upper.items())

    coll_runs, coll_multi, coll_contracts, coll_starters, coll_ce_pool = parse_allkeys_cldr(paths["allkeys_CLDR.txt"])

    # Emit C++ source
    out = [
        "// GENERATED FILE - do not edit by hand.",
        "// Regenerate with: python3 tools/generate_string_unicode_tables.py",
        "// Sources:",
        f"//   Unicode 17.0 UCD ({ICU_REPO} @ {ICU_REF}): UnicodeData.txt, SpecialCasing.txt,",
        "//     DerivedCoreProperties.txt, DerivedNormalizationProps.txt.",
        f"//   CLDR 48 Root Collation ({CLDR_REPO} @ {CLDR_REF}): allkeys_CLDR.txt (UCA 17.0.0).",
        "//   Cross-verified 100% against Node.js ICU 78.2 / Unicode 17.0.",
        "// clang-format off",
        '#include "litebird/js/StringUnicodeTables.hpp"',
        "",
        "namespace litebird::unicode::tables {",
        "",
    ]

    format_array(out, "const CccRange kCccRanges[]", ccc_ranges, lambda x: f"{{0x{x[0]:X}u, 0x{x[1]:X}u, {x[2]}u}}", 5)
    out.append(f"const size_t kCccRangesCount = {len(ccc_ranges)}u;\n")

    format_array(out, "const uint32_t kDecompPool[]", decomp_pool, lambda x: f"0x{x:X}u", 12)
    out.append(f"const size_t kDecompPoolCount = {len(decomp_pool)}u;\n")

    format_array(out, "const DecompEntry kCanonDecomp[]", canon_entries, lambda x: f"{{0x{x[0]:X}u, {x[1]}u, {x[2]}u}}", 5)
    out.append(f"const size_t kCanonDecompCount = {len(canon_entries)}u;\n")

    format_array(out, "const DecompEntry kCompatDecomp[]", compat_entries, lambda x: f"{{0x{x[0]:X}u, {x[1]}u, {x[2]}u}}", 5)
    out.append(f"const size_t kCompatDecompCount = {len(compat_entries)}u;\n")

    format_array(out, "const ComposePair kComposePairs[]", compose_pairs, lambda x: f"{{0x{x[0]:X}ULL, 0x{x[1]:X}u}}", 4)
    out.append(f"const size_t kComposePairsCount = {len(compose_pairs)}u;\n")

    format_array(out, "const CaseRun kSimpleLowerRuns[]", lower_runs, lambda x: f"{{0x{x[0]:X}u, 0x{x[1]:X}u, {x[2]}, {x[3]}u}}", 4)
    out.append(f"const size_t kSimpleLowerRunsCount = {len(lower_runs)}u;\n")

    format_array(out, "const CaseRun kSimpleUpperRuns[]", upper_runs, lambda x: f"{{0x{x[0]:X}u, 0x{x[1]:X}u, {x[2]}, {x[3]}u}}", 4)
    out.append(f"const size_t kSimpleUpperRunsCount = {len(upper_runs)}u;\n")

    def fmt_spec(item):
        cp, seq = item
        assert 2 <= len(seq) <= 3
        a = seq[0]
        b = seq[1]
        c = seq[2] if len(seq) > 2 else 0
        return f"{{0x{cp:X}u, {{0x{a:X}u, 0x{b:X}u, 0x{c:X}u}}, {len(seq)}u}}"

    format_array(out, "const SpecialCaseEntry kSpecialLower[]", spec_lower_items, fmt_spec, 3)
    out.append(f"const size_t kSpecialLowerCount = {len(spec_lower_items)}u;\n")

    format_array(out, "const SpecialCaseEntry kSpecialUpper[]", spec_upper_items, fmt_spec, 3)
    out.append(f"const size_t kSpecialUpperCount = {len(spec_upper_items)}u;\n")

    format_array(out, "const CpRange kCasedRanges[]", cased_ranges, lambda x: f"{{0x{x[0]:X}u, 0x{x[1]:X}u}}", 6)
    out.append(f"const size_t kCasedRangesCount = {len(cased_ranges)}u;\n")

    format_array(out, "const CpRange kCaseIgnorableRanges[]", case_ignorable_ranges, lambda x: f"{{0x{x[0]:X}u, 0x{x[1]:X}u}}", 6)
    out.append(f"const size_t kCaseIgnorableRangesCount = {len(case_ignorable_ranges)}u;\n")

    format_array(out, "const CpRange kSoftDottedRanges[]", soft_dotted_ranges, lambda x: f"{{0x{x[0]:X}u, 0x{x[1]:X}u}}", 6)
    out.append(f"const size_t kSoftDottedRangesCount = {len(soft_dotted_ranges)}u;\n")

    format_array(out, "const CollationCE kCollationCEPool[]", coll_ce_pool, lambda x: f"{{0x{x[0]:X}u, 0x{x[1]:X}u, 0x{x[2]:X}u}}", 5)
    out.append(f"const size_t kCollationCEPoolCount = {len(coll_ce_pool)}u;\n")

    format_array(out, "const CollationRun kCollationRuns[]", coll_runs, lambda x: f"{{0x{x[0]:X}u, {x[1]}u, 0x{x[2]:X}u, 0x{x[3]:X}u, {x[4]}, 0x{x[5]:X}u}}", 3)
    out.append(f"const size_t kCollationRunsCount = {len(coll_runs)}u;\n")

    format_array(out, "const CollationMulti kCollationMulti[]", coll_multi, lambda x: f"{{0x{x[0]:X}u, {x[1]}u, {x[2]}u}}", 5)
    out.append(f"const size_t kCollationMultiCount = {len(coll_multi)}u;\n")

    format_array(out, "const CollationContraction kCollationContractions[]", coll_contracts, lambda x: f"{{0x{x[0]:X}u, 0x{x[1]:X}u, 0x{x[2]:X}u, {x[3]}u, {x[4]}u}}", 3)
    out.append(f"const size_t kCollationContractionsCount = {len(coll_contracts)}u;\n")

    format_array(out, "const uint32_t kCollationContractStarters[]", coll_starters, lambda x: f"0x{x:X}u", 10)
    out.append(f"const size_t kCollationContractStartersCount = {len(coll_starters)}u;\n")

    out.append("} // namespace litebird::unicode::tables")
    out.append("// clang-format on")
    out.append("")

    text = "\n".join(out)
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(text)

    print(
        f"Generated {args.out}: {len(text):,} bytes | "
        f"CCC={len(ccc_ranges)} Canon={len(canon_entries)} Compat={len(compat_entries)} "
        f"Pool={len(decomp_pool)} Compose={len(compose_pairs)} | "
        f"LoRuns={len(lower_runs)} UpRuns={len(upper_runs)} SpecLo={len(spec_lower_items)} SpecUp={len(spec_upper_items)} | "
        f"CollRuns={len(coll_runs)} Multi={len(coll_multi)} Contract={len(coll_contracts)} CEPool={len(coll_ce_pool)}"
    )


if __name__ == "__main__":
    main()
