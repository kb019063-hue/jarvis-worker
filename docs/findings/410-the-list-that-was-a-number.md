# Finding 410: the list that was a number

## The symptom

```js
var t = document.createElementNS(SVG_NS, 'text');
t.setAttribute('x', '10 20 30');
t.x.baseVal.value;             // 0
t.x.baseVal.numberOfItems;     // undefined
```

Three positions on a text element read back as **zero**, and a write replaced
all three with one. `dx`, `dy` and `rotate` were `undefined` entirely.

## Why the phase before this one made it

Phase 395 gave every `<length>` geometry attribute an `SVGAnimatedLength`, and
that was right for all but four. `rect.x` is one length; `text.x` is a list of
them, one per glyph run (SVG 2 §4.5.3, §4.6.3). Phase 395 put the two text
elements in the same table as everything else:

```cpp
{"text", {"x", "y"}},
{"tspan", {"x", "y"}},
```

A table of (tag, attribute) → one length cannot express "this attribute is a
list", so the table said the wrong thing and every later reader believed it.

**The bug is a classification error, and it survived the phase's own tests.**
Phase 395's suite asserts `typeof text.x == "object"` — which stayed true,
because a single-length object is still an object. The suite that would have
caught it had to ask `numberOfItems`, and nothing did, because nothing in the
phase knew a list was possible.

## Why it was found now

Not by reading code: by asking a browser. `tools/conformance/verify_svg_length_lists.py`
runs one page script twice, once in pinned headless Chromium and once in
Litebird, and prints the answers side by side. The first run answered

```
t.x ctor: SVGAnimatedLengthList     (Litebird: SVGAnimatedLength)
```

which is a single line of output for a bug that had been in `main` since the
previous morning and had a passing suite.

## jsdom is no reference for this, which is why it went unnoticed

jsdom has **no `SVGLength`, no `SVGAnimatedLength`, and no list types at all**.
`rect.x` is `undefined` there, and `typeof SVGLength` does not even resolve. The
DOM conformance corpus is recorded from jsdom, so every case in it that could
have touched these attributes would have recorded `undefined` — and recorded
`undefined` again after the fix, and passed either way. A corpus can only
record what its reference can express.

This is the second time in the SVG work that jsdom could not answer (Phase 395
recorded the first), and it is worth stating as a rule: **for any SVG DOM
behaviour, jsdom is an oracle only for the absence of the feature.** The
reference is Chromium, and the harness now exists.

## Two things the differential caught that reasoning did not

**Indexed writes.** The first implementation refused `list[0] = 5` outright, on
the reading that §4.5.3's `SVGLengthList` has no indexed setter. Chromium
throws — but the message says why:

```
TypeError: Failed to set an indexed property [0] on 'SVGLengthList':
           parameter 2 is not of type 'SVGLength'.
```

The list *does* take an indexed write. It takes an `SVGLength`, puts its value
in the place named, and keeps the count; it is the argument **type** that is
enforced, before the range. A refused `[[Set]]` would have been silent in sloppy
code, which is how the difference was noticed: the probe threw in Chromium and
did not throw here.

**`SVGLength.value` is in user units.** Writing `value = 100` to an attribute
`x="1cm"` produced `"100cm"` here and `"2.64583cm"` in Chromium. §4.5.2 is
unambiguous — `value` converts, `valueInSpecifiedUnits` does not — and Phase
395 wrote both setters the same way. It is the same mistake the phase's own
write-up warns about for the *getter* ("a type that converted on the way in
could not answer the first two"), made in the other direction.

## What is asserted so it cannot come back

`tests/suites/410-text-positions-are-length-lists.cpp` — one file, the
interfaces, three entries from one attribute, the live write-back through
`getItem`, every member and its out-of-range exception, the argument type of an
indexed write, `textPath` answering `undefined` for all four, an absent
attribute being an empty list rather than a length of 0, and `value` in user
units for a plain `rect` as well as for a list entry. Reverting
`src/js/DomHtmlBindings.cpp` fails it at `:48`.

The differential is the other half: 28 probes, 3 matching before and 28 after,
kept in the repository so the next hand to touch these types can run it in a
minute instead of trusting a suite that already passed once while the bug was
live.
