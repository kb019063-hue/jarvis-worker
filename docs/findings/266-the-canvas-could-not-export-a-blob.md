# Finding 266 — the canvas could not export a Blob

**Severity:** an observable contract broken, not a crash. `toBlob()` is how a
page hands a canvas's pixels to `URL.createObjectURL`, `FormData` and
`FileReader`; without it the only way out of the bitmap was a `data:` URL,
and half of HTML §4.12.5.1's restriction on a tainted bitmap had nothing to
apply to.

**Found by:** the canvas row of `AGENTS.md`, which still read *"Still open:
`toBlob()` does not exist, so half of §4.12.5.1 has nothing to apply to"* —
and by reading the two documents that claimed it was done. Phase 153
(`docs/phases/153-canvas-toblob.md`) and [finding 158](158-canvas-toblob.md)
describe a `canvas_to_blob` in `src/js/DomHtmlBindings.cpp`, verified by a
suite named `canvas_to_blob_is_the_png_it_painted` ("all 309 engine test
suites pass"). Neither the code nor the suite was in the tree; `git grep
toBlob` on the base of this work finds only documents. The stale documents
were not evidence of an implementation — they were finding 266's own
precondition.

## What it was

`HTMLCanvasElement` had `toDataURL()` (Phase 144) and nothing else that hands
the pixels back. A script that wanted the canvas as a `Blob` had to base64-
decode a data URL by hand; a script on a tainted canvas had no second door
that was also shut.

## What the fix holds

`toBlob(callback, type, quality)` is on the prototype now, and the checks
that matter are the ones that can lie:

- The callback fires **asynchronously**, in its own task
  (`EventLoop::default_loop().post_task`); the function value is rooted
  across the gap (`js_roots`) and the realm is re-checked inside the task, so
  a closed page gets no callback. Finding 158's "task queueing" open item is
  closed by this.
- **The origin-clean flag is enforced, synchronously** — a canvas that drew
  a cross-origin image throws `SecurityError` from `toBlob()` just as it does
  from `toDataURL()`. The check runs before the type is parsed; the pixels
  are what is protected.
- `image/png` is the one type encoded (`ImageDecoder::encode_png`);
  everything else — an unsupported type, a non-MIME string, `null`, a zero-
  dimension canvas, or an over-budget one-pixel store — hands the callback
  `null`, the encoding-failure answer. `quality` is accepted and ignored.
- A missing or non-callable callback is a `TypeError`, synchronously; no
  context is created on a canvas that has none; the bitmap is not cleared.
- The stale "no flag to read" comment above `ascii_trimmed` (true when
  written, false since Phase 237) is rewritten.

## Still open

`image/jpeg` and `image/webp` encoders (callbacks get `null` today), and the
same Phase-153-record caveat: that phase's and finding 158's documents are
left unedited as written — see `docs/phases/262-the-canvas-makes-its-blob.md`
§4. OffscreenCanvas remains absent.
