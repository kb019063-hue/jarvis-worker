# The Unicode Bidirectional Algorithm's conformance suites

Two files, gzipped here byte for byte as the Unicode Consortium publishes them,
because they are 15 MB uncompressed and 1.7 MB like this:

| File | Cases | What it tests |
| :--- | ---: | :--- |
| `BidiTest.txt.gz` | 770,241 | Sequences of `Bidi_Class` values, each line run under every paragraph direction its bitset allows. Assumes no paired brackets are present. |
| `BidiCharacterTest.txt.gz` | 91,707 | Real code points, and therefore the class table, bracket pairs (BD16/N0) and mirroring. |

Both are `BidiTest-17.0.0.txt` / `BidiCharacterTest-17.0.0.txt`, from
`unicode-org/icu` at tag `release-78.2`, path
`icu4c/source/test/testdata/`:

    https://github.com/unicode-org/icu/blob/release-78.2/icu4c/source/test/testdata/BidiTest.txt
    https://github.com/unicode-org/icu/blob/release-78.2/icu4c/source/test/testdata/BidiCharacterTest.txt

The same files are published in the Unicode Character Database as
`unicodetools/data/ucd/17.0.0/` in `unicode-org/unicodetools`. The condition of
use is Unicode's: <https://www.unicode.org/terms_of_use.html>.

Checksums of the files in this directory (gzip of the originals):

    cbc1b3234d027dbcdd1383d1777d239e4b6e84ff91a0c5b665a95f00d1539e39  BidiCharacterTest.txt.gz
    8c6423f74aab86045ec1b4283654f235b18ae548f91975854c41524383453345  BidiTest.txt.gz

`make bidi-conformance` ungzips them into `build/bidi/` and runs
`build/bidi_check` over both. The checker is
`tools/conformance/bidi_check.cpp`; it needs `src/text/Bidi.cpp` and the tables
`tools/gen_bidi_tables.py` generates, not the engine. A case fails when a
resolved level differs, when a character the suite marks `x` (removed by X9)
is not the one this implementation removes, or when the visual order differs.

To look at one case:

    make build/bidi_check
    gzip -dc tests/bidi/BidiCharacterTest.txt.gz > /tmp/bidi.txt
    ./build/bidi_check /tmp/bidi.txt --filter "0028 05D0" --show 3
