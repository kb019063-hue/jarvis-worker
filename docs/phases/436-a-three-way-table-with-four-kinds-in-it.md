# Phase 436 — a three-way table with four kinds in it

Finding 436. The remainder Phase 431 named and deliberately did not fix, now
that the file it needed is free.

## 1. What was wrong

```
$ node -e 'zz = 3; var d = Object.getOwnPropertyDescriptor(globalThis, "zz");
           console.log(d.writable, d.enumerable, d.configurable, delete globalThis.zz)'
true true true true
```

This engine answered `true true false false` before this phase: an implicit
global could not be deleted, and it did not appear in `Object.keys`.

Phase 431 taught the global object to tell §9.1.1.4's kinds apart, and wrote
the table like this:

```cpp
if (binding.is_permanent) return false;
if (!binding.is_builtin) return binding.is_deletable;
return true;
```

That is a three-way table with **four** kinds in it. There are four:

| binding | configurable | recognisable by |
|---|---|---|
| `undefined`/`NaN`/`Infinity` | no | `is_permanent` |
| a top-level `var`/function | no | `is_permanent` (set in `hoist_var`) |
| a library global | yes | `is_builtin` |
| **a sloppy implicit global** | **yes** | **none of the three** |

The fourth row matches none of the tests, so it fell to the `is_deletable`
branch — which is about what a sloppy direct `eval` made, not this — and
answered `false`. The bug is not that a case was missed; it is that the case
was *unrepresentable* in the shape the code had. A table written as
progressive tests reads correctly from every row that exists and invents an
answer for the row that does not.

The `enumerable` half had a different cause and the same lesson: the name was
created through `JsEnvironment::define`, which a built-in's binding also goes
through, so it inherited `is_builtin` from the environment it landed in and
reported `{enumerable: false}`. `define_script_global` was written in Phase 431
for exactly this and had no caller, because its one caller lives in
`src/js/JsEval.cpp`, which the open #421 owned. Phase 431's write-up said so
and left the assertion out rather than committing a failing test.

## 2. What changed

Two lines.

`assign_identifier`'s implicit-global tail calls `define_script_global` instead
of `define`, which clears `is_builtin` — so the name is enumerable and does not
pretend to be a library global.

`is_configurable_binding` is one line:

```cpp
return !binding.is_permanent;
```

Everything but the two kinds §9.1.1.4 names is configurable. That is not a
shortcut around the table; it is the table, stated as the standard states it —
a positive rule about the two exceptions rather than an enumeration of the
cases that are allowed to be true.

## 3. How it was verified

A suite of its own, `436-implicit-global.cpp`, because the case is a *kind* of
binding and the four kinds deserve four sets of assertions rather than one more
line in Phase 431's file. Every expected value Node v22.22.3's.

```
$ ./build/test_suite
All 486 engine test suites passed successfully!
```

The red proof is the old rule, restored exactly:

```
436-implicit-global.cpp:36  [d.writable, d.enumerable, d.configurable] → "true,false,false"
```

Phase 431's suite also catches it, at `:101`, and fires first because it sorts
earlier; moving it aside shows the new suite failing on its own. So the defect
is guarded twice over, by the file that found it and by the file written for it.

```
test262  15196 → 15211 pass (96.9% → 97.0%), 0 lost
js conformance 1153/1158 (unchanged)   everyday probe 90/90
```

The 15 test262 tests are `language/statements/variable` and the global-object
cases that ask whether an assignment to an undeclared name is deletable.

## 4. What the first draft of this phase got wrong

The first attempt changed only the call site — `define` → `define_script_global`
— rebuilt, and found the output **unchanged**. That is what identified the
second half: with `is_builtin` cleared, `is_configurable_binding` still fell to
`return binding.is_deletable`, which is `false` here, so the name was now
correctly not-a-built-in and still not configurable. Two independent causes,
one symptom, and fixing the obvious one moved nothing — which is the signal to
stop reading the code and print the flags, and it was a debug `fprintf` on
`is_builtin`/`is_permanent` that showed both were `0` and the rule was the
problem.

## 5. What is still open

* **`delete globalThis.document` answers `true` and leaves `document`
  resolving.** Found while probing this phase's neighbours and confirmed
  present on `main` before it: `hasOwnProperty` goes `true` → `false`, and
  `typeof document` is still `"object"`, so the window's binding and the
  object's property have come apart. It is the same shape as the defect
  Phase 431 fixed and it is not this one — a page global is attached by
  `dom::attach_window`, not by the environment record this phase's rule
  reads. Not fixed here, and not asserted.
* **The `window` object's own globals** (`document`, `crypto`, the observers)
  are still reported `configurable: true` and are now deletable from the
  object; whether deleting one should also remove the binding is the same
  question as above and has the same answer pending.
