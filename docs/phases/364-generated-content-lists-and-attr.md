# Phase 364 — generated content keeps a list and reads the attribute

Task: the row that begins "`::before` and `::after` generate since Phase 215".
Finding 364 (`docs/findings/364-generated-content-was-a-single-string.md`).

## 1. What was still open after Phase 215

Phase 215 gave every `::before` and `::after` that carries a single quoted
string a box: the Rust Book's icon-only anchor went from `800×0` to `800×20`
and `#a::before{content:"XXX"}` grew a line 30.2 → 63.1. The remaining
open part of the same row was that only a string was represented:

- `counter()`, `attr()`, `url()` and the content list form generated nothing
  rather than the literal text `"counter(x)"`;
- only one string was allowed, so `content: "a" "b"` was invalid;
- `open-quote` / `close-quote` had no value.

A button whose label comes from its own attribute — `data-label`,
`title`, `aria-label` — is `content: attr(data-label)` on a pseudo-element.
Every icon-font control is the first bullet, every `attr()` control the
second.

## 2. What the engine did

`StyleResolver::apply_longhand` for `content` called `parse_content_string`,
which answered true only for exactly one quoted string with nothing but
whitespace after it. Anything else — a second string, `attr()`, a function,
two tokens — cleared `style.content`, which is what generates no box. The
resolver's `resolve_pseudo_element` then replayed the same call, so the
element that owned the pseudo-element was never asked.

Layout's `generated_run` already concatenated `pseudo.content` into a single
`TextBox` with `collapse_whitespace`; the box itself was correct, only the
string it was given was not.

## 3. The fix

`StyleResolver::evaluate_content(raw, element, vars)` replaces the single-string
check. It is called from `apply_longhand` (with no element) and from
`resolve_pseudo_element` (with the element that owns the pseudo-element, and
with the pseudo-element's own `custom_properties` so `var()` is resolved in
the right place).

The function:

1. resolves `var()` with `resolve_var_functions`;
2. returns empty for the single-keyword values `normal`, `none`, `inherit`,
   `initial`, `unset` (all mean "no box");
3. walks the raw value left to right, skipping ASCII whitespace, and for each
   token:
   - a quoted string (`"` or `'`) is decoded with the same CSS-escape logic
     as `parse_content_string` (hex escapes, escaped newlines, surrogate
     handling) and appended;
   - `attr(<name> [<type>]? [, <fallback>]?)` reads the element's attribute
     of that name (HTML attributes are lower-cased for the lookup); if the
     attribute is absent the optional fallback string (quoted, with escapes)
     is used, otherwise nothing for that item;
   - `url()`, `counter()`, `counters()` and any other function are skipped
     for this item — the list continues, rather than the whole declaration
     being discarded;
   - bare idents `open-quote` / `close-quote` append `"` and
     `no-open-quote` / `no-close-quote` append nothing;
   - any other ident or separator is ignored.

A list such as `"a" attr(x) "b"` therefore produces `"a"` + value of `x` +
`"b"` with no added whitespace; `"a" "b"` produces `"ab"`. A declaration
that mixes an unknown function with strings, e.g.
`"a" url(x.png) "b"`, produces `"ab"` — the unknown item is one empty
item, not a poison pill for the whole declaration. That is the honest
answer for a value the engine cannot mean, and the same one Phase 215 gave
for `counter()` alone.

`parse_content_string` is kept for the comment and for any caller that still
wants the single-string check, but the cascade no longer uses it.

## 4. Measured after

The existing suite `pseudo_elements_generate_a_box_that_can_be_hit` still
passes: a single-string icon still measures `9.60156` wide and still has
height, and `#a::before{content:"XXX"}` still grows the line.

The new suite `generated_content_lists_and_attr` (10 checks, all failing on
`main` at `91460c8`) now passes:

```
#a::before{content:"hello"}                          -> "hello" (regression)
#b::before{content:"a" "b" "c"}                      -> "abc"
#d::before{content:attr(data-value)}                 -> "42"
#e::before{content:"prefix-" attr(data-value) "-suffix"} -> "prefix-hello-suffix"
#f::before{content:attr(data-missing, "fallback")}  -> "fallback"
#i::before{content:open-quote "hi" close-quote}      -> "\"hi\""
#j::before{content:"a" url("x.png") "b"}            -> "ab" (url skipped)
#l::before{content:normal}                           -> "" (no box)
#o::before{content:"\f105" "-" attr(data-x)}         -> U+F105 + "-ok"
#q::before{content:"[" attr(data-v) "]"}             -> "[42]" as a TextBox, DOM textContent still "mid"
```

Variables are expanded first, so:

```css
:root{--x:"hi"} #p::before{content:var(--x) "!"}
```

produces `"hi!"`.

```
engine suites        395/395 (the new suite among them)
render reftests      48/48 and 41/41 accounted
tree-construction    1973/1973
JS conformance       1149/1158 (99.2%)
DOM conformance      678/678
```

## 5. What is not claimed

- `counter()` / `counters()` — the counter table (`counter-increment`,
  `counter-reset`, `counter-set`) does not exist, so those functions still
  produce no text. The list now tolerates them instead of discarding the
  neighbouring strings.
- `url()` — a `url()` in `content` is a replaced element (an image), not
  text. It still produces no box here.
- `::first-line`, `::first-letter`, `::marker`, `::placeholder`,
  `::selection` — no box is generated for them; they share only the
  `has_pseudo_element` bool that Phase 215 noted.
- `quotes` property — `open-quote` / `close-quote` always produce `"`; a
  real `quotes` list with nesting is not read.
- `content: normal` on a regular element vs pseudo-element distinction —
  both are treated as "no box", which matches the pseudo-element case that
  is the only one that generates.

## Files

- `include/litebird/css/StyleResolver.hpp` — `evaluate_content`
- `src/css/StyleResolver.cpp` — list parsing, `attr()` with fallback, quote
  handling, var resolution in the pseudo-element path
- `tests/suites/364-generated-content-lists-and-attr.cpp`
- `AGENTS.md`, `AUDIT.md` — the row and the finding (row remains open)

