# Finding 137 — boxed strings lost their index descriptors

Found while Phase 124 wrote the `Reflect` tests; fixed in Phase 132. The
implementation and Node.js comparison are in
[`docs/phases/132-boxed-string-own-property-descriptors.md`](../phases/132-boxed-string-own-property-descriptors.md).

## The mismatch

A boxed string already exposed its character indices through reads,
`hasOwnProperty`, and `Object.keys`, but the property descriptor path treated
only a primitive `JsType::String` as a string. The boxed value is an object
(`ObjectClass::String`), and its character indices are virtual String exotic
properties rather than entries in the ordinary property map. As a result:

```js
Object.getOwnPropertyDescriptor('ab', '1')          // correct descriptor
Object.getOwnPropertyDescriptor(new String('ab'), '1') // undefined (wrong)
```

ECMA-262 §10.4.3 requires the boxed character index to have the wrapped
UTF-16 code unit as its value, with `writable: false`, `enumerable: true`, and
`configurable: false`. The missed descriptor also made
`propertyIsEnumerable` false and prevented
`Object.getOwnPropertyDescriptors` from returning the character descriptors.

## The repair

`descriptor_for` now checks a String wrapper's internal primitive and answers
for in-range indices before looking in the ordinary property map. Since
`Object`, `Reflect`, `Object.getOwnPropertyDescriptors`, and
`propertyIsEnumerable` share this path, one repair corrects all of them.
The primitive-string case remains as it was, and an index beyond the wrapped
length remains an ordinary property.

Node.js v22.22.3 produced the expected answers for the seven cases in the new
`boxed_string_own_index_descriptors` suite. Node and the engine also printed
identical output for a standalone descriptor/enumerability probe. `make test`
passed all 296 engine suites.
