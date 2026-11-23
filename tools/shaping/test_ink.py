"""Dependency-free comparator tests; optional FreeType integration via --font."""
import hashlib
import json
from pathlib import Path
import sys
import unittest
import ink


class InkTests(unittest.TestCase):
    def test_rounding(self):
        self.assertEqual([ink.rounded_ratio(n, 2) for n in [-3, -1, 0, 1, 3]], [-2, -1, 0, 1, 2])

    def test_stride_coordinates_and_overlap(self):
        canvas = {}
        ink.composite(canvas, -2, -3, 1, 2, 2, [128, 255, 64, 255])
        self.assertEqual(canvas, {(-2, -3): 128, (-2, -2): 64})
        ink.composite(canvas, -2, -3, 1, 1, 1, [128])
        self.assertEqual(canvas[(-2, -3)], 192)
        self.assertEqual(ink.bounds(canvas), [-2, -3, -1, -1])

    def test_negative_controls(self):
        ref = {(0, 0): 100}
        self.assertEqual(ink.difference(ref, ref)["changed_pixels"], 0)
        for changed in [{}, {(1, 0): 100}, {(0, 1): 100}, {(0, 0): 101}]:
            self.assertGreater(ink.difference(ref, changed)["changed_pixels"], 0)
        self.assertEqual(ink.difference({}, {})["absolute_coverage_error"], 0)

    def test_invalid_runs(self):
        for row in [[[2, 0, 0, 0]], [[999, 0, 0, 0, 0]], [[1, True, 0, 0, 0]]]:
            with self.assertRaises(ValueError):
                ink.validate_run(row, 10)
        with self.assertRaises(ValueError):
            ink.composite({}, 0, 0, 2, 2, 1, [255] * 4)


def integration(font_path):
    import freetype as ft
    data = Path(font_path).read_bytes()
    face = ft.Face(font_path)
    gid = face.get_char_index(ord('a'))
    if gid == 0:
        raise ValueError("integration font must contain a")
    row = [gid, face.units_per_EM, 0, 0, 0]
    cases = []
    for name, actual in [
        ("identical", [row[:]]), ("missing", []),
        ("shifted", [[gid, row[1], 0, face.units_per_EM // 2, 0]]),
        ("advance-only", [[gid, row[1] + 1, 0, 0, 0]]),
        ("notdef", [[0, row[1], 0, 0, 0]])]:
        cases.append(dict(name=name, harfbuzz=[row[:]], litebird=actual))
    report = dict(sha256=hashlib.sha256(data).hexdigest(), units_per_em=face.units_per_EM, results=cases)
    result = ink.measure(report, data, [32], ft)["results"]
    assert [r["status"] for r in result] == ["same-ink-and-advance", "different", "different", "different", "unsupported"]
    assert result[3]["changed_pixels"] == 0  # Ink alone must not hide width errors.
    report["sha256"] = "wrong"
    try:
        ink.measure(report, data, [32], ft)
    except ValueError:
        pass
    else:
        raise AssertionError("font mismatch not rejected")
    print("FreeType integration controls passed")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--font":
        integration(sys.argv[2])
    else:
        unittest.main()
