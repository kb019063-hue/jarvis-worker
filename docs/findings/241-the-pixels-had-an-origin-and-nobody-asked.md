# Finding 241 — the pixels had an origin and nobody asked

**Severity:** security. `HTMLCanvasElement.toDataURL()` returned every pixel a
canvas held, with no origin check, while the engine already had the one fact
the check needs — and while a cross-origin `<img>` could be fetched, decoded,
and held as pixels. What was missing was the binding that would let a page draw
those pixels onto a canvas.

**Found by:** reading the AGENTS.md row and checking its premise instead of
trusting it. The row said the flag had "no input" because `drawImage`,
`ImageBitmap`/`createImageBitmap` and `Response.arrayBuffer()` did not exist.
Two of the three existed. The third was not needed: the taint source the row
had not considered was `<img>`.

## What the row said, and what had changed under it

> Today every pixel came from the page's own drawing primitives, because
> `drawImage`, `ImageBitmap`/`createImageBitmap` and `Response.arrayBuffer()`
> do not exist — so there is nothing that could taint a canvas, and no flag to
> consult.

The conclusion was correct when it was written, and the row was careful about
it — it refused to write a flag that is always true, on the grounds that a test
could not distinguish that from a flag that works. That instinct was right and
is why this finding has a two-page test rather than a one-page one.

What had changed:

```
$ grep createImageBitmap src/js/DomWindowBindings.cpp
1871:    I.method("createImageBitmap", 1, [](Realm& r, Args args) {
```

`createImageBitmap` existed. `Response.arrayBuffer()` existed. And in
`src/layout/LayoutTree.cpp` the renderer was already fetching `<img>` sources
under a CORS-aware request context and recording whether the response crossed
an origin (`ResourceResponse::cross_origin`). Every input the flag needed was
present; `drawImage` was simply not bound to script, so no page could reach
the pixels that would taint a canvas.

## The hole, and its exact size

Small, but real and on the wrong side of a security boundary:

* `toDataURL()` serialized unconditionally;
* `load_image_for_element` decoded cross-origin image bytes and kept them;
* nothing connected the second fact to the first.

The connection is one method. Once `drawImage` exists, a page can take a
cross-origin image's pixels through the canvas and out through `toDataURL()`,
which is precisely the exfiltration §4.12.5.1 exists to prevent — a
cross-origin image can require credentials the page's script may not read, and
the canvas would have laundered it into a `data:` URL.

It is worth being precise that the engine did not *already* leak: before this
phase no script could get those pixels onto a canvas, so `toDataURL()` was
honest. The vulnerability was created by adding `drawImage` and would have
existed for one commit if the flag had not been written in the same change.
That is the finding: **the flag is not a follow-up to `drawImage`, it is part
of it.**

## A second bug found on the way

`createImageBitmap(canvas)` — already shipped — built a bitmap of the canvas'
size filled with **zeroed pixels**:

```cpp
state->width = w;
state->height = h;
state->pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
```

The canvas had an image; the copy did not. A bitmap that is always transparent
is not a copy of anything. It is fixed here because drawing a bitmap is what
made the difference observable.

## Why the test is two pages

The row's warning decides the shape of the evidence. Three flags are
indistinguishable from one test:

| flag | cross-origin page | same-origin page |
| --- | --- | --- |
| always clean (the bug) | `toDataURL` returns pixels — **wrong** | returns pixels |
| always tainted (over-correction) | throws — right | throws — **wrong** |
| the real flag | throws | returns pixels |

So the suite loads the same page against both origins and requires opposite
answers. Both failure modes were then injected and the suite failed both times:

```
if (false)     -> Assertion `read_back == "err:SecurityError"' failed.
if (context)   -> Assertion `... b.toDataURL() ... == "data:image/png;base64,"' failed.
```

A test that only ever saw a `SecurityError` would pass against a flag that is
never true. A test that only ever saw a data URL would pass against no flag at
all.

## What was carried, and where it could have been lost

The origin has to travel with the pixels through three hops, and each one was
checked rather than assumed:

1. **fetch → element.** `ResourceResponse::cross_origin` is true when the final
   hop, after redirects, was not the document's origin under a CORS request.
   The loader that sets it (`load_image_for_element`) is now the one function
   that both decodes the pixels and records the fact, so the two cannot drift.
2. **element → bitmap.** `createImageBitmap` copies the flag with the pixels.
   Without it, a tainted canvas' bitmap would launder the taint.
3. **bitmap → destination canvas.** `drawImage` clears the destination's flag
   from the source's, so taint composes.

## Still open

* `toBlob()` does not exist, so half of §4.12.5.1's restriction has nothing to
  apply to. The flag it will need is in place.
* `drawImage` of an `<img>` requires the layout pass to have decoded it. A page
  that draws before layout gets a `TypeError`; it would be better to fetch on
  demand, and that is a follow-up rather than a silent wrong answer.
* `createImageBitmap`'s crop and resize options, and an `<img>`-to-`ImageBitmap`
  path, are outside this change.
