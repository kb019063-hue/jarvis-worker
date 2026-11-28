# Finding 541 — the suite cited Node v22.22.3 against Node v22.22.3

Phase 471's suite asserted that a duplicate JSON key keeps the **first**
occurrence's span, in a file whose header said so twice and said it matched
Node v22.22.3. Node v22.22.3 answers the **last**. Every duplicate-key
assertion in that suite was a behaviour the version it named refuses, and
five of its eight shapes were measured by probing that Node directly this
week and watching it answer the other way.

## How a claim like that gets written

The open row this came from said the duplicate key's span "wins by insertion
order (unasserted)" — which has two readings. The map's insertion order
(first recorded, never overwritten) is one; the text's insertion order (the
last writer, because that is who wins the property) is the other. Phase 471
took the first reading, wrote the suite to it, and stamped it
"matches Node v22.22.3". Nothing in the file quotes a probe, so the claim
cannot be checked from the file — and checking it takes thirty seconds:

```console
$ node -e 'JSON.parse("{\"a\":1,\"a\":22}", (k,v,c) => console.log(k, v, c.source))'
a 22 22
```

That is the lesson, and it is Phase 415's ("two of the first three
assertions written for it were themselves wrong and Node corrected them")
with a sharper edge: there, Node corrected assertions *before they merged*,
because they were written against Node first. Here the suite merged having
never met its oracle. **An assertion that cites a measurement should quote
the probe that made it** — one line, reproducible, and the reader can watch
it fail. Everything else is a claim about a conversation nobody had.

## Why it survived

Nothing since Phase 471 parsed a duplicate key under a reviver, so the wrong
rule ran unobserved for a hundred phases. The row knew the assertion was
missing ("unasserted" was its own word) — what it did not know was that the
assertion *existed* and asserted the opposite of its oracle. An unasserted
behaviour and a wrongly asserted one look identical from a row's distance;
the difference is one merged file, which is why the check for "is this
already pinned somewhere?" has to read the pin, not the index.

## What the rule actually is, and why it was never a choice

The reviver is visited once per property, with the value the property holds.
On `{"a":1,"a":22}` the property holds 22 — the first writer's 1 is gone
before any script can observe it. A context Record that says "22, written as
1" is describing a value the program cannot see. The last-wins rule is not
one interpretation among two: the first-wins rule has no reading of the spec
under which its source names the value the reviver receives. The one
deliberate part is the map's shape: a discarded earlier object leaves its
spans keyed by an identity no walk visits, which costs the duplicates a
document contains and nothing else.

The sibling clause — the span map built even when no reviver would read it —
closed the same day with §25.5.5's own step order as permission, and the
measurement (4,570 → about 1,900 ms for ten 2.1 MB parses) is in the phase
doc. That half needed no finding; this one did, because the defect was not
in the engine's answers to scripts — it was in a suite's answer to the
people who would read it next.
