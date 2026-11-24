# Finding 379 — CSS and paint dumps carried terminal instructions

A stylesheet could put ESC and BEL in a declaration, and page text could
put them in a paint command; the CLI printed both directly. The shared
terminal-text filter used by the DOM dump also passed UTF-8 C1 controls and
malformed sequences such as overlong encodings.

Phase 379 applies the filter to CSS names and values and paint text and
validates scalar UTF-8 within the shared filter. ASCII controls except tab
and line feed, raw or UTF-8 C1 controls, and malformed UTF-8 are replaced;
ordinary Unicode survives. The machine-readable webplatform tree dump is
unchanged. `make verify-css-paint-terminal-safety` is 6/13 on the old
implementation and 13/13 after, with actual CLI output and an independently
checked byte corpus.

The filter assumes a UTF-8 terminal: an 8-bit terminal might interpret a
valid continuation byte as a C1 control. Tabs and line feeds remain, so this
is not log-line-spoofing protection; console, other output and terminal
input remain outside this fix.
