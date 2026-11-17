# Phase 66 — ES module workers and structured clone

Finding 72. The companion finding is
[`docs/findings/72-es-module-workers-and-structured-clone.md`](../findings/72-es-module-workers-and-structured-clone.md).

## 1. What was wrong

`new Worker(url, { type: "module" })` was deliberately rejected by Phase 40.
That avoided silently treating a module as a classic script, but it also meant
that a worker could not use a module graph even though the resource loader
could resolve relative URLs, enforce MIME/status policy, and perform CORS
checks.

Worker messages were encoded with `JSON.stringify()` on one side and parsed on
the other. This changed or lost `undefined`, `NaN`, infinities, `-0`, aliases,
and cycles; it silently coerced some unsupported values rather than reporting
a cloning failure. The worker queue no longer did that after the first part of
this phase, but `Window.postMessage()` and the Phase 61 same-agent
`MessagePort` queue still held sender-agent `JsValue`s directly, so they shared
object identity and made asynchronous ownership unnecessarily fragile.

## 2. What changed

### Static module worker graph

`WorkerObject` now fetches a module entry with `RequestMode::Cors` and starts a
small module map in the worker agent. Each dependency uses the existing
`ResourceLoader` with the referrer's final URL as its base, and retains the
loader's HTTP status, JavaScript MIME, local-file, redirect, and CORS checks.
A module is evaluated once before its importers in a strict wrapper with a
private lexical top level. `importScripts` is intentionally absent from a
module-worker global; classic workers retain it and its `no-cors` classic
loading behavior.

The parser remains a classic-script parser, so the loader translates this
explicit static subset before evaluating the ordinary module body:

- side-effect imports and default, named, and namespace imports;
- `export var`/`let`/`const` with simple identifier bindings;
- named function/class exports, `export default`, local named export lists,
  and named re-exports (`export { x as y } from "…"`).

Imported/re-exported values are snapshots at module evaluation time, not live
module bindings. Cyclic graphs, `export *`, dynamic `import()`, `import.meta`,
top-level `await`, destructuring export declarations, and all module execution
outside module workers remain explicit unsupported cases. They are rejected
rather than falling through to classic evaluation.

### Neutral structured-clone packet

`JsStructuredClone` is a thread-neutral graph packet: it contains node indexes
and standard C++ data, never a sender agent's `JsValue`. Its writer supports
`undefined`, `null`, booleans, numbers (including `NaN`, infinities and `-0`),
strings, ordinary objects, and arrays. It reads enumerable own string
properties at send time, preserves repeated references and cycles, and
materializes fresh ordinary object/array storage in the receiver's realm. A
one-million-node bound prevents an accidentally unbounded native packet.

Worker inbound/outbound queues, `Window.postMessage`, and same-agent
`MessagePort` queues now hold shared clone packets. Receiver-side event tasks
decode only after entering the receiving agent/realm. This avoids a
cross-thread callback retaining source-agent heap handles and gives Window and
MessagePort the same copy semantics as workers. The port state only needs to
trace its stable endpoint now; queued packets have no GC handles.

Functions, symbols, DOM values, host/exotic values, and non-plain built-ins
such as `Date`, `RegExp`, `Map`, `Set`, `Error`, `Promise`, generators, and
iterators fail synchronously with `DataCloneError`. Empty transfer lists are
accepted as no-ops; non-empty transfer lists fail with `DataCloneError` rather
than pretending an ownership transfer occurred. Transferring ports and
cross-agent ports are still not implemented. The existing global
`structuredClone()` builtin is intentionally separate from this message packet
implementation.

Finally, the symbol registry and DOM interface registry are now process-lifetime
allocations. That removes their static-destruction ordering race with worker
shutdown, instead of relying only on the Phase 40 warm-up/`atexit` ordering.

## 3. How it was verified

On the Phase 66 tree:

```bash
make -j2 test
# All 212 engine test suites passed successfully!
# Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

make -j2 security-worker
# 17 checks; 0 failure(s).
```

`structured_clone_message_delivery_and_refusal` covers page-to-worker and
worker-to-page graph copying, aliases/cycles, `undefined`, special numbers,
empty/non-empty transfer lists, sender mutation after posting, and
`DataCloneError` in both directions. It also covers MessageChannel and local
Window delivery. `tests/cross_origin_windows.cpp` adds four checks for a
cross-origin Window graph round-trip and refusal of function/non-empty-transfer
messages.

`module_worker_static_graph_loading_and_refusal` builds a same-origin module
root with named/default/namespace imports plus a named re-export, checks strict
module top-level and the absent `importScripts`, and verifies that `export *`
fails before the worker body can post. The standalone worker-policy check runs
the same static graph through an HTTP server and retains MIME, error listener,
classic `importScripts`, and 60-process startup/exit checks. A focused
ASan+UBSan build also ran `build-asan/test_suite` (212 suites),
`build-asan/cross_origin_test` (114 checks), and the policy runner with 20
startup/exit iterations without a sanitizer diagnostic.

## 4. What is still open

This is not a general ECMAScript module implementation: no page `<script
type="module">`, ordinary `import`/`export` parser mode, live bindings,
cycles, `export *`, dynamic imports, `import.meta`, top-level `await`, module
namespace exotica, or complete module-resolution/import-map behavior exists.

Structured clone does not yet include `ArrayBuffer`, typed arrays, `DataView`,
`Blob`, `File`, `ImageBitmap`, `MessagePort`, `SharedArrayBuffer`, transfer
ownership/detachment, broad built-in cloning, cross-agent port transport, or
meaningful `messageerror` delivery. The separately exposed global
`structuredClone()` still has its own implementation and compatibility scope.
