# 61. ✅ RegExp lacked Unicode 17.0 property escapes, `v`/`d` flags, ES2025 modifiers / duplicate named groups / `RegExp.escape()`, parse-time literal validation, and `Symbol.*` dispatch (Phase 55)

> Moved out of `AUDIT.md` — written there as a `## 61. ✅ RegExp lacked Unicode 17.0 property escapes, `v`/`d` flags, ES2025 modifiers / duplicate named groups / `RegExp.escape()`, parse-time literal validation, and `Symbol.*` dispatch (Phase 55)` section.
> The text below is the section as it stood, unchanged.


**Problem.** Before Phase 55, Litebird's RegExp engine (`src/js/BuiltinsRegExp.cpp`) only recognized six flags (`g`, `i`, `m`, `s`, `u`, `y`), had no Unicode property-escape tables (`\p{L}` approximated a small handful of BMP ranges and rejected scripts, binary properties, and Properties of Strings), folded case only for ASCII letters, did not support ES2024 `v` (`unicodeSets`), ES2022 `d` (`hasIndices`), ES2025 `(?ims-ims:...)` modifiers, ES2025 duplicate named capturing groups, or ES2025 `RegExp.escape()`, did not validate regex literals at parse time in `JsParser.cpp`, and lacked well-known `Symbol.{match, matchAll, replace, search, split, species}` methods and `String.prototype` symbol delegation. As a result, Test262 scored only **739 / 1,879 (39.3%)** on `built-ins/RegExp` and **68 / 240 (28.3%)** on `language/literals/regexp`, and `make diff-strings` carried 24 `\p{L}` RegExp mismatches (`95` total diffs).

**Fix.**
- Generated compact deduplicated Unicode 17.0 RegExp tables (`include/litebird/js/RegExpUnicodeTables.hpp`, `src/js/RegExpUnicodeTables.cpp`, via `tools/generate_regexp_unicode_tables.py`): 22,207 shared ranges covering 80 `General_Category`, 98 Binary, 344 `Script`, and 344 `Script_Extensions` aliases, all 7 Properties of Strings (2,760 multi-CP sequences, 11,196 flattened code points), 1,482 Unicode 17.0 simple case-folding classes, and 1,144 legacy non-Unicode `Canonicalize` classes.
- Upgraded `src/js/BuiltinsRegExp.cpp`:
  - Normalized O(log R) `CharClass` with compile-time case closure (`close_case`), Unicode `\w`/`\W`/`\b`/`\B` folding on `ſ`/`K`, strict `u`-mode escapes/ranges, and exact `iu` vs `iv` complement ordering.
  - Full ES2024 `v` (`unicodeSets`) parser & matcher: `ClassSet` (`--`, `&&` with `[lookahead != &]`, nested `[...]`, `\q{...}`, Properties of Strings, static `MayContainStrings` negation checks, reserved double punctuators, `u`+`v` mutual exclusion) compiled to a single indexed `RegExpOp::MatchStringSet` instruction supporting longest-first forward and lookbehind backtracking via `m_ends_pool`.
  - Full ES2022 `d` (`hasIndices`): `match.indices` and `match.indices.groups` created with `CreateDataProperty` (`define_property`) sharing identical `[start, end]` pair arrays.
  - Full ES2025 duplicate named capturing groups across disjunction branches, multi-group `\k<name>`, and Unicode `RegExpIdentifierName` (`JsIdentifierTables.hpp` + `\uXXXX`/`\u{...}` escapes).
  - Full ES2025 inline modifiers `(?ims-ims:...)` via 3-bit `mode_flags` on `Node` and `RegExpInsn`.
  - Full ES2025 `RegExp.escape(S)`, `RegExp[Symbol.species]`, `Symbol.{match, matchAll, replace, search, split, species}`, non-constructor built-in methods (`not_constructor = true`), generic `RegExp.prototype[Symbol.*]` methods, and spec-accurate flag/source getters (`get ...` names, canonical `dgimsuvy` order).
- Added `js_regexp_validate(pattern, flags, err)` (`include/litebird/js/JsBuiltins.hpp`) and wired parse-time regex literal validation into `src/js/JsParser.cpp`.
- Updated `src/js/BuiltinsString.cpp` (`split`, `replace`, `replaceAll`, `match`, `matchAll`, `search`) to delegate through `GetMethod` on object arguments (`Symbol.*`), enforce `flags` checks on `matchAll`/`replaceAll`, and mark methods non-constructible.

**Verification.**
- **Test262** (`2e58f87d8350`, Unicode 17.0):
  - `built-ins/RegExp`: **739 → 1,849 / 1,879 (98.4%, +1,110 tests / +59.1%)** (`fail 26, skip 4`; 100% of runnable RegExp feature tests pass; `RGI_Emoji.js` also verified directly via `--run-script`).
  - `language/literals/regexp`: **68 → 237 / 240 (98.8%, +169 tests / +70.5%, 0 fail, 3 skip)**.
  - `built-ins/String/prototype/{match,matchAll,replace,replaceAll,search,split}`: **333 / 339 (98.2%, 6 fail — all BigInt literals)**.
- **Differential & spec verification (`make verify-regexp`, `tools/conformance/verify_regexp.py`)**: **588 / 588** matched vs Node.js v22.22.3 (`88` curated + `500` seeded fuzz; also verified at `2,088 / 2,088` with `--fuzz-count 2000`, `0` discrepancies) + **112 / 112** ES2025 spec checks (modifiers, duplicate named groups, `RegExp.escape()`).
- **Corpora & floors**:
  - `tests/js-conformance/regexp_unicode.dat`: **25 / 25** passing on both Node.js (`1,014 / 1,014`) and Litebird (`999 / 1,014`, everyday `90 / 90`; CI floor raised `974 → 999`, ASan floor `972 → 997`).
  - `make diff-strings`: **50,509 / 50,580** equal to Node.js (`71` differ, down from `95` — all 24 `\p{L}` RegExp diffs eliminated; `STRING_DIFF_CEILING` lowered `95 → 71`).
  - `make test`: **198 / 198** unit suites (added 3 Phase 55 suites).
  - `make dom-conformance`: **669 / 669**; `make wpt`: **7,998 / 8,009** (`0` unlisted failures).
  - **ASan + UBSan**: `build-asan/test_suite` **198 / 198**, `verify_regexp.py` **588 / 588 + 112 / 112**, `js-conformance` **998 / 1,014** (`--min-pass 997`), `0` sanitizer errors.

**Still open.**
- The 26 remaining failures in Test262 `built-ins/RegExp` are outside the RegExp engine: 12 cross-realm `$262.createRealm` tests, 10 `BigInt` literal tests, 2 `JsValue.cpp` null-prototype `__proto__` own-property reads (`groups-object.js`, `indices-groups-object.js`), 1 `BuiltinsArray.cpp` dense-array descriptor check (`indices-array-properties.js`), and 1 `globalThis` `RegExp` own-property descriptor check (`prop-desc.js`). Four Test262 tests are skipped by the harness (3 with embedded NUL bytes, 1 `RGI_Emoji.js` > 128 KB argv limit that passes via `--run-script`).

---
