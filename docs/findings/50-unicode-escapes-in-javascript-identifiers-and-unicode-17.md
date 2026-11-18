# 50. ✅ Unicode escapes in JavaScript identifiers and Unicode 17.0 identifier validation (Phase 44)

> Moved out of `AUDIT.md` — written there as a `## 50. ✅ Unicode escapes in JavaScript identifiers and Unicode 17.0 identifier validation (Phase 44)` section.
> The text below is the section as it stood, unchanged.


**What was wrong.** `JsLexer` had no handling for `\uXXXX` or `\u{HexDigits}`
escapes inside identifiers or `#PrivateName` tokens (`var \u0061 = 1`,
`o.\u2118 = 3`, `{ \u{6F}: 2 }`, `class C { #\u{1D49C} = 1 }` all threw
`SyntaxError: Invalid or unexpected token`), and `is_ident_start()` accepted
every raw UTF-8 byte `>= 0x80` outside four hard-coded whitespace/line-terminator
sequences (accepting non-ID code points like `U+2E2F` VERTICAL TILDE). Simply
unescaping `\u` sequences in `JsLexer` would have broken ~215 negative Test262
tests that previously passed by accident because `\` was rejected everywhere
outside strings/templates/regexes: escaped reserved words (`v\u0061r`, `\u0069f`,
`({ \u0069f })`), escaped contextual keywords (`st\u0061tic`, `g\u0065t`,
`s\u0065t`, `\u0061sync`, `\u006Ff`, `new.t\u0061rget`), strict-mode future
reserved words (`l\u0065t`, `im\u0070lements`, `st\u0061tic`, `yi\u0065ld`),
`await`/`yield` in async/generator/arrow-parameter/static-block contexts,
`arguments` inside class `static {}` blocks and field initializers, and invalid
escaped code points (`\u0030`, `\u200C`, `\u2E2F`, `\u0023`, surrogate pairs
`\uD835\uDC9C`).

**What changed.**
- Added `include/litebird/js/JsIdentifierTables.hpp`: compact `constexpr`
  Unicode 17.0 sorted range tables (682 ranges for `ID_Start`, 795 ranges for
  `ID_Continue` + `U+200C`/`U+200D`, generated from Node.js v22.22.3 / ICU
  Unicode 17.0) with an ASCII `< 0x80` fast path and binary search.
- Updated `include/litebird/js/JsLexer.hpp` and `src/js/JsLexer.cpp`:
  - Added `JsTokenType::EscapedKeyword` and `JsToken::has_escape`.
  - Kept a zero-overhead pure-ASCII fast path in `scan_identifier()` while
    decoding well-formed UTF-8 and `\uXXXX`/`\u{...}` escapes (with arbitrary
    leading zeros in `\u{...}`) on the slow path and validating against
    `is_js_id_start` / `is_js_id_continue`.
  - Extended `#PrivateName` scanning (`starts_identifier_at`), numeric literal
    trailing-identifier rejection (`1\u0061`), regex flag scanning (disallowing
    `\u` escapes), `regex_allowed()` (`obj.\u0069f / 2` as division), full
    ECMA-262 non-ASCII `WhiteSpace`, and `U+2028`/`U+2029` line terminators in
    `#!` hashbangs, comments, string line continuations, and regex literals.
- Updated `include/litebird/js/JsParser.hpp` and `src/js/JsParser.cpp`:
  - `check_identifier()` now requires `!peek().has_escape`, and all direct
    `async` checks use `check_identifier("async")`.
  - Added `validate_identifier()` enforcing strict-mode future reserved words,
    generator `yield`, async/static-block `await`, `let`/`const` lexical `"let"`
    prohibition, `FunctionExpression` own `[Yield, Await]` binding,
    `ArrowParameters[?Yield, ?Await]` inheritance (reset for `ConciseBody`),
    full-class strict mode (covering class name and `extends`), `case
    JsTokenType::Await:` gated on `m_functions.back().is_async`, and
    `ContainsArguments` early errors in class `static {}` blocks and field
    initializers.

**How it was verified.**
- Differential TC39 Test262 (`tools/conformance/run_test262.py --binary before=/tmp/litebird-before --binary now=build/litebird` at pinned commit `2e58f87d8350`):
  - **21 default directories (`make test262`)**: **10,835 → 12,275 / 15,686 (69.1% → 78.3%, +1,440 gained, 0 lost)**:
    `language/statements/class` +589, `language/expressions/class` +583,
    `language/expressions/object` +159, `language/expressions/assignment` +100,
    `language/statements/function` +5, `language/expressions/function` +2,
    `language/expressions/in` +1, `built-ins/Function` +1.
  - **`language/{identifiers,keywords,future-reserved-words,reserved-words}`**:
    **276 → 374 / 374 (73.8% → 100.0%, +98 gained, 0 lost)**.
  - **`language/{identifiers,keywords,future-reserved-words,reserved-words,white-space,line-terminators,comments,asi}`**:
    **496 → 627 / 635 (78.1% → 98.7%, +131 gained, 0 lost)**.
  - **`language/expressions/{arrow-function,async-arrow-function}`**:
    **323 → 337 / 403 (80.1% → 83.6%, +14 gained, 0 lost)**.
  - **Whole Test262 suite (`built-ins language annexB`, 47,967 tests)**:
    **26,012 → ~27,741 / 47,967 (54.2% → 57.8%, +1,730 gained)**. The single
    remaining difference outside the 21 default dirs is
    `built-ins/AsyncFunction/AsyncFunction-construct.js`, which previously
    passed by accident because `async function(){}.constructor` is still
    `Function` (no `%AsyncFunction%` constructor yet) and `parse_unary()`
    erroneously accepted `await 1` inside non-async functions.
- Added `tests/js-conformance/identifiers.dat` (14 cases, **14/14 in Node.js
  v22.22.3** via `--reference "node -e"`, **14/14 in Litebird**), raising
  `make js-conformance` from **907/921** to **921/935 (98.5%)** and raising
  CI `--min-pass` from `907` to `921` and `JS_ASAN_FLOOR` from `905` to `919`.
- Added 3 C++ unit test suites in `tests/test_suite.cpp` (`make test`: **169/169**
  in Release; ASan+UBSan `test_suite` 100% passing with 0 findings).
- Verified under Release and ASan+UBSan (`make test`, `make conformance`,
  `make js-conformance`, `make dom-conformance`, `make url-conformance`,
  `make wpt`, `make diff-strings`, `make bench`, `make bench-surrogates`,
  ASan+UBSan `test_suite`, `js-conformance` 920/935, `dom-conformance` 640/640).

**What remains open.**
- Strict-mode parameter/function-name validation when `"use strict"` appears
  inside a function's own body (e.g. `function f(static) { "use strict"; }`),
  and prohibiting `eval`/`arguments` as strict-mode binding/assignment targets,
  remain unimplemented.
- Escaped `"use strict"` directive strings (`"use\x20strict"`) still enable
  strict mode because `parse_statement_list` inspects cooked token text.
- `%AsyncFunction%` and `%GeneratorFunction%` distinct constructors/prototypes
  are tracked under the existing *"What a function is, in small"* task row.

---
