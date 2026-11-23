#!/usr/bin/env python3
"""Reduced Hebrew mark-order probes; accepts compare.py's CLI arguments."""
import sys
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
        sys.exit(compare.main())
    except (OSError, ValueError, subprocess.SubprocessError, ImportError) as exc:
        print(f"shaping oracle error: {exc}", file=sys.stderr)
        sys.exit(2)
