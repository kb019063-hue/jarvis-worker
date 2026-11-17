## 64. ✅ Indexed DOM list methods did not share Array identities or live semantics (Phase 58)

**The hole.** `tests/wpt/dom/nodes/Node-childNodes.html` compared all five
iteration methods with `Array.prototype` by identity. The Web IDL binding
created separate functions. Borrowing the existing Array versions was not a
fix: non-array receivers were copied at method entry, so changes to a live
list after calling `values()` would not be visible to `next()`. Also the
Array `values` and `Symbol.iterator` properties held distinct functions.

**The fix.** NodeList and DOMTokenList install their own realm's *same*
Array prototype function values. Array iterators reread length and index on
each step and keep the receiver reachable by GC; `forEach` reads the initial
length once but rereads each property, skips absent indices and supplies the
original receiver to the callback. Other Array methods and non-iterable
HTMLCollection / NamedNodeMap methods are deliberately unchanged.

**Verification.** `make test` **207/207** including the new mutation,
identity, and array-like regression suite; out-of-corpus
`compare_with_jsdom.py` iterator/list probe **12/12** lines match jsdom
31.0.0; `make wpt` **9539/9744** across 190 files, **205 listed**, **0
unlisted** (before: 9538/9744, 206 listed).

**Still open.** `strict-write` (2 WPT cases) needs the exotic `[[Set]]`
refusal path; other Array methods' `receiver()` still snapshots non-arrays.
