# Finding 364 — generated content was a single string

**Severity:** medium — any `::before`/`::after` that builds its text from an
attribute, from two strings, or from a quoted string plus a fallback is a
`content: attr(data-label)` button, and every such button was empty.

**Found by:** re-reading the row that Phase 215 left open: "only a string is
represented, so `counter()`, `attr()`, `url()` and the content list generate
nothing". Phase 215 had proved that `content: counter(x)` should generate
nothing rather than the literal `"counter(x)"`; the same gate now asked what
`content: attr(x)` and `content: "a" "b"` should do, and the answer was also
"nothing" where the standard says "the attribute's value" and `"ab"`.

## What was wrong

Two places, one decision:

1. **`apply_longhand` for `content`** called `parse_content_string`, which
   returns true only for exactly one quoted string with only whitespace after
   it. `content: "a" "b"` has a second `"` after the first string, so it
   returned false; `content: attr(x)` starts with `a` not `'`, so it returned
   false; both cleared `style.content`.

2. **`resolve_pseudo_element`** replayed the same call for every matched
   declaration. Even if the cascade had remembered `attr(x)`, the resolver
   never had the element to ask for the attribute, so a fallback could not
   be read.

Layout's `generated_run` was correct: it made a `TextBox` from
`pseudo.content` with `collapse_whitespace`. The string it was given was
empty.

## Measured before

On `91460c8` (main):

```
#b::before{content:"a" "b" "c"}                         -> "" (no TextBox)
#d::before{content:attr(data-value)} with data-value="42" -> "" (no TextBox)
#f::before{content:attr(data-missing, "fallback")}    -> "" (no TextBox)
#j::before{content:"a" url("x.png") "b"}               -> "" (whole list cleared)
```

The existing suite `pseudo_elements_generate_a_box_that_can_be_hit`
asserted that `content:counter(x)`, `content:none`, `content:attr(title)`
each generate nothing — all three were true, but for the same reason:
"nothing generates nothing". The test that would have distinguished the
second case — `content: attr(title)` where `title="t"` should be `"t"` — was
not written, and `content: "a" "b"` was not probed at all.

## The fix

`StyleResolver::evaluate_content(raw, element, vars)` walks the declaration
value after `var()` resolution:

- quoted strings are decoded with the CSS-escape logic Phase 215 introduced;
- `attr(name [, fallback])` reads the element's attribute or its fallback
  (the fallback may be a quoted string with escapes);
- `url()` / `counter()` / `counters()` produce no text for that item but keep
  the rest of the list;
- `open-quote` / `close-quote` produce `"` (a real `quotes` property is
  still not read).

`apply_longhand` now calls it with no element (so `attr()` without a fallback
is empty, which is the same as "the attribute is absent"); the pseudo-element
path calls it with the owning element.

## Measured after

On this branch at the same oracle:

```
#b::before{content:"a" "b" "c"}                         -> "abc" TextBox present
#d::before{content:attr(data-value)}                   -> "42"
#f::before{content:attr(data-missing, "fallback")}    -> "fallback"
#j::before{content:"a" url("x.png") "b"}               -> "ab" (url skipped, not fatal)
```

`pseudo_elements_generate_a_box_that_can_be_hit` still passes, and the new
suite `generated_content_lists_and_attr` covers the ten cases above
(failing on `main`, passing here).

## What is not claimed

- `counter()` / `counters()` still produce no text (no counter state exists);
  the list now tolerates them.
- `url()` still produces no box (no replaced-element generation for
  pseudo-elements).
- `::first-line`, `::first-letter`, `::marker`, `::placeholder`,
  `::selection` still match no box.
- Glyph fallback (U+F105 in a font without it measures `9.6px`) is unchanged;
  Phase 215's note still applies.

