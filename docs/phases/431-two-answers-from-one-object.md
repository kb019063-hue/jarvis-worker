# Phase 431 — two answers from one object

Finding 431. Found by running test262 against this engine and reading the
failures that were *not* `Proxy` or `BigInt`, then following one of them into
a place the harness trips over rather than names.

## 1. What was wrong

The global object answered two questions about the same property and gave
contradictory answers:

```
Object.getOwnPropertyDescriptor(globalThis, "Object").configurable   // true
delete globalThis.Object                                            // false
```

§9.1.1.4 makes two things non-configurable: the three permanent values
(§18.1.1-§18.1.3) and a `var` or function declaration **a script** makes at the
top level. Everything §17-§26 defines is an ordinary data property with
`{writable: true, enumerable: false, configurable: true}` — the Note under
§18.1 says so in as many words, and names `Array` and `RangeError` together.
This interpreter had it exactly backwards:

* `mark_all_builtin()` marked every binding present at construction time, and
  `[[Delete]]` refused every name so marked. That is the opposite of
  §9.1.1.4: it is a script's `var` that cannot be deleted, not the library's.
* `[[GetOwnProperty]]` derived `configurable` from `!binding->is_readonly`.
  That was right for `undefined` — readable, therefore "configurable" false —
  and wrong for every function: readable, therefore "configurable" true. It
  was right by coincidence, for one of three names, and the coincidence is
  why nothing caught it.

The second one is the more interesting defect. `configurable` and `writable`
are separate questions, and one was being answered with the other.

Underneath both: `[[Delete]]` did not remove the *binding*, only decide what to
answer. Node removes it — after `delete globalThis.Object`, the bare name
`Object` is a `ReferenceError`, not `undefined`. So the fix has to take the
name out of the environment, or the object would report a property gone while
the scope still resolved it.

## 2. What changed

`JsEnvironment` learned the three-way distinction §9.1.1.4 actually draws:

| binding | configurable | how it is recognised |
|---|---|---|
| `undefined`, `NaN`, `Infinity` | no | `is_permanent`, marked by `define_readonly` |
| a top-level `var`/function | no | `is_permanent`, marked in `hoist_var` |
| a library global (`Object`, `Math`) | **yes** | `is_builtin` |
| a sloppy implicit global (`zz = 3`) | **yes** | neither |

`is_configurable_binding` is that table, in one place, asked by three callers —
the descriptor, `[[Delete]]`, and nothing else. `[[Delete]]` then calls
`remove_own` when the answer is yes, which is what makes the bare name stop
resolving.

`hoist_var` marks a declaration `is_permanent`. It is the only place that knows
a name came from a declaration rather than an assignment, since both otherwise
arrive at `define`.

The old suite for Phase 404 asserted both halves of the contradiction — its
`configurable: true` check and its `delete ... answers false` check, twenty
lines apart. Both were measured against Node, and one of them was wrong. It is
corrected in place, with the reasoning, rather than deleted.

## 3. How it was verified

17 assertions, every expected value Node v22.22.3's by evaluating the same
expression there.

```
$ ./build/test_suite
All 477 engine test suites passed successfully!
```

Red on three engines, each failing a different assertion:

| Wrong engine | Fails at | Says |
|---|---|---|
| as it was — descriptor and delete both wrong | `431-global-configurable.cpp:49` | `delete globalThis.Object` is `false` |
| descriptor fixed, delete still refusing (the plausible half-fix) | `:49` | same |
| `[[Delete]]` answers `true` without removing the binding | `:50` | `hasOwnProperty` still `true` |

The third is the one worth having: it is the shape the bug *already had* in
reverse, a boolean that says one thing while the state says another, and only
an assertion about `hasOwnProperty` and the bare name catches it.

Two of the three need the Phase 404 suite moved aside to be visible, because
404 sorts first and fails on the same defect — which is itself evidence that
both now guard it.

`412` assertions across the two suites, plus:

```
test262  15185 → 15196 pass (96.8% → 96.9%), 0 lost
js conformance 1153/1158 (unchanged)   everyday probe 90/90
check_docs, check_numbers, check_suites 0
```

## 4. What is still open

* **A sloppy implicit global gets the wrong `configurable`.** `zz = 3` with no
  declaration is `{writable: true, enumerable: true, configurable: false}`
  here and `{..., configurable: true}` in Node. The name is created at
  `src/js/JsEval.cpp:2439` and goes through the same `define` a built-in does,
  and `define` cannot tell the two apart. `JsEnvironment::define_script_global`
  is written and waiting for the one-line call site; that file is claimed by
  the open #421, so the caller is left to whoever lands it. The assertion that
  would hold it is deliberately not written — a suite in the tree that fails is
  not a test of anything.
* **`eval("var x = 1")` at a function's top level** marks its binding
  deletable, which for a *global* `var` made by `eval` is right and for a
  function-local one is invisible. Unmeasured; `JsEval.cpp:769` and `:790`.
* **test262's `$262` global is absent**, so 18 `built-ins/Symbol` cross-realm
  tests fail for a reason that has nothing to do with the language. That is a
  harness gap, not a conformance one, and it is the same in every directory
  that uses it.
