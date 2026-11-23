# Phase 235 — a keyword with no grammar took any word

Task: `CSSStyleDeclaration.setProperty` accepted any string for a keyword
property. Finding 239.

## 1. What was wrong

```js
el.style.setProperty('table-layout', 'totallybogusvalue');
el.style.getPropertyValue('table-layout');   // "totallybogusvalue"
el.style.cssText;                            // "table-layout: totallybogusvalue;"
getComputedStyle(el).tableLayout;            // "totallybogusvalue"
```

A browser drops the whole declaration and reports the initial value (`auto`).
This engine stored the text and reported it back.

The cause is one line of design in `canonicalize_css_value`: a property with no
entry in `property_rules()` is treated as free text. That is deliberate — it is
what lets an unmodelled property survive a round-trip — but it means a property
gets *no* checking until someone writes a rule for it, and the failure is
silent in the worst way: the declaration looks accepted.

Twenty-seven properties were in that state:

| group | properties |
| :--- | :--- |
| table | `table-layout`, `border-collapse`, `empty-cells`, `caption-side` |
| writing | `writing-mode`, `direction`, `unicode-bidi` |
| compositing | `mix-blend-mode`, `isolation` |
| replaced content | `object-fit` |
| text | `white-space`, `word-break`, `overflow-wrap`, `hyphens`, `vertical-align` |
| interaction | `pointer-events`, `user-select`, `resize`, `cursor`, `touch-action`, `scroll-behavior` |
| lists and backgrounds | `list-style-type`, `background-repeat`, `background-attachment`, `background-clip`, `background-origin` |
| flexbox | `justify-content`, `align-items`, `align-self`, `align-content`, `justify-items` |
| font and paint hints | `font-weight`, `appearance`, `will-change`, `image-rendering` |

## 2. What changed

Each property gets a `ValueKind::Keyword` rule with the keywords CSS allows.
The validator already existed and already worked — `ValueKind::Keyword` returns
false for a value not in its list, and that is what `display: bogus` has always
done. The rules were simply missing.

Two properties needed more care:

- **`font-weight`** takes `700` as well as `bold`, so its rule is
  `ValueKind::Number` carrying the four keywords: keywords are tried first, and
  a number is accepted by the fall-through. A keyword-only rule would have
  refused `font-weight: 700`, which is worse than not checking at all.
- **`contain`, `filter`, `mask`, `clip-path`** are left alone on purpose. Their
  grammars are function lists (`blur(...)`, `url(...)`, `polygon(...)`,
  `contain: layout style`), and a keyword-only rule would refuse values a
  browser keeps. Refusing a valid declaration is a worse bug than accepting an
  invalid one, so they stay free text until someone writes the grammar.

## 3. Result

```
before: accepted-bogus: 27 properties
after:  accepted-bogus: contain | filter | mask | clip-path
```

and the values that were always valid still are:

```
table-layout: fixed          -> "fixed"
list-style-type: upper-roman -> "upper-roman"
font-weight: bold            -> "bold"
font-weight: 700             -> "700"
mix-blend-mode: multiply     -> "multiply"
cursor: pointer              -> "pointer"
table-layout: bogus          -> ""      (dropped)
font-weight: verybold        -> ""      (dropped)
cursor: pointy               -> ""      (dropped)
width: nonsense              -> ""      (unchanged: lengths were always checked)
```

## 4. How it was verified

```
make test               All 373 engine test suites passed successfully!
make js-conformance     JS conformance, full corpus: 1149/1158 (99.2%)
make dom-conformance    DOM conformance, full corpus: 678/678 (100.0%)
make wpt                Web Platform Tests: 9567/9824 subtests pass in 190 files
make render-conformance Canvas toDataURL: 45016/45016 checks agree
make check-docs         check_docs: 0 failure(s)
make check-numbers      check_numbers: 0 failure(s)
```

The new suite is `tests/suites/235-css-keyword-values.cpp`
(`a_keyword_value_outside_the_grammar_is_not_a_declaration`). For each of the
35 properties it sets a keyword CSS allows and a value nothing allows, and
asserts the first is stored and the second is not; it also asserts
`font-weight: 700` survives the new rule. With the rules removed it fails at its
first `refuses(...)` assertion.

## 5. What is still open

- `contain`, `filter`, `mask` and `clip-path` still take any string; each needs
  its function grammar in `canonicalize_css_value`.
- A keyword rule checks the *first* keyword only for properties whose real
  grammar is a list (`will-change: transform, opacity` is refused), so those
  need a comma-separated split before the check.
- The checker validates what a script sets. A `<style>` sheet's declarations go
  through the CSS parser and the cascade, which have their own (looser) idea of
  what is valid, so an invalid keyword in a sheet is still kept there. That is a
  separate finding.
