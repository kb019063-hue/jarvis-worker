# Finding 412 — `Array.fromAsync` was only ever a promise away

**Severity:** a standard method absent entirely (`typeof Array.fromAsync` was
`"undefined"`). Not a wrong answer — no answer. Every `async` idiom that reaches
for it — reading an async iterable into a list, awaiting a stream of promises in
order — had to be written by hand in script.

**Found by:** the row "A script can read and write the engine's own slots",
which lists `Array.fromAsync` and `Promise.try` among the things still absent.

## What it was

`Array.from` existed (§23.1.2.1, Phase 94's work); `Array.fromAsync` did not.
`Symbol.asyncIterator` was already installed, async generators already worked,
and `await` already knew how to block — so everything the method needs was
present, and nothing used it.

## What the method needs, and what the engine already had

§23.1.3.1 is a map over five ideas, four of which this engine already had:

| the spec asks for | what the engine has |
|---|---|
| `GetMethod(asyncItems, @@asyncIterator)` | a plain property read, then a callable check |
| `Call(next, iterator)` and await the step | `await`, which off the interpreter is a blocking loop |
| await each value, then map, then await the mapper's result | the same primitive, twice |
| `CreateDataPropertyOrThrow` for each place | `create_data_property_or_throw`, which `Array.from` already uses |
| reject on failure | `Promise::make_rejected`, from a caught `JsThrow` |

The one thing that did not exist was the *use* of the blocking await outside
`eval_await` — and since `eval_await` lives on the interpreter and a built-in is
not on it, the loop was written out in `BuiltinsArray.cpp`. Eight lines,
identical to the interpreter's.

## The measurement that had to be written down

test262 **does not move**: 15,180 before, 15,180 after, because the pinned
commit predates `Array.fromAsync`. A phase whose only conformance number is
unchanged looks like a phase that did nothing, so the doc says why, and the
evidence offered instead is fourteen probes compared line for line with Node
v22.22.3 — all identical — plus a unit suite that asserts the same answers.

Four of those probes are the ones a quick implementation gets wrong:

```js
Array.fromAsync({ length: 2, 0: Promise.resolve('p'), 1: 'q' })  // p,q  — places are awaited
Array.fromAsync(asyncIterable)                                    // @@asyncIterator wins
Array.fromAsync.call(C, [1, 2])                                   // an instance of C
Array.fromAsync.call({}, [1, 2])                                  // a plain array, not a throw
```

## Evidence

`typeof Array.fromAsync` `"undefined"` → `"function"`; `make test` **465/465**;
`js-conformance` and `wpt` unchanged; test262 unchanged, for the reason above.
Fourteen Node comparisons identical.

## Still open

`Promise.try` is `undefined` in Node v22.22.3 as well, so the row's mention of
it cannot be compared against the oracle and it was left alone.
`Set.prototype.union` and the other set methods are absent and are a separate
phase, not this one.
