<!-- Moved out of AUDIT.md when new write-ups began going to docs/
     (docs/README.md); AUDIT.md keeps the open findings. -->

# Finding 67 — Allowed child navigation was fragment-only, and message ports did not exist

Phase 61. The architecture is in
[`docs/phases/61-child-location-navigation-and-message-channel.md`](../phases/61-child-location-navigation-and-message-channel.md).

## 1. What was wrong

Phase 56's cross-origin table permits writes to a child frame's `location` and
`Location.href`, plus `Location.replace()`. All three reached `navigate()`,
which returned for every non-fragment target, so an allowed cross-document
operation silently did nothing. Separately, a page could use
`window.postMessage` but could not construct a private entangled channel:
`MessageChannel` and `MessagePort` did not exist.

## 2. What changed

A non-fragment Location target now verifies that its realm is the live child
browsing context of an iframe. Only that relationship delegates the resolved URL
to the iframe navigation scheduler. The scheduler gives a direct request
priority over markup `src`/`srcdoc`, keeps the iframe attributes unchanged, and
retains the existing loader, sandbox, and document replacement behavior,
including the stable WindowProxy.

`MessageChannel` creates entangled EventTarget ports. A port owns a FIFO of
pending values; `postMessage()` queues onto its peer and delivery runs later
through the normal `MessageEvent`/EventTarget path once `start()` or
`onmessage` starts it. `close()` drops queued messages and disconnects the
peer. Collector tracing covers port objects and queued JS values, while delivery
tasks root their targets.

## 3. How it was verified

```bash
make test
# All 204 engine test suites passed successfully!
# Cross-origin Window/Location suite: 110 checks, 0 failure(s).
```

The 110 checks exercise all three Location entry points, `srcdoc` navigation,
WindowProxy identity, unchanged iframe markup, cross-origin read refusal,
asynchronous FIFO delivery, queued-value GC safety, ordinary EventTarget
delivery, delayed start, and closing before and after delivery. The focused
ASan+UBSan run passed without diagnostics.

## 4. What is still open

The implementation is intentionally same-agent only: values retain identity,
the optional transfer argument is ignored, and there is no structured clone,
transferred/cross-agent port transport, or meaningful `messageerror` behavior.
Out-of-process site isolation also remains open.
