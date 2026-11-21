# Finding 132 — a string's byte count is not its length

Phase 127. Found while writing the `encodeInto()` assertions, fixed there.

## What it was

`JsString` carries two different numbers, and for any non-ASCII text they
disagree:

- `size()` — how many **UTF-8 bytes** the string's view holds.
- `length()` — how many **UTF-16 code units** script sees as `.length`.

`'hé'` is 3 bytes and 2 code units. `TextEncoder.encodeInto()` has to report
`read` in code units and `written` in bytes, so it needs the first for the
output and the second for the input count — and the first version of it used
`units.size()` for both.

```js
var b = new Uint8Array(8);
new TextEncoder().encodeInto('hé', b);   // {read: 3, written: 4}
```

The bytes it wrote were right. Both counts were one too high, and `read: 3`
is not merely off by one — it is a code-unit count for a two-unit string, so
it says the encoder consumed more input than exists.

The bug survives every ASCII test. `'hi'` is 2 bytes and 2 units, so the two
numbers agree, and the suite's ASCII cases passed while the very next line
failed. That is what makes it worth a finding rather than a comment: the
wrong accessor is right for 127 of 128 characters.

## Why it happened

`size()` is documented as "How many UTF-8 bytes `view()` holds", immediately
above `length()`, which is documented as "What script calls `.length`". Both
are size-related, both are `size_t`, and the one the caller wants depends on
which side of the conversion the count is on. A name like `utf8_byte_length()`
would not have been reachable by mistake here.

## The same walk, twice

`encode()` and `encodeInto()` were walking the string separately and had
already drifted: `encode` had the surrogate rule and `encodeInto` did not, so
`encode('\ud800')` was correct while `encodeInto('\ud800', …)` wrote nothing.
They are now one `walk_units()` that folds a well-formed surrogate pair,
replaces a lone surrogate with U+FFFD (§8), and hands each encoded code point
to a sink. `encode` passes a sink that never fills; `encodeInto` passes one
that refuses the first code point with no room, which is exactly where it must
stop — mid-code-point never happens because the sink sees whole code points
only.

Two bugs and one duplication, from one accessor.

## What is asserted now

`text_codec_a_decoder_and_an_encoder_in_script` pins `encodeInto('hé', 8)`
as `{read: 2, written: 3}`, `encodeInto('hé', 2)` as `{read: 1, written: 1}`,
`encodeInto('é', 1)` as `{read: 0, written: 0}`, `encode('\ud800')` as
`239,191,189`, and a surrogate pair as the four bytes of U+1F600. Each was
printed by Node before it was required of the engine.
