#!/usr/bin/env python3
"""Latin mark-order and blocking probes; accepts compare.py's CLI arguments."""
import subprocess
import sys
import compare

compare.CASES = [(name, text, "Latn", "ltr") for name, text in [
    ("acute-dot", "a\u0301\u0323"),
    ("dot-acute", "a\u0323\u0301"),
    ("cedilla-acute", "a\u0327\u0301"),
    ("acute-cedilla", "a\u0301\u0327"),
    ("equal-class-block", "a\u030d\u0301"),
    ("equal-class-reversed", "a\u0301\u030d"),
    ("x-order", "x\u0301\u0323"),
    ("x-canonical", "x\u0323\u0301"),
]]

if __name__ == "__main__":
    try:
        sys.exit(compare.main())
    except (OSError, ValueError, subprocess.SubprocessError, ImportError) as exc:
        print(f"mark-order diagnostic error: {exc}", file=sys.stderr)
        sys.exit(2)
