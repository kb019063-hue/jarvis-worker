# 8. ✅ No recursion guard in the JS engine

> Moved out of `AUDIT.md` — written there as a `## 8. ✅ No recursion guard in the JS engine` section.
> The text below is the section as it stood, unchanged.


> **Fixed.** Guard lives in `eval_expr` (guarding only call sites was not
> enough — the recursion is per operator level). Runaway recursion now reports
> `RangeError: Maximum call stack size exceeded` and the interpreter survives.
>
> Like finding 5, the guard is a stack-headroom check rather than a fixed call
> count, because one JS call costs ~1 KB of native stack in an `-O2` build but
> ~55 KB under AddressSanitizer. Measured: the guard trips at depth 133 under
> ASan and around 7,000 uninstrumented, both with the same 1 MB reserve intact.
> A fixed counter tuned for one build is either too shallow or unsafe in the
> other.
>
> One wrinkle found while testing: `JsInterpreter::eval()` runs the bytecode VM
> first and falls through to the AST interpreter on *any* exception, so a
> VM-level `RangeError` silently re-runs the whole script in the AST engine
> before reporting. Harmless here (both bounded), but it is surprising behaviour
> in a fallback path and is noted under finding 6.


There is no call-depth counter and no `RangeError` path. The VM has an explicit
`m_stack` (so bytecode frames are heap-allocated and bounded), but the
**AST interpreter** (`JsInterpreter::eval_expr`) recurses on the C++ stack for
nested expressions and calls. A deeply nested expression or unbounded JS
recursion is a straightforward stack-overflow crash.

---
