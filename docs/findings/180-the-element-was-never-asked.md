# Finding 180 — `toLocaleString` never asked the element

`toLocaleString` existed and looked finished, which is what made it hard to see.
`new Uint8Array([1,2]).toLocaleString()` said "1,2", and "1,2" is the right
answer for `[1,2]` in an English locale, so nothing in the output said
otherwise. The method was not calling the elements' own `toLocaleString` at all;
it was formatting the numbers itself.

The probe takes one line:

```js
Number.prototype.toLocaleString = () => "X";
new Uint8Array([1,2]).toLocaleString()   // engine "1,2", Node "X,X"
new Float32Array([NaN]).toLocaleString() // engine "NaN", Node "X"
```

A method that ignores the element's own method is not `toLocaleString`; it is
`join` with a different name. The fix is to ask each element and call what it
answers, which is what `Invoke(element, "toLocaleString")` means, with the boxed
element as `this`.

A typed array has no holes, so unlike `[1,,3]` — where a hole joins as the empty
string **without** being asked — every element of a view is a value and every
element is asked. That is the second line above: the `NaN` is asked, and answers
"X".

The second defect is identity rather than behaviour, and only a `===` finds it.
§23.2.3.30 says `%TypedArray%.prototype.toString` *is* `Array.prototype
.toString`. The engine gave the typed array its own copy, which answered the
same text ("1,2") and was the wrong value:

```js
Uint8Array.prototype.toString === Array.prototype.toString   // was false
```

## The mistake the fix made first

Reading `length` as a property is right for an array and wrong for a view. The
first version did that, and test262's `get-length-uses-internal-arraylength`
failed with `Expected SameValue(«1», «0»)`: redefining `length` on a
`Uint8Array` changed what its `toLocaleString` walked. A typed-array method
reads `[[ArrayLength]]`, the internal slot, and never the property.

## What the numbers say

Over all of `built-ins/TypedArray` and `built-ins/Array` — 4,536 tests — the
tree went from 3,315 to 3,329 passing, and **no test that passed before failed
after**. The two directories this row is about went from 13 of 50 to 28 of 50.

Of the 25 still failing, 20 need `BigInt`, 3 need the `$262` harness the runner
does not provide, and 2 need correct primitive `this`-binding — which this
engine lacks everywhere, including `(function(){ return typeof this; }).call(42)`,
and which is a row of its own rather than a part of this one.

## The half that is not in this change

`Array.prototype.toLocaleString` has the same defect and was fixed, measured,
and then **taken back out**: the fix needs `src/js/BuiltinsArray.cpp` and
`src/js/JsOps.cpp`, pull request #176 has both open and is older, and AGENTS.md
says an older claim keeps its files. Array's method is also not this row. The
measurement is written down in `AGENTS.md` instead — that half is worth +3 over
this one — so the next agent starts from a number rather than from the probe.
