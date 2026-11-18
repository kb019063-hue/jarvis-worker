# Phase 60 Architecture: Unicode 17.0 String Normalization, Full & Locale Case Mapping, and CLDR 48 UCA Collation (Finding 66)

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 60 Architecture: Unicode 17.0 String Normalization, Full & Locale Case Mapping, and CLDR 48 UCA Collation (Finding 66)` section.
> The text below is the section as it stood, unchanged.


### 1. What was wrong before Phase 60

Before Phase 60, Litebird's `String.prototype` Unicode methods (`src/js/BuiltinsString.cpp`) were placeholders or ASCII-only:

- `toLowerCase()` and `toUpperCase()` transformed only ASCII `'A'..'Z'` / `'a'..'z'`, leaving every non-ASCII code point (Greek, Cyrillic, Latin extended, supplementary Deseret/Adlam/Warang Citi/Garay/Beria Erfe), multi-code-point `SpecialCasing` expansion (`ß → SS`, `ﬁ → FI`, `İ → i\u0307`, `ΐ`), and contextual rule (`Final_Sigma`) untouched.
- `toLocaleLowerCase()` and `toLocaleUpperCase()` ignored their locale argument completely and delegated straight to the ASCII functions, with no BCP-47 locale validation and no `tr`/`az`, `lt`, or `el` rules.
- `normalize()` validated the form string (`NFC`/`NFD`/`NFKC`/`NFKD`) and then returned the input verbatim without decomposing, canonically reordering, or composing.
- `localeCompare()` compared raw UTF-16 code units (`<`/`>`), returning arbitrary code-point order (`'Z' < 'a'`, `'ä' > 'z'`, ignoring canonical equivalence and UCA multi-level weights) and ignoring its `locales` and `options` arguments.
- `this_string()` returned a `JsString` constructed from `ctx.current_this()` by `const JsValue&` reference — a dangling reference if `RequireObjectCoercible` / `ToString` triggered `valueOf()` / `toString()` re-entrancy that grew the call-context stack.
- Several `String.prototype` built-ins lacked `not_constructor = true`, `padStart`/`padEnd` reported `.length === 2` instead of `1`, `[Symbol.iterator]` lacked standard `.name`/`.length`, and boxed `new String(...)` receivers were not unboxed via `this_string_value` before `ToString`.

Together, these accounted for all **71 remaining discrepancies** in `make diff-strings` (`50,509 / 50,580`) and left Test262's nine String Unicode/locale directories at **105 / 156 (67.3%)**.

### 2. Static Unicode 17.0 / ICU 78.2 / CLDR 48 data tables

Following the zero-runtime-dependency table pattern of `IdnaTables.{hpp,cpp}` and `RegExpUnicodeTables.{hpp,cpp}`, `tools/generate_string_unicode_tables.py` downloads official Unicode 17.0 UCD files (`UnicodeData.txt`, `SpecialCasing.txt`, `DerivedNormalizationProps.txt`, `DerivedCoreProperties.txt`, `NormalizationTest.txt`) and CLDR 48 (`allkeys_CLDR.txt`, `CollationTest_CLDR_NON_IGNORABLE_SHORT.txt`), verifies all 1,114,112 code points across 6 operations against Node.js's ICU 78.2 during generation, and emits `src/js/StringUnicodeTables.cpp` (895 KB source, **293 KB** `.o`, 0 warnings) against `include/litebird/js/StringUnicodeTables.hpp`:

- **Normalization**: `kCccRuns` (403 runs), `kCanonDecomp` (2,081 entries), `kCompatDecomp` (3,849 entries), shared `kDecompPool` (6,763 UTF-16 code units), `kComposePairs` (961 canonical composition pairs, excluding `Full_Composition_Exclusion`), and packed property runs (`kNfcQcRuns`, `kNfkcQcRuns`, `kNfdQcNoRuns`, `kNfkdQcNoRuns`, `kCasedRuns`, `kCaseIgnorableRuns`, `kSoftDottedRuns`).
- **Case mapping**: delta-compressed run-length tables `kSimpleLowerRuns` (185 runs) and `kSimpleUpperRuns` (198 runs), plus full `SpecialCasing` tables `kSpecialLower` (1 unconditional entry: `U+0130`) and `kSpecialUpper` (102 multi-code-point entries) indexed into a shared UTF-16 pool.
- **Collation (CLDR 48 / UCA 17.0 root)**: `kCollationRuns` (6,300 delta-compressed single-CE runs, flagging variable elements for `ignorePunctuation`), `kCollationMulti` (3,985 multi-CE expansion entries), `kCollationContractions` (974 sorted 2–3 code-point contraction entries with ASCII starter check for `L·`/`l·`), and `kCollationCePool` (10,689 packed 64-bit CEs storing primary/secondary/tertiary weights and variable bit).

### 3. Runtime architecture (`litebird::unicode` in `src/js/BuiltinsString.cpp`)

1. **UAX #15 Normalization (`normalize_utf16`)**:
   - Fast-path QuickCheck scans the UTF-16 input and returns immediately when all code points are `YES` and in non-decreasing CCC order (including pure ASCII).
   - Algorithmic Hangul Jamo decomposition (`SBase=0xAC00`, `LBase=0x1100`, `VBase=0x1161`, `TBase=0x11A7`) and composition (`L+V → LV`, `LV+T → LVT`) without storing 11,172 table rows.
   - Recursive canonical (`NFD`/`NFC`) and compatibility (`NFKD`/`NFKC`) decomposition with unpaired surrogates preserved verbatim.
   - Canonical Ordering Algorithm (stable insertion sort over non-zero CCC runs).
   - Canonical Composition Algorithm (`NFC`/`NFKC`) tracking the last starter and blocking recombinations whenever an intervening combining mark has `last_ccc >= ccc`.

2. **Full & Locale-Sensitive Case Mapping (`to_lower_utf16`, `to_upper_utf16`)**:
   - **Root Unicode 17.0**: applies `SpecialCasing` unconditional expansions (`ß → SS`, `İ → i\u0307`, ligatures, iota-subscript forms), `Final_Sigma` context (`Cased` before, skipping `Case_Ignorable`, and not followed by `Cased`), and simple run-length mappings across all 17 Unicode planes (decoding surrogate pairs cleanly and preserving lone surrogates).
   - **Turkish / Azeri (`tr`, `az`)**: `toLocaleLowerCase` maps `U+0130` (`İ`) → `i`, `I` + combining dot above (`U+0307`, skipping intervening `ccc != 0 && ccc != 230` marks) → `i` (consuming the dot), and bare `I` → `ı` (`U+0131`); `toLocaleUpperCase` maps `i` → `İ` (`U+0130`).
   - **Lithuanian (`lt`)**: `toLocaleLowerCase` inserts `U+0307` after `I`/`J`/`Į` when `More_Above` (`ccc == 230`) follows, and maps `Ì`/`Í`/`Ĩ` to `i\u0307` + accent; `toLocaleUpperCase` strips `U+0307` when preceded by a `Soft_Dotted` base (`After_I` / `Soft_Dotted` skipping non-`230` marks).
   - **Greek (`el`)**: `toLocaleUpperCase` implements the exact ICU 78.2 `GreekUpper::toUpper` state machine (`AFTER_CASED`, `AFTER_VOWEL_WITH_PRECOMPOSED_ACCENT`, `AFTER_VOWEL_WITH_COMBINING_ACCENT`), consuming following Greek combining marks after *all* Greek letters (vowels and consonants), inserting dialytika on accented diphthong second vowels (`άι → ΑΪ`, `όυ → ΟΫ`, without adding dialytika after already-dialytika vowels like `ΰυ`), preserving the disjunctive tonos on standalone `ή`, and folding `ypogegrammeni` (`U+0345`) to `Ι`.

3. **CLDR 48 / UCA 17.0 Collation (`locale_compare_utf16`)**:
   - NFD-normalizes both operands (with an ASCII fast path that bypasses normalization unless `0x00B7` follows `L`/`l` for Catalan `L·`/`l·`).
   - Walks code points using UCA S2.1 longest-match contiguous and discontiguous contraction matching (blocking across marks with `last_ccc >= ccc` or `ccc == 0`), collecting packed `(primary, secondary, tertiary, variable)` CEs.
   - Unmapped code points emit the 7 UCA 17.0 / CLDR 48 implicit two-CE groups: `FB00` (Tangut), `FB01` (Tangut Components), `FB02` (Nushu), `FB03` (Khitan Small Script), `FB40+(cp>>15)` (Core Han + 12 CJK compatibility ideographs), `FB80+(cp>>15)` (Han Extensions A–J), and `FBC0+(cp>>15)` (unassigned / surrogates).
   - Supports ECMA-402 collation options and Unicode `-u-` subtags (`-u-kn`, `-u-kn-true`/`false`, `-u-kf-upper`/`lower`/`false`):
     - `ignorePunctuation`: skips variable CEs (and any immediately following secondary-only combining marks).
     - `numeric`: compares maximal runs of ASCII digits (`0x21E6..0x21EF`) by leading-zero-stripped length then digit value, keeping raw CE tie-breaking at lower levels.
     - `caseFirst` (`upper` / `lower`): applies case-level weighting on uppercase tertiary weights (`0x08..0x12`) ahead of tertiary comparison.
     - `sensitivity` (`base` / `accent` / `case` / `variant`): selects primary, primary+secondary, primary+case, or primary+secondary+tertiary passes.

4. **ECMA-402 Validation & VM Hardening**:
   - `CanonicalizeLocaleList` validates every BCP-47 language tag (rejecting malformed subtags, duplicate singletons/variants, invalid grandfathered/private-use shapes, non-string/non-object entries) and extracts canonical primary language plus `-u-kf`/`-u-kn` extensions, throwing `RangeError` / `TypeError` per ECMA-402.
   - Options object parsing validates `usage`, `localeMatcher`, `sensitivity`, `caseFirst`, and reads `numeric` / `ignorePunctuation` via `JsValue::is_truthy()`.
   - `this_string()` copies `ctx.current_this()` by value before invoking `ToString`, eliminating re-entrancy dangling references; `this_string_value()` unboxes `String` wrappers; `String.prototype` methods are marked `not_constructor = true`, `padStart`/`padEnd` have `.length === 1`, and `[Symbol.iterator]` exposes proper metadata.

### 4. Verification (merged tree)

- `make diff-strings`: **50,580 / 50,580** equal to Node.js v22.22.3 (ICU 78.2 / Unicode 17.0), **0 differ** (`STRING_DIFF_CEILING ?= 0`, lowered from `71`).
- `make verify-string-unicode` (`tools/conformance/verify_string_unicode.py`):
  - Exhaustive all-code-point sweep: **1,114,112 CPs × 11 operations (12,255,232 checks)** vs Node.js = **0 diffs**.
  - Curated + seeded fuzz: **1,112 strings × 11 operations** + **1,038 collation pairs** vs Node.js = **0 diffs**.
  - ECMA-262 / ECMA-402 validation: **25 / 25** passed.
  - With `--ucd-dir /tmp/ucd_cldr`: **400,680** official UCD `NormalizationTest.txt` assertions (5 invariants × 4 forms × 20,034 lines) = **0 fails**, and **206,297** official CLDR 48 `CollationTest_CLDR_NON_IGNORABLE_SHORT.txt` adjacent ordering pairs = **0 fails** (10.93s total).
- Test262 (`tools/conformance/run_test262.py`, Unicode 17.0):
  - Nine String Unicode/locale suites (`built-ins/String/prototype/{normalize,toLowerCase,toUpperCase,toLocaleLowerCase,toLocaleUpperCase,localeCompare}` + `intl402/String/prototype/{toLocaleLowerCase,toLocaleUpperCase,localeCompare}`): **105 → 152 / 156 (97.4%, +47)**.
  - Full `built-ins/String`: **1,199 / 1,223 (98.0%)** (24 remaining failures are BigInt / `$262` / `with` / prototype-meta outside String Unicode).
- `make js-conformance`: **1,020 / 1,034 (98.6%)** (`string_unicode.dat` **17 / 17**, verified 1,034/1,034 against `node -e`; CI floor raised `1003 → 1020`, `JS_ASAN_FLOOR` raised `1001 → 1018`).
- `make test`: **204 / 204** engine unit suites (3 new Phase 60 suites) + **80 / 80** cross-origin checks.
- Regression & sanitizer checks on the merged tree: `make conformance` **1,792 / 1,792**, `make dom-conformance` **669 / 669**, `make url-conformance` **1,188 / 1,188**, `make wpt` **9,538 / 9,744** (0 unlisted), `make verify-regexp` **588 / 588 + 112 / 112**, `make fuzz` / `make bench` / `make bench-surrogates` all green; **ASan + UBSan** (`build-asan/test_suite` 204/204, `build-asan/cross_origin_test` 80/80, `verify_string_unicode.py --fuzz 400 --skip-node --skip-full-sweep`, `diff_strings.py --max-differ 0` 50,580/50,580, `run_js_conformance.py --min-pass 1018` with 0 crashes): **0 sanitizer findings**.

### 5. Still open

- **No `Intl` namespace / `Intl.Collator` object**: the 4 remaining failures in `intl402/String/prototype/localeCompare` construct or spy on `Intl.Collator` directly. `Intl.DateTimeFormat`, `Intl.NumberFormat`, `Intl.Segmenter`, and `Intl.Collator` remain Wave 2 Row 10 work.
- **Locale-specific CLDR collation tailorings**: `localeCompare()` implements the full CLDR 48 root collation order (DUCET + CLDR root contractions + `-u-kf`/`-u-kn` + all ECMA-402 options), which matches `en`/`und` and default ICU root collation, but language-specific tailorings (e.g. Swedish `å`/`ä`/`ö` after `z`, German phonebook `ä ≈ ae`, Spanish traditional `ch`, Japanese Hiragana/Katakana quaternary weights) are not yet tabulated.
- **`usage: "search"`**: accepted by options validation and mapped to root collation; locale-specific search tailorings depend on CLDR tailoring tables.

---
