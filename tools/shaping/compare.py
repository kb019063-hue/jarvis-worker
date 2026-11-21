#!/usr/bin/env python3
"""Exact single-face, single-script shaping differential; differences exit 1."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

# Each input is one directional script run, not a bidi paragraph.
CASES = [
    ("empty", "", "Latn", "ltr"),
    ("latin", "Hello world", "Latn", "ltr"),
    ("kerning", "AVATAR To Wa", "Latn", "ltr"),
    ("ligatures", "office affinity ffi fl", "Latn", "ltr"),
    ("combining", "a\u0301 x\u0323\u0301", "Latn", "ltr"),
    ("greek", "Ελληνικά", "Grek", "ltr"),
    ("cyrillic", "Привет", "Cyrl", "ltr"),
    ("hebrew", "שלום", "Hebr", "rtl"),
    ("hebrew-marks", "שָׁלוֹם", "Hebr", "rtl"),
    ("arabic", "سلام", "Arab", "rtl"),
    ("arabic-marks", "السَّلَامُ", "Arab", "rtl"),
    ("arabic-zwnj", "ب\u200cب", "Arab", "rtl"),
    ("arabic-zwj", "ب\u200dب", "Arab", "rtl"),
    ("devanagari", "नमस्ते", "Deva", "ltr"),
]


def compare(expected, actual):
    return expected == actual


def self_test():
    sample = [[42, 600, 0, 0, 0], [43, 0, 0, -20, 50]]
    assert compare(sample, [row[:] for row in sample])
    for column in range(5):
        mutated = [row[:] for row in sample]
        mutated[0][column] += 1
        assert not compare(sample, mutated), f"missed column {column}"
    assert not compare(sample, sample[::-1])
    assert not compare(sample, sample[:1])
    print("negative controls: IDs, advances, offsets, order and count detected", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path)
    parser.add_argument("--probe", default="build/shaping-probe")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    self_test()
    if args.self_test:
        return 0
    if args.font is None:
        parser.error("--font is required (no silent system-font substitution)")
    import uharfbuzz as hb
    data = args.font.read_bytes()
    face = hb.Face(data)
    font = hb.Font(face)
    hb.ot_font_set_funcs(font)
    font.scale = (face.upem, face.upem)
    results = []
    for name, text, script, direction in CASES:
        buf = hb.Buffer()
        buf.add_str(text)
        buf.script, buf.direction, buf.language = script, direction, "und"
        hb.shape(font, buf)
        expected = [[g.codepoint, p.x_advance, p.y_advance, p.x_offset, p.y_offset]
                    for g, p in zip(buf.glyph_infos or [], buf.glyph_positions or [])]
        proc = subprocess.run([args.probe, str(args.font), direction,
                               *[f"{ord(c):x}" for c in text]],
                              capture_output=True, text=True, timeout=15, check=True)
        actual = json.loads(proc.stdout)
        if not isinstance(actual, list) or any(
                not isinstance(row, list) or len(row) != 5 or
                any(type(v) is not int for v in row) for row in actual):
            raise ValueError("invalid probe output")
        # .notdef agreement is not evidence of script support.
        missing = any(row[0] == 0 for row in expected + actual)
        status = "unsupported" if missing else "match" if compare(expected, actual) else "mismatch"
        results.append(dict(name=name, text=text, script=script, direction=direction,
                            status=status, harfbuzz=expected, litebird=actual))
        print(f"{name}: {status}", file=sys.stderr)
    report = dict(font=str(args.font), sha256=hashlib.sha256(data).hexdigest(),
                  uharfbuzz=hb.__version__, harfbuzz=hb.version_string(),
                  units_per_em=face.upem, language="und", features="defaults",
                  fields=["glyph", "x_advance", "y_advance", "x_offset", "y_offset"],
                  results=results)
    rendered = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered)
    else:
        print(rendered, end="")
    counts = {s: sum(r["status"] == s for r in results)
              for s in ("match", "mismatch", "unsupported")}
    print(counts, file=sys.stderr)
    return int(any(r["status"] != "match" for r in results))


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError, ImportError) as exc:
        print(f"shaping oracle error: {exc}", file=sys.stderr)
        sys.exit(2)
