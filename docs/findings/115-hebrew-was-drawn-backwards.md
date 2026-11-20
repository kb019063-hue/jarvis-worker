# Finding 115 — the painter draws left to right, so an RTL run is handed to it backwards

**Resolved by Phase 109** (`docs/phases/109-bidi-uax9.md`).

## The finding

There was no bidirectionality in the engine at all, and the shape of the layout
code made that easy to miss: the inline formatting context places fragments in
logical order with increasing `x`, and the painter draws each fragment's string
from its `x` rightwards. Nothing in that pipeline is wrong for a left-to-right
script, and a page of English exercises none of it. A page of Hebrew exercises
all of it:

```
$ ./build/litebird hebrew.html --dump-paint      # before Phase 109
  DrawText: (0, 8) "שלום"
```

`ש` is at the left. It is the *first* letter of the word, and the eye starts at
the right, so the word reads backwards. Two words were worse: `שלום עולם` put
`שלום` on the left, which reverses the sentence as well as each word.

## Why it was not a one-line fix

Sorting the fragments right-to-left gets the words in the right order and
leaves each word inside out, because the order of the *characters within a
fragment* is a separate question with its own rules. Splitting a string at
arbitrary points is also wrong: which characters form a run is decided by
`Bidi_Class`, by the numbers in the text (a European number inside Arabic is
read left to right), and by bracket pairs, which take a direction from what
they enclose.

So the fix is the algorithm — UAX #9 rules P2 to L2 over the line's text — and
then the painter's contract: because `DrawText` writes its string left to
right, an odd-level run is passed to it in reverse order with each mirrored
character replaced by its pair (L4). `(שלום)` is the case that needs both: the
run reverses, and the parentheses are exchanged so the *logical* opening
bracket is drawn on the right, opening leftwards, as it must in RTL.

## The evidence that it is right

Unicode publishes the answer, so this needed no reference implementation:
`tests/bidi/BidiTest.txt` and `tests/bidi/BidiCharacterTest.txt`, 861,948
cases between them, all passing (`make bidi-conformance`). The four bugs the
suite caught were all in the rules' exact wording rather than in their shape:

1. `RLE` and `LRE` push a **neutral** override status; the override belongs to
   `RLO` and `LRO`. Inheriting it made `RLO LRE L` resolve to 3 instead of 2.
2. W2 and W7 search backwards for the first *strong* type. A number is not one
   — `AN EN` must let W7 turn the `EN` into `L`, not stop at the `AN`.
3. `sos`, `eos` and N2 use the levels the **X rules** left. Reusing the working
   levels let one isolating run sequence's I1/I2 change the next one's
   boundaries: `AN RLE NSM` resolved the mark at level 2 instead of 1.
4. N0 pairs brackets that are still `ON`. A bracket under an explicit override
   has already been made `L` or `R`, and re-pairing it undid the override:
   `LRE … LRO )` put the closing bracket at level 3 instead of 2.

The last failing case was 64 nested parentheses, which is BD16's stack limit:
UAX #9 allows an implementation to stop pairing past 63, and the suite's
expected levels are the ones you get when the whole sequence's pairs are
dropped.
