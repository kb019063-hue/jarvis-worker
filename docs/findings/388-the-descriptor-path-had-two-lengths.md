# Finding 388 — an array's length was two different properties, depending on how you asked

**Severity:** `Object.defineProperty(arr, 'length', …)` and `arr.length = …` are
the same algorithm in §10.4.2.4. In this engine they were two: the assignment
path deleted the places above the new length and refused when one would not go,
and the descriptor path did neither. A script could shorten an array past a
frozen element through `defineProperty` and get away with it, and an array's
`length` could be made enumerable and configurable by asking for it to become
non-writable.

**Found by:** the row "A data property on an index of an array has no
attributes of its own", whose still-open half names 190 `defineProperty` cases
as undiagnosed. The row's own first explanation — that an index has no property
entry of its own — is contradicted by the source and is explicitly marked as
not to be repeated; what is actually there is below.

## What it was

The family is not one fault. Sorting it by failure message gives:

| message | cases | cause |
|---|---|---|
| `Expected a TypeError to be thrown but no exception was thrown` | 25 | the sweep (§10.4.2.4 step 4) never ran on the descriptor path |
| `length descriptor should not be enumerable` / `should not be writable` | 8 | the non-writable branch wrote `Enumerable\|Configurable` instead of `Writable` |
| `0 descriptor should not be enumerable` | 14 | an index in the dense representation has no attribute bits — the row's original claim, still true for the plain path |
| `arr.hasOwnProperty("2") !== true` | 1 | the dense representation again |
| `Expected "a === 10", actually 0` | 5 | a different family |

Two of those had a cause in `define_own_property` and are fixed here:

```cpp
// was: only the range was checked for a descriptor on `length`
if (cell->is_array && key == "length" && value.present) {
    if (!(asked >= 0) || ... ) js_throw_range_error("Invalid array length");
}

// and making it non-writable rewrote the attributes as Enumerable|Configurable
object.define_property("length", ..., PropertyMap::Enumerable | PropertyMap::Configurable);
```

```js
var a = [0, 1];
Object.defineProperty(a, '1', { value: 1, configurable: false });
Object.defineProperty(a, 'length', { value: 0 });   // was: silent;  Node: TypeError

Object.defineProperty([], 'length', { writable: false });
// was: enumerable true, configurable true;  Node: false, false
Object.defineProperty([], 'length', { configurable: true });
// was: silent;  Node: TypeError
```

## The ordering mistake, which is the interesting part

The first version of the fix was written *below* the plain-attributes shortcut
in the same function, and the shortcut returns early for exactly the descriptor
shape these tests use — `{ value: n }` on `length`, with no attribute
mentioned. The patch compiled, the code was correct, and **not one case
changed**. The block had to move above the shortcut before it could run.

That is the same class of mistake as a stale binary: a correct fix in a place
that never executes, which reads as "the diagnosis was wrong". The difference
is that this one can be found by reading the function's early returns, which is
cheaper than rebuilding.

## Evidence

test262 15,153 → **15,168**, +15 with **0 broken**;
`built-ins/Object/defineProperties` 580 → 586 of 632, `defineProperty` 1,074 →
1,080 of 1,131; `language` unchanged at 11,163; `make test` **450/450**;
`js-conformance` and `wpt` unchanged. Every assertion in
`tests/suites/388-array-set-length.cpp` was answered by Node v22.22.3 first.

## What is still open, stated as measurements

98 cases under these two directories still fail, and they are now grouped: ~25
are the same missing `TypeError` in shapes the sweep does not reach (descriptor
combining `value` with an attribute change, and the `defineProperties` ordering
of §20.1.2.3.1), ~14 are the dense-representation attribute gap, 5 are the
`a === 10` family, and 3 are `BigInt64Array` being undefined — which is not a
property-descriptor question at all.
