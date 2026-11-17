# Phase 61 — Child `Location` navigation and `MessageChannel`

Finding 67. The companion finding is
[`docs/findings/67-child-location-navigation-and-message-ports.md`](../findings/67-child-location-navigation-and-message-ports.md).

## 1. What was wrong

The cross-origin `Window` table deliberately permits assigning a child frame's
`location` and `location.href`, and calling `location.replace()`. The engine's
`navigate()` implementation, however, only handled fragment targets; a
permitted cross-document URL therefore silently did nothing.

Pages could use `window.postMessage`, but had no way to create an entangled,
private channel: neither `MessageChannel` nor `MessagePort` was exposed to
script.

## 2. What changed

For a non-fragment target, `navigate()` now verifies that the target realm is
the live child browsing context of an iframe in its parent realm. Only that
relationship can call `request_child_frame_navigation()`, which feeds the
already-resolved URL into the iframe's existing
`schedule_iframe_navigation()` path. The direct request wins over markup
`src`/`srcdoc` without changing either attribute; sandbox parsing, resource and
MIME loading, generation cancellation, load delay, and parent load-event
ordering stay in that existing pipeline. `navigate_document()` replaces the
child document while retaining its `WindowProxy` identity. No cross-origin read
or reflection capability was added.

`MessageChannel` creates two entangled `MessagePort` EventTargets backed by
`MessagePortState`. Each state records its owner realm id, peer, stable JS port
object, FIFO pending values, and start/close/task state. `postMessage(value)`
enqueues on the peer; assigning `onmessage` or calling `start()` schedules
asynchronous delivery through the ordinary `MessageEvent` and EventTarget
dispatcher. `close()` clears its pending values and breaks the entanglement.
The state traces stable ports and queued JS values, and a delivery task roots
its target so pending messages remain visible to the collector.

## 3. How it was verified

On the Phase 61 merged tree:

```bash
make test
# All 204 engine test suites passed successfully!
# Cross-origin Window/Location suite: 110 checks, 0 failure(s).
```

`tests/cross_origin_windows.cpp` covers all three permitted Location entry
points, direct navigation over `srcdoc`, retained `WindowProxy` identity,
unchanged iframe markup, and continued cross-origin read refusal. Its channel
cases cover asynchronous FIFO delivery, queued-value GC visibility, ordinary
EventTarget target/receiver behavior, delayed `start()`, and closing before or
after a queued delivery. A focused ASan+UBSan run of the same 110 checks was
also clean.

## 4. What is still open

This is deliberately same-agent, identity-preserving messaging. It does not
implement structured clone, transfer lists, transferred or cross-agent ports,
or meaningful `messageerror` delivery. Top-level cross-document navigation and
cross-document session-history replacement are still outside this child-iframe
path. `Location.assign()`, `reload()`, and `ancestorOrigins` remain unexposed
cross-origin; out-of-process site isolation remains future work.
