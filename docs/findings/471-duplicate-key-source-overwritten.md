# Finding 471 — duplicate key source was overwritten

Phase 471. Status: fixed; reviver's `context.source` for a duplicate key keeps the first span.

## What was wrong

`JSON.parse('{"a":1,"a":2}', reviver)` should give the reviver for `a` a `source` of `"1"` — the text that established the property. The engine gave `"2"` because `JsonParser::remember` unconditionally overwrote the `SourceMap` entry for `(holder,key)`:

```cpp
m_sources[SourceKey{id,key}] = last_source; // last wins
```

A JSON object with duplicate keys is legal text; the second member replaces the first member's value, but the property's origin in the text is the first occurrence. The standard's `InternalizeJSONProperty` walk keys the source by holder and key *as created*, not as overwritten. Node's `JSON.parse` keeps the first span; this engine's `std::map` insertion order made the second win, which is observable only through the reviver's third argument.

## What changed

One branch in `src/js/BuiltinsJson.cpp`:

```cpp
SourceKey k{id,key};
if (m_sources.find(k) == m_sources.end()) m_sources[k] = last_source;
```

First insertion wins. No duplicate key: no change. Root (`""`) has no duplicate.

## How it was found

Re-reading Phase 405's still-open list: "a duplicate key's span wins by insertion order (unasserted)" — checking Node showed `{"a":1,"a":2}`'s reviver source is `"1"`, while the engine gave `"2"`. The existing 405 suite does not cover duplicate keys, so the overwrite was invisible.

## Verification

- New suite `471-json-reviver-duplicate-key` — duplicate, triple duplicate, nested object duplicate, non-duplicate control.
- Existing `405-json-reviver-source` unchanged.
- `make test` 415/415.

## Still open

- Per-primitive `std::string` copy per call (performance, unmeasured).
- `Array.fromAsync`/`Promise.try` absent.
