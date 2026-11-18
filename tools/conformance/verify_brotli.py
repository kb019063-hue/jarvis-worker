#!/usr/bin/env python3
"""Differential and mutation-fuzz verifier for Litebird's RFC 7932 Brotli decoder.

Verifies `brotli_check` against:
  1. Built-in RFC 7932 edge-case vectors (always run, including `--skip-node`).
  2. Live Node.js `zlib.brotliCompressSync` differential corpus across qualities
     0..11, window bits 10..24, all modes, multi-meta-block streams, >1 MiB
     payloads (5/6-nibble MLEN), UTF-8 dictionary transforms, and mixed data.
  3. Seeded mutation fuzzing (bit flips, truncation, bomb output limits,
     invalid headers) verifying crash/UB-free deterministic rejection/decoding.
"""

import argparse
import os
import random
import subprocess
import sys

# Hand-assembled & zlib-verified RFC 7932 vectors for `--skip-node` mode.
STATIC_VALID_VECTORS = [
    ("empty_wbits16", "06", b""),
    ("empty_wbits24", "3b", b""),
    ("hello", "8b108048656c6c6f2c204c6974656269726420485454502f312e31202b2042726f746c692103",
     b"Hello, Litebird HTTP/1.1 + Brotli!"),
    ("dict_words",
     "1b730040ac1278f36655587b639e50aa91962072743554d65461687792a889aa929eaa280b5439605fb24cbc8007abd9c139969a0806c19f7dd7d47d0e8b2e2bbdb77aacad1f411862224031e17031f5d569fd12852400",
     b" the and for with that from this have not are was were TIME DATA CODE SITE CITY OPEN https://example.com/index.html "),
    ("unicode_upper",
     "0b1780c3a4c3b6c3bcc39f20e282acc2a3c2a52074686520616e6420666f722054484520414e4420464f5220c384c396c39c03",
     "äöüß €£¥ the and for THE AND FOR ÄÖÜ".encode("utf-8")),
    ("repeat_72k",
     "5b3f1901401e1c436dd6f0e3c118548708a89ebd072416358f04",
     b"abcdef0123456789" * 4500),
    ("q0_uncompressed",
     "0b71020080aaaaaaeaff74d5c32de21c97bbc3ddc04d1d3cc03773370b0822fabfabfe3df756f78ceaf23686bbe2c5c2c2c2c6c6aa5e6654071f3efc180c06e338935535180c0683c1600e7546a4a2340a87857692bd27f8fe4ecff3ebb17de6becff5ebb2ecc7787f8f6df1fe5ad6f1ff7b5cc7676e7339e63aced7bfe76dff191876f49b186e",
     b"Uncompressed/fast q0 payload with repeated chunks: " + b"xyz" * 400),
    ("lgwin10_wrap",
     "a1f8c70020e090233d602106d6927a90427a790c4ce4c4822cfcee6c847ffc82c59706168b3521006000",
     b"Window size 10 wraparound check " * 200),
]


def bits_to_bytes(bits):
    out = bytearray()
    for i in range(0, len(bits), 8):
        b = 0
        for j in range(8):
            if i + j < len(bits) and bits[i + j]:
                b |= 1 << j
        out.append(b)
    return bytes(out)


def push_bits(bits, val, n):
    for i in range(n):
        bits.append((val >> i) & 1)


def build_crafted_vectors():
    valid = []
    invalid = []

    # 1. Metadata block (0-byte skip) + empty last block
    b = []
    push_bits(b, 0, 1)  # WBITS=16
    push_bits(b, 0, 1)  # ISLAST=0
    push_bits(b, 3, 2)  # MNIBBLES=metadata
    push_bits(b, 0, 1)  # reserved=0
    push_bits(b, 0, 2)  # MSKIPBYTES=0
    while len(b) % 8:
        b.append(0)
    push_bits(b, 1, 1)  # ISLAST=1
    push_bits(b, 1, 1)  # ISLASTEMPTY=1
    valid.append(("meta_zero_skip", bits_to_bytes(b).hex(), b""))

    # 2. Metadata block with 2-byte MSKIPBYTES + uncompressed block + empty last
    b = []
    push_bits(b, 0, 1)  # WBITS=16
    push_bits(b, 0, 1)  # ISLAST=0
    push_bits(b, 3, 2)  # metadata
    push_bits(b, 0, 1)  # reserved=0
    push_bits(b, 1, 2)  # MSKIPBYTES=1 (1 byte -> 4 bytes skipped: raw=3)
    push_bits(b, 3, 8)
    while len(b) % 8:
        b.append(0)
    for byte in b"META":
        push_bits(b, byte, 8)
    # Uncompressed meta-block: ISLAST=0, MNIBBLES=4, MLEN=3 (raw=2), ISUNCOMPRESSED=1
    push_bits(b, 0, 1)
    push_bits(b, 0, 2)
    push_bits(b, 2, 16)
    push_bits(b, 1, 1)
    while len(b) % 8:
        b.append(0)
    for byte in b"XYZ":
        push_bits(b, byte, 8)
    push_bits(b, 1, 1)
    push_bits(b, 1, 1)
    valid.append(("meta_plus_uncompressed", bits_to_bytes(b).hex(), b"XYZ"))

    # 3. 1-symbol code-length complex prefix tree (HSKIP=3, only cl[4]=2)
    b = []
    push_bits(b, 0, 1)
    push_bits(b, 1, 1)
    push_bits(b, 0, 1)
    push_bits(b, 0, 2)
    push_bits(b, 3, 16)  # MLEN=4
    push_bits(b, 0, 1)
    push_bits(b, 0, 1)
    push_bits(b, 0, 1)
    push_bits(b, 0, 2)
    push_bits(b, 0, 4)
    push_bits(b, 0, 2)
    push_bits(b, 0, 1)
    push_bits(b, 0, 1)
    push_bits(b, 3, 2)  # HSKIP=3 -> idx 3 = sym 4
    b.extend([1, 1, 0])  # cl[4] = 2
    for _ in range(4, 18):
        b.extend([0, 0])
    push_bits(b, 1, 2)
    push_bits(b, 0, 2)
    push_bits(b, 144, 10)  # IAC insert=2, copy=2
    push_bits(b, 1, 2)
    push_bits(b, 0, 2)
    push_bits(b, 6, 6)  # dist=2
    # Two literals using code 0000 (0) and 0001 -> reversed MSB 0001 -> bits 0,0,0,1 = 8 -> sym 1
    push_bits(b, 0, 4)
    push_bits(b, 8, 4)
    valid.append(("single_cl_tree", bits_to_bytes(b).hex(), bytes([0, 1, 0, 1])))

    # 4. Invalid streams
    invalid.append(("empty_input", ""))
    invalid.append(("nonzero_trailing_bits", "86"))  # WBITS=16, ISLAST=1, ISLASTEMPTY=1, high bit=1
    invalid.append(("reserved_wbits_0010001", "1100"))  # b0=1, n=0, m=1 (0b001_000_1 = 0x11)
    # Exuberant nibble (mnibbles=5 with top nibble = 0)
    b = []
    push_bits(b, 0, 1)
    push_bits(b, 1, 1)
    push_bits(b, 0, 1)
    push_bits(b, 1, 2)  # MNIBBLES=5
    push_bits(b, 10, 20)  # top nibble 0!
    invalid.append(("exuberant_mlen_nibble", bits_to_bytes(b).hex()))
    # Metadata with reserved=1
    b = []
    push_bits(b, 0, 1)
    push_bits(b, 0, 1)
    push_bits(b, 3, 2)
    push_bits(b, 1, 1)  # reserved=1
    push_bits(b, 0, 2)
    invalid.append(("metadata_reserved_1", bits_to_bytes(b).hex()))
    # Non-zero padding before uncompressed payload
    b = []
    push_bits(b, 0, 1)
    push_bits(b, 0, 1)
    push_bits(b, 0, 2)
    push_bits(b, 0, 16)
    push_bits(b, 1, 1)  # is_uncompressed=1
    push_bits(b, 1, 3)  # non-zero padding bits!
    push_bits(b, 65, 8)
    push_bits(b, 1, 1)
    push_bits(b, 1, 1)
    invalid.append(("nonzero_uncomp_pad", bits_to_bytes(b).hex()))

    return valid, invalid


def generate_node_corpus():
    node_script = r"""
const zlib = require("zlib");
const fs = require("fs");

const cases = [];
function addCase(name, raw, opts = {}) {
  const comp = zlib.brotliCompressSync(raw, opts);
  cases.push({ name, rawHex: raw.toString("hex"), compHex: comp.toString("hex") });
}

addCase("empty", Buffer.alloc(0));
addCase("one_byte", Buffer.from([0x42]));
addCase("all_bytes", Buffer.from(Array.from({length: 256}, (_, i) => i)));
addCase("rle_100k", Buffer.from("Z".repeat(100000)));
addCase("overlap_dist1", Buffer.from("A" + "A".repeat(10000)));
addCase("overlap_dist3", Buffer.from("abc".repeat(5000)));

// >1 MiB payload exercising 6-nibble MLEN and multi-block splits
const big = Buffer.from(("<!doctype html><html><head><title>Litebird</title></head>" +
  "<body><p>The quick brown fox jumps over the lazy dog 1234567890.</p></body></html>\n").repeat(8000));
addCase("big_1mb_q4", big, { params: { [zlib.constants.BROTLI_PARAM_QUALITY]: 4 } });
addCase("big_1mb_q11", big, { params: { [zlib.constants.BROTLI_PARAM_QUALITY]: 11 } });

const codeCorpus = Buffer.from(
  fs.readFileSync("src/net/ResourceLoader.cpp", "utf8") +
  fs.readFileSync("README.md", "utf8")
);
for (let q = 0; q <= 11; q++) {
  addCase(`code_q${q}`, codeCorpus.slice(0, 70000), {
    params: { [zlib.constants.BROTLI_PARAM_QUALITY]: q }
  });
}
for (let w = 10; w <= 24; w++) {
  addCase(`code_lgwin${w}`, codeCorpus.slice(0, 80000), {
    params: { [zlib.constants.BROTLI_PARAM_QUALITY]: 6, [zlib.constants.BROTLI_PARAM_LGWIN]: w }
  });
}
for (const m of [0, 1, 2]) {
  addCase(`mode_${m}`, codeCorpus.slice(0, 50000), {
    params: { [zlib.constants.BROTLI_PARAM_QUALITY]: 11, [zlib.constants.BROTLI_PARAM_MODE]: m }
  });
}

// Seeded pseudo-random + compressible hybrid blocks
let rng = 0x13572468;
function nextByte() { rng = (rng * 1664525 + 1013904223) >>> 0; return (rng >>> 24) & 0xff; }
const rand1 = Buffer.alloc(140000);
for (let i = 0; i < rand1.length; i++) rand1[i] = nextByte();
addCase("random_140k_q2", rand1, { params: { [zlib.constants.BROTLI_PARAM_QUALITY]: 2 } });
addCase("random_140k_q11", rand1, { params: { [zlib.constants.BROTLI_PARAM_QUALITY]: 11 } });
addCase("hybrid", Buffer.concat([
  rand1.slice(0, 40000),
  Buffer.from("function render(node) { return node.tagName + ':' + node.id; }\n".repeat(700)),
  rand1.slice(40000, 90000),
  Buffer.from(" the and for with that from this have not are was were ".repeat(900))
]), { params: { [zlib.constants.BROTLI_PARAM_QUALITY]: 9 } });

// Multi-flush streaming chunks
const st = zlib.createBrotliCompress({ params: { [zlib.constants.BROTLI_PARAM_QUALITY]: 7 } });
const chunks = [];
const parts = [
  Buffer.from("Chunk 1 HTML ".repeat(80)),
  rand1.slice(0, 4000),
  Buffer.from("Chunk 3 CSS margin: 0 auto; padding: 12px; ".repeat(120)),
  Buffer.from("Final trailing chunk!")
];
st.on("data", d => chunks.push(d));
(function feed(idx) {
  if (idx >= parts.length) { st.end(); return; }
  st.write(parts[idx]);
  st.flush(zlib.constants.BROTLI_OPERATION_FLUSH, () => feed(idx + 1));
})(0);
st.on("end", () => {
  cases.push({
    name: "multi_flush",
    rawHex: Buffer.concat(parts).toString("hex"),
    compHex: Buffer.concat(chunks).toString("hex")
  });
  for (const c of cases) {
    process.stdout.write(`${c.name}\t${c.rawHex}\t${c.compHex}\n`);
  }
});
"""
    res = subprocess.run(["node", "-e", node_script], capture_output=True, text=True, check=True)
    out = []
    for line in res.stdout.splitlines():
        if not line:
            continue
        name, raw_hex, comp_hex = line.split("\t")
        out.append((name, comp_hex, bytes.fromhex(raw_hex)))
    return out


def run_batch(binary, queries):
    stdin_data = "".join(f"{max_out}\t{comp_hex}\n" for max_out, comp_hex in queries)
    res = subprocess.run([binary, "--batch"], input=stdin_data, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"{binary} exited with {res.returncode}: {res.stderr}")
    lines = res.stdout.splitlines()
    if len(lines) != len(queries):
        raise RuntimeError(f"Expected {len(lines)} lines, got {len(queries)} responses")
    return lines


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="build/brotli_check")
    parser.add_argument("--fuzz", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=20261115)
    parser.add_argument("--skip-node", action="store_true")
    args = parser.parse_args()

    if not os.path.exists(args.binary):
        sys.exit(f"Binary not found: {args.binary}")

    valid_cases = list(STATIC_VALID_VECTORS)
    crafted_valid, invalid_cases = build_crafted_vectors()
    valid_cases.extend(crafted_valid)

    node_count = 0
    if not args.skip_node:
        node_cases = generate_node_corpus()
        node_count = len(node_cases)
        valid_cases.extend(node_cases)

    # 1. Valid stream checks
    queries = [(64 * 1024 * 1024, comp_hex) for _, comp_hex, _ in valid_cases]
    results = run_batch(args.binary, queries)
    for (name, _, expected_raw), line in zip(valid_cases, results):
        parts = line.split("\t", 2)
        if parts[0] != "OK":
            sys.exit(f"FAIL valid case {name}: got {line}")
        got = bytes.fromhex(parts[2]) if len(parts) > 2 and parts[2] else b""
        if got != expected_raw:
            sys.exit(f"FAIL mismatch in {name}: got {len(got)}B != expected {len(expected_raw)}B")

    # 2. Output bomb limit checks (max_output_bytes < actual size -> ERR)
    bomb_queries = []
    bomb_names = []
    for name, comp_hex, expected_raw in valid_cases:
        if len(expected_raw) > 0:
            bomb_queries.append((len(expected_raw) - 1, comp_hex))
            bomb_names.append(name)
            bomb_queries.append((len(expected_raw), comp_hex))
            bomb_names.append(name + "@exact")
    bomb_results = run_batch(args.binary, bomb_queries)
    for i, line in enumerate(bomb_results):
        if i % 2 == 0 and line != "ERR":
            sys.exit(f"FAIL bomb cap {bomb_names[i]}: expected ERR, got {line}")
        if i % 2 == 1 and not line.startswith("OK\t"):
            sys.exit(f"FAIL exact cap {bomb_names[i]}: expected OK, got {line}")

    # 3. Invalid stream checks
    inv_results = run_batch(args.binary, [(64 * 1024 * 1024, hx) for _, hx in invalid_cases])
    for (name, _), line in zip(invalid_cases, inv_results):
        if line != "ERR":
            sys.exit(f"FAIL invalid case {name}: expected ERR, got {line}")

    # 4. Seeded mutation & truncation fuzzing
    rng = random.Random(args.seed)
    seed_blobs = [bytes.fromhex(c[1]) for c in valid_cases if len(c[1]) > 0]
    fuzz_queries = []
    for i in range(args.fuzz):
        base = bytearray(rng.choice(seed_blobs))
        mode = i % 5
        if mode == 0 and len(base) > 1:
            # Truncate
            cut = rng.randint(1, len(base) - 1)
            mut = bytes(base[:cut])
        elif mode == 1:
            # Bit flip
            idx = rng.randrange(len(base))
            base[idx] ^= 1 << rng.randrange(8)
            mut = bytes(base)
        elif mode == 2:
            # Multi-byte splice/corrupt
            for _ in range(rng.randint(1, 4)):
                base[rng.randrange(len(base))] = rng.randrange(256)
            mut = bytes(base)
        elif mode == 3:
            # Completely random bytes
            mut = bytes(rng.randrange(256) for _ in range(rng.randint(1, 256)))
        else:
            # Corrupt header with tiny max_output_bytes cap
            for k in range(min(4, len(base))):
                base[k] = rng.randrange(256)
            mut = bytes(base)
        cap = 16384 if mode == 4 else 4 * 1024 * 1024
        fuzz_queries.append((cap, mut.hex()))

    fuzz_results = run_batch(args.binary, fuzz_queries)
    ok_fuzz = sum(1 for line in fuzz_results if line.startswith("OK\t"))
    err_fuzz = sum(1 for line in fuzz_results if line == "ERR")
    if ok_fuzz + err_fuzz != len(fuzz_queries):
        sys.exit(f"FAIL fuzz unexpected output line count/format")

    print(
        f"verify_brotli: {len(valid_cases)} valid ({node_count} Node-compressed), "
        f"{len(bomb_queries) // 2} bomb-cap, {len(invalid_cases)} invalid, "
        f"and {len(fuzz_queries)} mutation fuzz inputs passed cleanly."
    )


if __name__ == "__main__":
    main()
