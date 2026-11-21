# Phase 138 — an inferred name is a name, not a binding

Task: **an anonymous class expression takes the name it is bound to**
(`ClassExpression : class ClassTail`, §8.6.3 NamedEvaluation). Finding 143.

## 1. Where the number came from

`make test262` on this tree: **14,131 / 15,686 (90.1%)**, 1,555 failures.
Grouped by what the runner said rather than by file, the second-largest group
was 121 cases of one string:

```
Expected SameValue(«""», «"cls"») to be true
```

Every one of them involved an anonymous class expression used as an
initialiser. The engine was not failing to *evaluate* anything — the class was
built and worked — it was failing to notice that the language had given it a
name.

## 2. What was wrong

`evaluate_named` exists for exactly this. It takes an expression and the name
of the binding it is being evaluated for, and names an anonymous function after
it; that is why `var f = function () {}` has `f.name === "f"`. Its Class arm
built the class and threw the hint away:

```cpp
} else if (expr->kind == NodeKind::Class) {
    auto* cls_expr = static_cast<const ClassExpr*>(expr);
    if (cls_expr->cls->name.empty()) {
        JsValue ctor = define_class(cls_expr->cls, env);
        return ctor;                       // name_hint, unread
    }
}
```

So a function was named in five positions and a class in none:

| source | Node.js 22 | this engine, before |
| :--- | :--- | :--- |
| `var a = class {}` | `"a"` | `""` |
| `var o = { p: class {} }` | `"p"` | `""` |
| `var { q = class {} } = {}` | `"q"` | `""` |
| `var [ w = class {} ] = []` | `"w"` | `""` |
| `function f(x = class {})` | `"x"` | `""` |

The fix is the mirror of the arm above it: pass the hint to `define_class` and
let it reach `make_function`, which already knows that `fd.name` is
`node->name.empty() ? name_hint : node->name`. Positions where nothing names
the class — a constructor argument, a `return`, an element of an array literal
— pass no hint and stay `""`, as they should.

## 3. The half that was wrong, and why it was invisible

The first version also installed the name as a *binding* inside the class
environment, so that `var a = class { static f() { return a; } }` would answer
the class. That reads well and is wrong. An inferred name is a **name**, not a
binding: a class that *spells* a name gets a scope for it inside its own body,
and one whose name is merely inferred does not, so its body keeps reading
whatever that binding means where the class was written.

The difference is invisible in `name`, and it broke two cases the JS corpus
already had:

```
for (var i = 0; i < 20000; i++) C = class extends C {};
```

must keep extending the **previous** `C`. The heritage expression is evaluated
before the class environment is used, so that was never at risk — but
`static get who() { return this === C; }` answered `false` for every class in
the chain, because `C` inside the body had become a copy of the class instead
of the outer `var`. `make js-conformance` went from **1143 to 1142** and named
`a chain of 20,000 classes: static lookup, the walk up, and letting go of it`.

Two things about how that was found are worth keeping:

- The comparison that caught it was the *scoreboard*, not the 121 test262 cases
  that were the point of the phase. Those all passed with the wrong version.
- The first reading of the failure was "pre-existing, not mine", because the
  case is listed among 16 known failures. It was not: the pre-change binary
  passes it. Checking that the case failed *before* the change was the step
  that turned a scoreboard number into a bug report — and the honest thing to
  write down is that the number alone did not identify it.

## 4. The scoreboard

Two binaries from the same tree, test262 at `2e58f87d`:

```
before     14131 pass (90.1%)  [fail 1555]
now        14249 pass (90.8%)  [fail 1437]
118 pass in now and not in before; 0 pass in before and not in now
```

The empty-name group went from 121 to 3 (the three left are `Symbol.prototype
.description`, a different question). Everything else is unchanged:

- `make test` → **297 / 297**, including the suite added here.
- `make js-conformance` → **1143 / 1158 (98.7%)**, `make dom-conformance` →
  **678 / 678**.

## 5. What this exposed, and did not fix

The 118 cases unblocked 43 more that were sitting behind them: with a name
finally present, the suite's `verifyProperty` can inspect it, and 43 cases now
fail with `name descriptor should not be writable`. That is a different bug in
a different file — a function's `name` and `length` are *virtual* in this
engine (answered out of `FunctionData`, absent from its own property table), so
`Object.getOwnPropertyNames(function (a) {})` is `""`, `f.name = 'x'` writes
through where the language says the property is non-writable, and
`delete f.name` leaves `"g"` where the language says it removes it. It is new
row of `AGENTS.md`, not a change made in passing.
