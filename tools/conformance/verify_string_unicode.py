#!/usr/bin/env python3
"""Differential & spec verifier for Litebird's Unicode 17.0 / CLDR 48 String engine.

Verification layers:
  1. Exhaustive all-1,114,112 code-point sweep (incl. lone surrogates) vs Node.js
     across 11 operations (NFC, NFD, NFKC, NFKD, toLowerCase, toUpperCase,
     tr/lt toLocaleLowerCase, tr/lt/el toLocaleUpperCase) = 12,255,232 checks.
  2. Curated multi-character differential suite vs Node.js (UAX #15 reordering,
     Hangul Jamo, composition exclusions, Final_Sigma, Turkish/Azeri/Lithuanian/
     Greek contextual casing, CLDR 48 contractions & implicit weights, options:
     sensitivity, ignorePunctuation, numeric, caseFirst, -u- extensions).
  3. Deterministic seeded differential fuzzer (1,000 random multi-script +
     combining-mark + surrogate + Hangul + Greek/Turkish/Lithuanian strings)
     comparing normalization, case mapping, and pairwise localeCompare vs Node.js.
  4. Direct ECMA-262 / ECMA-402 validation checks (invalid normalization forms,
     BCP-47 CanonicalizeLocaleList validating every entry, invalid options,
     non-constructor checks, and `this` coercion edge cases).
  5. Optional (--ucd-dir): official Unicode 17.0 NormalizationTest.txt (400,680
     assertions) and CLDR 48 CollationTest_CLDR_NON_IGNORABLE_SHORT.txt (206,297
     adjacent pairs) executed end-to-end inside Litebird.
"""

from __future__ import annotations

import argparse
import json
import os
import random
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run_js(binary: list[str], script: str, timeout: int = 60) -> str:
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False, encoding="utf-8") as tf:
        tf.write(script)
        script_path = tf.name
    try:
        if binary[0] == "node":
            cmd = ["node", script_path]
        else:
            cmd = [*binary, "--run-script", script_path, os.devnull]
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        if proc.returncode != 0:
            raise RuntimeError(
                f"Command {' '.join(cmd)} failed (rc={proc.returncode}):\n"
                f"STDOUT:\n{proc.stdout[:1000]}\nSTDERR:\n{proc.stderr[:1000]}"
            )
        return proc.stdout
    finally:
        try:
            os.unlink(script_path)
        except OSError:
            pass


def js_escape(s: str) -> str:
    out = ['"']
    for ch in s:
        cp = ord(ch)
        if cp == 0x22:
            out.append('\\"')
        elif cp == 0x5C:
            out.append("\\\\")
        elif cp == 0x0A:
            out.append("\\n")
        elif cp == 0x0D:
            out.append("\\r")
        elif 0x20 <= cp <= 0x7E:
            out.append(ch)
        elif cp <= 0xFFFF:
            out.append(f"\\u{cp:04X}")
        else:
            u = cp - 0x10000
            out.append(f"\\u{0xD800 + (u >> 10):04X}\\u{0xDC00 + (u & 0x3FF):04X}")
    out.append('"')
    return "".join(out)


def verify_all_code_points(lb_bin: list[str], skip_node: bool = False) -> int:
    script = r"""
const diffs = [];
function hex(s) {
  let r = "";
  for (let i = 0; i < s.length; i++) r += (i ? "," : "") + s.charCodeAt(i).toString(16);
  return r;
}
for (let cp = 0; cp <= 0x10FFFF; cp++) {
  const s = (cp >= 0xD800 && cp <= 0xDFFF) ? String.fromCharCode(cp) : String.fromCodePoint(cp);
  const nfc = s.normalize("NFC"); if (nfc !== s) diffs.push(["nfc", cp, hex(nfc)]);
  const nfd = s.normalize("NFD"); if (nfd !== s) diffs.push(["nfd", cp, hex(nfd)]);
  const nfkc = s.normalize("NFKC"); if (nfkc !== s) diffs.push(["nfkc", cp, hex(nfkc)]);
  const nfkd = s.normalize("NFKD"); if (nfkd !== s) diffs.push(["nfkd", cp, hex(nfkd)]);
  const lo = s.toLowerCase(); if (lo !== s) diffs.push(["lo", cp, hex(lo)]);
  const up = s.toUpperCase(); if (up !== s) diffs.push(["up", cp, hex(up)]);
  const trl = s.toLocaleLowerCase("tr"); if (trl !== lo) diffs.push(["trl", cp, hex(trl)]);
  const tru = s.toLocaleUpperCase("tr"); if (tru !== up) diffs.push(["tru", cp, hex(tru)]);
  const ltl = s.toLocaleLowerCase("lt"); if (ltl !== lo) diffs.push(["ltl", cp, hex(ltl)]);
  const ltu = s.toLocaleUpperCase("lt"); if (ltu !== up) diffs.push(["ltu", cp, hex(ltu)]);
  const elu = s.toLocaleUpperCase("el"); if (elu !== up) diffs.push(["elu", cp, hex(elu)]);
}
console.log(JSON.stringify(diffs));
"""
    l_diffs = set(tuple(x) for x in json.loads(run_js(lb_bin, script, timeout=180)))
    if skip_node:
        print(f"PASS [all-code-points/no-node]: 1,114,112 CPs x 11 ops executed cleanly ({len(l_diffs)} non-identity entries)")
        return 0
    n_diffs = set(tuple(x) for x in json.loads(run_js(["node"], script)))
    only_node = sorted(n_diffs - l_diffs)
    only_lb = sorted(l_diffs - n_diffs)
    if only_node or only_lb:
        print(f"FAIL [all-code-points]: only_node={len(only_node)} only_lb={len(only_lb)}", file=sys.stderr)
        for item in only_node[:10]:
            print("  node only:", item, file=sys.stderr)
        for item in only_lb[:10]:
            print("  lb   only:", item, file=sys.stderr)
        return len(only_node) + len(only_lb)
    print(f"PASS [all-code-points]: 1,114,112 CPs x 11 ops (12,255,232 checks, {len(n_diffs)} non-identity entries) = 0 diffs")
    return 0


CURATED_STRINGS = [
    "", "abc", "ABC", "aBc123!@#",
    # Normalization: combining mark ordering, Hangul, exclusions, compat, astral, surrogates
    "e\u0301", "\u00E9", "e\u0323\u0301", "e\u0301\u0323",
    "\u1E9B\u0323", "\u017F\u0323\u0307", "\u212B", "\u00C5", "A\u030A",
    "\u0958", "\u0915\u093C", "\uFB1D", "\u05D9\u05B4",
    "\uAC00", "\u1100\u1161", "\uAC01", "\u1100\u1161\u11A8", "\uD7A3", "\u1112\u1175\u11C2",
    "\uFB01\uFB03\u00BD\u33A1\u2460\u3200\uFF21\uFF41",
    "a\u0300\u0315\u031B\u0323\u0301\u0302",
    "\uD800\u0301", "\uDC00\u0301", "e\uD800\u0301", "\uD834\uDD5E",
    # Case mapping: Final_Sigma, ligatures, supplementary planes
    "\u039F\u0394\u03A5\u03A3\u03A3\u0395\u03A5\u03A3",
    "A\u180E\u03A3", "A\u180E\u03A3B", "A\u03A3\u180E", "A\u03A3.", "\u03A3", "'\u03A3'", "A'\u03A3'",
    "\u00DF", "\u1E9E", "\u0130", "\u0149", "\u01F0", "\u0390", "\u03B0", "\u0587", "\u1E96", "\u1F80", "\u1F88", "\u1FFC",
    "\uFB00\uFB01\uFB02\uFB03\uFB04\uFB05\uFB06",
    "\U00010400\U00010428\U000104B0\U000104D8\U000118A0\U000118C0\U0001E900\U0001E922",
    # Turkish / Azeri
    "I", "i", "\u0130", "\u0131", "I\u0307", "I\u0323\u0307", "I\u0300\u0307", "IA\u0307",
    "I\U000101FD\u0307", "I\U0001D185\u0307", "\u0130STANBUL", "istanbul", "Diyarbak\u0131r",
    # Lithuanian
    "\u00CC", "\u00CD", "\u0128", "I\u0300", "J\u0300", "\u012E\u0300",
    "I\u0325\u0300", "J\U000101FD\U0001D185", "i\u0307", "j\u0323\u0307", "\u012F\u0307",
    "\U0001D456\u0307", "I\u0307",
    # Greek (el)
    "\u03AE", "\u0389", "\u03AE ", " \u03AE ", "\u03B1 \u03AE \u03B2", "\u03B1\u03AE", "\u03AE\u03B1",
    "\u03AC\u03B9", "\u03AD\u03B9", "\u03CC\u03B9", "\u03CD\u03B9",
    "\u03AC\u03C5", "\u03AD\u03C5", "\u03AE\u03C5", "\u03CC\u03C5",
    "\u03B1\u0390", "\u03BF\u0390", "\u03B1\u03B0", "\u03B5\u03B0",
    "\u0386\u03B9", "\u039C\u03AC\u03B9\u03BF\u03C2", "\u0391\u03B8\u03AE\u03BD\u03B1",
    "\u03B1\u0301\u03B9", "\u03B1\u0301", "\u03B9\u0308\u0301", "\u03C5\u0308\u0301",
    "\u1F84", "\u1F86", "\u1FB3", "\u1FC3", "\u1FF3", "\u1FE4", "\u1FE5", "\u03AE,", "1\u03AE",
]

COLLATION_PAIRS = [
    # Basic, case, accents, canonical equivalents
    ("a", "b", "und", None), ("a", "A", "und", None), ("A", "b", "und", None),
    ("a", "\u00E1", "und", None), ("\u00E9", "e\u0301", "und", None), ("\u00C5", "A\u030A", "und", None),
    ("\u1E9B\u0323", "\u017F\u0323\u0307", "und", None), ("\u00DF", "ss", "und", None),
    # Contractions & Hangul & CJK & Tangut/Nushu/Khitan & surrogates
    ("\u013F", "L\u00B7", "und", None), ("\u0439", "\u0438\u0306", "und", None),
    ("\uAC00", "\u1100\u1161", "und", None), ("\u4E00", "\u3400", "und", None),
    ("\u4E00", "\U00020000", "und", None), ("\U00017000", "\U00018800", "und", None),
    ("\U00018800", "\U0001B170", "und", None), ("\U0001B170", "\U00018B00", "und", None),
    ("\U00018B00", "\u4E00", "und", None), ("\uD800", "\uDC00", "und", None),
    ("\uD800", "\uFFFD", "und", None), ("\uD800", "a", "und", None),
    # Options: sensitivity, ignorePunctuation, numeric, caseFirst, -u- extensions
    ("a", "\u00E1", "und", {"sensitivity": "base"}),
    ("a", "A", "und", {"sensitivity": "base"}),
    ("a", "\u00E1", "und", {"sensitivity": "accent"}),
    ("a", "A", "und", {"sensitivity": "accent"}),
    ("a", "\u00E1", "und", {"sensitivity": "case"}),
    ("a", "A", "und", {"sensitivity": "case"}),
    ("a", "A", "und", {"sensitivity": "variant"}),
    ("a", "A", "und", {"caseFirst": "upper"}),
    ("a", "A", "und", {"caseFirst": "lower"}),
    ("a", "A", "en-u-kf-upper", None),
    ("a-b", "ab", "und", {"ignorePunctuation": True}),
    ("a b!", "ab?", "und", {"ignorePunctuation": True}),
    ("file2", "file10", "und", {"numeric": True}),
    ("file02", "file2", "und", {"numeric": True}),
    ("file2", "file10", "en-u-kn-true", None),
    ("file2", "file10", "en-u-kn", None),
    ("100", "20", "und", {"numeric": True}),
    ("100", "20", "und", {"numeric": False}),
]


def verify_curated_and_fuzz(lb_bin: list[str], fuzz_count: int, seed: int, skip_node: bool = False) -> int:
    rng = random.Random(seed)
    alphabet = [
        "a", "A", "e", "E", "i", "I", "j", "J", "o", "O", "s", "S", "0", "1", "9", " ", "-", ".",
        "\u00E9", "\u00C9", "\u00DF", "\u0130", "\u0131", "\u012E", "\u012F", "\u00CC", "\u00CD",
        "\u03A3", "\u03C3", "\u03C2", "\u03AC", "\u03AE", "\u03B9", "\u03C5", "\u0390", "\u03B0",
        "\u0300", "\u0301", "\u0307", "\u0308", "\u0323", "\u0325", "\u0345",
        "\u180E", "\uFB01", "\u212B", "\u017F", "\u1E9B",
        "\uAC00", "\u1100", "\u1161", "\u11A8", "\u4E00", "\u3400",
        "\U00010400", "\U00010428", "\U000101FD", "\U0001D185", "\U0001F600",
        "\uD800", "\uDC00",
    ]
    fuzz_strings = []
    for _ in range(fuzz_count):
        n = rng.randint(1, 8)
        fuzz_strings.append("".join(rng.choice(alphabet) for _ in range(n)))

    all_strings = CURATED_STRINGS + fuzz_strings
    fuzz_pairs = [(fuzz_strings[i], fuzz_strings[(i + 1) % len(fuzz_strings)], "und", None) for i in range(len(fuzz_strings))]
    all_pairs = COLLATION_PAIRS + fuzz_pairs

    parts = [
        "function hex(s) { let r = ''; for (let i = 0; i < s.length; i++) r += (i ? ',' : '') + s.charCodeAt(i).toString(16); return r; }",
        "function sgn(x) { return x < 0 ? -1 : x > 0 ? 1 : 0; }",
        "const strs = [" + ",".join(js_escape(s) for s in all_strings) + "];",
        "const res = [];",
        r"""
for (let i = 0; i < strs.length; i++) {
  const s = strs[i];
  res.push([
    hex(s.normalize("NFC")),
    hex(s.normalize("NFD")),
    hex(s.normalize("NFKC")),
    hex(s.normalize("NFKD")),
    hex(s.toLowerCase()),
    hex(s.toUpperCase()),
    hex(s.toLocaleLowerCase("tr")),
    hex(s.toLocaleUpperCase("tr")),
    hex(s.toLocaleLowerCase("lt")),
    hex(s.toLocaleUpperCase("lt")),
    hex(s.toLocaleUpperCase("el")),
  ]);
}
""",
        "const pairs = [",
    ]
    for a, b, loc, opt in all_pairs:
        opt_js = "undefined" if opt is None else json.dumps(opt)
        parts.append(f"  [{js_escape(a)}, {js_escape(b)}, {json.dumps(loc)}, {opt_js}],")
    parts.append("];")
    parts.append(r"""
const cres = [];
for (let i = 0; i < pairs.length; i++) {
  const p = pairs[i];
  cres.push(sgn(p[0].localeCompare(p[1], p[2], p[3])));
}
console.log(JSON.stringify({ res, cres }));
""")
    script = "\n".join(parts)
    lb_data = json.loads(run_js(lb_bin, script, timeout=120))
    if skip_node:
        assert len(lb_data["res"]) == len(all_strings) and len(lb_data["cres"]) == len(all_pairs)
        print(
            f"PASS [curated+fuzz/no-node]: {len(all_strings)} strings x 11 ops + {len(all_pairs)} collation pairs executed cleanly"
        )
        return 0
    node_data = json.loads(run_js(["node"], script))

    ops = ["NFC", "NFD", "NFKC", "NFKD", "toLower", "toUpper", "tr-lo", "tr-up", "lt-lo", "lt-up", "el-up"]
    fails = 0
    for idx, (nr, lr) in enumerate(zip(node_data["res"], lb_data["res"])):
        if nr != lr:
            for op, nv, lv in zip(ops, nr, lr):
                if nv != lv:
                    fails += 1
                    if fails <= 10:
                        print(f"FAIL [string #{idx} {op}] input={all_strings[idx]!r}: lb={lv} node={nv}", file=sys.stderr)
    for idx, (nc, lc) in enumerate(zip(node_data["cres"], lb_data["cres"])):
        if nc != lc:
            fails += 1
            if fails <= 10:
                print(f"FAIL [collation #{idx}] {all_pairs[idx]!r}: lb={lc} node={nc}", file=sys.stderr)

    if fails == 0:
        print(
            f"PASS [curated+fuzz]: {len(all_strings)} strings x 11 ops + {len(all_pairs)} collation pairs = 0 diffs"
        )
    return fails


def verify_ecma_validation(lb_bin: list[str]) -> int:
    script = r"""
let fails = 0;
function check(name, cond) {
  if (!cond) { fails++; console.log("FAIL:", name); }
}
function throwsKind(fn, Kind) {
  try { fn(); return false; } catch (e) { return e instanceof Kind; }
}
check("norm invalid RangeError", throwsKind(() => "a".normalize("INVALID"), RangeError));
check("norm null RangeError", throwsKind(() => "a".normalize(null), RangeError));
check("norm symbol TypeError", throwsKind(() => "a".normalize(Symbol()), TypeError));
check("norm null this TypeError", throwsKind(() => String.prototype.normalize.call(null), TypeError));
check("norm undef this TypeError", throwsKind(() => String.prototype.normalize.call(undefined), TypeError));
check("norm default NFC", "e\u0301".normalize() === "\u00E9");
check("norm undefined NFC", "e\u0301".normalize(undefined) === "\u00E9");

check("locale validates all lower", throwsKind(() => "".toLocaleLowerCase(["en-US", "not a locale"]), RangeError));
check("locale validates all upper", throwsKind(() => "".toLocaleUpperCase(["en-US", "not a locale"]), RangeError));
check("locale null TypeError", throwsKind(() => "a".toLocaleLowerCase(null), TypeError));
check("locale element null TypeError", throwsKind(() => "a".toLocaleLowerCase([null]), TypeError));
check("localeCompare null locales TypeError", throwsKind(() => "a".localeCompare("b", null), TypeError));
check("localeCompare invalid locale RangeError", throwsKind(() => "a".localeCompare("b", "not_valid"), RangeError));
check("localeCompare null opts TypeError", throwsKind(() => "a".localeCompare("b", "und", null), TypeError));
check("localeCompare bad sensitivity RangeError", throwsKind(() => "a".localeCompare("b", "und", { sensitivity: "x" }), RangeError));
check("localeCompare bad caseFirst RangeError", throwsKind(() => "a".localeCompare("b", "und", { caseFirst: "x" }), RangeError));
check("localeCompare bad usage RangeError", throwsKind(() => "a".localeCompare("b", "und", { usage: "x" }), RangeError));

for (const m of ["normalize", "toLowerCase", "toUpperCase", "toLocaleLowerCase", "toLocaleUpperCase", "localeCompare"]) {
  check("not constructor " + m, throwsKind(() => new String.prototype[m](), TypeError));
}
check("toLowerCase on Boolean obj", (() => { const b = new Boolean(true); b.toLowerCase = String.prototype.toLowerCase; return b.toLowerCase() === "true"; })());
check("toUpperCase on RegExp obj", (() => { const r = new RegExp("abc"); r.toUpperCase = String.prototype.toUpperCase; return r.toUpperCase() === "/ABC/"; })());
console.log("VALIDATION_FAILS=" + fails);
"""
    out = run_js(lb_bin, script)
    fails = int(out.strip().splitlines()[-1].split("=")[1])
    if fails:
        print(out, file=sys.stderr)
    else:
        print("PASS [ecma-validation]: 25/25 ECMA-262/ECMA-402 validation & error checks passed")
    return fails


def verify_ucd_official(lb_bin: list[str], ucd_dir: Path) -> int:
    fails = 0
    norm_path = ucd_dir / "NormalizationTest.txt"
    if norm_path.exists():
        cases = []
        with open(norm_path, encoding="utf-8") as f:
            for line in f:
                line = line.split("#", 1)[0].strip()
                if not line or line.startswith("@"):
                    continue
                cols = [[int(x, 16) for x in c.strip().split()] for c in line.split(";")[:5]]
                cases.append(cols)
        lines = ["let checks = 0, fails = 0;", "const cases = ["]
        for cols in cases:
            lines.append("  [" + ",".join(js_escape("".join(chr(c) for c in col)) for col in cols) + "],")
        lines.append("];")
        lines.append(r"""
for (let i = 0; i < cases.length; i++) {
  const [c1, c2, c3, c4, c5] = cases[i];
  const ok =
    c2 === c1.normalize("NFC") && c2 === c2.normalize("NFC") && c2 === c3.normalize("NFC") &&
    c4 === c4.normalize("NFC") && c4 === c5.normalize("NFC") &&
    c3 === c1.normalize("NFD") && c3 === c2.normalize("NFD") && c3 === c3.normalize("NFD") &&
    c5 === c4.normalize("NFD") && c5 === c5.normalize("NFD") &&
    c4 === c1.normalize("NFKC") && c4 === c2.normalize("NFKC") && c4 === c3.normalize("NFKC") &&
    c4 === c4.normalize("NFKC") && c4 === c5.normalize("NFKC") &&
    c5 === c1.normalize("NFKD") && c5 === c2.normalize("NFKD") && c5 === c3.normalize("NFKD") &&
    c5 === c4.normalize("NFKD") && c5 === c5.normalize("NFKD");
  checks += 20;
  if (!ok) fails++;
}
console.log(JSON.stringify([cases.length, checks, fails]));
""")
        n_lines, n_checks, n_fails = json.loads(run_js(lb_bin, "\n".join(lines), timeout=120).strip().splitlines()[-1])
        fails += n_fails
        if n_fails:
            print(f"FAIL [NormalizationTest.txt]: {n_fails} failed lines out of {n_lines}", file=sys.stderr)
        else:
            print(f"PASS [NormalizationTest.txt]: {n_lines} lines ({n_checks:,} assertions) = 0 fails")

    coll_path = ucd_dir / "CollationTest_CLDR_NON_IGNORABLE_SHORT.txt"
    if coll_path.exists():
        strs = []
        with open(coll_path, encoding="utf-8") as f:
            for line in f:
                line = line.split("#", 1)[0].strip()
                if not line:
                    continue
                cps = [int(x, 16) for x in line.rstrip(";").split()]
                strs.append(js_escape("".join(chr(c) for c in cps)))
        total_pairs = 0
        c_fails = 0
        chunk = 55000
        for start in range(0, len(strs), chunk):
            lo = max(0, start - 1)
            hi = min(len(strs), start + chunk)
            script = "const arr = [\n" + ",\n".join(strs[lo:hi]) + "\n];\n" + r"""
let fails = 0;
for (let i = 1; i < arr.length; i++) {
  if (arr[i - 1].localeCompare(arr[i]) > 0) fails++;
}
console.log(JSON.stringify([arr.length - 1, fails]));
"""
            p, fl = json.loads(run_js(lb_bin, script, timeout=120).strip().splitlines()[-1])
            total_pairs += p
            c_fails += fl
        fails += c_fails
        if c_fails:
            print(f"FAIL [CollationTest_CLDR]: {c_fails} failed pairs out of {total_pairs}", file=sys.stderr)
        else:
            print(f"PASS [CollationTest_CLDR]: {total_pairs:,} adjacent pairs = 0 fails")
    return fails


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=str(ROOT / "build" / "litebird"))
    ap.add_argument("--fuzz", type=int, default=1000)
    ap.add_argument("--seed", type=int, default=20261117)
    ap.add_argument("--ucd-dir", default="")
    ap.add_argument("--skip-node", action="store_true")
    ap.add_argument("--skip-full-sweep", action="store_true")
    args = ap.parse_args()

    if not args.skip_node and not shutil.which("node"):
        print("node is required for differential String Unicode verification", file=sys.stderr)
        return 2

    t0 = time.time()
    lb_bin = [args.binary]
    fails = 0
    if not args.skip_full_sweep:
        fails += verify_all_code_points(lb_bin, skip_node=args.skip_node)
    fails += verify_curated_and_fuzz(lb_bin, args.fuzz, args.seed, skip_node=args.skip_node)
    fails += verify_ecma_validation(lb_bin)
    if args.ucd_dir:
        fails += verify_ucd_official(lb_bin, Path(args.ucd_dir))

    elapsed = time.time() - t0
    if fails:
        print(f"FAILED: {fails} total discrepancies ({elapsed:.2f}s)", file=sys.stderr)
        return 1
    print(f"ALL STRING UNICODE CHECKS PASSED ({elapsed:.2f}s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
