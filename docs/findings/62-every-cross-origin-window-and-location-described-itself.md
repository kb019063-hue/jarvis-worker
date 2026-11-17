# 62. ✅ Every cross-origin `Window` and `Location` described itself with the ordinary own-keys walk (Phase 56)

> Moved out of `AUDIT.md` — written there as a `## 62. ✅ Every cross-origin `Window` and `Location` described itself with the ordinary own-keys walk (Phase 56)` section.
> The text below is the section as it stood, unchanged.


**The hole.** Phase 42 made cross-origin reads and writes refuse
(`WindowExotic::get`/`set` throw `SecurityError` for non-safelisted names,
finding 48). Everything that *describes* a cross-origin object was still the
ordinary path, so a parent could enumerate, describe and mutate what it may
not name:

```js
const w = document.querySelector('iframe').contentWindow;   // cross-origin
Object.getOwnPropertyNames(w);   // the child's ordinary own keys — not the spec's list
Object.getOwnPropertyDescriptor(w, 'length');   // whatever was stored, or undefined
Object.defineProperty(w, 'name', { value: 'owned' });  // writes into the child's own map
Object.freeze(w);                // flips the child's cell bits from another realm
Object.getPrototypeOf(w);        // the child realm's Window.prototype (spec: null)
for (const k in w) …;            // enumerated the child's interface members
const L = w.location;
L.href;                          // the child's full address, no error
L.pathname; L.origin; L.reload(); // same, plus a callable
```

**The fix.** One cross-origin predicate drives every reflection seam; see
`ARCHITECTURE.md`, Phase 56, for the mechanics.

- `dom::cross_origin_own_keys` (DomWindowBindings.cpp, declared in
  `DomBindings.hpp`) answers for a cross-origin `Window`/`Location` and fills
  the list `[[OwnPropertyKeys]]` must return: the child navigable indices
  ascending, then (when the caller wants non-enumerable names too) the
  cross-origin table names in spec order, then `then`. Every reflection site
  consults it instead of (or before) the ordinary walk: `Object.keys`/
  `values`/`entries`/`assign`, `getOwnPropertyNames`/`getOwnPropertyDescriptors`,
  `Reflect.ownKeys`, `getOwnPropertyDescriptor`, `for…in`, spread, object
  rest, `JSON.stringify`.
- `descriptor_for` answers for a cross-origin object with spec-shaped
  descriptors: indices are enumerable data descriptors holding the child
  proxy; `then` is a configurable, non-enumerable `undefined`; an attribute
  entry of the table is an accessor descriptor with a native get wrapper (and,
  for `location`, a set wrapper); a method entry is a configurable,
  non-enumerable data descriptor holding the child's function.
  `Object.defineProperty`/`Reflect.defineProperty` on a cross-origin object
  always throw `SecurityError` before reading the descriptor (HTML
  §7.2.3.6/§7.2.4.6).
- `Location` gained `LocationExotic`: cross-origin it exposes only `then`,
  `href` (set, not get — reading throws `SecurityError`) and `replace()`.
- `has()` reports only indices, table names, `then` and named child navigables;
  `delete` throws `SecurityError`; `Object.freeze`/`seal`/`preventExtensions`
  throw `TypeError`; `setPrototypeOf` refuses (`Reflect` answers `false`);
  `getPrototypeOf` answers `null`; `instanceof` is `false`; `for…in`
  enumerates only the indices.
- `location` joined the cross-origin attribute safelist it was missing
  (`win.location = url` already navigated the child via the set path; reading
  it threw `SecurityError` in error, and `'location' in win` was `false`).

**Verification.** `tests/cross_origin_windows.cpp` — **80 checks** over a
parent realm at `https://app.example.test` and a cross-origin child at
`https://other.example.test` (served from a temporary origin root; the child
has one named `srcdoc` grandchild), run by `make test`; `tests/test_suite.cpp`
is untouched here. Full merged-tree verification (Release): `make test`
**198/198**; `js-conformance` **999/1014**; `dom-conformance` **669/669**;
`xml-conformance` **1718/1718**; `url-conformance` **1188/1188**; `wpt`
**7998/8009** (11 fail as listed, 0 unlisted).

**Still open.** Cross-document navigation (`childWin.location = url` with a
non-fragment URL — `navigate()` is fragment-only by design); the three symbol
keys in `Reflect.ownKeys` (no symbol key is reported anywhere in the engine);
`in` on a non-exposed name answers `false` rather than throwing; a
`[[CrossOriginPropertyDescriptorMap]]` cache (wrapper identities differ from
the child's functions); `MessageChannel`/`MessagePort`; process-level site
isolation.

---
