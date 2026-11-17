# Finding 72 — Module workers were unavailable and messages were JSON-shaped

Phase 66. The implementation notes are in
[`docs/phases/66-es-module-workers-and-structured-clone.md`](../phases/66-es-module-workers-and-structured-clone.md).

## 1. What was wrong

Phase 40 correctly refused `{ type: "module" }` rather than executing it as a
classic worker, but no module worker could run at all. The existing resource
loader already had the capability checks needed for a graph fetch; the missing
piece was a worker-side module map compatible with a classic-only parser.

Worker messaging used JSON text. It could not preserve cyclic or aliased
ordinary graphs, changed important primitive values, and did not provide a
proper `DataCloneError` boundary. Window and same-agent MessagePort delivery
went the other way: queued source `JsValue`s were retained directly, sharing
sender identity instead of copying it and creating avoidable asynchronous
heap ownership coupling.

## 2. What changed

Module workers now load a MIME-checked, CORS-mode static dependency graph in
the worker agent. The explicit supported syntax is side-effect/default/named/
namespace imports; simple declaration, named function/class and default
exports; local named exports; and named re-exports. Modules run in strict
private wrappers in dependency order, cache each evaluated module, and do not
expose classic `importScripts()`.

The unsupported remainder is intentional and explicit: no live bindings or
cycles, `export *`, dynamic `import()`, `import.meta`, top-level `await`,
destructuring export declarations, general module parser mode, or page module
scripts. This prevents the old silent-classic fallback while not claiming a
full module runtime.

`JsStructuredClone` carries a neutral indexed graph across Worker queues,
Window tasks, and MessagePort queues. It preserves supported primitives,
ordinary objects, arrays, aliases and cycles while decoding only in the
receiver's agent/realm. Functions, symbols, DOM/host/exotic values and
unsupported built-ins throw `DataCloneError`; empty transfer lists are no-ops
and non-empty ones fail with that same error. Packets contain no heap handles,
so queued asynchronous work does not root a sender agent's value.

## 3. How it was verified

```bash
make -j2 test
# All 212 engine test suites passed successfully!
# Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

make -j2 security-worker
# 17 checks; 0 failure(s).
```

The unit suite exercises a real nested module graph (including default, named,
namespace and named re-exports), module strictness and absent `importScripts`,
and a rejected `export *`. It also exercises clone identity/cycles, special
primitives, sender-side mutation isolation, Worker/Window/Port delivery, and
`DataCloneError` refusal. The policy runner repeats module graph loading over
HTTP while retaining worker MIME/origin/error/exit-stability coverage. A
focused ASan+UBSan build passed both engine/cross-origin binaries and the
20-iteration policy stress without a diagnostic.

## 4. What is still open

Full module semantics and transferable messaging remain separate work:
page-module execution, live bindings, cyclic module graphs, `export *`, dynamic
loading, `import.meta`, top-level `await`, import maps, and module namespace
exotica are absent. Structured clone still lacks buffers/typed arrays, broad
built-in support, transfer/detach ownership, transferred or cross-agent ports,
`SharedWorker`, and useful `messageerror` events. The global `structuredClone`
builtin remains independent of message transport.
