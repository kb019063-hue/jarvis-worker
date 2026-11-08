#!/usr/bin/env python3
"""Mutation fuzzing for the HTML parser.

Takes the conformance corpus as a seed, applies random byte and structural
mutations, and feeds each result to `litebird --dump-dom-webplatform`. A crash
(non-zero exit other than clean failure), a timeout, or a sanitizer report is a
finding.

Run against a sanitizer build for real value:

    make asan          # builds build-asan/litebird and runs this script

Usage:
    python3 tools/fuzz/fuzz_parser.py [--iterations N] [--binary PATH] [--seed S]
"""

from __future__ import annotations

import argparse
import random
import subprocess
import sys
import tempfile
from pathlib import Path

DEFAULT_BINARY = "build-asan/litebird"
CORPUS = Path("tests/html5lib/tree-construction")

INTERESTING = [b"<", b">", b"</", b"<!", b"<![CDATA[", b"&", b"&#", b"&#x", b";",
               b"--", b"<!--", b"-->", b'"', b"'", b"=", b"\x00", b"\xff", b"\r\n",
               b"<table>", b"<svg>", b"<math>", b"<template>", b"<plaintext>",
               b"<script>", b"</script>", b"<style>", b"<select>", b"<li>", b"<p>",
               b"</p>", b"<b>", b"</b>", b"<i>", b"</i>", b"<td>", b"<tr>"]


def seeds() -> list[bytes]:
    out: list[bytes] = []
    for path in sorted(CORPUS.glob("*.dat")):
        for block in path.read_bytes().split(b"\n\n"):
            if not block.startswith(b"#data"):
                continue
            body = block[len(b"#data\n"):]
            marker = body.find(b"\n#errors")
            if marker > 0:
                body = body[:marker]
            if body:
                out.append(body)
    return out


def mutate(data: bytes, rng: random.Random) -> bytes:
    buf = bytearray(data)
    for _ in range(rng.randint(1, 8)):
        choice = rng.random()
        if not buf:
            buf.extend(rng.choice(INTERESTING))
            continue
        if choice < 0.35:
            pos = rng.randrange(len(buf))
            buf[pos] = rng.randrange(256)
        elif choice < 0.6:
            pos = rng.randrange(len(buf))
            token = rng.choice(INTERESTING)
            buf[pos:pos] = token
        elif choice < 0.75:
            pos = rng.randrange(len(buf))
            del buf[pos:pos + rng.randint(1, 16)]
        elif choice < 0.9:
            start = rng.randrange(len(buf))
            end = min(len(buf), start + rng.randint(1, 48))
            pos = rng.randrange(len(buf))
            buf[pos:pos] = buf[start:end]
        else:
            buf = bytearray(bytes(buf) * 2)  # blow-ups and deep nesting
    return bytes(buf)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=DEFAULT_BINARY)
    parser.add_argument("--iterations", type=int, default=400)
    parser.add_argument("--seed", type=int, default=20261107)
    parser.add_argument("--timeout", type=float, default=10.0)
    args = parser.parse_args()

    if not Path(args.binary).exists():
        print(f"error: {args.binary} not found (run `make asan`)", file=sys.stderr)
        return 2

    seed_corpus = seeds()
    if not seed_corpus:
        print("error: no seed inputs found", file=sys.stderr)
        return 2

    rng = random.Random(args.seed)
    findings = 0
    for iteration in range(args.iterations):
        data = mutate(rng.choice(seed_corpus), rng)
        with tempfile.NamedTemporaryFile("wb", suffix=".html", delete=False) as handle:
            handle.write(data)
            path = handle.name
        try:
            completed = subprocess.run([args.binary, path, "--dump-dom-webplatform"],
                                       capture_output=True, timeout=args.timeout)
        except subprocess.TimeoutExpired:
            completed = None
        finally:
            Path(path).unlink(missing_ok=True)

        crashed = completed is None or completed.returncode < 0 or completed.returncode > 1
        sanitizer_report = completed is not None and b"Sanitizer" in completed.stderr
        if crashed or sanitizer_report:
            findings += 1
            reason = "timeout" if completed is None else (
                "sanitizer" if sanitizer_report else f"exit {completed.returncode}")
            artifact = Path("build-asan") / f"crash-{iteration:04d}.html"
            artifact.parent.mkdir(exist_ok=True)
            artifact.write_bytes(data)
            print(f"  ! finding #{findings} ({reason}) saved to {artifact}")
            if completed is not None and completed.stderr:
                print("    " + completed.stderr.decode("utf-8", "replace").splitlines()[0][:160])

    print(f"fuzz: {args.iterations} mutations, {findings} finding(s)")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
