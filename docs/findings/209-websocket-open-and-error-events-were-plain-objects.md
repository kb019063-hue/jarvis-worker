# Finding 209: WebSocket open and error events were plain objects

## 1. What was wrong

The WebSocket success (`open`) and failure (`error`) callbacks received
ordinary objects, not the generic DOM `Event` specified for those event types.
A few event-like properties were assigned directly, but the values had no
Event prototype or internal event state. Scripts could not use
`instanceof Event`, `isTrusted`, `eventPhase`, `composedPath()`, or event
propagation methods; a call to `stopImmediatePropagation()` therefore did not
stop the remaining WebSocket listeners. The error path also exposed a
`message` property used by existing diagnostics, which needed to remain
available.

## 2. What changed

WebSocket `open` and `error` now use the existing DOM event factory to create
trusted generic `Event` instances and the host-target begin/end dispatch
helpers to expose and clear the target's dispatch state. The listener loops
stop when `stopImmediatePropagation()` is called. The existing `error.message`
expando, handler-before-listener ordering, and error-before-close sequencing
are preserved. The WebSocket owner realm is selected for `open` construction.

## 3. Verification

`make security-websocket` reports **44 checks, 0 failures**. The verifier
asserts the `Event` interface and trusted target state for both event types,
checks that phase/currentTarget/path are cleared after dispatch, and confirms
that `stopImmediatePropagation()` suppresses the second listener. It also
checks that the error diagnostic remains present. `make -j2 build/litebird`
and `python3 -m py_compile tools/security/verify_websocket.py` passed.

## 4. What is still open

HTTP proxy tunneling (`CONNECT`) is not implemented, and `WebSocket.prototype`
remains a separate host-constructor gap. Worker event delivery is out of scope.
