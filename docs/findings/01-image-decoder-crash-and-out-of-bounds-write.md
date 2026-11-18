# 1. ✅ Image decoder: crash and out-of-bounds write

> Moved out of `AUDIT.md` — written there as a `## 1. ✅ Image decoder: crash and out-of-bounds write` section.
> The text below is the section as it stood, unchanged.


**The hole.** Both `decode_bmp` and `decode_ppm` computed `width * height` as
`int`:

```cpp
img->pixels.resize(width * height, 0xFF000000);   // int * int
```

A BMP header is 54 bytes and fully attacker-controlled. Two shapes break it:

| Header claims | `w * h` as `int` | Effect |
|---|---|---|
| 65536 × 65535 | **−65536** | `resize(negative)` → `std::length_error` → `std::terminate` |
| 65537 × 65537 | **131073** | buffer holds 131,073 entries but `set_pixel` trusts 65537 × 65537 → **OOB heap write** |
| 65536 × 65536 | 0 | zero-length buffer, silent |

Reproduced against the real binary, no sanitizer needed:

```
$ cat page.html
<html><body><img src="evil3.bmp"></body></html>
$ ./build/litebird --render-bmp out.bmp page.html
terminate called after throwing an instance of 'std::length_error'
  what():  vector::_M_fill_insert
```

154 bytes of untrusted input, no `<script>` involved, and the process dies.
There is no `try`/`catch` anywhere in `main.cpp`.

**Why the original fuzzer missed it.** `tools/fuzz/fuzz_parser.py` fed every
mutation to `--dump-dom-webplatform`, which returns before layout and never
calls `ImageDecoder`. Phase 22 adds a pipeline fuzzer that runs corpus mutations
through layout and bitmap rendering, and seeds image cases with malformed BMP
and PPM headers. That pass also exposed unchecked decimal overflow in PPM
`read_int`; the parser now consumes overlong fields safely and rejects them.

**The fix.** Dimensions are validated before any multiplication, using division
so the check cannot itself overflow, capped at 64 megapixels. Also fixed:
`std::abs(INT32_MIN)` is UB and stays negative; and
`row_offset + row_width > size` could overflow, so it is now
`row_width > size - row_offset`. Regression test:
`image_decoder_rejects_hostile_dimensions`.

**Still open in the same file:** PPM `read_int()` consumes an unbounded run of
digits, so a long numeric token wraps `int` (UB) before the range check sees it.

---
