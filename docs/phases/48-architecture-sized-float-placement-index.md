# Phase 48 Architecture: Sized-Float Placement Index

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 48 Architecture: Sized-Float Placement Index` section.
> The text below is the section as it stood, unchanged.


`LayoutBox::layout_block` builds a per-block `FloatPlacementIndex` lazily when
the first float needs placement. Ordered edges partition vertical space into
intervals of constant maximum left/right intrusion. A randomized balanced
treap keyed by each interval's top caches the maximum remaining width in its
subtree, so placement checks the interval containing `y_cursor`, then finds
the earliest later edge with sufficient capacity without sweeping earlier
rows. Inserting a float splits the partition at its top and bottom and
updates only intersecting intervals, with logarithmic index maintenance per
touched interval. A float whose width never fits retains the old y0 overflow
behavior. Negative intrusions are retained rather than clamped to zero.

The index is not the source of `float_bands`: inline layout still receives
the original bands, and `clear` and containing-height accounting still use
the existing side-specific lowest bottoms, including zero-height floats.
The partition can still cost O(n²) when n new bands span n distinct prior
intervals; it removes the repeated global sort and sweep for the common
packed-row case, not all possible overlapping-float complexity.

Reproduce the before/after comparison by saving a pre-change `build/litebird`
as `build/litebird-floats-before`, then running:

```sh
python3 tools/bench/bench_sized_floats.py build/litebird-floats-before --out build/floats-before --sizes 1000 3000 10000
python3 tools/bench/bench_sized_floats.py build/litebird --out build/floats-after --sizes 1000 3000 10000
for f in build/floats-before/*.layout; do cmp "$f" "build/floats-after/${f##*/}"; done
```

The fixed-seed generator makes three independently generated patterns at each
size: packed rows, mixed left/right widths/heights with negative horizontal
margins, and mixed floats with clearance and negative top margins. On this
branch all nine full layout dumps were byte-identical. End-to-end CLI times
(seconds; one local machine, no speedup claim for unrelated workloads):

| Floats | Packed rows before → after | Mixed before → after | Clear before → after |
| :--- | ---: | ---: | ---: |
| 1,000 | 0.076 → 0.013 | 0.129 → 0.016 | 0.037 → 0.015 |
| 3,000 | 0.494 → 0.031 | 1.134 → 0.039 | 0.261 → 0.037 |
| 10,000 | 5.244 → 0.096 | 12.505 → 0.125 | 2.621 → 0.120 |

`make bench-floats` runs the 1k and 10k patterns under a generous
four-second-per-page CI wall-time guard (the old 10k row page exceeds it);
timing is not used as a pixel oracle.

The CSS parser and tree building are included in these times. The baseline
was the already-correct band-edge sweep, **not** the old 46-second capped
slide that let floats escape. Unit tests cover mixed sides, `clear:left` and
`clear:right`, negative margins including a negative intrusion, and the
existing rows beyond 1,024 and zero-height band behavior.
