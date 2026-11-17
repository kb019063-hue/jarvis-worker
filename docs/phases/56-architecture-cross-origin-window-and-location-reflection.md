# Phase 56 Architecture: Cross-Origin `Window` and `Location` Reflection

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 56 Architecture: Cross-Origin `Window` and `Location` Reflection` section.
> The text below is the section as it stood, unchanged.


### 1. What Phase 42 left open

Phase 42 made a cross-origin `WindowProxy` refuse to read, write or invoke what
it may not: `WindowExotic::get`/`set` throw `SecurityError` before consulting
the child's own properties, the scope or the prototype chain, and the indexed
child navigables and named children stay reachable (finding 48). Everything
that *describes* the object was still the ordinary path, though:

- `Object.getOwnPropertyNames(win)` listed the child's ordinary own properties
  (its interface members are on the prototype, so it listed scripts' globals
  and whatever had been remembered), not the spec's cross-origin list;
  `Reflect.ownKeys` and `Object.keys` were the same.
- `Object.getOwnPropertyDescriptor(win, 'length')` etc. describe what is
  stored, not what a cross-origin caller may see.
- `childWin.location` was a plain Iface-branded object: a parent could read
  `loc.href`, `loc.pathname`, `loc.origin` — leaking the child's address —
  and `loc.reload()` was callable. No origin check existed anywhere in its
  member list.
- `Object.defineProperty(win, ...)` and `Reflect.defineProperty` wrote straight
  into the child's own property map.
- `Object.getOwnPropertyNames(win).indexOf('gamma')` hinted at ids of the
  child; `for…in`, spread, rest and `JSON.stringify` keys were all derived
  from the ordinary own-keys walk.
- `Object.getPrototypeOf(win)` returned the child realm's `Window.prototype`
  (browsers answer `null`), so `for…in` enumerated the interface's
  members and `win instanceof Object` walked into the child.

### 2. One predicate, every reflection seam

The whole phase hinges on a single new helper, declared in
`include/litebird/js/DomBindings.hpp` and defined in `src/js/DomWindowBindings.cpp`:

```cpp
bool dom::cross_origin_own_keys(const JsValue& object, bool include_non_enumerable,
                                std::vector<std::string>& out);
```

It returns `false` when `object` is not a `Window`/`Location` proxy whose
`realm` is not same-effective-origin with the caller (`same_effective_origin`,
unchanged), and `true` otherwise, filling `out` with HTML §7.2.3.10's
`[[OwnPropertyKeys]]` result, ascending:

1. the indices of the document-tree child navigables (`0`…`length-1`); then,
   only when `include_non_enumerable` is asked (the engine maps that flag to
   `own_keys(true)`, what `Reflect.ownKeys`, `getOwnPropertyNames` and
   `for…in`'s seen-set use);
2. the cross-origin table names in spec order — `window`, `self`, `location`,
   `close`, `closed`, `focus`, `blur`, `frames`, `length`, `top`, `opener`,
   `parent`, `postMessage` for a window, or `href`, `replace` for a location;
3. `then`.

(The spec's three symbol keys — `%Symbol.toStringTag%`, `%Symbol.hasInstance%`,
`%Symbol.isConcatSpreadable%` — are part of `CrossOriginOwnPropertyKeys`, but
no symbol key is reported anywhere in this engine, so none is reported here
either.) The named-property set of the child navigables deliberately does not
appear: it drives named access, not `[[OwnPropertyKeys]]`.

Every seam that reports keys or descriptors was switched to it:

- `BuiltinsObject.cpp`: a local `reflected_own_keys` wraps all reflection
  calls (`Object.keys`/`values`/`entries`/`assign`, `getOwnPropertyNames`,
  `getOwnPropertyDescriptors`, `Reflect.ownKeys`), and `descriptor_for`
  answers for a cross-origin object before anything else.
- `descriptor_for` shapes the descriptors: indices are data descriptors
  (`writable: false, enumerable: true, configurable: true`) holding the child
  proxy; `then` is `{ value: undefined, enumerable: false, configurable: true }`;
  an attribute entry of the table is an **accessor descriptor** whose `get` is
  a native reading-sequence wrapper (HTML §7.2.1.3.4's crossOriginGet) and
  whose `set` exists only where the spec has one (`location`, and the Location
  `href` entry, whose descriptor has *no getter* cross-origin); a method
  entry (`close`, `closed`…) — a data descriptor whose value is the child's
  own function, configurable and non-enumerable. The engine has no
  caller-realm anonymous built-in to put there; that substitution is the one
  place the shape is not byte-identical to the spec.
- `define_from_descriptor` refuses a cross-origin object with
  `SecurityError` before reading the descriptor (HTML §7.2.3.6 / §7.2.4.6:
  `[[DefineOwnProperty]]` always throws; `Reflect.defineProperty` does not
  return `false`).
- `Object.getPrototypeOf` / `Reflect.getPrototypeOf` answer `null`;
  `Object.setPrototypeOf` / `Reflect.setPrototypeOf` refuse with `TypeError` /
  `false`; `Object.freeze` / `Object.seal` / `Object.preventExtensions` throw
  `TypeError` (`[[PreventExtensions]]` is `false`).
- `js_instance_of` answers `false` for a cross-origin object (no prototype to
  walk). `for…in` sees only the indices (the prototype walk is skipped), and
  spread, `Object.assign`, object rest destructuring and `JSON.stringify`
  gather only the indices.
- `WindowExotic::has` reports only the indices, the table names, `then` and
  the named child navigables; `WindowExotic::remove` and
  `LocationExotic::remove` throw `SecurityError` (HTML §7.2.3.9 / §7.2.4.9).
- The `location` attribute is in the cross-origin safelist now (it always
  needed Get+Set and was missing from `is_cross_origin_safelisted`; the
  `set` path already special-cased it).

### 3. `Location` became an exotic

`Location` is created as a branded ordinary object in `setup_window`; it is
now handed a `LocationExotic` at the same place (cached on the child window,
so one `Location` per window as before). Cross-origin it answers only what
the standard exposes: `then` reads as `undefined`, `replace()` reaches the
ordinary method (which navigates the child — a fragment move, as everywhere
in the engine), `href` may be **written** but **read throws**
(`SecurityError`, since the cross-origin `href` entry carries no getter),
and `has()` answers only `href` / `replace` / `then`. Same-origin callers
see none of this: every hook returns false and the ordinary accessors run
unchanged.

### 4. Still open, honestly

- `childWin.location = url` and `loc.replace(url)` with a cross-document URL
  are no-ops — `navigate()` is fragment-only by design (Phase 42); nothing
  here changes that.
- `%Symbol.toStringTag%`, `%Symbol.hasInstance%`, `%Symbol.isConcatSpreadable%`
  are not reported by `Reflect.ownKeys`; no symbol key is, anywhere in the
  engine.
- `'x' in win` for a non-exposed `x` answers `false` instead of throwing
  `SecurityError` — the `in` path cannot throw through this engine's
  boolean `has()`.
- Other boxes on the `WindowProxy`/`Location` security table —
  `MessageChannel`/`MessagePort`, out-of-process site isolation, and a
  cached `[[CrossOriginPropertyDescriptorMap]]` (the spec memoizes the
  wrapper functions; ours create one per `getOwnPropertyDescriptor` call,
  identity differs from the child's function) — remain open.
- Named child navigables are reachable by name but do not appear in
  `[[OwnPropertyKeys]]`, per the spec; `Location`'s `ancestorOrigins`,
  `assign()`, `reload()` stay unexposed cross-origin.
- `Object.prototype.hasOwnProperty.call(win, 'length')` answers correctly;
  `w.hasOwnProperty('length')` itself throws, because reading the method
  cross-origin is not exposed.

### 5. Verification

`tests/cross_origin_windows.cpp` (new, standalone binary built and run by
`make test`) loads a parent at `https://app.example.test/index.html` with a
cross-origin child at `https://other.example.test/child.html` (served from a
temporary directory through `ResourceLoader::set_origin_root_for_testing`),
the child with one named `srcdoc` grandchild. It asserts **80 checks**: the
two-name `[[OwnPropertyKeys]]` lists, every descriptor shape, the
`SecurityError` table for read/write/delete/define, the integrity-level and
prototype refusal, `for…in`/spread/`Object.assign`/JSON containment, and the
same-origin control (a `srcdoc` iframe, untouched). `tests/test_suite.cpp`
is untouched (another open PR has it).

Merged-tree verification (this branch, Release): `make test` — **198/198**
suites + the 80 cross-origin checks; `js-conformance` **999/1014** (Everyday
**90/90**); `dom-conformance` **669/669** (Everyday **68/68**);
`xml-conformance` **1718/1718**; `url-conformance` **1188/1188**; `wpt`
**7998/8009**, 11 fail as listed, 0 unlisted.

---
