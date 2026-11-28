# Phase 529: XML diagnostics cannot command the terminal

A malformed XML encoding declaration can carry terminal control bytes in its declared encoding name. `XmlError::describe()` faithfully included that input, and the `--xml-canonical` / `--xml-events` CLI printed it raw to stderr. A declaration such as `<?xml version='1.0' encoding='UTF-8 ESC ]52;c;owned BEL'?>` therefore reached the user's terminal as an OSC 52 clipboard instruction.

The CLI now applies the existing `terminal_safe` boundary only when displaying the human-readable decode and parse diagnostics. The parser's `XmlError`, input bytes, exit status, and successful canonical/event output remain untouched. XML output formats intended for tools are not filtered.

`make verify-xml-diagnostic-terminal-safety` exercises a real CLI process and captures its output without displaying attack bytes. It checks hostile encoding declarations through both CLI modes, unchanged ordinary parse-error wording, and unchanged canonical XML output: **4/4 pass**. The existing `make verify-cli-error-terminal-safety` also passes **8/8**. `make check-numbers` and `make check-suites` pass.
