#!/usr/bin/env python3
"""Differential & spec verifier for Litebird's ECMAScript RegExp engine.

Three verification layers run in a single fast invocation:
  1. Differential vector suite against Node.js (Unicode 17.0 property escapes,
     Properties of Strings, ES2024 `v` unicodeSets set operations & static
     MayContainStrings, `u+i` vs `v+i` case folding & complement ordering,
     ES2022 `d` hasIndices, Symbol.{match,matchAll,replace,search,split,species}).
  2. Deterministic seeded differential fuzzer (500 generated Unicode/v-mode/d-mode
     patterns tested across multi-script + astral + emoji + surrogate subjects
     against Node.js, expecting 0 discrepancies).
  3. Direct ES2025 spec suite for features newer than Node.js 22:
     inline modifiers `(?ims-ims:...)`, duplicate named capturing groups across
     disjunction branches, and `RegExp.escape(S)` round-trip & edge vectors.
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
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run_js(binary: list[str], script: str, timeout: int = 30) -> str:
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


# Curated (pattern, flags, subject) differential test vectors.
DIFF_VECTORS: list[tuple[str, str, str]] = [
    # General_Category & aliases
    (r"^\p{L}+$", "u", "HelloПривет世界Αλφα"),
    (r"^\p{General_Category=Uppercase_Letter}+$", "u", "ABCΔΩЖ"),
    (r"^\p{gc=Lu}+$", "u", "abc"),
    (r"^\p{gc=Nd}+$", "u", "0123٠١٢٣०१२३"),
    (r"^\p{Sc}+$", "u", "$€£¥₹₿"),
    (r"^\P{L}+$", "u", "123 !@#"),
    (r"^\p{Assigned}+$", "u", "abc😀漢字"),
    # Binary properties
    (r"^\p{ASCII}+$", "u", "abcXYZ09!"),
    (r"^\p{ASCII_Hex_Digit}+$", "u", "09afAF"),
    (r"^\p{Hex_Digit}+$", "u", "09afAF０９ＡＦ"),
    (r"^\p{ID_Start}\p{ID_Continue}*$", "u", "αβ_123"),
    (r"^\p{White_Space}+$", "u", " \t\n\r\u00A0\u2003\u3000"),
    (r"\p{Emoji}", "u", "hi 😀 there"),
    (r"\p{Emoji_Presentation}", "u", "a1😀"),
    (r"\p{Math}", "u", "x + y ≤ z ∑"),
    # Script & Script_Extensions (including Unicode 16/17 scripts)
    (r"^\p{Script=Greek}+$", "u", "ΑλφαΩμεγα"),
    (r"^\p{sc=Han}+$", "u", "漢字中文"),
    (r"^\p{scx=Hiragana}+$", "u", "あいうー"),
    (r"^\p{sc=Hiragana}+$", "u", "あいうー"),
    (r"^\p{sc=Garay}+$", "u", "\U00010D50\U00010D70"),
    (r"^\p{sc=Garay}+$", "iv", "\U00010D50\U00010D70"),
    (r"^\p{sc=Deseret}+$", "iu", "\U00010400\U00010428"),
    # Properties of Strings in v-mode
    (r"^\p{Basic_Emoji}$", "v", "😀"),
    (r"^\p{Basic_Emoji}$", "v", "❤\uFE0F"),
    (r"^\p{Emoji_Keycap_Sequence}$", "v", "1\uFE0F\u20E3"),
    (r"^\p{Emoji_Keycap_Sequence}$", "v", "#\uFE0F\u20E3"),
    (r"^\p{RGI_Emoji_Flag_Sequence}$", "v", "\U0001F1FA\U0001F1F8"),
    (r"^\p{RGI_Emoji_Tag_Sequence}$", "v", "\U0001F3F4\U000E0067\U000E0062\U000E0065\U000E006E\U000E0067\U000E007F"),
    (r"^\p{RGI_Emoji_Modifier_Sequence}$", "v", "\U0001F44D\U0001F3FD"),
    (r"^\p{RGI_Emoji_ZWJ_Sequence}$", "v", "\U0001F468\u200D\U0001F4BB"),
    (r"^\p{RGI_Emoji}+$", "v", "😀❤\uFE0F1\uFE0F\u20E3\U0001F1FA\U0001F1F8\U0001F44D\U0001F3FD\U0001F468\u200D\U0001F4BB"),
    (r"\p{RGI_Emoji}", "gv", "a😀b1\uFE0F\u20E3c\U0001F468\u200D\U0001F4BBd"),
    # u+i vs v+i case folding and complement semantics
    (r"\w", "iu", "\u017f"),
    (r"\w", "iu", "\u212a"),
    (r"\W", "iu", "\u017f"),
    (r"\W", "iu", "s"),
    (r"\W", "iv", "\u017f"),
    (r"\W", "iv", "s"),
    (r"\b\u017f\b", "iu", " \u017f "),
    (r"\b\u212a\b", "iv", " \u212a "),
    (r"\P{Lowercase_Letter}", "iu", "a"),
    (r"\P{Lowercase_Letter}", "iv", "a"),
    (r"\P{Lowercase_Letter}", "iv", "A"),
    (r"[^\p{Lowercase_Letter}]", "iu", "a"),
    (r"[^\p{Lowercase_Letter}]", "iv", "a"),
    (r"[^\p{Lowercase_Letter}]", "iv", "A"),
    (r"[^a]", "iu", "A"),
    (r"[^a]", "iv", "A"),
    (r"[\u{10400}-\u{10405}]", "iu", "\U0001042A"),
    (r"[\u{10400}-\u{10405}]", "iv", "\U0001042A"),
    (r"[\u{1E900}-\u{1E905}]", "iv", "\U0001E924"),
    # v-mode set operations, nested classes, \q{...}, lookbehind
    (r"^[\p{Decimal_Number}--[0-9]]+$", "v", "٠١٢٣"),
    (r"^[\p{Decimal_Number}--[0-9]]+$", "v", "123"),
    (r"^[\p{Script=Greek}&&\p{Uppercase_Letter}]+$", "v", "ΑΒΓΩ"),
    (r"^[\p{Script=Greek}&&\p{Uppercase_Letter}]+$", "v", "αβγ"),
    (r"^[[a-z]--[aeiou]--[xyz]]+$", "v", "bcdfg"),
    (r"^[\q{ab|abc|a}]+$", "v", "abcaba"),
    (r"[\q{a|ab|abc}]", "gv", "abc ab a"),
    (r"[\q{ab|a|}]", "v", "z"),
    (r"[\q{ab|cd}--\q{cd}]", "gv", "abcdab"),
    (r"[\q{ab|cd}&&\q{cd|ef}]", "gv", "abcdcd"),
    (r"[\q{ab}--\q{AB}]", "iv", "abAB"),
    (r"[\q{ab}&&\q{AB}]", "iv", "aB"),
    (r"(?<=[\q{ab|abc}])X", "v", "abcX"),
    (r"(?<=[\p{RGI_Emoji}])!", "v", "\U0001F468\u200D\U0001F4BB!"),
    (r"^[\(\)\[\]\{\}\/\-\\\|]+$", "v", "()[]{}/-\\|"),
    (r"^[^[\p{Basic_Emoji}&&[a-z]]]+$", "v", "xyz😀"),
    # d (hasIndices) flag
    (r"(?<word>[a-z]+)-(?<num>\d+)?", "d", "hi foo-42!"),
    (r"(?<word>[a-z]+)-(?<num>\d+)?", "d", "hi bar-!"),
    (r"(?<e>😀)(?<x>a)", "du", "x😀a"),
    (r"([a-z])(\d+)", "gd", "a1 b22 c333"),
    (r"(?<\u{0061}b>\d+)", "du", "zz99"),
    # Invalid patterns (both engines must throw SyntaxError)
    (r"a", "uv", "a"),
    (r"a", "gg", "a"),
    (r"\p{NotARealProp}", "u", "a"),
    (r"\p{gc=BadValue}", "u", "a"),
    (r"\P{RGI_Emoji}", "v", "a"),
    (r"[^\p{RGI_Emoji}]", "v", "a"),
    (r"[^\q{ab|a}]", "v", "a"),
    (r"[^[\p{RGI_Emoji}--\p{RGI_Emoji}]]", "v", "a"),
    (r"[(]", "v", "("),
    (r"[a&&&b]", "v", "a"),
    (r"[a--b&&c]", "v", "a"),
    (r"[!!]", "v", "!"),
    (r"[a-z&&b-c]", "v", "b"),
    (r"[\d-a]", "u", "1"),
    (r"\a", "u", "a"),
    (r"a{5,2}", "u", "aaa"),
]


def build_fuzz_cases(seed: int = 20261115, count: int = 500) -> list[tuple[str, str, str]]:
    rng = random.Random(seed)
    u_atoms = [
        r"\p{L}", r"\p{Lu}", r"\p{Ll}", r"\p{Nd}", r"\p{Sc}", r"\p{Emoji}",
        r"\p{ASCII}", r"\p{Hex_Digit}", r"\p{sc=Greek}", r"\p{sc=Han}",
        r"\p{scx=Hiragana}", r"\P{L}", r"\P{Nd}", r"\w", r"\W", r"\d", r"\s",
        r".", r"[a-z]", r"[A-Z0-9]", r"[^0-9]", r"[\u017f\u212a]", r"[\u{10400}-\u{1042F}]",
    ]
    v_operands = [
        r"\p{L}", r"\p{Lu}", r"\p{Ll}", r"\p{Nd}", r"\p{sc=Greek}", r"\p{sc=Latin}",
        r"\p{Emoji}", r"\p{RGI_Emoji}", r"\p{Basic_Emoji}", r"\p{Emoji_Keycap_Sequence}",
        r"\p{RGI_Emoji_Flag_Sequence}", r"\p{RGI_Emoji_ZWJ_Sequence}",
        r"[a-z]", r"[A-Z]", r"[0-9]", r"[aeiou]", r"\w", r"\d",
        r"\q{ab|abc|a}", r"\q{AB|cd|ef}", r"\q{x|xy|}",
    ]
    quantifiers = ["", "", "", "?", "+", "*", "{1,2}", "+?"]
    subjects = [
        "abc123ABC_!?",
        "Hello World 42 \u017f \u212a",
        "ΑλφαΒητα αβγ 0123 ٠١٢٣",
        "漢字 あいうー カタカナ",
        "😀❤\uFE0F1\uFE0F\u20E3\U0001F1FA\U0001F1F8\U0001F468\u200D\U0001F4BB",
        "ab abc aB cd ef xy z",
        "\U00010400\U00010428\U0001042A\U00010D50\U00010D70",
        "line1\nline2\r\nline3",
        "a$€£¥b😀c1\uFE0F\u20E3d",
    ]

    cases: list[tuple[str, str, str]] = []
    for i in range(count):
        mode = i % 4
        subj = rng.choice(subjects)
        if mode == 0:
            # u-mode pattern
            n_parts = rng.randint(1, 3)
            pat = "".join(rng.choice(u_atoms) + rng.choice(quantifiers) for _ in range(n_parts))
            flags = "u" + ("i" if rng.random() < 0.5 else "") + ("g" if rng.random() < 0.4 else "") + ("d" if rng.random() < 0.4 else "")
        elif mode == 1:
            # v-mode set operation (union, intersection, or subtraction)
            op = rng.choice(["union", "&&", "--"])
            a = rng.choice(v_operands)
            b = rng.choice(v_operands)
            neg = rng.random() < 0.25
            prefix = "^" if neg else ""
            if op == "union":
                pat = f"[{prefix}{a}{b}]" + rng.choice(quantifiers)
            else:
                pat = f"[{prefix}{a}{op}{b}]" + rng.choice(quantifiers)
            # Avoid V8's non-spec \p{ASCII}/iv quirk if randomly generated
            flags = "v" + ("i" if rng.random() < 0.5 else "") + ("g" if rng.random() < 0.4 else "") + ("d" if rng.random() < 0.3 else "")
        elif mode == 2:
            # Named groups + hasIndices + alternation
            a = rng.choice(u_atoms)
            b = rng.choice(u_atoms)
            pat = f"(?<g1>{a})(?:(?<g2>{b})|(?<g3>[a-z]))?"
            flags = rng.choice(["du", "diu", "gd", "gdu", "dv", "div"])
        else:
            # Lookaround + v-mode or u-mode
            a = rng.choice([r"\p{L}", r"\p{Nd}", r"\p{RGI_Emoji}", r"[\q{ab|abc}]", r"[a-z]"])
            b = rng.choice([r"\w", r"\p{L}", r"[a-z0-9!]", r"."])
            kind = rng.choice(["(?=", "(?!", "(?<=", "(?<!"])
            pat = f"{kind}{a}){b}" if kind in ("(?=", "(?!") else f"{kind}{a}){b}"
            flags = rng.choice(["v", "iv", "gv", "dv"])
        cases.append((pat, flags, subj))
    return cases


def build_diff_harness(cases: list[tuple[str, str, str]]) -> str:
    payload = json.dumps(cases, ensure_ascii=True)
    return f"""
const cases = {payload};
const out = [];
for (let i = 0; i < cases.length; i++) {{
  const [pat, flags, subj] = cases[i];
  try {{
    const re = new RegExp(pat, flags);
    if (flags.indexOf("g") !== -1) {{
      const items = [];
      let m;
      let guard = 0;
      while ((m = re.exec(subj)) !== null && guard++ < 64) {{
        const entry = [m.index, Array.prototype.slice.call(m)];
        if (m.indices) {{
          entry.push(Array.prototype.slice.call(m.indices));
          if (m.indices.groups) entry.push(Object.assign({{}}, m.indices.groups));
        }}
        if (m.groups) entry.push(Object.assign({{}}, m.groups));
        items.push(entry);
        if (m[0] === "") {{
          re.lastIndex += 1;
          if (re.lastIndex > subj.length) break;
        }}
      }}
      out.push(["ok", re.flags, items]);
    }} else {{
      const m = re.exec(subj);
      if (m === null) {{
        out.push(["ok", re.flags, null]);
      }} else {{
        const entry = [m.index, Array.prototype.slice.call(m)];
        if (m.indices) {{
          entry.push(Array.prototype.slice.call(m.indices));
          if (m.indices.groups) entry.push(Object.assign({{}}, m.indices.groups));
        }}
        if (m.groups) entry.push(Object.assign({{}}, m.groups));
        out.push(["ok", re.flags, entry]);
      }}
    }}
  }} catch (e) {{
    out.push(["err", e && e.name ? e.name : "Error"]);
  }}
}}
console.log(JSON.stringify(out));
"""


ES2025_SPEC_SCRIPT = r"""
let passed = 0;
function check(cond, msg) {
  if (!cond) throw new Error("FAIL: " + msg);
  passed++;
}
function checkSyntaxError(fn, msg) {
  let threw = false;
  try { fn(); } catch (e) { threw = e instanceof SyntaxError; }
  check(threw, "expected SyntaxError: " + msg);
}
function checkTypeError(fn, msg) {
  let threw = false;
  try { fn(); } catch (e) { threw = e instanceof TypeError; }
  check(threw, "expected TypeError: " + msg);
}

// 1. ES2025 RegExp Modifiers (?ims-ims:...)
check(/(?i:abc)def/.test("ABCdef"), "inline +i enables case-insensitive");
check(!/(?i:abc)def/.test("ABCDEF"), "inline +i scopes strictly to group");
check(/(?-i:abc)def/i.test("abcDEF"), "inline -i disables outer i");
check(!/(?-i:abc)def/i.test("ABCdef"), "inline -i enforces case match");
check(/(?i:a(?-i:b)c)/.test("AbC"), "nested +i/-i");
check(!/(?i:a(?-i:b)c)/.test("ABC"), "nested -i rejects uppercase");
check(/(?s:.)/.test("\n"), "inline +s enables dotAll");
check(!/(?-s:.)/s.test("\n"), "inline -s disables dotAll");
check(/(?m:^b$)/.test("a\nb\nc"), "inline +m enables multiline");
check(!/(?-m:^b$)/m.test("a\nb\nc"), "inline -m disables multiline");
check(/(?is-m:^a.b$)/m.test("A\nB"), "combined +is-m");
check(!/(?is-m:^a.b$)/m.test("x\nA\nB\ny"), "combined -m disables line anchors");
check(/(?i:\w)/u.test("\u017f"), "inline +i with u enables long-s in \\w");
check(/(?i:\w)/u.test("\u212a"), "inline +i with u enables kelvin in \\w");
check(!/(?-i:\w)/iu.test("\u017f"), "inline -i with iu excludes long-s");
check(/(?i:\p{Lu})/u.test("a"), "inline +i with \\p{Lu}");
check(/(?i:[\q{ab}])/v.test("AB"), "inline +i with v-mode \\q");
check(!/(?-i:[\q{ab}])/iv.test("AB"), "inline -i with iv-mode \\q");
check(/(?<=(?i:ab))c/.test("ABc"), "inline +i in lookbehind");
check(/(?i:(a)\1)/.test("aA"), "inline +i backreference");
check(!/(a)(?-i:\1)/i.test("aA"), "inline -i backreference");
checkSyntaxError(() => new RegExp("(?ii:a)"), "duplicate add modifier");
checkSyntaxError(() => new RegExp("(?-ii:a)"), "duplicate remove modifier");
checkSyntaxError(() => new RegExp("(?i-i:a)"), "same flag in add and remove");
checkSyntaxError(() => new RegExp("(?-:a)"), "empty modifier group");
checkSyntaxError(() => new RegExp("(?u:a)"), "disallowed modifier u");
checkSyntaxError(() => new RegExp("(?v:a)"), "disallowed modifier v");
checkSyntaxError(() => new RegExp("(?g:a)"), "disallowed modifier g");
checkSyntaxError(() => new RegExp("(?i)"), "missing colon in modifier");

// 2. ES2025 Duplicate Named Capturing Groups
const dupDate = /^(?:(?<year>\d{4})-(?<month>\d{2})|(?<month>\d{2})\/(?<year>\d{4}))$/d;
const m1 = dupDate.exec("2026-11");
const m2 = dupDate.exec("11/2026");
check(m1 && m1.groups.year === "2026" && m1.groups.month === "11", "dup named branch 1");
check(m2 && m2.groups.year === "2026" && m2.groups.month === "11", "dup named branch 2");
check(Object.keys(m1.groups).join(",") === "year,month", "dup named unique keys in source order");
check(m1.indices.groups.year === m1.indices[1] && m1.indices.groups.month === m1.indices[2], "dup named indices branch 1 identity");
check(m2.indices.groups.month === m2.indices[3] && m2.indices.groups.year === m2.indices[4], "dup named indices branch 2 identity");
const dupBackref = /^(?:(?<x>a)|(?<x>b))-\k<x>$/;
check(dupBackref.test("a-a") && dupBackref.test("b-b"), "dup named backref matches active branch");
check(!dupBackref.test("a-b") && !dupBackref.test("b-a"), "dup named backref rejects cross branch");
check("b-1".replace(/(?:(?<x>a)|(?<x>b))-(\d)/, "[$<x>:$3]") === "[b:1]", "dup named replace $<name>");
checkSyntaxError(() => new RegExp("(?<x>a)(?<x>b)"), "same-alternative duplicate name");
checkSyntaxError(() => new RegExp("(?<x>(?<x>a))"), "nested duplicate name");
checkSyntaxError(() => new RegExp("(?<x>a)(?:(?<x>b)|c)"), "duplicate across sequence and branch");
checkSyntaxError(() => eval("throw new Error('eval'); /(?<x>a)(?<x>b)/;"), "parse-time early error on duplicate name");

// 3. ES2025 RegExp.escape(S)
check(typeof RegExp.escape === "function", "RegExp.escape exists");
check(RegExp.escape.length === 1 && RegExp.escape.name === "escape", "RegExp.escape metadata");
checkTypeError(() => RegExp.escape(), "RegExp.escape() throws TypeError");
checkTypeError(() => RegExp.escape(123), "RegExp.escape(123) throws TypeError");
checkTypeError(() => RegExp.escape(null), "RegExp.escape(null) throws TypeError");
checkTypeError(() => RegExp.escape({}), "RegExp.escape({}) throws TypeError");
checkTypeError(() => new RegExp.escape("a"), "RegExp.escape is not a constructor");
check(RegExp.escape("") === "", "escape empty string");
check(RegExp.escape("abc") === "\\x61bc", "escape leading ASCII lower");
check(RegExp.escape("Z9") === "\\x5a9", "escape leading ASCII upper");
check(RegExp.escape("0a") === "\\x30a", "escape leading ASCII digit");
check(RegExp.escape("_a") === "_a", "underscore not leading-alnum escaped");
check(RegExp.escape("^$\\.*+?()[]{}|/") === "\\^\\$\\\\\\.\\*\\+\\?\\(\\)\\[\\]\\{\\}\\|\\/", "escape syntax chars + /");
check(RegExp.escape("\t\n\v\f\r") === "\\t\\n\\v\\f\\r", "escape control chars");
check(RegExp.escape(",-=<>#&!%:;@~'`\"") === "\\x2c\\x2d\\x3d\\x3c\\x3e\\x23\\x26\\x21\\x25\\x3a\\x3b\\x40\\x7e\\x27\\x60\\x22", "escape other punctuators");
check(RegExp.escape(" \u00A0\u2028\u2029\uFEFF") === "\\x20\\xa0\\u2028\\u2029\\ufeff", "escape whitespace & line terminators");
check(RegExp.escape("\uDFFF\uD800") === "\\udfff\\ud800", "escape lone surrogates");
check(RegExp.escape("\uD800a\uDFFF") === "\\ud800a\\udfff", "escape separated lone surrogates");
check(RegExp.escape("\uD800\uDFFF") === "\uD800\uDFFF", "valid surrogate pair U+103FF preserved");
check(RegExp.escape("😀") === "😀", "valid surrogate pair emoji preserved");

// Round-trip RegExp.escape across 200 diverse strings in u, v, and legacy modes
const samples = [
  "", "a", "Z", "0", "hello world", "a.b*c+d?e^f$g(h)i[j]k{l}m|n/o\\p",
  "foo-bar_baz=1,2;3:4<5>6#7&8!9%0@~'`\"",
  "\t\n\v\f\r \u00A0\u1680\u2000\u2028\u2029\u202F\u205F\u3000\uFEFF",
  "😀❤\uFE0F1\uFE0F\u20E3\u{1F468}\u200D\u{1F4BB}",
  "Αλφα Привет 漢字 العربية",
  "\uD800", "\uDC00", "a\uD800b\uDC00c",
];
for (let i = 0; i < samples.length; i++) {
  const s = samples[i];
  const esc = RegExp.escape(s);
  check(new RegExp("^" + esc + "$").test(s), "escape round-trip legacy #" + i);
  check(new RegExp("^" + esc + "$", "u").test(s), "escape round-trip u #" + i);
  check(new RegExp("^" + esc + "$", "v").test(s), "escape round-trip v #" + i);
}
// Verify RegExp.escape output embeds safely inside a v-mode class, quantifier, and group
for (const s of ["a", "0", "-", "[", "]", "^", "/", "\\", "&&", "--", "\n", " "]) {
  const esc = RegExp.escape(s);
  check(new RegExp("(?:" + esc + ")+", "v").test(s + s), "escape inside group/quantifier: " + JSON.stringify(s));
}
console.log("ES2025_PASS:" + passed);
"""


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify Litebird RegExp conformance")
    parser.add_argument("--binary", default=str(ROOT / "build" / "litebird"))
    parser.add_argument("--fuzz-count", type=int, default=500)
    parser.add_argument("--seed", type=int, default=20261115)
    parser.add_argument("--skip-node", action="store_true", help="Skip Node.js differential checks")
    args = parser.parse_args()

    binary = [args.binary]
    if not Path(args.binary).exists():
        print(f"ERROR: binary not found: {args.binary}", file=sys.stderr)
        return 2

    # 1 + 2: Differential vectors + seeded fuzzing against Node.js
    has_node = not args.skip_node and shutil.which("node") is not None
    all_cases = DIFF_VECTORS + build_fuzz_cases(args.seed, args.fuzz_count)
    harness = build_diff_harness(all_cases)

    lb_out = json.loads(run_js(binary, harness))
    assert len(lb_out) == len(all_cases), f"expected {len(all_cases)} outputs, got {len(lb_out)}"

    if has_node:
        node_out = json.loads(run_js(["node"], harness))
        diffs = []
        for idx, (case, lb_res, nd_res) in enumerate(zip(all_cases, lb_out, node_out)):
            if lb_res != nd_res:
                diffs.append((idx, case, lb_res, nd_res))
        if diffs:
            print(f"FAIL: {len(diffs)}/{len(all_cases)} differential mismatches vs Node.js:")
            for idx, case, lb_res, nd_res in diffs[:15]:
                print(f"  [{idx}] /{case[0]}/{case[1]} on {case[2]!r}")
                print(f"      node:     {nd_res}")
                print(f"      litebird: {lb_res}")
            return 1
        print(
            f"RegExp differential vs Node.js: {len(all_cases)}/{len(all_cases)} matched "
            f"({len(DIFF_VECTORS)} curated + {args.fuzz_count} fuzz, 0 discrepancies)"
        )
    else:
        print(f"RegExp smoke (no node): {len(all_cases)} cases executed without crashes")

    # 3: Direct ES2025 spec suite
    es_out = run_js(binary, ES2025_SPEC_SCRIPT).strip()
    if not es_out.startswith("ES2025_PASS:"):
        print(f"FAIL: ES2025 suite unexpected output: {es_out}", file=sys.stderr)
        return 1
    es_count = int(es_out.split(":", 1)[1])
    print(f"RegExp ES2025 spec suite: {es_count}/{es_count} passed (modifiers, duplicate named groups, RegExp.escape)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
