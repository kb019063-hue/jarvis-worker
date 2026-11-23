#!/usr/bin/env python3
"""Negative controls for the two-build pixel comparison; no engine required."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from diff_render import ComparisonError, compare_images
from render_check import Image

SCRIPT = Path(__file__).with_name("diff_render.py")


def image(width, height, changes=()):
    data = bytearray([0, 0, 0, 255] * (width * height))
    for x, y, rgba in changes:
        i = (y * width + x) * 4
        data[i:i + 4] = bytes(rgba)
    return Image(width, height, bytes(data))


class PixelComparison(unittest.TestCase):
    def test_identical(self):
        self.assertEqual(compare_images(image(2, 2), image(2, 2)),
                         {"changed_pixels": 0, "bounds": None, "first": None})

    def test_count_bounds_first_and_alpha(self):
        a = image(3, 3)
        b = image(3, 3, [(2, 0, (0, 0, 0, 254)), (0, 2, (5, 0, 0, 255))])
        result = compare_images(a, b)
        self.assertEqual(result["changed_pixels"], 2)
        self.assertEqual(result["bounds"], {"left": 0, "top": 0, "right": 2, "bottom": 2})
        self.assertEqual(result["first"], {"x": 2, "y": 0, "before": [0, 0, 0, 255],
                                            "after": [0, 0, 0, 254]})

    def test_tolerance_is_per_channel_not_sum(self):
        a = image(2, 1)
        b = image(2, 1, [(0, 0, (2, 2, 2, 253)), (1, 0, (3, 0, 0, 255))])
        result = compare_images(a, b, 2)
        self.assertEqual(result["changed_pixels"], 1)
        self.assertEqual(result["first"]["x"], 1)
        self.assertEqual(compare_images(a, b, 3)["changed_pixels"], 0)

    def test_dimensions_and_invalid_buffer(self):
        with self.assertRaises(ComparisonError):
            compare_images(image(2, 1), image(1, 2))
        with self.assertRaises(ComparisonError):
            compare_images(Image(1, 1, b"bad"), image(1, 1))
        with self.assertRaises(ValueError):
            compare_images(image(1, 1), image(1, 1), -1)


# The fake is a separate executable: it exercises argument ordering, output
# files, exit codes and decoding without borrowing the engine's PNG writer.
FAKE = r'''#!/usr/bin/env python3
import pathlib, struct, sys, time, zlib
mode = MODE
args = sys.argv[1:]
width, height = map(int, next(x for x in args if x.startswith('--viewport=')).split('=')[1].split('x'))
out = pathlib.Path(args[args.index('--render-png') + 1])
page = pathlib.Path(args[-1])
if not page.is_file(): sys.exit(13)
if mode == 'exit': print('intentional error', file=sys.stderr); sys.exit(7)
if mode == 'timeout': time.sleep(2); sys.exit(0)
if mode == 'missing' or (mode == 'stale' and 'SECOND' in page.read_text()): sys.exit(0)
if mode == 'corrupt': out.write_bytes(b'not a png'); sys.exit(0)
if mode == 'wrongsize': width += 1
pixels = bytearray([0, 0, 0, 255] * width * height)
if mode == 'onepixel': pixels[(width + 1) * 4] = 9
if mode == 'alpha': pixels[(width + 1) * 4 + 3] = 254
raw = b''.join(b'\0' + pixels[y*width*4:(y+1)*width*4] for y in range(height))
def chunk(kind, body):
    return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
out.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB',width,height,8,6,0,0,0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
'''


class CommandLine(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.suite = self.directory / "suite.json"
        self.suite.write_text(json.dumps({"tests": [
            {"name": "one", "html": "FIRST", "width": 3, "height": 2},
            {"name": "two", "html": "SECOND", "width": 3, "height": 2, "known_failure": "still compared"},
        ]}))

    def renderer(self, name, mode):
        path = self.directory / name
        path.write_text(FAKE.replace("mode = MODE", "mode = " + repr(mode)))
        path.chmod(0o755)
        return path

    def run_diff(self, before, after, *extra):
        return subprocess.run([sys.executable, str(SCRIPT), "--before", str(before), "--after", str(after),
                               "--suite", str(self.suite), "--json", *extra], capture_output=True, text=True)

    def test_same_binary_and_known_failure_is_compared(self):
        binary = self.renderer("same", "same")
        result = self.run_diff(binary, binary)
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        self.assertEqual((data["pages"], data["different"], data["errors"]), (2, 0, 0))
        self.assertEqual([r["name"] for r in data["results"]], ["one", "two"])

    def test_one_pixel_and_alpha_fail(self):
        before = self.renderer("before", "same")
        for mode in ("onepixel", "alpha"):
            with self.subTest(mode=mode):
                after = self.renderer("after", mode)
                result = self.run_diff(before, after, "--filter", "one")
                self.assertEqual(result.returncode, 1, result.stderr)
                data = json.loads(result.stdout)
                self.assertEqual((data["pages"], data["different"], data["errors"]), (1, 1, 0))
                self.assertEqual(data["results"][0]["changed_pixels"], 1)
                self.assertEqual(data["results"][0]["bounds"], {"left": 1, "top": 1, "right": 1, "bottom": 1})

    def test_tolerance(self):
        before = self.renderer("before", "same")
        after = self.renderer("after", "alpha")
        result = self.run_diff(before, after, "--tolerance", "1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["different"], 0)

    def test_errors_are_not_counted_as_equal(self):
        before = self.renderer("before", "same")
        for mode, text in (("exit", "exited 7"), ("missing", "did not write"),
                           ("corrupt", "invalid PNG"), ("wrongsize", "expected 3x2"),
                           ("timeout", "timed out")):
            with self.subTest(mode=mode):
                after = self.renderer("after", mode)
                timeout = "0.3" if mode == "timeout" else "5"
                result = self.run_diff(before, after, "--filter", "one", "--timeout", timeout)
                self.assertEqual(result.returncode, 1, result.stderr)
                entry = json.loads(result.stdout)["results"][0]
                self.assertIn("after:", entry["error"])
                self.assertIn(text, entry["error"])

    def test_stale_output_cannot_pass_the_next_page(self):
        before = self.renderer("before", "same")
        after = self.renderer("after", "stale")
        result = self.run_diff(before, after)
        self.assertEqual(result.returncode, 1, result.stderr)
        data = json.loads(result.stdout)
        self.assertEqual(data["errors"], 1)
        self.assertIn("did not write", data["results"][1]["error"])

    def test_no_matches_and_invalid_options_are_errors(self):
        binary = self.renderer("same", "same")
        for flags in (("--filter", "not-there"), ("--tolerance", "256"), ("--timeout", "nan")):
            with self.subTest(flags=flags):
                result = self.run_diff(binary, binary, *flags)
                self.assertEqual(result.returncode, 2)

    def test_single_page_uses_its_own_path_and_viewport(self):
        binary = self.renderer("same", "same")
        page = self.directory / "example.html"
        page.write_text("test")
        result = subprocess.run([sys.executable, str(SCRIPT), "--before", str(binary), "--after", str(binary),
                                 "--page", str(page), "--width", "3", "--height", "2", "--json"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        entry = json.loads(result.stdout)["results"][0]
        self.assertEqual((entry["name"], entry["width"], entry["height"]), (str(page), 3, 2))


if __name__ == "__main__":
    unittest.main(verbosity=2)
