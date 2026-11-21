# Finding 172 — an AVIF was a file the engine called "not an image"

`ImageDecoder::decode_bytes` is a chain of magic-number tests, and anything that
fell off the end of it returned `nullptr`. An AVIF fell off the end. To the
engine an AVIF was not an image at all: `<img src="x.avif">` got the same answer
as a text file, which is true as far as it goes — there is no AV1 decoder — and
unhelpful in the way that matters, because the two are not the same situation.
One is a file that cannot be read. The other is a file whose structure is
perfectly readable and whose samples this engine cannot decode yet.

Everything a caller needs *before* the samples is in the box structure, and none
of it needs a decoder: the size to lay the box out at, the bit depth, whether
there is an alpha plane, the colour information, and the exact bytes to hand a
decoder when there is one. That is now read, and the row says so.

## The bug inside the first attempt

The box walker returned a `bool`: `true` for "walked everything", `false` for
"stopped" **and** for "malformed". The first version then used it the obvious
way:

```cpp
if (!walk_boxes(data, size, [&](const Box& b) {
        if (b.type == "ftyp") { box = b; found = true; return false; }  // stop
        return true;
    })) { error = "a box header runs past the end of the file"; return false; }
```

`ftyp` is the first box, so the walk always stopped immediately, always returned
`false`, and every well-formed AVIF in the world was reported as having a box
header past the end of the file. Five real files, every one of them refused with
the same wrong sentence — which is at least a good failure, because the message
was specific enough to be checkable, and one box dump showed the file to be
perfectly regular: 32 + 235 + 58 = 325 bytes exactly.

The fix is a three-valued result — `Ok`, `Stopped`, `Malformed` — because "I
found what I was looking for" and "the file is broken" are different answers and
one boolean cannot carry both. The type now makes it impossible to confuse them.

## What the container answers, and what it deliberately does not

It answers: is this an AVIF, what size is it, how deep are the planes, is there
colour information, where are the bytes. It does not answer "what does it look
like", and no entry point pretends to. A test asserts that `ImageDecoder::decode`
returns `nullptr` for both fixtures, so that assertion is what fails the day a
decoder lands — the row cannot be closed by accident, and cannot be believed
closed when it is not.

## Hostile files

Every field in this structure is a length or an offset. 200,000 mutated files
through the parser under ASan and UBSan produced no crash, no leak and no
undefined behaviour, with each file landing in one of the four answers. The
extent check is the one that matters most: an `iloc` entry that points outside
the file is refused *before* anything is copied, so the buffer that would
otherwise be read past is never reached.
