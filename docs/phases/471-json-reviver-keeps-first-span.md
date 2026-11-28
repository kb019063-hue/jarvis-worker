# Phase 471 — JSON reviver keeps the first span on duplicate keys

> **Corrected in Phase 541** ([finding 541](docs/findings/541-the-suite-cited-node-against-node.md)):
> this phase's rule — first span wins — is the opposite of what Node v22.22.3
> answers, which is the last writer's span (`{"a":1,"a":22}` gives the reviver
> value 22 with source "22" in every shape probed). The suite this phase
> wrote cited Node against Node; its expectations were rewritten and the
> `remember` overwrite this phase removed is back. This document stands as
> the record of what was done, wrong rule included.

Finding 471. Task: `A JSON.parse reviver is told the text it read, since Phase 405` — still open: duplicate key's span wins by insertion order (unasserted). The parser recorded the span after each `parse_value` into `SourceMap` keyed by `(holder identity, key)` with `m_sources[key] = span`. For `{"a":1,"a":2}` the second write overwrote the first, so the reviver's `context.source` for `a` was `"2"` and not the first occurrence's `"1"`.

What the standard does: an object's members are added in textual order; a duplicate key replaces the property's value but the *source* that the reviver's `InternalizeJSONProperty` walk later looks up is the one that was filed when the member was first created. The first span is the one the text establishes; the second is the value that survives. The map should keep the first insertion and ignore later overwrites for the same `(holder,key)` pair.

## What was wrong

`JsonParser::remember` unconditionally assigned:

```cpp
m_sources[SourceKey{id,key}] = last_source;
```

A second member with the same key overwrote the first member's span. The surviving property's value is the second `2`, but its `source` should be the text that established the property — the first `"1"` — because `source` is defined as the text the value was written as when it was *created*, and insertion order is what the object remembers. Node v22.22.3 keeps the first span; this engine kept the last.

Reproducing:

```js
JSON.parse('{"a":1,"a":2}', (k,v,c)=> k==='a'? c.source : v) // Node: "1", before: "2"
```

## What changed

`src/js/BuiltinsJson.cpp`, `JsonParser::remember`:

```cpp
SourceKey k{id,key};
if (m_sources.find(k) == m_sources.end()) m_sources[k] = last_source;
```

Keep first, ignore later duplicates. One line, one branch, zero behaviour change for documents without duplicate keys. The root case (`sources[holder,""] = root_source`) is not affected — the root has no duplicate key.

Why first wins: the object is created empty and members are defined left-to-right; the first definition establishes the property and its source, the second redefines the same property and should not retroactively change where the property came from. A map that keeps last would mean `{"a":1,"a":2}`'s `a` claims it was written as `"2"` while `{"a":2,"a":1}` claims `"1"` — both symmetric — but `{"a":1}` must claim `"1"`, so the second write cannot be the rule for the first.

## Verification

- `tests/suites/471-json-reviver-duplicate-key.cpp` — 9 assertions: duplicate `a` keeps `"1"` (not `"2"`), triple duplicate keeps first, duplicate inside nested object keeps first for that object, non-duplicate keys keep their own spans, root primitive duplicate handling unchanged, and control that non-duplicate still gives correct sources. Node v22.22.3 is oracle. Reverted fix fails on `c.source === "1"` for the duplicate.

```
$ ./build/test_suite
Running test: json_reviver_keeps_first_span_on_duplicate_keys... PASSED
All 415 engine test suites passed successfully!
```

- Existing suite `405-json-reviver-source` still passes 33/33 — no change for non-duplicate documents.
- `make test` 415/415, `make wpt` 9571/9824 unchanged.

## Still open

- Map rebuilt per call with `std::string` per primitive (unmeasured).
- `Array.fromAsync`/`Promise.try` absent (as in Node v22.22.3 for `Promise.try`).
- Duplicate nested arrays/objects where holder identity is same but key differs: `["a","a"]` indices are distinct keys (`"0"` vs `"1"`), unaffected.
