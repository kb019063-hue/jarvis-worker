# Finding 130 — the integrity level was a bit, and not a question

Phase 125. The architecture and the measurements are in
[`docs/phases/125-object-integrity-levels-ask-every-property.md`](../phases/125-object-integrity-levels-ask-every-property.md).

## 1. What was wrong

`Object.isFrozen` and `Object.isSealed` read a boolean that `Object.freeze` and
`Object.seal` had set:

```js
Object.isFrozen(Object.preventExtensions({}))   // false  — the language has true
Object.isFrozen(Object.seal({}))                // false  — the language has true
```

The row in `AGENTS.md` describes this as a fault about the **empty** object:
`preventExtensions({})` is not extensible and has no properties, so nothing in
it is writable or configurable, and it is frozen. The engine could not see
that, because no call had set the bit.

That description is right about the mechanism and too narrow about the damage.
The condition is not "has no properties". An object is frozen when it is not
extensible **and every own property is non-writable and non-configurable** —
which an object can be without anybody ever calling `freeze` on it:

```js
var o = Object.preventExtensions(
          Object.defineProperty({}, 'a', { value: 1, writable: false, configurable: false }));
Object.isFrozen(o);   // true — the engine said false
```

`Object.seal({})` is the same fault in the other direction and is the more
likely one to be met in a page, because sealing a table that happens to be
empty is a normal thing to do and freezing it afterwards is not.

## 2. Why it was possible to be wrong in this particular way

The engine already had `extensible`, and it already had the descriptors right:
`seal` cleared `configurable` and left `writable`, `freeze` cleared both, and
both cleared `extensible`. Every fact needed to answer the question was
present and correct. What was missing was the loop.

The `frozen`/`sealed` bits are not useless — they are a *cache of an intent*
that `defineProperty` and the property map keep honest, and `push` and
`defineProperty` read them. The mistake was letting a cache stand in for a
predicate in the two places the standard asks the predicate directly. §7.3.15
does not consult any record of what `freeze` did; it asks each own property
what it *is*.

This is the difference between:

- "did anyone freeze this?" — a bit, and the wrong question, and
- "is this frozen?" — `[[IsExtensible]]` false and no own property that could
  still be written or reconfigured, and the question the standard asks.

A bit answering a question is only right when the answer cannot change behind
its back. Here it can, in both directions: `Object.defineProperty(o, 'a',
{writable: false})` makes a *sealed* object frozen without touching any bit,
which the new suite asserts (`before false`, `after true`).

## 3. What the fix looks like, and the two places the spec is easy to get wrong

```cpp
bool test_integrity_level(const JsValue& object, bool frozen) {
    if (!object.is_object_like()) return true;               // §7.3.15 step 1
    const JsObject* cell = object.property_cell();
    const bool extensible = !cell || cell->extensible;
    if (extensible) return false;                            // step 2
    for (const std::string& key : reflected_own_keys(object, false)) {   // step 3
        const JsValue descriptor = descriptor_for(object, key);
        if (descriptor.is_undefined()) continue;
        if (frozen) {
            const JsValue writable = descriptor.get_property("writable");
            if (writable.type == JsType::Boolean && writable.bool_val) return false;
        }
        const JsValue configurable = descriptor.get_property("configurable");
        if (configurable.type == JsType::Boolean && configurable.bool_val) return false;
    }
    return true;
}
```

**`reflected_own_keys(object, false)` is not a stylistic choice.** The second
argument asks for non-enumerable keys too. §7.3.15 step 3 says "for each own
key", so a frozen object whose only property is non-enumerable is frozen, and
`for (key in o)` would have answered `true` for an object that is not. The
difference is invisible on the objects in the table above, which is exactly why
it would have survived review.

**Accessors have no `[[Writable]]`.** `descriptor.get_property("writable")` on
an accessor descriptor is `undefined`, not `false`, and the test has to be
"is a boolean and true" rather than "is not false" — the language does not
fail a frozen accessor for being unwritable, because it is not a data
property. `frozen` gates only this test; the configurable test runs at both
levels. That is what makes `preventExtensions` over a writable non-configurable
property `frozen: false, sealed: true`, an answer the old code could not
produce at all.

**Step 1 is `true`, not `false`.** `Object.isFrozen(1)` is `true`, because a
non-object is trivially not extensible and has no properties that could fail
anything. It reads backwards and is the standard.

## 4. What it cost, and what it bought

Four lines in a helper and two call sites. test262's two directories went from
85/92 to 88/92; the whole corpus from 13,935 to 13,938 passing, with no other
directory moving, which is what a change this narrow should look like. The four
remaining failures in those two directories are `Proxy` (not defined in this
engine) and `not-a-constructor` (the syntax, not this predicate), both already
open rows.

The wider reading is that the engine's integrity machinery was *sound and
unused* — the descriptors, the extensibility flag and the property map all
already answered §7.3.15 — and a bit was kept in front of them. The same shape
appeared in `src/js/BuiltinsArray.cpp`, where `push` tested `frozen` rather
than `[[IsExtensible]]` and a sealed array therefore grew; that was found by
this phase's probing, written up as an open row, and then closed by #146's
rewrite of the place-moving methods, which merged while this branch was in
flight. Re-probed against the merged tree, all eight calls throw exactly where
Node throws, so the row is recorded as closed and no code was needed for it
here.

The lesson worth keeping is about the shape rather than either bug: an
operation that stores the *outcome* of an earlier call and answers a later
question from it is right only when nothing can change the answer behind its
back. Both `isFrozen` and `push` read a bit that `freeze()` had set, and both
were wrong for the objects `seal()` and `preventExtensions()` produced, which
are non-extensible all the same.
