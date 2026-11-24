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
// C0 controls (except tab and newline), DEL and C1 controls are replaced by
// U+FFFD. C1 controls are refused both as raw bytes and as UTF-8 scalars;
// malformed UTF-8 is replaced one byte at a time. Other valid UTF-8, including
// a 0x9B continuation byte in an ordinary character, is retained unchanged.
// This assumes a UTF-8 terminal: in an 8-bit mode a continuation byte may be
// interpreted as a control. The `--dump-dom-webplatform` data is not filtered.
std::string terminal_safe(std::string text);

} // namespace litebird
