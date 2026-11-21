# Phase 122 — the oracle reaches layout

Task row: **Pixels are measured since Phase 120**, extended. Finding 127.

Phase 120 measured ten pages of the box model and wrote down what it had not
measured: *"flexbox, grid, floats, `transform`, SVG, canvas, border radius and
images are unmeasured"*. This phase takes the first six of those and finds out
whether they are right. Three of them were not, in four different ways, and two
of the tree's own descriptions of itself turned out to be out of date in the
process.

Nothing in the engine changed.

## 1. What was wrong

An unmeasured layout mode is not a known-good one, and there is no other
scoreboard that could have caught these: HTML tree construction, the DOM
corpus, WPT's script tests, the XML suite, URL and JavaScript all read a
*textual* answer. A float painted under the block that follows it prints
nothing, throws nothing, and returns the correct node tree.

Sixteen pages later:

| Statement | Was it true? |
| :--- | :--- |
| A float paints above the background of the block beside it (Appendix E step 5) | **no** — `(10,10)` was the block's blue |
| A transformed element paints in the positioned layer (Transforms 1 §3) | **no** — a later sibling covered it at `(20,45)` |
| `align-items` positions items on the cross axis, and `stretch` is the default | **no** — ignored; an item with no height of its own is 0 tall |
| `overflow: hidden` clips to the rounded corner of its box | **no** — the clip is a square rectangle |
| `justify-content`, `flex-direction`, `flex-wrap`, grid tracks and `gap`, `clear`, absolute positioning, `border-radius`, image scaling, `transform`'s effect on paint vs layout | yes — and are now asserted |

Finding 127 has the pages and the pixels.

### 1.1 Two documents that described the engine wrongly

`ARCHITECTURE.md` listed "rounded-corner clipping" among what is still missing
after Phase 36. That is *almost* right and the near-miss matters: a rounded
**background and border** is implemented and anti-aliased — the first six
reftest attempts at it failed because the corner pixels are blended, not white,
and the assertion had to become `not-colour` — while the **clip** is square.
The line now says which half is missing.

`ROADMAP.md` said the render row was *"30 assertions"* and that flexbox, grid,
floats, `transform`, border radius and images were unmeasured. Both numbers are
now stale and corrected in place.

And one line that had nothing to do with this phase: `ROADMAP.md` claimed
**"an HTTP cache has none"** — no oracle. That has been false since Phase 73,
whose suite `http_cache_rfc9111_freshness_etag_304_vary_and_invalidation`
counts requests on a loopback server this repository starts, so every byte of
the exchange is the engine's. Corrected, because a survey that takes that line
at its word goes and builds a second cache oracle.

## 2. What was added

`tests/render/reftests.json`: **26 pages, 100 assertions** (was 10 and 30).
Each page is one small HTML document and a list of statements about its pixels,
in the four kinds the harness had (`pixel`, `rect`, `uniform`, `not-colour`,
`differs`).

- **flex, main axis** — `justify-content: space-between` (three items, edges
  and both gaps asserted), `center`, `flex-end`, `flex-direction: column`,
  `flex-wrap: wrap` with a third item on the second line.
- **flex and grid, cross axis** — item stretch, `align-items: center`, grid
  item stretch. Three `known_failure` pages: the oracle's way of saying *this
  is what the spec says and this is not what happens*.
- **grid** — `grid-template-columns: 60px 40px`, `grid-template-rows: 30px 20px`,
  `gap: 10px`, all four cells placed and the gaps asserted as unpainted. This
  one passes: the track arithmetic is correct.
- **floats** — a left float beside a block, and `clear: both` below it. Known
  failure, on the paint order only.
- **positioning** — an `absolute` child placed against a `relative` parent with
  all four edges asserted from outside.
- **clipping** — `overflow: hidden` with a child far larger than the parent,
  and a second page for the rounded clip (known failure).
- **images** — a 1×1 PNG in a data URI scaled to 40×40, painted edge to edge.
- **`border-radius`** — a 12 px radius, corners asserted *not* to be the box's
  colour (they are anti-aliased), the middle and both straight edges asserted
  to be exactly it.
- **`transform`** — the ink moves by `translate(20px, 10px)` and the next block
  stays at `y=40`, which is the half that works; where the moved ink overlaps
  the next block the paint order is wrong, and that is the known failure.

Two mechanisms were added to the harness:

**A `known_failure` ledger.** A page may carry a `known_failure` string. The run
prints `KNOWN <name>` and the reason, does not count it as a pass, and does not
fail. `Render reftests: 20/26 pages match their reference pixels; 6 more fail
as recorded, with a reason in the corpus (26 of 26 accounted for)`.

The other direction is what makes the ledger honest: a `known_failure` on a
page that **passes** prints `FIXED`, says the entry must come out, and exits 1.
A ledger that only excuses is a place bugs go to be forgotten. Both directions
are self-tested (`self_test()` case 4), and the failure direction was
demonstrated on a scratch corpus before being trusted:

```
$ python3 tools/conformance/render_check.py --suite /tmp/ledger_probe.json
FIXED flex_column_stacks_items_downward
       it passes now -- take the known_failure entry out of tests/render/reftests.json and let it count
$ echo $?
1
```

**Nothing else.** No assertion kind, no new reader, no engine change, no new
Makefile target (`make render-conformance` already runs the suite) and no CI
change (the `render-conformance` job already runs it).

## 3. Numbers

```
$ python3 tools/conformance/render_check.py --binary build/litebird
... 20 ok, 6 KNOWN, 0 FAIL ...
Render reftests: 20/26 pages match their reference pixels; 6 more fail as
recorded, with a reason in the corpus (26 of 26 accounted for)
```

An honest accounting of the first run, because it says more about the oracle
than the final one does. The first run of the new pages was **16 of 22**, and
of the six reds **three were my own arithmetic**, not the engine's:

1. a `pixel` assertion at `(20,0)` expected white beside a 12 px radius — that
   point is *inside* the shape;
2. a `rect` assertion over a transformed box's whole layout rectangle, which
   the following block legitimately overlaps — "contains blue" was correct;
3. `pixel(10,55, WHITE)` on the float page, contradicting a `rect(0,50,100,10)`
   green in the same list.

Each was re-derived from the spec before the engine was blamed. The lesson is
the reason the harness demands `describe` on every assertion: *"CSS 2.1
Appendix E paints floats above in-flow block backgrounds"* can be checked
against a document, and *"the float is at y=50"* cannot.

## 4. What is still open

- The three engine bugs of finding 127 — paint order, cross-axis alignment and
  the square clip — are **rows of `AGENTS.md`**, not fixed here. Phase 120's
  rule was to extend a row rather than take it; the same rule says a bug found
  outside a task becomes a row and is not fixed in passing.
- Inline formatting and line breaking: no reftests. Text is asserted only where
  a line box starts.
- Table cell backgrounds and the collapsed border model: unmeasured.
- SVG and canvas: unmeasured. Both have their own text-based oracles
  (Phase 79's compositor comparison), so this is the least urgent gap.
- Glyph *shape* is still not compared, only where the ink lands.
- **No WPT reftests are vendored**: `find tests/wpt -name '*-ref*'` is 0.
  Borrowing a handful would test hypotheses nobody here thought to write.
- There is no two-build pixel diff: a change is compared with an assertion, not
  with what the previous binary painted.
