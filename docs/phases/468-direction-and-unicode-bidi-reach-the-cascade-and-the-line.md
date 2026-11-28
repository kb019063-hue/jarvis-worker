# Phase 468 — `direction` and `unicode-bidi` reach the cascade and the line

Phase 109 built the algorithm — UAX #9, rules P2 to L2, isolating run
sequences, bracket pairs, mirroring — and the inline formatting context has
run it on every line since. But nothing a page could say reached it. The
paragraph's direction came from P2/P3 alone (the first strong character), so
`dir="rtl"` on an element styled nothing, `direction` and `unicode-bidi` were
unknown properties that matched no declaration, and `<bdo>` was an ordinary
inline element. A page with English UI text and one Arabic paragraph had no
way to ask for the paragraph to run the other way.

After this phase the two properties are cascade properties: they parse, they
inherit, they compute, the `dir` attribute hints them, the UA sheet gives the
isolated-content elements their `unicode-bidi: isolate` and `bdo` its
`bidi-override`, and the line assembler takes the paragraph's direction from
the computed style instead of the first strong character.

```html
<!-- Nothing worked before; all of it does now. -->
<p dir="rtl">شكرا جزيلا</p>
<p style="direction: rtl; unicode-bidi: bidi-override">mixed 123 text</p>
<bdo dir="rtl">backwards ASCII</bdo>
```

## The cascade gets the two properties

`ComputedStyle` carries `direction` and `unicode_bidi` (`normal`, `embed`,
`isolate`, `bidi-override`, `isolate-override`, `plaintext`). Both parse from
declarations, both inherit, both join the style key's hash, and
`getComputedStyle` answers both by name. The initial value of `text-align`
becomes `start`, which is the value CSS says it is, and `start`/`end`
resolve against the line's direction at layout time (left and right stay
physical).

The `dir` attribute is a presentational hint, set before the matched rules so
an author declaration can win over it: `dir=ltr` and `dir=rtl` set
`direction` directly. `dir=auto` asks the resolver's question for the
element's own text content — the base level of `bidi_paragraph_utf8` with no
explicit paragraph level — so `<div dir=auto>שלום</div>` computes
`direction: rtl` and `<div dir=auto>hello</div>` computes `direction: ltr`,
with digits (no strong character) falling back to ltr.

The UA sheet carries the HTML §Rendering rules: the block containers and
table pieces (`address`, `div`, `p`, `li`, `td`, …) get `unicode-bidi:
isolate`, and `bdo` gets `unicode-bidi: bidi-override`. The same two lists
exist in the computed-value path's element defaults (`js/DomStyleBindings.cpp`),
so what a script is told and what layout does agree.

## The line takes the paragraph's direction

`apply_bidi_to_line` now takes the computed style of the box that owns the
line. Its base level is that style's direction — not P2/P3 — unless the style
says otherwise: `plaintext` asks the text itself (P2/P3), and `embed`,
`isolate`, and the two overrides ride as the spec's inline formatting
annotations on the runs that carry them.

The line is assembled as a paragraph of its own. Inline elements that open a
new embedding context (an `isolate` or `embed`/`override` in a line whose
paragraph style has neither) wrap their content in an implicit level shift:
uniform, even, and therefore invisible to rule L2 — the reordering is
unchanged, but the boundaries are where the spec puts them, so a Latin
isolate inside a Hebrew line cannot swap places with the Hebrew around it.
Synthetic fragments (generated content, list markers) join the paragraph
without contributing one. The fast path that skips the pass for plain
left-to-right ASCII keeps out of the way: it must not fire for an ltr line
whose style carries `isolate`, `embed`, `plaintext`, or an override, because
those change the answer for non-ASCII content inside.

`unicode-bidi`'s override values seed the elements inside them:
`bidi-override` and `isolate-override` on an element reach its children's
computed styles before the matched rules run, which is what makes `<bdo
dir="rtl">abc</bdo>` paint `cba` inside an ordinary paragraph — the
paragraph's own text keeps its order, and only the bdo's content runs back.

## A run's piece carries the run's text

The reorder builds visual pieces from the runs it computed. It always cut the
*width* correctly — a run of three letters got three letters of advance — but
only rebuilt the *text* for right-to-left runs. A left-to-right run sliced
out of a longer fragment kept the whole fragment's text, painted inside a
piece-sized rectangle, so the line drew `(שלום)(שלום)` where the levels had
moved a single `(`, the word, and `)`. Phase 109 never showed this because
first-strong kept such a fragment one run: the slice was the whole fragment,
so the unsliced text was correct. Rule N0's bracket resolution is what
exposed it — `"(שלום)"` resolves to three runs (`(`, the word, `)`) on the
brackets' even level, and every one of them needs its own text. Pieces now
always carry exactly their code point range: copied in order on an even
level, reversed and mirrored on an odd one.

## What the corpus could not ask

The whole render corpus — reftests, conformance pages, WPT files — had no
`dir` attribute in either direction and not one `direction:` or
`unicode-bidi:` declaration: the properties were unparseable, so no page
bothered. The suite (`tests/suites/468-direction-and-unicode-bidi-reach-the-line.cpp`)
is the feature as the page sees it: computed values (hints, inheritance,
`dir=auto`, the UA sheet's two lists, every `unicode-bidi` value by name), a
mixed line flipping under `dir="rtl"`, the override painting ASCII backwards
and only where asked, start/end alignment following the line while
left/right stay physical, and the fallback paragraph level the resolver's own
question uses. `make test` runs it with the rest; the Unicode conformance
suites (`make bidi-conformance`, 861,948 cases) still pass unchanged.
