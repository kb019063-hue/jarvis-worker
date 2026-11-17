## 64. ✅ Array-Like Iteration Lost `ToLength` and `forEach` Getter Order (Phase 58)

**The hole.** PR #77 fixed the `Node.childNodes` WPT function-identity failure
and gave indexed lists the actual generic Array iterator methods. It directly
cast `js_to_integer(length)` to `size_t`, however: infinity and oversized
lengths can leave an iterator finished instead of producing index zero, and
out-of-range floating-to-integer conversion is undefined in C++. `forEach`
checked the callback before evaluating the receiver's `length` getter,
where ECMA-262 evaluates length first. No test outside the WPT corpus pinned
liveness as the original list changes between `next()` calls. PR #77 also
passed **9550/9744** WPT subtests but left the minimum floor at **9538**.

**The fix.** A shared `iteration_length()` clamps to 2^53 − 1 before casting;
`forEach` evaluates it before checking the callback. A new unit suite covers
identity, DOM list mutation, ordinary sparse/large array-likes, and getter
order. The WPT floor rises to 9550, without adding any expected failure.

**Verification.** `make test` **210/210** plus 80 cross-origin checks; `make
wpt` **9550/9744**, 194 listed, 0 unlisted (unchanged WPT score, stronger
floor). Out-of-corpus `compare_with_jsdom.py`: **12/12** lines match jsdom
31.0.0. The `Infinity`/getter regression is in `tests/test_suite.cpp`.

**Still open.** Other Array methods use `receiver()`'s snapshot of non-array
objects; this change does not claim to make all Array methods generic. The
remaining 194 WPT failures are classified in `tests/wpt/README.md`.
