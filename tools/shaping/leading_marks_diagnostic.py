#!/usr/bin/env python3
"""Leading combining mark probes; accepts compare.py CLI arguments."""
import subprocess
import sys
import compare

compare.CASES = [(name, text, "Latn", "ltr") for name, text in [
    ("acute-dot", "\u0301\u0323"),
    ("dot-acute", "\u0323\u0301"),
    ("leading-before-a", "\u0301\u0323a"),
    ("equal-classes", "\u030d\u0301"),
    ("three-classes", "\u0301\u0323\u0327"),
    ("after-space", " \u0301\u0323"),
    ("after-zwnj", "\u200c\u0301\u0323"),
    ("explicit-circle", "\u25cc\u0301\u0323"),
]]

if __name__ == "__main__":
    try:
        sys.exit(compare.main())
    except (OSError, ValueError, subprocess.SubprocessError, ImportError) as exc:
        print(f"leading-marks diagnostic error: {exc}", file=sys.stderr)
        sys.exit(2)
