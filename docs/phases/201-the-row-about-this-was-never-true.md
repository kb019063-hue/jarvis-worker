# Phase 201 — the row that asked for a change the engine did not need

Task: the row that began "A method's `this` is boxed before the call", which
claimed the engine boxes a strict function's `this` and asked for a change to
the call path of every function. Finding 205
(`docs/findings/205-the-row-about-this-was-never-true.md`).

**No phase or finding?** This is not engine work — it corrects a row of
`AGENTS.md` and adds two documents. The pair `Phase 201, finding 205` is
recorded here for the record; the change moves no scoreboard.

## 1. The probe came before the code

The row carried its own check:

> The check is `(function () { return typeof this; }).call(42)` under
> `"use strict"` and without, and the sloppy and strict answers must differ for
> a primitive

Run against the binary built from `main` at `c295b1c`:

```
$ ./build/litebird --dump-dom this.html
sloppy .call(42)      = object
strict .call(42)      = number      ← the row said this was "object"
strict imm .call(true)= boolean
strict .call(null)    = object
```

Node v22.22.3 answers the same. The sloppy and strict answers **do** differ, and
differ for a primitive — the row's own acceptance check already passed. The row
was describing an engine that did not exist.

## 2. Fifteen forms, because one proves nothing

A call path is large: `.call`, `.apply`, `Reflect.apply`, `bind`, method
shorthand, class methods (strict by nature), accessors, and a primitive
receiver for each. Every form was run in both engines with the same script and
compared line by line:

```
sloppy.call(42)                string  | string        (value: "object")
strict.call(42)                number  | number
strict.call(true)              boolean | boolean
strict.call('s')               string  | string
strict.call(null)              object  | object
strict.call(undefined)         undefined | undefined
obj method .call(42)           object  | object
obj shorthand .call(42)        object  | object
obj strict .call(42)           number  | number
class method .call(42)         number  | number
strict bound(42)               number  | number
sloppy bound(42)               object  | object
strict getter on a primitive   boolean | boolean
Reflect.apply strict(42)       number  | number
"ab".slice(0,1)                a       | a
```

15/15. There is no form in which the engine boxes a strict function's `this`.

## 3. The row's example was wrong twice

`Boolean.prototype.toString = function () { return typeof this; }` makes
`(true).toString()` answer `"object"` **in Node too**. The function just written
is sloppy, so boxing is correct for it — the row wrote a sloppy function, then
compared it against a strict function's expected answer:

```
sloppy replacement function:  Node "object"   Litebird "object"
strict replacement function:  Node "boolean"  Litebird "boolean"
```

Both engines agree in both modes. The row drew a defect out of a comparison
between two different things.

## 4. The test262 cases it blamed fail for another reason

`built-ins/Array/prototype/toLocaleString` at the pinned `2e58f87d8350`:
**4 pass, 8 fail**. Two of the eight are the ones the row named. Reading them
shows `flags: [onlyStrict]` and a strict replacement `toString` — so they expect
`"boolean"`, and the engine answered `"true,false"`: the *numbers*, not the
overridden method's return.

The fault is that `Array.prototype.toLocaleString` never asked its elements for
their own `toLocaleString`:

```
Boolean.prototype.toLocaleString = function () { return "CALLED"; };
[true, false].toLocaleString()      Litebird "true,false"   Node "CALLED,CALLED"
(true).toLocaleString()             Litebird "CALLED"       (so the call path is fine)
```

`js_define_method(ap, "toLocaleString", …)` was `join_array(receiver, ",")`. That
is Marten's `#238` (Phase 192 / finding 196), open and lower-numbered, and it
already replaces the method with `element.get_property("toLocaleString")` and
`call_with_this(element)`. The other six failures are `BigInt`-dependent
(`BigInt`, `BigInt64Array` undefined) or separate semantic gaps — none of them
`this`-binding.

## 5. What was changed

One table row. Struck through, replaced with what was measured, and with a check
column that asks for a probe rather than a change:

> A change here must show a call form where a strict function's `this` is boxed,
> or a sloppy one where it is not. No such form was found: probe before
> believing the row.

That is the whole change: `AGENTS.md` and two documents. No source file was
touched, and none needed to be.

## 6. What this does not claim

- Not that the call path is correct. Fifteen forms agreeing with Node is not a
  proof; it is the absence of the specific defect the row described.
- Not that the two test262 cases pass. They do not, and will not until `#238`
  lands; this phase did not touch it, because `src/js/BuiltinsArray.cpp` is
  Marten's and the row was claimed first.
- Not that `this` is uninteresting elsewhere: `Symbol.toPrimitive`, `Date`'s
  `toString`, and the `valueOf` fallback were not examined.

## Files

- `AGENTS.md` — the row, struck through, with the measurement that refutes it
- `docs/findings/205-the-row-about-this-was-never-true.md`
- `docs/phases/201-the-row-about-this-was-never-true.md`
