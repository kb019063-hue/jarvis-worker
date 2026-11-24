# Finding 294 — an arguments object was an Array with a label on it

**Severity:** the wrong prototype, so every inherited name came from the wrong
place. `typeof arguments.push` was `"function"`, and a delete refusal's message
said `#<Array>` where a browser says `#<Object>`. `Array.isArray(arguments)`
was right, but only because a class check had been added to answer it.

**Found by:** `AGENTS.md`'s Phase 123 row, which names it in one clause:
"`arguments`' prototype is `Array.prototype` here, so its message says
`#<Array>` where V8 says `#<Object>`."

## What it was

```cpp
JsValue arguments = JsValue::make_array(args);      // installs Array.prototype
arguments.object_val->klass = ObjectClass::Arguments;   // relabels it
```

The relabelling renamed the *class* and left the *prototype* alone. Two
questions were answered from the class (`Array.isArray`,
`Object.prototype.toString`) and every other inherited name — `push`, `pop`,
`map`, `concat` — came through `Array.prototype` as though the object were an
Array exotic object. §10.4.4 makes it `%Object.prototype%`: an ordinary object
that happens to have index properties and a `length`.

## What the fix holds

- **The class was not the prototype.** A test that asks `Array.isArray` of the
  object cannot tell you whether its prototype is right; the two were answered
  by different things and only one was corrected when the label was added.
- **§10.4.4.7 is not optional bookkeeping.** Moving the prototype takes away
  `Array.prototype[@@iterator]` with the rest of it, and an arguments object
  must keep iterating. The standard's answer is its own `@@iterator`, and it
  is *the same function object* as `%Array.prototype.values%` — an identity a
  browser shows and a fresh function would not.
- **Measure the corpus before believing the reasoning.** The prototype change
  is one line and reads as obviously right; it cost 75 test262 cases. The
  number, not the argument, is what said the standard had a second half.

## Still open

`callee` (§10.4.4.6) is `undefined` here and not an own property — pre-existing,
found by this phase's suite, out of its scope. Mapped arguments (§10.4.4.2),
which makes the index properties and the parameter bindings one thing, are
not modelled at all.
