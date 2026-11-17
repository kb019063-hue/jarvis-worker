#!/usr/bin/env python3
"""Mutation and adversarial fuzzer for Litebird's JPEG, GIF, WebP, and SVG stack.

Generates real JPEG/GIF/WebP seeds (using ImageMagick when present, plus minimal
hand-crafted binary seeds so the fuzzer also runs offline without convert) and
complex SVG seeds, then applies byte/bit/truncation/marker/structural mutations
plus explicit hostile edge cases (dimension bombs, <use>/gradient cycles,
truncated Huffman/LZW streams, malformed SVG path/transform grammars).

Any crash, non-zero exit, sanitizer report, or timeout is a failure.

Usage:
    python3 tools/fuzz/fuzz_media.py [--binary build/media_check]
                                     [--iterations 600] [--seed 20261115]
"""

from __future__ import annotations

import argparse
import os
import random
import shutil
import subprocess
import sys
import tempfile


def minimal_jpeg_seeds(tmpdir: str) -> list[bytes]:
    seeds: list[bytes] = []
    # Hand-crafted minimal invalid/edge JPEG headers
    seeds.append(b"\xFF\xD8\xFF\xD9")
    seeds.append(b"\xFF\xD8\xFF\xC0\x00\x0B\x08\xFF\xFF\xFF\xFF\x01\x01\x11\x00\xFF\xD9")  # 65535x65535 bomb
    seeds.append(b"\xFF\xD8\xFF\xC2\x00\x0B\x08\x00\x10\x00\x10\x01\x01\x11\x00\xFF\xDA\x00\x08\x01\x01\x00\x00\x00\x00\xFF\xD9")
    convert_bin = shutil.which("convert")
    if convert_bin:
        for idx, flags in enumerate([
            ["-size", "16x16", "gradient:red-blue", "-sampling-factor", "4:4:4"],
            ["-size", "24x20", "plasma:fractal", "-sampling-factor", "4:2:0", "-interlace", "Plane"],
            ["-size", "20x18", "gradient:white-black", "-colorspace", "Gray", "-interlace", "Plane"],
            ["-size", "24x16", "gradient:cyan-magenta", "-colorspace", "CMYK"],
            ["-size", "24x16", "gradient:yellow-green", "-define", "jpeg:restart-interval=1"],
        ]):
            p = os.path.join(tmpdir, f"seed_{idx}.jpg")
            subprocess.run([convert_bin] + flags + [p], check=True, capture_output=True)
            with open(p, "rb") as f:
                seeds.append(f.read())
    return seeds


def minimal_webp_seeds(tmpdir: str) -> list[bytes]:
    seeds: list[bytes] = []
    # Hand-crafted headers: a bare VP8L chunk with no payload, a dimension bomb
    # (16384 x 16384), a lossy VP8 chunk, a VP8X header with the animation flag,
    # and a RIFF whose stated size runs past the buffer.
    seeds.append(b"RIFF\x0c\x00\x00\x00WEBPVP8L\x00\x00\x00\x00")
    seeds.append(b"RIFF\x1e\x00\x00\x00WEBPVP8L\x11\x00\x00\x00\x2f\xff\xff\xff\x0f\x00\x00\x00\x00")
    seeds.append(b"RIFF\x14\x00\x00\x00WEBPVP8 \x08\x00\x00\x00\x30\x01\x00\x9d\x01\x2a\x00\x00")
    seeds.append(b"RIFF\x1a\x00\x00\x00WEBPVP8X\x0a\x00\x00\x00\x02\x00\x00\x00\x03\x00\x00\x02\x00\x00\x00")
    seeds.append(b"RIFF\xff\xff\xff\x7fWEBPVP8L\xff\xff\x00\x00\x2f\x00\x00\x00")
    convert_bin = shutil.which("convert")
    if convert_bin:
        for idx, args in enumerate([
            ["-size", "16x16", "gradient:red-blue"],
            ["-size", "13x9", "gradient:rgba(0,255,0,0)-rgba(255,0,0,1)"],
            ["-size", "17x11", "xc:", "+noise", "Random", "-colors", "4", "+dither"],
            ["-size", "24x24", "plasma:fuchsia-aqua", "-colors", "16", "+dither"],
            ["-size", "20x20", "xc:#102030", "-fill", "#f0e0d0", "-draw", "circle 4,4 9,9"],
        ]):
            p = os.path.join(tmpdir, f"seed_w{idx}.webp")
            subprocess.run([convert_bin] + args + ["-define", "webp:lossless=true", "-quality", "100", p],
                           check=True, capture_output=True)
            with open(p, "rb") as f:
                seeds.append(f.read())
    return seeds


def minimal_gif_seeds(tmpdir: str) -> list[bytes]:
    seeds: list[bytes] = []
    # Hand-crafted valid 1x1 GIF89a
    seeds.append(
        b"GIF89a\x01\x00\x01\x00\x80\x00\x00\xFF\x00\x00\x00\x00\x00"
        b"\x2C\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02\x02\x44\x01\x00\x3B"
    )
    # 65535x65535 dimension bomb GIF
    seeds.append(b"GIF89a\xFF\xFF\xFF\xFF\x80\x00\x00\x00\x00\x00\xFF\xFF\xFF\x3B")
    convert_bin = shutil.which("convert")
    if convert_bin:
        for idx, args in enumerate([
            ["-size", "16x16", "gradient:red-blue", "-colors", "16"],
            ["-size", "19x15", "gradient:green-yellow", "-interlace", "Line", "-colors", "32"],
            [
                "-delay", "10", "-loop", "0", "-dispose", "Background",
                "(", "-size", "16x16", "xc:none", "-fill", "red", "-draw", "rectangle 1,1 8,8", ")",
                "(", "-size", "16x16", "xc:none", "-fill", "blue", "-draw", "rectangle 6,6 14,14", ")",
            ],
        ]):
            p = os.path.join(tmpdir, f"seed_{idx}.gif")
            subprocess.run([convert_bin] + args + [p], check=True, capture_output=True)
            with open(p, "rb") as f:
                seeds.append(f.read())
    return seeds


def svg_seeds() -> list[bytes]:
    return [
        b'<svg viewBox="0 0 100 100"><path d="M10,10 C20,30 40,50 60,10 S90,90 10,90 Q50,0 80,50 T10,10 A20,30 45 1010,20 Z" fill="red" stroke="blue" stroke-width="4"/></svg>',
        b'<svg viewBox="0 0 50 50"><defs><linearGradient id="g" href="#g"><stop offset="0%" stop-color="red"/></linearGradient></defs><rect width="50" height="50" fill="url(#g)"/></svg>',
        b'<svg viewBox="0 0 50 50"><defs><g id="a"><use href="#b"/></g><g id="b"><use href="#a"/></g></defs><use href="#a"/></svg>',
        b'<svg viewBox="0 0 50 50"><defs><clipPath id="c"><circle cx="25" cy="25" r="20"/></clipPath></defs><g clip-path="url(#c)" transform="matrix(1,0,0,1,5,5) rotate(30) skewX(89.999)"><rect width="50" height="50" fill="rgba(10,20,30,0.5)"/></g></svg>',
        b'<svg viewBox="-1e30 -1e30 2e30 2e30"><path d="M0,0 A1e38,1e38 0 1 1 1,1" stroke-dasharray="0 0 0" stroke="red" stroke-width="1e9"/></svg>',
        b'<svg width="999999" height="999999"><rect width="10" height="10" fill="red"/></svg>',
        (b'<svg viewBox="0 0 40 40">' + b'<g transform="translate(1,1)">' * 200 + b'<rect width="5" height="5" fill="red"/>' + b'</g>' * 200 + b'</svg>'),
    ]


def mutate_bytes(rng: random.Random, data: bytes, interesting: list[bytes]) -> bytes:
    if not data:
        return b"\xFF\xD8"
    buf = bytearray(data)
    ops = rng.randint(1, 5)
    for _ in range(ops):
        kind = rng.randint(0, 6)
        if kind == 0 and buf:
            # Bit flip
            pos = rng.randrange(len(buf))
            buf[pos] ^= 1 << rng.randrange(8)
        elif kind == 1 and buf:
            # Byte overwrite
            pos = rng.randrange(len(buf))
            buf[pos] = rng.choice([0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF, rng.randrange(256)])
        elif kind == 2 and len(buf) > 4:
            # Truncate
            buf = buf[:rng.randint(1, len(buf) - 1)]
        elif kind == 3 and len(buf) > 4:
            # Delete slice
            a = rng.randrange(len(buf))
            b = min(len(buf), a + rng.randint(1, max(1, len(buf) // 4)))
            del buf[a:b]
        elif kind == 4 and len(buf) < 65536:
            # Insert interesting token
            tok = rng.choice(interesting)
            pos = rng.randrange(len(buf) + 1)
            buf[pos:pos] = tok
        elif kind == 5 and len(buf) > 2:
            # Overwrite 2-byte big/little endian boundary value
            pos = rng.randrange(len(buf) - 1)
            val = rng.choice([b"\x00\x00", b"\x00\x01", b"\xFF\xFF", b"\x7F\xFF", b"\x80\x00"])
            buf[pos:pos + 2] = val
        elif kind == 6 and len(buf) < 65536 and len(buf) > 4:
            # Duplicate slice
            a = rng.randrange(len(buf))
            b = min(len(buf), a + rng.randint(1, min(64, len(buf) - a)))
            pos = rng.randrange(len(buf) + 1)
            buf[pos:pos] = buf[a:b]
    return bytes(buf)


def run_batch(binary: str, kind: str, paths: list[str], timeout_s: float) -> None:
    cmd = [binary, "--fuzz-batch", kind] + paths
    res = subprocess.run(cmd, capture_output=True, timeout=timeout_s)
    if res.returncode != 0:
        # Narrow down to the exact failing file
        for p in paths:
            single = subprocess.run([binary, f"--fuzz-{kind}", p], capture_output=True, timeout=timeout_s)
            if single.returncode != 0:
                stderr = single.stderr.decode("utf-8", errors="replace")
                raise RuntimeError(
                    f"CRASH/FAILURE in {kind} fuzzing on {p} (exit={single.returncode}):\n{stderr}"
                )
        stderr = res.stderr.decode("utf-8", errors="replace")
        raise RuntimeError(f"Batch failure in {kind} fuzzing (exit={res.returncode}):\n{stderr}")


def fuzz_kind(binary: str, kind: str, seeds: list[bytes], interesting: list[bytes],
              iterations: int, rng: random.Random, tmpdir: str, timeout_s: float) -> int:
    batch_size = 100
    case_paths: list[str] = []
    # Always include raw unmutated seeds first
    cases: list[bytes] = list(seeds)
    while len(cases) < iterations:
        base = rng.choice(seeds)
        cases.append(mutate_bytes(rng, base, interesting))

    for i, blob in enumerate(cases):
        p = os.path.join(tmpdir, f"{kind}_{i:05d}.bin")
        with open(p, "wb") as f:
            f.write(blob)
        case_paths.append(p)
        if len(case_paths) >= batch_size:
            run_batch(binary, kind, case_paths, timeout_s)
            case_paths.clear()

    if case_paths:
        run_batch(binary, kind, case_paths, timeout_s)
    return len(cases)


def main() -> int:
    parser = argparse.ArgumentParser(description="Fuzz Litebird JPEG, GIF, WebP, and SVG decoders/renderer.")
    parser.add_argument("--binary", default="build/media_check", help="Path to media_check binary")
    parser.add_argument("--iterations", type=int, default=600, help="Cases per format (total = 4x)")
    parser.add_argument("--seed", type=int, default=20261115, help="PRNG seed")
    parser.add_argument("--timeout", type=float, default=30.0, help="Timeout per batch (seconds)")
    args = parser.parse_args()

    if not os.path.exists(args.binary):
        print(f"error: {args.binary} not found", file=sys.stderr)
        return 1

    rng = random.Random(args.seed)
    jpeg_tokens = [b"\xFF\xD8", b"\xFF\xD9", b"\xFF\xC0", b"\xFF\xC2", b"\xFF\xC4", b"\xFF\xDB",
                   b"\xFF\xDA", b"\xFF\xDD", b"\xFF\xD0", b"\xFF\x00", b"\xFF\xFF", b"\x00\x00"]
    gif_tokens = [b"GIF87a", b"GIF89a", b"\x21\xF9\x04", b"\x21\xFF\x0B", b"\x2C", b"\x3B",
                  b"\x00", b"\xFF", b"\x08", b"\x0C"]
    webp_tokens = [b"RIFF", b"WEBP", b"VP8L", b"VP8 ", b"VP8X", b"ANIM", b"ANMF", b"\x2f",
                   b"\xff\xff\xff\xff", b"\x00\x00\x00\x00", b"\x0f"]
    svg_tokens = [b"<svg", b"</svg>", b"<path d=\"", b"A1,1 0 1 1 2,2", b"url(#g)", b"<use href=\"#a\"/>",
                  b"transform=\"rotate(45) scale(1e9)\"", b"clip-path=\"url(#c)\"", b"NaN", b"1e300", b"-0.0"]

    print(f"=== Litebird Phase 38 Media Mutation Fuzzer ({args.binary}, seed={args.seed}) ===")
    with tempfile.TemporaryDirectory(prefix="litebird_fuzz_media_") as tmpdir:
        n_jpg = fuzz_kind(args.binary, "jpeg", minimal_jpeg_seeds(tmpdir), jpeg_tokens,
                          args.iterations, rng, tmpdir, args.timeout)
        print(f"  [PASS] JPEG fuzzing: {n_jpg} cases, 0 crashes/timeouts")

        n_gif = fuzz_kind(args.binary, "gif", minimal_gif_seeds(tmpdir), gif_tokens,
                          args.iterations, rng, tmpdir, args.timeout)
        print(f"  [PASS] GIF  fuzzing: {n_gif} cases, 0 crashes/timeouts")

        n_webp = fuzz_kind(args.binary, "webp", minimal_webp_seeds(tmpdir), webp_tokens,
                           args.iterations, rng, tmpdir, args.timeout)
        print(f"  [PASS] WebP fuzzing: {n_webp} cases, 0 crashes/timeouts")

        n_svg = fuzz_kind(args.binary, "svg", svg_seeds(), svg_tokens,
                          args.iterations, rng, tmpdir, args.timeout)
        print(f"  [PASS] SVG  fuzzing: {n_svg} cases, 0 crashes/timeouts")

    print(f"\nAll {n_jpg + n_gif + n_webp + n_svg} media fuzz cases completed cleanly!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
