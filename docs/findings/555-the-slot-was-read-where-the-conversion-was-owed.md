# Finding 555 — the slot was read where the conversion was owed

Ten of built-ins/JSON's 36 failures were the same defect wearing different
tests: wherever the standard converts — ToNumber a Number object, ToString a
String object or a replacer-list item, ToPrimitive nothing at all for the
space argument's plain objects — the engine read the slot the wrapper was
holding. The output was always *plausible*: 42 where 2 belonged, `"str"`
where an override should have answered, no gap where three spaces were due.
Plausible is the problem. Nothing downstream can tell a slot read from a
conversion except the object itself, and the object had a `valueOf` waiting
to be asked.

## The three ways the standard says "convert here"

1. **SerializeJSONProperty's wrapper steps** dispatch on `[[NumberData]]`,
   `[[StringData]]`, `[[BooleanData]]` — and the hint the dispatch implies is
   part of the contract: ToNumber is *hint Number* (valueOf first), ToString
   is *hint String* (toString first). Reading the slot answers the same as
   the conversion only when no method is overridden — which is exactly the
   case a conformance test does not write.
2. **The replacer list** converts by the same pattern with one twist: a
   String or Number *object* item is ToString'd with the String hint, so a
   `toString` override names the property. Everything else names nothing —
   the engine's "only bare strings and numbers" was close, and close enough
   that no page ever noticed.
3. **The space argument** is the instructive one, because it is where the
   correct answer is *less* work: the dispatch is on the slots with **no
   fallback**, so a plain object as space writes no gap at all. The
   temptation — "an object must convert to something" — is exactly wrong;
   ToPrimitive(hint Number) on `{}` would have produced
   `"[object Object]"` and ten spaces of it.

## The walk that wrote where it should define

The reviver's write-back looked like an assignment and was written as one
(`set_property`), but §25.5.5.2 step 3.a is `CreateDataProperty` — a
*definition* with all three flags true, and ordinary `[[DefineOwnProperty]]`
can refuse it. The refusal is not an error; it is the object keeping what it
holds. A reviver that runs `Object.defineProperty(this, 'b',
{configurable: false})` during an earlier key's visit has, in the standard's
eyes, frozen `b` against the walk it knows is coming. Node keeps 2; the
engine wrote 22 through it. The general lesson is the one the delete-rows
keep re-teaching: **`set` and `define` are different operations, and the
spec's write-backs are defines more often than they look.**

## The source that served whoever wrote last

The reviver's context Record keyed a span by `(holder, key)` — which is the
pair the *walk* travels by, not the pair the *value* lives by. A forward
replacement (`this[1] = 42` during key `0`'s visit) keeps both halves of the
key identical while changing the third thing: the value. A key of two is not
a key of one. The span now carries the value it describes and is served only
while that value is still in the slot — `strict_equals` against the current
read, which is identity for objects and value for primitives. What the test
asks for, and what Node answers, is a Record that says nothing the moment
its value is gone; a Record that outlives its value is a rumour about a
program state nobody can reach.
