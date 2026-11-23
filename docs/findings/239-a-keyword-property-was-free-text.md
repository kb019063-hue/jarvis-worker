# Finding 239 — a keyword property was free text

Status: **resolved** for the keyword grammars; four function grammars still
open. Phase 235, `docs/phases/235-a-keyword-with-no-grammar.md`.

## The finding

`canonicalize_css_value` treats any property it has no rule for as free text: it
normalises the string, stores it, and `getPropertyValue` hands it back. That is
a reasonable default for a property the engine does not model — a script's
round-trip should not lose it — but it means a property is unchecked until a
rule exists, and the symptom is not an error. It is a *successful* store of
nonsense:

```js
el.style.setProperty('cursor', 'pointy');
el.style.getPropertyValue('cursor');   // "pointy"
```

Twenty-seven properties were in that state at once, all of them keyword-only,
all of them with a grammar short enough to write down.

## What a reader should take from it

- **This is the third finding in a row of the same shape.** Finding 206: an
  `outline` the CSSOM reported and no `ComputedStyle` field held. Finding 221:
  two `text-decoration` lines that reached the painter and were dropped there.
  Here: a declaration that enters the style because no rule refused it. In all
  three the interface a page checks said yes and the engine could not act on it.
  The common cause is a *default* that accepts too much: a table of rules that
  answers "any" when it has no entry.
- **A default of "accept" hides missing work.** `ValueKind::Any` is correct for
  a property the engine genuinely does not model; it was being applied to
  properties that simply had no rule yet, and the two are indistinguishable from
  outside.
- **Not every gap should be closed by the same means.** `font-weight: 700` and
  `cursor: url(...), auto` show that a keyword set is sometimes only half a
  grammar. Checking half and refusing the other half is a regression dressed as
  a fix, which is why `contain`, `filter`, `mask` and `clip-path` were left
  free text rather than given keyword-only rules.

## The remaining four

`contain` (`layout style paint`, `content`, `size`), `filter`
(`blur(2px) contrast(1.2)`, `url(...)`), `mask` and `clip-path`
(`inset(...)`, `polygon(...)`, `url(...)`) each need a function-aware check.
The engine already has function parsing for `rusty` values other properties use
(`transform`, `background-image`); these four never got wired to it.
