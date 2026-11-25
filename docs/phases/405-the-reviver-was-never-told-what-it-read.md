# Phase 405 — the reviver was never told what it read

Finding 405. Task: the half of "JSON.parse source text access" that Phase 393
recorded as still open. Phase 393 added `JSON.rawJSON` and found that it was
only half useful: `JSON.rawJSON` makes an object `JSON.stringify` writes
verbatim, but a reviver had no way to *get* the text to feed it.

## 1. What was wrong

`JSON.parse(text, reviver)` called the reviver with two arguments. ES2025
§25.5.1 and §25.5.5 give it three: the third is a context Record with one
field, `source`, holding the text that value was written as.

What that is for is the value a `double` cannot hold:

```
$ node -e 'JSON.stringify(JSON.parse("{\"a\":1e1000}", function (k, v, c) {
  return typeof v === "number" ? JSON.rawJSON(c.source) : v; }))'
{"a":1e1000}
```

Without `source`, `1e1000` is `Infinity` by the time the reviver sees it, and
`JSON.stringify` writes `null`. In this engine the same expression threw
`TypeError: JSON.rawJSON is not a function` — no, worse: before Phase 393 there
was no `rawJSON`, and after it there was still no `c`, so `c.source` was a
`TypeError` on `undefined`.

The row did not name this. It was found by re-reading Phase 393's own
"still open" list and checking each item against Node, which is the cheaper
form of the same habit as the phase before it: **what a phase records as
still open is a work list, and it should be re-read before picking a new
row.**

## 2. What changed

`src/js/BuiltinsJson.cpp`, and nothing else.

- `ParsedSource` — the span of text a value was written as, plus whether the
  value is a primitive. Hoisted out of `JsonParser` because the reviver walk
  needs it.
- `JsonParser::parse_value` records that span for every value it returns, and
  `parse`, `parse_array` and `parse_object` file it under the holder it went
  into and the key it went in as — `("1", index)` for an array element,
  `(object, key)` for a member.
- `internalize` looks the pair up and passes a context as the reviver's third
  argument.

The key is the **holder and the key**, not the value's identity. A primitive
has no identity to key on; the property that holds it does, and that pair is
exactly what `InternalizeJSONProperty` already walks by.

## 3. How it was verified

`tests/suites/405-json-reviver-source.cpp`, 33 assertions, every expected
value Node v22.22.3's by evaluating the same expression there.

```
$ ./build/test_suite
All 463 engine test suites passed successfully!
```

**Red on three engines, each failing a different assertion:**

| Wrong engine | Fails at | What it says |
|---|---|---|
| no third argument (the engine as it was) | `405-json-reviver-source.cpp:28` | `arguments.length` is 2 |
| `source` on every value, containers included | `:63` | `'source' in c` is true for `[]`'s element |
| the root's `source` is the whole input, not the value's span | `:56` | `" 1 "` where Node says `"1"` |

The second is the one a reader is most likely to get wrong: §25.5.1 makes the
Record for a value that is **not an Object**, so `JSON.parse('{"a":1}', …)`
gives the root `{"source":"{\"a\":1}"}` but gives `a` — a number — a source and
the *container* none. An implementation that hands every value a `source` is
wrong in a way no "does `c.source` exist" test notices, which is why the suite
asserts `!('source' in c)` on a nested array and a nested object.

Two of the three assertions that caught the fakes were themselves wrong first,
and Node corrected them: `[1]`'s element 0 *is* a primitive and *does* have a
source, and the reviver visits the root too, so `[true,false]` yields three
entries and the last is empty. Both were fixed from Node's answer rather than
from reasoning about the code.

## 4. What is still open

- **`JSON.rawJSON` objects from a reviver** are honoured, because `stringify`
  already asks the slot — but the *other* direction of the proposal, a reviver
  that receives its own `rawJSON` object back through `JSON.parse`, is not
  separately asserted.
- **The source map is a `std::map` in the parse call**, rebuilt per call. For a
  large document that is a `std::string` per primitive. It is not measured;
  if `JSON.parse` with a reviver ever shows up in a profile, this is why.
- **Duplicate keys**: `{"a":1,"a":2}` records the span of the second `a` and
  both entries collapse to one map slot, which is the right answer for the
  value that survives — but it is the last writer that wins in the map only
  because it is inserted last, and nothing asserts it.
- `Array.fromAsync` is still absent, and `Promise.try` still is (as it is in
  Node v22.22.3).
