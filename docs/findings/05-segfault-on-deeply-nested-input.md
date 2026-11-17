# 5. ✅ Segfault on deeply nested input

> Moved out of `AUDIT.md` — written there as a `## 5. ✅ Segfault on deeply nested input` section.
> The text below is the section as it stood, unchanged.


> **Fixed.** The recursion was in `LayoutTree::build_box`, not the parser — see
> below. The per-frame cost was cut by moving the 920-byte style off the stack,
> and the recursion is now bounded by the *remaining native stack* rather than a
> fixed depth. Depth 50,000 parses and renders.
>
> A fixed depth constant was tried first, and the sanitizer CI job rejected it:
> a `build_box` level costs ~2.8 KB in an `-O2` build but ~34 KB under
> AddressSanitizer, so a nominal 512 levels exhausted an 8 MB stack at level 243
> in the ASan job while leaving plenty of room in the normal one. No single
> constant is correct for both. `include/litebird/common/StackLimit.hpp` instead
> compares the current stack pointer against the thread's stack bounds and stops
> when one more level would eat into a 1 MB reserve — the approach V8's
> `StackGuard` and SpiderMonkey's `CheckRecursionLimit` take. It adapts to build
> flags, platform and thread automatically.
>
> Consequence worth knowing: the *reachable* depth now depends on the build.
> Normal builds get thousands of levels; an ASan build gets ~1,000 for layout and
> ~130 for JS, because its frames are ~20-55x larger. That is a measurement of
> the sanitizer, not a regression.
>
> The limit on nesting in the parser that the text below asks for arrived in
> Phase 25 (finding 24). The HTML standard has no such rule; Blink and WebKit
> do, and what goes past the limit is attached as a sibling, not turned into
> text.


```
depth 1,000: ok
depth 3,000: SEGFAULT
depth 5,000: SEGFAULT
```

Deterministic, reproducible, ~9 KB of input. Nothing in the codebase tracks
recursion depth: the tree builder, `LayoutTree::build_box`, `Node::dump`,
`text_content()` and the JS expression evaluator are all unguarded recursion.
The crash happens *during parsing* (zero bytes reach stdout).

Browsers cap this at ~512 nesting depth for exactly this reason. The cheapest
honest fix is a depth counter in `HtmlTreeBuilder` that flushes the token as
text past the limit, which is what the HTML spec already tells you to do.

---
