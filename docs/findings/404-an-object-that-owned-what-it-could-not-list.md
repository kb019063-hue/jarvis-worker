# Finding 404 — an object that owned what it could not list

The global object could find `Object`, write it, describe it, and answer every
question about it except the two that are about *the set of names it owns*:

```
> Object.getOwnPropertyNames(globalThis).length
5
> Object.getOwnPropertyDescriptor(globalThis, "Object")
{writable: true, enumerable: false, configurable: true}
```

The second line is the diagnosis. A descriptor that correct cannot come from
nothing: something already knew, for a single name, exactly what the object
owned. `descriptor_for` had asked the global environment since Phase 199
(finding 203). The list of own keys was still the five names that happened to
be in the property table.

## Why one name at a time was enough, and then was not

The global environment is not the global object, and the object reaches across
through hooks on its cell:

```cpp
std::shared_ptr<std::function<bool(const std::string&, JsValue&)>> lookup_fallback;
std::shared_ptr<std::function<bool(const std::string&, const JsValue&)>> assign_hook;
```

Two hooks, two questions: *read this name*, *write this name*. Both take a name
the caller already has. Nothing asked *what names are there*, so nothing had to
be answered, and the gap was invisible to any script that named what it wanted.

The gap is exactly one question wide, and three operations are that question:
enumeration, `hasOwnProperty`, and `delete`.

## The delete that lied

`delete globalThis.Error` answered `true` and `Error` was still a function.

`[[Delete]]` answering `true` means *the property is gone and the request was
legal*; `false` means *it is still there*. A script that checks — and checking
is the standard's own pattern — was told the operation succeeded. The property
it asked about was never in the table the delete looked in, and `return true`
at the bottom of `JsValue::delete_property` is correct for an ordinary object
that had no such property. The global object is not one: it owned the name.

## The flag that had to exist

Giving the object its names was half the work. The other half was that the
names are of two kinds and the lists must not mix them:

```
Object.keys(globalThis)                   // the host's enumerable ones
Object.getOwnPropertyNames(globalThis)    // every own name
```

A built-in is non-enumerable (§9.1.1.4). A script's `var` is enumerable. A
`let` is neither — it is not a property at all. The bindings had no bit that
said which kind a name was, so the first version of this fix reported every
name in `Object.keys` as well, which is one wrong list replaced by another.

`Binding::is_builtin` is that bit, and it is set in one place:
`mark_all_builtin()`, called once at the end of the interpreter's
construction, when the environment holds exactly what the engine put there and
no script has run. Anything a script declares afterwards is not marked, which
is what the standard says. `let`/`const`/`class` stay out through the existing
`is_lexical` bit, which the same hook reads.

## What this does not touch

`Reflect.ownKeys` and the symbol half of `[[OwnPropertyKeys]]` reach `own_keys`
through `assign_keys`, so they see the new names; symbols are not property keys
in this engine yet, so there is nothing to add there.

The `%`-slot half of the row — a script can read and write `el['%classList']` —
is untouched. That is a different mechanism: names the engine uses as its own
storage, hidden from enumeration by `is_hidden_key`. Moving those slots off the
property table is what that half asks for, not this one.
