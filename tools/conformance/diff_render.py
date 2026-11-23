#!/usr/bin/env python3
"""Render the same pages with two Litebird binaries and compare RGBA pixels.

This is a regression detector, not a reference-engine conformance test. It
compares every pixel, including alpha, and does not evaluate reftest assertions
or treat known failures specially. The PNG reader is shared with the existing
independent reftest harness, not with either engine's PNG writer.

Examples:
  python3 tools/conformance/diff_render.py --before /tmp/old/litebird --after build/litebird
  python3 tools/conformance/diff_render.py --before old --after new --suite tests/render/body-origin.json
  python3 tools/conformance/diff_render.py --before old --after new --page examples/hello.html
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile

from render_check import Image, PngError, read_png

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SUITE = ROOT / "tests/render/reftests.json"


class ComparisonError(Exception):
    """A page could not be rendered or compared reliably."""


def compare_images(before: Image, after: Image, tolerance: int = 0) -> dict:
    """Count differing pixels and return their inclusive bounding box and first change."""
    if before.width != after.width or before.height != after.height:
        raise ComparisonError(f"image dimensions differ: {before.width}x{before.height} vs "
                              f"{after.width}x{after.height}")
    if not 0 <= tolerance <= 255:
        raise ValueError("tolerance must be between 0 and 255")
    expected = before.width * before.height * 4
    if len(before.pixels) != expected or len(after.pixels) != expected:
        raise ComparisonError("image has an invalid RGBA buffer length")
    if before.pixels == after.pixels:
        return {"changed_pixels": 0, "bounds": None, "first": None}

    count = 0
    left, top, right, bottom = before.width, before.height, -1, -1
    first = None
    stride = before.width * 4
    for y in range(before.height):
        start = y * stride
        old_row = before.pixels[start:start + stride]
        new_row = after.pixels[start:start + stride]
        if old_row == new_row:
            continue
        for x in range(before.width):
            offset = x * 4
            old = old_row[offset:offset + 4]
            new = new_row[offset:offset + 4]
            if old == new or all(abs(a - b) <= tolerance for a, b in zip(old, new)):
                continue
            count += 1
            left, right = min(left, x), max(right, x)
            top, bottom = min(top, y), max(bottom, y)
            if first is None:
                first = {"x": x, "y": y, "before": list(old), "after": list(new)}
    bounds = None if not count else {"left": left, "top": top, "right": right, "bottom": bottom}
    return {"changed_pixels": count, "bounds": bounds, "first": first}


def render(binary: Path, page: Path, output: Path, width: int, height: int, timeout: float) -> Image:
    # Never accept a previous page's PNG if this renderer exits without writing.
    output.unlink(missing_ok=True)
    command = [str(binary), f"--viewport={width}x{height}", "--render-png", str(output), str(page)]
    try:
        completed = subprocess.run(command, capture_output=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired as exc:
        raise ComparisonError(f"render timed out after {timeout:g}s") from exc
    except OSError as exc:
        raise ComparisonError(f"could not start renderer: {exc}") from exc
    if completed.returncode != 0:
        stderr = completed.stderr.decode("utf-8", "replace").strip()[:300]
        raise ComparisonError(f"renderer exited {completed.returncode}" + (f": {stderr}" if stderr else ""))
    if not output.is_file():
        raise ComparisonError("renderer did not write a PNG")
    try:
        image = read_png(output)
    except (OSError, PngError, ValueError, IndexError, struct.error) as exc:
        raise ComparisonError(f"invalid PNG: {exc}") from exc
    if (image.width, image.height) != (width, height):
        raise ComparisonError(f"renderer returned {image.width}x{image.height}, expected {width}x{height}")
    return image


def positive_integer(value: str) -> int:
    try:
        number = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if number <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--before", required=True, type=Path, help="baseline executable")
    parser.add_argument("--after", required=True, type=Path, help="candidate executable")
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--suite", type=Path, help="reftest JSON (default: tests/render/reftests.json)")
    source.add_argument("--page", type=Path, help="one HTML file, retaining its real path for resources")
    parser.add_argument("--filter", default="", help="only suite test names containing this text")
    parser.add_argument("--width", type=positive_integer, default=800, help="single-page viewport width")
    parser.add_argument("--height", type=positive_integer, default=600, help="single-page viewport height")
    parser.add_argument("--timeout", type=float, default=60, help="seconds per render (default: 60)")
    parser.add_argument("--tolerance", type=int, default=0, help="maximum per-channel difference ignored (0–255)")
    parser.add_argument("--json", action="store_true", help="emit machine-readable results only")
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0 or not 0 <= args.tolerance <= 255:
        parser.error("timeout must be positive and tolerance must be between 0 and 255")
    if args.page and args.filter:
        parser.error("--filter applies only to a suite")
    before, after = args.before.resolve(), args.after.resolve()
    for binary in (before, after):
        if not binary.is_file() or not os.access(binary, os.X_OK):
            parser.error(f"not an executable file: {binary}")

    if args.page:
        page = args.page.resolve()
        if not page.is_file():
            parser.error(f"page not found: {page}")
        tests = [{"name": str(page), "path": page, "width": args.width, "height": args.height}]
    else:
        path = args.suite or DEFAULT_SUITE
        try:
            suite = json.loads(path.read_text(encoding="utf-8"))
            entries = suite["tests"]
            if not isinstance(entries, list):
                raise ValueError("tests must be a list")
            tests = []
            for entry in entries:
                if not isinstance(entry, dict) or not isinstance(entry.get("name"), str) or not isinstance(entry.get("html"), str):
                    raise ValueError("every test needs string name and html fields")
                if args.filter in entry["name"]:
                    width, height = entry.get("width", 800), entry.get("height", 600)
                    if type(width) is not int or type(height) is not int or width <= 0 or height <= 0:
                        raise ValueError(f"invalid viewport for {entry['name']}")
                    tests.append({"name": entry["name"], "html": entry["html"], "width": width, "height": height})
        except (OSError, ValueError, KeyError, TypeError) as exc:
            parser.error(f"cannot read suite {path}: {exc}")
        if not tests:
            parser.error("no suite pages matched")

    results = []
    with tempfile.TemporaryDirectory(prefix="litebird-pixel-diff-") as directory:
        work = Path(directory)
        for test in tests:
            page = test.get("path", work / "page.html")
            if "html" in test:
                page.write_text(test["html"], encoding="utf-8")
            result = {"name": test["name"], "width": test["width"], "height": test["height"]}
            try:
                for label, binary in (("before", before), ("after", after)):
                    try:
                        image = render(binary, page, work / f"{label}.png", test["width"], test["height"], args.timeout)
                    except ComparisonError as exc:
                        raise ComparisonError(f"{label}: {exc}") from exc
                    if label == "before":
                        old = image
                    else:
                        result.update(compare_images(old, image, args.tolerance))
            except ComparisonError as exc:
                result["error"] = str(exc)
            results.append(result)
            if not args.json:
                if "error" in result:
                    print(f"ERROR {result['name']}: {result['error']}")
                elif result["changed_pixels"]:
                    print(f"DIFF {result['name']}: {result['changed_pixels']} changed pixels, "
                          f"bounds {result['bounds']}, first {result['first']}")
                else:
                    print(f"SAME {result['name']}")
    different = sum(bool(r.get("changed_pixels")) for r in results)
    errors = sum("error" in r for r in results)
    summary = {"pages": len(results), "different": different, "errors": errors,
               "tolerance": args.tolerance, "results": results}
    if args.json:
        print(json.dumps(summary))
    else:
        print(f"Pixel diff: {len(results)} pages, {different} differ, {errors} errors (tolerance {args.tolerance})")
    return 1 if different or errors else 0


if __name__ == "__main__":
    sys.exit(main())
