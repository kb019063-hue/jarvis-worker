# Phase 183 — the width nobody could read

Task: the row that began "`getComputedStyle(el).width` is still `auto`". Finding
187.

## 1. What the corpus could not ask

`getComputedStyle(f).width` answered the **computed** value. For a 100px box with
10px of padding and a 5px border that is `"100px"`, while the box is 130 wide;
for a block with no width of its own it is `"auto"`, while the block is as wide
as its containing block. Every geometry property a page actually reads was
unreachable through the one interface meant to answer it — the engine has had
`offsetWidth`, `clientWidth` and `getBoundingClientRect()` since Phase 36, and
none of them is what `getComputedStyle().width` is asked.

CSSOM is explicit that these two are not computed values:

> If the property applies to the element or pseudo-element and the resolved
> value of the `display` property is not `none` or `contents`, then the
> resolved value is the **used value**. Otherwise the resolved value is the
> computed value.
>
> — CSSOM, "resolved values" (`width`, `height`)

So the value is the box the layout made, and there was no box in the answer.

## 2. Which box, and why the answer is not simply "the content box"

The used `width` is not the border box: with `width: 100px; padding: 10px;
border: 5px solid` the box is 130 wide and `clientWidth` is 120, while a browser
reports `100px`. It is also not always the *content* box — `box-sizing` is what
decides which box the declaration controls, and a browser reports that box:

| declaration | box | this engine, before | Chromium 156 |
| :--- | :--- | :--- | :--- |
| `width:100px; padding:10px; border:5px` | 130 | `100px` (accidentally right) | **`100px`** |
| `width:100px` | 100 | `100px` | `100px` |
| `box-sizing:border-box; width:100px; padding:10px` | 100 | `100px` (accidentally right) | **`100px`** |
| `width:auto` in a 784px block | 784 | `auto` | **`784px`** |
| `width:50%` | 392 | `50%` | **`392px`** |
| `width:500px; max-width:100px` | 100 | `500px` | **`100px`** |

The first two rows are the trap: the old answer is *right* for a content-box
declaration with no padding and wrong for everything else, so a test written
against the simple case would have called the gap closed.

## 3. What changed

**The padding is now on the `Element`.** The layout box is the border box and
the border widths were already kept beside it (`Element::layout_border`); the
padding was not, so the content box could not be found from the DOM side.
`Element::set_layout_box` takes the padding too, `Element::layout_padding()`
reads it, `clear_layout_box()` forgets it, and both `LayoutTree` call sites
that publish a box now pass `dimensions.padding`.

**The value is resolved from the box, in the two places that ask.**
`used_content_size` finds the layout, takes the border box and subtracts the
edges — or, under `box-sizing: border-box`, takes the border box whole.
`computed_value` asks it only for `width` and `height` at `depth == 0`, so the
shorthand machinery, the inheritance walk and the `-width` border rules are
untouched.

**The layout is forced, as a browser does.** A percentage cannot resolve
without one, and `Document::ensure_layout()` is already version-guarded, so a
read with nothing changed costs a comparison.

**Four things keep the computed value**, each for a reason the spec or the
engine gives:

- `display: none` and `display: contents` — no box, so CSSOM says computed;
- a non-replaced inline box — CSS 2.1 §10.3.1 leaves its width to its content,
  which is what a browser reports;
- an element with no box at all (detached, never laid out);
- a `calc()` that survives the cascade. That one is worth its own paragraph.

## 4. Three corpus cases that had to stop asking

The DOM corpus's oracle is jsdom, which lays nothing out — "layout of any kind
(every box is 0 by 0)" is already on its list of known divergences, and rule 6
says a case that depends on one of those does not belong there. Recording the
three cases again was tried first and changed nothing: `--record` re-reads
jsdom, which is the point of it.

So the three ask the question they were really about, without the size:

- **the initial-value sweep** no longer prints `width`/`height` (it still prints
  the `min-`/`max-` four, which are computed values);
- **specificity** no longer prints `width` — and it could not have kept it
  meaningfully anyway, because its own `min-width: 19px` overrides
  `width: 16px` (CSS 2.1 §10.4), so the used width was 19 and the declaration
  said 16;
- **lengths in px** no longer prints `width`/`height` for the box whose
  `width: 50%` and `height: calc()` have no jsdom answer.

All three keep their subject, and `--reference` still reports 678/678, so the
recordings are still jsdom's. The used value itself is asserted in
`tests/test_suite.cpp`, where a real layout exists to compare against.

## 5. The `calc()` that had to stay

`computed_value` answers `width` by taking the computed longhand first, and a
`calc()` with a percentage in it keeps its text at computed-value time —
`calc(100% - 20px)` is still that string, because the basis is not known there.

Handing back the layout box instead looked right and was wrong in a way the
suite caught. One of its long-standing assertions writes a `calc()` nested
100,000 deep, which the resolver does not reduce; the layout does not
re-resolve such a declaration either, so the box still belongs to the
*previous* declaration. Reporting it would have answered `2px` for a width the
page had just replaced. So a computed value that still contains `calc(` is
returned as it always was, and the used value is reported only when the
computed value is a length the layout is known to have used.

That keeps one gap and leaves another open on purpose:

- **kept:** `calc(100% - 20px)` still reads as that text, not as 780px. The
  percentage basis is not available at computed-value time, and making it
  available is a change to the resolver.
- **found, not fixed:** a `calc()` percentage is resolved against the
  **viewport**, not the containing block. `calc(100% - 40px)` in a block whose
  containing block is 784 wide lays out at 760, not 744 — 100% was taken as
  800. This is a layout bug that predates this phase and is independent of it;
  the used value reports the box the layout made, which is why it is 760 and
  not the 744 the arithmetic says. It is on the task row.

## 6. Result

| question | before | after |
| :--- | :--- | :--- |
| `100px` + `padding:10px` + `border:5px`, `width` | `100px` | `100px` (box 130) |
| the same under `box-sizing:border-box` | `100px` | `100px` (box 100) |
| `width:auto` block in a 784 block | `auto` | `784px` |
| `width:50%` | `50%` | `392px` |
| `width:5em; font-size:10px` | `50px` | `50px` |
| `width:500px; max-width:100px` | `500px` | `100px` |
| `height:auto` after wrapping text | `auto` | the wrapped height |
| `display:none; width:100px` | `100px` | `100px` (computed, correct) |
| non-replaced `<span>` | `auto` | `auto` (correct) |

The suite asserts the invariant that cannot be faked rather than a table of
numbers: for every case, `getComputedStyle().width` must equal the box
`getBoundingClientRect()` reports, less the padding and border exactly when
`box-sizing` is `content-box`. A wrong answer in the resolver and a wrong
answer in the box cannot both be hidden that way.

The one pre-existing assertion that changed is a comment: it read "`width` is
`calc(100% - 20px)`" and still does. 336 suites pass (was 335); the new
`computed_width_and_height_are_used_values` aborts on the engine of `d51ed33`
and passes on this one.
