#!/usr/bin/env python3
"""Compare placed FreeType outlines in font units, without pixel scaling."""
import argparse
from collections import Counter
import hashlib
import importlib.metadata
import io
import json
from pathlib import Path
import sys
from ink import validate_run


def canonical(points):
    """Ignore only contour start; retain winding, point tags and multiplicity."""
    points = tuple(tuple(p) for p in points)
    if not points:
        raise ValueError("empty contour")
    lowest = min(points)
    return min(points[i:] + points[:i] for i, p in enumerate(points) if p == lowest)


def digest(value):
    return hashlib.sha256(json.dumps(value, separators=(",", ":")).encode()).hexdigest()


def extract(face, rows, ft):
    validate_run(rows, face.num_glyphs)
    contours = []
    x = y = 0
    face.set_transform(ft.Matrix(65536, 0, 0, 65536), ft.Vector(0, 0))
    for gid, dx, dy, ox, oy in rows:
        face.load_glyph(gid, ft.FT_LOAD_NO_SCALE | ft.FT_LOAD_NO_HINTING | ft.FT_LOAD_NO_BITMAP)
        if face.glyph.format != ft.FT_GLYPH_FORMAT_OUTLINE:
            raise ValueError("only outline glyphs supported")
        outline = face.glyph.outline
        start = 0
        for end in outline.contours:
            points = [(px + x + ox, py + y + oy, tag & 3)
                      for (px, py), tag in zip(outline.points[start:end + 1], outline.tags[start:end + 1])]
            # Fill semantics preserved; memory ownership/raster quality flags ignored.
            contours.append((outline.flags & 6, canonical(points)))
            start = end + 1
        x += dx
        y += dy
    return Counter(contours), [x, y]


def describe(contour, count):
    flags, points = contour
    min_x = min(p[0] for p in points)
    min_y = min(p[1] for p in points)
    relative = canonical([(x - min_x, y - min_y, tag) for x, y, tag in points])
    return dict(count=count, fill_flags=flags, points=points,
                bounds=[min_x, min_y, max(p[0] for p in points), max(p[1] for p in points)],
                relative_shape_sha256=digest([flags, relative]))


def measure(report, data, ft):
    if digest_bytes(data) != report.get("sha256"):
        raise ValueError("font SHA-256 differs from source report")
    face = ft.Face(io.BytesIO(data))
    if face.units_per_EM != report.get("units_per_em"):
        raise ValueError("units per em differ from source report")
    cases = report.get("results")
    if not isinstance(cases, list) or not 1 <= len(cases) <= 1000:
        raise ValueError("expected 1..1000 cases")
    results = []
    for case in cases:
        expected, actual = case["harfbuzz"], case["litebird"]
        ref, ref_pen = extract(face, expected, ft)
        got, got_pen = extract(face, actual, ft)
        unsupported = any(row[0] == 0 for row in expected + actual)
        status = ("unsupported" if unsupported else "same-contours-and-advance"
                  if ref == got and ref_pen == got_pen else "different")
        results.append(dict(name=case["name"], status=status,
                            harfbuzz_terminal_pen=ref_pen, litebird_terminal_pen=got_pen,
                            harfbuzz_only=[describe(c, n) for c, n in (ref - got).items()],
                            litebird_only=[describe(c, n) for c, n in (got - ref).items()]))
    return dict(sha256=report["sha256"], source_report_sha256=digest(report),
                units_per_em=face.units_per_EM, freetype=".".join(map(str, ft.version())),
                freetype_py=importlib.metadata.version("freetype-py"),
                source_harfbuzz=report.get("harfbuzz"), source_uharfbuzz=report.get("uharfbuzz"),
                policy="unscaled outline contour multiset; cyclic starts ignored; winding/tags retained",
                results=results)


def digest_bytes(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    import freetype as ft
    try:
        result = measure(json.loads(args.report.read_text()), args.font.read_bytes(), ft)
    except ft.FT_Exception as exc:
        raise ValueError(str(exc)) from exc
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    for case in result["results"]:
        print(f"{case['name']}: {case['status']}; "
              f"unmatched contours {len(case['harfbuzz_only'])}/{len(case['litebird_only'])}")
    return int(any(c["status"] != "same-contours-and-advance" for c in result["results"]))


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, ImportError, KeyError, TypeError) as exc:
        print(f"outline diagnostic error: {exc}", file=sys.stderr)
        sys.exit(2)
