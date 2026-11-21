# Finding 151 — the descriptor was a lie, and the oracle was a write

## What was seen

`Object.getOwnPropertyDescriptor(f, "name").writable` said `false`.
`f.name = "x"` changed it. Both statements were true at once, on the same tree,
about the same property.

That is the shape of a bug that ordinary testing does not find. Every test of
`f.name` passed — it was the right string — and every test of the descriptor's
*wording* passed, because the wording was right. What was wrong was the
relationship between them, and a test has to ask the property a question that
forces them together to see it.

## What forced it

test262's `propertyHelper.js` does exactly that, and the technique is worth
naming because it looks like vandalism:

```js
// Object.defineProperty(obj, name, { writable: false }) must actually refuse
obj[name] = otherValue;
assert.sameValue(obj[name], originalValue, "descriptor should not be writable");
```

It proves `writable: false` **by writing** and checking nothing happened. To
prove `configurable: true` it deletes the property and checks it is gone. Over
43 cases, one attribute each.

An engine that models attributes correctly passes this without noticing it is
being tested. An engine that answers a descriptor out of one place and the
value out of another fails it immediately, no matter how right the value is.

## Why it hid for so long

Because there was nothing to notice. `name` and `length` were not properties in
this engine; they were fields of the function's payload with a lookup that
answered for them. Nothing could *set* them, so the question "is it writable"
had no answer to contradict, and the descriptor was written to match the
standard rather than to match the behaviour.

The 43 cases only became visible when Phase 138 gave anonymous classes an
inferred name: with a `name` finally present, `verifyProperty` had something to
inspect, and what it found was a property that did not exist. A bug can sit
behind another bug indefinitely — 43 tests failing with a message about `name`
had, in fact, been failing with that message because `name` was `""` and
nothing could look at it.

## What it cost, and where the blame landed

The fix is small (install two properties, retire two branches of a lookup).
Three things went wrong on the way, and the pattern is worth recording:

| what I did | what caught it |
| :--- | :--- |
| made `prototype` eager without its `constructor` back-link | `make js-conformance` 1143 → 1142, one case named |
| put the caller rule in `has_own_property` | test262 lost 4 cases, all asserting `hasOwnProperty("caller") === false` |
| wrote `var arrow = function () {}` in the new test | the new test, immediately |

None of the first two was caught by the 43 cases this phase was aimed at — those
were green throughout. In both cases the failure was *over-presence*: the
feature worked, and did more than the standard allows. That is the class of
mistake a suite written for a change cannot catch, and the reason the repository
grades on both directions ("0 pass in before and not in now") and keeps
scoreboards whose cases nobody wrote for the change at hand.

## The general lesson

A descriptor is a promise about behaviour that cannot be checked by reading it.
Any place where a value and its attributes come from different places is a place
where the two will eventually disagree, and the disagreement will be invisible
until something writes through the property or deletes it.

The rule this suggests for an engine: if the language says a thing is a
property, store it as a property. `prototype` is the exception this engine still
makes, because making it eagerly for every function would cost an allocation for
the majority that are never constructors — and that exception is now the only
one left in this area.
