#!/usr/bin/env python3
"""Times the HTML tokenizer's repeated-attribute-name check, with correctness checks.

The tokenizer drops a repeated attribute name by comparing it with every
attribute it has already accepted. That is the square of the attribute count:
one element with 50,000 attributes spent about 2.9 s in the parser, 25,000
about 0.5 s (AUDIT finding 28). Each case here is one page, parsed with
`--dump-dom-webplatform` (parse and tree dump, no layout, no scripts). The
whole dump is compared with the tree the page must produce, computed here
from the markup rather than recorded from a binary: first attribute wins,
names ASCII-lowercased, order of first occurrence kept.

Usage:
    python3 tools/bench/bench_tokenizer_attributes.py
    python3 tools/bench/bench_tokenizer_attributes.py --only distinct --reps 3
    python3 tools/bench/bench_tokenizer_attributes.py --max-ms 1200
    python3 tools/bench/bench_tokenizer_attributes.py --baseline build/litebird-before
    python3 tools/bench/bench_tokenizer_attributes.py --size 100000

Times are whole-process times (CLI start-up and the dump included) and the
best of --reps is reported. The tree dump sorts attributes by name, so this
checks names, values and which duplicate survived; the tokenizer's order is
checked by tests/test_suite.cpp. Run it on an idle machine: another build or
a fuzz run on the same two cores inflates every number.
"""
import argparse
import hashlib
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def digest(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def dump(elements):
    """The tree the parser must produce: html/head/body around `elements`.

    Each element is (tag, attributes, text); attributes is the list of
    (name, value) pairs that survive, written sorted by name as the WPT tree
    format has them.
    """
    lines = ["| <!DOCTYPE html>", "| <html>", "|   <head>", "|   <body>"]
    for tag, attributes, text in elements:
        lines.append(f"|     <{tag}>")
        for name, value in sorted(attributes):
            lines.append(f'|       {name}="{value}"')
        if text:
            lines.append(f'|       "{text}"')
    return "\n".join(lines) + "\n"


def first_wins(pairs):
    """What the tokenizer keeps: the first pair of each lowercased name."""
    kept = {}
    for name, value in pairs:
        kept.setdefault(name.lower(), value)
    return list(kept.items())


def cases(size):
    """(name, page markup, expected tree dump) for each shape of tag."""
    out = []

    distinct = [(f"a{i}", f"v{i}") for i in range(size)]
    out.append(("distinct names",
                "<div " + " ".join(f'{n}="{v}"' for n, v in distinct) + ">x</div>",
                dump([("div", distinct, "x")])))

    twice = [(f"a{i // 2}", f"v{i // 2}" if i % 2 == 0 else f"dup{i}") for i in range(size)]
    out.append(("each name twice",
                "<div " + " ".join(f'{n}="{v}"' for n, v in twice) + ">x</div>",
                dump([("div", first_wins(twice), "x")])))

    folded = ([(f"a{i}", f"v{i}") for i in range((size + 1) // 2)] +
              [(f"A{i}", f"dup{i}") for i in range(size // 2)])
    out.append(("uppercase repeats",
                "<div " + " ".join(f'{n}="{v}"' for n, v in folded) + ">x</div>",
                dump([("div", first_wins(folded), "x")])))

    # Names past the small-string buffer of std::string: an index that points
    # at the accepted names has to survive the attribute vector growing.
    long_pairs = []
    for i in range(size):
        name = f"attribute-{i // 2:08d}-" + "n" * 48
        long_pairs.append((name, f"v{i // 2}" if i % 2 == 0 else f"dup{i}"))
    out.append(("long names, each twice",
                "<div " + " ".join(f'{n}="{v}"' for n, v in long_pairs) + ">x</div>",
                dump([("div", first_wins(long_pairs), "x")])))

    # One name in all four spellings: quoted, valueless, single-quoted and
    # unquoted. The first one wins, so the later empty value must not win.
    forms = []
    for i in range(size):
        name = f"a{i // 4}"
        if i % 4 == 0:
            forms.append((name, f'{name}="v{i // 4}"'))
        elif i % 4 == 1:
            forms.append((name, name))
        elif i % 4 == 2:
            forms.append((name, f"{name}='later{i}'"))
        else:
            forms.append((name, f"{name}=later{i}"))
    out.append(("quoted, bare, unquoted",
                "<div " + " ".join(text for _, text in forms) + ">x</div>",
                dump([("div", [(f"a{i}", f"v{i}") for i in range((size + 3) // 4)], "x")])))

    # The common shape: thousands of ordinary tags with a handful of
    # attributes each. A per-tag index must not make this slower.
    small = [("div", [("a0", "0"), ("a1", "1"), ("a2", "2")], "")] * max(1, size // 4)
    out.append(("many small tags",
                "".join('<div a0="0" a1="1" a2="2" a0="dup"></div>' for _ in small),
                dump(small)))

    # An end tag's attributes are parsed and then dropped by the tree
    # builder; the tokenizer still pays for the duplicate check.
    out.append(("end tag with attributes",
                "<div>x</div " + " ".join(f'{n}="{v}"' for n, v in distinct) + ">",
                dump([("div", [], "x")])))

    return [(name, "<!DOCTYPE html><body>" + markup, expected) for name, markup, expected in out]


def run(binary, page, timeout):
    start = time.perf_counter()
    try:
        proc = subprocess.run([binary, "--dump-dom-webplatform", str(page)],
                              capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "", f"timeout after {timeout:g}s"
    elapsed = time.perf_counter() - start
    if proc.returncode != 0:
        return elapsed, proc.stdout, f"exit {proc.returncode}: {(proc.stderr or proc.stdout).strip()[:120]}"
    return elapsed, proc.stdout, ""


def best_of(binary, page, timeout, reps):
    best = None
    out = ""
    for _ in range(reps):
        elapsed, out, error = run(binary, page, timeout)
        if error:
            return None, out, error
        best = elapsed if best is None else min(best, elapsed)
    return best, out, ""


def first_difference(expected, got):
    want = expected.split("\n")
    have = got.split("\n")
    for i in range(max(len(want), len(have))):
        a = want[i] if i < len(want) else "<end of expected tree>"
        b = have[i] if i < len(have) else "<end of dump>"
        if a != b:
            return f"line {i + 1}: expected {a[:80]!r}, got {b[:80]!r}"
    return "same lines, different bytes"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default="build/litebird")
    ap.add_argument("--baseline", default="", help="optional older Litebird binary to compare")
    ap.add_argument("--only", default="", help="run cases whose name contains this")
    ap.add_argument("--size", type=int, default=50000, help="attribute occurrences per large case (default 50000)")
    ap.add_argument("--timeout", type=float, default=60.0, help="seconds per run (default 60)")
    ap.add_argument("--reps", type=int, default=1, help="repetitions; best time is reported")
    ap.add_argument("--max-ms", type=float, default=0.0, help="fail when a case exceeds this whole-process time")
    args = ap.parse_args()
    if args.size < 4 or args.reps < 1 or args.timeout <= 0:
        ap.error("--size must be >=4, --reps >=1, --timeout >0")
    for binary in [args.binary] + ([args.baseline] if args.baseline else []):
        if not Path(binary).is_file():
            ap.error(f"binary not found: {binary}")
    selected = [case for case in cases(args.size) if args.only in case[0]]
    if not selected:
        ap.error("no cases match --only")

    failures = 0
    print(f"{'case':26} {'size':>7} {'current':>10} {'baseline':>10} {'speedup':>8}  result")
    with tempfile.TemporaryDirectory(prefix="litebird-tokenizer-") as directory:
        page = Path(directory) / "page.html"
        for name, markup, expected in selected:
            page.write_text(markup, encoding="utf-8", newline="")
            elapsed, out, error = best_of(args.binary, page, args.timeout, args.reps)
            note = error
            if not note and out != expected:
                note = "WRONG TREE: " + first_difference(expected, out)
            if not note and args.max_ms and elapsed * 1000 > args.max_ms:
                note = f"TOO SLOW (limit {args.max_ms:g}ms)"
            baseline_time = ""
            speedup = ""
            if args.baseline:
                old_elapsed, old_out, old_error = best_of(args.baseline, page, args.timeout, 1)
                if old_error:
                    baseline_time = "failed"
                    if not note:
                        note = f"baseline: {old_error}"
                else:
                    baseline_time = f"{old_elapsed * 1000:8.0f}ms"
                    if old_out != expected:
                        if not note:
                            note = "BASELINE WRONG TREE: " + first_difference(expected, old_out)
                    elif elapsed and elapsed > 0:
                        speedup = f"{old_elapsed / elapsed:7.1f}x"
            if note:
                failures += 1
            shown = f"{elapsed * 1000:8.0f}ms" if elapsed is not None else "failed"
            result = note or f"tree ok, sha256 {digest(out)[:12]}"
            print(f"{name:26} {args.size:7} {shown:>10} {baseline_time:>10} {speedup:>8}  {result}", flush=True)
    print(f"\n{len(selected)} case(s); {failures} failure(s). Every dump compared with the tree the markup must produce"
          + (" and with the baseline binary." if args.baseline else "."))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
