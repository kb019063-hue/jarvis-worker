# Finding 161 — an element write never asked the prototype

**Resolved by Phase 156** (`docs/phases/156-an-array-write-asks-the-prototype.md`).

## 1. What was found

`OrdinarySetWithOwnDescriptor` (§10.1.9.2) asks the prototype chain about a key
the object does not have. `JsValue::set_property` asked it for ordinary
properties and never for an array index: the element path wrote
`array_elements[index]` whenever the place was a hole or past the end, which
are exactly the two cases where the chain is the only thing that can answer.

```js
Object.defineProperty(Array.prototype, "0", { set(v) { log.push(v) }, get() { return "p" } });
var a = []; a[0] = 5;      // browser: the setter runs, a stays empty. here: the element was written.
```

Two test262 cases said so and failed — `Array/prototype/unshift/set-length-array-is-frozen.js`
and `Array/prototype/sort/precise-prototype-accessors.js`, the second with
`Expected SameValue(«undefined», «"set with 3"»)`.

## 2. What was done

The chain is asked when the array has no own property at the index — a hole or
past the end — and a setter found there runs with the array as `this`, the
place staying a hole. An accessor with no setter refuses the write. A data
property on a prototype index is left to the ordinary element write, because
its attributes are not stored (the neighbouring row) and a guess would make
`Object.freeze(Array.prototype)` change every later index write.

Gated on a counter of index-named accessors, so an ordinary program pays one
relaxed atomic load per element write: 3,000,000 writes to `a[0]` measured
860/895 ms on main and 873/914 ms after.

## 3. Evidence

`built-ins/Array`: 2,662 → **2,664 of 3,083**, the two gained being exactly
those cases, 0 lost. `language` 19,912/23,015 and `built-ins/Object`
3,018/3,411 unchanged on both builds. `make test` **314/314**; js-conformance
1144/1158; DOM trees 1973/1973; `make gc-oracle` PASS.

## 4. What is still open

- A non-writable (or writable) data property on a prototype index: it needs
  an index's attributes to exist first.
- `sort`'s own `Get`/`Set`/`DeleteProperty` order — `precise-comparefn-throws.js`
  and `precise-prototype-element.js` still fail.
