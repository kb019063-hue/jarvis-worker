# Phase 224 — `import` is a call where a parenthesis follows

Task row: **`import(...)` does not parse** (`AGENTS.md`, from the holdout-01
re-run). Finding 228.

## 1. What was wrong

`import` was treated as a module-only keyword everywhere. The statement path
failed on the token with *import and export are only valid in modules*, and
`parse_primary` had no case for it at all — so an expression like

```js
const spec = await import('./specLinks.mjs');
```

was a `SyntaxError` before a line of the program ran. That is what APG's
`shared/js/app.js` line 52 does, and what the row recorded: the page's script
died on the parse.

The language has had Import Calls since ES2020. ECMA-262 §13.3.12 makes
`import(specifier)` an expression, and §13.3 leaves `new import(x)` out of
`NewExpression` — but only the unparenthesised form.

## 2. What changed

- `include/litebird/js/JsParser.hpp`: `NodeKind::ImportCall` and an
  `ImportCallExpr` holding the specifier.
- `src/js/JsParser.cpp`: the statement path looks one token ahead and lets
  `import` through when it is followed by `(`, which is the only production
  that is not a module's; `parse_primary` parses the argument list, and
  `parse_new` refuses `new import(x)` where the call was not parenthesised.
  `import.meta`, `import x from …` and a bare `import` are refused exactly as
  before, with the same messages.
- `src/js/JsEval.cpp`: evaluating the node runs `ToString` on the specifier at
  the call site (§13.3.12 step 6, so a `Symbol` specifier throws rather than
  rejecting) and answers a **rejected promise** whose `TypeError` names the
  specifier: this engine has no module loader. `onerror`-style reporting of
  the rejection is the engine's existing unhandled-rejection path, unchanged.

What the parser refuses, the evaluator never sees; what it lets through always
answers a promise, so `await import('…')` now reaches the page's own `catch`
instead of killing the script.

## 3. How it was verified

Every expectation below is Node v22.22.3's, taken before the code was written:

| Form | Node | This engine |
| :--- | :--- | :--- |
| `import('./a.mjs')` | a `Promise` | a `Promise` |
| `import\n('./b.mjs')` | parses | parses |
| `import.meta` | `SyntaxError` | `SyntaxError` |
| `import x from './a.js'` | `SyntaxError` | `SyntaxError` |
| `typeof import` | `SyntaxError` | `SyntaxError` |
| `import()` | `SyntaxError` | `SyntaxError` |
| `import(...[])` | `SyntaxError` | `SyntaxError` |
| `new import('./c.mjs')` | `SyntaxError` (*Cannot use new with import*) | `SyntaxError` |
| `new (import('./c.mjs'))` | `TypeError` at run time | `TypeError` at run time |
| `import(Symbol())` | `TypeError` thrown at the call | `TypeError` thrown at the call |
| `import({toString(){…}})` | coerced once | coerced once |

```bash
make test      # All 363 engine test suites passed successfully!
```

`import_calls_parse_and_reject_without_a_loader` asserts all of it in a page
whose base URL is `https://example.test/app/page.html`; the rejection's message
is read in a second block after `run_microtasks()`, because a handler is a
microtask and checking it in the same block only ever sees "(pending)".

## 4. What is still open

- **Nothing is loaded.** There is no module loader behind the call: an
  `import()` always rejects, so a page that depends on the imported bindings
  still has nothing to use. The row asked for the parse, and this is the parse
  — the loader is the ES-module row's work, not this one's.
- **A second argument is a `SyntaxError` here** where Node answers a rejection
  with *The second argument to import() must be an object*; the second argument
  is an options bag (`with` attributes) this engine has no meaning for.
- **`import.meta`** stays refused, as the module row says.
