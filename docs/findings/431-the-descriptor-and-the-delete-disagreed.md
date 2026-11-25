# Finding 431 — the descriptor and the delete disagreed

Phase 431. Status: **fixed**, except for one call site named below.

## 1. What was wrong

The global object gave two answers to one question:

```
Object.getOwnPropertyDescriptor(globalThis, "Object").configurable   // true
delete globalThis.Object                                            // false
```

`[[GetOwnProperty]]` said the property could go; `[[Delete]]` refused. A
program that believed either one was wrong about the other, and both had been
in the tree together since the global-object hooks were written.

The cause was two separate misreadings of §9.1.1.4, which makes exactly two
things non-configurable:

1. `undefined`, `NaN` and `Infinity` (§18.1.1-§18.1.3);
2. a `var` or function declaration **a script** makes at the top level.

Everything §17-§26 defines — `Object`, `Math`, `JSON`, `RangeError`,
`parseInt` — is `{writable: true, enumerable: false, configurable: true}`. The
Note under §18.1 says so and names `Array` and `RangeError` in the same
sentence.

This interpreter had marked every binding it created before any script ran as
non-configurable, so `[[Delete]]` refused all of them — the opposite of the
rule, which makes the *script's* declaration the non-configurable one.

And the descriptor derived `configurable` from `!is_readonly`. For
`undefined` — readable, so "configurable" false — that was right. For every
function — readable, so "configurable" true — it was wrong. It was right by
coincidence for one name out of three, and the coincidence is the reason
nothing failed: the attribute was not being computed, it was being guessed from
a different one.

## 2. What changed

`JsEnvironment` now distinguishes the four kinds of global binding the
standard does:

| binding | configurable | recognised by |
|---|---|---|
| `undefined`, `NaN`, `Infinity` | no | `is_permanent` |
| a top-level `var`/function | no | `is_permanent`, set in `hoist_var` |
| a library global | **yes** | `is_builtin` |
| a sloppy implicit global | **yes** | neither |

`is_configurable_binding` is that table in one function. The descriptor asks it
instead of guessing, `[[Delete]]` asks it instead of refusing a whole class,
and when the answer is yes the delete calls `remove_own`.

That last part matters as much as the boolean. Node removes the binding: after
`delete globalThis.Object`, the bare name is a `ReferenceError`, not
`undefined`. A fix that only changed the answer would have made the object
report a property gone while the scope still resolved it — trading one
disagreement for a worse one. The third red proof below is exactly that
mistake, made on purpose.

`hoist_var` is where a declaration is told from an assignment, because it is
the only place that knows: both otherwise arrive at `define` with no way to
say which they were.

## 3. The mistake worth remembering

The Phase 404 suite asserted both halves of the contradiction. Twenty lines
apart, in the same file:

```
ok('Object configurable', d.configurable, true);      // line ~58
...
ok('delete Object answers false', delete globalThis.Object, false);   // line ~82
```

Its header says "Every expectation here was measured against Node v22.22.3
first", and Node answers `true` to both. So one of the two was measured and
the other was written from what the engine did — and because the file's
comment said the whole set was measured, the unmeasured line inherited that
credibility. This is the second phase in a row where a *passing test* was the
thing keeping a wrong belief alive (Phase 414 found a comment and a test
agreeing with each other, both wrong). A test that asserts two things that
cannot both be true is a red flag in the shape of a green tick.

## 4. What remains wrong

* **`zz = 3` (sloppy implicit global) reports `configurable: false`.** It
  should be `{writable: true, enumerable: true, configurable: true}`. The
  name is created at `src/js/JsEval.cpp:2439` with the same `define` a
  built-in uses, so it inherits `is_builtin` from the environment it lands in.
  `JsEnvironment::define_script_global` exists for it and needs one call site
  changed; `src/js/JsEval.cpp` is claimed by the open #421, so the call site
  is left to whoever lands it. The assertion is not written into the suite,
  because a test that fails in the tree tests nothing.
* **`eval("var x = 1")` inside a function** marks the binding deletable
  (`JsEval.cpp:769`, `:790`) — correct for a global, invisible for a local.
  Unmeasured either way.
