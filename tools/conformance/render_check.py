#!/usr/bin/env python3
"""Compare rendered pages with what they should look like, pixel by pixel.

`ROADMAP.md` Tier 1 row 8 said it plainly: "Nothing measures a pixel. Every
layout and font claim on this page is unverified." The engine renders — and
`--render-png` writes a real RGBA8 PNG — but until this script nothing in the
tree read one back, so a change to `LayoutBox`, `StyleResolver` or
`FontRenderer` could move every pixel on the page and no scoreboard noticed.

Why the reader is written out in Python rather than reusing the engine's
writer
--------------------------------------------------------------------------
An engine asked to grade its own PNG grades its own bugs. Every chunk here
(signature, `IHDR`, `IDAT`, the five per-scanline filters, Adler-32) is
decoded independently, so a fault in the writer shows up as a failed reftest
rather than being reproduced faithfully by the checker.

What a reftest is
-----------------
A page and a set of assertions about its pixels: a rectangle that must be
exactly one colour, a pixel that must be white, a region that must be
uniform. Exact equality, with no tolerance, because this engine paints boxes
without anti-aliasing their edges — a tolerance would hide the off-by-one
errors the harness exists to find. A test that legitimately needs slack says
so with `tolerance=`, and there is one place that does: text.

Assertions are checked against the page, and every one names the pixel or
rectangle it reads, so a failure reports coordinates rather than "differs".

Usage
-----
    python3 tools/conformance/render_check.py --binary build/litebird
    python3 tools/conformance/render_check.py --binary build/litebird --verbose
    python3 tools/conformance/render_check.py --list
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SUITE = REPO_ROOT / "tests" / "render" / "reftests.json"


# ---------------------------------------------------------------------------
# PNG reading
# ---------------------------------------------------------------------------

class PngError(Exception):
    pass


@dataclass
class Image:
    width: int
    height: int
    pixels: bytes  # RGBA8, row-major, no padding

    def at(self, x: int, y: int) -> tuple[int, int, int, int]:
        if not (0 <= x < self.width and 0 <= y < self.height):
            raise PngError(f"({x}, {y}) is outside the {self.width}x{self.height} image")
        offset = (y * self.width + x) * 4
        return (self.pixels[offset], self.pixels[offset + 1],
                self.pixels[offset + 2], self.pixels[offset + 3])

    def region(self, x: int, y: int, w: int, h: int) -> set[tuple[int, int, int, int]]:
        """The distinct colours in a rectangle. A uniform region has one."""
        found = set()
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                found.add(self.at(xx, yy))
        return found


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png(path: str | os.PathLike[str]) -> Image:
    """An 8-bit RGBA non-interlaced PNG, decoded without the engine's code."""
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise PngError("not a PNG: bad signature")

    pos = 8
    idat = bytearray()
    header = None
    while pos < len(data):
        if pos + 8 > len(data):
            raise PngError("truncated chunk header")
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if len(body) != length:
            raise PngError(f"truncated {kind!r} chunk")
        stored_crc = struct.unpack(">I", data[pos + 8 + length:pos + 12 + length])[0]
        if zlib.crc32(kind + body) & 0xFFFFFFFF != stored_crc:
            raise PngError(f"{kind!r} chunk fails its CRC")
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
        pos += 12 + length

    if header is None:
        raise PngError("no IHDR chunk")
    width, height, depth, colour, compression, filt, interlace = header
    if depth != 8 or colour != 6:
        raise PngError(f"expected 8-bit RGBA, found depth {depth} colour type {colour}")
    if compression != 0 or filt != 0:
        raise PngError("unknown compression or filter method")
    if interlace != 0:
        raise PngError("interlaced PNGs are not read here")

    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error as exc:
        raise PngError(f"IDAT does not decompress: {exc}") from exc

    bpp = 4
    stride = width * bpp
    if len(raw) != height * (stride + 1):
        raise PngError(f"expected {height * (stride + 1)} bytes of scanlines, got {len(raw)}")

    out = bytearray(height * stride)
    previous = bytearray(stride)
    p = 0
    for y in range(height):
        method = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if method == 0:
            pass
        elif method == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif method == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif method == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif method == 4:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                up = previous[i]
                up_left = previous[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + _paeth(left, up, up_left)) & 0xFF
        else:
            raise PngError(f"scanline {y} uses unknown filter {method}")
        out[y * stride:(y + 1) * stride] = line
        previous = line

    return Image(width, height, bytes(out))


# ---------------------------------------------------------------------------
# Reftests
# ---------------------------------------------------------------------------

@dataclass
class Failure:
    assertion: str
    detail: str


@dataclass
class Result:
    name: str
    failures: list[Failure] = field(default_factory=list)
    error: str = ""
    known_failure: str = ""       # why this page is expected not to match yet

    @property
    def failed(self) -> bool:
        return bool(self.failures) or bool(self.error)

    @property
    def passed(self) -> bool:
        return not self.failed

    @property
    def known(self) -> bool:
        """Expected to fail, and did: not a pass, and not a surprise."""
        return bool(self.known_failure) and self.failed

    @property
    def fixed(self) -> bool:
        """Expected to fail, and does not any more: the entry must come out."""
        return bool(self.known_failure) and not self.failed


def _colour(value) -> tuple[int, int, int, int]:
    """`"#rrggbb"` or `[r, g, b]` or `[r, g, b, a]` as a tuple."""
    if isinstance(value, str):
        text = value.lstrip("#")
        if len(text) == 6:
            return (int(text[0:2], 16), int(text[2:4], 16), int(text[4:6], 16), 255)
        if len(text) == 8:
            return (int(text[0:2], 16), int(text[2:4], 16),
                    int(text[4:6], 16), int(text[6:8], 16))
        raise ValueError(f"a colour is #rrggbb or #rrggbbaa, not {value!r}")
    parts = list(value)
    if len(parts) == 3:
        parts.append(255)
    if len(parts) != 4:
        raise ValueError(f"a colour is 3 or 4 numbers, not {value!r}")
    return tuple(int(p) for p in parts)


def check_assertion(image: Image, spec: dict) -> Failure | None:
    """Runs one assertion and returns what failed, or None."""
    kind = spec.get("kind")
    label = spec.get("describe") or kind or "assertion"

    def close(got, want, tolerance: int) -> bool:
        return all(abs(a - b) <= tolerance for a, b in zip(got, want))

    if kind == "pixel":
        x, y = int(spec["x"]), int(spec["y"])
        want = _colour(spec["colour"])
        tolerance = int(spec.get("tolerance", 0))
        got = image.at(x, y)
        if not close(got, want, tolerance):
            return Failure(label, f"({x}, {y}) is {got}, expected {want}"
                                  + (f" ±{tolerance}" if tolerance else ""))
        return None

    if kind == "rect":
        x, y = int(spec["x"]), int(spec["y"])
        w, h = int(spec["width"]), int(spec["height"])
        want = _colour(spec["colour"])
        found = image.region(x, y, w, h)
        if found != {want}:
            shown = ", ".join(str(c) for c in sorted(found)[:4])
            more = "" if len(found) <= 4 else f" and {len(found) - 4} more"
            return Failure(label, f"{w}x{h} at ({x}, {y}) is not uniformly {want}: "
                                  f"{shown}{more}")
        return None

    if kind == "not-colour":
        # The negative of `pixel`: everything *except* one colour, so a test can
        # say "the text drew something here" without naming the exact glyph.
        x, y = int(spec["x"]), int(spec["y"])
        unwanted = _colour(spec["colour"])
        if image.at(x, y) == unwanted:
            return Failure(label, f"({x}, {y}) is {unwanted}, which it must not be")
        return None

    if kind == "uniform":
        # A rectangle one colour throughout, without naming which: for a page
        # whose exact background is the engine's default rather than the test's.
        x, y = int(spec["x"]), int(spec["y"])
        w, h = int(spec["width"]), int(spec["height"])
        found = image.region(x, y, w, h)
        if len(found) != 1:
            shown = ", ".join(str(c) for c in sorted(found)[:4])
            return Failure(label, f"{w}x{h} at ({x}, {y}) has {len(found)} colours, not one: {shown}")
        return None

    if kind == "differs":
        # Two points that must not be the same colour: the cheapest way to say
        # "this element was painted at all, and not over the background".
        a = (int(spec["ax"]), int(spec["ay"]))
        b = (int(spec["bx"]), int(spec["by"]))
        got_a, got_b = image.at(*a), image.at(*b)
        if got_a == got_b:
            return Failure(label, f"{a} and {b} are both {got_a}, expected them to differ")
        # `a-besides` excludes a colour from the *first* point only, which is
        # how a test says "the glyph was painted here and it is not the page",
        # while the second point stays the blank page it is compared against.
        if "a-besides" in spec:
            forbidden = _colour(spec["a-besides"])
            if got_a == forbidden:
                return Failure(label, f"{a} is {got_a}, which is a colour this assertion excludes")
        return None

    return Failure(label, f"unknown assertion kind {kind!r}")


def run_reftest(binary: str, test: dict, workdir: Path) -> Result:
    name = test["name"]
    result = Result(name)
    page = workdir / f"{name}.html"
    png = workdir / f"{name}.png"
    page.write_text(test["html"], encoding="utf-8")
    command = [binary, "--render-png", str(png), str(page)]
    if "width" in test or "height" in test:
        # The viewport is the engine's default unless the test asks otherwise.
        command = [binary, f"--viewport={test.get('width', 800)}x{test.get('height', 600)}",
                   "--render-png", str(png), str(page)]
    try:
        done = subprocess.run(command, capture_output=True, timeout=test.get("timeout", 60))
    except subprocess.TimeoutExpired:
        result.error = "the render did not finish"
        return result
    if done.returncode != 0:
        result.error = (f"the render exited {done.returncode}: "
                        f"{done.stderr.decode('utf-8', 'replace').strip()[:300]}")
        return result
    if not png.exists():
        result.error = "no PNG was written"
        return result
    try:
        image = read_png(png)
    except PngError as exc:
        result.error = f"the PNG does not read: {exc}"
        return result

    if "width" in test and image.width != int(test["width"]):
        result.failures.append(Failure("page width", f"{image.width}, expected {test['width']}"))
    if "height" in test and image.height != int(test["height"]):
        result.failures.append(Failure("page height", f"{image.height}, expected {test['height']}"))

    for spec in test.get("assert", []):
        failure = check_assertion(image, spec)
        if failure:
            result.failures.append(failure)
    result.known_failure = test.get("known_failure", "")
    return result


def self_test() -> int:
    """Proves the reader and the assertions can fail.

    A harness that only ever passes is indistinguishable from no harness, so the
    two things that could silently rot are exercised on every run: the PNG
    decoder against images it did not write, and each assertion kind against a
    colour it must reject.
    """
    import shutil

    failures = []

    # 1. The decoder, against PNGs written here with each of the five scanline
    #    filters. If the reader shared a misconception with the engine's writer,
    #    this is where it would show.
    def make_png(path: str, w: int, h: int, rows: list[bytes], method: int) -> None:
        raw = bytearray()
        previous = bytearray(w * 4)
        for row in rows:
            line = bytearray(row)
            encoded = bytearray(len(line))
            for i in range(len(line)):
                a = line[i - 4] if i >= 4 else 0
                b = previous[i]
                c = previous[i - 4] if i >= 4 else 0
                if method == 0:
                    e = line[i]
                elif method == 1:
                    e = (line[i] - a) & 0xFF
                elif method == 2:
                    e = (line[i] - b) & 0xFF
                elif method == 3:
                    e = (line[i] - ((a + b) >> 1)) & 0xFF
                else:
                    p = a + b - c
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                    e = (line[i] - pr) & 0xFF
                encoded[i] = e
            raw.append(method)
            raw += encoded
            previous = line

        def chunk(kind: bytes, body: bytes) -> bytes:
            return (struct.pack(">I", len(body)) + kind + body +
                    struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

        blob = (b"\x89PNG\r\n\x1a\n" +
                chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(bytes(raw))) +
                chunk(b"IEND", b""))
        Path(path).write_bytes(blob)

    w, h = 4, 3
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            row += bytes([(x * 60 + y * 7) & 0xFF, (y * 80 + x * 3) & 0xFF,
                          (x * y * 17) & 0xFF, 255])
        rows.append(bytes(row))

    tmpdir = Path(tempfile.mkdtemp(prefix="litebird-render-selftest-"))
    try:
        for method in range(5):
            path = tmpdir / f"f{method}.png"
            make_png(str(path), w, h, rows, method)
            try:
                image = read_png(path)
            except PngError as exc:
                failures.append(f"filter {method} does not read: {exc}")
                continue
            for y in range(h):
                for x in range(w):
                    want = tuple(rows[y][x * 4:x * 4 + 4])
                    if image.at(x, y) != want:
                        failures.append(f"filter {method} at ({x}, {y}): "
                                        f"{image.at(x, y)} != {want}")
                        break

        # 2. A corrupted chunk must be refused, not silently decoded.
        blob = bytearray((tmpdir / "f0.png").read_bytes())
        blob[-20] ^= 0xFF
        (tmpdir / "bad.png").write_bytes(bytes(blob))
        try:
            read_png(tmpdir / "bad.png")
            failures.append("a PNG with a broken CRC was accepted")
        except PngError:
            pass

        # 3. Each assertion kind, against an image whose values are known, must
        #    reject the wrong colour and accept the right one.
        image = Image(2, 2, bytes([
            255, 0, 0, 255,   0, 255, 0, 255,
            0, 0, 255, 255,   255, 255, 255, 255,
        ]))
        must_pass = [
            {"kind": "pixel", "x": 0, "y": 0, "colour": "#ff0000"},
            {"kind": "rect", "x": 0, "y": 0, "width": 1, "height": 1, "colour": "#ff0000"},
            {"kind": "uniform", "x": 1, "y": 1, "width": 1, "height": 1},
            {"kind": "not-colour", "x": 0, "y": 0, "colour": "#00ff00"},
            {"kind": "differs", "ax": 0, "ay": 0, "bx": 1, "by": 0},
            {"kind": "differs", "ax": 0, "ay": 0, "bx": 1, "by": 1, "a-besides": "#00ff00"},
        ]
        for spec in must_pass:
            if check_assertion(image, spec) is not None:
                failures.append(f"{spec['kind']} rejected a true statement: {spec}")

        must_fail = [
            {"kind": "pixel", "x": 0, "y": 0, "colour": "#00ff00"},
            {"kind": "rect", "x": 0, "y": 0, "width": 2, "height": 1, "colour": "#ff0000"},
            {"kind": "rect", "x": 0, "y": 0, "width": 2, "height": 2, "colour": "#ff0000"},
            {"kind": "uniform", "x": 0, "y": 0, "width": 2, "height": 1},
            {"kind": "not-colour", "x": 0, "y": 0, "colour": "#ff0000"},
            {"kind": "differs", "ax": 0, "ay": 0, "bx": 0, "by": 0},
            {"kind": "differs", "ax": 0, "ay": 0, "bx": 1, "by": 0, "a-besides": "#ff0000"},
        ]
        for spec in must_fail:
            if check_assertion(image, spec) is None:
                failures.append(f"{spec['kind']} accepted a false statement: {spec}")

        # 4. The known-failure ledger, in both directions. An entry that excuses a
        #    page which fails is not a pass; an entry attached to a page that
        #    passes must be refused, or the corpus keeps an excuse it has outgrown.
        failure = Failure("rect", "the page is wrong")
        expected_bad = Result("page", [failure], known_failure="a reason")
        expected_good = Result("page", [], known_failure="a reason")
        if not (expected_bad.known and not expected_bad.passed and not expected_bad.fixed):
            failures.append("a page that fails with an entry is not reported as known")
        if not (expected_good.fixed and not expected_good.known and expected_good.passed):
            failures.append("a page that passes with an entry is not reported as fixed")
        if Result("page").known or Result("page").fixed:
            failures.append("a page with no entry is reported as known or fixed")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)

    if failures:
        for failure in failures:
            print(f"self-test FAIL: {failure}", file=sys.stderr)
        return 1
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=str(REPO_ROOT / "build" / "litebird"))
    parser.add_argument("--suite", default=str(DEFAULT_SUITE))
    parser.add_argument("--filter", default="", help="only the tests whose name contains this")
    parser.add_argument("--list", action="store_true", help="name the tests and exit")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--json", action="store_true", help="the result as JSON, for CI")
    parser.add_argument("--self-test", action="store_true",
                        help="prove the reader and the assertions can fail, then exit")
    parser.add_argument("--no-self-test", action="store_true",
                        help="skip the self-test that otherwise runs first")
    args = parser.parse_args(argv)

    if args.self_test:
        if self_test() != 0:
            return 1
        print("self-test: the reader and every assertion kind can fail")
        return 0
    if not args.no_self_test:
        if self_test() != 0:
            print("render_check: the harness itself is broken; refusing to report a score",
                  file=sys.stderr)
            return 2

    binary = args.binary
    if not (os.path.isfile(binary) and os.access(binary, os.X_OK)):
        print(f"render_check: {binary} is not an executable", file=sys.stderr)
        return 2
    suite_path = Path(args.suite)
    if not suite_path.is_file():
        print(f"render_check: no suite at {suite_path}", file=sys.stderr)
        return 2
    suite = json.loads(suite_path.read_text(encoding="utf-8"))
    tests = suite["tests"]
    if args.filter:
        tests = [t for t in tests if args.filter in t["name"]]
    if args.list:
        for test in tests:
            print(test["name"])
        return 0
    if not tests:
        print(f"render_check: nothing matched {args.filter!r}", file=sys.stderr)
        return 2

    results: list[Result] = []
    with tempfile.TemporaryDirectory(prefix="litebird-render-") as tmp:
        workdir = Path(tmp)
        for test in tests:
            result = run_reftest(binary, test, workdir)
            results.append(result)
            if args.verbose or result.failed or result.fixed:
                if result.fixed:
                    mark = "FIXED"
                elif result.known:
                    mark = "KNOWN"
                elif result.passed:
                    mark = "ok  "
                else:
                    mark = "FAIL"
                print(f"{mark} {test['name']}")
                if result.known:
                    print(f"       known: {result.known_failure}")
                if result.fixed:
                    print("       it passes now -- take the known_failure entry out of "
                          "tests/render/reftests.json and let it count")
                if result.error:
                    print(f"       {result.error}")
                if not result.known:
                    for failure in result.failures:
                        print(f"       {failure.assertion}: {failure.detail}")

    passed = sum(1 for r in results if r.passed)
    known = sum(1 for r in results if r.known)
    total = len(results)
    # A page that fails with no entry is a surprise; one that passes with an
    # entry is a stale entry, and the corpus is supposed to shrink, not rot.
    unexplained = [r for r in results if r.fixed or (not r.passed and not r.known)]
    if args.json:
        print(json.dumps({
            "pass": passed,
            "known": known,
            "total": total,
            "tests": [{"name": r.name, "passed": r.passed, "known": r.known,
                       "known_failure": r.known_failure,
                       "failures": [f"{f.assertion}: {f.detail}" for f in r.failures],
                       "error": r.error} for r in results],
        }))
    else:
        fixed = [r.name for r in results if r.fixed]
        if fixed:
            print(f"\n{len(fixed)} page(s) pass now but still carry a known_failure entry: "
                  + ", ".join(fixed))
        line = f"\nRender reftests: {passed}/{total} pages match their reference pixels"
        if known:
            line += (f"; {known} more fail as recorded, with a reason in the corpus"
                     f" ({passed + known} of {total} accounted for)")
        print(line)
    return 0 if not unexplained else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
