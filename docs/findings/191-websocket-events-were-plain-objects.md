# Finding 191: WebSocket messages and closes were plain objects

**Status:** Resolved in Phase 187 (`docs/phases/187-websocket-message-and-close-events.md`).

## 1. What was wrong

The DOM event registry already knew `MessageEvent` and `CloseEvent`, and their
constructors worked when called directly. WebSocket delivery did not use them:

- The message callback got a newly allocated object with `type`, `data`,
  `origin`, `lastEventId`, `source`, `target`, and `currentTarget` properties.
- The close callback got a newly allocated object with `type`, `code`,
  `reason`, `wasClean`, `target`, and `currentTarget` properties.

Those objects looked event-like when a handler read a field, but they were not
instances of the interfaces. In particular, `event instanceof MessageEvent`
and `event instanceof CloseEvent` were false, and the `Event` prototype's
accessors and methods were missing.

## 2. What changed

WebSocket now creates the message and close values through the existing DOM
event factory, with the interface-specific fields in the event's internal
state. A small host-target dispatch path initializes the internal `EventState`
while WebSocket invokes its own handlers: the event is trusted, has the socket
as target/currentTarget, reports `AT_TARGET`, and exposes the socket in
`composedPath()`. Finishing delivery clears `currentTarget`, `eventPhase`, and
the temporary path but leaves `target` set.

This keeps WebSocket's listener storage and callback order intact; it does not
turn the socket into a DOM `EventTarget` or implement its `WebSocket.prototype`.
The finding is limited to WebSocket `message` and `close`; its `open`/`error`
paths and Worker `message` delivery remain unchanged.

## 3. How it was verified

- `make security-websocket`: **40/40** checks, including live TLS WebSocket
  assertions for both event classes, target/currentTarget, trust, phase, and
  composed path.
- `make test`: **335/335** engine suites and **114/114** cross-origin checks.
- `make dom-conformance`: **678/678** checks.
