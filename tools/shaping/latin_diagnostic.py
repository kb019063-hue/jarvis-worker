#!/usr/bin/env python3
"""Reduced Latin composition inputs; same CLI as compare.py."""
import subprocess
import sys
import compare

compare.CASES = [(name, text, "Latn", "ltr") for name, text in [
    ("base-a", "a"),
    ("precomposed-a-acute", "á"),
    ("decomposed-a-acute", "a\u0301"),
    ("x-dot-acute", "x\u0323\u0301"),
    ("original-combining", "a\u0301 x\u0323\u0301"),
]]

if __name__ == "__main__":
    try:
        sys.exit(compare.main())
    except (OSError, ValueError, subprocess.SubprocessError, ImportError) as exc:
        print(f"Latin diagnostic error: {exc}", file=sys.stderr)
        sys.exit(2)
