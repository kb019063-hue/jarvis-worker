# Finding 507 — `Map.getOrInsert` was not a function

**Severity:** missing API, stage 4 proposal. Upsert pattern required two lookups and a branch at every call site.

**Found by:** checking `typeof Map.prototype.getOrInsert` — `undefined` where the proposal says `function`.

**Repro:**

```js
new Map().getOrInsert('k', 42) // TypeError: is not a function
```

**What changed:** `Map` and `WeakMap` now have `getOrInsert` and `getOrInsertComputed` with SameValueZero keying and object-key checks for WeakMap.

**Still open:** none for Map; WeakMap reclamation not tested.
