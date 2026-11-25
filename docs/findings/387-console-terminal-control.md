# Finding 387 — Console output carried terminal instructions

A page's console arguments and counter labels reached stdout or stderr without
terminal filtering. A string containing ESC, BEL, carriage return or encoded
C1 controls could change terminal state rather than merely display text. Both
the tree-walker and the independent bytecode VM console were affected.

Phase 387 filters displayed arguments and labels with `terminal_safe`, without
changing JavaScript values, counter key identity, or the application's own
warning/error ANSI decoration. The real CLI and page verifier passes 7/7,
versus 2/7 with the old console implementations; the new engine suite fails
against those implementations and passes after the fix.

Redirected console streams are filtered too, changing control-bearing logs.
The filter assumes a UTF-8 terminal; tabs and newlines still allow visual
line spoofing. Other terminal-output paths and input remain outside this fix.
