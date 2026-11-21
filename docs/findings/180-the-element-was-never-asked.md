# Finding 180 — `toLocaleString` never asked the element

`toLocaleString` existed and looked finished, which is what made it hard to see.
`new Uint8Array([1,2]).toLocaleString()` said "1,2", and "1,2" is the right
answer for `[1,2]` in an English locale, so nothing in the output said
otherwise. The method was not calling the elements' own `toLocaleString` at all;
it was formatting the numbers itself, and it had the same defect in
`Array.prototype.toLocaleString`.

The probe that shows it takes one line:

```js
Number.prototype.toLocaleString = () => "X";
[1,2].toLocaleString()                  // engine "1,2", Node "X,X"
new Uint8Array([1,2]).toLocaleString()  // engine "1,2", Node "X,X"
```

A method that ignores the element's own method is not `toLocaleString`; it is
`join` with a different name. The fix is to ask each element and call what it
answers, which is what `Invoke(element, "toLocaleString")` means.

Two things had to stay as they were. A hole and `undefined` join as the empty
string **without** asking, which is why `[1,,3].toLocaleString()` is "X,,X" and
not "X,X,X" — the standard's exception, and the reason this cannot be
`join`-shaped after all. And a typed array's `toLocaleString` is a **separate
function** from Array's: it validates the receiver, so `call(42)` is a
TypeError, where Array's boxes the 42 and answers "".

The second defect is identity rather than behaviour, and only a `===` finds it.
§23.2.3.30 says `%TypedArray%.prototype.toString` *is* `Array.prototype
.toString`. The engine gave the typed array its own copy, which answered the
same text ("1,2") and was the wrong value:

```js
Uint8Array.prototype.toString === Array.prototype.toString   // was false
```

## The mistake the fix made first

Reading `length` as a property is right for an array and wrong for a view. The
first version did that for both, and test262's
`get-length-uses-internal-arraylength` failed with `Expected SameValue(«1»,
«0»)`: redefining `length` on a `Uint8Array` changed what its `toLocaleString`
walked. A typed-array method reads `[[ArrayLength]]`, the internal slot, and
never the property.

## What the numbers say

Over all of `built-ins/TypedArray` and `built-ins/Array` — 4,536 tests, the
blast radius of changing `Array.prototype.toLocaleString` — the tree went from
3,315 to 3,332 passing, and **no test that passed before failed after**. In the
three directories this row is about: 13 to 29 of 54.

Of the 25 still failing, 20 need `BigInt`, 3 need the `$262` harness the runner
does not provide, and 2 need correct primitive `this`-binding — which this
engine lacks everywhere, including `(function(){ return typeof this; }).call(42)`,
and which is a row of its own rather than a part of this one.
