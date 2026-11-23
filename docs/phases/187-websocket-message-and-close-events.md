# Phase 187: WebSocket messages and closes are real event instances

## 1. What was wrong

`MessageEvent` and `CloseEvent` were already registered DOM interfaces with
working generic constructors. Direct construction on a Window produced objects
with their interface prototypes and IDL fields. The missing behavior was in
WebSocket delivery: `WebSocketObject::dispatch_message_on_owner()` and the
close path assembled ordinary objects by assigning `type`, `data`, `code`,
`reason`, `wasClean`, `target`, and other properties.

Consequently, a WebSocket message callback did not receive a `MessageEvent`, and
a close callback did not receive a `CloseEvent`. `instanceof`, the inherited
`Event` accessors and methods, `isTrusted`, `eventPhase`, and `composedPath()`
were absent. The ordinary `currentTarget` property also stayed set after the
callback instead of being cleared when delivery ended.

## 2. What changed

- WebSocket message delivery now builds a `MessageEvent` through the existing
  DOM event factory, supplying the received `data` and WebSocket `origin`. The
  interface defaults continue to provide an empty `lastEventId`, `source ===
  null`, and an empty `ports` array. Text, `ArrayBuffer`, and `Blob` payload
  selection is unchanged.
- Close delivery now builds a `CloseEvent` with the received close `code`,
  `reason`, and `wasClean` values.
- The DOM event bindings expose a small begin/end path for a host target that
  owns its listener list instead of using the DOM `EventTarget` listener store.
  During WebSocket delivery it sets the event's target and current target to the
  socket, sets `eventPhase` to `AT_TARGET`, marks the user-agent event trusted,
  and exposes the one-target composed path. On completion, it clears
  `currentTarget`, the phase, the transient path and propagation flags while
  retaining `target`.
- WebSocket's existing handler and listener storage remains in place. This
  change does not implement `WebSocket.prototype`, HTTP proxy tunneling, or
  typed WebSocket `open`/`error` delivery. Worker message delivery also
  remains unchanged; it is a separate host-event gap.

## 3. Verification

- `make test`: **335/335** engine suites; **114/114** cross-origin checks.
- `make dom-conformance`: **678/678** checks.
- `make security-websocket`: **40/40** checks, including new live `wss://`
  assertions that message and close handlers receive trusted, target-bound
  `MessageEvent` and `CloseEvent` instances; the target also runs the existing
  concurrent startup/exit stress checks.
- `python3 -m py_compile tools/security/verify_websocket.py` and
  `git diff --check`: clean.
- `python3 tools/ci/check_docs.py` and
  `python3 tools/ci/check_numbers.py`: clean.

## 4. What is still open

HTTP proxy tunneling (`CONNECT`) and `WebSocket.prototype` remain separate
items. The open/error callback objects are unchanged by this phase.
