#!/usr/bin/env python3
"""Supported Latin composites followed by marks; same CLI as compare.py."""
import subprocess
import sys
import compare

compare.CASES = [(name, text, "Latn", "ltr") for name, text in [
    ("acute-dot", "á\u0323"),
    ("acute-dot-explicit", "a\u0301\u0323"),
    ("ring-dot", "å\u0323"),
    ("ring-dot-explicit", "a\u030a\u0323"),
    ("circumflex-dot", "ê\u0323"),
    ("circumflex-dot-explicit", "e\u0302\u0323"),
    ("acute-equal-class", "á\u030d"),
    ("acute-alone", "á"),
]]

if __name__ == "__main__":
    try:
        sys.exit(compare.main())
    except (OSError, ValueError, subprocess.SubprocessError, ImportError) as exc:
        print(f"supported-composite diagnostic error: {exc}", file=sys.stderr)
        sys.exit(2)
