#!/usr/bin/env python3
"""Check terminal cells independently of the renderer's coordinate conversion.

The C++ fixture emits tiny display lists; this script compiles it with the
specified renderer source, decodes the public ANSI stream, and compares whole
cell grids with hand-specified answers. No browser/font rasterization is used:
a terminal intentionally approximates geometry in 10x20 pixel cells.

    python3 tools/conformance/verify_terminal_clipping.py
    python3 tools/conformance/verify_terminal_clipping.py --source build/baseline/TerminalRenderer.cpp

A pre-fix renderer should fail several cases. --self-test checks that malformed
output and a changed expected cell are rejected. g++ (or $CXX) is required.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CELL = re.compile(
    rb"\x1b\[38;2;(\d+);(\d+);(\d+)m"
    rb"\x1b\[48;2;(\d+);(\d+);(\d+)m"
    rb"(\x1b\[1m)?(.)"  # one output byte, with optional bold
    rb"(\x1b\[22m)?",
    re.DOTALL,
)
WHITE = (255, 255, 255)
DEFAULT = (220, 220, 220)
BLACK = (0, 0, 0)
RED = (255, 0, 0)
GREEN = (0, 255, 0)
BLUE = (0, 0, 255)
YELLOW = (255, 255, 0)
FOCUS = (245, 158, 11)


def decode(stream: bytes, width: int, height: int):
    rows = stream.split(b"\x1b[0m\n")
    if len(rows) != height + 1 or rows[-1] != b"\x1b[0m":
        raise ValueError("wrong number of rows or missing final reset")
    result = []
    for row in rows[:-1]:
        cells = []
        pos = 0
        while pos < len(row):
            m = CELL.match(row, pos)
            if not m:
                raise ValueError(f"malformed cell at byte {pos}: {row[pos:pos + 30]!r}")
            fg = tuple(map(int, m.group(1, 2, 3)))
            bg = tuple(map(int, m.group(4, 5, 6)))
            if bool(m.group(7)) != bool(m.group(9)):
                raise ValueError("unbalanced bold sequence")
            cells.append((m.group(8).decode("ascii"), fg, bg, bool(m.group(7))))
            pos = m.end()
        if len(cells) != width:
            raise ValueError(f"row has {len(cells)} cells, expected {width}")
        result.append(cells)
    return result


def blank():
    return [[(" ", DEFAULT, WHITE, False) for _ in range(5)] for _ in range(4)]


def write(grid, x, y, ch=None, fg=None, bg=None, bold=None):
    old = grid[y][x]
    grid[y][x] = (old[0] if ch is None else ch, old[1] if fg is None else fg,
                  old[2] if bg is None else bg, old[3] if bold is None else bold)


def exact_border():
    g = blank()
    for x, ch in enumerate("+-+"):
        write(g, x, 0, ch=ch, fg=RED)
        write(g, x, 2, ch=ch, fg=BLUE)
    write(g, 0, 1, ch="|", fg=YELLOW)
    write(g, 2, 1, ch="|", fg=GREEN)
    return g


def expectations():
    tests = {name: blank() for name in [
        "border_partial", "border_offscreen", "border_encloses_viewport",
        "border_clipped", "border_sides", "border_clip_visible", "border_transparent",
        "text_clipped", "nested_text_clips", "transformed_clip", "empty_clip", "text_negative",
        "text_scrolled", "transparent_text", "shadow_offset", "shadow_clipped",
        "shadow_offscreen", "rect_clipped_control", "image_scroll_control", "focus_exact",
        "focus_partial", "focus_offscreen", "invalid_geometry",
    ]}
    tests["border_exact"] = exact_border()
    tests["border_scrolled"] = exact_border()
    g = tests["border_partial"]
    write(g, 2, 0, ch="|", fg=GREEN)
    for x, ch in enumerate("--+"):
        write(g, x, 1, ch=ch, fg=BLUE)
    g = tests["border_sides"]
    for x in range(3):
        write(g, x, 0, ch="-", fg=RED)
        write(g, x, 2, ch="-", fg=BLUE)
    g = tests["border_clip_visible"]
    for x in (1, 2):
        write(g, x, 0, ch="-", fg=RED)
        write(g, x, 2, ch="-", fg=BLUE)
    g = tests["border_transparent"]
    write(g, 0, 0, ch="|", fg=YELLOW)
    write(g, 2, 0, ch="|", fg=GREEN)
    write(g, 0, 1, ch="|", fg=YELLOW)
    write(g, 2, 1, ch="|", fg=GREEN)
    for x, ch in enumerate("+-+"):
        write(g, x, 2, ch=ch, fg=BLUE)
    g = tests["text_clipped"]
    for x, ch in ((1, "B"), (2, "C")):
        write(g, x, 1, ch=ch, fg=BLACK)
    g = tests["nested_text_clips"]
    for x, ch in ((2, "C"), (3, "D")):
        write(g, x, 1, ch=ch, fg=BLACK)
    for x, ch in enumerate("1234"):
        write(g, x, 2, ch=ch, fg=BLACK)
    g = tests["transformed_clip"]
    for x, ch in ((1, "A"), (2, "B")):
        write(g, x, 1, ch=ch, fg=BLACK)
    for x, ch in enumerate("XY"):
        write(g, x, 0, ch=ch, fg=BLACK)
    for x, ch in enumerate("OK"):
        write(tests["empty_clip"], x, 0, ch=ch, fg=BLACK)
    for name, word in (("text_negative", "BCDE"), ("text_scrolled", "CDE")):
        for x, ch in enumerate(word):
            write(tests[name], x, 0, ch=ch, fg=BLACK)
    for x, ch in enumerate("RED"):
        write(tests["transparent_text"], x, 1, ch=ch, fg=(255, 127, 127))
    # The bitmap shadow painter uses 0.6 of the specified alpha on its core.
    # The terminal samples the unblurred rectangle using that same core alpha.
    for x in (2, 3):
        write(tests["shadow_offset"], x, 1, bg=(255, 179, 179))
    write(tests["shadow_clipped"], 3, 1, bg=(255, 179, 179))
    for x in (1, 2):
        write(tests["rect_clipped_control"], x, 1, bg=RED)
    write(tests["image_scroll_control"], 0, 0, bg=GREEN)
    for x, y in ((0, 0), (1, 0), (0, 1), (1, 1)):
        write(tests["focus_exact"], x, y, fg=FOCUS, bold=True)
    for x, y in ((2, 0), (0, 1), (1, 1), (2, 1)):
        write(tests["focus_partial"], x, y, fg=FOCUS, bold=True)
    tests["empty_viewport"] = []
    return tests


def self_test():
    valid = (b"\x1b[38;2;220;220;220m\x1b[48;2;255;255;255m "
             b"\x1b[0m\n\x1b[0m")
    got = decode(valid, 1, 1)
    assert got == [[(" ", DEFAULT, WHITE, False)]]
    assert got != [[("X", DEFAULT, WHITE, False)]]
    for broken in (valid[:-1], valid.replace(b"220m", b"oops", 1), valid.replace(b"\x1b[0m\n", b"\n")):
        try:
            decode(broken, 1, 1)
        except ValueError:
            pass
        else:
            raise AssertionError("malformed ANSI was accepted")
    print("ANSI verifier self-test: 5/5 (decode, wrong cell, three malformed streams)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", default="src/render/TerminalRenderer.cpp")
    parser.add_argument("--self-test", action="store_true", help="test only the ANSI decoder")
    args = parser.parse_args()
    self_test()
    if args.self_test:
        return 0
    source = Path(args.source)
    if not source.is_absolute():
        source = ROOT / source
    with tempfile.TemporaryDirectory(prefix="litebird-terminal-") as tmp:
        executable = Path(tmp) / "fixture"
        command = shlex.split(os.environ.get("CXX", "g++")) + [
            "-std=c++20", "-O2", "-I", str(ROOT / "include"),
            str(ROOT / "tools/conformance/terminal_clipping_fixture.cpp"), str(source), "-o", str(executable),
        ]
        subprocess.run(command, check=True, cwd=ROOT)
        failures = 0
        cases = expectations()
        for name, expected in cases.items():
            proc = subprocess.run([str(executable), name], capture_output=True, timeout=10)
            if proc.returncode:
                print(f"FAIL {name}: exited {proc.returncode}: {proc.stderr.decode(errors='replace').strip()}")
                failures += 1
                continue
            try:
                actual = decode(proc.stdout, 0 if name == "empty_viewport" else 5,
                                0 if name == "empty_viewport" else 4)
            except (ValueError, UnicodeDecodeError) as exc:
                print(f"FAIL {name}: {exc}")
                failures += 1
                continue
            if actual != expected:
                failures += 1
                mismatches = [(x, y, a, expected[y][x]) for y, row in enumerate(actual)
                              for x, a in enumerate(row) if a != expected[y][x]]
                print(f"FAIL {name}: {len(mismatches)} wrong cells; first: {mismatches[0]}")
            else:
                print(f"PASS {name}")
        print(f"Terminal cell checks: {len(cases) - failures}/{len(cases)} pass")
        return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
