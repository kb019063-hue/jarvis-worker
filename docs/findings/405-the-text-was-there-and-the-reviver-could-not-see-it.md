# Finding 405 — the text was there and the reviver could not see it

Phase 405. Status: **fixed; a reviver gets `{ source }` as its third
argument.**

## 1. What was wrong

`JSON.parse(text, reviver)` called the reviver with `(key, value)`.
ES2025 §25.5.1 gives it `(key, value, context)`, where `context` is a Record
with one field, `source`: the text the value was written as.

The engine had the text the whole time — its parser reads it character by
character and could have kept the span — and simply did not pass it on. That
made Phase 393's `JSON.rawJSON` useless for the case it exists for: a number
that is not a finite `double` parses to `Infinity`, and only the original text
can write it back out.

## 2. What changed

The parser records, for every value it finishes, the slice of source between
first and last character, filed under the holder it was put into and the key it
was put in as. `internalize` looks that pair up and builds the context object.

Keyed by **holder and key**, not by the value: a primitive has no identity.

## 3. The asymmetry

§25.5.1 makes the Record only for a value that is not an Object. So:

| Value | Context |
|---|---|
| a number, string, `true`/`false`/`null` | `{source: "<the text>"}` |
| an object, an array | `{}` |
| the root, when it is a primitive | `{source: "<the whole value's text>"}` |
| the root, when it is an object | `{source: "<the whole text>"}` |

The last two are why the root is built separately, from the whole input after
trimming — `JSON.parse(' 1 ', …)` gives `"1"`, not `" 1 "`. An implementation
that gives every value a `source` is wrong in a way that no "does `c.source`
work" test notices, so the suite asserts the absence on a nested array and a
nested object.

## 4. What remains wrong

- The `std::map` of sources is built per `JSON.parse` call and holds a
  `std::string` per primitive. Unmeasured; the place to look first if a
  reviver parse ever shows up in a profile.
- A duplicate key (`{"a":1,"a":2}`) records the later span, by insertion order.
  Right answer, but an accident of `std::map` insertion and not asserted.
- A value the reviver replaces keeps its entry until the call returns. Harmless
  (the map dies with the call) and not measured.
- `Array.fromAsync` and `Promise.try` are still absent, as recorded in Phase
  387's finding.
