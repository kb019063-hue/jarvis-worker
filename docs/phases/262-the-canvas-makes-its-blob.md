# Phase 262 — the canvas makes its Blob

Task row: the canvas row of `AGENTS.md` — *"Still open: `toBlob()` does not
exist, so half of §4.12.5.1 has nothing to apply to (the flag it needs is now
there)."* Finding 266.

## 1. What was wrong

`HTMLCanvasElement.toDataURL()` has existed since Phase 144. Its sibling,
`toBlob(callback, type, quality)` (HTML §4.12.5.4), did not: there was no
binding, no method on the prototype, and no suite. Half of §4.12.5.1's
restriction on a tainted bitmap therefore had nothing to apply to — a script
that cannot read a canvas's pixels through one method could always read them
through the other.

The documents disagreed with the tree. [`docs/phases/153-canvas-toblob.md`](153-canvas-toblob.md)
and [`docs/findings/158-canvas-toblob.md`](../findings/158-canvas-toblob.md) describe a
`canvas_to_blob` in `src/js/DomHtmlBindings.cpp` "alongside `toDataURL()`",
verify it with a suite named `canvas_to_blob_is_the_png_it_painted`, and list
"Asynchronous task queueing (currently invokes `callback` synchronously)" as
an open item. **None of that was ever in the tree**: `git grep toBlob` on
`91460c8` (the `main` this branch was cut from) finds the method's name only
in documents and in one accurate header comment, the named suite does not
exist, and the `AGENTS.md` row this task came from still said `toBlob()` does
not exist. The Phase 153 write-up's "309 engine test suites pass" and its
verification section describe a state that cannot be reproduced from the
commit it names. The shallow clone cannot say when or why the work was lost;
what can be said is the present tense, and in the present tense the method
did not exist.

This phase implements it for real, and implements the two things the stale
write-ups could not: the asynchronous delivery they listed as open, and the
origin-clean check the flag arrived with in Phase 237.

## 2. What changed

All in `src/js/DomHtmlBindings.cpp`, registered on the `HTMLCanvasElement`
prototype as `toBlob` (length 1), immediately after `toDataURL`:

- **Arguments, in WebIDL order.** The callback is required
  (`require_args` → `TypeError`, "1 argument required, but only 0 present");
  a value that is not callable is a `TypeError` thrown synchronously
  ("parameter 1 is not of type 'Function'.") — the WebIDL conversion happens
  before the algorithm runs, so no callback is ever invoked with a bogus one.
  `type` omitted or `undefined` means `image/png`, exactly as `toDataURL`
  reads it; an explicit `null` is the string `"null"`, which is not a MIME
  type. `quality` is accepted and ignored: it selects between lossy encoders,
  and there are none. The essence is ASCII-trimmed and lowercased
  (`"IMAGE/PNG"` is a PNG); parameters are carried into the result.
- **A tainted bitmap throws `SecurityError`, synchronously.** The check
  reads `canvas_context()->origin_clean()` before the type is even parsed —
  the same order and the same rule `toDataURL()` enforces — because the
  pixels are what is being protected, not the encoding. `drawImage` of a
  cross-origin `<img>` therefore closes both doors at once.
- **`image/png` is the one type the engine encodes**, through
  `ImageDecoder::encode_png`, exactly as `toDataURL()` does. A canvas with no
  context is serialized as a blank bitmap of the element's size and does not
  grow a context; an over-budget one-pixel store, a zero `width` or `height`,
  an encode failure, a type with no encoder, and a string that is not a
  MIME type all hand the callback **`null`** — the encoding-failure answer,
  not a blob of nothing.
- **The callback fires in its own task.** `EventLoop::default_loop().post_task`
  with `js_roots({callback})` keeps the function alive across the gap, and the
  task re-checks `realm->alive` before touching the realm — a page that
  closed between the call and the task gets no callback at all. The blob's
  `type` is the PNG essence with parameters, lowercased by `make_blob`'s own
  `normalized_type`, which is what any script-built `new Blob([], {type})`
  holds. The bitmap is not cleared and the element does not change.

Two honest deviations from "just like a browser", both inherited from
`toDataURL()` and recorded there: there is no JPEG/WebP encoder, so those
types answer `callback(null)` rather than a lossy re-encode; and the
delivery is a task on this engine's loop, not a network/task-source
identity, because there is no task-source model here yet.

The stale comment above `ascii_trimmed` ("Nothing here can taint a canvas
yet … until then there is no flag to read") described Phase 144's world and
has been rewritten; the flag has had an input since Phase 237.

## 3. How it was verified

**`make -j2 test`** — **all 396 engine test suites pass** (395 before this
branch; the +1 is this phase's suite), including the new
`tests/suites/262-canvas-to-blob.cpp`
(`a_canvas_serializes_itself_into_a_blob`), which requires, on one page and
then on the two-origin-root harness Phase 237 built:

- the callback has **not** fired synchronously; after
  `EventLoop::default_loop().run_until_idle` it has, with a value that is
  `instanceof Blob`, `type == "image/png"`, `size > 0`;
- the first eight bytes of `arrayBuffer()` are `137,80,78,71,13,10,26,10` —
  the PNG signature, read back through the promise the blob itself exposes;
- `image/jpeg`, `"not a mime type"` and a `width=0` canvas hand the callback
  `null`; an omitted type and an explicit `quality` of `0.01` both hand it
  `image/png`;
- a missing callback and a non-callable callback are each a `TypeError`;
- one `red.png` served from two origin roots: the page whose `<img>` is
  **same-origin** gets a `image/png` blob; the page whose `<img>` is
  **cross-origin** throws `SecurityError` synchronously — and the element's
  same-origin PNG drawn on the other page does not. The suite injects nothing
  to make these agree; they came out of the two pages disagreeing, which is
  the exercise.

**`python3 tools/ci/check_suites.py`** — `ok 23 suite(s) are files of their
own in tests/suites/`; `python3 tools/ci/check_suites.py --list` unchanged
(the count of suites writing their own prologue is a pre-existing note).
**`make check-numbers`** and **`make check-docs`** — see §4; both pass.
There is no Web Platform Tests file for `toBlob()` in `tests/wpt`, so the
conformance scoreboard does not measure this and none of its numbers move.

## 4. What is still open

- **`image/jpeg` and `image/webp`** — no encoders exist; both hand the
  callback `null`. A page that asks for a JPEG export gets nothing, and the
  suite says so out loud.
- **`OffscreenCanvas`** and its transfer, and an `<img>`→`ImageBitmap` path
  — unchanged, still absent.
- **The delivery is a task on the default loop**, not a distinct
  task-source; a page that spins its own dispatcher still sees the callback
  exactly once.
- **`docs/phases/153-canvas-toblob.md` and `docs/findings/158-canvas-toblob.md`
  are left as written.** They describe a landing that did not survive
  whatever happened between then and this branch, and rewriting history to
  make it look like this phase's continuation would be exactly the kind of
  edit that makes the next audit useless. This file is the correction; the
  old files are the record.
