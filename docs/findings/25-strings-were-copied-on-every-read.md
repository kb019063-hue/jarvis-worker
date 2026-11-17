# 25. ✅ Strings were copied on every read

> Moved out of `AUDIT.md` — written there as a `## 25. ✅ Strings were copied on every read` section.
> The text below is the section as it stood, unchanged.


Not a crash, but it was the largest defect left in the JavaScript engine,
found by timing ordinary scripts against Node. A string lived inside the value
that held it. Reading a string variable copied the string; `length` and
indexing scanned it to find out whether it was ASCII. Each of those was
proportional to the length of the string, so a loop over a long string was
quadratic, and so was building one with `+=`. A script that decoded, hashed or
built a large string one character at a time appeared to hang. One of this
audit's own corpus cases did: it built a 900 KB script with `+=` and took 8 s,
until it was rewritten to use `join`.

**The fix.** A string value now holds a `JsString`
(`include/litebird/js/JsString.hpp`): a 24-byte handle on a range of a shared,
reference-counted block of UTF-8.

- Copying a value copies the handle. `length` is a field.
- An ASCII string is indexed by reading a byte. Any other string gets a UTF-16
  copy the first time it is indexed; the copy stays with the block and is
  extended when the text grows, never rebuilt.
- `a + b` writes `b` into the free room after `a` when `a` is the newest text
  in its block, and copies nothing of `a`. Text that keeps moving to a bigger
  block is given half again as much room, so n appends cost n.
- A slice that is a quarter of its block or more is another range of the same
  block. Smaller slices are copies, which bounds how much memory a slice can
  keep alive.
- Bytes that have been written are never written again, so no string can see
  what was appended to another.

Milliseconds for the whole process on one machine. "Before" is the engine at
`54b1986`, one run with a 120 s limit. "After" and Node are the best of three.
The scripts are in `tools/bench/bench_strings.py` (`make bench`), which also
checks that both engines print the same result.

| Script | Before | After | Node 22 |
| :--- | ---: | ---: | ---: |
| `s.charCodeAt(i)` over 200,000 characters | 77,082 | 91 | 31 |
| `s[i]` over 200,000 characters | 55,912 | 63 | 31 |
| `s.charCodeAt(i)` over 200,000 characters that are not ASCII | over 120 s | 94 | 35 |
| `i < s.length` as the bound of that loop | 84,863 | 46 | 29 |
| one long string passed to a function 200,000 times | 46,722 | 199 | 29 |
| `s.substring(i, i + 5)` at each of 100,000 positions | 17,640 | 62 | 33 |
| the same in text that is not ASCII | 72,321 | 66 | 33 |
| `indexOf` from hit to hit, 20,000 hits | 4,000 | 13 | 30 |
| `s += x`, 100,000 times | 9,811 | 49 | 45 |
| `obj.s += x`, 100,000 times | 4,154 | 35 | 41 |
| 30,000 table rows appended to one string | 5,440 | 26 | 44 |
| append, then read the last character, 50,000 times | 4,071 | 33 | 191 |
| `s += x` keeping every intermediate string, 20,000 times | 1,815 | 18 | 40 |
| tokenizer that eats its input: `s = s.slice(k)`, 20,000 tokens | 2,556 | 18 | 31 |
| the same in text that is not ASCII | 13,747 | 20 | 31 |
| `re.exec(s)` in a loop, 20,000 matches in 200,000 characters | killed at 4.6 s | 44 | 33 |
| sticky-flag tokenizer over 150,000 characters | killed at 5.8 s | 115 | 35 |
| `/^x/.test(s)` on a 200,000-character string, 20,000 times | 5,060 | 31 | 31 |

The two "killed" rows ran out of memory, not time: every match array held its
own copy of the subject in `input` (190 KB a match), and nothing is collected
until `gc()` is called. `input` now shares the subject, and the same loop peaks
at 10 MB. The rows that are still several times Node's do a function call or
a regular-expression step per character; that is the interpreter, not the
string.

**What it cost.** A string of a few characters used to live inside its value
and now has a block of its own, so the worry was short strings, which is
nearly every string a page handles.

In time, nothing measurable. Best of five, before and after: `'k' + i`
200,000 times 87 and 84 ms; `===` on short strings 91 and 82; `typeof x ===
'string'` 103 and 93; 200,000 writes to string-keyed properties 94 and 91;
sorting 50,000 strings 66 and 64. Fourteen ordinary scripts (loops, calls,
objects, JSON, template literals, `Map`, short regular expressions) are each
within 1% of their old time or faster. One was not at first: filling a
dictionary with computed keys ran 16% slower, until `'key' + i` stopped going
through the general conversion path.

In memory, about 20 bytes for each distinct short string: 200,000 of them in
an array peak at 33.6 MB where they took 29.7. Against that, a value is 88
bytes, down from 96; strings that are copies of one another are one block
(200,000 references to one string: 27.8 MB, was 35.7); and slices of a long
string share it (a 5 MB string with 1,000 slices of 2 MB each: 15 MB, was
1.9 GB). A slice can keep at most four times its own size alive.

**What else it changed.** Six things that were wrong are right. The first
three follow from storing text one way; the rest were found by the same
tests.

- Two halves of a surrogate pair that meet are the character they spell:
  `"\uD83D" + "\uDE00" === "😀"` was false, and so were the same halves
  brought together by `join`, `repeat` or `padEnd`; as a property key the
  two were different keys, and `for`...`of` saw two characters. A corpus case
  now checks 63 ways for two halves to meet.
- `<` and `sort()` order by UTF-16 code unit. They compared UTF-8 bytes, which
  puts `"😀"` above `"\uFFFF"`; JavaScript puts it below.
- A string too long to exist is a `RangeError`. Doubling a string in a loop
  used to end with the system killing the process at 3.8 GB. The limit is
  2^29 − 24 bytes of UTF-8, which for ASCII is V8's limit exactly and for
  other text is reached sooner than V8's.
- `String.prototype.isWellFormed` and `toWellFormed` exist.
- Text that leaves the engine is UTF-8. `console.log("\uD83D")` wrote the
  three bytes of a lone half, which no reader accepts; it writes U+FFFD, as
  Node does.
- `decodeURIComponent` accepts only UTF-8. `"%ED%A0%BD"` (a surrogate) and
  `"%C0%80"` (an overlong form) decoded to something; both are a `URIError`.
  And `decodeURI("%00")` is the NUL character; it was left as `%00`.

`tools/conformance/diff_strings.py` (`make diff-strings`, and a step in CI)
evaluates a grid in Litebird and in Node and compares every result: 36 subjects (ASCII, accented, CJK, characters
outside the BMP, lone halves in every arrangement, text that crosses the
256-unit marks) through every `String.prototype` method, the operators, the
URI and JSON functions and 51 regular expressions. That is 1,133 expressions
and 50,580 results. 95 differ, and each of the 95 is Unicode data the engine
does not have:

| Results | What is missing |
| ---: | :--- |
| 46 | collation: `localeCompare` folds case, then compares code units |
| 24 | the letter table behind `\p{L}` is a short list of ranges; fullwidth letters and most letters outside the BMP are not in it |
| 17 | normalization: `normalize()` returns its string unchanged |
| 8 | case mapping for `ǅ`, `İ` and fullwidth letters |

The engine before differs on 1,103. 720 of those are the two methods it did
not have. Of the other 383, 290 are fixed (order, halves that meet, splitting
and matching inside a pair, what a lookbehind captures, `%00`) and 93 are
among the 95 above. The other two are new: `localeCompare` of a character
outside the BMP with a lone half, which comparing bytes got right by accident
and comparing code units gets wrong. Code units agree with Node's collation on
ten more of these results than bytes do, so they stay.

It also takes the engine before about 7 minutes to get through the grid. This
one takes 3 seconds and Node half a second: the function that prints a result
reads it one character at a time, and some results are 90,000 characters.

**Tests.**

- `tests/js-conformance/long-strings.dat`: 34 scripts with Node's output, most
  of them the loops in the table at a size where the old cost is a timeout.
  The engine before passes 12 of them at best: 15 time out, 2 end the
  process, and one of the 12 (100,000 appends) takes 9.9 s of the 10 s limit,
  so on a busy machine it is 11.
- `js_string_matches_a_utf16_model_under_random_edits`: 60,000 random
  operations on a pool of strings, each compared with a `std::u16string` after
  every step and the whole pool every 50, then every pair of 585 short strings
  ordered. It ends by counting work (`JsString::stats()`): 20,000 appends may
  copy five times the final text and no more, a second pass over indexed text
  converts nothing, a tokenizer copies less than its input once.
- `js_strings_are_shared_and_read_in_constant_time` asserts the same counts
  through the interpreter, where a regression would actually come from: a
  builtin that calls `str()` on its receiver, an operator that rebuilds its
  operand.
- `js_strings_can_be_used_from_several_threads`, and `make tsan`, which builds
  the two stress tests alone under ThreadSanitizer and under AddressSanitizer
  with leak checking (the engine-wide `make asan` has leak checking off).
- How much these tests can see was measured by planting bugs in `JsString`
  one at a time. Of 47 (a wrong offset, a unit miscounted, a pair not joined,
  room claimed that is not free, a count not kept, a lone half not found, a
  copy where there should be none), the two stress tests catch 46. The other
  one changes no result and no counted work: it makes a small slice of
  non-ASCII text walk up to 256 bytes further. The first run caught 32 of 38,
  which is how the tests got their boundary characters, their search checks
  and their work counts. Of
  four data races planted the same way, ThreadSanitizer reports all four and
  the assertions alone catch two, which is why `make tsan` runs in CI.

**Not part of this.** The audit's first table had two rows that are the same
shape in other containers, and both are fixed in finding 33 (Phase 30).
