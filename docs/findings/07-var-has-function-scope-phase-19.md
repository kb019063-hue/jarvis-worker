# 7. ✅ `var` has function scope (Phase 19)

> Moved out of `AUDIT.md` — written there as a `## 7. ✅ `var` has function scope (Phase 19)` section.
> The text below is the section as it stood, unchanged.


`var` declarations are hoisted to the nearest function or script scope,
including declarations nested in blocks and `for`/`for...of` headers. The
interpreter predeclares them as `undefined`; assignment during execution
updates the function-scoped binding. `let` and `const` remain block-scoped.
Regression coverage includes block escape, function isolation, use before
initialization (`typeof`), and loop-header declarations. Script-level `var`
bindings are synchronized with the VM global table for interoperability with
the existing bytecode execution path.

---
