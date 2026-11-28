# Finding 559 — named imports were snapshots

**Severity:** stale module value. The worker module loader exposed live export
namespaces, but generated a local `const` for each imported binding before
running the importer. A dependency could update its exported `let` and the
importer's named binding would still show the earlier value.

**Repro:**

```js
// state.js
export let value = 1;
export function increment() { value++; }

// entry.js
import { value, increment } from './state.js';
const before = value;
increment();
const after = value; // must be 2, not the captured 1
```

The engine's old snapshot path returned `1` for both reads.

**What changed:** imports now resolve through accessors on the generated
module environment, backed by the dependency's live namespace. The module body
still runs in a strict function; bare imported-function calls keep
`this === undefined`, and imported bindings remain immutable. The regression
suite also checks namespace and named re-export reads, local shadowing and the
`TypeError` for assignment. See [Phase 559](../phases/559-live-named-module-imports.md).

**Scope:** static module-worker named bindings only. This does not complete
module records or enable cycles, dynamic imports, `import.meta`, top-level
`await`, `export * as ns`, or transferable module messaging.
