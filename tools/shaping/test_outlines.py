"""Contour-comparator controls; optional actual-font integration tests."""
from collections import Counter
from pathlib import Path
import sys
import unittest
import outlines as out


class ContourTests(unittest.TestCase):
    def setUp(self):
        self.points = [(0, 0, 1), (9, 0, 1), (9, 7, 0), (0, 7, 1)]

    def test_start_rotation_only(self):
        for i in range(4):
            self.assertEqual(out.canonical(self.points), out.canonical(self.points[i:] + self.points[:i]))
        self.assertNotEqual(out.canonical(self.points), out.canonical(self.points[::-1]))

    def test_coordinate_and_tag_mutations(self):
        ref = out.canonical(self.points)
        for field in range(3):
            changed = [list(p) for p in self.points]
            changed[0][field] += 1
            self.assertNotEqual(ref, out.canonical(changed))

    def test_multiplicity_and_shape_translation(self):
        a = (0, out.canonical(self.points))
        b = (0, out.canonical([(x + 10, y - 5, t) for x, y, t in self.points]))
        self.assertNotEqual(a, b)
        self.assertEqual(out.describe(a, 1)["relative_shape_sha256"],
                         out.describe(b, 1)["relative_shape_sha256"])
        self.assertEqual(Counter([a, a]) - Counter([a]), Counter([a]))
        self.assertNotEqual(out.describe(a, 1)["relative_shape_sha256"],
                            out.describe((2, a[1]), 1)["relative_shape_sha256"])

    def test_degenerate_not_discarded(self):
        self.assertEqual(out.canonical([(1, 2, 1)]), ((1, 2, 1),))
        with self.assertRaises(ValueError):
            out.canonical([])


def integration(path):
    import freetype as ft
    data = Path(path).read_bytes()
    face = ft.Face(path)
    gid = face.get_char_index(ord('a'))
    if not gid:
        raise ValueError("font must contain a")
    ref = [gid, 600, 0, 0, 0]
    cases = [dict(name=name, harfbuzz=[ref], litebird=rows) for name, rows in [
        ("identity", [ref]), ("shift", [[gid, 600, 0, 1, 0]]),
        ("advance", [[gid, 601, 0, 0, 0]]), ("missing", []),
        ("duplicate", [ref, [gid, 0, 0, -600, 0]]),
        ("notdef", [[0, 600, 0, 0, 0]])]]
    report = dict(sha256=out.digest_bytes(data), units_per_em=face.units_per_EM, results=cases)
    result = out.measure(report, data, ft)["results"]
    assert [r["status"] for r in result] == ["same-contours-and-advance"] + ["different"] * 4 + ["unsupported"]
    assert not result[2]["harfbuzz_only"] and not result[2]["litebird_only"]
    report["sha256"] = "wrong"
    try:
        out.measure(report, data, ft)
    except ValueError:
        pass
    else:
        raise AssertionError("font mismatch accepted")
    print("Outline integration controls passed")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--font":
        integration(sys.argv[2])
    else:
        unittest.main()
