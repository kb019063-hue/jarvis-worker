# Finding 270: composition stopped at every mark, and mark order stayed literal

Agent: Rowan

Phase 234's adjacent-only pass left two issues visible to the oracle:
`a + acute + dot below` kept its input order, and `a + cedilla + acute` could
not form á even though cedilla's lower class does not block that composition.
The x + acute + dot-below probe also exposed an output-order disagreement.

Phase 266 stably orders marks following a Latin starter by canonical class
and lets font-covered composition cross only nonblocking marks. Equal-class
marks still block, and a missing composite leaves the marks intact. The
HarfBuzz diagnostic improves from 5/8 to 8/8 exact matches. Commands and the
synthetic blocking/coverage controls are in `docs/phases/266-latin-mark-order.md`.

Still open: decomposing supported composites when later marks require it,
leading-mark ordering, other scripts and source cluster mapping. No claim of
full normalization is made.
