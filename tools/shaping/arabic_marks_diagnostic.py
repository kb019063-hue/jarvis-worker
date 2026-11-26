#!/usr/bin/env python3
"""Arabic mark and joining probes; accepts the compare.py CLI."""
import itertools
import subprocess
import sys
import compare

compare.CASES = [(name, text, "Arab", "rtl") for name, text in [
    ("beh-fatha", "بَ"),
    ("beh-kasra", "بِ"),
    ("beh-shadda-fatha", "بَّ"),
    ("beh-fatha-shadda", "بَّ"),
    ("beh-damma-shadda", "بُّ"),
    ("beh-kasra-shadda", "بِّ"),
    ("beh-two-marks", "بَُ"),
    ("joined-marks", "بَسِمُ"),
    ("lam-alef-mark", "لَا"),
    ("lam-alef-marks", "لَاِ"),
    ("zwnj-marks", "بَ\u200cبِ"),
    ("zwj-marks", "بَ\u200dبِ"),
    ("greeting", "السَّلَامُ"),
    ("alef-hamza", "أ"),
    ("alef-hamza-precomposed", "أ"),
    ("alef-fatha-hamza", "أَ"),
    ("alef-hamza-fatha", "أَ"),
    ("alef-madda", "آ"),
    ("alef-hamza-below", "إ"),
    ("beh-fatha-hamza", "بَٔ"),
    ("lam-alef-hamza", "لأ"),
    ("yeh-hamza-fatha", "ئَ"),
    ("hamza-yeh-fatha", "ئَ"),
]]

if __name__ == "__main__":
    try:
        if "--matrix" in sys.argv:
            sys.argv.remove("--matrix")
            bases = (0x0628, 0x0633, 0x0644, 0x0627, 0x0645, 0x0626)
            marks = (0x064E, 0x064F, 0x0650, 0x0651, 0x0652, 0x0653, 0x0654, 0x0655, 0x0670)
            compare.CASES = [
                (f"{base:04x}-{first:04x}-{second:04x}",
                 "".join(map(chr, (base, first, second))), "Arab", "rtl")
                for base, first, second in itertools.product(bases, marks, marks)
            ]
        elif "--ordering" in sys.argv:
            sys.argv.remove("--ordering")
            compare.CASES = [(name, text, "Arab", "rtl") for name, text in [
                ("alef-madda-hamza-below", "\u0627\u0653\u0655"),
                ("alef-hamza-above-hamza-below", "\u0627\u0654\u0655"),
                ("precomposed-madda-hamza-below", "\u0622\u0655"),
                ("precomposed-hamza-above-hamza-below", "\u0623\u0655"),
                ("precomposed-madda-fatha-hamza-below", "\u0622\u064e\u0655"),
                ("precomposed-hamza-above-kasra-hamza-below", "\u0623\u0650\u0655"),
                ("alef-madda-hamza-above-block", "\u0627\u0653\u0654"),
                ("waw-madda-fatha-hamza-above-block", "\u0648\u0653\u064e\u0654"),
                ("beh-fathatan-fatha-shadda", "\u0628\u064b\u064e\u0651"),
                ("beh-shadda-fatha-fathatan", "\u0628\u0651\u064e\u064b"),
                ("beh-fatha-kasra-shadda", "\u0628\u064e\u0650\u0651"),
                ("beh-hamza-above-fatha-shadda", "\u0628\u0654\u064e\u0651"),
                ("beh-fatha-kasra-hamza-above", "\u0628\u064e\u0650\u0654"),
                ("yeh-hamza-precomposed-madda-fatha-hamza", "\u0626\u0653\u064e\u0654"),
                ("yeh-hamza-below-fatha-hamza-above", "\u064a\u0655\u064e\u0654"),
                ("beh-shadda-fathatan-control", "\u0628\u0651\u064b"),
            ]]
        sys.exit(compare.main())
    except (OSError, ValueError, subprocess.SubprocessError, ImportError) as exc:
        print(f"Arabic-mark diagnostic error: {exc}", file=sys.stderr)
        sys.exit(2)
