# Phase 559 — named module imports stay live

**Finding:** [`docs/findings/559-named-imports-were-snapshots.md`](../findings/559-named-imports-were-snapshots.md)

The static module-worker loader published live export namespaces, but it made
an importer-local `const` from each namespace property before running the
module body. That copied named imports: after an imported function changed an
exported `let`, the importer's name still returned the initial value.

## What changed

- Named import properties are now accessors over the dependency's live export
  namespace. The importing module's object environment resolves the local
  names through those getters on each read; namespace imports remain namespace
  objects.
- The generated object environment is only the carrier for bindings. The
  module body runs inside a strict inner function, and bare calls of imported
  functions retain `undefined` as `this` rather than receiving the carrier
  object. Imported accessors have no setter, so strict assignment to an
  imported binding throws `TypeError`.
- Named re-exports use the same live accessor path. The existing module-worker
  regression was updated to expect a named import to read `2` after the
  dependency's `bump()` (previously it asserted the stale `1`).
- The new standalone suite, `tests/suites/559-live-named-module-imports.cpp`,
  checks a reassigned export through a named import, an alias, a namespace
  import and a named re-export, as well as immutable assignment, bare-call
  `this`, and legitimate lexical/property-name shadowing.

## Scope

This fixes named-binding liveness within the existing static module-worker
subset. It does not add complete module records or broaden the accepted module
grammar: dynamic `import()`, `import.meta`, cycles, top-level `await`,
`export * as ns`, and transferable module messaging remain open.
