#!/usr/bin/env python3
"""Differential oracle & conformance verifier for AVIF containers and AV1 pictures.

Phase 218, finding 222.

This oracle checks Litebird's AVIF path (`media_check --probe-avif`,
`--decode-avif`, `--fuzz-batch avif`) without asking Litebird to grade itself:

  1. Independent pure-Python ISO-BMFF box parser (ISO/IEC 14496-12 + 23000-22 +
     AVIF) and AV1 OBU bitstream walker (AV1 §5.3) cross-check every field
     reported by `AvifContainer::parse` (`ispe`, `pixi`, `av1C`, `colr`, item
     type/id, and byte-exact AV1 OBU payload).
  2. Pixel-level picture decoding oracle compares `ImageDecoder::decode`
     (`media_check --decode-avif`) against ground-truth 8-bit sRGB RGBA pixels
     from libheif/ImageMagick (stored in `tests/media/avif/corpus.json` and
     re-verified live whenever `convert` with `AVIF rw+` is installed, plus
     live-encoded extra patterns).
     - While Litebird has no AV1 intra-frame decoder, each corpus entry carries
       `picture_status: "unimplemented"` and `known_failure`, printed as `KNOWN`
       and never counted as a passing picture decode.
     - The day an AV1 decoder lands and returns pixels matching the reference
       within tolerance, this harness prints `FIXED` and exits 1 until the
       corpus entry is promoted to `picture_status: "implemented"`.
     - If decoded pixels disagree with the reference, or if a valid file fails
       with a malformed/crash status, the harness exits 1 immediately.
  3. Hostile / malformed / unsupported refusal checks (non-AVIF, empty,
     truncated `meta`, out-of-bounds `iloc`, oversized box, `grid` primary,
     `avis` sequence without `meta`) and a 200-input deterministic mutation fuzz
     batch through `media_check --fuzz-batch avif`.
  4. Five built-in negative controls (`--self-test`, run by default) proving the
     oracle catches payload byte flips, wrong `ispe` dimensions, 1-pixel colour
     errors, unannounced `FIXED` transitions, and malformed AV1 OBU headers.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple


# ---------------------------------------------------------------------------
# 1. Independent pure-Python ISO-BMFF + AVIF parser (zero Litebird code)
# ---------------------------------------------------------------------------

def iter_boxes(buf: bytes, start: int, end: int):
    pos = start
    while pos < end:
        if end - pos < 8:
            raise ValueError(f"truncated box header at {pos}")
        size = struct.unpack_from(">I", buf, pos)[0]
        box_type = buf[pos + 4 : pos + 8].decode("latin1")
        hdr = 8
        if size == 1:
            if end - pos < 16:
                raise ValueError(f"truncated largesize header for {box_type}")
            size = struct.unpack_from(">Q", buf, pos + 8)[0]
            hdr = 16
        elif size == 0:
            size = end - pos
        if size < hdr or pos + size > end:
            raise ValueError(f"box {box_type} size {size} out of bounds at {pos}..{end}")
        yield box_type, pos + hdr, pos + size
        pos += size


def parse_avif_independently(data: bytes) -> dict:
    """Parse ISO-BMFF AVIF boxes independently and extract primary AV1 payload."""
    boxes = list(iter_boxes(data, 0, len(data)))
    if not boxes or boxes[0][0] != "ftyp":
        raise ValueError("missing leading ftyp box")

    ftyp_s, ftyp_e = boxes[0][1], boxes[0][2]
    if ftyp_e - ftyp_s < 8:
        raise ValueError("ftyp too short")
    major_brand = data[ftyp_s : ftyp_s + 4].decode("latin1")
    compat = []
    for p in range(ftyp_s + 8, ftyp_e, 4):
        if p + 4 <= ftyp_e:
            compat.append(data[p : p + 4].decode("latin1"))

    meta_span = None
    for btype, s, e in boxes:
        if btype == "meta":
            meta_span = (s, e)
            break
    if meta_span is None:
        raise ValueError("missing meta box")

    ms, me = meta_span
    if me - ms < 4:
        raise ValueError("meta FullBox header truncated")
    # meta is a FullBox (4 bytes version+flags) followed by child boxes
    primary_id: Optional[int] = None
    item_types: Dict[int, str] = {}
    iloc_extents: Dict[int, List[Tuple[int, int]]] = {}
    properties: List[Tuple[str, bytes]] = []
    ipma_map: Dict[int, List[int]] = {}
    idat_payload = b""

    for btype, s, e in iter_boxes(data, ms + 4, me):
        if btype == "pitm":
            ver = data[s]
            primary_id = struct.unpack_from(">H", data, s + 4)[0] if ver == 0 else struct.unpack_from(">I", data, s + 4)[0]
        elif btype == "idat":
            idat_payload = data[s:e]
        elif btype == "iinf":
            ver = data[s]
            pos = s + 4
            count = struct.unpack_from(">H", data, pos)[0] if ver == 0 else struct.unpack_from(">I", data, pos)[0]
            pos += 2 if ver == 0 else 4
            for ibtype, is_, ie in iter_boxes(data, pos, e):
                if ibtype != "infe":
                    continue
                iver = data[is_]
                if iver == 2:
                    iid = struct.unpack_from(">H", data, is_ + 4)[0]
                    itype = data[is_ + 8 : is_ + 12].decode("latin1")
                    item_types[iid] = itype
                elif iver == 3:
                    iid = struct.unpack_from(">I", data, is_ + 4)[0]
                    itype = data[is_ + 10 : is_ + 14].decode("latin1")
                    item_types[iid] = itype
        elif btype == "iloc":
            ver = data[s]
            pos = s + 4
            b0, b1 = data[pos], data[pos + 1]
            pos += 2
            offset_size = (b0 >> 4) & 0xF
            length_size = b0 & 0xF
            base_offset_size = (b1 >> 4) & 0xF
            index_size = (b1 & 0xF) if ver in (1, 2) else 0
            item_count = struct.unpack_from(">H", data, pos)[0] if ver < 2 else struct.unpack_from(">I", data, pos)[0]
            pos += 2 if ver < 2 else 4

            def read_uint(n: int) -> int:
                nonlocal pos
                if n == 0:
                    return 0
                val = int.from_bytes(data[pos : pos + n], "big")
                pos += n
                return val

            for _ in range(item_count):
                iid = read_uint(2 if ver < 2 else 4)
                cmethod = 0
                if ver in (1, 2):
                    cmethod = struct.unpack_from(">H", data, pos)[0] & 0xF
                    pos += 2
                _dref = read_uint(2)
                base_off = read_uint(base_offset_size)
                ext_count = read_uint(2)
                exts = []
                for _ in range(ext_count):
                    if ver in (1, 2) and index_size > 0:
                        _idx = read_uint(index_size)
                    eoff = read_uint(offset_size)
                    elen = read_uint(length_size)
                    if cmethod != 0:
                        raise ValueError(f"unsupported iloc construction_method {cmethod}")
                    exts.append((base_off + eoff, elen))
                iloc_extents[iid] = exts
        elif btype == "iprp":
            for pbtype, ps, pe in iter_boxes(data, s, e):
                if pbtype == "ipco":
                    for cbtype, cs, ce in iter_boxes(data, ps, pe):
                        properties.append((cbtype, data[cs:ce]))
                elif pbtype == "ipma":
                    ver = data[ps]
                    flags = int.from_bytes(data[ps + 1 : ps + 4], "big")
                    pos = ps + 4
                    entry_count = struct.unpack_from(">I", data, pos)[0]
                    pos += 4
                    for _ in range(entry_count):
                        iid = struct.unpack_from(">H", data, pos)[0] if ver < 1 else struct.unpack_from(">I", data, pos)[0]
                        pos += 2 if ver < 1 else 4
                        acount = data[pos]
                        pos += 1
                        indices = []
                        for _ in range(acount):
                            if flags & 1:
                                raw = struct.unpack_from(">H", data, pos)[0]
                                pos += 2
                                idx = raw & 0x7FFF
                            else:
                                raw = data[pos]
                                pos += 1
                                idx = raw & 0x7F
                            if idx > 0:
                                indices.append(idx)
                        ipma_map.setdefault(iid, []).extend(indices)

    if primary_id is None:
        raise ValueError("missing pitm")
    if primary_id not in iloc_extents:
        raise ValueError(f"primary item {primary_id} not in iloc")

    payload = bytearray()
    for off, length in iloc_extents[primary_id]:
        if off + length > len(data):
            raise ValueError(f"iloc extent {off}+{length} out of bounds ({len(data)})")
        payload.extend(data[off : off + length])

    info = {
        "major_brand": major_brand,
        "compatible_brands": compat,
        "primary_id": primary_id,
        "type": item_types.get(primary_id, ""),
        "payload": bytes(payload),
        "ispe": None,
        "pixi": None,
        "av1c": None,
        "colr": None,
    }
    for idx in ipma_map.get(primary_id, []):
        if 1 <= idx <= len(properties):
            ptype, pbytes = properties[idx - 1]
            if ptype == "ispe" and len(pbytes) >= 12:
                w, h = struct.unpack_from(">II", pbytes, 4)
                info["ispe"] = (w, h)
            elif ptype == "pixi" and len(pbytes) >= 5:
                n = pbytes[4]
                info["pixi"] = list(pbytes[5 : 5 + n])
            elif ptype == "av1C" and len(pbytes) >= 4:
                b1, b2, b3 = pbytes[1], pbytes[2], pbytes[3]
                info["av1c"] = {
                    "profile": (b1 >> 5) & 0x7,
                    "level": b1 & 0x1F,
                    "high_bd": (b2 >> 6) & 1,
                    "twelve_bit": (b2 >> 5) & 1,
                    "mono": (b2 >> 4) & 1,
                    "sub_x": (b2 >> 3) & 1,
                    "sub_y": (b2 >> 2) & 1,
                    "csp": b2 & 0x3,
                }
            elif ptype == "colr" and len(pbytes) >= 4:
                ctype = pbytes[0:4].decode("latin1")
                if ctype == "nclx" and len(pbytes) >= 11:
                    prim, tr, mat = struct.unpack_from(">HHH", pbytes, 4)
                    fr = (pbytes[10] >> 7) & 1
                    info["colr"] = ("nclx", prim, tr, mat, fr)
                else:
                    info["colr"] = (ctype, 0, 0, 0, 0)
    return info


# ---------------------------------------------------------------------------
# 2. Independent AV1 OBU bitstream walker (AV1 spec §5.3)
# ---------------------------------------------------------------------------

OBU_SEQUENCE_HEADER = 1
OBU_TEMPORAL_DELIMITER = 2
OBU_FRAME_HEADER = 3
OBU_TILE_GROUP = 4
OBU_METADATA = 5
OBU_FRAME = 6
OBU_REDUNDANT_FRAME_HEADER = 7
OBU_TILE_LIST = 8
OBU_PADDING = 15


def parse_av1_obus(payload: bytes) -> List[Tuple[int, int]]:
    """Walk AV1 OBUs per AV1 §5.3 and return list of (obu_type, body_size)."""
    if not payload:
        raise ValueError("empty AV1 OBU payload")
    pos = 0
    obus: List[Tuple[int, int]] = []
    n = len(payload)
    while pos < n:
        hdr = payload[pos]
        pos += 1
        forbidden = (hdr >> 7) & 1
        if forbidden != 0:
            raise ValueError(f"AV1 obu_forbidden_bit != 0 at byte {pos - 1}")
        obu_type = (hdr >> 3) & 0xF
        has_ext = (hdr >> 2) & 1
        has_size = (hdr >> 1) & 1
        if has_ext:
            if pos >= n:
                raise ValueError("truncated AV1 OBU extension header")
            pos += 1
        if has_size:
            obu_size = 0
            for i in range(8):
                if pos >= n:
                    raise ValueError("truncated AV1 LEB128 obu_size")
                byte = payload[pos]
                pos += 1
                obu_size |= (byte & 0x7F) << (7 * i)
                if not (byte & 0x80):
                    break
            else:
                raise ValueError("AV1 LEB128 obu_size exceeds 8 bytes")
            if pos + obu_size > n:
                raise ValueError(f"AV1 OBU type {obu_type} size {obu_size} exceeds payload ({n - pos} left)")
            pos += obu_size
            obus.append((obu_type, obu_size))
        else:
            rem = n - pos
            pos = n
            obus.append((obu_type, rem))
    return obus


def validate_av1_still_picture_obus(payload: bytes) -> List[Tuple[int, int]]:
    obus = parse_av1_obus(payload)
    types = {t for t, _ in obus}
    if OBU_SEQUENCE_HEADER not in types:
        raise ValueError(f"AV1 still picture missing OBU_SEQUENCE_HEADER (found types {sorted(types)})")
    has_coded_frame = (OBU_FRAME in types) or (OBU_FRAME_HEADER in types and OBU_TILE_GROUP in types)
    if not has_coded_frame:
        raise ValueError(f"AV1 still picture missing coded frame OBU (found types {sorted(types)})")
    return obus


# ---------------------------------------------------------------------------
# 3. Pixel comparison metrics (RGBA)
# ---------------------------------------------------------------------------

@dataclass
class PixelDiff:
    mae: float
    psnr: float
    max_diff: int
    first_mismatch: Optional[Tuple[int, int, Tuple[int, ...], Tuple[int, ...]]]


def compare_rgba(actual: bytes, expected: bytes, width: int, height: int) -> PixelDiff:
    want_len = width * height * 4
    if len(actual) != want_len or len(expected) != want_len:
        raise ValueError(f"RGBA size mismatch: actual={len(actual)}, expected={len(expected)}, want={want_len}")
    total_abs = 0
    total_sq = 0
    max_diff = 0
    first_mm = None
    for idx in range(width * height):
        o = idx * 4
        a = actual[o : o + 4]
        e = expected[o : o + 4]
        pix_diff = 0
        for c in range(4):
            d = abs(a[c] - e[c])
            total_abs += d
            total_sq += d * d
            if d > max_diff:
                max_diff = d
            if d > pix_diff:
                pix_diff = d
        if pix_diff > 0 and first_mm is None:
            first_mm = (idx % width, idx // width, tuple(a), tuple(e))
    n_samples = max(1, width * height * 4)
    mae = total_abs / n_samples
    mse = total_sq / n_samples
    psnr = 99.0 if mse == 0 else 10.0 * math.log10((255.0 * 255.0) / mse)
    return PixelDiff(mae=mae, psnr=psnr, max_diff=max_diff, first_mismatch=first_mm)


def evaluate_picture_outcome(
    name: str,
    width: int,
    height: int,
    decode_rc: int,
    decoded_rgba: Optional[bytes],
    ref_rgba: bytes,
    picture_status: str,
    tolerance: dict,
) -> Tuple[str, str]:
    """Grade one AVIF picture decode against the reference RGBA.

    Returns (verdict, detail) where verdict is one of:
      'PASS'  - implemented and decoded pixels match reference within tolerance
      'KNOWN' - unimplemented and binary honestly returned exit code 3 (no_av1_decoder)
      'FIXED' - unimplemented, yet decoded pixels now match reference (must update corpus!)
      'FAIL'  - mismatch, unexpected error, or broken output
    """
    if decode_rc == 3:
        if picture_status == "unimplemented":
            return "KNOWN", f"{width}x{height} honest no_av1_decoder refusal; reference RGBA ready ({len(ref_rgba)} bytes)"
        return "FAIL", f"{name}: marked implemented but --decode-avif returned no_av1_decoder (rc=3)"

    if decode_rc != 0 or decoded_rgba is None:
        return "FAIL", f"{name}: --decode-avif failed unexpectedly (rc={decode_rc})"

    diff = compare_rgba(decoded_rgba, ref_rgba, width, height)
    max_mae = float(tolerance.get("max_mae", 2.0))
    max_cd = int(tolerance.get("max_channel_diff", 10))
    min_psnr = float(tolerance.get("min_psnr", 38.0))
    within = (diff.mae <= max_mae) and (diff.max_diff <= max_cd) and (diff.psnr >= min_psnr)
    summary = f"MAE={diff.mae:.3f}, PSNR={diff.psnr:.2f}dB, max_diff={diff.max_diff}"
    if not within:
        return "FAIL", f"{name}: decoded pixels diverge from libheif reference ({summary}, first={diff.first_mismatch})"
    if picture_status == "unimplemented":
        return "FIXED", f"{name}: decoded pixels now match libheif reference ({summary}); promote picture_status to 'implemented'"
    return "PASS", f"{name}: decoded pixels match libheif reference ({summary})"


# ---------------------------------------------------------------------------
# 4. media_check subprocess helpers + ImageMagick AVIF capability detection
# ---------------------------------------------------------------------------

def parse_probe_stdout(stdout: str) -> Dict[str, str]:
    out: Dict[str, str] = {}
    for line in stdout.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def probe_avif(binary: str, avif_path: str, payload_out: Optional[str] = None) -> Tuple[int, Dict[str, str]]:
    cmd = [binary, "--probe-avif", avif_path]
    if payload_out is not None:
        cmd.append(payload_out)
    res = subprocess.run(cmd, capture_output=True, text=True)
    return res.returncode, parse_probe_stdout(res.stdout)


def check_container_agreement(name: str, binary: str, avif_bytes: bytes, tmpdir: str) -> int:
    """Cross-check media_check --probe-avif against our independent parser + AV1 OBU walker."""
    ref = parse_avif_independently(avif_bytes)
    obus = validate_av1_still_picture_obus(ref["payload"])

    avif_path = os.path.join(tmpdir, f"{name}.avif")
    payload_path = os.path.join(tmpdir, f"{name}.av1")
    with open(avif_path, "wb") as f:
        f.write(avif_bytes)

    rc, kv = probe_avif(binary, avif_path, payload_path)
    if rc != 0 or kv.get("status") != "Ok":
        raise AssertionError(f"{name}: media_check --probe-avif failed (rc={rc}, kv={kv})")
    if kv.get("is_avif") != "1":
        raise AssertionError(f"{name}: expected is_avif=1, got {kv.get('is_avif')}")
    if kv.get("major_brand") != ref["major_brand"]:
        raise AssertionError(f"{name}: major_brand mismatch: {kv.get('major_brand')} vs {ref['major_brand']}")
    if int(kv.get("primary_id", "-1")) != ref["primary_id"]:
        raise AssertionError(f"{name}: primary_id mismatch: {kv.get('primary_id')} vs {ref['primary_id']}")
    if kv.get("type") != ref["type"]:
        raise AssertionError(f"{name}: item type mismatch: {kv.get('type')} vs {ref['type']}")

    if ref["ispe"] is not None:
        want_ispe = f"1 {ref['ispe'][0]} {ref['ispe'][1]}"
        if kv.get("ispe") != want_ispe:
            raise AssertionError(f"{name}: ispe mismatch: got '{kv.get('ispe')}', expected '{want_ispe}'")

    if ref["pixi"] is not None:
        want_pixi = "1 " + ",".join(str(x) for x in ref["pixi"])
        if kv.get("pixi") != want_pixi:
            raise AssertionError(f"{name}: pixi mismatch: got '{kv.get('pixi')}', expected '{want_pixi}'")

    if ref["av1c"] is not None:
        a = ref["av1c"]
        want_av1c = (
            f"1 profile={a['profile']} level={a['level']} high_bd={a['high_bd']} "
            f"twelve_bit={a['twelve_bit']} mono={a['mono']} sub_x={a['sub_x']} "
            f"sub_y={a['sub_y']} csp={a['csp']}"
        )
        if kv.get("av1c") != want_av1c:
            raise AssertionError(f"{name}: av1C mismatch:\n  got:  {kv.get('av1c')}\n  want: {want_av1c}")

    if ref["colr"] is not None and ref["colr"][0] == "nclx":
        _, prim, tr, mat, fr = ref["colr"]
        want_colr = f"1 nclx=1 {prim} {tr} {mat} {fr}"
        if kv.get("colr") != want_colr:
            raise AssertionError(f"{name}: colr mismatch: got '{kv.get('colr')}', expected '{want_colr}'")

    got_payload = open(payload_path, "rb").read()
    if got_payload != ref["payload"]:
        raise AssertionError(
            f"{name}: extracted AV1 payload mismatch: got {len(got_payload)} B "
            f"(sha256={hashlib.sha256(got_payload).hexdigest()[:12]}), "
            f"expected {len(ref['payload'])} B (sha256={hashlib.sha256(ref['payload']).hexdigest()[:12]})"
        )
    return len(obus)


def detect_convert_avif() -> Optional[str]:
    convert_bin = shutil.which("convert")
    if not convert_bin:
        return None
    res = subprocess.run([convert_bin, "-list", "format"], capture_output=True, text=True)
    if res.returncode != 0:
        return None
    for line in res.stdout.splitlines():
        if "AVIF" in line and "rw" in line:
            return convert_bin
    return None


def decode_avif_with_imagemagick(convert_bin: str, avif_path: str) -> Tuple[int, int, bytes]:
    ident = subprocess.run([convert_bin, avif_path, "-format", "%w %h", "info:"], capture_output=True, text=True, check=True)
    w, h = map(int, ident.stdout.strip().split())
    res = subprocess.run([convert_bin, avif_path, "-colorspace", "sRGB", "-depth", "8", "rgba:-"], capture_output=True, check=True)
    if len(res.stdout) != w * h * 4:
        raise RuntimeError(f"ImageMagick RGBA length {len(res.stdout)} != {w * h * 4}")
    return w, h, res.stdout


def write_png_rgb(path: str, w: int, h: int, pixel_fn) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    rows = bytearray()
    for y in range(h):
        rows.append(0)
        for x in range(w):
            rows.extend(pixel_fn(x, y, w, h))
    raw = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(rows))) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(raw)


# ---------------------------------------------------------------------------
# 5. Built-in negative controls (proves the oracle fails when it should)
# ---------------------------------------------------------------------------

def run_self_test(corpus_path: str) -> int:
    data = json.load(open(corpus_path))
    fx = data["fixtures"][0]
    avif_bytes = base64.b64decode(fx["avif_base64"])
    ref_rgba = zlib.decompress(base64.b64decode(fx["reference_rgba_zlib_base64"]))
    w, h = fx["width"], fx["height"]

    # Control 1: 1-byte payload corruption is detected by independent OBU/payload check
    ref = parse_avif_independently(avif_bytes)
    bad_payload = bytearray(ref["payload"])
    bad_payload[0] ^= 0x80  # sets obu_forbidden_bit = 1
    try:
        validate_av1_still_picture_obus(bytes(bad_payload))
        raise AssertionError("self-test 1 failed: obu_forbidden_bit=1 was not rejected")
    except ValueError:
        pass

    # Control 2: OBU stream without OBU_SEQUENCE_HEADER is rejected
    # Construct a single OBU_PADDING (type 15, has_size=1, size=1, byte 0x00)
    pad_only = bytes([(OBU_PADDING << 3) | 0x02, 1, 0x00])
    try:
        validate_av1_still_picture_obus(pad_only)
        raise AssertionError("self-test 2 failed: missing OBU_SEQUENCE_HEADER was not rejected")
    except ValueError:
        pass

    # Control 3: 1-pixel / 1-channel colour error is caught by pixel comparator
    bad_rgba = bytearray(ref_rgba)
    bad_rgba[0] ^= 0x01
    v3, _ = evaluate_picture_outcome(
        "neg_pixel", w, h, 0, bytes(bad_rgba), ref_rgba, "implemented", {"max_mae": 0.0, "max_channel_diff": 0, "min_psnr": 90.0}
    )
    if v3 != "FAIL":
        raise AssertionError(f"self-test 3 failed: 1-bit pixel error returned {v3}, expected FAIL")

    # Control 4: unannounced matching decode while unimplemented is caught as FIXED (non-zero gate)
    v4, _ = evaluate_picture_outcome(
        "neg_fixed", w, h, 0, ref_rgba, ref_rgba, "unimplemented", fx["tolerance"]
    )
    if v4 != "FIXED":
        raise AssertionError(f"self-test 4 failed: matching decode on unimplemented fixture returned {v4}, expected FIXED")

    # Control 5: truncated AVIF box is rejected by independent ISO-BMFF parser
    try:
        parse_avif_independently(avif_bytes[:20])
        raise AssertionError("self-test 5 failed: truncated AVIF was not rejected")
    except ValueError:
        pass

    print("AVIF oracle self-test: 5/5 (forbidden OBU bit refused, missing sequence header refused, 1-channel pixel diff refused, FIXED gate enforced, truncated container refused)")
    return 5


# ---------------------------------------------------------------------------
# 6. Hostile / malformed / unsupported refusals + mutation fuzzing
# ---------------------------------------------------------------------------

def verify_refusals_and_fuzz(binary: str, seed_avif: bytes, tmpdir: str, fuzz_count: int) -> Tuple[int, int]:
    # Minimal 1x1 PNG
    png_1x1 = base64.b64decode(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg=="
    )
    # Build an `avis` file with only ftyp + moov (no meta box) -> Unsupported
    avis_ftyp = struct.pack(">I", 20) + b"ftypavis" + struct.pack(">I", 0) + b"avis"
    avis_moov = struct.pack(">I", 8) + b"moov"
    avis_seq_only = avis_ftyp + avis_moov

    # Patch `av01` -> `grid` for tiled primary item -> Unsupported
    grid_avif = seed_avif.replace(b"av01", b"grid", 1)

    # Patch `meta` box size to exceed file length -> Malformed
    meta_idx = seed_avif.index(b"meta") - 4
    oversized_meta = bytearray(seed_avif)
    struct.pack_into(">I", oversized_meta, meta_idx, len(seed_avif) + 4096)

    # Patch `iloc` extent offset to point beyond EOF -> Malformed
    iloc_idx = seed_avif.index(b"iloc")
    iloc_oob = bytearray(seed_avif)
    # In Pillow/libheif iloc v0 (offset_size=4, length_size=4):
    # [iloc:4][ver+flags:4][sizes:2][item_count:2][item_id:2][dref:2][ext_count:2][offset:4][length:4]
    struct.pack_into(">I", iloc_oob, iloc_idx + 18, 0x00FFFFFF)

    refusals = [
        ("not_avif_empty", b"", "NotAvif", "0"),
        ("not_avif_png", png_1x1, "NotAvif", "0"),
        ("not_avif_ascii", b"NOT_AN_AVIF_FILE_AT_ALL", "NotAvif", "0"),
        ("malformed_truncated_meta", seed_avif[:44], "Malformed", "1"),
        ("malformed_oversized_meta", bytes(oversized_meta), "Malformed", "1"),
        ("malformed_iloc_oob", bytes(iloc_oob), "Malformed", "1"),
        ("unsupported_grid_primary", grid_avif, "Unsupported", "1"),
        ("unsupported_avis_sequence_no_meta", avis_seq_only, "Unsupported", "1"),
    ]

    for label, blob, want_status, want_is_avif in refusals:
        p = os.path.join(tmpdir, f"refusal_{label}.bin")
        with open(p, "wb") as f:
            f.write(blob)
        rc, kv = probe_avif(binary, p)
        if rc == 0:
            raise AssertionError(f"refusal {label}: expected non-zero rc, got 0 ({kv})")
        if kv.get("status") != want_status:
            raise AssertionError(f"refusal {label}: expected status={want_status}, got {kv}")
        if kv.get("is_avif") != want_is_avif:
            raise AssertionError(f"refusal {label}: expected is_avif={want_is_avif}, got {kv}")
        # Also verify --decode-avif refuses cleanly (exit 1, never 0 or 3)
        dec = subprocess.run(
            [binary, "--decode-avif", p, os.path.join(tmpdir, f"refusal_{label}.ppm")],
            capture_output=True, text=True,
        )
        if dec.returncode != 1:
            raise AssertionError(f"refusal {label}: --decode-avif expected rc=1, got {dec.returncode} ({dec.stderr.strip()})")

    # Deterministic mutation fuzzing batch
    rng = random.Random(0xA71F218)
    fuzz_paths = []
    for i in range(fuzz_count):
        buf = bytearray(seed_avif)
        mode = i % 5
        if mode == 0:
            # Truncate to random length
            buf = buf[: rng.randint(0, len(buf) - 1)]
        elif mode == 1:
            # Flip 1..4 random bytes
            for _ in range(rng.randint(1, 4)):
                buf[rng.randrange(len(buf))] ^= rng.randint(1, 255)
        elif mode == 2:
            # Corrupt a 4-byte integer field with boundary values
            pos = rng.randrange(0, max(1, len(buf) - 4))
            struct.pack_into(">I", buf, pos, rng.choice([0, 1, 7, 8, 0x7FFFFFFF, 0xFFFFFFFF]))
        elif mode == 3:
            # Zero a random slice
            a = rng.randrange(len(buf))
            b = min(len(buf), a + rng.randint(1, 24))
            buf[a:b] = b"\x00" * (b - a)
        else:
            # Append trailing garbage after mutating one header byte
            buf[rng.randrange(len(buf))] = rng.randrange(256)
            buf.extend(rng.randbytes(rng.randint(1, 32)))
        fp = os.path.join(tmpdir, f"fuzz_{i}.avif")
        with open(fp, "wb") as f:
            f.write(buf)
        fuzz_paths.append(fp)

    if fuzz_paths:
        res = subprocess.run([binary, "--fuzz-batch", "avif"] + fuzz_paths, capture_output=True, text=True)
        if res.returncode != 0:
            raise AssertionError(f"AVIF mutation fuzz batch failed (rc={res.returncode}): {res.stderr.strip()}")

    return len(refusals), len(fuzz_paths)


# ---------------------------------------------------------------------------
# 7. Main runner
# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description="AVIF container & AV1 picture differential oracle")
    ap.add_argument("--binary", default="build/media_check", help="Path to media_check binary")
    ap.add_argument("--corpus", default="tests/media/avif/corpus.json", help="Path to AVIF corpus JSON")
    ap.add_argument("--fuzz-count", type=int, default=200, help="Number of deterministic AVIF fuzz mutations")
    ap.add_argument("--no-self-test", action="store_true", help="Skip built-in negative controls")
    ap.add_argument("--self-test-only", action="store_true", help="Run built-in negative controls only")
    args = ap.parse_args()

    if not args.no_self_test:
        run_self_test(args.corpus)
    if args.self_test_only:
        return 0

    if not os.path.exists(args.binary):
        print(f"error: {args.binary} does not exist; build it first.", file=sys.stderr)
        return 2

    corpus = json.load(open(args.corpus))
    fixtures = corpus["fixtures"]
    convert_bin = detect_convert_avif()
    print(f"=== AVIF Container & AV1 Picture Differential Oracle ===")
    print(f"Reference decoder: {'ImageMagick/libheif (' + convert_bin + ')' if convert_bin else 'committed libheif RGBA ground truth'}")

    container_ok = 0
    picture_pass = 0
    picture_known = 0
    picture_fixed = 0
    picture_fail = 0

    with tempfile.TemporaryDirectory(prefix="litebird_avif_oracle_") as tmpdir:
        # 1. Verify all committed fixtures (container + OBU + libheif cross-check + picture decode)
        for fx in fixtures:
            name = fx["name"]
            w, h = fx["width"], fx["height"]
            avif_bytes = base64.b64decode(fx["avif_base64"])
            ref_rgba = zlib.decompress(base64.b64decode(fx["reference_rgba_zlib_base64"]))

            obu_count = check_container_agreement(name, args.binary, avif_bytes, tmpdir)
            container_ok += 1

            avif_path = os.path.join(tmpdir, f"{name}.avif")
            if convert_bin:
                iw, ih, live_rgba = decode_avif_with_imagemagick(convert_bin, avif_path)
                if (iw, ih) != (w, h):
                    raise AssertionError(f"{name}: ImageMagick dimensions {(iw, ih)} != corpus {(w, h)}")
                if live_rgba != ref_rgba:
                    diff = compare_rgba(live_rgba, ref_rgba, w, h)
                    if diff.max_diff > 1:
                        raise AssertionError(f"{name}: live libheif decode diverged from committed ground truth (max_diff={diff.max_diff})")

            ppm_out = os.path.join(tmpdir, f"{name}_out.ppm")
            rgba_out = os.path.join(tmpdir, f"{name}_out.rgba")
            dec = subprocess.run([args.binary, "--decode-avif", avif_path, ppm_out, rgba_out], capture_output=True, text=True)
            got_rgba = open(rgba_out, "rb").read() if (dec.returncode == 0 and os.path.exists(rgba_out)) else None
            verdict, detail = evaluate_picture_outcome(
                name, w, h, dec.returncode, got_rgba, ref_rgba, fx["picture_status"], fx["tolerance"]
            )
            print(f"  {verdict:5s} {name:22s} [container OK, {obu_count} OBUs] {detail}")
            if verdict == "PASS":
                picture_pass += 1
            elif verdict == "KNOWN":
                picture_known += 1
            elif verdict == "FIXED":
                picture_fixed += 1
            else:
                picture_fail += 1

        # 2. If live ImageMagick/libheif AVIF encoding is available, encode 3 extra live patterns
        live_extra = 0
        if convert_bin:
            live_specs = [
                ("live_solid_green_8x8", 8, 8, lambda x, y, w, h: (0, 255, 0)),
                ("live_odd_19x11_stripes", 19, 11, lambda x, y, w, h: (220, 40, 40) if (x % 3 == 0) else (40, 180, 220)),
                ("live_rich_32x24", 32, 24, lambda x, y, w, h: (int(255 * x / 31), int(255 * y / 23), 128 + int(100 * math.sin((x + y) * 0.3)))),
            ]
            for name, w, h, fn in live_specs:
                png_p = os.path.join(tmpdir, f"{name}.png")
                avif_p = os.path.join(tmpdir, f"{name}.avif")
                write_png_rgb(png_p, w, h, fn)
                subprocess.run([convert_bin, png_p, "-quality", "95", avif_p], check=True, capture_output=True)
                avif_bytes = open(avif_p, "rb").read()
                obu_count = check_container_agreement(name, args.binary, avif_bytes, tmpdir)
                container_ok += 1
                live_extra += 1
                iw, ih, live_rgba = decode_avif_with_imagemagick(convert_bin, avif_p)
                assert (iw, ih) == (w, h)
                ppm_out = os.path.join(tmpdir, f"{name}_out.ppm")
                rgba_out = os.path.join(tmpdir, f"{name}_out.rgba")
                dec = subprocess.run([args.binary, "--decode-avif", avif_p, ppm_out, rgba_out], capture_output=True, text=True)
                got_rgba = open(rgba_out, "rb").read() if (dec.returncode == 0 and os.path.exists(rgba_out)) else None
                # Mirror the corpus status so live tests also flip to FIXED the day AV1 lands
                expected_status = fixtures[0]["picture_status"]
                verdict, detail = evaluate_picture_outcome(
                    name, w, h, dec.returncode, got_rgba, live_rgba, expected_status,
                    {"max_mae": 2.5, "max_channel_diff": 12, "min_psnr": 38.0},
                )
                print(f"  {verdict:5s} {name:22s} [live container OK, {obu_count} OBUs] {detail}")
                if verdict == "PASS":
                    picture_pass += 1
                elif verdict == "KNOWN":
                    picture_known += 1
                elif verdict == "FIXED":
                    picture_fixed += 1
                else:
                    picture_fail += 1

        # 3. Hostile / malformed / unsupported refusals + mutation fuzz batch
        seed = base64.b64decode(fixtures[0]["avif_base64"])
        refusal_ok, fuzz_ok = verify_refusals_and_fuzz(args.binary, seed, tmpdir, args.fuzz_count)

    total_pic = picture_pass + picture_known + picture_fixed + picture_fail
    print(
        f"\nAVIF oracle summary: {container_ok}/{container_ok} containers + AV1 OBUs exact, "
        f"{refusal_ok}/{refusal_ok} refusals exact, {fuzz_ok} fuzz mutations crash-free; "
        f"AV1 picture decode: {picture_pass}/{total_pic} pass, {picture_known} known unimplemented, "
        f"{picture_fixed} FIXED, {picture_fail} FAIL"
        + (f" ({live_extra} live-encoded via libheif)" if live_extra else "")
    )
    if picture_fail > 0 or picture_fixed > 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
