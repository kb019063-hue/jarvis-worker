# Phase 276 — the row said there was no streaming, and there was

Task: `AGENTS.md`, row *"`TextDecoder` and `TextEncoder` reach script, and the
decoder reads in pieces since Phase 163"* — its open items. Finding 280.
Agent: Heron.

## 1. The task was to add a feature that was already there

The row said, and the header comment of `src/js/TextCodec.cpp` said it in the
same words:

> `TextDecoder` has no `stream` option either: an option that is accepted and
> ignored would be worse than one that is absent.

Line 391 of that same file is `read_stream_option(args[1])` inside `decode`.
`DecoderState::stream_bytes` holds the partial bytes. The feature exists, was
written in **Phase 163**, and works.

Four probes, run on the engine and on Node v22.22.3:

| probe | this engine | Node |
| :--- | :--- | :--- |
| `0xE2,0x82` then `0xAC`, `{stream:true}` | `""` then `"€"` | `""` then `"€"` |
| flush a truncated tail | `"U+FFFD"` | `"U+FFFD"` |
| `fatal`, split mid-stream | `""`, no throw | `""`, no throw |
| `fatal`, at the flush | `TypeError` | `TypeError` |

All four agree. So the work was not to build streaming. It was to stop two
documents saying it was missing, and to find what genuinely was.

**That is the finding, and it is worth more than the code:** a row that
describes a working feature as absent is not a harmless inaccuracy. Every
reader who trusted it — and a phase that "adds" the feature would have been
written against it — was working from a false map. This is the same shape as
finding 263 the day before: a row asserting an absence that the code
contradicted. Twice in two phases suggests the habits worth keeping are
*measure before building* and *make the measurement a command*.

## 2. The harness, so the next claim is not prose

`tools/conformance/verify_text_codec.py` (`make verify-text-codec`) runs **27
cases of both codecs against Node** and requires the same answers: labels and
canonical names, the BOM and `ignoreBOM`, `fatal`, split characters over two
and three chunks, `encodeInto`'s `read`/`written`, lone surrogates, and the
`length` of every method.

The comparison is on values, not on formatting: each case is a snippet whose
`String(...)` both sides print, so nothing depends on two serialisers agreeing.

`--self-test` checks the comparison itself on six fixed pairs — including a
pair that must *not* be judged equal — so a harness that compares nothing is
visible before it is trusted.

## 3. It found a defect on its first run

```
FAIL  decode's length: the Encoding Standard says 0
      new TextDecoder().decode.length
      the engine answers 1
      Node answers        0
```

The Encoding Standard's table gives `TextDecoder.prototype.decode` **length
0**: its one argument is optional, and `length` counts the parameters before
the first optional one. The engine had `js_define_method(proto, "decode", 1,
…)`. Nothing was checking it, so it had been 1 since the interface was written.

One line, and the engine now agrees with Node on all 27 cases.

## 4. What is genuinely still open, and what this did not do

- **`TextDecoderStream` and `TextEncoderStream` are absent**, as the row said.
  They are *not* a codec feature: they are built on `ReadableStream`,
  `WritableStream` and `TransformStream`, and this engine has **no Streams at
  all** (`typeof ReadableStream` is `undefined`). Adding the two classes
  without the Streams beneath them would be a facade that answers the right
  `typeof` and does nothing — which is exactly the "accepted and ignored"
  failure the old comment was warning about, done for real. So this phase
  scopes them out and says why, and the suite asserts the gap so the day one
  appears the suite fails and the row moves.
- **`TextEncoder` gains nothing from a `stream` option.** `encode()` answers
  UTF-8, where a lone surrogate is always `U+FFFD` and no character can be
  split across calls. Node has no such option either.
- **The `<meta charset>` prescan** is still one pass over the first 1,024
  bytes. Untouched here.

## 5. Checks

```
make -j2 all && make -j2 test          410 suites, all passing
make verify-text-codec                  27/27 agree with Node
python3 tools/conformance/verify_text_codec.py --self-test
                                        6 comparisons
make js-conformance                     1149/1158 (unchanged)
make dom-conformance                    678/678 (unchanged)
make conformance                        1973/1973 (unchanged)
make url-conformance                    1188/1188 (unchanged)
```

The one behaviour change is `decode.length` from 1 to 0, which no page can
observe except by reading it.
