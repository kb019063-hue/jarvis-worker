#!/usr/bin/env python3
"""General OpenType normalization and decomposable-mark probes (Grek, Cyrl, Latn, Common, Laoo); same CLI as compare.py."""
import argparse
import subprocess
import sys
import compare

DEFAULT_CASES = [
    # Greek: tonos/oxia, polytonic chains, equal-class blocking, pre-mark decomposition, CGJ
    ("grek-eps-acute", "\u03b5\u0301", "Grek", "ltr"),
    ("grek-alpha-psili-oxia", "\u03b1\u0313\u0301", "Grek", "ltr"),
    ("grek-alpha-oxia-psili-block", "\u03b1\u0301\u0313", "Grek", "ltr"),
    ("grek-alpha-ypog-psili", "\u03b1\u0345\u0313", "Grek", "ltr"),
    ("grek-precomp-1f04-ypog", "\u1f04\u0345", "Grek", "ltr"),
    ("grek-precomp-alpha-tonos-dot", "\u03ac\u0323", "Grek", "ltr"),
    ("grek-decomposed-alpha-dot-tonos", "\u03b1\u0323\u0301", "Grek", "ltr"),
    ("grek-reordered-alpha-tonos-dot", "\u03b1\u0301\u0323", "Grek", "ltr"),
    ("grek-iota-dialytika-tonos", "\u03b9\u0308\u0301", "Grek", "ltr"),
    ("grek-precomp-iota-dialytika-tonos", "\u03ca\u0301", "Grek", "ltr"),
    ("grek-iota-0344-mark-decomp", "\u03b9\u0344", "Grek", "ltr"),
    ("grek-alpha-0343-0341-marks", "\u03b1\u0343\u0341", "Grek", "ltr"),
    ("grek-cgj-psili-oxia", "\u03b1\u0313\u034f\u0301", "Grek", "ltr"),
    # Cyrillic: composition, pre-mark decomposition, mark ordering, equal-class blocking, CGJ
    ("cyrl-short-i", "\u0438\u0306", "Cyrl", "ltr"),
    ("cyrl-yo", "\u0435\u0308", "Cyrl", "ltr"),
    ("cyrl-yi", "\u0456\u0308", "Cyrl", "ltr"),
    ("cyrl-ie-grave", "\u0435\u0300", "Cyrl", "ltr"),
    ("cyrl-gje", "\u0433\u0301", "Cyrl", "ltr"),
    ("cyrl-yo-dot-precomp", "\u0451\u0323", "Cyrl", "ltr"),
    ("cyrl-yo-dot-reversed", "\u0435\u0308\u0323", "Cyrl", "ltr"),
    ("cyrl-yo-dot-canonical", "\u0435\u0323\u0308", "Cyrl", "ltr"),
    ("cyrl-equal-class-block", "\u0435\u0301\u0308", "Cyrl", "ltr"),
    ("cyrl-cgj-yo", "\u0435\u034f\u0308", "Cyrl", "ltr"),
    # Decomposable combining marks (1-char standalone vs multi-char cluster) & Common overlays
    ("latn-a-0341", "a\u0341", "Latn", "ltr"),
    ("latn-space-0341", " \u0341", "Latn", "ltr"),
    ("latn-0341-dot", "\u0341\u0323", "Latn", "ltr"),
    ("latn-standalone-0341", "\u0341", "Latn", "ltr"),
    ("common-neq", "=\u0338", "Zyyy", "ltr"),
    ("common-nless", "<\u0338", "Zyyy", "ltr"),
    ("common-nleftarrow", "\u2190\u0338", "Zyyy", "ltr"),
]

LAO_CASES = [
    ("lao-tone-vowel-reorder", "\u0e81\u0ec8\u0eb8", "Laoo", "ltr"),
    ("lao-vowel-tone-canonical", "\u0e81\u0eb8\u0ec8", "Laoo", "ltr"),
]


def main() -> int:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--with-lao", action="store_true")
    args, remaining = parser.parse_known_args()
    sys.argv = [sys.argv[0], *remaining]
    compare.CASES = [*DEFAULT_CASES, *LAO_CASES] if args.with_lao else list(DEFAULT_CASES)
    return compare.main()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError, ImportError) as exc:
        print(f"normalization diagnostic error: {exc}", file=sys.stderr)
        sys.exit(2)
