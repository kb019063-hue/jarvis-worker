# Phase 24 Architecture: Strings That Cost What They Read

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 24 Architecture: Strings That Cost What They Read` section.
> The text below is the section as it stood, unchanged.


### 1. Why this phase existed

Phase 23 left one defect that made ordinary scripts appear to hang. A string
was a `std::string` inside the `JsValue` that held it. Reading a variable
copied the text, and `length` and indexing scanned it to find out whether it
was ASCII, so anything done once per character cost the whole string each
time:

| Script | Phase 23 | Phase 24 | Node 22 |
| :--- | ---: | ---: | ---: |
| `s.charCodeAt(i)` over 200,000 characters | 77,082 ms | 91 ms | 31 ms |
| `s += x`, 100,000 times | 9,811 ms | 49 ms | 45 ms |
| tokenizer that eats its input, `s = s.slice(k)`, 20,000 tokens | 2,556 ms | 18 ms | 31 ms |
| `re.exec(s)` in a loop, 20,000 matches in 200,000 characters | killed: out of memory | 44 ms | 33 ms |

A longer table is in AUDIT.md under finding 25. `make bench` runs these and 35
more against Node and fails if a result differs or a script takes over 3 s.

### 2. `JsString`

`include/litebird/js/JsString.hpp`, `src/js/JsString.cpp`. A string value
holds a handle of 24 bytes:

```
JsString  { block*, byte offset, byte length, unit offset, unit length }
              |
              v
Block     { refs, used, capacity, moves, wide* } [ UTF-8 text ......... free room ]
                                         |
                                         v
Wide      { units ready, bytes read } [ UTF-16 code units ... ] [ a mark every 256 units ]
```

The text is UTF-8 in a block that is shared and reference-counted. A handle is
a range of its block. Bytes that have been written are never written again;
that one rule is what makes the sharing invisible to script.

- **Reading.** Copying a value copies the handle and bumps a count. `length`
  is a field. A string whose byte length equals its unit length is ASCII, and
  indexing it reads one byte. Any other string gets a UTF-16 copy (`Wide`) the
  first time it is indexed. The copy belongs to the block, is extended when
  the block's text grows, and is never rebuilt; next to it is a table that
  says where every 256th code unit is in the bytes, so a slice can be located
  by index without reading what comes before it.
- **Appending.** `a + b` looks at whether `a` ends where its block's text
  ends and whether there is room after it. If so it claims the room (one
  compare-and-swap on `used`) and writes `b` there; `a` is not copied, and
  every other handle still sees the bytes it saw. Otherwise the result goes
  into a new block. Text that has moved three times in a row is being built
  in a loop and gets half again as much room as it needs, so n appends copy
  about 3n bytes in total instead of n²/2. Text joined once or twice carries
  no spare room.
- **Pieces.** `s.slice(a, b)` is another range of the same block when it is a
  quarter of the block's bytes or more, and a copy when it is smaller. The
  quarter bounds what a slice can keep alive to four times its own size. A
  tokenizer that does `s = s.slice(n)` therefore copies less than its input
  once, in a few steps, as the remainder shrinks past each quarter.
- **One spelling.** Bytes that are not UTF-8 become U+FFFD on the way in, as
  when a browser decodes a page. Half of a surrogate pair may stand alone
  (three bytes), since script can make one, but a high half followed by a low
  half is always stored as the one four-byte character. Equal strings
  therefore have equal bytes, and `===`, property keys and `Map` keys can
  compare bytes. The cost is that a piece cannot share its block if it starts
  or ends between the two halves of a pair; it is built from the code units
  instead.
- **Order.** `<` and `sort()` compare UTF-16 code units. Comparing the bytes
  agrees with that except for a character outside the BMP, which has the
  highest bytes in UTF-8 but is a surrogate pair, below U+E000, in UTF-16.
  `JsString::compare` compares bytes until the texts differ and settles that
  one case there. Nothing else in the engine may order string text.
- **Limit.** A string is at most 2^29 − 24 bytes. Past that script gets
  `RangeError: Invalid string length`. For ASCII that is V8's limit exactly;
  other text reaches it sooner than in V8, which counts code units.
- **Threads.** Suspended async functions, generators and workers run on other
  OS threads (Phase 23, section 4), so handles are copied, read, appended to
  and dropped from several threads. The count, the claim on free room and the
  publication of the UTF-16 copy are atomic; building the UTF-16 copy takes
  one global mutex, and reading it takes none.

### 3. What changed around it

- `JsValue::string_val` is a `JsString`, and a value is 88 bytes (was 96).
  Engine code reads a string as `view()` (the bytes, no copy), `length()`,
  `unit_at(i)`, `substring(b, e)`, or through `Utf16View`, which addresses
  any string by code unit. `str()` makes an owned copy and is the one read
  whose cost grows with the text; code that runs once per character must not
  call it. `JsString::stats()` counts bytes copied and units converted per
  thread, and the unit tests assert on those counts, not on time.
- `String.prototype` methods take their receiver as a handle and search it in
  place. The regular-expression matcher reads its subject through a
  `Utf16View`; it used to compare the subject with a cached UTF-16 copy, and
  copy that, on every call. A match array's `input` is the subject itself,
  not a copy.
- `'key' + i` has a direct path in `js_add`. The general path converts both
  operands first, and with a string that is now a block of its own that was a
  measurable cost.
- Property names are still `std::string`. A string value used as a key
  (`o[k]`) is copied once per lookup; that is bounded by the key's length and
  is the same as before.
- Text that leaves the engine is UTF-8. Inside, half of a surrogate pair is
  three bytes that no decoder accepts; `console.log` and the uncaught-error
  report pass their text through `js_output_text`, which writes U+FFFD for a
  lone half, as Node does. The other direction is strict too:
  `decodeURIComponent` throws `URIError` for bytes that are not UTF-8
  (a surrogate, an overlong form, anything above U+10FFFF).

### 4. Lookbehind

`(?<=x)` used to be tried forward from every earlier position in the subject,
so one lookbehind that failed cost the length of everything before it:
`/(?<=,)\w+/g` over 120,000 characters took 50 s. The body of a lookbehind is
now matched right to left from the current position, as the language defines
it. The matcher keeps a direction; inside a lookbehind a sequence runs from
its last term to its first, a character is read from before the position, a
back reference is compared against the text that ends there, and a capture is
recorded as `[low, high)` whichever way it was walked. A lookahead inside a
lookbehind runs forward again. Lookarounds finish before their continuation
runs, so the direction is plain state that a scope object sets and restores.

In unicode mode the subject is a sequence of code points, so a match may not
begin between the halves of a surrogate pair; both the search loop and the
`lastIndex` a script sets are held to that.

### 5. How this is verified

| Check | Result |
| :--- | :--- |
| JavaScript corpus (`make js-conformance`) | 840 / 845; the five failures are the Phase 23 ones (BigInt, `Proxy`, typed arrays, array holes). 44 cases are new: `long-strings.dat` (34), lookbehind and unicode cases in `regexp.dat` (6), surrogate halves, `isWellFormed`, console output and `decodeURIComponent` in `strings.dat` (4). Phase 23's engine passes 810 of the 845 at best (809 when one script that takes it 9.9 s misses the 10 s limit) |
| The corpus re-run under Node 22 | 845 / 845 |
| Unit tests (`make test`) | 88 / 88. Three are new: a random-edit model test, a thread test, and one that asserts on counted work through the interpreter |
| `make tsan`: the two stress tests alone, under ThreadSanitizer, and under ASan + UBSan with leak checking | no report, nothing leaked |
| Planted bugs: 47 in `JsString`, one at a time | 46 caught by the stress tests; the other changes no result and no counted work |
| Planted data races: 4 | ThreadSanitizer reports all 4; the assertions alone catch 2 |
| `make diff-strings`: 1,133 string expressions over 36 subjects, evaluated here and in Node | 95 of 50,580 results differ, every one of them Unicode data the engine lacks: collation (46), the letter table behind `\p{L}` (24), normalization (17), case mapping of `ǅ`, `İ` and fullwidth letters (8). Phase 23's engine differs on 1,103: 720 are the two methods it did not have, 290 are fixed, 93 remain |
| Differential run against Node when the matcher changed: 75 lookbehind patterns over 20 subjects | 0 of 1,500 results differ; Phase 23's engine differs on 89 |
| ASan + UBSan: unit tests, the corpus, both fuzzers (`make asan`) | 88 / 88; corpus 839 / 845, the extra failure being the recursion 2,000 deep that a sanitizer build has no stack for; 400 + 400 fuzz runs; no sanitizer report anywhere |
| `make bench`: 39 string-heavy scripts against Node | every result equal to Node's; the slowest takes 0.2 s, the limit is 3 s |
| html5lib tree construction | 1770 / 1792, unchanged |

The string differential is `tools/conformance/diff_strings.py`; it takes a few
seconds, needs Node, and runs in CI with the 95 as a ceiling. The scripts that
plant bugs and the lookbehind grid are not in the repository; what they found
is, as corpus cases and as assertions in `tests/js_string_stress.hpp`.

### 6. What is still open here

- **Regular expressions over long input.** A group repeated more than about
  12,000 times ends in a `RangeError`, and a match is abandoned after 30
  million steps. Both are in AUDIT.md, finding 26; the first needs the matcher
  to keep its backtracking state on the heap.
- **`delete` and `shift()`** were quadratic in the size of the object or
  array (Phase 23, section 9). Phase 30 makes both linear: the property map
  keeps insertion order in a list with a stable hash index, and arrays are
  deques. They were found by the same timing run and are not string
  problems.
- **`repeat`, `padStart` and `padEnd`** build their result in a temporary and
  copy it into a block: twice the memory for a moment, on a very long result.
- **Short strings have no inline form.** Every string except the empty one and
  the 128 one-character ASCII strings has a block. Measured cost: no time,
  about 20 bytes a string (AUDIT.md, finding 25).

---
