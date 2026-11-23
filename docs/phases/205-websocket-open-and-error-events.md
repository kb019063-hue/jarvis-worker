# Phase 205: WebSocket open and error events are real Event instances

## 1. What was wrong

`WebSocketObject::dispatch_open_on_owner()` and the error half of
`dispatch_error_and_close_on_owner()` delivered plain JavaScript objects with
`type`, `target` and `currentTarget` properties. They were not instances of the
DOM `Event` interface: `instanceof Event` failed, the inherited event methods
were absent, and callbacks could not observe trusted target-dispatch state.
In particular, `stopImmediatePropagation()` could not stop later WebSocket
listeners.

## 2. What changed

- `open` and `error` delivery now creates generic `Event` instances through the
  DOM event factory. Both are non-bubbling and non-cancelable, as WebSocket
  events are. They are trusted and expose the socket as `target` and
  `currentTarget`, `AT_TARGET` as `eventPhase`, and a one-target `composedPath()`
  while callbacks run.
- The temporary dispatch state is ended after the handler and listener list
  finish: `currentTarget` becomes `null`, `eventPhase` becomes `NONE`, and
  `composedPath()` is empty, while `target` remains the socket.
- The WebSocket listener loops honor `stopImmediatePropagation()` without
  changing handler-before-listener order or the existing error-before-close
  sequence. The error event retains the pre-existing `message` expando for
  callers that use it as a diagnostic.
- The open event is created in the socket owner's realm. Message and close
  delivery, Worker events, HTTP proxy tunneling, and `WebSocket.prototype`
  are unchanged.

## 3. Verification

- `make -j2 build/litebird`: build completed.
- `python3 -m py_compile tools/security/verify_websocket.py`: passed.
- `make security-websocket`: **44 checks, 0 failures**, including trusted
  `Event` instances for `open` and `error`, active and cleared dispatch state,
  the preserved error diagnostic, and `stopImmediatePropagation()` on both
  listener lists.

## 4. What is still open

HTTP proxy tunneling (`CONNECT`) and `WebSocket.prototype` remain separate
items. Worker message/error delivery is not changed by this phase.
