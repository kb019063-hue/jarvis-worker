# 39. ✅ `:nth-child( An+B )` reads a sign separated from its digits by spaces

> Moved out of `AUDIT.md` — written there as a `## 39. ✅ `:nth-child( An+B )` reads a sign separated from its digits by spaces` section.
> The text below is the section as it stood, unchanged.


**The hole.** The formula matcher (`src/css/StyleResolver.cpp`,
`match_nth_formula`) parsed the offset part of an `<an+b>` formula with
`std::stoi`. `"+ 1"` is not parseable by `stoi` — the sign must be attached
to the digits — so the `catch` branch silently defaulted the offset to `0`.
`li:nth-child(2n + 1)` matched only the even items; with three items, only
`b` turned red. Any formula written with a space between the sign and the
number (`2n + 1`, `-n + 3`, `+ n + 1`, the common style in real pages) was
read as `b = 0`, and `:nth-of-type()` shared the matcher and the bug.

**The fix.** The `<an+b>` micro-syntax ignores whitespace tokens, exactly as
the CSS parser does, so the matcher now erases CSS whitespace (Tab, LF, FF,
CR, Space) between tokens before parsing: `2n + 1`, `2n+1` and
`2n\t+\n1` are the same formula, and a sign may sit next to or away from the
digits it signs (`+ 2n + 1` is `+2n+1`). The coefficient and the offset are
parsed as strictly signed integers (`parse_b_integer`): an empty coefficient
or a bare sign means `1`, and anything that is not a signed integer (`1.5n`,
`xn`, `n++1`, `1+1`, `++1`) makes the formula **invalid**, so it matches
nothing — as an invalid selector must — instead of falling back to `a = 1`
or `b = 0` (both silent-default bugs found while fixing this one, both dead
now). `:nth-of-type()` is fixed by the same change.

**How verified.** The finding-35 repro now paints items 1 and 3 red and item
2 dark:
`printf '<ul><li>a</li><li>b</li><li>c</li></ul><style>li:nth-child(2n + 1){color:rgb(255,0,0)}</style>' > /tmp/nth.html && ./build/litebird /tmp/nth.html --dump-paint`.
`make test` passes 133 suites including the new `nth_child_formula_parsing`
(the sign-space matrix `2n + 1`, `2n+ 1`, `2n +1`, tab/newline/FF/CR between
tokens, `2N+1`, `+ 2n + 1`, plus `odd`/`even`/exact/`0n+2`/negative steps,
and an invalid list that must match nothing and never crash). The benchmark
keeps its glued `+1` so it still measures the trimming cost, untouched.
`make bench-css` passes 20/20 with dumps identical to the compact pages;
`make conformance` is 1781/1792, `make dom-conformance` is 629/629,
`make js-conformance` is 840/845 and `make url-conformance` is 1188/1188 —
the corpora are unchanged.

**What is still approximate.** The whitespace set is the five code points
above, not the CSS whitespace production in general (finding 35 documents
the same limit in the trim path); an empty token span such as `:nth-child()`
or a comment inside the formula (`2n/*x*/+1`) matches nothing, as an invalid
argument does. The grammar covered is the `<an+b>` subset of the micro-syntax
(`odd`, `even`, an optional signed coefficient, `n`, an optional signed
offset, a plain integer, and arbitrary whitespace between the tokens); it is
deliberately not a general CSS tokenizer. A sign separated from its
coefficient by whitespace (e.g. `- 2n + 1`) is accepted, which token-boundary
purists may call generous; no valid formula is rejected.

---
