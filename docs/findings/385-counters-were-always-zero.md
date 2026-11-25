# Finding 385 — counters were always zero

**Severity:** medium — any `::before`/`::after` that numbers itself with
`counter()`/`counters()` was empty; the engine explicitly skipped those items.

**Found by:** re-reading the row Phase 364 left open: "only a string is
represented, so `counter()`, `attr()`, `url()` and the content list generate
nothing" — after `attr()` landed, `counter()` remained as that same gate.

## What was wrong

Two places, one decision:

1. **`apply_longhand` for `counter-*`** did not exist. `counter-reset`,
   `counter-increment`, `counter-set` were unknown properties and discarded, so
   no element established or mutated a counter.

2. **`evaluate_content` for `counter()`/`counters()`** was

   ```cpp
   else if (lower_ident == "url" || lower_ident == "counter" || lower_ident == "counters") {}
   ```

   — an empty item. `content: counter(n)` therefore produced `""` (no box),
   and `content: "a" counter(n) "b"` produced `"ab"` without the number.
   `counters()` had no scope stack to join.

3. **`LayoutTree::build_boxes`** generated pseudo runs from `pseudo.content`
   directly, which had already lost the counter items. Even if the cascade had
   remembered them, layout had no counter state to answer `counter(n)`.

## Measured before

On `928f4b7` (main, after Phase 364):

```
body{counter-reset:n 0} .x{counter-increment:n 1} .x::before{content:counter(n)}
  => "" (no TextBox) for both siblings
.x::before{content:"#" counter(n) " " attr(data-lbl)} with data-lbl="a"
  => "# a" (counter missing, list tolerated but empty)
counters(sec, ".") => "" (no stack)
counter(missing) => "" (spec says "0" for absent counter, same as empty)
```

The existing suite `pseudo_elements_generate_a_box_that_can_be_hit` asserted
that `content:counter(x)` generates no `counter(x)` literal — true, but for
the same reason it generated nothing at all.

## The fix

- `ComputedStyle` gains `content_raw` plus `counter_reset`/`counter_increment`/
  `counter_set` vectors; `hash()` and `operator==` include them.
- `StyleResolver::parse_counter_ops` parses `none` or `<name> [<int>]` lists
  with defaults `0`/`1`/`0`.
- `StyleResolver::evaluate_content(..., counters)` resolves `counter(name[,
  style])` and `counters(name, sep[, style])` against a
  `map<string, vector<int>>` of stacks, formatting via `format_counter_value`
  (`decimal`, `decimal-leading-zero`, `lower/upper-roman`, `lower/upper-alpha`).
- `LayoutTree::build_boxes` threads a `CounterContext`:
  reset pushes, set/increment mutate the top, `::before`/`::after` re-evaluate
  `content_raw` with the current stacks, and resets are popped after the
  subtree.

## Measured after

On this branch at the same oracles:

```
.x::before{content:counter(n)} with two siblings => "1", "2"
h1/h2 nested with counters(sec,".") => "1.1", "1.2", "2.1"
counter(r, lower-roman) across three increments => "i","II","c"
counter-set: n 10 => "1","10","11"
"#" counter(n) " " attr(data-lbl) => "#1 a","#2 b"
counter(missing) => "0" and "x0y"
```

`pseudo_elements_generate_a_box_that_can_be_hit` still passes; the new suite
`counters_in_content` covers the ten cases above (failing on `main`, passing
here).

## What is not claimed

- `url()` still produces no box.
- `::first-line`, `::first-letter`, `::marker`, `::placeholder`,
  `::selection` still match no box.
- `list-style` outside `content` and implicit `<ol>` counters are not
  implemented; only explicit `counter-*` properties.
