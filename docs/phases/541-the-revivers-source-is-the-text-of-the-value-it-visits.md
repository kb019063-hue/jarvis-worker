# Phase 541 — the reviver's source is the text of the value it visits

The JSON.parse-reviver row carried two open clauses: a duplicate key's span
"wins by insertion order (unasserted)", and the span map is "rebuilt per call
with a `std::string` per primitive (unmeasured)". Both are closed here, and
the first turned out to be a correction of a merged phase, not a new pin.

## The duplicate key's span is the last one written

Phase 471 wrote the suite `json_reviver_keeps_first_span_on_duplicate_keys`
and asserted that `{"a":1,"a":22}` tells the reviver value 22 with source
"1" — the first occurrence's text — "matches Node v22.22.3". Measured on
that exact version today:

```js
JSON.parse('{"a":1,"a":22}', (k, v, c) => c.source)   // "22"
JSON.parse('{"a":1,"a":2,"a":3}', (k, v, c) => c.source)  // "3"
JSON.parse('{"a":null,"a":5}', (k, v, c) => c.source)     // "5"
```

Node answers the **last** writer's span, in every shape the 471 suite covered
(duplicates, triples, nested objects, separate holders) and in the ones it
did not (a null first writer; a string keeping its quotes). The reason is
structural, not a choice: the parse gives the property the last writer's
value, the reviver is visited once per property with that value, and its
context says what *that* value was written as. First-wins made the reviver a
record of something it never saw — value 22, "written as 1".

`remember` now overwrites, the 471 suite's expectations are rewritten from
fresh probes, and its name says the rule it now pins
(`json_reviver_keeps_the_last_span_on_duplicate_keys`). A discarded earlier
object leaves its spans keyed by its own identity, which no walk visits —
the map grows by the duplicates a document contains, and answers nothing
about them.

## The recording is skipped when nobody reads it

`JSON.parse` without a reviver built the whole span map anyway — one string
per key and per primitive, per parse. §25.5.5's own step order reads the
text, asks whether the reviver is callable, and only then parses, so the
parser can be told up front whether anyone will ever look:

```cpp
const bool with_reviver = reviver.is_function();   // §25.5.5 step 2
JsonParser parser(text, with_reviver);
```

A 2.1 MB document (100,000 rows, 200,000 primitives) parsed ten times
measured **4,570 ms** with the recording on and **about 1,900 ms** without;
a parse with a reviver keeps the recording and is unchanged within noise
(the walk, not the map, is its cost).

## The suite

`tests/suites/541-the-revivers-source-is-the-text-of-the-value-it-visits.cpp`
pins the two invariants the wrong rule broke, both written out of Node first:
every primitive's `JSON.parse(c.source)` equals the value it came with (under
first-wins the duplicate case read value 22, "written as 1" — a disagreement
no correct rule can produce), and a parse without a reviver returns exactly
the document the reviver walk returns while a parse with one still finds its
sources. It ends with the use the whole Record exists for: a duplicate
`1e1000`/`1e999` pair round-trips through `JSON.rawJSON(c.source)` as
`{"a":1e999}` — the last one's text. Under the old rule both new suites fail
on their first assertion; `make test` is **570/570** with the fix.
