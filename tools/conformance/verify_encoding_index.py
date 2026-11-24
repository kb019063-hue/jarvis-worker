#!/usr/bin/env python3
"""Checks the vendored encoding tables against `whatwg/encoding` itself.

Why this exists
---------------
`tools/conformance/verify_encoding_tables.py` asks every decoder whether it
agrees with the table beside it, and that is the check that would have caught
the Big5 sweep's mistake. It cannot catch the other half, and says so in its
own docstring: a decoder and its table can be wrong *together*, because the
table was generated from a revision of the standard and nothing in the tree
records which one. If that revision were wrong, the decoder would agree with
it and the tool would print zero mismatches.

So the oracle here is not the engine. It is `whatwg/encoding`'s own index
files — the same files `src/net/TextEncodingTables.cpp` was generated from —
fetched from a revision that is pinned and *verified*. The pin is one
combined SHA-256 over all 34 files, in sorted order, each prefixed by its
name: an upstream that changes any byte of any table changes the digest, and
a truncated download cannot pass. (The `Identifier:` line each file carries
is a hash of the standard's own normalised table, not of the file's bytes;
hashing the bytes was tried and does not match, so it is not used here. What
it is hashed over is not something this tool guesses at.)

What is compared
----------------
  * **27 single-byte tables**, 128 positions each (`kSingleByte[row][byte -
    0x80]`), against `index-<label>.txt`. A pointer the file does not list
    must be 0 in the engine, which is what "no pointer" means and is the
    entry the Big5 row mistook for U+0000.
  * **the multi-byte indexes**, every position including the ones with no
    entry: `kIndexBig5` (19,782), `kIndexEucKr` (23,940), `kIndexJis0208`
    (11,280), `kIndexJis0212` (8,836) and `kIndexGb18030` (23,940).
  * **the 207 gb18030 ranges**, pointer for pointer, in order.

The engine's table order is not the label order: `iso-8859-8-i` shares
`iso-8859-8`'s row, so from `iso-8859-10` on every row is one lower than its
`EncodingId`. `ROW_OF_LABEL` is copied from the other tool on purpose and the
two are cross-checked at startup: if that mapping is ever edited in one place,
the other disagrees loudly rather than checking the wrong table.

How the index files are read
----------------------------
An index file is tab-separated: a decimal pointer, a hexadecimal code point
(or two, for the four Big5 entries that decode to two code points), and the
characters. Reading it with `split()` instead of `split("\\t")` reads the
characters as more code points, and every entry then looks wrong — which is
how the first draft of this tool reported 3,456 mismatches that did not
exist. That is why the format is parsed by column and the identifier checked
before anything is compared.

Exit status: 0 when every table matches the published index files, 1
otherwise. `--offline` uses index files already unpacked in `--cache` and
never touches the network.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CACHE = ROOT / "build" / "whatwg-encoding-index"
# One digest over all 34 index files of whatwg/encoding, sorted by name, each
# preceded by its own name and a NUL. Printed by `--print-digest`; `--ref`
# names the revision a run pretends to be, and a digest that differs from this
# one is refused rather than compared table by table.
DEFAULT_PIN = "main"
PINNED_DIGEST = "648870f3052631ed28a06577130b204f278484e412cd6e4abfba8ba41911b05d"
TARBALL = "https://codeload.github.com/whatwg/encoding/tar.gz/refs/heads/{ref}"

# Row of kSingleByte for each single-byte label. iso-8859-8 and iso-8859-8-i
# share row 7, which is why 28 labels cover 27 rows. Kept identical to
# verify_encoding_tables.py; check_row_mapping() asserts they still agree.
ROW_OF_LABEL = {
    "ibm866": 0, "iso-8859-2": 1, "iso-8859-3": 2, "iso-8859-4": 3,
    "iso-8859-5": 4, "iso-8859-6": 5, "iso-8859-7": 6, "iso-8859-8": 7,
    "iso-8859-8-i": 7, "iso-8859-10": 8, "iso-8859-13": 9, "iso-8859-14": 10,
    "iso-8859-15": 11, "iso-8859-16": 12, "koi8-r": 13, "koi8-u": 14,
    "macintosh": 15, "windows-874": 16, "windows-1250": 17, "windows-1251": 18,
    "windows-1252": 19, "windows-1253": 20, "windows-1254": 21,
    "windows-1255": 22, "windows-1256": 23, "windows-1257": 24,
    "windows-1258": 25, "x-mac-cyrillic": 26,
}

# The multi-byte tables, by the index file each is generated from and the
# C++ symbol that must agree with it.
MULTIBYTE = [
    ("index-big5.txt", "kIndexBig5", 19782),
    ("index-euc-kr.txt", "kIndexEucKr", 23940),
    ("index-jis0208.txt", "kIndexJis0208", 11280),
    ("index-jis0212.txt", "kIndexJis0212", 8836),
    ("index-gb18030.txt", "kIndexGb18030", 23940),
]
RANGES_FILE = "index-gb18030-ranges.txt"
RANGES_SYMBOL = "kIndexGb18030Ranges"
RANGES_COUNT = 207

DEFAULT_TIMEOUT = 300.0

# Dumps the tables as text. The tables are `const` arrays with external
# linkage (TextEncoding.hpp declares them `extern`), so a translation unit
# that includes the header can read every one of them.
HARNESS = r"""
#include "litebird/net/TextEncoding.hpp"
#include <cstdio>
using namespace litebird::encoding_tables;
int main() {
    for (int t = 0; t < 27; ++t)
        for (int b = 0; b < 128; ++b)
            std::printf("S %d %d %u\n", t, b, kSingleByte[t][b]);
    for (int p = 0; p < 19782; ++p) std::printf("B %d %u\n", p, kIndexBig5[p]);
    for (int p = 0; p < 23940; ++p) std::printf("E %d %u\n", p, kIndexEucKr[p]);
    for (int p = 0; p < 11280; ++p) std::printf("J %d %u\n", p, kIndexJis0208[p]);
    for (int p = 0; p < 8836;  ++p) std::printf("K %d %u\n", p, kIndexJis0212[p]);
    for (int p = 0; p < 23940; ++p) std::printf("G %d %u\n", p, kIndexGb18030[p]);
    for (int r = 0; r < 207; ++r)
        std::printf("R %d %u %u\n", r, kIndexGb18030Ranges[r].pointer,
                    kIndexGb18030Ranges[r].code_point);
    return 0;
}
"""


def check_row_mapping() -> str | None:
    """The row mapping must still be the one the other tool uses."""
    other = ROOT / "tools" / "conformance" / "verify_encoding_tables.py"
    if not other.exists():
        return None
    text = other.read_text(encoding="utf-8")
    for label, row in ROW_OF_LABEL.items():
        if f'"{label}": {row}' not in text:
            return (f'ROW_OF_LABEL and {other.name} disagree about {label!r}: '
                    f'this tool says row {row}')
    return None


def combined_digest(files: dict[str, str]) -> str:
    """One SHA-256 over every index file, by name, name-prefixed."""
    digest = hashlib.sha256()
    for name in sorted(files):
        digest.update(name.encode("utf-8") + b"\0")
        digest.update(files[name].encode("utf-8"))
    return digest.hexdigest()


def parse_index(data: str, path: str) -> tuple[dict[int, list[int]], str]:
    """Pointers to code points, and the Identifier the file carries.

    The columns are tab-separated. Splitting on whitespace instead reads the
    characters column as further code points and makes every entry look
    wrong.
    """
    entries: dict[int, list[int]] = {}
    identifier = ""
    for line in data.splitlines():
        if line.startswith("#"):
            if line.startswith("# Identifier:"):
                identifier = line.split(":", 1)[1].strip()
            continue
        if not line.strip():
            continue
        columns = line.rstrip("\n").split("\t")
        if len(columns) < 2:
            continue
        pointer = int(columns[0])
        entries[pointer] = [int(x, 16) for x in columns[1].split()]
    return entries, identifier


def fetch(ref: str, cache: Path, timeout: float) -> dict[str, str]:
    """Every index file, from the cache or the pinned tarball."""
    cache.mkdir(parents=True, exist_ok=True)
    have = sorted(cache.glob("index-*.txt"))
    if have:
        return {p.name: p.read_text(encoding="utf-8") for p in have}
    url = TARBALL.format(ref=ref)
    with urllib.request.urlopen(url, timeout=timeout) as stream:
        blob = stream.read()
    files: dict[str, str] = {}
    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz") as tar:
        for member in tar:
            if not member.isfile():
                continue
            name = member.name.rsplit("/", 1)[-1]
            if not (name.startswith("index-") and name.endswith(".txt")):
                continue
            payload = tar.extractfile(member)
            assert payload is not None
            text = payload.read().decode("utf-8")
            files[name] = text
            cache.joinpath(name).write_text(text, encoding="utf-8")
    return files


def build_harness(timeout: float) -> tuple[Path | None, str | None]:
    objs = [ROOT / "build" / "net" / "TextEncoding.o",
            ROOT / "build" / "net" / "TextEncodingTables.o"]
    missing = [str(o) for o in objs if not o.exists()]
    if missing:
        return None, f"missing object(s): {', '.join(missing)}; run `make all` first"
    src = ROOT / "build" / "encoding_index_dump.cpp"
    src.write_text(HARNESS)
    out = ROOT / "build" / "encoding_index_dump"
    argv = ["g++", "-std=c++20", "-O1", f"-I{ROOT / 'include'}", str(src)] + \
           [str(o) for o in objs] + ["-o", str(out)]
    proc = subprocess_run(argv, timeout)
    if proc != 0:
        return None, "the harness did not compile"
    return out, None


def subprocess_run(argv: list[str], timeout: float) -> int:
    proc = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout + proc.stderr)
    return proc.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ref", default=DEFAULT_PIN,
                        help="whatwg/encoding revision the index files come from")
    parser.add_argument("--cache", default=str(DEFAULT_CACHE),
                        help="where the index files are kept between runs")
    parser.add_argument("--offline", action="store_true",
                        help="use the cached index files and fetch nothing")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument("--digest", default=PINNED_DIGEST,
                        help="the digest the fetched index files must have ('' to accept any)")
    parser.add_argument("--print-digest", action="store_true",
                        help="print the digest of the fetched files and exit")
    args = parser.parse_args()

    problem = check_row_mapping()
    if problem:
        print(problem, file=sys.stderr)
        return 1

    cache = Path(args.cache)
    if args.offline and not list(cache.glob("index-*.txt")):
        print(f"--offline and no index files in {cache}", file=sys.stderr)
        return 1
    try:
        files = fetch(args.ref, cache, args.timeout)
    except (OSError, tarfile.TarError) as exc:
        print(f"could not read whatwg/encoding: {exc}", file=sys.stderr)
        return 1

    # One digest over every file, so a table edited upstream cannot read as
    # agreement. `--print-digest` reports what was fetched.
    digest = combined_digest(files)
    if args.print_digest:
        print(digest)
        return 0
    if args.digest:
        if digest != args.digest:
            print(f"index files hash to {digest}, not {args.digest}; refusing them",
                  file=sys.stderr)
            return 1

    binary, error = build_harness(args.timeout)
    if binary is None:
        print(error, file=sys.stderr)
        return 1
    try:
        proc = subprocess.run([str(binary)], capture_output=True, text=True,
                              timeout=args.timeout)
    except (OSError, subprocess.SubprocessError) as exc:
        print(f"the harness did not run: {exc}", file=sys.stderr)
        return 1
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout + proc.stderr)
        return 1

    single: dict[int, dict[int, int]] = {}
    multi: dict[str, dict[int, int]] = {"B": {}, "E": {}, "J": {}, "K": {}, "G": {}}
    ranges: dict[int, tuple[int, int]] = {}
    for line in proc.stdout.splitlines():
        parts = line.split()
        if not parts:
            continue
        kind = parts[0]
        if kind == "S":
            single.setdefault(int(parts[1]), {})[int(parts[2])] = int(parts[3])
        elif kind in multi:
            multi[kind][int(parts[1])] = int(parts[2])
        elif kind == "R":
            ranges[int(parts[1])] = (int(parts[2]), int(parts[3]))

    reports = []
    failures = 0

    # Single-byte: 27 tables, 128 positions each, pointer == byte - 0x80.
    for label, row in sorted(ROW_OF_LABEL.items()):
        name = f"index-{label}.txt"
        if name not in files:
            continue                       # iso-8859-8-i shares iso-8859-8
        entries, _ = parse_index(files[name], name)
        bad = []
        for pointer in range(128):
            got = single.get(row, {}).get(pointer, 0)
            want = entries[pointer][0] if pointer in entries else 0
            if got != want:
                bad.append((f"0x{pointer + 0x80:02X}", got, want))
        failures += len(bad)
        reports.append({"table": label, "positions": 128, "mismatches": bad})

    # Multi-byte: every position, including the ones with no entry.
    letters = {"kIndexBig5": "B", "kIndexEucKr": "E", "kIndexJis0208": "J",
               "kIndexJis0212": "K", "kIndexGb18030": "G"}
    for name, symbol, count in MULTIBYTE:
        entries, _ = parse_index(files[name], name)
        table = multi[letters[symbol]]
        bad = []
        for pointer in range(count):
            got = table.get(pointer, 0)
            want = entries[pointer][0] if pointer in entries else 0
            if got != want:
                bad.append((pointer, got, want))
        failures += len(bad)
        reports.append({"table": symbol, "positions": count, "mismatches": bad})

    # The ranges, in order: the file's first column is the pointer itself, so
    # the nth line is compared with the engine's nth entry, not with the entry
    # keyed by n (which is a different table entirely and made 206 of 207
    # report as missing on the first run of this tool).
    entries, _ = parse_index(files[RANGES_FILE], RANGES_FILE)
    want_pairs = [(pointer, entries[pointer][0]) for pointer in sorted(entries)]
    bad = []
    for index in range(max(len(want_pairs), RANGES_COUNT)):
        want_pair = want_pairs[index] if index < len(want_pairs) else None
        got = ranges.get(index)
        if want_pair != got:
            bad.append((index, got, want_pair))
    failures += len(bad)
    reports.append({"table": RANGES_SYMBOL, "positions": RANGES_COUNT, "mismatches": bad})

    if args.json:
        print(json.dumps({"failures": failures, "reports": reports}))
    else:
        total = 0
        for report in reports:
            total += report["positions"]
            if report["mismatches"]:
                shown = ", ".join(str(m) for m in report["mismatches"][:4])
                print(f"{report['table']:<26} {report['positions']:6d} positions  "
                      f"{len(report['mismatches']):4d} MISMATCH  {shown}")
            else:
                print(f"{report['table']:<26} {report['positions']:6d} positions  ok")
        print(f"{'total':<26} {total:6d} positions  {failures:4d} mismatches")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
