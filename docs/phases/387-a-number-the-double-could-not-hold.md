# Phase 387 — a number the double could not hold

Finding 387. Task: no open row names this. It was found by asking the built-in
namespaces for the members they do not have — see section 1 — and the row it
belongs to is the JSON one: `JSON.parse` reads source text access into a
reviver since Phase 288, and the other half of that proposal, `JSON.rawJSON`
and `JSON.isRawJSON`, was not there.

## 1. What was wrong

`typeof JSON.rawJSON` and `typeof JSON.isRawJSON` both answered `"undefined"`.
They are ES2025 §25.5.3.1 and §25.5.3.2 — Stage 4, the "JSON.parse source text
access" proposal — and both are in Node v22.22.3.

What they are for is a value the engine cannot represent:

```
$ node -e 'console.log(JSON.stringify(1e1000), JSON.stringify(JSON.rawJSON("1e1000")))'
null 1e1000
```

`1e1000` is not a finite `double`, so `JSON.stringify` writes `null` for it and
the number is gone. `JSON.rawJSON` makes an object that carries the *text*, and
`JSON.stringify` writes that text as it stands.

**How it was found.** Not by reading `BuiltinsJson.cpp` for what looked
missing, but by asking the engine for the complement: a list of the standard's
own member names for `Object`, `Reflect`, `Math`, `Number`, `String`, `Array`,
`JSON` and `Promise`, and a report of every name that was neither an own
property nor answered for. Four names came back:

| Namespace | Missing |
|---|---|
| `Array` | `fromAsync` |
| `JSON` | `rawJSON`, `isRawJSON` |
| `Promise` | `try` |

The last one is not a defect: Node v22.22.3 answers `typeof Promise.try` with
`"undefined"` too, so this engine agrees with the oracle and the list was
wrong. Of the other three, `JSON.rawJSON` is the one that is both Node-checkable
and self-contained — no promises, no async iteration — so it is this phase.

## 2. What changed

**`src/js/BuiltinsJson.cpp`** — the two methods, and the walk that writes them.

`JSON.rawJSON(text)` converts its argument to a string, holds that string to
§25.5.1.1's grammar, and returns an object whose prototype is `null`, carrying
one non-writable non-configurable enumerable `rawJSON` property, sealed and
frozen. The grammar is not "any JSON value": an *object* and an *array* are
refused. `JsonParser` would accept both, so the number is checked by hand
against the JSON number production and the string case is the one the parser
is used for.

The text is held in the cell's `internal` slot, with a new boolean beside the
cell's other flags (ninth bool, in the padding before `klass` — `JsObject` does
not grow). `JSON.isRawJSON` asks the slot and nothing else, and
`JsonWriter::write_value` writes the slot's text instead of serialising it.

## 3. How it was verified

`tests/suites/387-json-raw-json.cpp`, 50 assertions, every expected value
Node v22.22.3's by evaluating the same expression there.

```
$ ./build/test_suite
All 450 engine test suites passed successfully!
```

**Red on three engines, each failing a different assertion:**

| Wrong engine | Fails at | What it says |
|---|---|---|
| both methods absent | `387-json-raw-json.cpp:28` | `typeof JSON.rawJSON` is not `"function"` |
| `isRawJSON`/`stringify` read a `rawJSON` property instead of a slot | `:68` | `JSON.isRawJSON({rawJSON: '1'})` is `true`, Node says `false` |
| the argument checked by the general parser ("any JSON value") | `:90` | `JSON.rawJSON('{}')` is accepted, Node says `SyntaxError` |

The second is the one that matters. A property-shaped implementation answers
every other assertion in the file correctly — the text comes out, the descriptor
is right, the proto is `null` — and the only thing that separates it from the
standard is that a script can forge it. That is why the suite asserts on a
forged object, an inherited one, one given a `%`-prefixed property, and on
`Object.assign({}, JSON.rawJSON('1'))` spreading to a plain object.

## 4. What is still open

- **`Array.fromAsync`** — the other name the sweep found. It needs async
  iteration, which is a row of its own.
- **`Promise.try`** — absent in Node v22.22.3 as well, so not asserted and not
  built. TC39 has it at Stage 2.7; this engine is not ahead of the oracle.
- **`JSON.parse` source text access** — the same proposal's other half:
  `JSON.parse(text, reviver)` with a third `context` argument carrying
  `{ source }` for primitives, and `JSON.rawJSON` objects returned from a
  reviver. Not attempted here.
- The sweep covered eight namespaces' method names only. It said nothing about
  accessors, symbol-keyed members, or the typed-array statics.
