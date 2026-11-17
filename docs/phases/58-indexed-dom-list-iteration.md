## Phase 58 Architecture: Indexed DOM Lists Share the Array Iteration Functions

The `Node.childNodes` WPT already exercised iteration, but its final assertions
compared **function identity** with `Array.prototype`. The DOM installed
separate lookalike methods, and the array implementation copied non-array
receivers on entry, so assigning the built-ins directly would have broken
liveness. `Array.prototype.values` and `@@iterator` were also two functions.

`BuiltinsArray.cpp` now exposes the same `values` function at both keys and
implements `keys`/`values`/`entries` by keeping their original receiver as a
GC root. Each `next()` reads its current length and indexed property anew.
`forEach` takes the initial length once, skips missing indices, reads each
present index at callback time, and passes the original collection as its
third callback argument. `DomCollectionBindings.cpp` installs those exact
realm-local functions on Web IDL indexed-value iterables (`NodeList` and
`DOMTokenList`), leaving HTMLCollection and NamedNodeMap's separate
iterator-only definitions untouched. The new regression suite exercises
identity, mutations between `next()` calls, `DOMTokenList` updates,
`forEach`'s this/third argument and length boundary, and borrowed methods on
ordinary array-like objects including holes. An independent out-of-corpus
`compare_with_jsdom.py` probe (identity, live NodeList/token iteration, and
`forEach` boundaries) matched jsdom 31.0.0 on **12/12** output lines.

**Verification:** `make test` **207/207**; `make wpt` **9539/9744** across
190 files (205 listed failures, 0 unlisted), up from **9538/9744** (206
listed). Remaining indexed-collection WPT failures are `strict-write` (2),
which needs the exotic property-write refusal path, not iteration. Other
array methods still use `receiver()`'s snapshot of non-arrays; this phase
does not claim to make every Array method generic.
