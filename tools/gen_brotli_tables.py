#!/usr/bin/env python3
"""Generates src/net/BrotliTables.cpp from RFC 7932 static tables via libbrotlicommon.

Usage:
    python3 tools/gen_brotli_tables.py [--lib /path/to/libbrotlicommon.so.1] > src/net/BrotliTables.cpp
"""

import argparse
import ctypes
import hashlib
import sys

EXPECTED_DICT_SHA256 = "20e42eb1b511c21806d4d227d07e5dd06877d8ce7b3a817f378f313653f35c70"


class BrotliDictionary(ctypes.Structure):
    _fields_ = [
        ("size_bits_by_length", ctypes.c_uint8 * 32),
        ("offsets_by_length", ctypes.c_uint32 * 32),
        ("data_size", ctypes.c_size_t),
        ("data", ctypes.POINTER(ctypes.c_uint8)),
    ]


class BrotliTransforms(ctypes.Structure):
    _fields_ = [
        ("prefix_suffix_size", ctypes.c_uint16),
        ("prefix_suffix", ctypes.POINTER(ctypes.c_uint8)),
        ("prefix_suffix_map", ctypes.POINTER(ctypes.c_uint16)),
        ("num_transforms", ctypes.c_uint32),
        ("transforms", ctypes.POINTER(ctypes.c_uint8)),
        ("params", ctypes.POINTER(ctypes.c_uint8)),
        ("cut_off_transforms", ctypes.c_int16 * 10),
    ]


def c_escape_bytes(raw: bytes) -> str:
    out = []
    for b in raw:
        if b == 10:
            out.append("\\n")
        elif b == 9:
            out.append("\\t")
        elif b == 34:
            out.append('\\"')
        elif b == 92:
            out.append("\\\\")
        elif 32 <= b < 127:
            out.append(chr(b))
        else:
            out.append("\\x%02x" % b)
    return "".join(out)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lib", default="libbrotlicommon.so.1")
    args = parser.parse_args()

    lib = ctypes.CDLL(args.lib)
    lib.BrotliGetDictionary.restype = ctypes.POINTER(BrotliDictionary)
    lib.BrotliGetTransforms.restype = ctypes.POINTER(BrotliTransforms)

    d = lib.BrotliGetDictionary().contents
    if d.data_size != 122784:
        sys.exit(f"Unexpected dictionary size: {d.data_size}")
    dict_bytes = bytes(d.data[:d.data_size])
    digest = hashlib.sha256(dict_bytes).hexdigest()
    if digest != EXPECTED_DICT_SHA256:
        sys.exit(f"Dictionary SHA-256 mismatch: {digest}")

    t = lib.BrotliGetTransforms().contents
    if t.num_transforms != 121:
        sys.exit(f"Unexpected transform count: {t.num_transforms}")

    ps_blob = bytes(t.prefix_suffix[:t.prefix_suffix_size])
    ps_strings = []
    for i in range(50):
        off = t.prefix_suffix_map[i]
        slen = ps_blob[off]
        ps_strings.append(ps_blob[off + 1 : off + 1 + slen])

    ctx_lut = bytes((ctypes.c_uint8 * 2048).in_dll(lib, "_kBrotliContextLookupTable"))

    print("// GENERATED FILE - do not edit by hand.")
    print("// Regenerate with: python3 tools/gen_brotli_tables.py > src/net/BrotliTables.cpp")
    print("// Source: IETF RFC 7932 Appendix A static dictionary, Section 8 transforms,")
    print("//         and Section 7.1 context lookup tables (MIT-licensed Google brotli).")
    print(f"// Dictionary SHA-256: {digest}")
    print("// clang-format off")
    print('#include "litebird/net/Brotli.hpp"')
    print()
    print("namespace litebird::detail {")
    print()

    print("const uint8_t kBrotliDictSizeBitsByLength[kBrotliMaxWordLength + 1] = {")
    print("    " + ", ".join(str(d.size_bits_by_length[i]) for i in range(25)))
    print("};")
    print()

    print("const uint32_t kBrotliDictOffsetsByLength[kBrotliMaxWordLength + 1] = {")
    print("    " + ", ".join(str(d.offsets_by_length[i]) for i in range(25)))
    print("};")
    print()

    print("const uint8_t kBrotliDictionaryData[kBrotliDictionarySize] = {")
    for off in range(0, len(dict_bytes), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in dict_bytes[off : off + 16])
        comma = "," if off + 16 < len(dict_bytes) else ""
        print(f"    {chunk}{comma}")
    print("};")
    print()

    print("const char* const kBrotliPrefixSuffix[kBrotliNumPrefixSuffix] = {")
    for i, s in enumerate(ps_strings):
        comma = "," if i + 1 < len(ps_strings) else ""
        print(f'    "{c_escape_bytes(s)}"{comma}')
    print("};")
    print()

    print("const uint8_t kBrotliPrefixSuffixLen[kBrotliNumPrefixSuffix] = {")
    print("    " + ", ".join(str(len(s)) for s in ps_strings))
    print("};")
    print()

    print("const BrotliTransform kBrotliTransforms[kBrotliNumTransforms] = {")
    for i in range(121):
        p = t.transforms[3 * i]
        op = t.transforms[3 * i + 1]
        s = t.transforms[3 * i + 2]
        comma = "," if i + 1 < 121 else ""
        print(f"    {{{p}, {op}, {s}}}{comma}")
    print("};")
    print()

    print("const uint8_t kBrotliContextLookup[2048] = {")
    for off in range(0, 2048, 16):
        chunk = ", ".join(f"0x{b:02x}" for b in ctx_lut[off : off + 16])
        comma = "," if off + 16 < 2048 else ""
        print(f"    {chunk}{comma}")
    print("};")
    print()
    print("} // namespace litebird::detail")


if __name__ == "__main__":
    main()
