# Finding 436 — the fourth kind of global

Phase 436. Status: **fixed.**

## 1. What was wrong

An assignment to an undeclared name in sloppy code makes a property of the
global object, and §9.1.1.4 gives it all three attributes true:

```
zz = 3;
Object.getOwnPropertyDescriptor(globalThis, "zz")
// Node: {writable: true, enumerable: true, configurable: true}
// here: {writable: true, enumerable: true, configurable: false}
delete globalThis.zz   // Node: true; here: false
```

Phase 431 taught the global object to distinguish the kinds of binding
§9.1.1.4 has, and wrote the distinction as three progressive tests:

```cpp
if (binding.is_permanent) return false;
if (!binding.is_builtin) return binding.is_deletable;
return true;
```

There are four kinds, not three. The fourth — a sloppy implicit global — is
not permanent, not a built-in, and not a name a sloppy `eval` made. It matches
none of the tests and so received the answer `false` from a branch written
about something else.

**The defect was not a missing case, it was an unrepresentable one.** Written
as tests, an enumeration of kinds reads correctly for every kind it has and
silently invents a verdict for the kind it lacks. Written as the standard
writes it — "these two are not configurable, the rest are" — there is no row to
fall through.

## 2. What changed

`is_configurable_binding` became one line, `return !binding.is_permanent;`.

And the name is now created by `define_script_global`, which clears
`is_builtin`. Without that, an implicit global inherits the library-global
mark from the environment it lands in and reports `enumerable: false` — the
same wrong-kind answer from the other side. The function was written in Phase
431 for exactly this and had no caller, because its caller lives in
`src/js/JsEval.cpp`, which the open #421 owned at the time.

## 3. Two causes, one symptom

The first attempt changed only the call site. Rebuilt, the output was
**identical**: `true,true,false`. Clearing `is_builtin` did not help, because
`is_configurable_binding` then fell through to `is_deletable`, which is also
false. Only a debug print of both flags showed the second cause.

That is worth recording because the instinct on seeing no change was to assume
the build had not taken — it had. The rule was `return !binding.is_permanent`
either way, and the flags were already `0,0` before it. A fix that moves
nothing is evidence about the *other* half of the rule, not evidence that the
change is inert.

## 4. What is still open

* **`delete globalThis.document` answers `true` and `document` survives.**
  `hasOwnProperty` flips to `false` while `typeof document` stays `"object"`,
  so the object's property and the scope's binding have come apart. Present on
  `main` before this phase; a page global is attached by
  `dom::attach_window` rather than by the environment record this rule reads.
  The same shape as Phase 431, in a different mechanism, and not fixed here.
