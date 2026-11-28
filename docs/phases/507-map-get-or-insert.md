# Phase 507 — `Map.prototype.getOrInsert` and `WeakMap.prototype.getOrInsert`

**Finding:** [`docs/findings/507-map-get-or-insert.md`](../findings/507-map-get-or-insert.md)

`Map` and `WeakMap` lacked the upsert helpers `getOrInsert` and `getOrInsertComputed` (TC39 proposal `upsert`, stage 4). The pattern `if (!m.has(k)) m.set(k, v)` is two lookups and, when the computation is expensive, needs a thunk.

## What was wrong

`Map.prototype.getOrInsert` and `getOrInsertComputed` were `undefined`; same for `WeakMap`. Scripts that used the new API threw `TypeError: … is not a function`.

## What changed

`src/js/BuiltinsCollections.cpp`:

- `Map.prototype.getOrInsert(key, defaultValue)` — one hash lookup; returns existing value if present, otherwise `set(key, defaultValue)` and returns it. `-0` normalized to `+0`, `NaN` equals `NaN`, objects by identity — through `CollectionData::hash_key` already used by `get`/`set`.
- `Map.prototype.getOrInsertComputed(key, callback)` — same, but `callback` must be callable; if missing, `callback(key)` is called with `undefined` as `this` and its result is inserted. Throws `TypeError` if callback not callable.
- `WeakMap` mirrors both, with `require_object_key` for non-object keys (as `set` does).

Lengths are `2`, as the spec defines.

## What it measured

- `tests/suites/507-map-get-or-insert.cpp` — 22 assertions: insertion vs existing, `size`, `-0`/`+0`/`NaN`/object identity, `getOrInsertComputed` callback-once and `this`/`key` args, `TypeError` for non-callable and for non-object WeakMap keys, receiver checks, `.length` 2.

- `make test` 534/534 (533 + new suite).

## Still open

- `WeakMap.getOrInsert` clearing? No; GC not observable here.
