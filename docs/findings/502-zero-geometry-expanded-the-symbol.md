# Finding 502: zero geometry expanded the symbol

A rectangle with a zero side contributed a line-shaped box to its instanced
symbol's union, even where Chromium omitted the rectangle entirely. Negative
viewport dimensions also became negative percentage bases. Phase 502 fixes
these cases and documents the measured zero-origin point behavior; see the
[phase](../phases/502-when-a-symbol-draws-no-rectangle.md) for the 466-case
browser differential and mutation test. Other SVG viewport references, text,
stroke and exact transformed curves remain open.
