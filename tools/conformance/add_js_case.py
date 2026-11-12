#!/usr/bin/env python3
"""Appends a case to a JavaScript corpus file, with the expectation taken from Node.

The corpus rule is that an `#expect` section is what a real engine printed,
never what Litebird printed. This does the three steps in one, so that there is
no moment at which the output could be typed in by hand:

    python3 tools/conformance/add_js_case.py tests/js-conformance/strings.dat "case name" script.js
    python3 tools/conformance/add_js_case.py tests/js-conformance/strings.dat "case name" - < script.js

It runs the script with `node -e` under TZ=UTC (as the runner does), refuses a
script that fails or prints nothing, refuses a name the file already has, and
appends the case. Check the file afterwards with:

    python3 tools/conformance/run_js_conformance.py --reference "node -e" --filter <file>
"""
import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    path, name, source = Path(sys.argv[1]), sys.argv[2].strip(), sys.argv[3]
    code = (sys.stdin.read() if source == "-" else Path(source).read_text(encoding="utf-8")).strip("\n")
    if not name or "\n" in name:
        print("the case needs a one-line name", file=sys.stderr)
        return 2
    if any(line in ("#code", "#expect") or line.startswith("#name ") for line in code.split("\n")):
        print("the script contains a line the corpus format reserves (#name, #code, #expect)", file=sys.stderr)
        return 2

    existing = path.read_text(encoding="utf-8") if path.exists() else ""
    if any(line == f"#name {name}" for line in existing.split("\n")):
        print(f"{path.name} already has a case named {name!r}", file=sys.stderr)
        return 1

    env = dict(os.environ, TZ="UTC")
    try:
        result = subprocess.run(["node", "-e", code], capture_output=True, text=True, env=env, timeout=60)
    except FileNotFoundError:
        print("node is not installed; expectations have to come from it", file=sys.stderr)
        return 1
    except subprocess.TimeoutExpired:
        print("node did not finish the script in 60 s", file=sys.stderr)
        return 1
    if result.returncode != 0:
        print(f"node exited {result.returncode}:\n{result.stderr.strip()[-600:]}", file=sys.stderr)
        return 1
    expect = result.stdout.rstrip("\n")
    if not expect:
        print("the script printed nothing; a case has to print what it checks", file=sys.stderr)
        return 1

    separator = "" if not existing else ("\n" if existing.endswith("\n") else "\n\n")
    with path.open("a", encoding="utf-8") as out:
        out.write(f"{separator}#name {name}\n#code\n{code}\n#expect\n{expect}\n")
    print(f"added to {path.name}: {name}\n#expect\n{expect}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
