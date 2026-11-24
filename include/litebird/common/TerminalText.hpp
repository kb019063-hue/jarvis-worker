#pragma once

#include <string>

namespace litebird {

// Page text that goes to a terminal.
//
// A dump, a `console.log` and an error message all carry text a page chose, and
// a terminal acts on some bytes rather than printing them. `ESC` (0x1B) starts
// a control sequence; `0x9B` starts a CSI with no `ESC` in front of it; `0x9D`
// starts an OSC, which is what `osascript`, `iTerm2`'s clipboard and every
// hyperlink-capable terminal read; `BEL` ends an OSC string, and `CR` returns
// the cursor to column zero. A page that puts any of them in its `title`, an
// attribute, a text node or a comment prints an instruction, not text: with
//
//     <title>OK&#27;]52;c;base64</title>
//
// every copy of the run rewrites the operator's clipboard. The shell and the
// uncaught-error diagnostics refuse these bytes (`terminal_diagnostic`,
// `JsOps.cpp`, and `make verify-js-error-terminal-safety`); the human dumps
// never did.
//
// What comes out is the same text with every byte a terminal acts on replaced
// by U+FFFD, which is what "a character that is not a character" looks like in
// the output and cannot be mistaken for one of the page's own. Everything else
// — a tab, a newline, and every byte of a UTF-8 sequence — is untouched, so a
// dump of an ordinary page is byte for byte what it was, and the tools that
// read these dumps (`#text: "` in `tools/fuzz/deep_pages.py`) still find them.
//
// The 8-bit forms only arise from a latin-1 or windows-1252 document, where a
// single byte really is U+009B. In UTF-8, `0x9B` cannot start a character
// (a continuation byte always follows a lead byte), so replacing a lone one
// cannot damage text that was meant.
std::string terminal_safe(std::string text);

} // namespace litebird
