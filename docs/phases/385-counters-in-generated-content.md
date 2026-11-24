# Phase 385 — counters in generated content

Task: the row that begins "`::before` and `::after` generate since Phase 215".
Finding 385 (`docs/findings/385-counters-were-always-zero.md`).

## 1. What was still open after Phase 364

Phase 215 gave every `::before`/`::after` with a single quoted string a box.
Phase 364 extended it to a list: strings plus `attr()` (with fallback),
`open-quote`/`close-quote`, and tolerated `url()`/`counter()` items as empty
rather than invalid. The remaining open part of the same row was `counter()`
and `counters()`:

- `counter-reset`, `counter-increment`, `counter-set` were parsed as unknown
  declarations and discarded;
- `content: counter(n)` was parsed as an empty item, so `counter(n)` in a list
  generated nothing instead of `"1"` etc.;
- `counters(n, "-")` had no scope stack, so nested lists could not produce
  `"1-1"`.

A table of contents, an ordered list, a heading numbered `1.2.3` — every such
control is `content: counter(...)` on a pseudo-element.

## 2. What the engine did

`StyleResolver::evaluate_content` handled `counter`/`counters` as

```cpp
else if (lower_ident == "url" || lower_ident == "counter" || lower_ident == "counters") {}
```

— explicitly empty. `apply_longhand` for `content` stored the evaluated string
and nothing else; `counter-*` declarations were not parsed, and `ComputedStyle`
had no place to keep them. `LayoutTree::build_boxes` generated the pseudo run
from `pseudo.content` directly, so even after `attr()` worked, a counter was
still an empty item, not a poison pill, but still empty.

Layout had no counter state at all: no stacks, no resets, no increments, and
no layout-time evaluation of `content`.

## 3. The fix

### 3.1 Cascade

`ComputedStyle` now carries:

```cpp
struct CounterOp { std::string name; int value; };
std::string content_raw;                 // raw declaration for late evaluation
std::vector<CounterOp> counter_reset;
std::vector<CounterOp> counter_increment;
std::vector<CounterOp> counter_set;
```

`content_raw` keeps the declaration text (`'"a" counter(n) "b"'`) so layout can
re-evaluate it with the current counters; `content` remains the
cascade-time evaluation for non-counter uses.

`StyleResolver::parse_counter_ops(raw, default_value)` parses
`counter-reset` / `counter-increment` / `counter-set`:

- `none` → empty,
- otherwise a whitespace-separated list of `<name> [<integer>]?` — the integer
  defaults to `0` for `reset`/`set` and `1` for `increment`,
- names keep their case but are compared case-insensitively at layout time.

`apply_longhand` for `content` stores both `content_raw` and `content`;
for `counter-*` it fills the three vectors. `resolve_pseudo_element` does the
same for the pseudo-element's own matched declarations.

`evaluate_content(raw, element, vars, counters)` gains an optional fourth
argument — `unordered_map<string, vector<int>> *counters`. Helpers added:

- `eval_counter_function` — `counter(name [, style])`,
- `eval_counters_function` — `counters(name, separator [, style])`,
- `format_counter_value` — `decimal` (default), `decimal-leading-zero`,
  `lower-roman`/`upper-roman` (1..3999), `lower-alpha`/`upper-alpha` (and
  `lower-latin`/`upper-latin` aliases), `none` → `""`.

`split_counter_args` respects quoted commas so `counters(sec, ".", upper-roman)`
parses correctly; `decode_counter_string_arg` decodes the separator's CSS
escapes.

### 3.2 Layout

`LayoutTree::build_boxes` now threads a `CounterContext`:

```cpp
struct CounterContext {
  unordered_map<string, vector<int>> stacks;
  vector<string> apply_reset(ops); // push 0-based scopes
  void apply_set(ops);             // set top or push
  void apply_increment(ops);       // add or create
  void pop_resets(pushed);
};
```

Threaded via `thread_local CounterContext* t_counter_ctx` so the recursive
`build_boxes` signature need not change for `display:contents` etc.:

1. At top of element handling, apply `reset`/`set`/`increment` from its
   `ComputedStyle` — reset pushes, increment runs before `::before` (so a
   `counter-increment: n 1` with `::before { content: counter(n) }` shows
   `1` for the first sibling, matching the spec's typical `li` case).
2. `generated_run(which)` resolves the pseudo-element, then re-evaluates
   `pseudo.content_raw` with `element`, `pseudo.custom_properties`, and
   `t_counter_ctx->stacks`. If the raw is empty it falls back to `pseudo.content`
   (string-only Phase 215 case). An empty final string generates no box.
3. Reurse into children (they inherit the same stacks).
4. Generate `::after` similarly.
5. Pop the resets pushed for this element (sets/increments are not popped).

`LayoutTree::build` creates a fresh `CounterContext` per layout; `build_box`
does the same for subtree builds. `hash()` includes the new members.

## 4. Measured after

The existing suite `pseudo_elements_generate_a_box_that_can_be_hit` still
passes: a single-string icon still measures `9.6px` and still has height.

The new suite `counters_in_content` (10 checks) covers:

```
body{counter-reset:n 0} .item{counter-increment:n 1} .item::before{content:counter(n) ". "}
  => "1. " "2. "
h1{counter-increment:sec 1; counter-reset:sub 0} h2{counter-increment:sub 1}
h1::before{content:counter(sec) ". "} h2::before{content:counters(sec,".") "." counter(sub) " "}
  => "1. " "1.1 " "1.2 " "2. " "2.1 "
#a::before{content:counter(r, lower-roman)} => "i", "II", "c"
counter-set: n 10 => jump from "1" to "10" to "11"
"#" counter(n) " " attr(data-lbl) => "#1 a", "#2 b"
counters(item, "-") with nested ol/li => joined stack "1-1" (smoke)
counter(missing) => "0", "x0y"
counter-increment:none / counter-reset:none => still "5"
::before and ::after both see incremented value
DOM textContent unchanged, resolved_style(el).content empty, generated TextBox hit-testable
```

No `url()`/`marker`/`::selection` change.

## 5. What is not claimed

- `list-style-type` outside `counter()` is still not implemented; `counter()`
  inside `content` now honours `lower-roman` etc., but `list-style: upper-roman`
  on `<ol>` does not.
- Implicit HTML counters (`<ol>` numbering) are not generated — only explicit
  `counter-*` properties.
- `counter-set` is implemented as set-or-create at current scope; the spec's
  `reversed()` and `symbols()` counter styles are not.
- `url()` in `content` remains a skipped item (no replaced element).
- `::first-line`, `::first-letter`, `::marker`, `::placeholder`,
  `::selection` still generate no box.

## Files

- `include/litebird/css/StyleResolver.hpp` — `CounterOp`, `content_raw`, three vectors, counter-aware `evaluate_content`
- `src/css/StyleResolver.cpp` — `parse_counter_ops`, counter helpers, `evaluate_content` overload, `apply_longhand` for `counter-*`/`content_raw`
- `src/layout/LayoutTree.cpp` — `CounterContext`, `t_counter_ctx`, reset/set/increment + pop, re-evaluation of pseudo content
- `tests/suites/385-counters-in-generated-content.cpp`
- `AGENTS.md`, `AUDIT.md` — row remains open (counter styles, url, other pseudos)
