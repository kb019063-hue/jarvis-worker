# Finding 468 — the line ran the algorithm nothing could steer

Phase 109 put the whole bidirectional algorithm in the engine and then gave
it exactly one input: the first strong character of the text itself. That
made the pass unfalsifiable from the page — no declaration parsed, no
attribute styled, so every page got the same answer for the same text, and
the corpus (no `dir=` anywhere, not one `direction:` in any suite) never
once exercised a second answer.

```html
<!-- All of this was inert. -->
<p dir="rtl">hello שלום</p>          <!-- P2 still chose ltr: "hello" is strong -->
<p style="direction: rtl">hello שלום</p>   <!-- unknown property -->
<bdo dir="rtl">abc</bdo>             <!-- an ordinary span -->
```

A direction that cannot be asked for is not a smaller feature than an
algorithm that cannot answer: it is the same missing half. The algorithm's
conformance (861,948 Unicode cases) said nothing about it, because the
conformance harness states its cases as class lists plus an explicit
paragraph level — the one input the page had no way to give.

## The defect that was hiding behind the one input

While wiring the style through, a second, older defect surfaced in the
reorder's output assembly. The line's runs are slices of the fragments the
line breaker made, and each visual piece got its slice of the *width* — but
only right-to-left runs had their *text* rebuilt. A left-to-right run kept
its whole source fragment's text inside a piece-sized rectangle:

```
logical: ( שלום )        one fragment, three runs after N0
run 0: "("  width of "("   text "(שלום)"   ← whole fragment
run 1: word width of word  text "םולש"     ← rebuilt
run 2: ")"  width of ")"   text "(שלום)"   ← whole fragment
```

The line painted `(שלום)םולש(שלום)`. Phase 109 could not have caught this:
with first-strong as the only base, a lone bracket pair around one Hebrew
word resolved N0's brackets onto the embedding level *with* the word's
neighbours in a way that kept the fragment one run in every case the corpus
had — and when the slice is the whole fragment, carrying the whole text is
correct. Explicit direction made the engine split `"(שלום)"` into the three
runs N0 always meant, and the unsliced text became visible at once.

The wrong reflex is to make the test match the output, because the pieces'
*widths* were already right and the concatenation merely looked like a
different-but-plausible parenthesisation. The check that caught it —
concatenate what the line paints and compare against what one reversed
fragment used to paint — is the invariant worth keeping: **reordering moves
ink, it does not duplicate it.** The levels of a line may change under any
`direction`; the multiset of code points it paints may not.
