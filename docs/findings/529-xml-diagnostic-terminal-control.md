# Finding 529: XML diagnostics carried terminal instructions

The parser intentionally retains the text of an invalid XML encoding name in its `XmlError` message. The XML command-line path printed `error.describe()` directly to stderr. This is a useful diagnostic in ordinary cases, but XML input can supply that encoding name, including ESC and BEL bytes. I reproduced the defect through the public `litebird --xml-canonical` command: the raw bytes appeared in stderr, including an OSC 52 clipboard sequence.

This is a display-boundary issue, not a parser or XML-data issue. Filtering at the parser would corrupt the diagnostic value for nonterminal callers, while filtering canonical output would break its machine-readable contract. The CLI now passes just the human diagnostic text through `terminal_safe`. It replaces terminal-active controls with U+FFFD and preserves ordinary printable text; no parser error object, input, file path or successful XML output is modified.

The focused verifier checks the attack bytes safely in a captured subprocess result, ordinary error stability and exact canonical output. The existing CLI-error verifier guards adjacent error handling.
