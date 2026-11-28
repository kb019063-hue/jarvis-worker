#!/usr/bin/env python3
"""Render both recorded shaping runs with FreeType; not a browser screenshot test."""
import argparse
import hashlib
import importlib.metadata
import io
import json
from pathlib import Path
import sys


def rounded_ratio(n, d):
    """Nearest integer, ties away from zero (including negative origins)."""
    return (1 if n >= 0 else -1) * ((2 * abs(n) + d) // (2 * d))


def composite(canvas, x, y, width, height, pitch, coverage):
    """Source-over white coverage on transparent black; coordinates are y-down."""
    if pitch < width or len(coverage) < pitch * height:
        raise ValueError("invalid grayscale bitmap stride")
    for row in range(height):
        for col in range(width):
            alpha = coverage[row * pitch + col]
            if alpha:
                key = (x + col, y + row)
                old = canvas.get(key, 0)
                canvas[key] = alpha + ((255 - alpha) * old + 127) // 255


def difference(a, b):
    deltas = [abs(a.get(key, 0) - b.get(key, 0)) for key in a.keys() | b.keys()]
    return dict(changed_pixels=sum(v != 0 for v in deltas),
                absolute_coverage_error=sum(deltas), max_coverage_error=max(deltas, default=0))


def bounds(canvas):
    if not canvas:
        return None
    xs, ys = zip(*canvas)
    return [min(xs), min(ys), max(xs) + 1, max(ys) + 1]


def validate_run(rows, glyph_count):
    if not isinstance(rows, list) or len(rows) > 4096:
        raise ValueError("expected a glyph run of at most 4096 entries")
    for row in rows:
        if not isinstance(row, list) or len(row) not in (5, 6) or any(type(v) is not int for v in row):
            raise ValueError("expected five or six integer fields per glyph")
        if not 0 <= row[0] < glyph_count or any(abs(v) > 1_000_000 for v in row[1:5]):
            raise ValueError("glyph ID or positioning outside diagnostic limits")
        if len(row) == 6 and not 0 <= row[5] <= 1_000_000:
            raise ValueError("cluster index outside diagnostic limits")


def render(face, rows, size, ft):
    face.set_pixel_sizes(0, size)
    canvas = {}
    x = y = 0
    for gid, dx, dy, ox, oy, *_ in rows:
        # Sum in font units first. Quantize only the absolute origin to 26.6.
        px = rounded_ratio((x + ox) * size * 64, face.units_per_EM)
        py = rounded_ratio((y + oy) * size * 64, face.units_per_EM)
        ix, fx = divmod(px, 64)
        iy, fy = divmod(py, 64)
        face.set_transform(ft.Matrix(65536, 0, 0, 65536), ft.Vector(fx, fy))
        face.load_glyph(gid, ft.FT_LOAD_NO_HINTING | ft.FT_LOAD_NO_BITMAP)
        face.glyph.render(ft.FT_RENDER_MODE_NORMAL)
        bitmap = face.glyph.bitmap
        if bitmap.width and bitmap.rows:
            if bitmap.pixel_mode != ft.FT_PIXEL_MODE_GRAY or bitmap.num_grays != 256:
                raise ValueError("only 8-bit grayscale glyph masks are supported")
            if bitmap.width * bitmap.rows > 4_000_000:
                raise ValueError("glyph bitmap exceeds diagnostic limit")
            composite(canvas, ix + face.glyph.bitmap_left, -iy - face.glyph.bitmap_top,
                      bitmap.width, bitmap.rows, bitmap.pitch, bitmap.buffer)
        x += dx
        y += dy
    return canvas, [x, y]


def measure(report, data, sizes, ft):
    if hashlib.sha256(data).hexdigest() != report.get("sha256"):
        raise ValueError("font SHA-256 differs from the shaping report")
    # Use the checked bytes, not a second read of a mutable font path.
    face = ft.Face(io.BytesIO(data))
    if face.units_per_EM != report.get("units_per_em"):
        raise ValueError("units per em differ from the shaping report")
    results = report.get("results")
    if not isinstance(results, list) or not results or len(results) > 1000:
        raise ValueError("expected 1..1000 shaping cases")
    output = []
    for case in results:
        expected, actual = case["harfbuzz"], case["litebird"]
        validate_run(expected, face.num_glyphs)
        validate_run(actual, face.num_glyphs)
        missing = any(row[0] == 0 for row in expected + actual)
        for size in sizes:
            ref, ref_pen = render(face, expected, size, ft)
            got, got_pen = render(face, actual, size, ft)
            delta = difference(ref, got)
            status = ("unsupported" if missing else "same-ink-and-advance"
                      if delta["changed_pixels"] == 0 and ref_pen == got_pen else "different")
            output.append(dict(name=case["name"], size_px=size, status=status,
                               harfbuzz_bounds=bounds(ref), litebird_bounds=bounds(got),
                               harfbuzz_terminal_pen=ref_pen, litebird_terminal_pen=got_pen,
                               **delta))
    return dict(sha256=report["sha256"], units_per_em=face.units_per_EM,
                source_report_sha256=hashlib.sha256(json.dumps(report, sort_keys=True).encode()).hexdigest(),
                source_uharfbuzz=report.get("uharfbuzz"), source_harfbuzz=report.get("harfbuzz"),
                freetype=".".join(map(str, ft.version())),
                freetype_py=importlib.metadata.version("freetype-py"),
                policy="unhinted grayscale; absolute origins rounded to 26.6; source-over; no clipping",
                results=output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--font", required=True, type=Path)
    parser.add_argument("--sizes", nargs="+", type=int, default=[16, 32, 64])
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if len(args.sizes) > 16 or any(not 1 <= s <= 256 for s in args.sizes):
        parser.error("use at most 16 sizes, each 1..256 pixels")
    import freetype as ft
    try:
        result = measure(json.loads(args.report.read_text()), args.font.read_bytes(), args.sizes, ft)
    except ft.FT_Exception as exc:
        raise ValueError(f"FreeType rejected input: {exc}") from exc
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    for case in result["results"]:
        print(f"{case['name']} @{case['size_px']}: {case['status']}; "
              f"{case['changed_pixels']} changed pixels")
    return int(any(c["status"] != "same-ink-and-advance" for c in result["results"]))


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, TypeError, ImportError) as exc:
        print(f"ink diagnostic error: {exc}", file=sys.stderr)
        sys.exit(2)
