# Finding 499 — the context was ready before the global

The open note said OffscreenCanvas installed a subset of the 2D context and that
`Path2D` had not reached it. Reading the construction path showed the opposite:

```cpp
JsValue object = st.context->to_js_value(st.context);
```

That is the full context object. Its path-taking overloads were already present.
The missing work was one layer above it: workers did not expose either constructor
needed to reach the code.

This is the finding: **a missing feature at a call site can be an unreachable
implementation, not an incomplete implementation.** Before adding methods to the
object a page eventually receives, prove the page can construct the receiver and
the argument in that realm.

## Why a Window probe was misleading

A page Window exposed both constructors lazily through the DOM interface registry,
and the path methods worked on an OffscreenCanvas there. The same
`JsInterpreter` class builds a worker with no document, but its constructor list
was explicit: URL, URLSearchParams, Blob, and File. `OffscreenCanvas` and `Path2D`
were absent from that list.

So this Window probe was green and irrelevant:

```js
new OffscreenCanvas(20, 20).getContext('2d').fill(new Path2D());
```

The meaningful probe had to execute in the worker's own interpreter.

## A global had two faces

The worker implementation keeps bare global bindings in an environment and builds
a separate object for `self`. Existing host features such as `fetch`, `Request`,
`Worker`, and `WebSocket` are copied to both.

Installing only bare `Path2D` and `OffscreenCanvas` made ordinary source work and
left this false:

```js
self.Path2D === Path2D
```

The mutation that omitted those two `self` properties still drew every expected
pixel. Only the surface assertion caught it. When an engine models the global and
`self` separately, every exposed host constructor needs both entries until that
architectural split is removed.

## A realm is not necessarily a Window

The first pixel read found a second bad assumption. ImageData creation treated
“realm global” and “Window” as synonyms and searched `realm.window` for
`Uint8ClampedArray`. A worker DOM realm has no Window, even though its interpreter
has all typed-array constructors.

The tempting repair was to put the worker global into `realm.window`. That would
make unrelated DOM code believe a Window exists. The narrow repair records the
worker environment as `global_scope` and lets ImageData's built-in lookups fall
back to it.

The distinction is reusable: **an interface exposed in both Window and Worker
needs a realm-global dependency, not a Window dependency.** `convertToBlob()` has
the same shape with `Promise` and remains open.

## The oracle had to return pixels

A suite that asserted only `typeof Path2D === 'function'` would pass two broken
versions:

1. `OffscreenCanvas` still absent;
2. constructors present, drawing calls accepted, `getImageData()` unable to make
   its typed array.

The final oracle draws four ways and returns RGBA strings from both classic and
module workers. Removing either constructor or the ImageData bridge fails inside
the worker; removing only `self` mirroring fails the exact reply. The result is a
realm test, an API test, and a pixel test together.

## An agent test must not become the next test's input

The first complete oracle passed and made a later automatic-GC suite fail. Each
worker correctly owned its heap cells as a separate agent, but the unit process
runs hundreds of suites in agent 0; adding two more agents changed the shared
heap's live-cell history enough to perturb a later threshold measurement.
Terminating workers, clearing their page references and loader roots, draining
tasks, and collecting agent 0 could not collect cells owned by another agent.

The oracle now gives its owner page a fresh heap agent too, collects that agent,
and restores the previous one. A no-op mutation of only this suite made the later
GC test pass; the agent boundary makes the real classic/module pixel work and the
GC test pass together. A realm test is not finished merely when its own assertions
are green: it must leave the process as though that realm had gone away.
