# Phase 366 — an async function and a generator function are not ordinary functions

Task: the still-open half of Phase 278's row (§`AGENTS.md`, the poison-pill
row). Two things were named there: `%AsyncFunction.prototype%` and
`%GeneratorFunction.prototype%` were not separate objects —
`Object.getPrototypeOf(async function () {})` was `Function.prototype` and
`Object.prototype.toString` said `[object Function]` for both — and a *native*
function listed `name,length` where the standard defines `length` first.
Finding 366.

## 1. What was wrong

Three independent faults, all of which show through the same two questions.

**Nothing distinguished the three kinds of function.** `property_cell` gave
every script function `%Function.prototype%` as its prototype, whatever shape
it had:

```js
Object.getPrototypeOf(async function () {})   // was Function.prototype
Object.getPrototypeOf(function* () {})        // was Function.prototype
```

§20.2.3.2 and §27.3.3.2 give each kind a prototype of its own between the
function and `%Function.prototype%`. Without one there is no place for a kind's
tag to live, and `typeof` — which answers "function" for all three — leaves a
script no way to tell them apart.

**`object_tag` answered "Function" before it looked for a tag.** §20.1.3.6
step 15 asks `@@toStringTag` first and only then names the built-in tag. The
switch in `BuiltinsObject.cpp` returned `"Function"` for `JsType::Function` and
`JsType::NativeFunction` and never reached the shared `@@toStringTag` lookup
below it, so a tag planted anywhere would have been invisible anyway. This one
had to go first: it is what makes the other two observable.

**A native function's `name` was defined before its `length`.** §10.2.4 and
§10.2.5 define `length` first, and the order is observable through
`Object.getOwnPropertyNames`:

```js
Object.getOwnPropertyNames(Math.max)          // was ["name","length"]
Object.getOwnPropertyNames(Array.prototype.push)  // was ["name","length"]
```

## 2. What changed

`JsIntrinsics` gained two slots, `async_function_proto` and
`generator_function_proto`, built in `JsOps.cpp` right after
`%Function.prototype%` — both with it as parent, both `ObjectClass::Function`
but **not** functions themselves, because Node says
`typeof Object.getPrototypeOf(async function () {})` is `"object"` while
`typeof p.call` is `"function"`: the prototype is a callable exotic object, not
a language function.

`JsValue::function_kind_prototype` picks the slot from the `FunctionData` the
function was made with — `is_generator` first, then `is_async`, else the
ordinary prototype. `property_cell` asks it instead of naming
`%Function.prototype%`.

The installation in `JsInterpreter.cpp` gives each prototype its
`@@toStringTag` (non-writable, non-enumerable, configurable — Node's descriptor
exactly), a `constructor` back-link with one parameter, and, for the generator
kind only, the `prototype` object a generator function makes, tagged
"Generator". Neither constructor is a global: `typeof AsyncFunction` is
`"undefined"` in Node, so the only way to reach one is through the prototype.

`object_tag` now lets a function fall through to the `@@toStringTag` lookup and
answers "Function" only if nothing named a tag, which keeps an ordinary and a
native function exactly as they were.

`js_make_native` defines `length` before `name`.

## 3. What it was measured against

Every probe was answered by Node v22.22.3 first, and **three assertions were
written the wrong way round** before that and corrected:

- The kind prototypes are `typeof "object"`, not `"function"` — my first cut
  made them functions.
- `Object.getPrototypeOf(async function () {}).constructor` is reachable and
  named `AsyncFunction`, but `AsyncFunction` is not a global binding.
- This engine spells a symbol key as an `"@@name"` string (§`AGENTS.md`,
  "What an iterator still is not"), so `Object.getOwnPropertyDescriptor(p,
  '@@toStringTag')` answers here where Node wants the symbol. The suite asks
  the string form deliberately and says so.

## 4. The numbers

| corpus | before | now |
|---|---|---|
| `make test` | 426/426 | **427/427** |
| test262, all of it | 15,140 (96.5%) | **15,144 (96.5%)**, +4, 0 broken |
| `built-ins/Function` | 467/509 | **469/509** |
| `built-ins/Object` | 3,164/3,411 | **3,166/3,411** |
| `language` | 11,164/11,328 | 11,164/11,328 (unchanged) |
| `make js-conformance` | 1,150/1,158 | **1,152/1,158 (99.5%)** |

The four test262 cases gained are `built-ins/Function/property-order.js`,
`built-ins/Function/prototype/property-order.js`,
`built-ins/Object/property-order.js` (all three are the `length,name` order)
and `built-ins/Object/prototype/toString/symbol-tag-override-instances.js`
(the tag). Nothing was lost anywhere, and `language` — which the arguments
object phase broke 75 of by moving a prototype alone — is untouched.

## 5. The crash the landing session found

The first push crashed: `make wpt` on `dom/nodes/Document-createElementNS.html`
died of a segmentation fault (CI run 45357940902, and in the lander's own lot),
and so did the unit suite `generator_gc_reclaims_frames_and_threads` from
#363 — a suite this phase never touches. One fault, two faces.

**The cause was a missing GC root.** The realm keeps the objects it owns alive
by naming them in one explicit list (`m_roots`, in the `JsIntrinsics`
constructor). Every prototype is in it, because nothing else points at them.
`async_function_proto` and `generator_function_proto` were not: they were
allocated, given a parent and properties, and then left for the collector to
reclaim as unreachable. `make_object` on a collected cell reads freed memory,
so what followed was undefined — a large page that happens to collect garbage
(such as this WPT file) crashed, a small page did not, and an unrelated GC
suite crashed when the timing went another way.

```cpp
// The list the two new prototypes were missing from.
for (JsObject* cell : {object_proto, function_proto,
                       async_function_proto, generator_function_proto,   // added
                       array_proto, ...})
    m_roots.push_back(JsValue::make_object(cell));
```

Two corrections came out of finding it. The kind prototypes are plain objects,
not `ObjectClass::Function` — that class tells `make_object` to read the cell
as a function's cell, and these have no function behind them. And an earlier
guess of mine (that the class alone was the fault) was wrong: the binary had
silently not rebuilt, so the guess looked confirmed when it was not. The
bisect was only worth reading once every step checked that `Build complete`
had actually printed.

After the fix: `make wpt` 9,570/9,824 with 0 unexpected, `make test` 447/447
with `main` (including #363's suite), and the WPT page 596/596.

## 6. What is still open

- The two kind prototypes are not *callable*: `typeof p.call` is `"function"`
  in Node and `"undefined"` here. Nothing in the standard's own pages reaches
  for it, and no corpus case asks; it is recorded rather than modelled.
- `%AsyncGeneratorFunction.prototype%` (§27.4.3) — an async generator is still
  an ordinary generator here.
- A sloppy ordinary function still shadows `caller`/`arguments` with its own
  `null` data properties (Phase 278), which is what Node answers too.
