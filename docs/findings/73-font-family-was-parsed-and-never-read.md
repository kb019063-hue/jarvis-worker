# Finding 73 — `font-family` was parsed, kept, and never read

**Severity:** high — a property the engine accepted and silently ignored, and
one that appears in the engine's own stylesheets.

**Found by:** reading. `grep` for `font-family` across `src/` and `include/`
returns the parser's property list, the `DomStyleBindings` tables that preserve
its case, the UA stylesheet rule for `pre`, three stylesheets in
`WebBrowserServer` — and no read of the value anywhere.

## What was wrong

`font-family` was a known property. Its value survived the cascade. Nothing
asked for it.

So every element was measured and drawn in the single face `FontRenderer`
loaded at start-up. Concretely:

- The UA stylesheet has `pre, listing, xmp, plaintext, code, kbd, samp, tt
  { font-family: monospace }`. Those elements were laid out with proportional
  advances, so `<pre>` did not line up and code did not look like code.
- `WebBrowserServer`'s own pages declare `-apple-system, BlinkMacSystemFont,
  "Segoe UI", Roboto, sans-serif` and `ui-monospace, SFMono-Regular, Menlo,
  monospace`. Both rendered identically.
- `ParsedCanvasFont` had a `family` member, defaulting to `"sans-serif"`,
  parsed out of the canvas `font` shorthand — and it was passed to nothing.
  Canvas text selection did not exist either.

`DomStyleBindings.cpp` preserving the case of `font-family` values (a special
case shared only with `animation-name`) is what made this conspicuous: the code
went to trouble over a value with no reader.

## What fixed it

Phase 67: the `name` table is read, the host's faces are registered by the
family names they declare, a CSS list is resolved per element, and the winning
face is loaded lazily and used to measure and draw.

## How it was verified

`make test`, suites `font_family_selects_a_face` and
`font_family_reaches_layout_from_css`, 211 suites in total passing.

The oracle is Ahem, which is in the repository and therefore available on any
host: every one of its 278 codepoints advances exactly 1em, so four characters
at 16 px measure 64.0 px in Ahem and in essentially nothing else — an absolute
check, not a comparison between two faces.

```
$ grep -n 'font_family' src/css/StyleResolver.cpp   # the property is read now
1785:    } else if (prop == "font-family") {
```

## The bug the test is really there for

Two places used `kDefaultFamily` — `0` — as the "not chosen yet" marker. But
`0` is also the id of the first family found, which on this host is DejaVu
Sans. So `sans-serif` could never be assigned (the right answer looked
unassigned forever) and `sans-serif, Ahem` fell through and drew Ahem.

Neither was an error. Both were wrong answers, which is why the regression test
asserts `"sans-serif, Ahem"` does *not* resolve to Ahem rather than merely
checking that `"Ahem"` does.

## What is still open

No `@font-face`, no italic face, and the weight is read from the subfamily name
("Book", "Bold") rather than `OS/2.usWeightClass`. The generic-family mapping
is a guess made from font names, because there is no fontconfig and none was
added.
