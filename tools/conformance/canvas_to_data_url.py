#!/usr/bin/env python3
"""`HTMLCanvasElement.toDataURL()` measured against a PNG reader that is not the engine's.

The engine writes the PNG (`ImageDecoder::encode_png`) and the base64
(`litebird::base64_encode`); asking it to read the result back would let one
bug answer the other. This script instead runs the browser binary, takes the
`data:` URL a page produced, decodes it with `render_check.read_png`'s own
reader — all five scanline filters, per-chunk CRC, no engine code — and
compares every pixel with what the page painted.

It self-tests before it is trusted: a 1x1 red PNG is written here with `zlib`,
read back, compared with the wrong colour (which must fail) and corrupted
(which must raise). Run it directly:

    python3 tools/conformance/canvas_to_data_url.py --binary build/litebird

Exit status 0 when every check agrees, 1 when one does not, 2 when the
harness's own self-test does not.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import struct
import subprocess
import sys
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import render_check  # noqa: E402  (the independent PNG reader)


# ---------------------------------------------------------------------------
# A PNG writer, for the self-test only
# ---------------------------------------------------------------------------

def png_bytes(width: int, height: int, pixels: list[tuple[int, int, int, int]]) -> bytes:
    """An 8-bit RGBA PNG with filter 0 on every row. Not used on engine output."""
    def chunk(kind: bytes, body: bytes) -> bytes:
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(pixels[y * width + x])
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


# ---------------------------------------------------------------------------
# Fetching a data: URL from a page
# ---------------------------------------------------------------------------

@dataclass
class Answer:
    name: str
    url: str


PROBE = r"""
var c = document.createElement('canvas');
c.width = 3; c.height = 2;
var g = c.getContext('2d');
g.fillStyle = '#ff0000'; g.fillRect(0, 0, 1, 1);
g.fillStyle = '#00ff00'; g.fillRect(1, 0, 1, 1);
g.fillStyle = '#0000ff'; g.fillRect(2, 1, 1, 1);
console.log('case:painted:' + c.toDataURL());

var blank = document.createElement('canvas');
blank.width = 2; blank.height = 1;
console.log('case:blank:' + blank.toDataURL());

var never = document.createElement('canvas');
console.log('case:default:' + never.toDataURL());

console.log('case:jpeg:' + c.toDataURL('image/jpeg'));
console.log('case:webp:' + c.toDataURL('image/webp'));
console.log('case:malformed:' + c.toDataURL('not a mime type'));
console.log('case:cased:' + c.toDataURL('IMAGE/PNG'));

var zero = document.createElement('canvas');
zero.width = 0; zero.height = 4;
console.log('case:zero:' + zero.toDataURL());
"""


def ask(binary: str) -> dict[str, Answer]:
    proc = subprocess.run([binary, "--run-js", PROBE], capture_output=True, text=True, timeout=300)
    if proc.returncode != 0:
        raise SystemExit(f"{binary} exited {proc.returncode} on the probe script\n{proc.stderr}")
    answers: dict[str, Answer] = {}
    for line in proc.stdout.splitlines():
        if not line.startswith("case:"):
            continue
        _, name, url = line.split(":", 2)
        answers[name] = Answer(name, url)
    missing = {"painted", "blank", "default", "jpeg", "webp", "malformed", "cased", "zero"} - answers.keys()
    if missing:
        raise SystemExit(f"the probe printed nothing for: {', '.join(sorted(missing))}\n{proc.stdout}")
    return answers


# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------

class Failures(list):
    def __init__(self) -> None:
        super().__init__()
        self.checks = 0

    def check(self, condition: bool, what: str) -> None:
        self.checks += 1
        if not condition:
            self.append(what)


def decode(url: str, failures: Failures, label: str, workdir: Path) -> render_check.Image | None:
    prefix = "data:image/png;base64,"
    if not url.startswith(prefix):
        failures.check(False, f"{label}: does not begin {prefix!r} (got {url[:40]!r})")
        return None
    try:
        raw = base64.b64decode(url[len(prefix):], validate=True)
    except (binascii.Error, ValueError) as exc:
        failures.check(False, f"{label}: the base64 does not decode: {exc}")
        return None
    path = workdir / f"{label}.png"
    path.write_bytes(raw)
    try:
        return render_check.read_png(path)
    except render_check.PngError as exc:
        failures.check(False, f"{label}: not a PNG the independent reader accepts: {exc}")
        return None


def expect_pixels(image: render_check.Image, expected: dict[tuple[int, int], tuple[int, int, int, int]],
                  failures: Failures, label: str) -> None:
    for y in range(image.height):
        for x in range(image.width):
            want = expected.get((x, y), (0, 0, 0, 0))
            got = image.at(x, y)
            failures.check(got == want, f"{label}: pixel ({x},{y}) is {got}, painted {want}")


def run(binary: str) -> int:
    answers = ask(binary)
    failures = Failures()

    with tempfile.TemporaryDirectory(prefix="canvas-data-url-") as tmp:
        workdir = Path(tmp)

        painted = decode(answers["painted"].url, failures, "painted", workdir)
        if painted is not None:
            failures.check(painted.width == 3 and painted.height == 2,
                           f"painted: image is {painted.width}x{painted.height}, canvas is 3x2")
            expect_pixels(painted, {(0, 0): (255, 0, 0, 255),
                                    (1, 0): (0, 255, 0, 255),
                                    (2, 1): (0, 0, 255, 255)}, failures, "painted")

        blank = decode(answers["blank"].url, failures, "blank", workdir)
        if blank is not None:
            failures.check(blank.width == 2 and blank.height == 1,
                           f"blank: image is {blank.width}x{blank.height}, canvas is 2x1")
            expect_pixels(blank, {}, failures, "blank")

        default = decode(answers["default"].url, failures, "default", workdir)
        if default is not None:
            failures.check(default.width == 300 and default.height == 150,
                           f"default: image is {default.width}x{default.height}, a canvas defaults to 300x150")
            expect_pixels(default, {}, failures, "default")

        # The same bitmap, a type this engine does not encode, and a string
        # that is not a MIME type: the standard's `data:,` and nothing else.
        for name, url in (("jpeg", answers["jpeg"].url), ("webp", answers["webp"].url),
                          ("malformed", answers["malformed"].url), ("zero", answers["zero"].url)):
            failures.check(url == "data:,", f"{name}: answered {url[:40]!r}, not 'data:,'")

        failures.check(answers["cased"].url == answers["painted"].url,
                       "cased: 'IMAGE/PNG' produced a different URL from 'image/png'")

        # One line a failure, then the count — the way render_check.py reports.
        for failure in failures:
            print(f"  FAIL {failure}")
        pixel_checks = sum(image.width * image.height
                           for image in (painted, blank, default) if image is not None)
        print(f"\nCanvas toDataURL: {failures.checks - len(failures)}/{failures.checks} checks agree "
              f"({pixel_checks} pixels read back by render_check.py's own PNG reader)")

    return 0 if not failures else 1


# ---------------------------------------------------------------------------
# The harness proves it can fail
# ---------------------------------------------------------------------------

def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="canvas-data-url-self-") as tmp:
        path = Path(tmp) / "red.png"
        path.write_bytes(png_bytes(1, 1, [(255, 0, 0, 255)]))

        image = render_check.read_png(path)
        assert (image.width, image.height) == (1, 1)
        assert image.at(0, 0) == (255, 0, 0, 255), "the reader did not read the writer"

        # A comparison of the right pixel with the wrong colour must fail.
        wrong = Failures()
        expect_pixels(image, {(0, 0): (0, 255, 0, 255)}, wrong, "self")
        assert wrong, "expect_pixels accepted the wrong colour"

        # A CRC the reader does not accept must be refused, not ignored.
        damaged = bytearray(path.read_bytes())
        damaged[29] ^= 0xFF                      # one byte of the IDAT body
        bad = Path(tmp) / "damaged.png"
        bad.write_bytes(damaged)
        try:
            render_check.read_png(bad)
        except render_check.PngError:
            pass
        else:
            raise AssertionError("read_png accepted a PNG whose CRC is wrong")

        # A `data:,` is not a PNG, and must be refused rather than read as one.
        empty = Path(tmp) / "empty.png"
        empty.write_bytes(b"")
        try:
            render_check.read_png(empty)
        except render_check.PngError:
            pass
        else:
            raise AssertionError("read_png accepted an empty file")

    print("Canvas toDataURL harness self-test: 5/5 (read, wrong colour refused, "
          "CRC refused, empty refused)")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="build/litebird", help="the browser binary")
    args = parser.parse_args(argv)

    try:
        self_test()
    except AssertionError as exc:
        print(f"harness self-test failed: {exc}", file=sys.stderr)
        return 2

    return run(args.binary)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
