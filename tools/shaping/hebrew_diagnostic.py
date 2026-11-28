#!/usr/bin/env python3
"""Reduced Hebrew mark-order probes; accepts compare.py's CLI arguments."""
import sys
import itertools
import subprocess
import compare

# Canonically equivalent orders, plus controls removing each mark in turn.
# U+05B8 QAMATS has combining class 18; U+05C1 SHIN DOT has class 24.
compare.CASES = [
    ("shin", "\u05e9", "Hebr", "rtl"),
    ("shin-qamats", "\u05e9\u05b8", "Hebr", "rtl"),
    ("shin-dot", "\u05e9\u05c1", "Hebr", "rtl"),
    ("qamats-then-dot", "\u05e9\u05b8\u05c1", "Hebr", "rtl"),
    ("dot-then-qamats", "\u05e9\u05c1\u05b8", "Hebr", "rtl"),
]

if __name__ == "__main__":
    try:
        if "--permutations" in sys.argv:
            sys.argv.remove("--permutations")
            compare.CASES = [
                (f"{base:04x}-" + "-".join(f"{cp:04x}" for cp in marks),
                 "".join(map(chr, (base, *marks))), "Hebr", "rtl")
                for base in (0x05E9, 0x05D1)
                for marks in itertools.permutations((0x05B8, 0x05C1, 0x05BC))
            ]
        elif "--extended" in sys.argv:
            sys.argv.remove("--extended")
            compare.CASES = [(name, text, "Hebr", "rtl") for name, text in [
                ("shin-nonhebrew-230-220", "\u05e9\u0301\u05b8\u0323\u05c1\u05bc"),
                ("shin-holam-sheva-dagesh", "\u05e9\u05b0\u05b9\u05bc\u05c2"),
                ("bet-varika-qamats-rafe", "\u05d1\ufb1e\u05b8\u05bf"),
                ("bet-hataf-qubuts-dagesh", "\u05d1\u05bb\u05b1\u05bc"),
                ("lamed-patah-hiriq-meteg", "\u05dc\u05b7\u05b4\u05bd"),
                ("lamed-meteg-hiriq-patah", "\u05dc\u05bd\u05b4\u05b7"),
                ("lamed-qamats-sheva-meteg", "\u05dc\u05b8\u05b0\u05bd"),
                ("lamed-patah-hiriq-dotbelow", "\u05dc\u05b7\u05b4\u0323"),
                ("lamed-tsere-hiriq-meteg-noswap", "\u05dc\u05b5\u05b4\u05bd"),
                ("shin-dot-patah-hiriq-meteg", "\u05e9\u05bd\u05b4\u05b7\u05c1"),
                ("fb2a-standalone", "\ufb2a"),
                ("fb2c-standalone", "\ufb2c"),
                ("fb30-standalone", "\ufb30"),
                ("fb4f-compat-qamats", "\ufb4f\u05b8"),
                ("fb2a-qamats", "\ufb2a\u05b8"),
                ("fb2c-qamats", "\ufb2c\u05b8"),
                ("fb30-qamats", "\ufb30\u05b8"),
                ("fb1d-meteg", "\ufb1d\u05bd"),
                ("fb1f-meteg", "\ufb1f\u05bd"),
                ("fb4b-dagesh", "\ufb4b\u05bc"),
                ("yerushalayim-cgj", "\u05dc\u05b7\u034f\u05b4"),
                ("yerushalayim-rev-cgj", "\u05dc\u05b4\u034f\u05b7"),
                ("shin-qamats-cgj-dot", "\u05e9\u05b8\u034f\u05c1"),
                ("fb2a-cgj-qamats", "\ufb2a\u034f\u05b8"),
            ]]
        sys.exit(compare.main())
    except (OSError, ValueError, subprocess.SubprocessError, ImportError) as exc:
        print(f"shaping oracle error: {exc}", file=sys.stderr)
        sys.exit(2)
