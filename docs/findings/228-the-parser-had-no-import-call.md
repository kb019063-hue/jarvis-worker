# Finding 228 — the parser had no import call

**Resolved by Phase 224** (`docs/phases/224-import-is-a-call-where-a-paren-follows.md`).

## 1. What was found

`import(...)` was a parse error in every position. `JsParser` handled `import`
in two places and both refused it: the statement path with *import and export
are only valid in modules*, and `parse_primary` by not having a case for the
token. APG's `shared/js/app.js` awaits `import('./specLinks.mjs')` on line 52,
so that page's script never ran — the holdout recorded the `SyntaxError` and
the row was written from it.

The language has made `import(...)` an expression since ES2020 (§13.3.12), and
`import` is a keyword only where a `(` or `.` follows it in expression position.

## 2. What was done

A look-ahead in the statement path lets `import` through when `(` follows, and
`parse_primary` parses the argument as an assignment expression into a new
`ImportCallExpr`. `parse_new` refuses `new import(x)`, which §13.3 keeps out of
`NewExpression` — and which Node reports as *Cannot use new with import* — while
`new (import(x))` parses, as it does in Node, and is a `TypeError` at run time.

Evaluating it coerces the specifier with `ToString` at the call site (so a
`Symbol` throws, which is §13.3.12 step 6) and answers a rejected promise whose
`TypeError` names what was asked for. There is no loader; the rejection is the
honest answer, and it is the answer a page's `catch` can now see.

## 3. Evidence

`make test` **363/363**. `import_calls_parse_and_reject_without_a_loader`
holds twelve grammar and coercion answers to Node v22.22.3's — including
`import.meta`, a module `import x from`, a bare `typeof import`, both `new`
forms and a `Symbol` specifier — and three about the rejection, read after the
microtask queue has run.

## 4. What is still open

No module loader: the call always rejects, so an imported binding is never
there. A second argument is a `SyntaxError` here and a rejection in Node. The
ES-module row owns both.
