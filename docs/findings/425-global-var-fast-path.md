# Finding 425 — the global `var` paid for a property path twice

Status: **resolved and measured.** Phase 425,
[`docs/phases/425-global-var-fast-path.md`](../phases/425-global-var-fast-path.md).
This corrects the next-open-item diagnosis recorded in Phase 204, finding 208.

## The finding

A script-level `var` that has no global environment binding is stored as an own
data property of the global object. For an assignment, `assign_identifier`
first walks the environment and misses; the generic setter then makes the
Window and global-object hooks check for an environment binding again. On the
page path those checks miss and the property is stored once. It was not a
binding write followed by a second write to that binding, as the earlier
finding described.

`i++` on such a global took the same generic route: no scope binding, then a
`Reference`, a global-property read, another environment walk on write, and the
property setter. Phase 204's measurement (748 ms at top level versus 355 ms
inside a function) had shown the cost, but had not separated the repeated
lookup from a second store.

## What changed

- After `env->try_assign` misses, `assign_identifier` directly stores a
  writable own data property only when the global object itself is not frozen.
  Accessors, non-writable properties, and frozen globals still use
  `set_property`, preserving setters and strict-mode errors.
- `eval_update` directly updates a writable own global data property when the
  global is unfrozen and its value is a side-effect-free primitive. Object
  coercion, accessors, read-only properties, `with` scopes, and unresolved
  names retain the general `Reference` path.
- `tests/suites/425-global-var-fast-path.cpp` covers global-object reflection,
  string/object coercion (including a descriptor mutation from `valueOf`),
  accessors, strict refusal, frozen-global writes, `with`, and local shadowing.

## Measurement

With `make bench-js-core` and the pre-change binary on the same host, the
1.5-million-iteration top-level case moved from **821.2 ms to 424.0 ms
(1.94× faster)**. Node.js v22.22.3 ran the same global case with `-e` in
43.1 ms, making the engine **9.8× Node** after the change. The six local cases
remained within the host's ±20% run-to-run movement; no speedup is claimed
for them.
