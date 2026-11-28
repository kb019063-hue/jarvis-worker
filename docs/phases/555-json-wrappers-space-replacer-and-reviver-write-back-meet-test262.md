# Phase 555 — JSON.stringify's wrappers, space and replacer list, and the reviver walk's write-back, meet test262

`make test262 built-ins/JSON` ran **130 of 166**. Categorising the 36: 19
need `Proxy` (one of those also `$262`), 6 need BigInt syntax, and **10 were
engine defects** — every one of them a place where the engine read a slot
where the standard reads a conversion, or wrote where the standard defines.
This phase fixes the 10; the 26 blocked ones stay listed with their reason.

## The conversions the engine skipped

`SerializeJSONProperty` step 4-5: a Number object converts with **ToNumber** —
the hint the call gives, so `valueOf` answers first and a `toString` override
is never asked; a String object converts with **ToString**, the other way
round; a Boolean object with **ToBoolean**, which is the slot and no method.
The engine extracted the wrapped primitive through
`js_wrapped_primitive` and skipped the methods entirely, so a wrapper whose
`valueOf` returned 2 serialised as the 42 its slot held
(`value-number-object`, `value-string-object`), and the abrupt completion of
a wrapper whose methods throw never happened.

The replacer list (step 4 of the same algorithm) accepts a String item,
`ToString`s a Number item, and `ToString`s a String or Number **object** —
with the String hint, so a `toString` override answers before `valueOf` —
and everything else (`null`, `undefined`, booleans, plain objects, symbols,
functions) names nothing. The engine took bare strings and numbers only, so
`JSON.stringify({10: 1, toString: 2}, [numberObject])` selected no property
where the standard selects `toString` (`replacer-array-number-object`,
`replacer-array-string-object`).

The `space` argument dispatches on the wrapper's slot: a Number object is
`ToNumber`'d (its `valueOf` answered 3 where the engine saw a non-number and
gave no gap), a String object is `ToString`'d (its `toString` gave `---`),
anything else leaves the gap empty — there is no ToPrimitive fallback, which
is why `JSON.stringify(v, null, {})` writes no space — and a Number result
is truncated toward zero before the cap at ten, so `-1.99999` is `-1` is no
gap (`space-number-float`, `space-number-object`, `space-string-object`).

## The walk that wrote through a refusal

`InternalizeJSONProperty` step 3.a is `CreateDataProperty`: the write-back is
a definition whose descriptor says `{writable, enumerable, configurable}` all
true, and a definition the object refuses is **ignored**, not an error. A
reviver can make a property non-configurable ahead of the walk —
`Object.defineProperty(this, 'b', {configurable: false})` during key `a`'s
visit — and the value it already holds is what survives. The engine used
`set_property`, which wrote through the refusal, so the reviver's 22 replaced
the 2 the test keeps (`reviver-object-non-configurable-prop-create`,
`reviver-array-non-configurable-prop-create`). The check is the whole of
`OrdinaryDefineOwnProperty` for that descriptor: the only way it is refused
is an existing property that is not configurable, and that is what
`may_create_data_property` asks.

## The source that outlived its value

`reviver-forward-modifies-object` replaces `holder[1]` while visiting key
`0`, then requires the later visits — the replacement's own keys, and key
`1` itself — to see **no `source`**: the context Record is the parse's
description of the value the parse put in that slot, and the slot no longer
holds that value. The engine looked the span up by `(holder, key)` alone, so
the replaced key still carried the stale "2". Each recorded span now carries
the value the parse put there, and the lookup serves it only while
`strict_equals(recorded, current)` — which is the whole difference between
"this slot's text" and "this slot's text, whoever wrote it last".

## Numbers

- `built-ins/JSON`: **130 → 140 of 166**; the 26 that remain fail on
  `Proxy is not defined`, `$262 is not defined`, or BigInt literals — 19, 6
  and 1 of them respectively — and none on the answers this phase touches.
- The harness's default 21-directory corpus is unchanged, 15,350 before and
  after the fix (the same binary rebuild both ways), so the conversions
  moved nothing they should not.
- `make test` **584/584** with the new suite
  (`tests/suites/555-json-wrappers-space-replacer-and-reviver-write-back-meet-test262.cpp`),
  which pins all five fixes with Node v22.22.3-measured expectations and
  fails on the old code at its wrapper conversion.
