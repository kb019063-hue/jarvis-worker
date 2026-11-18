# 3. ✅ Assignment operators were missing from the grammar

> Moved out of `AUDIT.md` — written there as a `## 3. ✅ Assignment operators were missing from the grammar` section.
> The text below is the section as it stood, unchanged.


> **Fixed in `18be77f`.** All five compound forms plus `++`/`--` now work on both
> the bytecode VM and the AST interpreter, with a regression test that asserts
> the two paths agree. Details below are the original finding.


```js
var s = 0; s += 1;   // JS Runtime Error: Unexpected token in JS expression: =
var s = 0; s++;      // JS Runtime Error: Unexpected token in JS expression: +
var s = 5; s -= 2;   // same
var s = 5; s *= 2;   // same
```

`JsTokenType` has `Plus`, `Minus`, `Star`… but no `PlusEqual`, `MinusEqual`,
`PlusPlus`, `MinusMinus`. The lexer has no branch that would produce them, so
the parser sees `Plus` followed by `Equal` and fails.

**This was the single biggest obstacle to running real scripts.** `+=` and `++`
are in essentially every nontrivial script ever written; the engine's own
example pages hand-unroll them (`seconds = seconds + 1`) to work around it.
Fixing it means: 6 token types, a lexer branch each, a `parse_assignment()`
level in the parser, and an interpreter case that reads-modifies-writes the
lvalue. Roughly a day's work, and it unlocks more real-world compatibility than
any other single item on this list.

Phase 19 adds `typeof`, `switch`, right-associative `**`, and short-circuit `??`
to the parser and AST interpreter. These constructs use the interpreter fallback
when bytecode compilation does not support them. `"a".repeat(3)` remains outside
this phase and still returns `undefined`.

---
