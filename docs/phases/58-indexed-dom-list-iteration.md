## Phase 58 Architecture: Array-Like Iteration Boundaries and Live DOM Lists

PR #77 made Web IDL indexed collections share the Array prototype's iterator
functions by identity. Its generic `keys`/`values`/`entries` read the original
receiver's `length` and indexed properties on each `next()`, and `forEach`
passes that receiver to the callback. That change retired `nodelist-iterator`
and ten other listed WPT failures, but the WPT floor remained at 9538.

**What remained wrong.** `BuiltinsArray.cpp` converted an arbitrary array-like
`length` directly from `double` to `size_t`. `Infinity` and numbers above the
integer range have undefined C++ conversion, rather than ECMAScript's
`ToLength` clamp at 2^53 − 1. An iterator over `{length: Infinity}` could
finish before producing index zero. `forEach` tested whether its callback was
callable before getting `length`, reversing the spec's observable order when
that property is a getter.

**How it works now.** The shared `iteration_length()` converts a length with
`ToIntegerOrInfinity`, maps negatives and `NaN` to zero, and clamps positive
values (including infinity) to `9007199254740991` before conversion to
`size_t`. Array iterator `next()` still calls it on every step to see live
length changes; `forEach` calls it once, before `IsCallable`, so appended
indices are not visited but existing values are looked up when reached. No
other Array method is changed. The new unit suite checks NodeList / DOMTokenList
function identities, list shrink/grow while iterating, `forEach`'s `this`,
third argument, and length snapshot, sparse borrowed array-likes, `Infinity`,
very large lengths, and getter/callback ordering. The independent out-of-corpus
`compare_with_jsdom.py` list probe matched jsdom 31.0.0 on **12/12** lines.

**Verification:** `make test` **218/218** suites plus 114 cross-origin/channel checks;
`make wpt` **9550/9744** subtests across 190 files (**194 listed**, **0
unlisted**), with `WPT_FLOOR` raised from 9538 to **9550**. The old 9538 floor
was not sufficient to catch losing PR #77's eleven passing subtests. More
specific verification and what is still open are in finding 64.
