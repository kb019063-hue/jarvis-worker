# Finding 439 — Layout dumps carried terminal instructions

The human-readable layout dump printed page-controlled element IDs and text
without filtering. A page containing ESC, BEL, CR or UTF-8 C1 characters could
make `--dump-layout` send terminal commands instead of displaying the data.

Phase 439 filters the ID and both text branches as they are written, keeping
DOM and layout data unchanged. The real-CLI verifier passes 9/9 checks, versus
6/9 with the old implementation; the new unit suite fails against that old
implementation.

Redirected dumps are filtered too. The filter assumes a UTF-8 terminal, and
tabs and line feeds remain, so it does not prevent line spoofing. The
webplatform tree dump remains unfiltered for its machine-readable corpus;
other terminal output and input remain outside this fix.
