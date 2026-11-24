#!/usr/bin/env python3
"""Checks `TextDecoder` and `TextEncoder` against Node, case by case.

Why this exists
---------------
`AGENTS.md` carried this, twice, and the header of `src/js/TextCodec.cpp` said
the same thing in the same words:

> `TextDecoder` has no `stream` option either: an option that is accepted and
>   ignored would be worse than one that is absent

It has had one since Phase 163. `decode()`'s second argument is read by
`read_stream_option`, the held-back bytes live in `DecoderState::stream_bytes`,
and a three-byte character split across two chunks comes out as it does in
Node. Nothing checked that, so the row went on saying it was missing, and a row
that describes a working feature as absent costs every reader who believes it.

So the check is the point of this harness: run both codecs against Node on the
same inputs and require the same answers. A claim about them then has a command
behind it.

Usage
-----
    python3 tools/conformance/verify_text_codec.py build/litebird
    python3 tools/conformance/verify_text_codec.py build/litebird --dump
    python3 tools/conformance/verify_text_codec.py --list
    python3 tools/conformance/verify_text_codec.py --self-test

`--self-test` checks this script's own comparison on fixed texts, so a harness
that has stopped noticing a difference is visible before it is trusted.

Node is required. If `node` is not on `PATH` the run says so and exits 2 rather
than reporting success it did not establish.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass

# Every case is a snippet whose value is printed as JSON. The engine prints
# `console.log(String(x))`-style text through the harness below; Node prints
# real JSON. Comparing the two therefore compares the *values*, not the
# formatting of two different serialisers.
CASES: list[tuple[str, str]] = [
    # ---- TextDecoder, one shot -------------------------------------------
    ("new TextDecoder().encoding", "the default encoding"),
    ("new TextDecoder('utf-16le').encoding",
     "a label that is not utf-8"),
    ("new TextDecoder('UTF-8').encoding",
     "a label is matched case-insensitively"),
    ("new TextDecoder('iso-8859-1').encoding",
     "a legacy label answers its canonical name"),
    ("new TextDecoder().fatal", "fatal defaults to false"),
    ("new TextDecoder('utf-8', {fatal: true}).fatal", "fatal can be set"),
    ("new TextDecoder().ignoreBOM", "ignoreBOM defaults to false"),
    ("JSON.stringify(new TextDecoder().decode(new Uint8Array([0xEF,0xBB,0xBF,0x41])))",
     "a BOM is stripped"),
    ("JSON.stringify(new TextDecoder('utf-8',{ignoreBOM:true}).decode(new Uint8Array([0xEF,0xBB,0xBF,0x41])))",
     "ignoreBOM keeps it"),
    ("JSON.stringify(new TextDecoder().decode(new Uint8Array([0xE2,0x82,0xAC])))",
     "a three-byte character"),
    ("JSON.stringify(new TextDecoder().decode(new Uint8Array([0xE2,0x82])))",
     "a truncated character is replaced"),
    ("JSON.stringify(new TextDecoder('utf-8',{fatal:true}).decode(new Uint8Array([0xE2,0x82])))",
     "a truncated character throws under fatal -- caught by the harness"),

    # ---- TextDecoder, streaming -- the thing the row said was missing ----
    # A character split across two chunks is the whole of the `stream`
    # option's visible behaviour: hold the partial bytes, and produce the
    # character once they arrive.
    ("(function(){ var d = new TextDecoder();"
     "  return JSON.stringify(d.decode(new Uint8Array([0xE2,0x82]),{stream:true}))"
     "    + JSON.stringify(d.decode(new Uint8Array([0xAC]))); })()",
     "a split three-byte character"),
    ("(function(){ var d = new TextDecoder();"
     "  d.decode(new Uint8Array([0xE2,0x82]),{stream:true});"
     "  return JSON.stringify(d.decode()); })()",
     "flushing a held-back tail replaces it"),
    ("(function(){ var d = new TextDecoder('utf-8',{fatal:true});"
     "  var held = d.decode(new Uint8Array([0xE2]),{stream:true});"
     "  try { d.decode(); return JSON.stringify(held) + 'no throw'; }"
     "  catch (e) { return JSON.stringify(held) + e.name; } })()",
     "fatal holds mid-stream and throws at the flush"),
    ("(function(){ var d = new TextDecoder();"
     "  return JSON.stringify(d.decode(new Uint8Array([0x41]),{stream:true})"
     "    + d.decode(new Uint8Array([0x42]),{stream:true})"
     "    + d.decode(new Uint8Array([0x43]))); })()",
     "three chunks, one character each"),

    # ---- TextEncoder ------------------------------------------------------
    ("new TextEncoder().encoding", "the encoder's encoding"),
    ("JSON.stringify(Array.from(new TextEncoder().encode('a')))",
     "one ASCII byte"),
    ("JSON.stringify(Array.from(new TextEncoder().encode('\\u00e9')))",
     "two bytes for one character"),
    ("JSON.stringify(Array.from(new TextEncoder().encode('\\uD83D\\uDE00')))",
     "four bytes for an astral character"),
    ("JSON.stringify(Array.from(new TextEncoder().encode('\\uD800')))",
     "a lone surrogate becomes U+FFFD"),
    ("(function(){ var b = new Uint8Array(4);"
     "  var r = new TextEncoder().encodeInto('ab', b);"
     "  return JSON.stringify([r.read, r.written, Array.from(b)]); })()",
     "encodeInto answers read and written"),

    # ---- the `length` of each, which is where a defect was ----------------
    ("TextDecoder.length", "the decoder constructor's length"),
    ("TextEncoder.length", "the encoder constructor's length"),
    ("new TextDecoder().decode.length",
     "decode's length: the Encoding Standard says 0"),
    ("new TextEncoder().encode.length", "encode's length"),
    ("new TextEncoder().encodeInto.length", "encodeInto's length"),
]


@dataclass(frozen=True)
class Answer:
    value: str
    threw: str = ""


def run_engine(binary: str, source: str) -> Answer:
    """The engine's answer, through `--run-js` with `console.log(String(...))`."""
    wrapped = f"try {{ console.log(String(({source}))); }} catch (e) {{ console.log('THREW ' + e.name); }}"
    finished = subprocess.run(
        [binary, "--run-js", wrapped],
        capture_output=True, text=True, timeout=120, check=False,
    )
    text = finished.stdout.strip().splitlines()
    if not text:
        return Answer("", "no output")
    last = text[-1].strip()
    if last.startswith("THREW "):
        return Answer("", last[len("THREW "):])
    return Answer(last)


def run_node(source: str) -> Answer:
    """The same value from Node, which is the oracle."""
    script = (f"try {{ console.log(String(({source}))); }}"
              f" catch (e) {{ console.log('THREW ' + e.name); }}")
    finished = subprocess.run(["node", "-e", script], capture_output=True, text=True,
                              timeout=120, check=False)
    text = finished.stdout.strip().splitlines()
    if not text:
        return Answer("", (finished.stderr.strip().splitlines() or ["no output"])[0])
    last = text[-1].strip()
    if last.startswith("THREW "):
        return Answer("", last[len("THREW "):])
    return Answer(last)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("binary", nargs="?", help="the litebird binary")
    parser.add_argument("--list", action="store_true", help="print every case")
    parser.add_argument("--dump", action="store_true", help="print every answer")
    parser.add_argument("--self-test", action="store_true", help="check the comparison only")
    args = parser.parse_args(argv[1:])

    if args.self_test:
        # The failure this guards against is a harness that compares nothing
        # and says "ok". Each pair below must be judged as written.
        pairs = [
            (Answer('"utf-8"'), Answer('"utf-8"'), True),
            (Answer('"utf-8"'), Answer('"utf-16le"'), False),
            (Answer("", "TypeError"), Answer("", "TypeError"), True),
            (Answer("", "TypeError"), Answer(""), False),
            (Answer("0"), Answer(""), False),
            (Answer("0"), Answer("0"), True),
        ]
        for engine, node, must_agree in pairs:
            agree = engine == node
            if agree != must_agree:
                print(f"self-test: {engine} vs {node} judged agree={agree}, expected {must_agree}")
                return 1
        print(f"verify_text_codec: self-test ok ({len(pairs)} comparisons)")
        return 0

    if args.list:
        for source, note in CASES:
            print(f"{source}\n    {note}")
        print(f"\n{len(CASES)} case(s)")
        return 0

    if not shutil.which("node"):
        print("verify_text_codec: node is not on PATH; the oracle is missing", file=sys.stderr)
        return 2
    binary = args.binary
    if not binary:
        print("verify_text_codec: no binary given", file=sys.stderr)
        return 2

    failures = 0
    for source, note in CASES:
        engine = run_engine(binary, source)
        node = run_node(source)
        if args.dump:
            print(f"  {note}")
            print(f"    engine {engine}  node {node}")
        if engine != node:
            failures += 1
            print(f"  FAIL  {note}")
            print(f"        {source}")
            print(f"        the engine answers {engine}")
            print(f"        Node answers        {node}")

    if failures:
        print(f"\nverify_text_codec: {failures} of {len(CASES)} differ")
        return 1
    print(f"verify_text_codec: {len(CASES)}/{len(CASES)} agree with Node")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
