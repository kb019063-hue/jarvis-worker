# Finding 187 — the width was a promise the layout never made

## What was wrong

`getComputedStyle(el).width` answered the **computed** value where CSSOM asks for
the **used** value, so every page that measured a box this way got a number that
described the declaration instead of the box.

```
                                          main (d51ed33)   Chromium 156
width:100px; padding:10px; border:5px           100px          100px  (box 130)
box-sizing:border-box; width:100px; padding:10px 100px          100px  (box 100)
width:auto in a 784px block                      auto          784px
width:50%                                        50%           392px
width:500px; max-width:100px                    500px          100px
height:auto after the text wrapped               auto          160px
```

The first two rows are why this survived so long: the old answer is *right* for
a content-box declaration with no edges, so any test that measured a plain
`width:100px` box called the gap closed. It was only wrong for every box that
had padding, a border, a percentage, an `auto`, or a clamp — which is to say,
for boxes.

## What made it hard to see

`offsetWidth`, `clientWidth` and `getBoundingClientRect()` have answered
correctly since Phase 36, so the layout was never in doubt. The problem was one
level up: the computed-style path resolves declarations, and a declaration is
not a box. Two different mechanisms answered "how wide is this element" and only
one of them had ever been asked the question the standard poses.

## What changed

`Element` now keeps the **padding** beside the border (`Element::layout_padding`,
published by `LayoutTree::build`), because the content box is the border box less
both edges and only the border had been recorded. `computed_value` asks the
layout for `width` and `height`, forcing `Document::ensure_layout()` the way a
browser forces a synchronous layout for the same read.

Which box is reported follows `box-sizing`, because that is the box the `width`
declaration controls — the content box by default, the border box under
`border-box`.

## The mistake worth recording

The first attempt reported the content box unconditionally. It was wrong for
`border-box` and it was wrong for a `calc()` the resolver cannot reduce: the
suite's 100,000-deep `calc()` leaves the layout holding the *previous*
declaration's box, and reporting that box would have answered `2px` for a width
the page had just changed. Both were caught by the suite, and the fix is a guard
that is narrower than it first looks — the computed value is only replaced when
it is a length the layout is known to have used.

Two checks would have caught neither: `check_numbers` and the DOM corpus do not
know what a used value is, and the only assertion that moved is a comment.

## Left open, and named

A `calc()` with a percentage in it still keeps its text at computed-value time,
which is the old gap and stays. Separately, and **found rather than fixed**: the
layout resolves a `calc()` percentage against the **viewport**, not the
containing block — `calc(100% - 40px)` in a block whose containing block is 784
wide lays out 760 wide, not 744. The used value reports 760, because 760 is the
box. That is a layout bug older than this phase and it is on the task row.
