#!/usr/bin/env python3
"""Independent W3C CSS Color 4/5 verification oracle and fuzzer for Litebird.

Computes all CSS Color 4 §10/§17 matrices directly from raw CIE chromaticity
coordinates using exact rational arithmetic, independently implements CSS Color 4
§13.2 OKLCH binary-search chroma reduction gamut mapping, verifies upstream WPT
CSS Color 4/5 computed & invalid vectors, and runs an adversarial mutation/grammar
fuzzer against build/color_check (including under ASan/UBSan).
"""

import argparse
import math
import random
import re
import subprocess
import sys
from fractions import Fraction


# ============================================================================
# Independent Exact-Rational W3C CSS Color 4 §17 Reference Oracle
# ============================================================================

def _mat_mul_f(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def _mat_vec_f(m, v):
    return [sum(m[i][k] * v[k] for k in range(3)) for i in range(3)]


def _mat_inv_frac(m):
    a = [[Fraction(x) for x in row] + [Fraction(1 if i == j else 0) for j in range(3)]
         for i, row in enumerate(m)]
    for col in range(3):
        piv = next(r for r in range(col, 3) if a[r][col] != 0)
        a[col], a[piv] = a[piv], a[col]
        d = a[col][col]
        a[col] = [x / d for x in a[col]]
        for r in range(3):
            if r != col and a[r][col] != 0:
                f = a[r][col]
                a[r] = [a[r][c] - f * a[col][c] for c in range(6)]
    return [row[3:] for row in a]


def _rgb_to_xyz_matrix(rx, ry, gx, gy, bx, by, wx, wy):
    r = [rx / ry, Fraction(1), (1 - rx - ry) / ry]
    g = [gx / gy, Fraction(1), (1 - gx - gy) / gy]
    b = [bx / by, Fraction(1), (1 - bx - by) / by]
    w = [wx / wy, Fraction(1), (1 - wx - wy) / wy]
    m = [[r[i], g[i], b[i]] for i in range(3)]
    s = _mat_vec_f(_mat_inv_frac(m), w)
    return [[float(m[i][j] * s[j]) for j in range(3)] for i in range(3)]


def _to_float_mat(m):
    return [[float(x) for x in row] for row in m]


_D65_xy = (Fraction(3127, 10000), Fraction(3290, 10000))
_D50_xy = (Fraction(3457, 10000), Fraction(3585, 10000))
_D65_W = [float(_D65_xy[0] / _D65_xy[1]), 1.0, float((1 - _D65_xy[0] - _D65_xy[1]) / _D65_xy[1])]
_D50_W = [float(_D50_xy[0] / _D50_xy[1]), 1.0, float((1 - _D50_xy[0] - _D50_xy[1]) / _D50_xy[1])]

_M_BRAD = [
    [Fraction(8951, 10000), Fraction(2664, 10000), Fraction(-1614, 10000)],
    [Fraction(-7502, 10000), Fraction(17135, 10000), Fraction(367, 10000)],
    [Fraction(389, 10000), Fraction(-685, 10000), Fraction(10296, 10000)],
]
_M_BRAD_INV = _mat_inv_frac(_M_BRAD)


def _bradford(w_src, w_dst):
    ws = [w_src[0] / w_src[1], Fraction(1), (1 - w_src[0] - w_src[1]) / w_src[1]]
    wd = [w_dst[0] / w_dst[1], Fraction(1), (1 - w_dst[0] - w_dst[1]) / w_dst[1]]
    cs = _mat_vec_f(_M_BRAD, ws)
    cd = _mat_vec_f(_M_BRAD, wd)
    diag = [[cd[i] / cs[i] if i == j else Fraction(0) for j in range(3)] for i in range(3)]
    return _to_float_mat(_mat_mul_f(_M_BRAD_INV, _mat_mul_f(diag, _M_BRAD)))


M_D65_TO_D50 = _bradford(_D65_xy, _D50_xy)
M_D50_TO_D65 = _bradford(_D50_xy, _D65_xy)

M_SRGB_TO_XYZ65 = _rgb_to_xyz_matrix(
    Fraction(64, 100), Fraction(33, 100), Fraction(30, 100), Fraction(60, 100),
    Fraction(15, 100), Fraction(6, 100), *_D65_xy)
M_XYZ65_TO_SRGB = _to_float_mat(_mat_inv_frac([[Fraction(x) for x in r] for r in M_SRGB_TO_XYZ65]))

M_P3_TO_XYZ65 = _rgb_to_xyz_matrix(
    Fraction(68, 100), Fraction(32, 100), Fraction(265, 1000), Fraction(69, 100),
    Fraction(15, 100), Fraction(6, 100), *_D65_xy)
M_XYZ65_TO_P3 = _to_float_mat(_mat_inv_frac([[Fraction(x) for x in r] for r in M_P3_TO_XYZ65]))

M_A98_TO_XYZ65 = _rgb_to_xyz_matrix(
    Fraction(64, 100), Fraction(33, 100), Fraction(21, 100), Fraction(71, 100),
    Fraction(15, 100), Fraction(6, 100), *_D65_xy)
M_XYZ65_TO_A98 = _to_float_mat(_mat_inv_frac([[Fraction(x) for x in r] for r in M_A98_TO_XYZ65]))

M_PROPHOTO_TO_XYZ50 = _rgb_to_xyz_matrix(
    Fraction(7347, 10000), Fraction(2653, 10000), Fraction(1596, 10000), Fraction(8404, 10000),
    Fraction(366, 10000), Fraction(1, 10000), *_D50_xy)
M_XYZ50_TO_PROPHOTO = _to_float_mat(_mat_inv_frac([[Fraction(x) for x in r] for r in M_PROPHOTO_TO_XYZ50]))

M_REC2020_TO_XYZ65 = _rgb_to_xyz_matrix(
    Fraction(708, 1000), Fraction(292, 1000), Fraction(170, 1000), Fraction(797, 1000),
    Fraction(131, 1000), Fraction(46, 1000), *_D65_xy)
M_XYZ65_TO_REC2020 = _to_float_mat(_mat_inv_frac([[Fraction(x) for x in r] for r in M_REC2020_TO_XYZ65]))

M_XYZ65_TO_LMS = [
    [0.8190224379967030, 0.3619062600528904, -0.1288737815209879],
    [0.0329836539323885, 0.9292868615863434, 0.0361446663506424],
    [0.0481771893596242, 0.2642395317527308, 0.6335478284694309],
]
M_LMS_TO_OKLAB = [
    [0.2104542683093140, 0.7936177747023054, -0.0040720430116193],
    [1.9779985324311684, -2.4285922420485799, 0.4505937096174110],
    [0.0259040424655478, 0.7827717124575296, -0.8086757549230774],
]
M_OKLAB_TO_LMS = _to_float_mat(_mat_inv_frac([[Fraction(str(x)) for x in r] for r in M_LMS_TO_OKLAB]))
M_LMS_TO_XYZ65 = _to_float_mat(_mat_inv_frac([[Fraction(str(x)) for x in r] for r in M_XYZ65_TO_LMS]))


def _sgn(x):
    return -1.0 if x < 0 else 1.0


def _cbrt(x):
    return math.copysign(abs(x) ** (1.0 / 3.0), x)


def _srgb_lin(v):
    return v / 12.92 if abs(v) <= 0.04045 else _sgn(v) * (((abs(v) + 0.055) / 1.055) ** 2.4)


def _lin_srgb(v):
    return _sgn(v) * (1.055 * (abs(v) ** (1.0 / 2.4)) - 0.055) if abs(v) > 0.0031308 else 12.92 * v


def _a98_lin(v):
    return _sgn(v) * (abs(v) ** (563.0 / 256.0))


def _lin_a98(v):
    return _sgn(v) * (abs(v) ** (256.0 / 563.0))


def _pro_lin(v):
    return v / 16.0 if abs(v) <= 16.0 / 512.0 else _sgn(v) * (abs(v) ** 1.8)


def _lin_pro(v):
    return _sgn(v) * (abs(v) ** (1.0 / 1.8)) if abs(v) >= 1.0 / 512.0 else 16.0 * v


def _rec_lin(v):
    a, b = 1.09929682680944, 0.018053968510807
    return v / 4.5 if abs(v) < b * 4.5 else _sgn(v) * (((abs(v) + a - 1.0) / a) ** (1.0 / 0.45))


def _lin_rec(v):
    a, b = 1.09929682680944, 0.018053968510807
    return _sgn(v) * (a * (abs(v) ** 0.45) - (a - 1.0)) if abs(v) > b else 4.5 * v


def _to_xyz65(space, c):
    if space == "srgb":
        return _mat_vec_f(M_SRGB_TO_XYZ65, [_srgb_lin(x) for x in c])
    if space == "srgb-linear":
        return _mat_vec_f(M_SRGB_TO_XYZ65, c)
    if space == "display-p3":
        return _mat_vec_f(M_P3_TO_XYZ65, [_srgb_lin(x) for x in c])
    if space == "display-p3-linear":
        return _mat_vec_f(M_P3_TO_XYZ65, c)
    if space == "a98-rgb":
        return _mat_vec_f(M_A98_TO_XYZ65, [_a98_lin(x) for x in c])
    if space == "prophoto-rgb":
        return _mat_vec_f(M_D50_TO_D65, _mat_vec_f(M_PROPHOTO_TO_XYZ50, [_pro_lin(x) for x in c]))
    if space == "rec2020":
        return _mat_vec_f(M_REC2020_TO_XYZ65, [_rec_lin(x) for x in c])
    if space == "xyz-d50":
        return _mat_vec_f(M_D50_TO_D65, c)
    if space == "xyz-d65":
        return list(c)
    if space in ("lab", "lch"):
        lab = c if space == "lab" else [c[0], c[1] * math.cos(math.radians(c[2])), c[1] * math.sin(math.radians(c[2]))]
        k, eps = 24389.0 / 27.0, 216.0 / 24389.0
        f1 = (lab[0] + 16.0) / 116.0
        f0 = lab[1] / 500.0 + f1
        f2 = f1 - lab[2] / 200.0
        x = f0 ** 3 if f0 ** 3 > eps else (116.0 * f0 - 16.0) / k
        y = f1 ** 3 if lab[0] > k * eps else lab[0] / k
        z = f2 ** 3 if f2 ** 3 > eps else (116.0 * f2 - 16.0) / k
        return _mat_vec_f(M_D50_TO_D65, [x * _D50_W[0], y * _D50_W[1], z * _D50_W[2]])
    if space in ("oklab", "oklch"):
        ok = c if space == "oklab" else [c[0], c[1] * math.cos(math.radians(c[2])), c[1] * math.sin(math.radians(c[2]))]
        lms_c = _mat_vec_f(M_OKLAB_TO_LMS, ok)
        return _mat_vec_f(M_LMS_TO_XYZ65, [v ** 3 for v in lms_c])
    raise ValueError(space)


def _from_xyz65(space, xyz):
    if space == "srgb":
        return [_lin_srgb(v) for v in _mat_vec_f(M_XYZ65_TO_SRGB, xyz)]
    if space == "srgb-linear":
        return _mat_vec_f(M_XYZ65_TO_SRGB, xyz)
    if space == "display-p3":
        return [_lin_srgb(v) for v in _mat_vec_f(M_XYZ65_TO_P3, xyz)]
    if space == "display-p3-linear":
        return _mat_vec_f(M_XYZ65_TO_P3, xyz)
    if space == "a98-rgb":
        return [_lin_a98(v) for v in _mat_vec_f(M_XYZ65_TO_A98, xyz)]
    if space == "prophoto-rgb":
        d50 = _mat_vec_f(M_D65_TO_D50, xyz)
        return [_lin_pro(v) for v in _mat_vec_f(M_XYZ50_TO_PROPHOTO, d50)]
    if space == "rec2020":
        return [_lin_rec(v) for v in _mat_vec_f(M_XYZ65_TO_REC2020, xyz)]
    if space == "xyz-d50":
        return _mat_vec_f(M_D65_TO_D50, xyz)
    if space == "xyz-d65":
        return list(xyz)
    if space in ("lab", "lch"):
        d50 = _mat_vec_f(M_D65_TO_D50, xyz)
        k, eps = 24389.0 / 27.0, 216.0 / 24389.0
        f = [_cbrt(d50[i] / _D50_W[i]) if d50[i] / _D50_W[i] > eps else (k * (d50[i] / _D50_W[i]) + 16.0) / 116.0
             for i in range(3)]
        lab = [116.0 * f[1] - 16.0, 500.0 * (f[0] - f[1]), 200.0 * (f[1] - f[2])]
        if space == "lab":
            return lab
        c = math.hypot(lab[1], lab[2])
        h = 0.0 if c <= 1e-4 else (math.degrees(math.atan2(lab[2], lab[1])) % 360.0)
        return [lab[0], c, h]
    if space in ("oklab", "oklch"):
        lms = _mat_vec_f(M_XYZ65_TO_LMS, xyz)
        ok = _mat_vec_f(M_LMS_TO_OKLAB, [_cbrt(v) for v in lms])
        if space == "oklab":
            return ok
        c = math.hypot(ok[1], ok[2])
        h = 0.0 if c <= 1e-6 else (math.degrees(math.atan2(ok[2], ok[1])) % 360.0)
        return [ok[0], c, h]
    if space == "hsl":
        r, g, b = _from_xyz65("srgb", xyz)
        mx, mn = max(r, g, b), min(r, g, b)
        d = mx - mn
        l = 0.5 * (mx + mn)
        s = 0.0 if d == 0 or l in (0.0, 1.0) else (mx - l) / min(l, 1.0 - l)
        h = 0.0
        if d != 0:
            if mx == r:
                h = (g - b) / d + (6.0 if g < b else 0.0)
            elif mx == g:
                h = (b - r) / d + 2.0
            else:
                h = (r - g) / d + 4.0
            h *= 60.0
        if s < 0:
            h = (h + 180.0) % 360.0
            s = -s
        return [0.0 if abs(s) <= 1e-6 else h % 360.0, s * 100.0, l * 100.0]
    if space == "hwb":
        r, g, b = _from_xyz65("srgb", xyz)
        mx, mn = max(r, g, b), min(r, g, b)
        d = mx - mn
        h = 0.0
        if d != 0:
            if mx == r:
                h = (g - b) / d + (6.0 if g < b else 0.0)
            elif mx == g:
                h = (b - r) / d + 2.0
            else:
                h = (r - g) / d + 4.0
            h *= 60.0
        w, bl = mn * 100.0, (1.0 - mx) * 100.0
        return [0.0 if w + bl >= 100.0 - 1e-6 else h % 360.0, w, bl]
    raise ValueError(space)


def py_gamut_map_srgb8(space, coords):
    xyz = _to_xyz65(space, coords)
    srgb = _from_xyz65("srgb", xyz)
    def in_g(v):
        return all(-1e-6 <= x <= 1.0 + 1e-6 for x in v)
    def clamp01(v):
        return [max(0.0, min(1.0, x)) for x in v]
    def pack(v):
        return tuple(int(round(max(0.0, min(1.0, x)) * 255.0)) for x in v)
    if in_g(srgb):
        return pack(srgb)
    L, C0, H = _from_xyz65("oklch", xyz)
    if L >= 1.0:
        return (255, 255, 255)
    if L <= 0.0:
        return (0, 0, 0)
    def de(a, b):
        return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))
    orig_ok = _from_xyz65("oklab", xyz)
    clipped = clamp01(srgb)
    if de(orig_ok, _from_xyz65("oklab", _to_xyz65("srgb", clipped))) < 0.02:
        return pack(clipped)
    lo, hi = 0.0, C0
    lo_in = True
    best = clipped
    for _ in range(32):
        if hi - lo <= 0.0001:
            break
        mid = 0.5 * (lo + hi)
        cur = _from_xyz65("srgb", _to_xyz65("oklch", [L, mid, H]))
        if lo_in and in_g(cur):
            lo = mid
            best = cur
            continue
        cl = clamp01(cur)
        d = de(_from_xyz65("oklab", _to_xyz65("oklch", [L, mid, H])),
               _from_xyz65("oklab", _to_xyz65("srgb", cl)))
        if d < 0.02:
            if 0.02 - d < 0.0001:
                best = cl
                break
            lo_in = False
            lo = mid
            best = cl
        else:
            hi = mid
    return pack(best)


# ============================================================================
# Batch Runner Against build/color_check
# ============================================================================

class ColorCheckProcess:
    def __init__(self, binary):
        self.proc = subprocess.Popen(
            [binary, "--batch"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    def run_lines(self, lines):
        payload = "\n".join(lines) + "\n"
        out, err = self.proc.communicate(payload, timeout=30)
        if self.proc.returncode != 0:
            raise RuntimeError(f"color_check crashed with rc={self.proc.returncode}: {err}")
        return out.splitlines()


def _parse_serialized_tokens(s):
    m = re.match(r"^([a-z-]+)\((.*)\)$", s.strip())
    if not m:
        return None
    fn, inner = m.group(1), m.group(2)
    toks = re.split(r"[\s,]+", inner.strip())
    return fn, toks


def _fuzzy_color_equal(actual, expected, tol=0.002):
    if actual == expected:
        return True
    pa = _parse_serialized_tokens(actual)
    pe = _parse_serialized_tokens(expected)
    if not pa or not pe or pa[0] != pe[0] or len(pa[1]) != len(pe[1]):
        return False
    for ta, te in zip(pa[1], pe[1]):
        if ta == te:
            continue
        try:
            fa, fe = float(ta), float(te)
            if abs(fa - fe) > tol:
                return False
        except ValueError:
            return False
    return True


# ============================================================================
# Test Suites
# ============================================================================

ALL_SPACES = [
    "srgb", "srgb-linear", "display-p3", "display-p3-linear", "a98-rgb",
    "prophoto-rgb", "rec2020", "xyz-d50", "xyz-d65",
    "lab", "lch", "oklab", "oklch", "hsl", "hwb",
]


def verify_space_matrix(binary):
    """Cross-verify all 15x15 space conversions against the rational Python oracle."""
    samples = [
        ("srgb", [1.0, 0.0, 0.0], "color(srgb 1 0 0)"),
        ("srgb", [0.0, 1.0, 0.0], "color(srgb 0 1 0)"),
        ("srgb", [0.0, 0.0, 1.0], "color(srgb 0 0 1)"),
        ("srgb", [1.0, 1.0, 1.0], "color(srgb 1 1 1)"),
        ("srgb", [0.4, 0.2, 0.6], "color(srgb 0.4 0.2 0.6)"),
        ("display-p3", [1.0, 0.0, 0.0], "color(display-p3 1 0 0)"),
        ("display-p3", [0.2, 0.9, 0.3], "color(display-p3 0.2 0.9 0.3)"),
        ("a98-rgb", [0.8, 0.2, 0.7], "color(a98-rgb 0.8 0.2 0.7)"),
        ("prophoto-rgb", [0.7, 0.3, 0.5], "color(prophoto-rgb 0.7 0.3 0.5)"),
        ("rec2020", [0.9, 0.5, 0.1], "color(rec2020 0.9 0.5 0.1)"),
        ("lab", [55.0, 40.0, -30.0], "lab(55 40 -30)"),
        ("oklch", [0.65, 0.18, 145.0], "oklch(0.65 0.18 145)"),
    ]

    queries = []
    meta = []
    for src_sp, coords, expr in samples:
        xyz = _to_xyz65(src_sp, coords)
        for dst_sp in ALL_SPACES:
            expected = _from_xyz65(dst_sp, xyz)
            queries.append(f"convert:{dst_sp}\t-\t0\t{expr}")
            meta.append((src_sp, dst_sp, expr, expected))

    lines = ColorCheckProcess(binary).run_lines(queries)
    assert len(lines) == len(queries), f"line count mismatch: {len(lines)} != {len(queries)}"

    failures = []
    max_err = 0.0
    for line, (src_sp, dst_sp, expr, exp) in zip(lines, meta):
        parts = line.split("\t")
        if parts[0] != "OK":
            failures.append(f"{expr} -> {dst_sp}: got {line}")
            continue
        got = [float(parts[1]), float(parts[2]), float(parts[3])]
        miss = [int(parts[5]), int(parts[6]), int(parts[7])]
        for i in range(3):
            if miss[i]:
                continue  # powerless achromatic hue
            # Hue wrap tolerance near 0/360
            diff = abs(got[i] - exp[i])
            if dst_sp in ("lch", "oklch") and i == 2 and diff > 180.0:
                diff = 360.0 - diff
            if dst_sp in ("hsl", "hwb") and i == 0 and diff > 180.0:
                diff = 360.0 - diff
            tol = 5e-3 if dst_sp in ("lab", "lch", "hsl", "hwb") else 5e-5
            max_err = max(max_err, diff / (100.0 if dst_sp in ("lab", "lch", "hsl", "hwb") else 1.0))
            if diff > tol:
                failures.append(f"{expr} -> {dst_sp} ch[{i}]: got={got[i]} exp={exp[i]} diff={diff}")
    return len(queries), max_err, failures


def verify_gamut_mapping(binary):
    """Verify CSS Color 4 §13.2 OKLCH binary-search gamut mapping against Python oracle."""
    cases = [
        ("display-p3", [1.0, 0.0, 0.0], "color(display-p3 1 0 0)"),
        ("display-p3", [0.0, 1.0, 0.0], "color(display-p3 0 1 0)"),
        ("display-p3", [0.0, 0.0, 1.0], "color(display-p3 0 0 1)"),
        ("display-p3", [1.0, 0.8, 0.0], "color(display-p3 1 0.8 0)"),
        ("rec2020", [1.0, 0.0, 0.0], "color(rec2020 1 0 0)"),
        ("rec2020", [0.0, 1.0, 0.0], "color(rec2020 0 1 0)"),
        ("prophoto-rgb", [0.9, 0.1, 0.1], "color(prophoto-rgb 0.9 0.1 0.1)"),
        ("oklch", [0.7, 0.32, 145.0], "oklch(0.7 0.32 145)"),
        ("oklch", [0.6, 0.35, 30.0], "oklch(0.6 0.35 30)"),
        ("oklch", [0.85, 0.25, 195.0], "oklch(0.85 0.25 195)"),
        ("lab", [60.0, 95.0, 65.0], "lab(60 95 65)"),
        ("lab", [70.0, -90.0, 50.0], "lab(70 -90 50)"),
        ("srgb", [0.5, 0.25, 0.75], "color(srgb 0.5 0.25 0.75)"),
    ]
    queries = [f"parse\t-\t0\t{expr}" for _, _, expr in cases]
    lines = ColorCheckProcess(binary).run_lines(queries)
    failures = []
    for line, (sp, coords, expr) in zip(lines, cases):
        parts = line.split("\t")
        if parts[0] != "OK":
            failures.append(f"gamut {expr}: ERR")
            continue
        got_rgb = (int(parts[2]), int(parts[3]), int(parts[4]))
        exp_rgb = py_gamut_map_srgb8(sp, coords)
        if any(abs(g - e) > 1 for g, e in zip(got_rgb, exp_rgb)):
            failures.append(f"gamut {expr}: got={got_rgb} exp={exp_rgb}")
    return len(cases), failures


# Upstream WPT CSS Color 4/5 computed-value vectors: (expr, expected, currentcolor, dark)
WPT_VALID_VECTORS = [
    # Legacy & modern rgb/rgba + none
    ("#f00", "rgb(255, 0, 0)", "-", 0),
    ("#0f08", "rgba(0, 255, 0, 0.533)", "-", 0),
    ("#11223380", "rgba(17, 34, 51, 0.5)", "-", 0),
    ("rebeccapurple", "rgb(102, 51, 153)", "-", 0),
    ("transparent", "rgba(0, 0, 0, 0)", "-", 0),
    ("rgb(255, 0, 128)", "rgb(255, 0, 128)", "-", 0),
    ("rgb(100%, 0%, 50%)", "rgb(255, 0, 128)", "-", 0),
    ("rgb(255 50% 0 / 50%)", "rgba(255, 128, 0, 0.5)", "-", 0),
    ("rgba(10 20 30 / 0.25)", "rgba(10, 20, 30, 0.25)", "-", 0),
    ("rgb(none none none)", "color(srgb none none none)", "-", 0),
    ("rgb(128 none none)", "color(srgb 0.501961 none none)", "-", 0),
    ("rgb(20% none none / none)", "color(srgb 0.2 none none / none)", "-", 0),

    # hsl / hsla + angle units + none
    ("hsl(120, 100%, 50%)", "rgb(0, 255, 0)", "-", 0),
    ("hsl(0.5turn 100% 50%)", "rgb(0, 255, 255)", "-", 0),
    ("hsl(200grad 100% 50%)", "rgb(0, 255, 255)", "-", 0),
    ("hsl(3.14159265rad 100% 50%)", "rgb(0, 255, 255)", "-", 0),
    ("hsla(240 100 50 / 50%)", "rgba(0, 0, 255, 0.5)", "-", 0),
    ("hsl(120 none 50%)", "hsl(120 none 50%)", "-", 0),
    ("hsl(120 80% none)", "hsl(120 80% none)", "-", 0),
    ("hsl(none 100% 50% / none)", "hsl(none 100% 50% / none)", "-", 0),

    # hwb (upstream WPT color-computed-hwb.html)
    ("hwb(120 30% 50%)", "rgb(77, 128, 77)", "-", 0),
    ("hwb(120 60% 60%)", "rgb(128, 128, 128)", "-", 0),
    ("hwb(0 100% 0%)", "rgb(255, 255, 255)", "-", 0),
    ("hwb(0 0% 100%)", "rgb(0, 0, 0)", "-", 0),
    ("hwb(120 30% 50% / 50%)", "rgba(77, 128, 77, 0.5)", "-", 0),
    ("hwb(150 20% 10%)", "rgb(51, 230, 140)", "-", 0),
    ("hwb(45 40% 80%)", "rgb(85, 85, 85)", "-", 0),
    ("hwb(0 70% 60%)", "rgb(137, 137, 137)", "-", 0),
    ("hwb(90 50% 25%)", "rgb(159, 191, 128)", "-", 0),
    ("hwb(120 none 50%)", "hwb(120 none 50%)", "-", 0),
    ("hwb(none none none / none)", "hwb(none none none / none)", "-", 0),

    # lab / lch / oklab / oklch (upstream WPT color-computed-lab.html)
    ("lab(0 0 0)", "lab(0 0 0)", "-", 0),
    ("lab(0 0 0 / 1)", "lab(0 0 0)", "-", 0),
    ("lab(20 0 10/0.5)", "lab(20 0 10 / 0.5)", "-", 0),
    ("lab(20 0 10/50%)", "lab(20 0 10 / 0.5)", "-", 0),
    ("lab(400 0 10/50%)", "lab(100 0 10 / 0.5)", "-", 0),
    ("lab(-40 0 0)", "lab(0 0 0)", "-", 0),
    ("lab(50% 100% -100%)", "lab(50 125 -125)", "-", 0),
    ("lab(calc(50 * 3) calc(0.5 - 1) calc(1.5) / calc(-0.5 + 1))", "lab(100 -0.5 1.5 / 0.5)", "-", 0),
    ("lab(calc(NaN) 0 0)", "lab(0 0 0)", "-", 0),
    ("lab(calc(0 / 0) 0 0)", "lab(0 0 0)", "-", 0),
    ("lch(50 calc(infinity) 0)", "lch(50 calc(infinity) 0)", "-", 0),
    ("oklch(0.5 calc(infinity) 0)", "oklch(0.5 calc(infinity) 0)", "-", 0),
    ("lab(none none none / none)", "lab(none none none / none)", "-", 0),
    ("rgb(/* R */ 0, /* G */ 51, /* B */ 255)", "rgb(0, 51, 255)", "-", 0),
    ("lch(50% 50% 0.5turn)", "lch(50 75 180)", "-", 0),
    ("lch(50 -20 -90deg)", "lch(50 0 270)", "-", 0),
    ("oklab(0.2 0 0.1/50%)", "oklab(0.2 0 0.1 / 0.5)", "-", 0),
    ("oklab(4 0 0.1/50%)", "oklab(1 0 0.1 / 0.5)", "-", 0),
    ("oklab(50% 50% -50%)", "oklab(0.5 0.2 -0.2)", "-", 0),
    ("oklch(60% 50% 120deg / 80%)", "oklch(0.6 0.2 120 / 0.8)", "-", 0),
    ("oklch(0.7 -0.1 400deg)", "oklch(0.7 0 40)", "-", 0),

    # color() predefined spaces (upstream WPT color-computed-color-function.html)
    ("color(srgb 20% 0 10/50%)", "color(srgb 0.2 0 10 / 0.5)", "-", 0),
    ("color(srgb-linear 0.2 0.4 0.6)", "color(srgb-linear 0.2 0.4 0.6)", "-", 0),
    ("color(display-p3 1 0.5 0)", "color(display-p3 1 0.5 0)", "-", 0),
    ("color(display-p3-linear 0.1 0.2 0.3)", "color(display-p3-linear 0.1 0.2 0.3)", "-", 0),
    ("color(a98-rgb 0.25 0.5 0.75)", "color(a98-rgb 0.25 0.5 0.75)", "-", 0),
    ("color(prophoto-rgb 0.3 0.6 0.9)", "color(prophoto-rgb 0.3 0.6 0.9)", "-", 0),
    ("color(rec2020 0.4 0.5 0.6 / 0.7)", "color(rec2020 0.4 0.5 0.6 / 0.7)", "-", 0),
    ("color(xyz 0.2 0.4 0.6)", "color(xyz-d65 0.2 0.4 0.6)", "-", 0),
    ("color(xyz-d50 0.2 0.4 0.6)", "color(xyz-d50 0.2 0.4 0.6)", "-", 0),
    ("color(xyz-d65 none 0.5 none / none)", "color(xyz-d65 none 0.5 none / none)", "-", 0),
    ("color(display-p3 calc(50% * 3) calc(-150% / 3) calc(50%) / calc(-50% * 3))", "color(display-p3 1.5 -0.5 0.5 / 0)", "-", 0),

    # color-mix() (upstream WPT color-computed-color-mix-function.html)
    ("color-mix(in srgb, red, blue)", "color(srgb 0.5 0 0.5)", "-", 0),
    ("color-mix(in srgb, red 25%, blue)", "color(srgb 0.25 0 0.75)", "-", 0),
    ("color-mix(in srgb, 25% red, blue)", "color(srgb 0.25 0 0.75)", "-", 0),
    ("color-mix(in srgb, red 20%, blue 20%)", "color(srgb 0.5 0 0.5 / 0.4)", "-", 0),
    ("color-mix(in srgb, red 60%, blue 60%)", "color(srgb 0.5 0 0.5)", "-", 0),
    ("color-mix(in srgb, red)", "color(srgb 1 0 0)", "-", 0),
    ("color-mix(in srgb, red 50%)", "color(srgb 1 0 0 / 0.5)", "-", 0),
    ("color-mix(in srgb, red, green, blue)", "color(srgb 0.333333 0.16732 0.333333)", "-", 0),
    ("color-mix(in srgb, red 50%, green 30%, blue 20%)", "color(srgb 0.5 0.150588 0.2)", "-", 0),
    ("color-mix(in srgb, red 60%, green, blue 50%)", "color(srgb 0.545455 0 0.454545)", "-", 0),
    ("color-mix(in srgb, red 0%, green 0%, blue 0%)", "color(srgb 0.25 0.12549 0.5 / 0)", "-", 0),
    ("color-mix(in oklch, teal calc(-10%), olive 0%)", "oklch(0.561894 0.109643 152.2691 / 0)", "-", 0),
    ("color-mix(in hsl, hsl(0 100% 100%), blue)", "color(srgb 1 0.5 1)", "-", 0),
    ("color-mix(in hsl, oklch(100% 0 0), blue)", "color(srgb 0.625 0.625 0.875)", "-", 0),
    ("color-mix(in hwb, hwb(0 50.0 50.0), blue)", "color(srgb 0.75 0.25 0.75)", "-", 0),
    ("color-mix(in oklch, oklab(0.1 none none), oklch(0.3 0.2 90deg))", "oklch(0.2 0.2 90)", "-", 0),
    ("color-mix(in hsl, hsl(120deg 10% 20%) 0%, hsl(30deg 30% 40%) 0%)", "color(srgb 0.33 0.36 0.24 / 0)", "-", 0),
    ("color-mix(in hsl, hsl(120deg 40% 40% / none), hsl(0deg 40% 40% / none))", "hsl(60 40% 40% / none)", "-", 0),
    ("color-mix(in hwb, hwb(none none none), hwb(none none none))", "hwb(none none none)", "-", 0),
    ("hsl(from rebeccapurple h s none)", "hsl(270 50% none)", "-", 0),
    ("hwb(from rebeccapurple none w b / alpha)", "hwb(none 20% 40%)", "-", 0),
    ("color-mix(in hsl, hsl(120deg 10% 20%), hsl(30deg 30% 40%))", "color(srgb 0.33 0.36 0.24)", "-", 0),
    ("color-mix(in hsl, hsl(120deg 10% 20%) 25%, hsl(30deg 30% 40%))", "color(srgb 0.4375 0.415625 0.2625)", "-", 0),
    ("color-mix(in hsl, hsl(120deg 10% 20%) 12.5%, hsl(30deg 30% 40%) 37.5%)", "color(srgb 0.4375 0.415625 0.2625 / 0.5)", "-", 0),
    ("color-mix(in hsl, hsl(120deg 10% 20% / .4), hsl(30deg 30% 40% / .8))", "color(srgb 0.372222 0.411111 0.255556 / 0.6)", "-", 0),
    ("color-mix(in hsl, transparent, hsl(30deg 30% 40%))", "color(srgb 0.52 0.4 0.28 / 0.5)", "-", 0),
    ("color-mix(in hsl, hsl(120deg 10% 20% / 0), hsl(30deg 30% 40%))", "color(srgb 0.46 0.52 0.28 / 0.5)", "-", 0),
    ("color-mix(in hsl, white, blue)", "color(srgb 0.625 0.625 0.875)", "-", 0),
    ("color-mix(in hsl shorter hue, red, white)", "color(srgb 0.875 0.625 0.625)", "-", 0),
    ("color-mix(in hsl longer hue, red, white)", "color(srgb 0.625 0.875 0.875)", "-", 0),
    ("color-mix(in hsl longer hue, red, transparent)", "color(srgb 0 1 1 / 0.5)", "-", 0),
    ("color-mix(in hsl increasing hue, hsl(330deg 50% 50%), hsl(50deg 50% 50%))", "color(srgb 0.75 0.333333 0.25)", "-", 0),
    ("color-mix(in hsl decreasing hue, hsl(330deg 50% 50%), hsl(50deg 50% 50%))", "color(srgb 0.25 0.666667 0.75)", "-", 0),
    ("color-mix(in oklab, oklab(0.4 0.1 0.2), oklab(0.8 -0.1 0))", "oklab(0.6 0 0.1)", "-", 0),
    ("color-mix(in oklch shorter hue, oklch(0.6 0.2 350), oklch(0.8 0.1 10))", "oklch(0.7 0.15 0)", "-", 0),
    ("color-mix(in oklch longer hue, oklch(0.6 0.2 350), oklch(0.8 0.1 10))", "oklch(0.7 0.15 180)", "-", 0),
    ("color-mix(in oklch increasing hue, oklch(0.6 0.2 10), oklch(0.8 0.1 350))", "oklch(0.7 0.15 180)", "-", 0),
    ("color-mix(in oklch decreasing hue, oklch(0.6 0.2 10), oklch(0.8 0.1 350))", "oklch(0.7 0.15 0)", "-", 0),
    ("color-mix(in lab, lab(20 40 60), lab(80 -20 0))", "lab(50 10 30)", "-", 0),
    ("color-mix(in display-p3, color(display-p3 1 0 0), color(display-p3 0 1 0))", "color(display-p3 0.5 0.5 0)", "-", 0),
    ("color-mix(in srgb, color-mix(in srgb, red, yellow), blue)", "color(srgb 0.5 0.25 0.5)", "-", 0),

    # Relative color syntax (upstream WPT color-computed-relative-color.html)
    ("rgb(from rebeccapurple r g b)", "color(srgb 0.4 0.2 0.6)", "-", 0),
    ("rgb(from rebeccapurple b alpha r / g)", "color(srgb 0.6 0.003922 0.4)", "-", 0),
    ("rgb(from rebeccapurple r calc(g * 2) 10)", "color(srgb 0.4 0.4 0.039216)", "-", 0),
    ("rgb(from rebeccapurple calc((r / 255) * 100%) calc((g / 255) * 100%) calc((b / 255) * 100%) / calc(alpha * 100%))", "color(srgb 0.4 0.2 0.6)", "-", 0),
    ("rgb(from rgb(100 110 120 / 0.8) calc(r + 1) calc(g + 1) calc(b + 1) / calc(alpha + 0.01))", "color(srgb 0.396078 0.435294 0.47451 / 0.81)", "-", 0),
    ("rgb(from rgb(from rebeccapurple r g b / calc(alpha + 0.5)) r g b / calc(alpha - 0.5))", "color(srgb 0.4 0.2 0.6 / 0.5)", "-", 0),
    ("rgb(from rgb(from rebeccapurple r g b / calc(alpha - 1.5)) r g b / calc(alpha + 0.5))", "color(srgb 0.4 0.2 0.6 / 0.5)", "-", 0),
    ("hsl(from rebeccapurple calc(h) calc(s) calc(l))", "color(srgb 0.4 0.2 0.6)", "-", 0),
    ("hsl(from rebeccapurple h s calc(50% * l / 100))", "color(srgb 0.2 0.1 0.3)", "-", 0),
    ("hsl(from hsl(50 50 50) h s calc((sin(asin(sin(l))) + 1) * 50))", "color(srgb 0.553 0.492 0.184)", "-", 0),
    ("hsl(from hsl(50 50 50) h s calc((sin(l * (50rad / (50deg * (180 / pi)))) + 1) * 50))", "color(srgb 0.553 0.492 0.184)", "-", 0),
    ("hwb(from rebeccapurple calc(h) calc(w) calc(b))", "color(srgb 0.4 0.2 0.6)", "-", 0),
    ("lab(from lab(50 5 10 / 0.8) calc(l + 1) calc(a + 1) calc(b + 1) / calc(alpha + 0.01))", "lab(51 6 11 / 0.81)", "-", 0),
    ("oklab(from oklab(0.5 0.1 -0.1) calc(l + 0.1) calc(a * 2) b)", "oklab(0.6 0.2 -0.1)", "-", 0),
    ("oklch(from oklch(0.6 0.2 30) calc(l + 0.1) calc(c / 2) calc(h + 180))", "oklch(0.7 0.1 210)", "-", 0),
    ("color(from color(display-p3 0.2 0.4 0.6 / 0.5) display-p3 calc(r + 0.1) g b / calc(alpha * 2))", "color(display-p3 0.3 0.4 0.6)", "-", 0),
    ("color(from color(xyz-d65 0.2 0.3 0.4) xyz-d65 x calc(y + 0.1) z)", "color(xyz-d65 0.2 0.4 0.4)", "-", 0),

    # CSS Values 4 math functions inside colors
    ("oklch(clamp(0.2, 0.9, 0.7) min(0.3, 0.15) max(30, 120))", "oklch(0.7 0.15 120)", "-", 0),
    ("lab(abs(-50) pow(2, 4) sqrt(625))", "lab(50 16 25)", "-", 0),
    ("lab(hypot(30, 40) mod(-10, 30) rem(-10, 30))", "lab(50 20 -10)", "-", 0),
    ("lab(round(nearest, 47, 10) round(up, 11, 10) round(down, -11, 10))", "lab(50 20 -20)", "-", 0),
    ("lab(calc(sign(5) * 50) calc(log(e) * 10) calc(exp(0) * 20))", "lab(50 10 20)", "-", 0),
    ("oklch(0.5 0.2 atan2(1, 1))", "oklch(0.5 0.2 45)", "-", 0),

    # currentcolor + light-dark()
    ("color-mix(in srgb, currentcolor 50%, white)", "color(srgb 1 0.5 0.5)", "red", 0),
    ("oklch(from currentcolor l c h)", "oklch(0.627955 0.257683 29.233885)", "red", 0),
    ("light-dark(white, black)", "rgb(255, 255, 255)", "-", 0),
    ("light-dark(white, black)", "rgb(0, 0, 0)", "-", 1),
    ("light-dark(oklch(0.3 0.1 240), oklch(0.9 0.05 240))", "oklch(0.3 0.1 240)", "-", 0),
    ("light-dark(oklch(0.3 0.1 240), oklch(0.9 0.05 240))", "oklch(0.9 0.05 240)", "-", 1),
]

WPT_INVALID_VECTORS = [
    "", "notacolor", "#12", "#12345", "#1234567", "#123456789", "#ggg",
    "rgb()", "rgb(255, 0)", "rgb(255, 0, 0, 1, 1)", "rgb(255, 50%, 0)",
    "rgb(255, none, 0)", "rgb(255 0 0, 0.5)", "rgb(255 0 0 / 0.5 / 0.5)",
    "rgb(255px 0 0)", "hsl(120, 50, 50)", "hsl(120, 50%, 50)", "hsl(50% 50% 50%)",
    "hwb(120, 10%, 20%)", "hwb(120deg 10% 20% 50%)", "hwb(50% 10% 20%)",
    "lab(50, 20, 30)", "lab(50 20)", "lab(50 20 30 40)", "lab(50deg 20 30)",
    "lch(50, 20, 30)", "lch(50 20 50%)", "oklab(0.5, 0.1, 0.1)", "oklch(0.5 0.1 50%)",
    "color(unknown-space 1 0 0)", "color(srgb, 1, 0, 0)", "color(srgb 1 0)",
    "color(srgb 1 0 0 1)", "color(display-p3 1deg 0 0)",
    "color-mix(in srgb, red -10%, blue)",
    "color-mix(in srgb, red 150%, blue)",
    "color-mix(in srgb shorter hue, red, blue)",
    "color-mix(in lab longer hue, red, blue)",
    "color-mix(in oklab increasing hue, red, blue)",
    "color-mix(in hsl hue, red, blue)",
    "color-mix(in hsl shorter, red, blue)",
    "color-mix(in hsl foo, red, blue)",
    "color-mix(in hsl red, blue)",
    "color-mix(in hsl, red blue)",
    "color-mix(red, blue, in hsl)",
    "color-mix(in srgb, red, blue blue)",
    "color-mix(in unknown, red, blue)",
    "rgb(from red r, g, b)",
    "rgb(from red r g)",
    "rgb(from notacolor r g b)",
    "hsl(from red h s l, 0.5)",
    "oklch(from red l c unknown)",
    "light-dark(white)",
    "light-dark(white, black, red)",
    "light-dark(white, notacolor)",
    "light-dark(notacolor, black)",
    "rgb(calc(10 + 20px) 0 0)",
    "rgb(calc(10% * 20%) 0 0)",
    "rgb(calc(10+20) 0 0)",
    "rgb(calc(((1 + 2) 0 0)",
    "color-mix(in srgb, " * 40 + "red, blue" + ")" * 40,
]


def verify_wpt_vectors(binary):
    queries = []
    for expr, _, cc, dark in WPT_VALID_VECTORS:
        queries.append(f"parse\t{cc}\t{dark}\t{expr}")
    for expr in WPT_INVALID_VECTORS:
        queries.append(f"parse\t-\t0\t{expr}")
    lines = ColorCheckProcess(binary).run_lines(queries)

    failures = []
    nv = len(WPT_VALID_VECTORS)
    for line, (expr, exp, cc, dark) in zip(lines[:nv], WPT_VALID_VECTORS):
        parts = line.split("\t")
        if parts[0] != "OK":
            failures.append(f"VALID rejected: {expr!r} (expected {exp!r})")
            continue
        got = parts[1]
        if not _fuzzy_color_equal(got, exp):
            failures.append(f"VALID mismatch: {expr!r} -> got {got!r}, expected {exp!r}")

    for line, expr in zip(lines[nv:], WPT_INVALID_VECTORS):
        if line != "ERR":
            failures.append(f"INVALID accepted: {expr!r} -> {line!r}")

    return len(WPT_VALID_VECTORS), len(WPT_INVALID_VECTORS), failures


def run_fuzzer(binary, count, seed):
    rng = random.Random(seed)
    spaces = ALL_SPACES + ["xyz", "bogus"]
    funcs = ["rgb", "rgba", "hsl", "hsla", "hwb", "lab", "lch", "oklab", "oklch", "color", "color-mix", "light-dark"]
    math_fns = ["calc", "min", "max", "clamp", "abs", "sign", "sin", "cos", "tan", "asin", "acos", "atan",
                "atan2", "sqrt", "exp", "pow", "log", "hypot", "mod", "rem", "round"]
    atoms = [
        "0", "1", "-1", "0.5", "128", "255", "360", "-720", "1e9", "-1e9", "1e-9",
        "50%", "-10%", "150%", "0deg", "180deg", "0.5turn", "200grad", "3.14rad",
        "none", "NaN", "infinity", "-infinity", "pi", "e", "r", "g", "b", "h", "s", "l", "w", "a", "c", "x", "y", "z", "alpha",
        "red", "blue", "rebeccapurple", "transparent", "currentcolor", "#f0a", "#11223380",
    ]
    hue_modes = ["", " shorter hue", " longer hue", " increasing hue", " decreasing hue", " bad hue"]

    def gen_math(depth=0):
        if depth >= 3 or rng.random() < 0.45:
            return rng.choice(atoms)
        fn = rng.choice(math_fns)
        if fn == "calc":
            op = rng.choice([" + ", " - ", " * ", " / "])
            return f"calc({gen_math(depth + 1)}{op}{gen_math(depth + 1)})"
        arity = 3 if fn == "clamp" else (2 if fn in ("pow", "mod", "rem", "atan2") else 1)
        args = ", ".join(gen_math(depth + 1) for _ in range(arity))
        return f"{fn}({args})"

    def gen_color(depth=0):
        if depth >= 3 or rng.random() < 0.25:
            return rng.choice(["red", "blue", "rebeccapurple", "transparent", "currentcolor", "#0f0", "#12345678"])
        fn = rng.choice(funcs)
        if fn == "color-mix":
            sp = rng.choice(spaces)
            hm = rng.choice(hue_modes)
            p1 = f" {rng.choice(['0%', '25%', '50%', '75%', '100%', '-5%', '120%'])}" if rng.random() < 0.5 else ""
            p2 = f" {rng.choice(['0%', '25%', '50%', '75%', '100%'])}" if rng.random() < 0.5 else ""
            return f"color-mix(in {sp}{hm}, {gen_color(depth + 1)}{p1}, {gen_color(depth + 1)}{p2})"
        if fn == "light-dark":
            return f"light-dark({gen_color(depth + 1)}, {gen_color(depth + 1)})"
        rel = f"from {gen_color(depth + 1)} " if rng.random() < 0.35 else ""
        sp_prefix = f"{rng.choice(spaces)} " if fn == "color" else ""
        sep = ", " if (not rel and fn in ("rgb", "rgba", "hsl", "hsla") and rng.random() < 0.3) else " "
        chs = sep.join(gen_math() for _ in range(3))
        if rng.random() < 0.4:
            chs += (f", {gen_math()}" if sep == ", " else f" / {gen_math()}")
        return f"{fn}({rel}{sp_prefix}{chs})"

    queries = []
    # Fixed hostile edge cases: extreme nesting, huge parens, weird ASCII bytes
    hostile = [
        "(" * 500 + ")" * 500,
        "calc(" * 100 + "1" + ")" * 100,
        "rgb(from " * 50 + "red" + " r g b)" * 50,
        "color-mix(in oklch, " * 50 + "red, blue" + ")" * 50,
        "light-dark(" * 50 + "white, black" + ")" * 50,
        "oklch(calc(1 / 0) calc(-1 / 0) calc(0 / 0) / calc(0 / 0))",
        "color(srgb 1e308 -1e308 1e-308 / 1e308)",
        "lab(999999999999999999999 -999999999999999999999 999999999999999999999)",
    ]
    for h in hostile:
        queries.append(f"fuzz\trebeccapurple\t0\t{h}")

    for i in range(count):
        expr = gen_color()
        # Randomly mutate 30% of generated expressions
        if rng.random() < 0.30 and expr:
            mut = rng.randint(0, 4)
            pos = rng.randint(0, len(expr) - 1)
            if mut == 0:
                expr = expr[:pos] + expr[pos + 1:]  # delete byte
            elif mut == 1:
                expr = expr[:pos] + rng.choice("(),/%+-*#09az ") + expr[pos:]  # insert byte
            elif mut == 2:
                expr = expr[:pos]  # truncate
            elif mut == 3:
                expr = expr.replace(" ", "  \t ")
            else:
                expr = expr.upper()
        # Strip tabs/newlines so batch protocol stays 1-line
        expr = expr.replace("\t", " ").replace("\n", " ").replace("\r", " ")
        cc = "rebeccapurple" if (i % 2 == 0) else "-"
        dark = i % 2
        queries.append(f"fuzz\t{cc}\t{dark}\t{expr}")

    lines = ColorCheckProcess(binary).run_lines(queries)
    if len(lines) != len(queries):
        raise RuntimeError(f"fuzz line mismatch: {len(lines)} != {len(queries)}")
    ok_cnt = sum(1 for l in lines if l == "OK")
    return len(queries), ok_cnt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default="build/color_check")
    ap.add_argument("--fuzz", type=int, default=0, help="If >0, run N fuzz cases instead of oracle suite")
    ap.add_argument("--seed", type=int, default=20261115)
    args = ap.parse_args()

    if args.fuzz > 0:
        total, ok_cnt = run_fuzzer(args.binary, args.fuzz, args.seed)
        print(f"CSS Color 4/5 fuzzer: {total} inputs ({ok_cnt} valid, {total - ok_cnt} rejected), 0 crashes")
        return 0

    n_conv, max_err, conv_fail = verify_space_matrix(args.binary)
    n_gamut, gamut_fail = verify_gamut_mapping(args.binary)
    n_valid, n_invalid, wpt_fail = verify_wpt_vectors(args.binary)

    all_fail = conv_fail + gamut_fail + wpt_fail
    print(f"CSS Color 4/5 space matrix:  {n_conv - len(conv_fail)}/{n_conv} passed (max normalized error {max_err:.2e})")
    print(f"CSS Color 4 §13.2 gamut map: {n_gamut - len(gamut_fail)}/{n_gamut} passed")
    print(f"WPT Color 4/5 valid vectors: {n_valid - sum(1 for f in wpt_fail if f.startswith('VALID'))}/{n_valid} passed")
    print(f"WPT Color 4/5 invalid rules: {n_invalid - sum(1 for f in wpt_fail if f.startswith('INVALID'))}/{n_invalid} passed")

    if all_fail:
        print("\nFAILURES:", file=sys.stderr)
        for f in all_fail[:40]:
            print("  " + f, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
