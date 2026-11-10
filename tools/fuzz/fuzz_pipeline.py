#!/usr/bin/env python3
"""Fuzz layout, bitmap rendering, and image decoding through the CLI.

HTML inputs are derived from the html5lib tree-construction corpus and run
through both layout and bitmap rendering. Image cases exercise the real file
image-decoding path by embedding a mutated BMP/PPM file in a tiny page.

Run with a sanitizer build:
    python3 tools/fuzz/fuzz_pipeline.py --binary build-asan/litebird
"""

from __future__ import annotations

import argparse
import os
import random
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from fuzz_parser import DEFAULT_BINARY, mutate, seeds

MAX_HTML_BYTES = 1 << 20


def bmp_seed(width: int, height: int, *, bpp: int = 24, offset: int = 54,
             compression: int = 0, payload_size: int = 16) -> bytes:
    data = bytearray(54 + payload_size)
    data[0:2] = b"BM"
    struct.pack_into("<I", data, 2, len(data))
    struct.pack_into("<I", data, 10, offset)
    struct.pack_into("<I", data, 14, 40)
    struct.pack_into("<i", data, 18, width)
    struct.pack_into("<i", data, 22, height)
    struct.pack_into("<H", data, 26, 1)
    struct.pack_into("<H", data, 28, bpp)
    struct.pack_into("<I", data, 30, compression)
    struct.pack_into("<I", data, 34, payload_size)
    return bytes(data)


def image_seeds() -> list[tuple[bytes, str, tuple[int, int]]]:
    """Return (bytes, suffix, protected dimension range) seeds.

    Dimension bytes are not randomly mutated: hostile seeds stay over the
    decoder's pixel cap, while valid seeds stay tiny, preventing fuzz cases
    from allocating tens of megabytes by accident.
    """
    out: list[tuple[bytes, str, tuple[int, int]]] = []
    for blob in (
        bmp_seed(2, 2),
        bmp_seed(2, 2, bpp=32, payload_size=16),
        bmp_seed(65_536, 65_536),       # pixel-count cap
        bmp_seed(65_536, 65_535),       # historical signed-product overflow
        bmp_seed(65_537, 65_537),       # historical small-positive wraparound
        bmp_seed(4, -(1 << 31)),        # INT32_MIN height
        bmp_seed(2, 2, offset=0xFFFFFFFF),
        bmp_seed(2, 2, bpp=8),
        bmp_seed(2, 2, compression=1),
        bmp_seed(2, 2, payload_size=0),
    ):
        out.append((blob, ".bmp", (18, 26)))

    ppm_headers = (
        (b"P6\n2 2\n255\n" + bytes(range(12)), ".ppm"),
        (b"P3\n2 2\n255\n0 1 2 3 4 5 6 7 8 9 10 11\n", ".ppm"),
        (b"P6\n65536 65536\n255\n", ".ppm"),
        (b"P6\n65536 65535\n255\n", ".ppm"),
        (b"P6\n0 1\n255\n", ".ppm"),
        (b"P6\n-1 1\n255\n", ".ppm"),
        (b"P6\n999999999999999999999999999999 1\n255\n", ".ppm"),
        (b"P6\n1 1\n999999999999999999999999999999\n", ".ppm"),
        (b"P3\n2 2\n999999999999999999999999999999\n", ".ppm"),
    )
    for blob, suffix in ppm_headers:
        first_nl = blob.find(b"\n")
        second_nl = blob.find(b"\n", first_nl + 1)
        protected = (first_nl + 1, second_nl) if second_nl >= 0 else (0, 0)
        out.append((blob, suffix, protected))
    return out


def mutate_image(seed: bytes, protected: tuple[int, int], rng: random.Random) -> bytes:
    buf = bytearray(seed)
    lo, hi = protected
    positions = [i for i in range(len(buf)) if not lo <= i < hi]
    for _ in range(rng.randint(1, 8)):
        if not positions:
            break
        pos = rng.choice(positions)
        buf[pos] = rng.randrange(256)
    return bytes(buf)


def sanitizer_report(stderr: bytes) -> bool:
    markers = (b"AddressSanitizer", b"UndefinedBehaviorSanitizer",
               b"runtime error:", b"SUMMARY: ", b"LeakSanitizer")
    return any(marker in stderr for marker in markers)


def run_case(binary: str, args: list[str], html: bytes, *, label: str,
             iteration: int, timeout: float, image: bytes | None = None,
             image_suffix: str = ".img") -> tuple[bool, str]:
    with tempfile.TemporaryDirectory(prefix="litebird-fuzz-") as temp_dir:
        root = Path(temp_dir)
        html_path = root / "case.html"
        output_path = root / "render.bmp"
        image_path = root / f"input{image_suffix}"
        if image is not None:
            image_path.write_bytes(image)
            html = ("<!doctype html><html><head><style>img { display:block; "
                    "width:2px;height:2px }</style></head><body>"
                    f'<img src="{image_path}" width="2" height="2">'
                    "</body></html>").encode()
        html = html[:MAX_HTML_BYTES]
        html_path.write_bytes(html)
        command = [binary, *args]
        if "--render-bmp" in args:
            command.append(str(output_path))
        command.append(str(html_path))
        timed_out = False
        try:
            result = subprocess.run(command, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.PIPE, timeout=timeout)
            stderr = result.stderr
            failed_exit = result.returncode < 0 or result.returncode > 1
            exit_code = result.returncode
        except subprocess.TimeoutExpired as exc:
            timed_out = True
            stderr = exc.stderr or b""
            failed_exit = True
            exit_code = None

        sanitizable = sanitizer_report(stderr)
        if failed_exit or sanitizable:
            reason = "timeout" if timed_out else (
                "sanitizer report" if sanitizable else f"exit {exit_code}")
            artifact_dir = Path("build-asan/fuzz-findings")
            artifact_dir.mkdir(parents=True, exist_ok=True)
            stem = f"{label}-{iteration:04d}"
            if image is None:
                (artifact_dir / f"{stem}.html").write_bytes(html)
            else:
                saved_image = artifact_dir / f"{stem}{image_suffix}"
                saved_image.write_bytes(image)
                repro = ("<!doctype html><html><head><style>img { display:block; "
                         "width:2px;height:2px }</style></head><body>"
                         f'<img src="{saved_image.resolve()}" width="2" height="2">'
                         "</body></html>")
                (artifact_dir / f"{stem}.html").write_text(repro)
            (artifact_dir / f"{stem}.stderr").write_bytes(stderr)
            detail = stderr.decode("utf-8", "replace").splitlines()
            return True, f"{reason}; saved {artifact_dir / stem} ({detail[0][:160] if detail else 'no stderr'})"
        return False, ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=DEFAULT_BINARY)
    parser.add_argument("--html-iterations", type=int, default=150)
    parser.add_argument("--image-iterations", type=int, default=100)
    parser.add_argument("--seed", type=int, default=20261108)
    parser.add_argument("--timeout", type=float, default=10.0)
    args = parser.parse_args()

    if not Path(args.binary).exists():
        print(f"error: {args.binary} not found (run `make asan`)", file=sys.stderr)
        return 2
    html_seeds = seeds()
    if not html_seeds:
        print("error: no HTML seed inputs found", file=sys.stderr)
        return 2

    rng = random.Random(args.seed)
    findings = 0
    runs = 0

    for i in range(args.html_iterations):
        data = mutate(rng.choice(html_seeds), rng)[:MAX_HTML_BYTES]
        for mode, flags in (("layout", ["--dump-layout"]),
                            ("render", ["--render-bmp"])):
            runs += 1
            found, detail = run_case(args.binary, flags, data, label=mode,
                                     iteration=i, timeout=args.timeout)
            if found:
                findings += 1
                print(f"  ! {mode} finding #{findings}: {detail}")

    images = image_seeds()
    for i in range(args.image_iterations):
        blob, suffix, protected = rng.choice(images)
        data = mutate_image(blob, protected, rng)
        runs += 1
        found, detail = run_case(args.binary, ["--render-bmp"], b"", label="image",
                                 iteration=i, timeout=args.timeout,
                                 image=data, image_suffix=suffix)
        if found:
            findings += 1
            print(f"  ! image/render finding #{findings}: {detail}")

    print(f"pipeline fuzz: {runs} runs ({args.html_iterations} HTML inputs through layout+render, "
          f"{args.image_iterations} malformed/valid image cases), {findings} finding(s)")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
