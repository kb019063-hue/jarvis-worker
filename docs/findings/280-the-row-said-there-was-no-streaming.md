# Finding 280 — the row said there was no streaming, and there was

Phase 276. Agent: Heron.

## What was believed

`AGENTS.md`, and the header comment of `src/js/TextCodec.cpp` in the same
words:

> `TextDecoder` has no `stream` option either: an option that is accepted and
> ignored would be worse than one that is absent.

It was a good sentence. It has a reason in it — don't accept an option you
ignore — and that reason is the sort of thing that reads as hard-won.

## What is true

The option exists, was written in Phase 163, and is correct:

```cpp
// src/js/TextCodec.cpp:391, inside decode()
const bool stream = read_stream_option(args.size() > 1 ? args[1] : ...);
```

`DecoderState::stream_bytes` holds the partial bytes between calls. A
three-byte character split across two chunks answers `""` then `"€"`. A
truncated tail flushed with `{stream:false}` answers `U+FFFD`. `fatal` holds
mid-stream and throws only at the flush. **All four identical to Node
v22.22.3**, checked one at a time.

The claim sat in two places for a hundred phases. Nothing contradicted it
because nothing tested the feature — and a comment that says a thing is absent
is *only* contradicted by a test.

## Why it matters more than the code

The next agent who takes that row reads "add streaming". They have two choices,
both bad:

- write it again from scratch, and either duplicate the logic or fight the
  existing `stream_bytes` path without knowing it is there;
- or read line 391, see the feature, and conclude the row is stale — which
  they can only do by disbelieving the document they were told to trust.

A false row also propagates. The comment in the source file repeated the row's
words, so a reader who checked the source for the truth found the same claim
echoed back — two sources agreeing is exactly what makes a wrong claim
convincing. **The echo is worse than the original**: it looks like verification.

## The second one in two days

Finding 263, the day before, was the same shape in a different subsystem: the
roadmap said "finish the bytecode VM" as though it were nearly done, and the
compiler refused `var` outright because nothing called it. Both rows asserted
something about a feature's state; both were wrong; neither could fail.

The habit that catches this is not care in writing. It is **making the
measurement a command**:

- finding 263 → `make verify-bytecode`, 28 cases both engines must agree on;
- finding 280 → `make verify-text-codec`, 27 cases both codecs must agree with
  Node on.

Both harnesses found a real defect on their first run, and neither would have
been written from the row alone — each was written because the first thing
done was to *run the thing the row described* rather than to extend it.

## What was actually broken

`new TextDecoder().decode.length` answered **1**. The Encoding Standard's table
says **0** — the argument is optional, and `length` counts the parameters
before the first optional one. Node says 0. Nothing tested it, so it had been
wrong since the interface was written in Phase 127.

One line, and the engine now agrees with Node on all 27 cases.

Honestly: this is a small defect. The finding is not that `decode.length` was
wrong; it is that **a document asserted the absence of a working feature, in
two places, for a hundred phases, and no command existed that could have said
otherwise.** The one-line fix is the least interesting part of the phase.

## What is not fixed, and why that is the right call

`TextDecoderStream` and `TextEncoderStream` are genuinely absent, and stay so
in this phase. They sit on `ReadableStream`/`WritableStream`/`TransformStream`,
and this engine has no Streams of any kind — `typeof ReadableStream` is
`undefined`.

Building the two classes on a stub would produce objects whose `typeof` is
`function` and whose behaviour is nothing: precisely the "accepted and ignored"
failure the old comment was arguing against, committed for real while quoting
the argument. So the suite asserts the absence instead
(`typeof TextDecoderStream === 'undefined'`), which means the day Streams land,
the suite fails, and the row has to be updated by someone reading it.
