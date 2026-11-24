# Finding 248 — a function value was free text

**Severity:** validation, not correctness of what is stored. An unparseable
`filter`, `mask`, `clip-path` or `contain` declaration was accepted by
`setProperty`, kept in the declaration block and echoed back by
`getPropertyValue` and `cssText`, where a browser drops it. (`will-change`
had the opposite bug: a valid declaration was refused.)

**Found by:** taking Phase 235's open list at its word, as the rules say to —
`docs/phases/235-a-keyword-with-no-grammar.md` §5 names exactly these five
properties.

## What it was

`property_rules()` had no rule for the four function-grammar properties, and
`normalize_value` treats a ruleless property as free text — deliberately, so
an unmodelled property survives a round-trip. The failure mode Phase 235 named
is silent: the declaration looks accepted. `will-change` had a keyword rule
naming six idents, and `setProperty('will-change', 'transform, opacity')`
compared the whole string against that list and dropped it.

## What the fix holds

Each of the five has its own validator (`src/js/DomStyleBindings.cpp`), run
after the global-keyword and `var()` passes. Design choices that are easy to
get wrong, recorded so the next reader does not:

- **`none` is early-returned in all four grammars.** The first draft of this
  phase refused `clip-path: none`; the suite caught it. The trap behind it:
  in the REPL `document.body` is `null`, so an appended-then-styled element
  never got its style set and `getPropertyValue` read as `""` — a missing
  declaration looks identical to a refused one from inside that probe, and so
  does the *initial* value when no declaration exists at all
  (`getPropertyValue('clip-path')` on an untouched element answers `none`
  here). Probes against the REPL are not a substitute for the suite.
- **`ellipse()` takes two radii or none, `circle()` one or none**, and a
  position after `at` is one or two tokens (`at center`, `at left top`).
- **A drop-shadow has no spread**, its two offsets may be negative, its blur
  (third length) may not, and its one color token may come first or last
  (`drop-shadow(red 2px 3px)`).
- **Each `mask` layer needs a `<mask-reference>`**; a layer of pure
  components (`mask: luminance`, `mask: cover`) is refused.
- **`will-change` accepts any custom ident.** A whitelist of property names
  would refuse valid declarations (Phase 235's own rule: refusing a valid
  value is the worse bug). `auto` must stand alone and a CSS-wide keyword is
  refused inside a list.
- **`shape()` is checked structurally** (balanced parens, safe charset), not
  modelled; a valid `shape()` is never refused for a missing feature.
- `url()` in `clip-path` stands alone; in `filter` it may appear beside
  other functions.

## Still open

The `<style>` sheet path (separate finding, unchanged since Phase 235);
`cursor` url-lists; `touch-action` and `background-repeat` multi-keyword
lists; gradient argument contents; the `mask-*` longhands.
