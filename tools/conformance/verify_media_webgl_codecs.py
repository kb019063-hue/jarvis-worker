#!/usr/bin/env python3
"""Spec-backed conformance oracle for HTMLMediaElement (<audio>/<video>),
MediaSource/Web Audio, WebCodecs, and WebGL 1/2.

Executes every case in tests/conformance/media_webgl_codecs.json inside
Litebird via `--run-script` against expected values derived strictly from the
published WHATWG HTML, W3C MSE / Web Audio / WebCodecs, and Khronos WebGL
specifications.

Grading contract (same as tools/conformance/render_check.py and verify_avif.py):
  - PASS  : case without known_failure matches spec-expected JSON value.
  - KNOWN : case with known_failure does NOT match expected (printed with reason,
            never counted as a pass).
  - FIXED : case with known_failure NOW matches expected -> exits 1 so the
            implementer removes known_failure and promotes it to permanent CI.
  - FAIL  : case without known_failure differs, throws unexpectedly, or goes
            missing -> exits 1 immediately.
"""

from __future__ import annotations

import argparse
import base64
import json
import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CORPUS = ROOT / "tests/conformance/media_webgl_codecs.json"
DEFAULT_BINARY = ROOT / "build/litebird"
MARKER = "__LB_MWC_RESULT__:"


def make_pcm_wav_bytes(sample_rate: int = 8000, duration_s: float = 0.1, freq_hz: float = 440.0) -> bytes:
    """Generate a valid uncompressed 16-bit mono PCM RIFF/WAVE file."""
    n_samples = int(round(sample_rate * duration_s))
    pcm = bytearray()
    for i in range(n_samples):
        val = int(round(16000.0 * math.sin(2.0 * math.pi * freq_hz * i / sample_rate)))
        pcm.extend(struct.pack("<h", max(-32768, min(32767, val))))
    data_size = len(pcm)
    num_channels = 1
    bits_per_sample = 16
    byte_rate = sample_rate * num_channels * (bits_per_sample // 8)
    block_align = num_channels * (bits_per_sample // 8)
    fmt_chunk = struct.pack(
        "<4sIHHIIHH",
        b"fmt ",
        16,
        1,  # PCM
        num_channels,
        sample_rate,
        byte_rate,
        block_align,
        bits_per_sample,
    )
    data_chunk = struct.pack("<4sI", b"data", data_size) + bytes(pcm)
    riff_size = 4 + len(fmt_chunk) + len(data_chunk)
    return struct.pack("<4sI4s", b"RIFF", riff_size, b"WAVE") + fmt_chunk + data_chunk


def make_pcm_wav_data_uri() -> str:
    return "data:audio/wav;base64," + base64.b64encode(make_pcm_wav_bytes()).decode("ascii")


def build_probe_script(cases: list[dict[str, Any]], wav_data_uri: str) -> str:
    lines: list[str] = [
        f"var __PCM_WAV_DATA_URI__ = {json.dumps(wav_data_uri)};",
        f"var __MARKER__ = {json.dumps(MARKER)};",
        "function __emit__(name, status, value) {",
        "  console.log(__MARKER__ + JSON.stringify({ name: name, status: status, value: value }));",
        "}",
        "function __runSync__(name, fn) {",
        "  try {",
        "    __emit__(name, 'ok', fn());",
        "  } catch (e) {",
        "    __emit__(name, 'throw', (e && e.name ? e.name + ': ' : '') + String(e));",
        "  }",
        "}",
        "function __runAsync__(name, fn) {",
        "  try {",
        "    var res = fn();",
        "    if (res && typeof res.then === 'function') {",
        "      res.then(",
        "        function(v) { __emit__(name, 'ok', v); },",
        "        function(e) { __emit__(name, 'throw', (e && e.name ? e.name + ': ' : '') + String(e)); }",
        "      );",
        "    } else {",
        "      __emit__(name, 'ok', res);",
        "    }",
        "  } catch (e) {",
        "    __emit__(name, 'throw', (e && e.name ? e.name + ': ' : '') + String(e));",
        "  }",
        "}",
    ]
    for c in cases:
        runner = "__runAsync__" if c.get("async") else "__runSync__"
        lines.append(f"{runner}({json.dumps(c['name'])}, function() {{ {c['script']} }});")
    return "\n".join(lines) + "\n"


def parse_probe_output(stdout: str) -> dict[str, dict[str, Any]]:
    results: dict[str, dict[str, Any]] = {}
    for line in stdout.splitlines():
        if line.startswith(MARKER):
            payload = json.loads(line[len(MARKER) :])
            results[payload["name"]] = payload
    return results


def grade_cases(
    cases: list[dict[str, Any]],
    observed: dict[str, dict[str, Any]],
    strict: bool = False,
) -> tuple[list[dict[str, Any]], dict[str, int]]:
    rows: list[dict[str, Any]] = []
    counts = {"pass": 0, "known": 0, "fixed": 0, "fail": 0}
    for case in cases:
        name = case["name"]
        expected = case["expected"]
        known_reason = case.get("known_failure")
        rec = observed.get(name)
        if rec is None:
            matched = False
            actual_repr = "<missing output>"
            status_raw = "missing"
        else:
            status_raw = rec.get("status", "unknown")
            actual_val = rec.get("value")
            matched = status_raw == "ok" and actual_val == expected
            actual_repr = (
                f"THROWS {actual_val}" if status_raw == "throw" else json.dumps(actual_val)
            )

        if known_reason:
            if matched:
                verdict = "FIXED"
                counts["fixed"] += 1
            elif strict:
                verdict = "FAIL"
                counts["fail"] += 1
            else:
                verdict = "KNOWN"
                counts["known"] += 1
        else:
            if matched:
                verdict = "PASS"
                counts["pass"] += 1
            else:
                verdict = "FAIL"
                counts["fail"] += 1

        rows.append(
            {
                "name": name,
                "domain": case["domain"],
                "verdict": verdict,
                "expected": json.dumps(expected),
                "actual": actual_repr,
                "known_failure": known_reason,
                "status_raw": status_raw,
            }
        )
    return rows, counts


def run_self_tests() -> bool:
    """5 built-in negative controls verifying the grader and WAV fixture."""
    checks_passed = 0
    total = 5

    # 1. Passing case with wrong observed output MUST produce FAIL
    c1 = [{"name": "a", "domain": "webgl", "expected": [1, 2]}]
    _, cnt1 = grade_cases(c1, {"a": {"status": "ok", "value": [1, 999]}})
    assert cnt1["fail"] == 1 and cnt1["pass"] == 0, f"Control 1 failed: {cnt1}"
    checks_passed += 1

    # 2. Passing case that throws unexpectedly MUST produce FAIL
    _, cnt2 = grade_cases(c1, {"a": {"status": "throw", "value": "TypeError: boom"}})
    assert cnt2["fail"] == 1 and cnt2["pass"] == 0, f"Control 2 failed: {cnt2}"
    checks_passed += 1

    # 3. known_failure case that now matches expected MUST produce FIXED
    c3 = [{"name": "b", "domain": "webgl", "expected": True, "known_failure": "unimplemented"}]
    _, cnt3 = grade_cases(c3, {"b": {"status": "ok", "value": True}})
    assert cnt3["fixed"] == 1 and cnt3["known"] == 0, f"Control 3 failed: {cnt3}"
    checks_passed += 1

    # 4. Missing output line (e.g. silent crash) MUST produce FAIL on a non-known case
    _, cnt4 = grade_cases(c1, {})
    assert cnt4["fail"] == 1 and cnt4["pass"] == 0, f"Control 4 failed: {cnt4}"
    checks_passed += 1

    # 5. Generated 100ms 8kHz mono 16-bit PCM WAV header & payload structure
    wav = make_pcm_wav_bytes(sample_rate=8000, duration_s=0.1)
    assert wav[:4] == b"RIFF" and wav[8:12] == b"WAVE", "Bad RIFF/WAVE header"
    riff_size = struct.unpack_from("<I", wav, 4)[0]
    assert riff_size + 8 == len(wav), f"Bad RIFF size: {riff_size + 8} != {len(wav)}"
    fmt_tag, fmt_size, audio_fmt, channels, rate, _, _, bps = struct.unpack_from("<4sIHHIIHH", wav, 12)
    assert (fmt_tag, fmt_size, audio_fmt, channels, rate, bps) == (b"fmt ", 16, 1, 1, 8000, 16)
    data_tag, data_size = struct.unpack_from("<4sI", wav, 36)
    assert data_tag == b"data" and data_size == 800 * 2 and len(wav) == 44 + 1600
    checks_passed += 1

    print(f"self-test: {checks_passed}/{total} negative controls verified")
    return checks_passed == total


def run_corpus(binary: Path, corpus_path: Path, strict: bool = False, quiet: bool = False) -> int:
    if not run_self_tests():
        return 1

    corpus = json.loads(corpus_path.read_text(encoding="utf-8"))
    cases = corpus["cases"]
    wav_uri = make_pcm_wav_data_uri()
    script_src = build_probe_script(cases, wav_uri)
    html_src = "<!doctype html><html><head><meta charset=\"utf-8\"></head><body></body></html>\n"

    with tempfile.TemporaryDirectory(prefix="litebird-mwc-") as tmp:
        page = Path(tmp) / "probe.html"
        script = Path(tmp) / "probe.js"
        page.write_text(html_src, encoding="utf-8")
        script.write_text(script_src, encoding="utf-8")
        proc = subprocess.run(
            [str(binary), "--run-script", str(script), str(page)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=60,
        )

    if proc.returncode != 0:
        print(f"FAIL binary exited {proc.returncode}:\n{proc.stderr}", file=sys.stderr)
        return 1

    observed = parse_probe_output(proc.stdout)
    rows, counts = grade_cases(cases, observed, strict=strict)

    domain_stats: dict[str, dict[str, int]] = {}
    for row in rows:
        d = domain_stats.setdefault(row["domain"], {"pass": 0, "known": 0, "fixed": 0, "fail": 0})
        d[row["verdict"].lower()] += 1
        if not quiet or row["verdict"] in ("FAIL", "FIXED"):
            if row["verdict"] == "PASS":
                print(f"PASS  [{row['domain']}] {row['name']}")
            elif row["verdict"] == "KNOWN":
                print(
                    f"KNOWN [{row['domain']}] {row['name']}: {row['known_failure']} "
                    f"(expected {row['expected']}, got {row['actual']})"
                )
            elif row["verdict"] == "FIXED":
                print(
                    f"FIXED [{row['domain']}] {row['name']}: now matches {row['expected']}; "
                    f"remove known_failure from {corpus_path.relative_to(ROOT)}"
                )
            else:
                print(
                    f"FAIL  [{row['domain']}] {row['name']}: "
                    f"expected {row['expected']}, got {row['actual']}"
                )

    total = len(cases)
    parts = ", ".join(
        f"{dom}={st['pass']}/{sum(st.values())} pass ({st['known']} known)"
        for dom, st in domain_stats.items()
    )
    print(
        f"media/webgl/codecs conformance: {counts['pass']}/{total} pass, "
        f"{counts['known']} known, {counts['fixed']} fixed, {counts['fail']} fail "
        f"[{parts}]"
    )
    return 1 if (counts["fail"] > 0 or counts["fixed"] > 0) else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--corpus", type=Path, default=DEFAULT_CORPUS)
    parser.add_argument("--self-test", action="store_true", help="Run built-in negative controls only")
    parser.add_argument("--strict", action="store_true", help="Fail on known_failure entries too")
    parser.add_argument("--quiet", action="store_true", help="Only print failures, fixes, and summary")
    args = parser.parse_args()

    if args.self_test:
        return 0 if run_self_tests() else 1

    binary = args.binary if args.binary.is_absolute() else (ROOT / args.binary)
    corpus = args.corpus if args.corpus.is_absolute() else (ROOT / args.corpus)
    if not binary.exists():
        print(f"FAIL binary not found: {binary}", file=sys.stderr)
        return 1
    return run_corpus(binary, corpus, strict=args.strict, quiet=args.quiet)


if __name__ == "__main__":
    raise SystemExit(main())
