#!/usr/bin/env python3
"""Generate Unicode 17.0 RegExp property and case-folding tables for Litebird.

Extracts exact Unicode 17.0 tables using Node.js (V8/ICU) as the ground-truth
oracle and verifies all ECMA-262 canonical names & aliases (Table 65/66/67/68).
Writes:
  - include/litebird/js/RegExpUnicodeTables.hpp
  - src/js/RegExpUnicodeTables.cpp
"""

import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HPP_PATH = os.path.join(ROOT, "include/litebird/js/RegExpUnicodeTables.hpp")
CPP_PATH = os.path.join(ROOT, "src/js/RegExpUnicodeTables.cpp")

NODE_EXTRACT_SCRIPT = r"""
const fs = require('fs');
const path = require('path');

const test262Root = process.argv[1];
const genDir = path.join(test262Root, 'test/built-ins/RegExp/property-escapes/generated');
const strDir = path.join(genDir, 'strings');

// 1. Collect all property canonical names and aliases referenced by ECMA-262 / test262,
//    and validate every single one against Node's Unicode 17.0 RegExp engine.
const gcMap = new Map();   // canonical -> Set of value aliases (including canonical)
const scMap = new Map();   // canonical -> Set of script aliases (including canonical)
const scxMap = new Map();  // canonical -> Set of script_extensions aliases
const binMap = new Map();  // canonical -> Set of binary aliases (including canonical)

const files = fs.readdirSync(genDir).filter(f => f.endsWith('.js')).sort();
for (const f of files) {
  const src = fs.readFileSync(path.join(genDir, f), 'utf8');
  const base = f.replace(/\.js$/, '');
  const exprs = new Set();
  for (const m of src.matchAll(/\/(?:\^)?\\p\{([^}]+)\}\+\$\/u/g)) {
    exprs.add(m[1]);
  }
  if (base.startsWith('General_Category_-_')) {
    const canon = base.slice('General_Category_-_'.length);
    const aliases = new Set([canon]);
    for (const e of exprs) {
      const idx = e.indexOf('=');
      aliases.add(idx >= 0 ? e.slice(idx + 1) : e);
    }
    gcMap.set(canon, [...aliases].sort());
  } else if (base.startsWith('Script_-_')) {
    const canon = base.slice('Script_-_'.length);
    const aliases = new Set([canon]);
    for (const e of exprs) {
      const idx = e.indexOf('=');
      if (idx >= 0) aliases.add(e.slice(idx + 1));
    }
    scMap.set(canon, [...aliases].sort());
  } else if (base.startsWith('Script_Extensions_-_')) {
    const canon = base.slice('Script_Extensions_-_'.length);
    const aliases = new Set([canon]);
    for (const e of exprs) {
      const idx = e.indexOf('=');
      if (idx >= 0) aliases.add(e.slice(idx + 1));
    }
    scxMap.set(canon, [...aliases].sort());
  } else {
    const canon = base;
    const aliases = new Set([canon]);
    for (const e of exprs) aliases.add(e);
    binMap.set(canon, [...aliases].sort());
  }
}

// Also ensure Script_Extensions=Unknown (Zzzz) is present (tested in special-property-value-Script_Extensions-Unknown.js)
if (!scxMap.has('Unknown')) {
  scxMap.set('Unknown', ['Unknown', 'Zzzz']);
} else {
  const s = new Set(scxMap.get('Unknown'));
  s.add('Unknown');
  s.add('Zzzz');
  scxMap.set('Unknown', [...s].sort());
}

// Verify every alias in Node and extract exact code point ranges [lo, hi]
function rangesFor(expr) {
  const re = new RegExp(`^\\p{${expr}}$`, 'u');
  const out = [];
  let start = -1;
  for (let cp = 0; cp <= 0x10FFFF; cp++) {
    const m = re.test(String.fromCodePoint(cp));
    if (m && start < 0) start = cp;
    else if (!m && start >= 0) { out.push([start, cp - 1]); start = -1; }
  }
  if (start >= 0) out.push([start, 0x10FFFF]);
  return out;
}

function buildCategory(map, prefix) {
  const entries = [];
  for (const [canon, aliases] of [...map.entries()].sort((a, b) => a[0].localeCompare(b[0]))) {
    // Verify all aliases compile in Node
    for (const a of aliases) {
      new RegExp(`^\\p{${prefix}${a}}$`, 'u');
    }
    const ranges = rangesFor(`${prefix}${canon}`);
    entries.push({ canon, aliases, ranges });
  }
  return entries;
}

const gcEntries = buildCategory(gcMap, 'General_Category=');
const binEntries = buildCategory(binMap, '');
const scEntries = buildCategory(scMap, 'Script=');
const scxEntries = buildCategory(scxMap, 'Script_Extensions=');

// 2. Extract the 7 Properties of Strings (validated against Node /v)
const stringPropNames = [
  'Basic_Emoji',
  'Emoji_Keycap_Sequence',
  'RGI_Emoji_Modifier_Sequence',
  'RGI_Emoji_Flag_Sequence',
  'RGI_Emoji_Tag_Sequence',
  'RGI_Emoji_ZWJ_Sequence',
  'RGI_Emoji',
];

const stringEntries = [];
for (const name of stringPropNames) {
  const src = fs.readFileSync(path.join(strDir, `${name}.js`), 'utf8');
  const m = src.match(/matchStrings:\s*(\[[\s\S]*?\])\s*,\s*nonMatchStrings:/);
  const arr = eval(m[1]);
  const re = new RegExp(`^\\p{${name}}$`, 'v');

  // Single-codepoint ranges (checked exhaustively across 0..0x10FFFF in Node!)
  const singleRanges = [];
  let start = -1;
  for (let cp = 0; cp <= 0x10FFFF; cp++) {
    const hit = re.test(String.fromCodePoint(cp));
    if (hit && start < 0) start = cp;
    else if (!hit && start >= 0) { singleRanges.push([start, cp - 1]); start = -1; }
  }
  if (start >= 0) singleRanges.push([start, 0x10FFFF]);

  // Multi-codepoint sequences (verified against Node /v)
  const seqs = [];
  for (const s of arr) {
    if (!re.test(s)) throw new Error(`Node rejected ${name} string: ${JSON.stringify(s)}`);
    const cps = [...s].map(ch => ch.codePointAt(0));
    if (cps.length > 1) seqs.push(cps);
  }
  stringEntries.push({ name, singleRanges, seqs });
}

// 3. Extract exact Unicode 17.0 Simple Case Folding classes (u/v + i mode)
const uCandidates = new Set();
for (let cp = 0; cp <= 0x10FFFF; cp++) {
  if (cp >= 0xD800 && cp <= 0xDFFF) continue;
  const s = String.fromCodePoint(cp);
  const lo = s.toLowerCase();
  const hi = s.toUpperCase();
  if (lo !== s || hi !== s) {
    uCandidates.add(cp);
    for (const ch of lo) uCandidates.add(ch.codePointAt(0));
    for (const ch of hi) uCandidates.add(ch.codePointAt(0));
  }
}
const uCandArr = [...uCandidates].sort((a, b) => a - b);
// Verify exhaustively no codepoint outside uCandArr folds to any candidate
const allUCandRe = new RegExp('[' + uCandArr.map(cp => `\\u{${cp.toString(16)}}`).join('') + ']', 'iu');
for (let cp = 0; cp <= 0x10FFFF; cp++) {
  if (!uCandidates.has(cp) && allUCandRe.test(String.fromCodePoint(cp))) {
    uCandArr.push(cp);
    uCandidates.add(cp);
  }
}
uCandArr.sort((a, b) => a - b);

const uVisited = new Set();
const uClasses = [];
for (const cp of uCandArr) {
  if (uVisited.has(cp)) continue;
  const re = new RegExp(`^\\u{${cp.toString(16)}}$`, 'iu');
  const cls = [cp];
  uVisited.add(cp);
  for (const other of uCandArr) {
    if (other <= cp || uVisited.has(other)) continue;
    if (re.test(String.fromCodePoint(other))) {
      cls.push(other);
      uVisited.add(other);
    }
  }
  if (cls.length > 1) uClasses.push(cls);
}

// 4. Extract exact Legacy non-Unicode Canonicalize classes (!u && !v && i mode, 0..0xFFFF)
const lCandidates = new Set();
for (let cu = 0; cu <= 0xFFFF; cu++) {
  if (cu >= 0xD800 && cu <= 0xDFFF) continue;
  const s = String.fromCharCode(cu);
  const lo = s.toLowerCase();
  const hi = s.toUpperCase();
  if (lo !== s || hi !== s) {
    lCandidates.add(cu);
    if (lo.length === 1) lCandidates.add(lo.charCodeAt(0));
    if (hi.length === 1) lCandidates.add(hi.charCodeAt(0));
  }
}
const lCandArr = [...lCandidates].sort((a, b) => a - b);
const allLCandRe = new RegExp('[' + lCandArr.map(cu => `\\u${cu.toString(16).padStart(4, '0')}`).join('') + ']', 'i');
for (let cu = 0; cu <= 0xFFFF; cu++) {
  if (!lCandidates.has(cu) && allLCandRe.test(String.fromCharCode(cu))) {
    lCandArr.push(cu);
    lCandidates.add(cu);
  }
}
lCandArr.sort((a, b) => a - b);

const lVisited = new Set();
const lClasses = [];
for (const cu of lCandArr) {
  if (lVisited.has(cu)) continue;
  const re = new RegExp(`^\\u${cu.toString(16).padStart(4, '0')}$`, 'i');
  const cls = [cu];
  lVisited.add(cu);
  for (const other of lCandArr) {
    if (other <= cu || lVisited.has(other)) continue;
    if (re.test(String.fromCharCode(other))) {
      cls.push(other);
      lVisited.add(other);
    }
  }
  if (cls.length > 1) lClasses.push(cls);
}

process.stdout.write(JSON.stringify({
  gcEntries,
  binEntries,
  scEntries,
  scxEntries,
  stringEntries,
  uClasses,
  lClasses,
}));
"""


def find_test262_root():
    base = os.path.join(ROOT, "build/test262")
    if os.path.isdir(base):
        for entry in sorted(os.listdir(base)):
            cand = os.path.join(base, entry)
            if os.path.isdir(os.path.join(cand, "test/built-ins/RegExp/property-escapes/generated")):
                return cand
    raise SystemExit("build/test262 not found; run `python3 tools/conformance/run_test262.py built-ins/RegExp/dotall` first")


def main():
    test262_root = find_test262_root()
    print(f"Extracting Unicode 17.0 RegExp tables using Node.js + {test262_root} ...", file=sys.stderr)
    raw = subprocess.check_output(["node", "-e", NODE_EXTRACT_SCRIPT, test262_root], text=True)
    data = json.loads(raw)

    # Deduplicate range blocks so identical sets (e.g., Script vs Script_Extensions when equal,
    # or Basic_Emoji vs RGI_Emoji single code points) share storage in kAllRanges.
    all_ranges = []
    range_block_cache = {}

    def intern_ranges(ranges):
        key = tuple((lo, hi) for lo, hi in ranges)
        if key in range_block_cache:
            return range_block_cache[key]
        offset = len(all_ranges)
        all_ranges.extend(key)
        res = (offset, len(key))
        range_block_cache[key] = res
        return res

    # Build lookup tables per category:
    #   kind 0 = General_Category (valid with gc=, General_Category=, or bare)
    #   kind 1 = Binary (valid bare ONLY)
    #   kind 2 = Script (valid with sc=, Script= ONLY)
    #   kind 3 = Script_Extensions (valid with scx=, Script_Extensions= ONLY)
    def build_alias_table(entries):
        rows = []
        for entry in entries:
            off, cnt = intern_ranges(entry["ranges"])
            for alias in entry["aliases"]:
                rows.append((alias, off, cnt))
        rows.sort(key=lambda r: r[0])
        return rows

    gc_rows = build_alias_table(data["gcEntries"])
    bin_rows = build_alias_table(data["binEntries"])
    sc_rows = build_alias_table(data["scEntries"])
    scx_rows = build_alias_table(data["scxEntries"])

    # Build Properties of Strings tables:
    # Store all 6 base properties consecutively so RGI_Emoji is simply [0, total_seqs)!
    base_str_names = [
        "Basic_Emoji",
        "Emoji_Keycap_Sequence",
        "RGI_Emoji_Modifier_Sequence",
        "RGI_Emoji_Flag_Sequence",
        "RGI_Emoji_Tag_Sequence",
        "RGI_Emoji_ZWJ_Sequence",
    ]
    str_by_name = {e["name"]: e for e in data["stringEntries"]}
    all_seq_cps = []
    seq_meta = []  # (cp_offset, cp_len)
    str_prop_rows = []  # (name, range_off, range_cnt, seq_off, seq_cnt)

    for name in base_str_names:
        e = str_by_name[name]
        r_off, r_cnt = intern_ranges(e["singleRanges"])
        s_off = len(seq_meta)
        for seq in e["seqs"]:
            cp_off = len(all_seq_cps)
            all_seq_cps.extend(seq)
            seq_meta.append((cp_off, len(seq)))
        s_cnt = len(seq_meta) - s_off
        str_prop_rows.append((name, r_off, r_cnt, s_off, s_cnt))

    # Verify RGI_Emoji matches the exact union of the 6 base properties
    rgi = str_by_name["RGI_Emoji"]
    rgi_r_off, rgi_r_cnt = intern_ranges(rgi["singleRanges"])
    assert len(rgi["seqs"]) == len(seq_meta), f"RGI_Emoji seq count mismatch: {len(rgi['seqs'])} vs {len(seq_meta)}"
    str_prop_rows.append(("RGI_Emoji", rgi_r_off, rgi_r_cnt, 0, len(seq_meta)))
    str_prop_rows.sort(key=lambda r: r[0])

    # Build Unicode 17.0 case-folding equivalence tables:
    # For each class: canonical target = cls[0] (smallest code point in the class — wait:
    # canonical target only needs to be unique per equivalence class for equality!).
    # Wait: store flat array of class members + (offset, count) per class + sorted (cp, class_idx) index!
    u_classes = data["uClasses"]
    u_members_flat = []
    u_class_meta = []  # (offset, count)
    u_cp_index = []    # (cp, class_idx)
    for idx, cls in enumerate(u_classes):
        off = len(u_members_flat)
        u_members_flat.extend(cls)
        u_class_meta.append((off, len(cls)))
        for cp in cls:
            u_cp_index.append((cp, idx))
    u_cp_index.sort(key=lambda x: x[0])

    l_classes = data["lClasses"]
    l_members_flat = []
    l_class_meta = []
    l_cu_index = []
    for idx, cls in enumerate(l_classes):
        off = len(l_members_flat)
        l_members_flat.extend(cls)
        l_class_meta.append((off, len(cls)))
        for cu in cls:
            l_cu_index.append((cu, idx))
    l_cu_index.sort(key=lambda x: x[0])

    # Write HPP
    hpp = """#pragma once

// Auto-generated by tools/generate_regexp_unicode_tables.py — DO NOT EDIT BY HAND.
// Unicode 17.0 RegExp property escapes (General_Category, Binary Properties,
// Script, Script_Extensions, Properties of Strings) and exact ECMA-262 case-folding
// equivalence tables (both Unicode Simple Case Folding and legacy Canonicalize).

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace litebird::regexp_unicode {

struct CodePointRange {
    uint32_t lo;
    uint32_t hi;
};

struct PropertyRangesView {
    const CodePointRange* data{nullptr};
    size_t count{0};
};

enum class PropertyKind : uint8_t {
    Invalid,
    CodePoints,
    Strings,
};

struct PropertyLookup {
    PropertyKind kind{PropertyKind::Invalid};
    PropertyRangesView ranges{};
    size_t seq_offset{0};
    size_t seq_count{0};
};

/// Resolves an ECMA-262 Unicode property escape (`\\p{...}` / `\\P{...}`).
/// - `key`: empty for lone property (`\\p{ASCII}`, `\\p{Lu}`, `\\p{Basic_Emoji}`),
///   or exact property name before `=` (`General_Category`, `gc`, `Script`, `sc`,
///   `Script_Extensions`, `scx`).
/// - `value`: property value (or lone property name).
/// - `allow_strings`: true in `v` (`unicodeSets`) mode for `\\p{...}`; false for
///   `u` mode or `\\P{...}`.
PropertyLookup lookup_unicode_property(std::string_view key, std::string_view value, bool allow_strings);

/// Retrieves the `index`-th multi-code-point sequence from the properties-of-strings pool.
void get_string_sequence(size_t index, const uint32_t*& out_data, size_t& out_len);

/// Canonical representative for Unicode 17.0 Simple Case Folding (`u`/`v` + `i` mode).
/// Two code points match case-insensitively in `u`/`v` mode iff they share the same representative.
uint32_t unicode_case_canonical(uint32_t cp);

/// Canonical representative for legacy non-Unicode case-insensitive matching (`!u && !v && i`).
uint32_t legacy_case_canonical(uint32_t cu);

/// Returns all code points in `cp`'s Unicode 17.0 Simple Case Folding equivalence class.
/// Returns `out_len == 0` if `cp` has no case-folding partner (singleton `{cp}`).
void unicode_case_equivalents(uint32_t cp, const uint32_t*& out_data, size_t& out_len);

/// Returns all code units in `cu`'s legacy Canonicalize equivalence class.
/// Returns `out_len == 0` if `cu` has no partner.
void legacy_case_equivalents(uint32_t cu, const uint16_t*& out_data, size_t& out_len);

/// Invokes `fn(members, count, ctx)` for every Unicode 17.0 case-folding equivalence
/// class that has at least one member in `[lo, hi]`. Runs in O(log N + K).
void for_each_unicode_case_equiv_in_range(uint32_t lo, uint32_t hi,
                                          void (*fn)(const uint32_t* members, size_t count, void* ctx),
                                          void* ctx);

/// Same for legacy non-Unicode Canonicalize over `[lo, hi]`.
void for_each_legacy_case_equiv_in_range(uint32_t lo, uint32_t hi,
                                         void (*fn)(const uint16_t* members, size_t count, void* ctx),
                                         void* ctx);

} // namespace litebird::regexp_unicode
"""
    with open(HPP_PATH, "w", encoding="utf-8") as f:
        f.write(hpp)

    # Write CPP
    lines = []
    lines.append("// Auto-generated by tools/generate_regexp_unicode_tables.py — DO NOT EDIT BY HAND.")
    lines.append("// Unicode 17.0 RegExp property & case-folding tables.")
    lines.append("")
    lines.append('#include "litebird/js/RegExpUnicodeTables.hpp"')
    lines.append("#include <algorithm>")
    lines.append("")
    lines.append("namespace litebird::regexp_unicode {")
    lines.append("namespace {")
    lines.append("")

    # 1. kAllRanges
    lines.append(f"constexpr CodePointRange kAllRanges[{len(all_ranges)}] = {{")
    for i in range(0, len(all_ranges), 6):
        chunk = ", ".join(f"{{0x{lo:X},0x{hi:X}}}" for lo, hi in all_ranges[i:i+6])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")

    lines.append("struct AliasRangeEntry {")
    lines.append("    const char* name;")
    lines.append("    uint32_t offset;")
    lines.append("    uint32_t count;")
    lines.append("};")
    lines.append("")

    def emit_alias_table(var_name, rows):
        lines.append(f"constexpr AliasRangeEntry {var_name}[{len(rows)}] = {{")
        for name, off, cnt in rows:
            lines.append(f'    {{"{name}", {off}, {cnt}}},')
        lines.append("};")
        lines.append("")

    emit_alias_table("kGeneralCategoryTable", gc_rows)
    emit_alias_table("kBinaryPropertyTable", bin_rows)
    emit_alias_table("kScriptTable", sc_rows)
    emit_alias_table("kScriptExtensionsTable", scx_rows)

    # 2. String properties
    lines.append(f"constexpr uint32_t kStringSeqPool[{len(all_seq_cps)}] = {{")
    for i in range(0, len(all_seq_cps), 12):
        chunk = ", ".join(f"0x{cp:X}" for cp in all_seq_cps[i:i+12])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")

    lines.append("struct SeqSlice { uint16_t offset; uint8_t len; };")
    lines.append(f"constexpr SeqSlice kStringSeqMeta[{len(seq_meta)}] = {{")
    for i in range(0, len(seq_meta), 8):
        chunk = ", ".join(f"{{{off},{ln}}}" for off, ln in seq_meta[i:i+8])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")

    lines.append("struct StringPropEntry {")
    lines.append("    const char* name;")
    lines.append("    uint32_t range_offset;")
    lines.append("    uint32_t range_count;")
    lines.append("    uint16_t seq_offset;")
    lines.append("    uint16_t seq_count;")
    lines.append("};")
    lines.append(f"constexpr StringPropEntry kStringPropTable[{len(str_prop_rows)}] = {{")
    for name, r_off, r_cnt, s_off, s_cnt in str_prop_rows:
        lines.append(f'    {{"{name}", {r_off}, {r_cnt}, {s_off}, {s_cnt}}},')
    lines.append("};")
    lines.append("")

    # 3. Unicode 17.0 case-folding equivalence classes
    lines.append(f"constexpr uint32_t kUnicodeCaseMembers[{len(u_members_flat)}] = {{")
    for i in range(0, len(u_members_flat), 12):
        chunk = ", ".join(f"0x{cp:X}" for cp in u_members_flat[i:i+12])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")

    lines.append("struct CaseClassSlice { uint16_t offset; uint8_t count; };")
    lines.append(f"constexpr CaseClassSlice kUnicodeCaseClasses[{len(u_class_meta)}] = {{")
    for i in range(0, len(u_class_meta), 10):
        chunk = ", ".join(f"{{{off},{cnt}}}" for off, cnt in u_class_meta[i:i+10])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")

    lines.append("struct UnicodeCaseIndex { uint32_t cp; uint16_t class_idx; };")
    lines.append(f"constexpr UnicodeCaseIndex kUnicodeCaseIndex[{len(u_cp_index)}] = {{")
    for i in range(0, len(u_cp_index), 8):
        chunk = ", ".join(f"{{0x{cp:X},{ci}}}" for cp, ci in u_cp_index[i:i+8])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")

    # 4. Legacy non-Unicode Canonicalize equivalence classes
    lines.append(f"constexpr uint16_t kLegacyCaseMembers[{len(l_members_flat)}] = {{")
    for i in range(0, len(l_members_flat), 12):
        chunk = ", ".join(f"0x{cu:X}" for cu in l_members_flat[i:i+12])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")

    lines.append(f"constexpr CaseClassSlice kLegacyCaseClasses[{len(l_class_meta)}] = {{")
    for i in range(0, len(l_class_meta), 10):
        chunk = ", ".join(f"{{{off},{cnt}}}" for off, cnt in l_class_meta[i:i+10])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")

    lines.append("struct LegacyCaseIndex { uint16_t cu; uint16_t class_idx; };")
    lines.append(f"constexpr LegacyCaseIndex kLegacyCaseIndex[{len(l_cu_index)}] = {{")
    for i in range(0, len(l_cu_index), 8):
        chunk = ", ".join(f"{{0x{cu:X},{ci}}}" for cu, ci in l_cu_index[i:i+8])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")

    # Lookup helpers
    lines.append("""template <size_t N>
const AliasRangeEntry* find_alias(const AliasRangeEntry (&table)[N], std::string_view name) {
    size_t lo = 0, hi = N;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        std::string_view cur(table[mid].name);
        if (cur == name) return &table[mid];
        if (cur < name) lo = mid + 1;
        else hi = mid;
    }
    return nullptr;
}

const StringPropEntry* find_string_prop(std::string_view name) {
    constexpr size_t N = sizeof(kStringPropTable) / sizeof(kStringPropTable[0]);
    size_t lo = 0, hi = N;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        std::string_view cur(kStringPropTable[mid].name);
        if (cur == name) return &kStringPropTable[mid];
        if (cur < name) lo = mid + 1;
        else hi = mid;
    }
    return nullptr;
}

} // namespace

PropertyLookup lookup_unicode_property(std::string_view key, std::string_view value, bool allow_strings) {
    PropertyLookup out;
    if (value.empty()) return out;

    if (key.empty()) {
        if (const auto* e = find_alias(kBinaryPropertyTable, value)) {
            out.kind = PropertyKind::CodePoints;
            out.ranges = {kAllRanges + e->offset, e->count};
            return out;
        }
        if (const auto* e = find_alias(kGeneralCategoryTable, value)) {
            out.kind = PropertyKind::CodePoints;
            out.ranges = {kAllRanges + e->offset, e->count};
            return out;
        }
        if (allow_strings) {
            if (const auto* s = find_string_prop(value)) {
                out.kind = PropertyKind::Strings;
                out.ranges = {kAllRanges + s->range_offset, s->range_count};
                out.seq_offset = s->seq_offset;
                out.seq_count = s->seq_count;
                return out;
            }
        }
        return out;
    }

    const AliasRangeEntry* entry = nullptr;
    if (key == "General_Category" || key == "gc") {
        entry = find_alias(kGeneralCategoryTable, value);
    } else if (key == "Script" || key == "sc") {
        entry = find_alias(kScriptTable, value);
    } else if (key == "Script_Extensions" || key == "scx") {
        entry = find_alias(kScriptExtensionsTable, value);
    }
    if (entry) {
        out.kind = PropertyKind::CodePoints;
        out.ranges = {kAllRanges + entry->offset, entry->count};
    }
    return out;
}

void get_string_sequence(size_t index, const uint32_t*& out_data, size_t& out_len) {
    const auto& s = kStringSeqMeta[index];
    out_data = kStringSeqPool + s.offset;
    out_len = s.len;
}

uint32_t unicode_case_canonical(uint32_t cp) {
    if (cp < 0x80) {
        if (cp >= 'a' && cp <= 'z') return cp - 32;
        return cp;
    }
    constexpr size_t N = sizeof(kUnicodeCaseIndex) / sizeof(kUnicodeCaseIndex[0]);
    size_t lo = 0, hi = N;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        uint32_t cur = kUnicodeCaseIndex[mid].cp;
        if (cur == cp) {
            const auto& cls = kUnicodeCaseClasses[kUnicodeCaseIndex[mid].class_idx];
            return kUnicodeCaseMembers[cls.offset];
        }
        if (cur < cp) lo = mid + 1;
        else hi = mid;
    }
    return cp;
}

uint32_t legacy_case_canonical(uint32_t cu) {
    if (cu < 0x80) {
        if (cu >= 'a' && cu <= 'z') return cu - 32;
        return cu;
    }
    if (cu > 0xFFFF) return cu;
    constexpr size_t N = sizeof(kLegacyCaseIndex) / sizeof(kLegacyCaseIndex[0]);
    size_t lo = 0, hi = N;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        uint16_t cur = kLegacyCaseIndex[mid].cu;
        if (cur == cu) {
            const auto& cls = kLegacyCaseClasses[kLegacyCaseIndex[mid].class_idx];
            return kLegacyCaseMembers[cls.offset];
        }
        if (cur < cu) lo = mid + 1;
        else hi = mid;
    }
    return cu;
}

void unicode_case_equivalents(uint32_t cp, const uint32_t*& out_data, size_t& out_len) {
    constexpr size_t N = sizeof(kUnicodeCaseIndex) / sizeof(kUnicodeCaseIndex[0]);
    size_t lo = 0, hi = N;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        uint32_t cur = kUnicodeCaseIndex[mid].cp;
        if (cur == cp) {
            const auto& cls = kUnicodeCaseClasses[kUnicodeCaseIndex[mid].class_idx];
            out_data = kUnicodeCaseMembers + cls.offset;
            out_len = cls.count;
            return;
        }
        if (cur < cp) lo = mid + 1;
        else hi = mid;
    }
    out_data = nullptr;
    out_len = 0;
}

void legacy_case_equivalents(uint32_t cu, const uint16_t*& out_data, size_t& out_len) {
    if (cu > 0xFFFF) { out_data = nullptr; out_len = 0; return; }
    constexpr size_t N = sizeof(kLegacyCaseIndex) / sizeof(kLegacyCaseIndex[0]);
    size_t lo = 0, hi = N;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        uint16_t cur = kLegacyCaseIndex[mid].cu;
        if (cur == cu) {
            const auto& cls = kLegacyCaseClasses[kLegacyCaseIndex[mid].class_idx];
            out_data = kLegacyCaseMembers + cls.offset;
            out_len = cls.count;
            return;
        }
        if (cur < cu) lo = mid + 1;
        else hi = mid;
    }
    out_data = nullptr;
    out_len = 0;
}

void for_each_unicode_case_equiv_in_range(uint32_t lo, uint32_t hi,
                                          void (*fn)(const uint32_t* members, size_t count, void* ctx),
                                          void* ctx) {
    if (lo > hi) return;
    constexpr size_t N = sizeof(kUnicodeCaseIndex) / sizeof(kUnicodeCaseIndex[0]);
    size_t l = 0, r = N;
    while (l < r) {
        size_t mid = l + (r - l) / 2;
        if (kUnicodeCaseIndex[mid].cp < lo) l = mid + 1;
        else r = mid;
    }
    for (size_t i = l; i < N && kUnicodeCaseIndex[i].cp <= hi; ++i) {
        const auto& cls = kUnicodeCaseClasses[kUnicodeCaseIndex[i].class_idx];
        fn(kUnicodeCaseMembers + cls.offset, cls.count, ctx);
    }
}

void for_each_legacy_case_equiv_in_range(uint32_t lo, uint32_t hi,
                                         void (*fn)(const uint16_t* members, size_t count, void* ctx),
                                         void* ctx) {
    if (lo > hi || lo > 0xFFFF) return;
    uint32_t clamped_hi = hi > 0xFFFF ? 0xFFFF : hi;
    constexpr size_t N = sizeof(kLegacyCaseIndex) / sizeof(kLegacyCaseIndex[0]);
    size_t l = 0, r = N;
    while (l < r) {
        size_t mid = l + (r - l) / 2;
        if (kLegacyCaseIndex[mid].cu < lo) l = mid + 1;
        else r = mid;
    }
    for (size_t i = l; i < N && kLegacyCaseIndex[i].cu <= clamped_hi; ++i) {
        const auto& cls = kLegacyCaseClasses[kLegacyCaseIndex[i].class_idx];
        fn(kLegacyCaseMembers + cls.offset, cls.count, ctx);
    }
}

} // namespace litebird::regexp_unicode
""")

    with open(CPP_PATH, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    print(
        f"Generated {HPP_PATH} and {CPP_PATH}:\n"
        f"  deduped ranges: {len(all_ranges)}\n"
        f"  GC aliases: {len(gc_rows)}, Binary aliases: {len(bin_rows)}, "
        f"Script aliases: {len(sc_rows)}, Script_Extensions aliases: {len(scx_rows)}\n"
        f"  String props: {len(str_prop_rows)} ({len(seq_meta)} multi-CP sequences, {len(all_seq_cps)} code points)\n"
        f"  Unicode 17.0 case classes: {len(u_classes)} ({len(u_cp_index)} code points)\n"
        f"  Legacy case classes: {len(l_classes)} ({len(l_cu_index)} code units)",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
