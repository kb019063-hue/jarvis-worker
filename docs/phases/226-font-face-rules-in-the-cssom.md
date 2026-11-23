# Phase 226 — font-face rules in the CSSOM

## What was wrong

The CSSOM's sheet parser skipped `@font-face` blocks altogether, although the
separate CSS parser can read them for rendering. Consequently `cssRules` had
the wrong length and order, `insertRule('@font-face …')` threw, and scripts
could not inspect or edit the block as a `CSSFontFaceRule`.

## What changed

The CSSOM keeps font-face blocks in sheet order, including inside `@media`,
and accepts them through `insertRule` and `replaceSync`. A rule has type 5,
`CSSRule` inheritance, a stable `CSSStyleDeclaration`, and a live declaration
block. Descriptor reads, writes, removal, `cssText`, priorities, and indexing
use the same block. Parsing sheet text and assigning `cssText` rejects
unknown descriptors and `!important`; `setProperty` follows the separate
script-write path. Common `src` URL/local and font-family serialization,
font-display and weight checks are handled without treating the block as an
ordinary style rule. Font-face rules never participate in selector matching.
Rule wrapper identity is keyed by sheet and underlying rule rather than a
sheet-local number, so another sheet cannot replace the wrapper used by a
retained style's `parentRule`.

## Evidence

`tools/conformance/verify_css_font_face.py` executes the same labeled probes
in Litebird and pinned headless Chromium. The original 11 checks gave **1/11
before and 11/11 after**; the expanded probe gives **25/25 after**, covering
sheet order, branding and type, identity across sheets, nested parents,
serialization, insertion/deletion, retained rules, mutations, filtering,
priorities and receiver checks. The engine unit suite adds a stylesheet-owned
rule and a cascade control.

```sh
make -j2 all
npm install --prefix build/svg-oracle @sparticuz/chromium@131.0.1 puppeteer-core@23.11.1
mkdir -p build/svg-oracle/runtime
node -e "require('fs').createReadStream(process.argv[1]).pipe(require('zlib').createBrotliDecompress()).pipe(process.stdout)" \
  build/svg-oracle/node_modules/@sparticuz/chromium/bin/al2023.tar.br | tar x -C build/svg-oracle/runtime
python3 tools/conformance/verify_css_font_face.py
make test
```

The verifier keeps the caller's `LD_LIBRARY_PATH` and adds the package's Linux
runtime library directory; some minimal systems also need NSS libraries.

## Still open

This exposes and edits the CSSOM model only. The font loader and renderer use
the separate CSS parser; CSSOM edits do not update loaded fonts or rendering.
Descriptor grammars and CSS serialization are incomplete: advanced escaping,
complex source functions, and full validation/canonicalization of
`unicode-range`, stretch, variation and metric overrides are not implemented.
The general CSSOM rule cache retains wrappers for the realm lifetime; this
change keys them correctly but does not implement weak caching. The existing
CSSOM's detached-rule parent handling and its `@supports`/`@layer` flattening
remain separate gaps. These 25 probes do not establish full CSS Fonts or
CSSOM compliance.
