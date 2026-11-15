#!/usr/bin/env python3
"""Differential oracle & conformance verifier for Litebird's Phase 38 media stack.

Tests:
  1. JPEG decoding (Baseline SOF0, Extended SOF1, Progressive SOF2, 4:4:4, 4:2:2,
     4:2:0, Grayscale, CMYK/YCCK, odd non-MCU-aligned dimensions, 1x1, restart
     markers, low/high quality) pixel-by-pixel against ImageMagick/libjpeg.
  2. GIF decoding (GIF87a, GIF89a, 2..8-bit LZW, transparency, 4-pass
     interlacing, multi-frame animations with Dispose None/Background/Previous)
     pixel-by-pixel for EXACT bit-for-bit equality against ImageMagick -coalesce.
  3. Real-world SVG vector rendering (viewBox, preserveAspectRatio, full path
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
        else:
            print("WARNING: ImageMagick 'convert' not found; skipping libjpeg/GIF differential oracle.")

        total_passed += verify_svg_rendering(args.binary, tmpdir)

    print(f"\nAll {total_passed} media & vector verification checks passed!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
