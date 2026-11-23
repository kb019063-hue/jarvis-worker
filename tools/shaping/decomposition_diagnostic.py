#!/usr/bin/env python3
"""Missing-composite cases and explicit-part controls; same CLI as compare.py."""
import subprocess
import sys
import compare

compare.CASES = [(name, text, "Latn", "ltr") for name, text in [
    ("plain-a", "a"),
    ("missing-a-acute", "á"),
    ("a-acute-parts", "a\u0301"),
    ("missing-a-ring", "å"),
    ("a-ring-parts", "a\u030a"),
    ("missing-a-ring-acute", "ǻ"),
    ("a-ring-acute-parts", "a\u030a\u0301"),
]]

if __name__ == "__main__":
    try:
        sys.exit(compare.main())
    except (OSError, ValueError, subprocess.SubprocessError, ImportError) as exc:
        print(f"decomposition diagnostic error: {exc}", file=sys.stderr)
        sys.exit(2)
