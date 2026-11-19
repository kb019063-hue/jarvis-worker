#!/usr/bin/env python3
"""Differential oracle & conformance verifier for Litebird's Phase 38 media stack.

Tests:
  1. JPEG decoding (Baseline SOF0, Extended SOF1, Progressive SOF2, 4:4:4, 4:2:2,
     4:2:0, Grayscale, CMYK/YCCK, odd non-MCU-aligned dimensions, 1x1, restart
     markers, low/high quality) pixel-by-pixel against ImageMagick/libjpeg.
  2. GIF decoding (GIF87a, GIF89a, 2..8-bit LZW, transparency, 4-pass
     interlacing, multi-frame animations with Dispose None/Background/Previous)
     pixel-by-pixel for EXACT bit-for-bit equality against ImageMagick -coalesce.
  3. WebP lossless (VP8L) decoding: the container (simple and extended
     `VP8X`), the entropy-coded image stream (meta prefix codes, colour cache,
     LZ77), and all four self-similarity transforms (predictor, cross-colour,
     subtract-green, colour-indexing with 2/4/16/256-entry palettes and pixel
     bundling) — bit-exact against libwebp, since lossless has no tolerance.
     Refusals are checked too: truncated and non-WebP input must be reported,
     not drawn.
  3b. Animated WebP (`VP8X` canvas, `ANIM`, one `ANMF` per frame): every frame
     composited and compared, with its rectangle, duration, blending method and
     disposal method, against ImageMagick where it can be the oracle (opaque
     frames, which it coalesces itself) and against `composite_frames` where it
     cannot (it blends frames that ask not to be blended), whose frame images
     come from ImageMagick. Metadata, the frame cap, `decode()`'s first frame,
     and the refusals are checked here.
  4. Real-world SVG vector rendering (viewBox, preserveAspectRatio, full path
     grammar including W3C F.6.5/F.6.6 elliptical arcs and compact flags,
     transforms, evenodd/nonzero fill rules, linear/radial gradients, <use>,
     <clipPath>, inline styles, rgba/#RRGGBBAA, opacity, currentColor).
"""

import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile


def run_media_check(binary, args):
    res = subprocess.run([binary] + args, capture_output=True)
    if res.returncode != 0:
        err = res.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"media_check {' '.join(args)} failed ({res.returncode}): {err}")
    return res.stdout.decode("utf-8", errors="replace")


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # Parse P6 header
    lines = []
    pos = 0
    while len(lines) < 3:
        nl = data.index(b"\n", pos)
        ln = data[pos:nl].decode("ascii").strip()
        pos = nl + 1
        if ln and not ln.startswith("#"):
            lines.append(ln)
    assert lines[0] == "P6", f"Expected P6 PPM, got {lines[0]}"
    w, h = map(int, lines[1].split())
    assert lines[2] == "255"
    rgb = data[pos:]
    assert len(rgb) == w * h * 3, f"Expected {w*h*3} RGB bytes, got {len(rgb)}"
    return w, h, rgb


def write_ppm(path, w, h, pixel_fn):
    data = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b = pixel_fn(x, y, w, h)
            data.extend((r & 0xFF, g & 0xFF, b & 0xFF))
    with open(path, "wb") as f:
        f.write(f"P6\n{w} {h}\n255\n".encode("ascii"))
        f.write(data)


def rich_pattern(x, y, w, h):
    """Smooth gradients + color blocks + diagonal stripe for JPEG stress testing."""
    fx = x / max(1, w - 1)
    fy = y / max(1, h - 1)
    r = int(20 + 215 * fx)
    g = int(20 + 215 * fy)
    b = int(128 + 100 * math.sin((x + y) * 0.25))
    if (x // 8 + y // 8) % 2 == 0 and x < w // 2 and y < h // 2:
        r, g, b = 230, 40, 30
    if abs(x - y) <= 1:
        r, g, b = 245, 240, 20
    return max(0, min(255, r)), max(0, min(255, g)), max(0, min(255, b))


def compare_bytes(actual, expected, channels=3, ignore_rgb_when_alpha_zero=False):
    assert len(actual) == len(expected), f"Length mismatch: {len(actual)} vs {len(expected)}"
    total_abs = 0
    total_sq = 0
    max_diff = 0
    compared = 0
    for i in range(0, len(actual), channels):
        if channels == 4 and ignore_rgb_when_alpha_zero and actual[i + 3] == 0 and expected[i + 3] == 0:
            compared += 4
            continue
        for c in range(channels):
            d = abs(actual[i + c] - expected[i + c])
            total_abs += d
            total_sq += d * d
            if d > max_diff:
                max_diff = d
            compared += 1
    mae = total_abs / max(1, compared)
    mse = total_sq / max(1, compared)
    psnr = 99.0 if mse == 0 else 10.0 * math.log10((255.0 * 255.0) / mse)
    return mae, psnr, max_diff


def decode_jpeg_with_imagemagick(convert_bin, jpeg_path):
    ident = subprocess.run([convert_bin, jpeg_path, "-format", "%w %h", "info:"],
                           capture_output=True, check=True)
    w, h = map(int, ident.stdout.decode("ascii").strip().split())
    res = subprocess.run([convert_bin, jpeg_path, "-colorspace", "sRGB", "-depth", "8", "rgb:-"],
                         capture_output=True, check=True)
    return w, h, res.stdout


def verify_jpegs_with_oracle(binary, convert_bin, tmpdir):
    cases = [
        ("baseline_444_32x24", 32, 24, ["-sampling-factor", "4:4:4", "-quality", "92"]),
        ("baseline_420_48x36", 48, 36, ["-sampling-factor", "4:2:0", "-quality", "85"]),
        ("baseline_422_48x36", 48, 36, ["-sampling-factor", "4:2:2", "-quality", "85"]),
        ("baseline_odd_420_37x29", 37, 29, ["-sampling-factor", "4:2:0", "-quality", "90"]),
        ("baseline_odd_444_17x13", 17, 13, ["-sampling-factor", "4:4:4", "-quality", "95"]),
        ("baseline_1x1", 1, 1, ["-sampling-factor", "4:4:4", "-quality", "95"]),
        ("progressive_420_64x48", 64, 48, ["-sampling-factor", "4:2:0", "-interlace", "Plane", "-quality", "88"]),
        ("progressive_odd_444_53x41", 53, 41, ["-sampling-factor", "4:4:4", "-interlace", "Plane", "-quality", "90"]),
        ("progressive_422_50x34", 50, 34, ["-sampling-factor", "4:2:2", "-interlace", "Plane", "-quality", "85"]),
        ("grayscale_baseline_45x31", 45, 31, ["-colorspace", "Gray", "-quality", "90"]),
        ("grayscale_progressive_45x31", 45, 31, ["-colorspace", "Gray", "-interlace", "Plane", "-quality", "90"]),
        ("restart_markers_48x32", 48, 32, ["-sampling-factor", "4:2:0", "-define", "jpeg:restart-interval=2", "-quality", "85"]),
        ("restart_progressive_48x32", 48, 32, ["-sampling-factor", "4:2:0", "-interlace", "Plane", "-define", "jpeg:restart-interval=3", "-quality", "85"]),
        ("cmyk_baseline_36x28", 36, 28, ["-colorspace", "CMYK", "-quality", "90"]),
        ("cmyk_progressive_36x28", 36, 28, ["-colorspace", "CMYK", "-interlace", "Plane", "-quality", "90"]),
        ("low_quality_64x64", 64, 64, ["-sampling-factor", "4:2:0", "-quality", "20"]),
        ("high_quality_64x64", 64, 64, ["-sampling-factor", "4:4:4", "-quality", "100"]),
        ("high_quality_prog_64x64", 64, 64, ["-sampling-factor", "4:4:4", "-interlace", "Plane", "-quality", "100"]),
        ("sampling_440_48x36", 48, 36, ["-sampling-factor", "4:4:0", "-quality", "88"]),
        ("optimized_huff_96x72", 96, 72, ["-sampling-factor", "4:2:0", "-define", "jpeg:optimize-coding=true", "-quality", "82"]),
        ("large_prog_rst_128x96", 128, 96, ["-sampling-factor", "4:2:0", "-interlace", "Plane", "-define", "jpeg:restart-interval=4", "-quality", "85"]),
    ]

    passed = 0
    for name, w, h, flags in cases:
        ppm_path = os.path.join(tmpdir, f"{name}.ppm")
        jpg_path = os.path.join(tmpdir, f"{name}.jpg")
        out_ppm = os.path.join(tmpdir, f"{name}.out.ppm")
        write_ppm(ppm_path, w, h, rich_pattern)
        subprocess.run([convert_bin, ppm_path] + flags + [jpg_path], check=True, capture_output=True)

        rw, rh, ref_rgb = decode_jpeg_with_imagemagick(convert_bin, jpg_path)
        meta = run_media_check(binary, ["--decode-jpeg", jpg_path, out_ppm]).strip()
        aw, ah, act_rgb = read_ppm(out_ppm)

        if (aw, ah) != (rw, rh):
            raise AssertionError(f"[JPEG {name}] size mismatch: got {aw}x{ah}, expected {rw}x{rh}")

        mae, psnr, max_diff = compare_bytes(act_rgb, ref_rgb, channels=3)
        is_cmyk = "cmyk" in name
        max_mae = 2.0 if is_cmyk else 1.0
        min_psnr = 40.0 if is_cmyk else 45.0
        if mae > max_mae or psnr < min_psnr:
            raise AssertionError(
                f"[JPEG {name}] pixel mismatch vs libjpeg: MAE={mae:.3f} (max {max_mae}), "
                f"PSNR={psnr:.2f} dB (min {min_psnr}), max_diff={max_diff} ({meta})"
            )
        print(f"  [PASS] JPEG {name:28s} {aw:2d}x{ah:<2d}  MAE={mae:.3f}  PSNR={psnr:5.2f} dB  max_diff={max_diff}")
        passed += 1
    return passed


def verify_gifs_with_oracle(binary, convert_bin, tmpdir):
    passed = 0

    def check_gif(name, gif_path, expect_min_frames=1):
        nonlocal passed
        ident = subprocess.run(
            [convert_bin, gif_path, "-coalesce", "-format", "%w %h %T\n", "info:"],
            capture_output=True, check=True,
        )
        lines = [ln.strip() for ln in ident.stdout.decode("ascii").splitlines() if ln.strip()]
        ref_frames_meta = [tuple(map(int, ln.split())) for ln in lines]

        raw_proc = subprocess.run(
            [convert_bin, gif_path, "-coalesce", "-depth", "8", "rgba:-"],
            capture_output=True, check=True,
        )
        raw_all = raw_proc.stdout

        prefix = os.path.join(tmpdir, f"gif_out_{name}")
        stdout = run_media_check(binary, ["--decode-gif", gif_path, prefix])
        out_lines = [ln.strip() for ln in stdout.splitlines() if ln.strip()]
        cw, ch, nframes, loop_count = map(int, out_lines[0].split())

        if nframes < expect_min_frames or nframes != len(ref_frames_meta):
            raise AssertionError(
                f"[GIF {name}] frame count mismatch: got {nframes}, expected {len(ref_frames_meta)}"
            )

        offset = 0
        worst_diff = 0
        for idx, (rw, rh, _delay_cs) in enumerate(ref_frames_meta):
            if (cw, ch) != (rw, rh):
                raise AssertionError(f"[GIF {name} frame {idx}] size {cw}x{ch} != {rw}x{rh}")
            nbytes = rw * rh * 4
            ref_rgba = raw_all[offset:offset + nbytes]
            offset += nbytes
            with open(f"{prefix}_{idx}.rgba", "rb") as f:
                act_rgba = f.read()
            mae, psnr, max_diff = compare_bytes(act_rgba, ref_rgba, channels=4, ignore_rgb_when_alpha_zero=True)
            worst_diff = max(worst_diff, max_diff)
            if max_diff != 0:
                raise AssertionError(
                    f"[GIF {name} frame {idx}] lossless mismatch: MAE={mae:.3f}, max_diff={max_diff}"
                )
        print(f"  [PASS] GIF  {name:28s} {cw:2d}x{ch:<2d}  frames={nframes}  loop={loop_count}  max_diff={worst_diff}")
        passed += 1

    p1 = os.path.join(tmpdir, "src1.ppm")
    write_ppm(p1, 32, 24, rich_pattern)

    # 1. GIF87a single-frame 32x24
    g1 = os.path.join(tmpdir, "gif87a.gif")
    subprocess.run([convert_bin, p1, "-colors", "64", f"gif87:{g1}"], check=True)
    check_gif("gif87a_64colors_32x24", g1)

    # 2. GIF89a with small 4-color palette (LZW min_code_size=2)
    g2 = os.path.join(tmpdir, "gif_4colors.gif")
    subprocess.run([convert_bin, p1, "-colors", "4", g2], check=True)
    check_gif("gif89a_4colors_lzw2", g2)

    # 3. GIF89a with full 256-color palette (LZW min_code_size=8 + table growth)
    p3 = os.path.join(tmpdir, "src3.ppm")
    write_ppm(p3, 64, 48, rich_pattern)
    g3 = os.path.join(tmpdir, "gif_256colors.gif")
    subprocess.run([convert_bin, p3, "-colors", "256", g3], check=True)
    check_gif("gif89a_256colors_lzw8", g3)

    # 4. Interlaced GIF89a with odd dimensions (4-pass interlacing)
    p4 = os.path.join(tmpdir, "src4.ppm")
    write_ppm(p4, 41, 33, rich_pattern)
    g4 = os.path.join(tmpdir, "gif_interlaced.gif")
    subprocess.run([convert_bin, p4, "-interlace", "Line", "-colors", "128", g4], check=True)
    check_gif("gif89a_interlaced_41x33", g4)

    # 5. GIF89a with transparency
    g5 = os.path.join(tmpdir, "gif_transparent.gif")
    subprocess.run([
        convert_bin, "-size", "35x27", "xc:none",
        "-fill", "red", "-draw", "rectangle 4,4 20,20",
        "-fill", "blue", "-draw", "circle 24,14 30,14",
        g5,
    ], check=True)
    check_gif("gif89a_transparent_35x27", g5)

    # 6. Multi-frame animation: Dispose=None (1)
    g6 = os.path.join(tmpdir, "anim_none.gif")
    subprocess.run([
        convert_bin, "-delay", "10", "-loop", "3",
        "-dispose", "None",
        "(", "-size", "32x32", "xc:navy", "-fill", "yellow", "-draw", "rectangle 2,2 10,10", ")",
        "(", "-size", "32x32", "xc:none", "-fill", "lime", "-draw", "rectangle 12,12 22,22", ")",
        "(", "-size", "32x32", "xc:none", "-fill", "red", "-draw", "rectangle 20,4 30,14", ")",
        g6,
    ], check=True)
    check_gif("gif89a_anim_dispose_none", g6, expect_min_frames=3)

    # 7. Multi-frame animation: Dispose=Background (2)
    g7 = os.path.join(tmpdir, "anim_bg.gif")
    subprocess.run([
        convert_bin, "-delay", "15", "-loop", "0",
        "-dispose", "Background",
        "(", "-size", "32x32", "xc:none", "-fill", "red", "-draw", "rectangle 2,2 14,14", ")",
        "(", "-size", "32x32", "xc:none", "-fill", "green", "-draw", "rectangle 14,14 28,28", ")",
        "(", "-size", "32x32", "xc:none", "-fill", "blue", "-draw", "rectangle 6,18 18,30", ")",
        g7,
    ], check=True)
    check_gif("gif89a_anim_dispose_bg", g7, expect_min_frames=3)

    # 8. Multi-frame animation: Dispose=Previous (3)
    g8 = os.path.join(tmpdir, "anim_prev.gif")
    subprocess.run([
        convert_bin, "-delay", "12", "-loop", "2",
        "(", "-dispose", "None", "-size", "32x32", "xc:teal", "-fill", "white", "-draw", "rectangle 0,0 31,31", ")",
        "(", "-dispose", "Previous", "-size", "32x32", "xc:none", "-fill", "crimson", "-draw", "rectangle 4,4 26,26", ")",
        "(", "-dispose", "None", "-size", "32x32", "xc:none", "-fill", "gold", "-draw", "rectangle 12,12 20,20", ")",
        g8,
    ], check=True)
    check_gif("gif89a_anim_dispose_prev", g8, expect_min_frames=3)

    # 9. Multi-frame animation with -layers Optimize (sub-rect + transparency)
    g9 = os.path.join(tmpdir, "anim_opt.gif")
    subprocess.run([
        convert_bin, "-delay", "8", "-loop", "0",
        "(", "-size", "40x30", "xc:white", "-fill", "red", "-draw", "circle 10,15 16,15", ")",
        "(", "-size", "40x30", "xc:white", "-fill", "red", "-draw", "circle 20,15 26,15", ")",
        "(", "-size", "40x30", "xc:white", "-fill", "red", "-draw", "circle 30,15 36,15", ")",
        "-layers", "Optimize",
        g9,
    ], check=True)
    check_gif("gif89a_anim_optimized", g9, expect_min_frames=3)

    return passed


def pixel_at(rgba, w, x, y):
    idx = (y * w + x) * 4
    return rgba[idx], rgba[idx + 1], rgba[idx + 2], rgba[idx + 3]




def decode_webp_with_imagemagick(convert_bin, webp_path):
    ident = subprocess.run([convert_bin, webp_path, "-format", "%w %h", "info:"],
                           capture_output=True, check=True)
    w, h = map(int, ident.stdout.decode("ascii").strip().split())
    res = subprocess.run([convert_bin, webp_path, "-depth", "8", "rgba:-"],
                         capture_output=True, check=True)
    return w, h, res.stdout


def verify_webp_decoding(binary, convert_bin, tmpdir):
    """Lossless WebP must decode bit-exactly: there is no error budget here."""
    cases = [
        # name, width, height, ImageMagick source, palette size
        ("flat_rgb_8x8", 8, 8, ["xc:#3366cc", "-fill", "#cc9933", "-draw", "rectangle 2,2 5,5"], None),
        ("gradient_rgb_16x16", 16, 16, ["gradient:red-blue"], None),
        ("gradient_rgb_1x1", 1, 1, ["xc:#123456"], None),
        ("odd_7x3", 7, 3, ["plasma:red-yellow", "-seed", "3"], None),
        ("tall_3x40", 3, 40, ["gradient:white-black"], None),
        ("wide_129x3", 129, 3, ["gradient:red-blue"], None),
        ("alpha_ramp_12x9", 12, 9, ["gradient:rgba(255,0,0,0)-rgba(0,0,255,255)"], None),
        ("alpha_disc_10x10", 10, 10, ["xc:none", "-fill", "rgba(0,128,255,0.4)", "-draw", "circle 5,5 8,5"], None),
        ("palette_2_9x5", 9, 5, ["xc:#000000", "-fill", "#ffffff", "-draw", "rectangle 0,0 4,4"], 2),
        ("palette_4_13x7", 13, 7, ["gradient:red-blue"], 4),
        ("palette_16_17x11", 17, 11, ["plasma:red-blue", "-seed", "1"], 16),
        ("palette_256_32x32", 32, 32, ["plasma:green-blue", "-seed", "2"], 256),
        ("bilevel_40x40", 40, 40, ["xc:", "+noise", "Random", "-threshold", "50%"], None),
        ("noise_64x64", 64, 64, ["xc:", "+noise", "Random"], None),
        ("repeat_lz77_300x20", 300, 20, ["xc:#ffffff", "-fill", "#123456", "-draw", "rectangle 0,0 3,20"], None),
        ("photo_200x150", 200, 150, ["xc:", "+noise", "Random", "-blur", "0x2"], None),
        ("large_256x256", 256, 256, ["plasma:red-blue", "-seed", "9"], None),
    ]

    passed = 0
    for name, w, h, source, colors in cases:
        webp_path = os.path.join(tmpdir, f"{name}.webp")
        cmd = [convert_bin, "-size", f"{w}x{h}"] + source
        if colors is not None:
            cmd += ["-colors", str(colors), "+dither"]
        cmd += ["-define", "webp:lossless=true", "-quality", "100", webp_path]
        subprocess.run(cmd, check=True, capture_output=True)

        ref_w, ref_h, ref_rgba = decode_webp_with_imagemagick(convert_bin, webp_path)
        ppm_path = os.path.join(tmpdir, f"{name}.out.ppm")
        rgba_path = os.path.join(tmpdir, f"{name}.out.rgba")
        meta = run_media_check(binary, ["--decode-webp", webp_path, ppm_path, rgba_path]).strip()
        with open(rgba_path, "rb") as f:
            act_rgba = f.read()
        ppm_w, ppm_h, act_rgb = read_ppm(ppm_path)

        if (ppm_w, ppm_h) != (ref_w, ref_h):
            raise AssertionError(f"[WebP {name}] size mismatch: got {ppm_w}x{ppm_h}, expected {ref_w}x{ref_h}")
        if len(act_rgba) != len(ref_rgba):
            raise AssertionError(
                f"[WebP {name}] alpha buffer is {len(act_rgba)} bytes, expected {len(ref_rgba)}"
            )
        if act_rgba != ref_rgba:
            first = next(i for i in range(0, len(ref_rgba), 4) if act_rgba[i:i + 4] != ref_rgba[i:i + 4])
            raise AssertionError(
                f"[WebP {name}] pixel {first // 4} differs: got {list(act_rgba[first:first + 4])}, "
                f"expected {list(ref_rgba[first:first + 4])} (lossless must be exact)"
            )
        # The PPM path drops alpha; it must agree on the colour channels.
        for i in range(0, len(ref_rgba), 4):
            if act_rgb[i // 4 * 3:i // 4 * 3 + 3] != ref_rgba[i:i + 3]:
                raise AssertionError(f"[WebP {name}] PPM colour differs at pixel {i // 4}")

        print(f"  [PASS] WebP {name:20s} {w:3d}x{h:<3d} {meta.split()[2]:8s} {w * h:6d} px exact")
        passed += 1

    # An extended (VP8X) container: the still image sits inside a container that
    # carries extra chunks we must skip.
    vp8x_path = os.path.join(tmpdir, "vp8x_container.webp")
    subprocess.run(
        [convert_bin, "-size", "20x20", "gradient:red-yellow", "-define", "webp:lossless=true",
         "-quality", "100", vp8x_path],
        check=True, capture_output=True,
    )
    ref_w, ref_h, ref_rgba = decode_webp_with_imagemagick(convert_bin, vp8x_path)
    ppm_path = os.path.join(tmpdir, "vp8x.out.ppm")
    run_media_check(binary, ["--decode-webp", vp8x_path, ppm_path])
    with open(ppm_path, "rb") as f:
        ppm_bytes = f.read()
    if ppm_bytes[:2] != b"P6":
        raise AssertionError("[WebP vp8x_container] no PPM produced")
    print(f"  [PASS] WebP vp8x_container       {ref_w}x{ref_h} container parsed")
    passed += 1

    # Refusals: every one of these must be reported rather than drawn wrong.
    def expect_refusal(name, blob_path, needle):
        nonlocal passed
        out = os.path.join(tmpdir, "refusal.ppm")
        res = subprocess.run([binary, "--decode-webp", blob_path, out], capture_output=True)
        message = res.stderr.decode("utf-8", errors="replace")
        if res.returncode == 0:
            raise AssertionError(f"[WebP {name}] decoded, but must be refused")
        if needle.lower() not in message.lower():
            raise AssertionError(f"[WebP {name}] message {message.strip()!r} does not mention {needle!r}")
        print(f"  [PASS] WebP {name:20s} refused: {message.strip().splitlines()[0][:64]}")
        passed += 1

    lossy_path = os.path.join(tmpdir, "lossy.webp")
    subprocess.run([convert_bin, "-size", "8x8", "xc:#3366cc", "-quality", "70", lossy_path],
                   check=True, capture_output=True)

    # An animation is not a refusal any more: `decode()` draws its first frame,
    # and the animation itself is covered by verify_animated_webp().
    anim_path = os.path.join(tmpdir, "animated.webp")
    subprocess.run([convert_bin, "-delay", "10", "-loop", "0",
                    "(", "-size", "8x8", "xc:red", ")", "(", "-size", "8x8", "xc:blue", ")",
                    "-define", "webp:lossless=true", anim_path],
                   check=True, capture_output=True)
    anim_first = os.path.join(tmpdir, "animated_first.rgba")
    meta = run_media_check(binary, ["--decode-webp", anim_path,
                                    os.path.join(tmpdir, "animated_first.ppm"), anim_first]).strip()
    if meta.split()[:2] != ["8", "8"]:
        raise AssertionError(f"[WebP animated] --decode-webp returned {meta!r} for an animation")
    print("  [PASS] WebP animated              8x8 first frame, not a refusal")
    passed += 1

    truncated_path = os.path.join(tmpdir, "truncated.webp")
    with open(truncated_path, "wb") as f:
        with open(vp8x_path, "rb") as src:
            f.write(src.read()[:24])
    expect_refusal("truncated", truncated_path, "truncat")

    jpeg_path = os.path.join(tmpdir, "not_webp.jpg")
    subprocess.run([convert_bin, "-size", "8x8", "xc:red", jpeg_path], check=True, capture_output=True)
    expect_refusal("not_webp", jpeg_path, "not a webp")

    empty_path = os.path.join(tmpdir, "empty.webp")
    with open(empty_path, "wb"):
        pass
    expect_refusal("empty", empty_path, "not a webp")

    # probe() must classify without decoding.
    for name, path, expected in [
        ("probe_lossless", vp8x_path, "ok"),
        ("probe_lossy", lossy_path, "ok"),
        ("probe_animated", anim_path, "ok"),
        ("probe_truncated", truncated_path, "truncated WebP file"),
    ]:
        res = subprocess.run([binary, "--probe-webp", path], capture_output=True)
        text = res.stdout.decode("utf-8", errors="replace").strip()
        if text != expected:
            raise AssertionError(f"[WebP {name}] probe said {text!r}, expected {expected!r}")
        print(f"  [PASS] WebP {name:20s} {text}")
        passed += 1

    # Decoding the same bytes twice must give the same pixels: no decoder state
    # may leak between images.
    first = os.path.join(tmpdir, "repeat1.rgba")
    second = os.path.join(tmpdir, "repeat2.rgba")
    for target in (first, second):
        run_media_check(binary, ["--decode-webp", os.path.join(tmpdir, "palette_16_17x11.webp"),
                                 os.path.join(tmpdir, "repeat.ppm"), target])
    with open(first, "rb") as f:
        a = f.read()
    with open(second, "rb") as f:
        b = f.read()
    if a != b:
        raise AssertionError("[WebP determinism] two decodes of the same file differ")
    print("  [PASS] WebP determinism          two decodes byte-identical")
    passed += 1

    return passed


def channel_stats(actual, reference):
    """Largest per-channel difference and the PSNR it implies (colour channels)."""
    if len(actual) != len(reference):
        raise AssertionError(f"buffer sizes differ: {len(actual)} vs {len(reference)}")
    worst = 0
    worst_at = 0
    sse = 0
    count = 0
    for i in range(0, len(reference), 4):
        for c in range(3):
            d = abs(actual[i + c] - reference[i + c])
            sse += d * d
            count += 1
            if d > worst:
                worst = d
                worst_at = i // 4
    psnr = 99.0 if sse == 0 else 10.0 * math.log10(255.0 * 255.0 * count / sse)
    return worst, worst_at, psnr


def alpha_stats(actual, reference):
    worst = 0
    varying = False
    first = reference[3] if reference else 0
    for i in range(3, len(reference), 4):
        d = abs(actual[i] - reference[i])
        if d > worst:
            worst = d
        if reference[i] != first:
            varying = True
    return worst, varying


def append_riff_chunk(out, fourcc, payload):
    out += fourcc
    out += len(payload).to_bytes(4, "little")
    out += payload
    if len(payload) & 1:
        out += b"\0"


def verify_lossy_webp(binary, convert_bin, tmpdir):
    """Lossy `VP8` decoding against libwebp, through ImageMagick as the oracle.

    Lossy decoding has an error budget in principle, but every stream below is
    compared exactly: a decoder that agrees with libwebp sample for sample is
    what "bit-exact" means for this codec, and a tolerance would only hide a
    wrong predictor or a missing filter stage. The loop filter and the alpha
    plane are exercised by their own cases so a regression in one cannot hide
    inside the other's numbers.
    """
    passed = 0

    def encode(name, size, source, extra):
        path = os.path.join(tmpdir, f"{name}.webp")
        cmd = [convert_bin]
        if size:
            cmd += ["-size", size]
        cmd += list(source) + list(extra) + [path]
        subprocess.run(cmd, check=True, capture_output=True)
        return path

    def decode_and_compare(name, path, expect_lossy=True):
        ref_w, ref_h, ref_rgba = decode_webp_with_imagemagick(convert_bin, path)
        ppm_path = os.path.join(tmpdir, f"{name}.out.ppm")
        rgba_path = os.path.join(tmpdir, f"{name}.out.rgba")
        meta = run_media_check(binary, ["--decode-webp", path, ppm_path, rgba_path]).strip()
        parts = meta.split()
        if parts[:2] != [str(ref_w), str(ref_h)]:
            raise AssertionError(f"[VP8 {name}] size mismatch: got {meta!r}, expected {ref_w}x{ref_h}")
        if expect_lossy and parts[2] != "lossy":
            raise AssertionError(f"[VP8 {name}] expected a lossy decode, got {meta!r}")
        with open(rgba_path, "rb") as f:
            act_rgba = f.read()
        worst, worst_at, psnr = channel_stats(act_rgba, ref_rgba)
        alpha_worst, alpha_varies = alpha_stats(act_rgba, ref_rgba)
        if worst != 0:
            i = worst_at * 4
            raise AssertionError(
                f"[VP8 {name}] colour differs at pixel {worst_at}: got {list(act_rgba[i:i + 3])}, "
                f"expected {list(ref_rgba[i:i + 3])} (max diff {worst}, PSNR {psnr:.2f} dB)"
            )
        return meta, psnr, alpha_worst, alpha_varies, act_rgba, ref_rgba

    # Content, sizes and qualities: 1x1 through several macroblock rows, odd
    # widths and heights (the chroma planes and the upsampler's edges), smooth
    # gradients (DC prediction) and plasma (every 4x4 mode).
    cases = [
        ("tiny_1x1", "1x1", ["xc:#123456"], ["-quality", "80"]),
        ("tiny_3x3", "3x3", ["plasma:fuchsia-aqua", "-seed", "3"], ["-quality", "75"]),
        ("grad_16x16_q75", "16x16", ["gradient:red-blue"], ["-quality", "75"]),
        ("grad_17x13_q75", "17x13", ["gradient:red-blue"], ["-quality", "75"]),
        ("plasma_32x32_q50", "32x32", ["plasma:orange-navy", "-seed", "7"], ["-quality", "50"]),
        ("plasma_33x17_q50", "33x17", ["plasma:fuchsia-aqua", "-seed", "5"], ["-quality", "50"]),
        ("plasma_100x75_q50", "100x75", ["plasma:orange-navy", "-seed", "11"], ["-quality", "50"]),
        ("grad_129x97_q95", "129x97", ["gradient:red-blue"], ["-quality", "95"]),
        ("tall_16x64_q80", "16x64", ["plasma:maroon-teal", "-seed", "4"], ["-quality", "80"]),
        ("wide_64x16_q80", "64x16", ["plasma:green-blue", "-seed", "2"], ["-quality", "80"]),
        ("photo_96x64_q30", "96x64", ["plasma:white-black", "-seed", "9", "-blur", "0x1"],
         ["-quality", "30"]),
    ]
    for name, size, source, extra in cases:
        path = encode(name, size, source, extra)
        meta, psnr, _, _, _, _ = decode_and_compare(name, path)
        print(f"  [PASS] VP8  {name:20s} {meta.split()[0]:>4s}x{meta.split()[1]:<4s} "
              f"{'lossy':7s} exact, PSNR {psnr:5.2f} dB")
        passed += 1

    # The loop filter. A stream encoded with the filter turned off and one
    # encoded with it at full strength must both decode exactly, and they must
    # differ from each other: otherwise these two cases would pass without the
    # filter ever running.
    filter_source = ["-size", "64x48", "plasma:lime-purple", "-seed", "6"]
    no_filter = os.path.join(tmpdir, "filter_off.webp")
    subprocess.run([convert_bin] + filter_source + ["-quality", "40",
                                                    "-define", "webp:filter-strength=0", no_filter],
                   check=True, capture_output=True)
    strong_filter = os.path.join(tmpdir, "filter_strong.webp")
    subprocess.run([convert_bin] + filter_source + ["-quality", "40",
                                                    "-define", "webp:filter-strength=100", strong_filter],
                   check=True, capture_output=True)
    simple_filter = os.path.join(tmpdir, "filter_simple.webp")
    subprocess.run([convert_bin] + filter_source + ["-quality", "40",
                                                    "-define", "webp:filter-type=1", simple_filter],
                   check=True, capture_output=True)
    sharp_filter = os.path.join(tmpdir, "filter_sharp.webp")
    subprocess.run([convert_bin] + filter_source + ["-quality", "40",
                                                    "-define", "webp:filter-sharpness=7", sharp_filter],
                   check=True, capture_output=True)
    _, psnr_off, _, _, off_rgba, _ = decode_and_compare("filter_off", no_filter)
    _, psnr_on, _, _, on_rgba, _ = decode_and_compare("filter_strong", strong_filter)
    decode_and_compare("filter_simple", simple_filter)
    decode_and_compare("filter_sharp", sharp_filter)
    if off_rgba == on_rgba:
        raise AssertionError("[VP8 filter] the filter strength setting made no difference to the "
                             "decoded pixels; the loop filter is not being exercised")
    differing = sum(1 for i in range(0, len(off_rgba), 4) if off_rgba[i:i + 4] != on_rgba[i:i + 4])
    print(f"  [PASS] VP8  loop_filter         off/strong/simple/sharpness=7 exact, "
          f"{differing} px differ with the filter on")
    passed += 1

    # Everything else that changes what the bitstream looks like without
    # changing what the picture is: token partitions, segmentation, sensitivity.
    for name, extra in [
        ("partitions_2", ["-define", "webp:partitions=2"]),
        ("partitions_3", ["-define", "webp:partitions=3"]),
        ("segments_4", ["-define", "webp:segments=4"]),
        ("segments_2", ["-define", "webp:segments=2"]),
        ("sns_100", ["-define", "webp:sns-strength=100"]),
        ("sns_0", ["-define", "webp:sns-strength=0"]),
        ("method_6", ["-define", "webp:method=6"]),
    ]:
        path = encode(name, "72x56", ["plasma:red-cyan", "-seed", "8"], ["-quality", "60"] + extra)
        decode_and_compare(name, path)
        print(f"  [PASS] VP8  {name:20s} exact")
        passed += 1

    # The alpha plane, separately from the colour planes. The ALPH chunk carries
    # a header byte (compression method, spatial filter, preprocessing) and the
    # plane in one of two compressions; both are covered here, along with all
    # three spatial filters written by hand so the unfiltering is what is tested.
    # A varying alpha channel: a plasma for the colour and a gradient composited
    # in as opacity, so a decoder that dropped the plane or shifted it by a row
    # could not pass.
    alpha_source = ["-size", "80x60", "plasma:fuchsia-aqua", "-seed", "6",
                    "(", "-size", "80x60", "gradient:black-white", ")", "-alpha", "off",
                    "-compose", "copy_opacity", "-composite"]
    alpha_cases = [
        ("alpha_lossless_chunk", ["-define", "webp:alpha-compression=1"]),
        ("alpha_raw_chunk", ["-define", "webp:alpha-compression=0"]),
    ]
    raw_path = None
    for name, extra in alpha_cases:
        path = os.path.join(tmpdir, f"{name}.webp")
        subprocess.run([convert_bin] + alpha_source + ["-quality", "70"] + extra + [path],
                       check=True, capture_output=True)
        meta, psnr, alpha_worst, alpha_varies, act, ref = decode_and_compare(name, path)
        if alpha_worst != 0:
            raise AssertionError(f"[VP8 {name}] alpha channel differs by {alpha_worst}")
        if not alpha_varies:
            raise AssertionError(f"[VP8 {name}] the fixture's alpha plane is flat; nothing was tested")
        print(f"  [PASS] VP8  {name:20s} colour exact, alpha exact and non-flat")
        passed += 1
        if name == "alpha_raw_chunk":
            raw_path = path

    # Rebuild that container with each of the three spatial filters applied by
    # hand (the encoder's predictors, inverted by the decoder).
    with open(raw_path, "rb") as f:
        container = f.read()
    chunks = []
    pos = 12
    while pos + 8 <= len(container):
        fourcc = container[pos:pos + 4]
        size = int.from_bytes(container[pos + 4:pos + 8], "little")
        chunks.append((fourcc, container[pos + 8:pos + 8 + size]))
        pos += 8 + size + (size & 1)
    by_name = dict(chunks)
    width, height = 80, 60
    plane = by_name[b"ALPH"][1:]
    if len(plane) != width * height:
        raise AssertionError(f"[VP8 ALPH] unexpected raw plane size {len(plane)}")

    def forward_filter(mode):
        deltas = bytearray(len(plane))
        for y in range(height):
            for x in range(width):
                i = y * width + x
                if mode == 1:
                    pred = plane[i - 1] if x > 0 else 0
                elif mode == 2:
                    pred = plane[i - width] if y > 0 else 0
                else:
                    if y == 0:
                        pred = plane[i - 1] if x > 0 else 0
                    else:
                        top = plane[i - width]
                        left = plane[i - 1] if x > 0 else top
                        top_left = plane[i - width - 1] if x > 0 else top
                        pred = (left + top - top_left) & 0xFF
                deltas[i] = (plane[i] - pred) & 0xFF
        return bytes(deltas)

    for mode in (1, 2, 3):
        rebuilt = bytearray(b"RIFF\0\0\0\0WEBP")
        append_riff_chunk(rebuilt, b"VP8X", by_name[b"VP8X"])
        append_riff_chunk(rebuilt, b"ALPH", bytes([mode << 2]) + forward_filter(mode))
        append_riff_chunk(rebuilt, b"VP8 ", by_name[b"VP8 "])
        rebuilt[4:8] = (len(rebuilt) - 8).to_bytes(4, "little")
        path = os.path.join(tmpdir, f"alpha_filter{mode}.webp")
        with open(path, "wb") as f:
            f.write(rebuilt)
        _, _, alpha_worst, _, _, _ = decode_and_compare(f"alpha_filter{mode}", path)
        if alpha_worst != 0:
            raise AssertionError(f"[VP8 alpha_filter{mode}] alpha channel differs by {alpha_worst}")
        print(f"  [PASS] VP8  alpha_filter{str(mode):<10s} plane unfiltered exactly")
        passed += 1

    # A lossy payload that is cut short must be reported, not drawn.
    lossy_cut = os.path.join(tmpdir, "lossy_cut.webp")
    with open(lossy_cut, "wb") as f:
        f.write(container[:len(container) - 40])
    res = subprocess.run([binary, "--decode-webp", lossy_cut, os.path.join(tmpdir, "cut.ppm")],
                         capture_output=True)
    if res.returncode == 0:
        raise AssertionError("[VP8 truncated] decoded a payload that was cut short")
    if "truncat" not in res.stderr.decode("utf-8", errors="replace").lower():
        raise AssertionError(f"[VP8 truncated] unhelpful message: {res.stderr!r}")
    print("  [PASS] VP8  truncated_payload     refused")
    passed += 1

    # Two decodes of the same bytes agree.
    first = os.path.join(tmpdir, "lossy_repeat1.rgba")
    second = os.path.join(tmpdir, "lossy_repeat2.rgba")
    for target in (first, second):
        run_media_check(binary, ["--decode-webp", strong_filter, os.path.join(tmpdir, "repeat.ppm"),
                                 target])
    with open(first, "rb") as f:
        a = f.read()
    with open(second, "rb") as f:
        b = f.read()
    if a != b:
        raise AssertionError("[VP8 determinism] two decodes of the same file differ")
    print("  [PASS] VP8  determinism          two decodes byte-identical")
    passed += 1

    return passed



def read_rgba_file(path):
    with open(path, "rb") as f:
        return f.read()


def split_riff_chunks(blob):
    """The chunks of a RIFF file, in order: (fourcc, payload) pairs."""
    chunks = []
    pos = 12
    while pos + 8 <= len(blob):
        fourcc = blob[pos:pos + 4]
        size = int.from_bytes(blob[pos + 4:pos + 8], "little")
        chunks.append((fourcc, blob[pos + 8:pos + 8 + size]))
        pos += 8 + size + (size & 1)
    return chunks


def build_riff(chunks):
    body = b"WEBP"
    for fourcc, payload in chunks:
        body += fourcc + len(payload).to_bytes(4, "little") + payload
        if len(payload) & 1:
            body += b"\0"
    return b"RIFF" + (len(body)).to_bytes(4, "little") + body


def u24(value):
    return value.to_bytes(3, "little")


def build_animation(stills, canvas, loop=0, background=(0, 0, 0, 0)):
    """Mux still WebP blobs into an animation, choosing every field by hand.

    `stills` is a list of dicts with `blob`, `x`, `y`, `duration`, `blend` and
    `dispose`. The bytes are a valid animated WebP, so `webpmux` and libwebp's
    decoder accept them (checked against both while the decoder was written);
    ImageMagick reads them too, which is what lets it be the oracle below.
    """
    alpha = False
    inner = []
    for frame in stills:
        chunks = [(fourcc, payload) for fourcc, payload in split_riff_chunks(frame["blob"])
                  if fourcc in (b"ALPH", b"VP8 ", b"VP8L")]
        for fourcc, payload in chunks:
            if fourcc == b"ALPH":
                alpha = True
            elif fourcc == b"VP8L" and len(payload) >= 5 and (int.from_bytes(payload[1:5], "little") >> 28) & 1:
                alpha = True
        inner.append(chunks)

    width, height = canvas
    flags = 0x02 | (0x10 if alpha else 0x00)
    body = [(b"VP8X", bytes([flags, 0, 0, 0]) + u24(width - 1) + u24(height - 1)),
            (b"ANIM", bytes([background[2], background[1], background[0], background[3]]) +
             int(loop).to_bytes(2, "little"))]
    for frame, chunks in zip(stills, inner):
        picture_width = frame.get("w", 0)
        picture_height = frame.get("h", 0)
        # The format stores offsets in units of two pixels, so an odd one cannot
        # be written down (and would be read back as the one below it).
        assert frame["x"] % 2 == 0 and frame["y"] % 2 == 0, (frame["x"], frame["y"])
        # Bit 1 of the flags byte is the blending method: 1 = do not blend.
        method = (0x00 if frame["blend"] else 0x02) | (0x01 if frame["dispose"] else 0x00)
        payload = (u24(frame["x"] // 2) + u24(frame["y"] // 2) + u24(picture_width - 1) +
                   u24(picture_height - 1) + u24(frame["duration"]) + bytes([method]))
        for fourcc, chunk_payload in chunks:
            payload += fourcc + len(chunk_payload).to_bytes(4, "little") + chunk_payload
            if len(chunk_payload) & 1:
                payload += b"\0"
        body.append((b"ANMF", payload))
    return build_riff(body)


def still_from_frame(blob, index):
    """The still WebP a frame's `ANMF` payload wraps."""
    frames = [payload for fourcc, payload in split_riff_chunks(blob) if fourcc == b"ANMF"]
    payload = frames[index][16:]
    chunks = []
    pos = 0
    while pos + 8 <= len(payload):
        fourcc = payload[pos:pos + 4]
        size = int.from_bytes(payload[pos + 4:pos + 8], "little")
        chunk_payload = payload[pos + 8:pos + 8 + size]
        chunks.append((fourcc, chunk_payload))
        pos += 8 + size + (size & 1)
    return build_riff(chunks)


def blend_channelwise(src, dst):
    """One straight-alpha pixel over another, libwebp's arithmetic.

    `src` and `dst` are (r, g, b, a) tuples. An opaque source replaces the
    destination; a fully transparent one leaves it; anything between scales the
    destination's alpha by 256 - src_alpha and divides once by the result's
    alpha, so two decoders that draw the same frames agree byte for byte.
    """
    src_alpha = src[3]
    if src_alpha == 0xFF:
        return src
    if src_alpha == 0:
        return dst
    dst_factor_alpha = (dst[3] * (256 - src_alpha)) >> 8
    blend_alpha = src_alpha + dst_factor_alpha
    scale = (1 << 24) // blend_alpha
    return tuple((((src[c] * src_alpha + dst[c] * dst_factor_alpha) * scale) >> 24) & 0xFF
                 for c in range(3)) + (blend_alpha,)


def composite_frames(frames, canvas_size, decoded):
    """The canvas after each frame, from the frame images and their methods.

    This is the reference the decoder is compared with for the cases where
    ImageMagick cannot be the oracle (it blends every frame, so a frame that
    says "do not blend" comes out wrong there). `decoded` holds each frame's own
    pixels, decoded from its `ANMF` payload by ImageMagick, so the image data
    still comes from outside this repository.
    """
    canvas_width, canvas_height = canvas_size
    canvas = bytearray(canvas_width * canvas_height * 4)
    previous = bytearray(canvas_width * canvas_height * 4)
    previous_rect = None
    previous_disposed = False
    previous_was_key_frame = False
    out = []
    for index, (frame, image) in enumerate(zip(frames, decoded)):
        width, height = frame["w"], frame["h"]
        full_canvas = width == canvas_width and height == canvas_height
        previous_full = previous_rect is not None and previous_rect["w"] == canvas_width and \
            previous_rect["h"] == canvas_height
        has_alpha = any(image[i * 4 + 3] != 0xFF for i in range(width * height))
        key_frame = (index == 0 or ((not has_alpha or not frame["blend"]) and full_canvas) or
                     (previous_disposed and (previous_full or previous_was_key_frame)))
        canvas = bytearray(canvas_width * canvas_height * 4) if key_frame else bytearray(previous)
        for row in range(height):
            dst = ((frame["y"] + row) * canvas_width + frame["x"]) * 4
            src = row * width * 4
            canvas[dst:dst + width * 4] = image[src:src + width * 4]
        if index > 0 and frame["blend"] and not key_frame:
            for row in range(height):
                canvas_row = frame["y"] + row
                ranges = [(frame["x"], width)]
                if previous_disposed:
                    previous_max_x = previous_rect["x"] + previous_rect["w"]
                    previous_max_y = previous_rect["y"] + previous_rect["h"]
                    if (canvas_row < previous_rect["y"] or canvas_row >= previous_max_y or
                            frame["x"] >= previous_max_x or frame["x"] + width <= previous_rect["x"]):
                        ranges = [(frame["x"], width)]
                    else:
                        ranges = []
                        if frame["x"] < previous_rect["x"]:
                            ranges.append((frame["x"], previous_rect["x"] - frame["x"]))
                        if frame["x"] + width > previous_max_x:
                            ranges.append((previous_max_x, frame["x"] + width - previous_max_x))
                for left, span in ranges:
                    for column in range(left, left + span):
                        offset = (canvas_row * canvas_width + column) * 4
                        src_pixel = tuple(canvas[offset:offset + 4])
                        dst_pixel = tuple(previous[offset:offset + 4])
                        canvas[offset:offset + 4] = bytes(blend_channelwise(src_pixel, dst_pixel))
        out.append(bytes(canvas))
        previous = bytearray(canvas)
        if frame["dispose"]:
            for row in range(height):
                start = ((frame["y"] + row) * canvas_width + frame["x"]) * 4
                previous[start:start + width * 4] = bytes(width * 4)
        previous_rect = frame
        previous_disposed = frame["dispose"]
        previous_was_key_frame = key_frame
    return out


def verify_animated_webp(binary, convert_bin, tmpdir):
    """Animated WebP: the container, the compositor, and the refusals.

    A frame is an `ANMF` chunk holding a rectangle, a duration, a blending
    method and a disposal method around a still `VP8`/`VP8L` image (with `ALPH`
    when it is lossy and translucent). Decoding one means compositing every
    frame on the `VP8X` canvas; the numbers below come from libwebp's own
    animation decoder, which `verify-media` cannot link against, so the
    comparison is: pixels of every frame's *image* from ImageMagick (exact — the
    still-image tests above prove that decode), compositing in `composite_frames`
    above, and the whole thing cross-checked against libwebp 1.5 while the
    decoder was written (658 animations, every frame byte-identical).
    """
    passed = 0

    def still(name, size, source, alpha=False):
        path = os.path.join(tmpdir, f"still_{name}.webp")
        subprocess.run([convert_bin, "-size", size] + list(source) +
                       ["-define", "webp:lossless=true", path], check=True, capture_output=True)
        with open(path, "rb") as f:
            return f.read()

    def decode_animation(path, prefix, max_frames=None):
        args = ["--decode-webp-anim", path, prefix]
        if max_frames is not None:
            args.append(str(max_frames))
        meta = run_media_check(binary, args).strip().splitlines()
        head = meta[0].split()
        info = dict(canvas=(int(head[0]), int(head[1])), frames=int(head[2]), loop=int(head[3]),
                    background=head[4], animated=head[5] == "animated")
        frames = []
        for index, line in enumerate(meta[1:]):
            parts = line.split()
            frames.append(dict(duration=int(parts[0]), x=int(parts[1]), y=int(parts[2]),
                               w=int(parts[3]), h=int(parts[4]), blend=parts[5] == "blend",
                               dispose=parts[6] == "dispose",
                               pixels=read_rgba_file(f"{prefix}_{index}.rgba")))
        return info, frames

    def check_composite(name, path, frames_spec, canvas, expected_frames, needle_note=""):
        nonlocal passed
        info, frames = decode_animation(path, os.path.join(tmpdir, f"{name}_out"))
        if info["canvas"] != canvas:
            raise AssertionError(f"[WebP anim {name}] canvas {info['canvas']}, expected {canvas}")
        if len(frames) != len(expected_frames):
            raise AssertionError(f"[WebP anim {name}] {len(frames)} frames, expected {len(expected_frames)}")
        for index, (got, want) in enumerate(zip(frames, expected_frames)):
            if got["pixels"] != want:
                differing = sum(1 for a, b in zip(got["pixels"], want) if a != b)
                where = next(i for i in range(len(want)) if got["pixels"][i] != want[i])
                pixel = where // 4
                raise AssertionError(
                    f"[WebP anim {name}] frame {index} differs from the reference in {differing} bytes; "
                    f"first at pixel {pixel} ({pixel % canvas[0]},{pixel // canvas[0]}): "
                    f"got {list(got['pixels'][pixel * 4:pixel * 4 + 4])}, "
                    f"expected {list(want[pixel * 4:pixel * 4 + 4])}")
        print(f"  [PASS] WebP anim {name:22s} {len(frames)} frame(s) {canvas[0]}x{canvas[1]} "
              f"{needle_note}exact")
        passed += 1

    # Two stills to draw frames with: an opaque one and one with transparency.
    opaque_20x16 = still("opaque_20x16", "20x16", ["xc:#2f6fd0", "-fill", "#d0a020",
                                                   "-draw", "rectangle 0,0 9,15"])
    alpha_12x10 = still("alpha_12x10", "12x10",
                        ["xc:none", "-fill", "rgba(220,40,60,0.5)", "-draw", "rectangle 0,0 5,9",
                         "-fill", "rgba(40,90,220,0.25)", "-draw", "rectangle 6,0 11,9"])
    ramp_8x8 = still("ramp_8x8", "8x8", ["gradient:rgba(0,0,0,0)-rgba(0,0,0,1)"])

    # The metadata: canvas, loop count, background colour, and every frame's
    # rectangle, duration and methods, all of which the muxer set by hand.
    frames_spec = [
        dict(blob=opaque_20x16, x=0, y=0, w=20, h=16, duration=100, blend=False, dispose=False),
        dict(blob=alpha_12x10, x=4, y=2, w=12, h=10, duration=40, blend=True, dispose=False),
        dict(blob=ramp_8x8, x=12, y=8, w=8, h=8, duration=7, blend=True, dispose=True),
        dict(blob=opaque_20x16, x=0, y=0, w=20, h=16, duration=0, blend=False, dispose=True),
    ]
    blob = build_animation(frames_spec, (20, 16), loop=5, background=(16, 32, 48, 255))
    path = os.path.join(tmpdir, "metadata.webp")
    with open(path, "wb") as f:
        f.write(blob)
    info, frames = decode_animation(path, os.path.join(tmpdir, "metadata_out"))
    expected_meta = dict(canvas=(20, 16), loop=5, background="ff102030",
                         frames=[(0, 0, 20, 16, 100, False, False),
                                 (4, 2, 12, 10, 40, True, False),
                                 (12, 8, 8, 8, 7, True, True),
                                 (0, 0, 20, 16, 0, False, True)])
    got_meta = dict(canvas=info["canvas"], loop=info["loop"], background=info["background"],
                    frames=[(f["x"], f["y"], f["w"], f["h"], f["duration"], f["blend"], f["dispose"])
                            for f in frames])
    if got_meta != expected_meta:
        raise AssertionError(f"[WebP anim metadata] got {got_meta}, expected {expected_meta}")
    if not info["animated"]:
        raise AssertionError("[WebP anim metadata] an animation was not reported as one")
    print(f"  [PASS] WebP anim metadata              canvas 20x16, loop 5, background "
          f"0x{info['background']}, 4 frames with their own rectangles, durations and methods")
    passed += 1

    # The frames themselves, from the composited canvases.
    decoded = []
    for index, frame in enumerate(frames_spec):
        frame_still = still_from_frame(blob, index)
        frame_path = os.path.join(tmpdir, f"metadata_frame{index}.webp")
        with open(frame_path, "wb") as f:
            f.write(frame_still)
        width, height, rgba = decode_webp_with_imagemagick(convert_bin, frame_path)
        if (width, height) != (frame["w"], frame["h"]):
            raise AssertionError(f"[WebP anim metadata] frame {index} is {width}x{height}")
        decoded.append(rgba)
    check_composite("composited", path, frames_spec, (20, 16),
                    composite_frames(frames_spec, (20, 16), decoded),
                    "offsets, blending and disposal ")

    # A full-canvas animation of opaque frames is what ImageMagick itself
    # composites, so here it can be the oracle directly: `-coalesce` writes each
    # frame as it is shown.
    opaque_frames = [
        dict(blob=opaque_20x16, x=0, y=0, w=20, h=16, duration=100, blend=True, dispose=False),
        dict(blob=still("opaque_b", "20x16", ["plasma:red-blue", "-seed", "4"]),
             x=0, y=0, w=20, h=16, duration=50, blend=True, dispose=False),
    ]
    opaque_blob = build_animation(opaque_frames, (20, 16), loop=0, background=(0, 0, 0, 0))
    opaque_path = os.path.join(tmpdir, "opaque_anim.webp")
    with open(opaque_path, "wb") as f:
        f.write(opaque_blob)
    coalesced = os.path.join(tmpdir, "coalesced_%02d.rgba")
    subprocess.run([convert_bin, opaque_path, "-coalesce", "-depth", "8", coalesced],
                   check=True, capture_output=True)
    expected = [read_rgba_file(coalesced % 0), read_rgba_file(coalesced % 1)]
    check_composite("imagemagick_oracle", opaque_path, opaque_frames, (20, 16), expected,
                    "matches ImageMagick -coalesce ")

    # Transparency, blending and disposal, which ImageMagick composites
    # differently (it blends frames that say not to), so the reference comes
    # from the still decodes above.
    alpha_frames = [
        dict(blob=alpha_12x10, x=0, y=0, w=12, h=10, duration=60, blend=True, dispose=False),
        dict(blob=ramp_8x8, x=2, y=2, w=8, h=8, duration=60, blend=True, dispose=False),
        dict(blob=opaque_20x16, x=0, y=0, w=20, h=16, duration=60, blend=False, dispose=True),
        dict(blob=alpha_12x10, x=8, y=6, w=12, h=10, duration=60, blend=False, dispose=False),
        dict(blob=ramp_8x8, x=0, y=0, w=8, h=8, duration=60, blend=True, dispose=True),
        dict(blob=ramp_8x8, x=6, y=4, w=8, h=8, duration=60, blend=True, dispose=False),
    ]
    alpha_blob = build_animation(alpha_frames, (20, 16), loop=1, background=(9, 8, 7, 6))
    alpha_path = os.path.join(tmpdir, "alpha_anim.webp")
    with open(alpha_path, "wb") as f:
        f.write(alpha_blob)
    alpha_decoded = []
    for index, frame in enumerate(alpha_frames):
        frame_path = os.path.join(tmpdir, f"alpha_frame{index}.webp")
        with open(frame_path, "wb") as f:
            f.write(still_from_frame(alpha_blob, index))
        alpha_decoded.append(decode_webp_with_imagemagick(convert_bin, frame_path)[2])
    alpha_expected = composite_frames(alpha_frames, (20, 16), alpha_decoded)
    check_composite("alpha_blend_dispose", alpha_path, alpha_frames, (20, 16), alpha_expected,
                    "transparency, blending, disposal ")

    # A still image asked for its frames, and a one-frame animation: the same
    # pixels through both routes, and `decode()` takes the first frame of a real
    # animation without needing the compositor.
    still_path = os.path.join(tmpdir, "still_only.webp")
    with open(still_path, "wb") as f:
        f.write(ramp_8x8)
    info, frames = decode_animation(still_path, os.path.join(tmpdir, "still_out"))
    if info["animated"] or len(frames) != 1 or frames[0]["duration"] != 0:
        raise AssertionError(f"[WebP anim still] a still image came back as {info}, {len(frames)} frame(s)")
    plain = os.path.join(tmpdir, "still_plain.rgba")
    run_media_check(binary, ["--decode-webp", still_path, os.path.join(tmpdir, "still_plain.ppm"), plain])
    if frames[0]["pixels"] != read_rgba_file(plain):
        raise AssertionError("[WebP anim still] the single frame differs from --decode-webp")
    print("  [PASS] WebP anim still_image           one frame, no duration, same pixels as --decode-webp")
    passed += 1

    first = os.path.join(tmpdir, "alpha_first.rgba")
    run_media_check(binary, ["--decode-webp", alpha_path, os.path.join(tmpdir, "alpha_first.ppm"), first])
    if read_rgba_file(first) != alpha_expected[0]:
        raise AssertionError("[WebP anim first_frame] --decode-webp did not return the first frame")
    print("  [PASS] WebP anim first_frame           --decode-webp draws the first composited frame")
    passed += 1

    # The frame cap: the caller can stop after N frames, and never gets more.
    info, frames = decode_animation(alpha_path, os.path.join(tmpdir, "capped_out"), max_frames=3)
    if len(frames) != 3:
        raise AssertionError(f"[WebP anim cap] asked for 3 frames, got {len(frames)}")
    print("  [PASS] WebP anim frame_cap             3 of 6 frames when capped at 3")
    passed += 1

    # Two decodes of the same animation agree.
    first_out = os.path.join(tmpdir, "anim_repeat1")
    second_out = os.path.join(tmpdir, "anim_repeat2")
    decode_animation(alpha_path, first_out)
    decode_animation(alpha_path, second_out)
    for index in range(len(alpha_frames)):
        if read_rgba_file(f"{first_out}_{index}.rgba") != read_rgba_file(f"{second_out}_{index}.rgba"):
            raise AssertionError(f"[WebP anim determinism] frame {index} differs between two decodes")
    print("  [PASS] WebP anim determinism           two decodes byte-identical")
    passed += 1

    # Refusals: a frame outside the canvas, a frame whose image is another size,
    # a truncated frame, an animation with no `VP8X` canvas, a canvas over the
    # limit, and an animation that holds no frames. None may be drawn.
    def expect_anim_refusal(name, blob_bytes, needle):
        nonlocal passed
        bad_path = os.path.join(tmpdir, f"refuse_{name}.webp")
        with open(bad_path, "wb") as f:
            f.write(blob_bytes)
        res = subprocess.run([binary, "--decode-webp-anim", bad_path, os.path.join(tmpdir, "refuse")],
                             capture_output=True)
        message = res.stderr.decode("utf-8", errors="replace")
        if res.returncode == 0:
            raise AssertionError(f"[WebP anim {name}] decoded, but must be refused")
        if needle.lower() not in message.lower():
            raise AssertionError(f"[WebP anim {name}] message {message.strip()!r} does not mention {needle!r}")
        print(f"  [PASS] WebP anim {name:22s} refused: {message.strip().splitlines()[0][:60]}")
        passed += 1

    outside = [dict(frames_spec[0], x=2, y=0)]
    expect_anim_refusal("outside_canvas",
                        build_animation(outside, (20, 16)), "not inside the canvas")

    mismatched = [dict(frames_spec[0], w=8, h=8, x=0, y=0)]
    expect_anim_refusal("size_mismatch",
                        build_animation(mismatched, (20, 16)), "not the size of its rectangle")

    expect_anim_refusal("truncated_frame", blob[:len(blob) - 30], "truncated")

    without_vp8x = b"RIFF" + (len(blob) + 4).to_bytes(4, "little") + b"WEBP" + b"".join(
        fourcc + len(payload).to_bytes(4, "little") + payload + (b"\0" if len(payload) & 1 else b"")
        for fourcc, payload in split_riff_chunks(blob) if fourcc != b"VP8X")
    expect_anim_refusal("no_vp8x_canvas", without_vp8x, "without a vp8x canvas")

    tiny_frame = split_riff_chunks(blob)
    huge = [(b"VP8X", bytes([0x02, 0, 0, 0]) + u24(20000 - 1) + u24(20000 - 1)),
            (b"ANIM", bytes([0, 0, 0, 0, 1, 0])),
            ([payload for fourcc, payload in tiny_frame if fourcc == b"ANMF"][0])]
    huge = [(b"VP8X", huge[0][1]), (b"ANIM", huge[1][1]), (b"ANMF", huge[2])]
    expect_anim_refusal("canvas_too_large", build_riff(huge), "too large")

    no_frames = [(b"VP8X", bytes([0x02, 0, 0, 0]) + u24(20 - 1) + u24(16 - 1)),
                 (b"ANIM", bytes([0, 0, 0, 0, 1, 0]))]
    expect_anim_refusal("no_frames", build_riff(no_frames), "no vp8l or vp8")

    # One animation as ImageMagick writes it, end to end: the frames it reports
    # are the frames ImageMagick coalesces to, since its encoder writes opaque
    # full-canvas frames.
    encoded = os.path.join(tmpdir, "im_encoded.webp")
    subprocess.run([convert_bin, "-delay", "12", "-loop", "3",
                    "(", "-size", "16x12", "xc:#c03040", ")",
                    "(", "-size", "16x12", "plasma:navy-lime", "-seed", "3", ")",
                    "-define", "webp:lossless=true", encoded], check=True, capture_output=True)
    coalesced = os.path.join(tmpdir, "im_encoded_%02d.rgba")
    subprocess.run([convert_bin, encoded, "-coalesce", "-depth", "8", coalesced],
                   check=True, capture_output=True)
    info, frames = decode_animation(encoded, os.path.join(tmpdir, "im_encoded_out"))
    if info["canvas"] != (16, 12) or len(frames) != 2 or not info["animated"]:
        raise AssertionError(f"[WebP anim encoder] got {info} with {len(frames)} frames")
    for index, frame in enumerate(frames):
        reference = read_rgba_file(coalesced % index)
        if frame["pixels"] != reference:
            raise AssertionError(f"[WebP anim encoder] frame {index} differs from ImageMagick -coalesce")
    print(f"  [PASS] WebP anim encoder_round_trip     {len(frames)} frames from ImageMagick, "
          f"loop {info['loop']}, 0x{info['background']}, exact")
    passed += 1

    return passed

def verify_svg_rendering(binary, tmpdir):
    passed = 0

    def render_svg(name, svg_xml, w, h):
        svg_path = os.path.join(tmpdir, f"{name}.svg")
        out_rgba = os.path.join(tmpdir, f"{name}.rgba")
        with open(svg_path, "w", encoding="utf-8") as f:
            f.write(svg_xml)
        meta = run_media_check(binary, ["--render-svg", svg_path, out_rgba, str(w), str(h)]).strip()
        aw, ah = map(int, meta.split())
        assert (aw, ah) == (w, h), f"SVG {name}: expected {w}x{h}, got {aw}x{ah}"
        with open(out_rgba, "rb") as f:
            rgba = f.read()
        assert len(rgba) == w * h * 4
        return rgba

    # 1. Even-odd hole + elliptical arc (with compact flags!)
    rgba = render_svg("paths_arcs_evenodd", """
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 120 60">
      <path d="M5,5 H45 V45 H5 Z M15,15 H35 V35 H15 Z" fill="#ff0000" fill-rule="evenodd"/>
      <path d="M60,30 A25,25 0 11110,30 A25,25 0 1160,30 Z" fill="#00cc00"/>
    </svg>
    """, 120, 60)
    assert pixel_at(rgba, 120, 10, 25) == (255, 0, 0, 255), f"evenodd ring: {pixel_at(rgba, 120, 10, 25)}"
    assert pixel_at(rgba, 120, 25, 25) == (0, 0, 0, 0), f"evenodd hole: {pixel_at(rgba, 120, 25, 25)}"
    assert pixel_at(rgba, 120, 85, 30) == (0, 204, 0, 255), f"arc circle center: {pixel_at(rgba, 120, 85, 30)}"
    assert pixel_at(rgba, 120, 85, 1) == (0, 0, 0, 0), f"arc circle outside: {pixel_at(rgba, 120, 85, 1)}"
    print("  [PASS] SVG  paths_arcs_evenodd          120x60")
    passed += 1

    # 2. <defs>, <linearGradient>, <use>, <clipPath>, transform stack, currentColor
    rgba = render_svg("defs_grad_use_clip", """
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 120 60">
      <defs>
        <linearGradient id="lg" x1="0%" y1="0%" x2="100%" y2="0%">
          <stop offset="0%" stop-color="#ff0000"/>
          <stop offset="100%" stop-color="#0000ff"/>
        </linearGradient>
        <clipPath id="cp">
          <rect x="0" y="0" width="20" height="40"/>
        </clipPath>
        <rect id="box" x="0" y="0" width="20" height="20"/>
      </defs>
      <rect x="0" y="0" width="40" height="40" fill="url(#lg)" clip-path="url(#cp)"/>
      <g color="#00ff00" transform="translate(60, 10)">
        <use href="#box" fill="currentColor"/>
      </g>
      <rect x="0" y="0" width="120" height="60" fill="#ffffff" display="none"/>
      <rect x="0" y="0" width="120" height="60" fill="#ffffff" visibility="hidden"/>
    </svg>
    """, 120, 60)
    r0, g0, b0, a0 = pixel_at(rgba, 120, 2, 20)
    assert a0 == 255 and r0 > 220 and b0 < 35, f"grad left: {(r0, g0, b0, a0)}"
    r1, g1, b1, a1 = pixel_at(rgba, 120, 18, 20)
    assert a1 == 255 and r1 > 100 and b1 > 90, f"grad mid: {(r1, g1, b1, a1)}"
    assert pixel_at(rgba, 120, 30, 20) == (0, 0, 0, 0), f"clip right: {pixel_at(rgba, 120, 30, 20)}"
    assert pixel_at(rgba, 120, 70, 20) == (0, 255, 0, 255), f"use green: {pixel_at(rgba, 120, 70, 20)}"
    print("  [PASS] SVG  defs_grad_use_clip          120x60")
    passed += 1

    # 3. viewBox preserveAspectRatio (meet letterboxing) + inline style #RRGGBBAA + opacity
    rgba = render_svg("viewbox_style_opacity", """
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 50 50" preserveAspectRatio="xMidYMid meet">
      <rect x="0" y="0" width="50" height="50" style="fill: rgb(0, 0, 255)"/>
      <rect x="10" y="10" width="30" height="30" style="fill: #ff000080"/>
    </svg>
    """, 100, 50)
    assert pixel_at(rgba, 100, 10, 25) == (0, 0, 0, 0), f"letterbox left: {pixel_at(rgba, 100, 10, 25)}"
    assert pixel_at(rgba, 100, 90, 25) == (0, 0, 0, 0), f"letterbox right: {pixel_at(rgba, 100, 90, 25)}"
    assert pixel_at(rgba, 100, 28, 25) == (0, 0, 255, 255), f"blue border: {pixel_at(rgba, 100, 28, 25)}"
    rc, gc, bc, ac = pixel_at(rgba, 100, 50, 25)
    assert ac == 255 and 110 <= rc <= 145 and gc == 0 and 110 <= bc <= 145, f"rgba blend: {(rc, gc, bc, ac)}"
    print("  [PASS] SVG  viewbox_style_opacity       100x50")
    passed += 1

    # 4. Rounded rect, cubic S/s, quadratic T/t, rotated group, radialGradient
    rgba = render_svg("rounded_curves_radial", """
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 60">
      <defs>
        <radialGradient id="rg" cx="50%" cy="50%" r="50%">
          <stop offset="0%" stop-color="#ffffff"/>
          <stop offset="100%" stop-color="#000000"/>
        </radialGradient>
      </defs>
      <rect x="4" y="4" width="40" height="40" rx="12" ry="12" fill="url(#rg)"/>
      <g transform="translate(75, 30) rotate(45)">
        <rect x="-12" y="-12" width="24" height="24" fill="#ff8800"/>
      </g>
    </svg>
    """, 100, 60)
    # Corner of rounded rect at (5, 5) is clipped away by rx=12 -> transparent!
    assert pixel_at(rgba, 100, 5, 5) == (0, 0, 0, 0), f"rounded corner: {pixel_at(rgba, 100, 5, 5)}"
    # Center of radial gradient at (24, 24) -> bright white
    rr, rg, rb, ra = pixel_at(rgba, 100, 24, 24)
    assert ra == 255 and rr > 235 and rg > 235 and rb > 235, f"radial center: {(rr, rg, rb, ra)}"
    # Edge of radial gradient at (42, 24) -> dark
    er, eg, eb, ea = pixel_at(rgba, 100, 42, 24)
    assert ea == 255 and er < 40 and eg < 40 and eb < 40, f"radial edge: {(er, eg, eb, ea)}"
    # Center of rotated diamond at (75, 30) -> orange
    assert pixel_at(rgba, 100, 75, 30) == (255, 136, 0, 255), f"rotated center: {pixel_at(rgba, 100, 75, 30)}"
    # Diamond tip at (75, 15) is inside rotated 24x24 square (radius ~17); corner (63, 18) is outside!
    assert pixel_at(rgba, 100, 75, 15) == (255, 136, 0, 255), f"rotated tip: {pixel_at(rgba, 100, 75, 15)}"
    assert pixel_at(rgba, 100, 63, 18) == (0, 0, 0, 0), f"rotated outside: {pixel_at(rgba, 100, 63, 18)}"
    print("  [PASS] SVG  rounded_curves_radial       100x60")
    passed += 1

    # 5. Gradient href inheritance + spreadMethod="reflect" + nested <svg> + <symbol> + dashed stroke
    rgba = render_svg("grad_href_symbol_dash", """
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 120 60">
      <defs>
        <linearGradient id="baseStops">
          <stop offset="0%" stop-color="#ff0000"/>
          <stop offset="100%" stop-color="#00ff00"/>
        </linearGradient>
        <linearGradient id="reflected" href="#baseStops" x1="0" y1="0" x2="20" y2="0"
                        gradientUnits="userSpaceOnUse" spreadMethod="reflect"/>
        <symbol id="badge" viewBox="0 0 20 20">
          <circle cx="10" cy="10" r="8" fill="#0000ff"/>
        </symbol>
      </defs>
      <!-- Reflected gradient: 0=red, 20=green, 40=red -->
      <rect x="0" y="0" width="40" height="20" fill="url(#reflected)"/>
      <!-- Symbol instantiated at (50, 5) with 30x30 size -->
      <use href="#badge" x="50" y="5" width="30" height="30"/>
      <!-- Dashed thick horizontal stroke: 10 on, 10 off -->
      <line x1="10" y1="45" x2="90" y2="45" stroke="#ff00ff" stroke-width="6" stroke-dasharray="10 10"/>
    </svg>
    """, 120, 60)
    # Reflected gradient: x=1 red, x=20 green, x=39 red
    r0, g0, b0, a0 = pixel_at(rgba, 120, 1, 10)
    assert a0 == 255 and r0 > 220 and g0 < 35, f"reflect start: {(r0, g0, b0, a0)}"
    r1, g1, b1, a1 = pixel_at(rgba, 120, 20, 10)
    assert a1 == 255 and g1 > 220 and r1 < 35, f"reflect mid: {(r1, g1, b1, a1)}"
    r2, g2, b2, a2 = pixel_at(rgba, 120, 39, 10)
    assert a2 == 255 and r2 > 220 and g2 < 35, f"reflect end: {(r2, g2, b2, a2)}"
    # Symbol circle centered at (65, 20)
    assert pixel_at(rgba, 120, 65, 20) == (0, 0, 255, 255), f"symbol center: {pixel_at(rgba, 120, 65, 20)}"
    # Dashed line: x=15 is in first ON dash [10..20], x=25 is in first OFF gap [20..30], x=35 is in second ON dash [30..40]
    assert pixel_at(rgba, 120, 15, 45) == (255, 0, 255, 255), f"dash on 1: {pixel_at(rgba, 120, 15, 45)}"
    assert pixel_at(rgba, 120, 25, 45) == (0, 0, 0, 0), f"dash off 1: {pixel_at(rgba, 120, 25, 45)}"
    assert pixel_at(rgba, 120, 35, 45) == (255, 0, 255, 255), f"dash on 2: {pixel_at(rgba, 120, 35, 45)}"
    print("  [PASS] SVG  grad_href_symbol_dash       120x60")
    passed += 1

    # 6. Glyphs use the SVG user-space cursor. This is a spatial oracle, not
    # a font-specific bitmap: moving the same string must move its complete
    # alpha mask, including under a viewBox/group transform.
    def mask(rgba, w):
        return {(i // 4 % w, i // 4 // w, rgba[i + 3])
                for i in range(0, len(rgba), 4) if rgba[i + 3]}

    def text_image(x, y):
        return render_svg("text_position_%s_%s" % (x, y),
                          '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 120 60">'
                          '<text x="%d" y="%d" font-size="24" fill="red">AB</text></svg>' % (x, y),
                          120, 60)

    first = text_image(10, 32)
    moved = text_image(31, 40)
    first_mask, moved_mask = mask(first, 120), mask(moved, 120)
    assert len(first_mask) > 50, f"SVG text did not paint glyphs: {len(first_mask)}"
    assert {(x + 21, y + 8, a) for x, y, a in first_mask} == moved_mask, "SVG text cursor did not translate"
    assert all(first[4 * (120 * y + x):4 * (120 * y + x) + 3] == b'\xff\x00\x00'
               for x, y, _ in first_mask), "SVG text fill is not red"
    print("  [PASS] SVG  text_cursor_and_fill        120x60")
    passed += 1

    # 7. tspan x resets the cursor, but style and the surrounding <g>
    # transform are inherited. Its blue glyph is not flattened into the
    # parent's red textContent or painted at the end of the red run.
    spans = render_svg("text_tspan", """
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 120 60">
      <g transform="translate(10,0)" font-size="24" fill="red">
        <text x="5" y="32">A<tspan x="75" fill="blue">B</tspan></text>
      </g>
    </svg>
    """, 120, 60)
    red = [(i // 4 % 120, i // 4 // 120) for i in range(0, len(spans), 4)
           if spans[i] > 0 and spans[i + 2] == 0 and spans[i + 3] > 0]
    blue = [(i // 4 % 120, i // 4 // 120) for i in range(0, len(spans), 4)
            if spans[i + 2] > 0 and spans[i] == 0 and spans[i + 3] > 0]
    assert len(red) > 20 and len(blue) > 20, f"tspan colors: {len(red)} red, {len(blue)} blue"
    assert max(x for x, _ in red) < 45 and min(x for x, _ in blue) >= 84, "tspan x / group transform"
    print("  [PASS] SVG  text_tspan_inherited_style   120x60")
    passed += 1

    return passed


def main():
    parser = argparse.ArgumentParser(description="Verify Litebird JPEG/GIF/SVG rendering.")
    parser.add_argument("--binary", default="build/media_check", help="Path to media_check binary")
    args = parser.parse_args()

    if not os.path.exists(args.binary):
        print(f"error: {args.binary} does not exist; build it first.", file=sys.stderr)
        return 1

    convert_bin = shutil.which("convert")
    print("=== Litebird Phase 38 Media & Vector Differential Verification ===")
    total_passed = 0

    with tempfile.TemporaryDirectory(prefix="litebird_media_verify_") as tmpdir:
        if convert_bin:
            print(f"Oracle: ImageMagick ({convert_bin})")
            total_passed += verify_jpegs_with_oracle(args.binary, convert_bin, tmpdir)
            total_passed += verify_gifs_with_oracle(args.binary, convert_bin, tmpdir)
            total_passed += verify_webp_decoding(args.binary, convert_bin, tmpdir)
            total_passed += verify_lossy_webp(args.binary, convert_bin, tmpdir)
            total_passed += verify_animated_webp(args.binary, convert_bin, tmpdir)
        else:
            print("WARNING: ImageMagick 'convert' not found; skipping libjpeg/GIF/WebP differential oracle.")

        total_passed += verify_svg_rendering(args.binary, tmpdir)

    print(f"\nAll {total_passed} media & vector verification checks passed!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
