# 66. ✅ String lacked Unicode 17.0 normalization, full/locale case mapping, and CLDR 48 UCA collation (Phase 60)

> Moved out of `AUDIT.md` — written there as a `## 66. ✅ String lacked Unicode 17.0 normalization, full/locale case mapping, and CLDR 48 UCA collation (Phase 60)` section.
> The text below is the section as it stood, unchanged.


**Problem.** Before Phase 60, `String.prototype` in `src/js/BuiltinsString.cpp` had no Unicode text-processing tables or algorithms:
- `toLowerCase()` and `toUpperCase()` only mapped ASCII `'A'..'Z'`/`'a'..'z'`, ignoring all non-ASCII letters, Unicode `SpecialCasing` expansions (`ß → SS`, `ﬁ → FI`, `İ → i\u0307`), and contextual `Final_Sigma`.
- `toLocaleLowerCase()` and `toLocaleUpperCase()` ignored their locale parameter, performed no BCP-47 validation, and implemented none of the `tr`/`az`, `lt`, or `el` locale rules.
- `normalize()` checked the form argument (`NFC`/`NFD`/`NFKC`/`NFKD`) and returned the input string unchanged.
- `localeCompare()` performed raw UTF-16 code-unit comparison (`<`/`>`), ignoring canonical equivalence, multi-level UCA weights, contractions, and `locales`/`options` arguments.
- `this_string()` held a `const JsValue&` reference to `ctx.current_this()` across `ToString`, risking a dangling reference if user `toString()`/`valueOf()` grew the call stack.

These gaps accounted for all **71 remaining diffs** in `make diff-strings` (`50,509 / 50,580`) and scored **105 / 156** across Test262's nine String Unicode/locale suites.

**Fix.**
- Added `tools/generate_string_unicode_tables.py` to generate compact static Unicode 17.0 / ICU 78.2 / CLDR 48 tables (`include/litebird/js/StringUnicodeTables.hpp`, `src/js/StringUnicodeTables.cpp`, 293 KB `.o`, 0 warnings): CCC runs (403), canonical/compatibility decompositions (2,081 / 3,849 into a 6,763-unit UTF-16 pool), canonical composition pairs (961), QuickCheck & casing-context properties, run-length simple lower/upper tables (185 / 198), `SpecialCasing` expansions (1 lower / 102 upper), and CLDR 48 root collation runs (6,300), multi-CE expansions (3,985), and contractions (974) over a 10,689-entry packed CE pool.
- Implemented the `litebird::unicode` runtime in `src/js/BuiltinsString.cpp`:
  - Full UAX #15 `NFC`/`NFD`/`NFKC`/`NFKD` with Hangul Jamo algorithmic decomposition/composition, QuickCheck fast paths, canonical ordering, and starter-blocked canonical composition (preserving unpaired surrogates).
  - Full Unicode 17.0 case mapping (`Final_Sigma`, unconditional `SpecialCasing`, supplementary planes) plus locale overrides for `tr`/`az`, `lt` (`More_Above`, `After_I` / `Soft_Dotted`), and the exact ICU 78.2 `el` `GreekUpper::toUpper` state machine (including consonant diacritic consumption, diphthong dialytika insertion, and standalone disjunctive `ή`).
  - CLDR 48 / UCA 17.0 root collation (NFD normalization, S2.1 contiguous + discontiguous contractions, all 7 UCA 17.0 implicit weight groups `FB00..FBC0`, ASCII fast path guarding Catalan `L·`/`l·`, plus `sensitivity`, `caseFirst`, `numeric`, `ignorePunctuation`, and `-u-kf`/`-u-kn`).
  - ECMA-402 BCP-47 `CanonicalizeLocaleList` and options validation (`RangeError`/`TypeError`), value-copy fix in `this_string()`, `this_string_value()` unboxing, `not_constructor = true`, `padStart`/`padEnd` `.length === 1`, and `[Symbol.iterator]` metadata.
- Added 5-layer differential & UCD/CLDR verifier (`tools/conformance/verify_string_unicode.py`, wired into `make verify-string-unicode`, CI, and `make asan`), 17 Node-verified JS conformance cases (`tests/js-conformance/string_unicode.dat`), and 3 unit test suites in `tests/test_suite.cpp`.

**Verification.**
- `make diff-strings`: **50,580 / 50,580** equal to Node.js v22.22.3 (ICU 78.2 / Unicode 17.0), **0 differ** (`STRING_DIFF_CEILING` lowered `71 → 0`; verified in both Release and ASan+UBSan).
- `make verify-string-unicode`: **1,114,112 CPs × 11 ops (12,255,232 checks)** + **1,112 curated/fuzz strings × 11 ops** + **1,038 collation pairs** + **25/25 ECMA validation checks** = **0 diffs** (9.89s). With `--ucd-dir /tmp/ucd_cldr`: **400,680** official UCD `NormalizationTest.txt` assertions and **206,297** CLDR 48 `CollationTest_CLDR` pairs = **0 fails** (10.93s).
- **Test262**: 9 String Unicode/locale suites **105 → 152 / 156 (97.4%, +47)** (`normalize` 14/14, `toLowerCase` 27/27, `toUpperCase` 25/25, `toLocaleLowerCase` 25/25, `toLocaleUpperCase` 24/24, `localeCompare` 13/13, `intl402/toLocaleLowerCase` 5/5, `intl402/toLocaleUpperCase` 4/4, `intl402/localeCompare` 6/10); full `built-ins/String` **1,199 / 1,223 (98.0%)**.
- **Merged-tree scoreboards**: `make test` **204 / 204** engine suites + **80 / 80** cross-origin checks; `make js-conformance` **1,020 / 1,034 (98.6%)** (`string_unicode.dat` 17/17; CI floor `1003 → 1020`, `JS_ASAN_FLOOR` `1001 → 1018`); `make conformance` **1,792 / 1,792**; `make dom-conformance` **669 / 669**; `make url-conformance` **1,188 / 1,188**; `make wpt` **9,538 / 9,744** (0 unlisted); `make verify-regexp` **588 / 588 + 112 / 112**; `make fuzz`, `make bench`, `make bench-surrogates` all green.
- **ASan + UBSan** (merged tree): `build-asan/test_suite` **204 / 204**, `build-asan/cross_origin_test` **80 / 80**, `verify_string_unicode.py --fuzz 400 --skip-node --skip-full-sweep` **PASS** (0.83s), `diff_strings.py --max-differ 0` **50,580 / 50,580 (0 diffs)**, `run_js_conformance.py --min-pass 1018` (0 crashes), **0 sanitizer findings**.

**Still open.**
- `Intl` namespace / `Intl.Collator` (the 4 remaining failures in Test262 `intl402/String/prototype/localeCompare` test `Intl.Collator` directly), along with `Intl.DateTimeFormat`, `Intl.NumberFormat`, and `Intl.Segmenter`.
- Language-specific CLDR collation tailorings beyond the CLDR 48 root collation order and `-u-kf`/`-u-kn`/ECMA-402 options (e.g., Swedish `å`/`ä`/`ö` after `z`, German phonebook, Spanish traditional, Japanese kana quaternary order).

---
