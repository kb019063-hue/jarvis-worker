# Finding 387 — the number was a double, and the text was lost

Phase 387. Status: **fixed; `JSON.rawJSON` and `JSON.isRawJSON` exist.**

## 1. What was wrong

`JSON.stringify(1e1000)` answers `"null"`, and it is right to: `1e1000` is
`Infinity` as a `double`, and JSON has no infinity. The value the script wrote
is unrecoverable by the time `stringify` sees it. ES2025's answer is
`JSON.rawJSON`, which carries the *source text* of a number so that
`JSON.stringify(JSON.rawJSON("1e1000"))` is `"1e1000"` — and neither it nor
`JSON.isRawJSON` existed. `typeof JSON.rawJSON` was `"undefined"`.

Two further facts about the discovery, because they are the reusable part:

- The complement was asked for, not the presence: eight namespaces' member
  names against `typeof`, reporting what answered `undefined`. Four names came
  back and one of them — `Promise.try` — is absent in Node v22.22.3 too, so the
  list was wrong about it. **A sweep's hits still have to be taken to an
  oracle; the sweep only says what this engine lacks, not what the standard
  has.**
- `JSON.rawJSON` is one of the few built-ins where the obvious implementation
  is *wrong in a way every straightforward test passes*. See section 3.

## 2. What changed

`src/js/BuiltinsJson.cpp` and one boolean in `include/litebird/js/Heap.hpp`.

The text lives in the cell's `internal` slot with a flag, not in a property.
`isRawJSON` and `stringify` both ask the slot. `rawJSON` is *also* an own
property of the object — because Node reports one — but it is a report of the
slot, not the slot itself.

## 3. The trap

§25.5.3.1 calls it `[[IsRawJSON]]`: an **internal slot**. A plain object with
a `rawJSON` property of its own is not one. Node is unambiguous:

```
JSON.isRawJSON({rawJSON: "1"})                    // false
JSON.stringify({a: {rawJSON: "1"}})               // {"a":{"rawJSON":"1"}}
JSON.isRawJSON(Object.create({rawJSON: "1"}))      // false
JSON.isRawJSON(givenProperty("%RawJSON", "1"))     // false
JSON.stringify(Object.assign({}, JSON.rawJSON("1")))  // {"rawJSON":"1"}
```

An implementation that reads the property produces the right answer for every
object `JSON.rawJSON` itself made, and a forgery for everything else. It passed
a first draft of this suite; the assertions in section 4 of the phase write-up
are what catch it, and they are the reason this engine has a flag on the cell
rather than a lookup.

## 4. What remains wrong

- `Array.fromAsync` is still absent (needs async iteration).
- `Promise.try` is absent, and absent in Node v22.22.3 — deliberately not
  built.
- `JSON.parse(text, reviver, context)` and the `{ source }` a reviver may be
  given are not implemented: that is the other half of the same proposal.
- The slot is a flag plus a `shared_ptr` in `internal`. A GC audit would be
  the way to show the text is traced and released; nothing here proves it
  beyond the suiterunning clean under the sanitizer build.
