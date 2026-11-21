# Finding 141 — legacy accessors were invisible

Found in the `AGENTS.md` backlog and fixed in Phase 136. The implementation and
measurements are in
[`docs/phases/136-legacy-accessor-reflection.md`](../phases/136-legacy-accessor-reflection.md).

## What was wrong

The four Annex B.3.3 methods on `Object.prototype` were absent:

```js
typeof Object.prototype.__defineGetter__  // "undefined" here; "function" in Node
typeof Object.prototype.__defineSetter__ // "undefined" here; "function" in Node
typeof Object.prototype.__lookupGetter__  // "undefined" here; "function" in Node
typeof Object.prototype.__lookupSetter__ // "undefined" here; "function" in Node
```

The engine could already store and describe accessors through
`Object.defineProperty`; it simply did not expose the legacy entry points.
That left code using the Annex B methods without a compatible interface.

## The repair and independent check

`BuiltinsObject.cpp` now implements getter/setter definition by building the
standard accessor descriptor and sending it through `define_from_descriptor`.
That retains an existing counterpart accessor and preserves the standard's
validation and property-key conversion order. The lookup pair asks
`descriptor_for` at each level of the prototype chain and stops at the first
own property, whether it is an accessor or data property.

The ten new suite checks were run under Node.js v22.22.3 before they were
asserted in the engine. The same script passed in both. `make test` passed all
297 engine suites. The relevant test262 directories report 44/54; all ten
remaining failures require `Proxy`, which is not defined in the engine.
