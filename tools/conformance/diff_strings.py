#!/usr/bin/env python3
"""Runs thousands of small string expressions in Litebird and in Node and lists the results that differ.

The corpus (`tests/js-conformance/`) checks scripts somebody thought of. This
checks a grid: a few dozen subject strings (ASCII, accented, CJK, characters
outside the BMP, halves of a surrogate pair on their own, text long enough to
cross the engine's internal boundaries) through every `String.prototype`
method, the operators, the string-taking globals and a set of regular
expressions. Both engines evaluate the same program and print every result
with each code unit spelled out, so a difference is a difference in the value
and never in how a terminal shows it. A thrown error is compared by its name.

Usage:
    python3 tools/conformance/diff_strings.py                    # summary, and the first 20 differences
    python3 tools/conformance/diff_strings.py --show 0           # summary only
    python3 tools/conformance/diff_strings.py --only split       # expressions whose source contains "split"
    python3 tools/conformance/diff_strings.py --binary /tmp/old/build/litebird
    python3 tools/conformance/diff_strings.py --max-differ 95    # exit 1 if more than 95 results differ

`make diff-strings` runs it with the ceiling CI enforces. It needs `node`.

Every difference it lists at the time of writing is Unicode data Litebird does
not have: collation for `localeCompare`, normalization (`normalize()` returns
its string), the case mappings of a few characters, and part of the letter
table behind `\p{L}`. ARCHITECTURE.md lists them under "Known gaps". A new
kind of difference is a bug: turn it into a corpus case with
tools/conformance/add_js_case.py and fix it.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from collections import Counter

# Subjects. Written with escapes so that the program text is ASCII and no
# shell, terminal or argument decoding can change what the engines are given.
SUBJECTS = [
    '""', '"a"', '"abc"', '"Hello, World"', '"  padded\\t\\n"', '"a,b,,c"', '"aXbXc"', '"ABCabc"',
    '"0123456789"', '"12.5px"', '"The quick brown fox"', '"a\\u0000b"',
    # outside ASCII, inside the BMP
    '"\\u00e9"', '"caf\\u00e9"', '"stra\\u00dfe"', '"\\u0130stanbul"', '"\\u01c5"', '"e\\u0301"',
    '"\\u65e5\\u672c\\u8a9e"', '"\\uff46\\uff55\\uff4c\\uff4c"', '"\\uffff"', '"\\u00a0x\\u2028"',
    # outside the BMP: two code units for one character
    '"\\ud83d\\ude00"', '"a\\ud83d\\ude00b"', '"\\ud83d\\ude00\\ud83d\\ude00"', '"\\udbff\\udfff\\ud800\\udc00"',
    # halves of a pair on their own, in every arrangement
    '"\\ud83d"', '"\\ude00"', '"a\\ud83d"', '"\\ude00b"', '"\\ud83dx\\ude00"', '"\\ude00\\ud83d"',
    '"\\ud83d\\ud83d\\ude00\\ude00"',
    # long enough to cross a 256-unit boundary, which is where the engine keeps its marks
    '"x".repeat(300)', '"\\u00e9".repeat(257) + "z"', '"ab\\ud83d\\ude00".repeat(90)',
]

# The second operand of the two-operand expressions.
PARTNERS = ['""', '"a"', '"\\u00e9"', '"\\ud83d\\ude00"', '"\\ud83d"', '"\\ude00"', '"b\\ud83d"', '"\\ude00c"', '"\\uffff"']

INDEXES = ["-1", "0", "1", "2", "5", "255", "256", "257", "299", "1e9", "NaN", "1.7", '"1"', "undefined"]
RANGES = ["0", "1", "-1", "2, 5", "5, 2", "-3, -1", "0, 0", "1, 1e9", "NaN, 3", "255, 258", "undefined, 2",
          "1, undefined", "-1e9, 1e9", "1.9, 3.2", "3", "0, -1", "256", "-257"]
NEEDLES = ['""', '"a"', '"b"', '","', '" "', '"ab"', '"x"', '"\\u00e9"', '"\\ud83d\\ude00"', '"\\ud83d"', '"\\ude00"',
           "s", "s.slice(1, 3)", "s.slice(-2)", "s.charAt(0)", "undefined"]
PADS = ["", '""', '"ab"', '"\\u00e9"', '"\\ud83d\\ude00"', '"\\ude00\\ud83d"', '"\\ud83d"']
REGEXES = [
    "/a/", "/a/g", "/./g", "/./gu", "/./gs", "/[^]/gu", "/\\w+/g", "/\\s+/", "/(?:)/g", "/(?:)/gu", "/[^a-z]/gi",
    "/\\ud83d/", "/\\ud83d/u", "/\\ude00/g", "/\\ude00/gu", "/[\\ud83d\\ude00]/g", "/[\\ud83d\\ude00]/gu",
    "/\\u{1f600}/u", "/\\p{L}+/gu", "/^.|.$/g", "/^.|.$/gu", "/(.)(.)/", "/(?<first>.)(?<rest>.*)/s", "/\\b/g",
    "/(?<=a)./g", "/(?<=\\ud83d)./g", "/(?<=\\ud83d)./gu", "/.(?=b)/g", "/[^\\x00-\\x7f]+/g", "/$/", "/x{2,}?/g",
    "/(a)|(b)/g", "/\\S/y", "/\\u00e9/i", "/[\\u00c0-\\u00ff]/gi", "/.{3}/gu",
    # lookbehind: matched right to left, which decides what its groups capture
    "/(?<=(\\d+)(\\d+))$/", "/(?<=(\\d+?)(\\d+))$/", "/(?<=\\1(a))b/i", "/(?<=(a)\\1)b/i", "/(?<=\\b\\w)\\w+/g",
    "/(?<!,)[a-c]/g", "/(?<=^|,)[^,]*/g", "/(?<![a-z])[a-z]+/gi", "/(?<=a(?=b)b)./gi", "/(?<!(?<=a)b)[a-c]/g",
    "/(?<=x{2,3})x/g", "/(?<=\\ud83d\\ude00)./gu", "/(?<=.)\\ude00/g", "/(?<=.)\\ude00/gu", "/(?<=(.)\\ud83d)(.)/",
]


def expressions():
    """Two lists of (family, source). In the first `s` is the subject; in the
    second `t` is a second operand as well."""
    one, two = [], []

    def add(family, source):
        one.append((family, source))

    add("length", "s.length")
    for i in INDEXES:
        for method in ("charAt", "charCodeAt", "codePointAt", "at"):
            add(method, f"s.{method}({i})")
        add("s[i]", f"s[{i}]")
    add("at", "s.at(-2)")
    add("charAt", "s.charAt(s.length - 1)")
    for r in RANGES:
        for method in ("slice", "substring", "substr"):
            add(method, f"s.{method}({r})")
    for k in range(0, 8):
        add("cut and join", f"s.slice(0, {k}) + s.slice({k}) === s")
        add("cut and join", f"(s.substring({k}) + s.substring(0, {k})).length === s.length")
        add("cut and join", f"s.slice({k}).concat(s.slice(0, {k}))")
    for n in NEEDLES:
        for method, arguments in (("indexOf", "{n}"), ("indexOf", "{n}, 2"), ("lastIndexOf", "{n}"),
                                  ("lastIndexOf", "{n}, 2"), ("includes", "{n}"), ("includes", "{n}, 1"),
                                  ("startsWith", "{n}"), ("startsWith", "{n}, 1"), ("endsWith", "{n}"),
                                  ("endsWith", "{n}, 2"), ("split", "{n}"), ("split", "{n}, 2"),
                                  ("replace", '{n}, "[$&]"'), ("replaceAll", '{n}, "<$`|$\'>"'),
                                  ("replace", "{n}, function (m, i) {{ return i + m.length; }}")):
            add(method, f"s.{method}({arguments.format(n=n)})")
    for method in ("toUpperCase", "toLowerCase", "toLocaleUpperCase", "toLocaleLowerCase", "trim", "trimStart",
                   "trimEnd", "normalize", "toString", "valueOf", "isWellFormed", "toWellFormed"):
        add(method, f"s.{method}()")
    for form in ("NFC", "NFD", "NFKC", "NFKD"):
        add("normalize", f's.normalize("{form}")')
    for n in ("0", "1", "3", "-1", "2.5", "Infinity"):
        add("repeat", f"s.repeat({n})")
    for width in ("0", "5", "12", "301"):
        for pad in PADS:
            comma = ", " if pad else ""
            add("padStart", f"s.padStart({width}{comma}{pad})")
            add("padEnd", f"s.padEnd({width}{comma}{pad})")
    for source in ("[...s].length", '[...s].join("|")', "Array.from(s).length", 's.split("").length',
                   's.split("").reverse().join("")', '[...s].reverse().join("")',
                   "[...s].map(function (c) { return c.codePointAt(0); })",
                   "String.fromCodePoint.apply(null, [...s].map(function (c) { return c.codePointAt(0); })) === s",
                   'String.fromCharCode.apply(null, s.split("").map(function (c) { return c.charCodeAt(0); })) === s',
                   "(function () { var n = 0; for (var c of s) n += c.length; return n; })()",
                   "(function () { var k = []; for (var i in s) { k.push(i); if (k.length > 3) break; } return k; })()"):
        add("iteration", source)
    for source in ("encodeURIComponent(s)", "encodeURI(s)", "decodeURIComponent(encodeURIComponent(s)) === s",
                   "decodeURI(encodeURI(s)) === s", "escape(s)", "unescape(escape(s)) === s"):
        add("URI functions", source)
    for source in ("JSON.stringify(s)", "JSON.parse(JSON.stringify(s)) === s", "JSON.stringify([s, s])",
                   "JSON.stringify({ k: s })",
                   "(function () { var o = {}; o[s] = 1; return Object.keys(JSON.parse(JSON.stringify(o)))[0] === s; })()"):
        add("JSON", source)
    for source in ("String(s)", "typeof s", "new String(s).length", "Object(s)[1]", "Number(s)", "parseInt(s)",
                   "parseFloat(s)", "+s", "!s", "s ? 1 : 2", "s + 1", "1 + s", "s * 1", "s == 0", "`<${s}>`",
                   "[s].join()", "[s, s].join(s)", "String([s, [s]])", "s.concat()", "s.concat(1, null, s)"):
        add("conversion", source)
    for source in ("(function () { var o = {}; o[s] = 1; return Object.keys(o)[0] === s; })()",
                   '(function () { var o = {}; o[s] = 1; return o[s + ""]; })()',
                   "new Map([[s, 7]]).get(s.slice(0, 1) + s.slice(1))",
                   'new Set([s, s + "", s.slice(0)]).size', "s in { a: 1, abc: 2 }",
                   "(function () { switch (s) { case s.slice(0, 2) + s.slice(2): return 1; } return 0; })()"):
        add("as a key", source)
    for regex in REGEXES:
        family = "regex " + regex
        for source in (
                "{r}.test(s)", "{r}.exec(s)", "s.search({r})", "s.match({r})", "s.split({r})", "s.split({r}, 3)",
                's.replace({r}, "[$&]")', 's.replace({r}, "$2-$1")',
                "s.replace({r}, function (m) {{ return m.length + arguments[arguments.length - 2]; }})",
                # every match of a global or sticky expression: where it starts and where lastIndex is left
                "(function () {{ var r = {r}, n = [], m; if (!r.global && !r.sticky) return -1; "
                "while ((m = r.exec(s)) && n.length < 400) {{ n.push(m.index, r.lastIndex); "
                'if (m[0] === "") r.lastIndex++; }} return n; }})()'):
            add(family, source.format(r=regex))
        if "g" in regex.rsplit("/", 1)[1]:
            add(family, f"[...s.matchAll({regex})].map(function (m) {{ return [m.index, m[0]]; }})")
            add(family, f's.replaceAll({regex}, "<$&>")')

    for family, source in (
            ("+", "s + t"), ("+", "t + s"), ("+", "(s + t).length"), ("+", "`${s}${t}`"), ("+", '[s, t].join("")'),
            ("+", "s.concat(t, s)"), ("+", "(function () { var a = s; a += t; a += s; return a === s + t + s; })()"),
            ("+", "[...(s + t)].length"), ("+", "(s + t).codePointAt(s.length - 1)"),
            ("+", "(s + t).slice(s.length) === t"), ("+", "JSON.stringify(s + t)"), ("+", "encodeURIComponent(s + t)"),
            ("+", '(function () { var o = {}; o[s + t] = 1; return o[[s, t].join("")]; })()'),
            ("comparison", "s < t"), ("comparison", "s <= t"), ("comparison", "s > t"), ("comparison", "s == t"),
            ("comparison", "s === t"), ("comparison", "[s, t, s + t, t + s].sort()"),
            ("comparison", "[s, t].sort(function (a, b) { return a < b ? -1 : a > b ? 1 : 0; })[0]"),
            ("localeCompare", "s.localeCompare(t)"),
            ("localeCompare", "Math.sign(s.localeCompare(t)) === -Math.sign(t.localeCompare(s))"),
            ("isWellFormed", "(s + t).isWellFormed()"), ("toWellFormed", "(s + t).toWellFormed()"),
            ("indexOf", "(s + t).indexOf(t)"), ("lastIndexOf", "(t + s).lastIndexOf(t)"),
            ("split", "(s + t + s).split(t).length"), ("split", "s.split(t, 4)"),
            ("padEnd", "s.padEnd(s.length + t.length, t) === s + t"), ("startsWith", "s.startsWith(t)"),
            ("endsWith", "s.endsWith(t)"), ("includes", "s.includes(t)"), ("replace", "s.replace(t, s)"),
            ("normalize", "(s + t).normalize() === s.normalize() + t.normalize()")):
        two.append((family, source))
    return one, two


SHOW = r"""
function show(v) {
  if (v === null) return "null";
  if (v === undefined) return "undefined";
  var kind = typeof v;
  if (kind === "string") {
    var text = '"';
    for (var i = 0; i < v.length; i++) {
      var c = v.charCodeAt(i);
      text += (c >= 32 && c < 127 && c !== 34 && c !== 92) ? v.charAt(i) : "\\u" + ("0000" + c.toString(16)).slice(-4);
    }
    return text + '"';
  }
  if (kind === "number") return (v === 0 && 1 / v < 0) ? "-0" : String(v);
  if (kind === "boolean") return String(v);
  if (Array.isArray(v)) {
    var parts = [];
    for (var j = 0; j < v.length; j++) parts.push(show(v[j]));
    var more = "";
    if (typeof v.index === "number") more = "@" + v.index + (v.groups ? show(v.groups) : "");
    return "[" + parts.join(",") + "]" + more;
  }
  if (kind === "object") {
    var keys = Object.keys(v), fields = [];
    for (var k = 0; k < keys.length; k++) fields.push(show(keys[k]) + ":" + show(v[keys[k]]));
    return "{" + fields.join(",") + "}";
  }
  return kind;
}
function run(f, s, t) {
  try { return show(f(s, t)); } catch (e) { return "throws " + (e && e.name); }
}
"""


def programs(one, two, limit=60_000):
    """JavaScript programs that between them evaluate everything. Litebird takes
    its program as an argument, so each stays well under the 128 KB a single
    argument may be."""
    head = (SHOW + "var S = [" + ", ".join(SUBJECTS) + "];\nvar T = [" + ", ".join(PARTNERS) + "];\n")
    one_tail = ("for (var i = 0; i < S.length; i++) { var out = []; for (var j = 0; j < F.length; j++) "
                "out.push(\"1 \" + i + \" \" + (BASE + j) + \" \" + run(F[j], S[i])); console.log(out.join(\"\\n\")); }\n")
    two_tail = ("for (var i = 0; i < S.length; i++) { var out = []; for (var k = 0; k < T.length; k++) "
                "for (var j = 0; j < F.length; j++) out.push(\"2 \" + i + \" \" + k + \" \" + (BASE + j) + \" \" + "
                "run(F[j], S[i], T[k])); console.log(out.join(\"\\n\")); }\n")
    result = []
    for tests, params, tail in ((one, "s", one_tail), (two, "s, t", two_tail)):
        start = 0
        while start < len(tests):
            body, count = "", 0
            while start + count < len(tests) and len(body) < limit:
                body += f"  function ({params}) {{ return {tests[start + count][1]}; }},\n"
                count += 1
            result.append(f"{head}var BASE = {start};\nvar F = [\n{body}];\n{tail}")
            start += count
    return result


def run_engine(command, program, timeout):
    """The lines one engine prints for one program, or None with a reason."""
    # The same zone and locale wherever this runs, so that the reference's
    # answers do not depend on the machine.
    env = dict(os.environ, TZ="UTC", LANG="en_US.UTF-8", LC_ALL="en_US.UTF-8")
    try:
        done = subprocess.run(command(program), capture_output=True, timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        return None, f"no answer in {timeout} s"
    if done.returncode != 0:
        return None, f"exit {done.returncode}: {done.stderr.decode('utf-8', 'replace').strip()[-300:]}"
    return done.stdout.decode("utf-8", "replace").split("\n"), ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--binary", default="build/litebird", help="the Litebird binary (default: build/litebird)")
    parser.add_argument("--reference", default="node", help="the engine to compare with (default: node)")
    parser.add_argument("--only", default="", help="only expressions whose source contains this text")
    parser.add_argument("--show", type=int, default=20, help="how many differences to print (default 20)")
    parser.add_argument("--max-differ", type=int, default=-1, help="exit 1 if more results than this differ")
    parser.add_argument("--timeout", type=int, default=120,
                        help="seconds one engine may take over one part of the grid (default 120)")
    args = parser.parse_args()

    if not shutil.which(args.reference):
        print(f"{args.reference} is not installed; there is nothing to compare with", file=sys.stderr)
        return 2
    if not os.path.exists(args.binary):
        print(f"{args.binary} does not exist; run `make` first", file=sys.stderr)
        return 2

    one, two = expressions()
    if args.only:
        one = [e for e in one if args.only in e[1]]
        two = [e for e in two if args.only in e[1]]
    total = len(one) * len(SUBJECTS) + len(two) * len(SUBJECTS) * len(PARTNERS)
    if total == 0:
        print("no expression matches", file=sys.stderr)
        return 2

    results = {"reference": {}, "litebird": {}}
    with tempfile.TemporaryDirectory() as scratch:
        for number, program in enumerate(programs(one, two)):
            path = os.path.join(scratch, f"part{number}.js")
            with open(path, "w", encoding="ascii") as out:
                out.write(program)
            for name, command in (("reference", lambda p: [args.reference, path]),
                                  ("litebird", lambda p: [args.binary, "--run-js", p])):
                lines, why = run_engine(command, program, args.timeout)
                if lines is None:
                    print(f"{name} failed on part {number}: {why}", file=sys.stderr)
                    return 1
                for line in lines:
                    fields = line.split(" ", 4 if line.startswith("2 ") else 3)
                    if len(fields) < 4:
                        continue
                    results[name][tuple(fields[:-1])] = fields[-1]

    reference, litebird = results["reference"], results["litebird"]
    if len(reference) != total:
        print(f"the reference printed {len(reference)} results, expected {total}", file=sys.stderr)
        return 1

    differing = []
    for key in sorted(reference, key=lambda k: (k[0], int(k[-1]), int(k[1]), int(k[2]) if k[0] == "2" else 0)):
        if litebird.get(key) != reference[key]:
            tests = one if key[0] == "1" else two
            differing.append((key, tests[int(key[-1])]))

    print(f"{total} results from {len(one) + len(two)} expressions over {len(SUBJECTS)} subjects: "
          f"{total - len(differing)} equal to {args.reference}'s, {len(differing)} differ")
    families = Counter(test[0] for _, test in differing)
    for family, count in families.most_common():
        print(f"  {count:5d}  {family}")
    for key, (family, source) in differing[:max(args.show, 0)]:
        subject = SUBJECTS[int(key[1])]
        partner = f", t = {PARTNERS[int(key[2])]}" if key[0] == "2" else ""
        print(f"\n{source}\n    s = {subject}{partner}\n    {args.reference}: {reference[key][:200]}\n"
              f"    litebird: {litebird.get(key, '(nothing printed)')[:200]}")
    if args.max_differ >= 0 and len(differing) > args.max_differ:
        print(f"\nmore than {args.max_differ} results differ", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
