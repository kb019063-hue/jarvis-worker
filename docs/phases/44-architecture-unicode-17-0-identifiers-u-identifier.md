# Phase 44 Architecture: Unicode 17.0 Identifiers, `\u` Identifier Escapes & Escaped Keyword Semantics

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 44 Architecture: Unicode 17.0 Identifiers, `\u` Identifier Escapes & Escaped Keyword Semantics` section.
> The text below is the section as it stood, unchanged.


### 1. Compact Unicode 17.0 `ID_Start` / `ID_Continue` Tables (`include/litebird/js/JsIdentifierTables.hpp`)
- Generated reproducibly from Node.js v22.22.3's ICU Unicode 17.0 data (`/\p{ID_Start}/u`, `/\p{ID_Continue}/u`, plus `U+200C` ZWNJ and `U+200D` ZWJ per ECMA-262 §12.7.1).
- Two sorted `constexpr CodePointRange` tables: 682 ranges (5.4 KB) for non-ASCII `ID_Start` and 795 ranges (6.3 KB) for non-ASCII `ID_Continue` + ZWNJ/ZWJ.
- `is_js_id_start(uint32_t cp)` and `is_js_id_continue(uint32_t cp)` take a branch-predicted ASCII `< 0x80` fast path (`a-z`, `A-Z`, `_`, `$`, plus `0-9` for continue) and fall back to binary search (~10 comparisons) for non-ASCII code points. Surrogates (`0xD800..0xDFFF`), out-of-range values (`> 0x10FFFF`), and non-ID symbols such as `U+2E2F` (VERTICAL TILDE) and `#` (`U+0023`) are rejected.

### 2. Lexer Decoding, Private Names, and Token Classification (`include/litebird/js/JsLexer.hpp`, `src/js/JsLexer.cpp`)
- **Pure-ASCII fast path**: unescaped ASCII identifiers scan directly via a single `substr` with zero extra allocations.
- **Unicode & escape slow path**: triggered on the first `\` or byte `>= 0x80`. Decodes well-formed UTF-8 code points (`decode_utf8`, rejecting overlongs and raw surrogates) and `\uXXXX` / `\u{HexDigits}` escapes (`scan_identifier_unicode_escape`, permitting arbitrary leading zeros per ECMA-262 while rejecting `> 0x10FFFF`, empty braces, surrogate pairs in identifiers, and `\xXX`). Validates the first code point with `is_js_id_start` and subsequent code points with `is_js_id_continue`.
- **Private names (`#IdentifierName`)**: `scan_token()` uses `starts_identifier_at(m_cursor + 1)` so `#\u0061`, `#\u{1D49C}`, `#\u0069f`, and `#k\u200C` decode cleanly into `JsTokenType::PrivateName`, while `#\u0030`, `#\u200C`, `#\u2E2F`, and `\u0023foo` fail with `JsSyntaxError`.
- **Escaped keyword classification (`JsTokenType::EscapedKeyword`, `JsToken::has_escape`)**:
  - Unconditionally reserved words written with a `\u` escape (`v\u0061r`, `\u0069f`, `tr\u0075e`, `n\u0065w`, etc.) are classified as `JsTokenType::EscapedKeyword` with `is_keyword = true` and `has_escape = true`. They are never accepted as keywords, bindings, references, labels, or shorthand properties, while remaining valid `IdentifierName` tokens (property names, member names, methods, `#` private names). `regex_allowed()` treats `EscapedKeyword` as expression-ending so `obj.\u0069f / 2` lexes as division.
  - Contextually reserved words with escapes (`l\u0065t`, `yi\u0065ld`, `aw\u0061it`) are classified as `JsTokenType::Identifier` with `has_escape = true` so they never act as keywords while remaining valid identifiers outside the contexts that reserve them.
- **Unicode whitespace, line terminators, and numeric/regex boundaries**: `skip_whitespace_and_comments()` handles all 19 non-ASCII ECMA-262 `WhiteSpace` and `LineTerminator` code points (`U+00A0`, `U+1680`, `U+2000..U+200A`, `U+2028`, `U+2029`, `U+202F`, `U+205F`, `U+3000`, `U+FEFF`); `#!` hashbangs, `//` and `/* */` comments, string line continuations, and `/.../` regex literals all recognize `U+2028`/`U+2029`; `scan_number()` rejects trailing `\` and Unicode `ID_Start` (`1\u0061`); `scan_regex()` scans flags with `IdentifierPartChar` (disallowing escapes); and `scan_escape()` permits arbitrary leading zeros in string/template `\u{...}` escapes.

### 3. Parser Enforcement (`include/litebird/js/JsParser.hpp`, `src/js/JsParser.cpp`)
- `check_identifier(const char*)` requires `!peek().has_escape`, preventing escaped contextual keywords (`static`, `async`, `get`, `set`, `of`, `target`) from acting as modifiers or syntax while preserving their validity as ordinary property/method names (`class C { st\u0061tic() {} }`, `({ g\u0065t() {} })`).
- `validate_identifier()` enforces strict-mode future reserved words (`implements`, `interface`, `let`, `package`, `private`, `protected`, `public`, `static`, `yield`), `yield` in generators and arrow parameters, and `await` in async functions, class `static {}` blocks, and arrow parameters — while `FunctionExpression` names bind their own `[Yield, Await]` parameters (`(function await(await) { return await; })` in `static {}`, `(function yield() {})` in `*g()`).
- Class strict mode now covers the entire `ClassDeclaration`/`ClassExpression` including the class name and `extends` clause; `let`/`const` lexical bindings forbid `let` even in sloppy mode; `case JsTokenType::Await:` in `parse_unary()` applies only when `m_functions.back().is_async`; and `FunctionContext::forbid_arguments` enforces `ContainsArguments` early errors in class `static {}` blocks and field initializers (inherited by arrow functions, cleared by non-arrow functions).

---
