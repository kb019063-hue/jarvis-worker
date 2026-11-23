# Phase 195 — a percentage of what the declaration began with

Task: the `calc()` row of the open-tasks table — a `calc()` whose percentage
resolved against the viewport instead of against the declaration's own basis.
Finding 199.

## 1. What was wrong

`font-size: calc(50% + 2px)` on a child of a 20px paragraph should be 12px.
On the engine of `2a86d30` it is 12px to the script and 402px to the layout: a
single character laid out a **502.5px** line box while `getComputedStyle`
reported `12px`.

```
<div style="font-size:20px"><div id=a style="font-size:calc(50% + 2px)">a</div></div>

  layout  BlockBox #a  w=800 h=502.5   IFC lines=1  text="a"
  script  cs.fontSize = "12px"
```

`502.5` is 402 × 1.25, the line box of a 402px font. The two halves of the
engine disagree because they were answering different questions:

- `StyleResolver` resolved every `calc()` in every declaration, before the
  property saw the value, with `m_viewport_width` as the percentage basis:
  `50%` of 800 + 2 = **402**, stored as a plain `px` length. The layout then
  did exactly what `font-size: 402px` asks for.
- the script side (`DomStyleBindings`) evaluated the same `calc()` against the
  inherited font size and answered `12px`, which is the number a browser
  answers.

The same defect, quieter, everywhere a `calc()` meets a percentage: a
`line-height: max(100%, 20px)` at 10px laid out an 800px line box, a
`font-size: max(50%, 30px)` laid out 500px while reporting 20px, and a
`line-height: calc(100% + 4px)` at 10px laid out 804px and reported
`calc(100% + 4px)`.

## 2. Two properties whose basis the cascade has

Most percentage lengths need the containing block, which only layout knows —
that is why a plain `width: 50%` is stored as a percentage and resolved by
`LayoutBox::resolve_percentages`. Two are different, and both are in the same
block as the declaration:

| property | a percentage is of | CSS |
| :--- | :--- | :--- |
| `font-size` | the **inherited** font size | Fonts 4 §2.4 |
| `line-height` | this element's **own** font size | 2.1 §10.8.1 |

Both were already handled correctly for a bare `50%`: `apply_longhand` has a
percentage branch for each, and both use the right basis (`m_em_base` for the
one, `style.font_size` for the other — the block is replayed in source order
with `font-size` landed first, which is why the second is known). What was
wrong was everything that reached them as a `calc()`: the value had already
been replaced by a number measured against the viewport by the time the
property branch ran.

So the basis is chosen where the resolution happens, and only `font-size` and
`line-height` get their own; every other property keeps the viewport, which is
still the wrong basis and is what §4 leaves open.

## 3. What changed

**`src/css/StyleResolver.cpp`, `apply_longhand`.** The one call that resolved
every `calc()` in every declaration now picks the basis by property:

```cpp
float basis = m_viewport_width;
if (prop == "font-size") basis = m_em_base;
else if (prop == "line-height") basis = style.font_size;
val = resolve_calc_functions(val, basis);
```

There is no other new code on this side. The property branches were already
right; they were being handed the wrong number.

**`src/js/DomStyleBindings.cpp`, three places that report.** The script side
was right about `calc()` and wrong about the rest of the family:

- `line-height: calc(100% + 4px)` kept its text. A percentage in a
  `line-height` is of the element's own font size, which the reporting side
  has, so it is now a length — and `line-height` has the same treatment for
  `min()`/`max()`/`clamp()` as for `calc()`.
- `font-size: max(50%, 30px)` came out as the **parent's** size. `CalcParser`
  in that file is a sums-and-products evaluator that does not know a
  comparison, and every one of them fell through to "inherit". They now go to
  `litebird::CalcContext`/`calc_eval_text` — the same typed evaluator the
  cascade uses — and, because `min(50%, 30px)` cannot be compared until the
  percentage is a length, through a small `resolve_math_with_basis` that
  substitutes the percentage against the basis and then evaluates.

**`tests/test_suite.cpp`, a new suite**
`a_calc_percentage_is_of_the_declaration_not_the_viewport` (344th suite). Its
central assertion is not a table of numbers but the one property that ties the
two resolvers together: **no case may report a font size its own line box
contradicts**. On the old engine the first case is out by the viewport.

**`AGENTS.md`**: the row now says what is fixed and what is not.

## 4. What is still open

A `calc()` percentage in a **box** length — `width`, `min-/max-width`,
`height`, `margin-*`, `padding-*`, `top`/`left`/… — still resolves against the
viewport. `width: calc(100% - 40px)` in a 600px block lays out **760** wide:
784/600 is not consulted, 800 is.

The fix is not a basis argument, which is why it was not made here: the
resolved number is stored in `ComputedStyle::width` and read as a pixel length
by roughly a hundred places in `src/layout/LayoutBox.cpp`, `LayoutTree.cpp` and
`FloatTableLayout.cpp`, each of which would have to learn that the field can
still be a percentage in a `calc()`. A `calc()` of *only* percentages does get
the right answer already, because it lands on the plain percentage path and
layout resolves it against the containing block; what is wrong is a `calc()`
that mixes the two. The suite pins the gap (`answer === viewport - 40`) so
that a later fix changes one line here rather than being a surprise.

Two smaller ones, also unchanged and both older than this phase: a `calc()`
nested past the resolver's depth limit stays text (and therefore leaves the
*previous* declaration's box in place), and `line-height: max(100%, 20px)` on
an *inherited* element is still computed at the point of inheritance rather
than at the point of use, which is the difference the CSS 2.1 §10.8.1 note
makes between a number and a percentage.

## 5. Result

Every row is the engine's own answer, before and after, on the same page
(`<body style="font-size:20px">` unless the row says otherwise):

| declaration | before (layout / script) | after (layout / script) |
| :--- | :--- | :--- |
| `font-size: calc(50% + 2px)` | 502.5px / `12px` | **15px / `12px`** |
| `font-size: calc(50%)` | 500px / `10px` | **12.5px / `10px`** |
| `font-size: calc(1em + 2px)` | — / `22px` | **27.5px / `22px`** |
| `font-size: max(50%, 30px)` | 500px / `20px` | **37.5px / `30px`** |
| `font-size: min(50%, 4px)` | 5px / `20px` | **5px / `4px`** |
| `font-size: clamp(5px, 50%, 8px)` | 10px / `20px` | **10px / `8px`** |
| `line-height: calc(100% + 4px)` at 10px | 804px / `calc(…)` | **14px / `14px`** |
| `line-height: calc(50% + 1px)` at 10px | 401px / `calc(…)` | **6px / `6px`** |
| `line-height: max(100%, 20px)` at 10px | 800px / `max(…)` | **20px / `20px`** |

(The layout column is the box height for one line of text, so it is 1.25 × the
font size for a `font-size` row and the line height itself for a
`line-height` row.)

How it was verified:

```
make test               # All 344 engine test suites passed successfully!
make js-conformance     # JS conformance, full corpus: 1146/1158 (99.0%)
make dom-conformance    # DOM conformance, full corpus: 678/678 (100.0%)
make wpt                # 9567/9824 subtests pass in 190 files
make render-conformance # Canvas toDataURL: 45016/45016 checks agree
make check-docs         # check_docs: 0 failure(s)
make check-numbers      # check_numbers: 0 failure(s)
```

The new suite aborts on `2a86d30` at its first assertion and passes here;
`main`'s own floors are unchanged by it, and no corpus case moved.
