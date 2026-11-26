# Phase 463 — codecs in module and nested worker realms

Task row: **`TextDecoder` and `TextEncoder` reach script, and the decoder
reads in pieces since Phase 163** (`AGENTS.md`). Phase 206 covered a classic
worker. The module-worker path and a worker created from inside another worker
had no codec assertion. Finding 463.

## What was missing

The codec constructors are installed during each `JsInterpreter`'s setup, so
workers should receive their own `TextDecoder` and `TextEncoder` globals. The
existing window and classic-worker checks did not exercise a module worker or
a nested worker. A later change could have accidentally made the codec install
window-only, or left one worker realm without it, without those tests noticing.

## What changed

`tests/suites/463-codecs-in-module-and-nested-workers.cpp` adds one suite. The
window is a control. It starts a `type: "module"` worker, which uses its own
`TextEncoder` and `TextDecoder` to round-trip `module €`; that module worker
then creates a classic child worker that round-trips `nested €`. Both results
are sent through `postMessage` and checked in the window. An error or a
missing reply fails the suite, and the child and parent workers are stopped.

No engine source changed: both worker realms already build their own
interpreter and receive the codec installation as part of that setup. This
phase closes the specific module/nested-worker coverage gap, not the separate
Streams or document-encoding work noted in the task row.

## Verification

- `python3 tools/ci/check_suites.py` accepts the new one-`TEST` suite file.
- `make -j2 test`: **509/509 engine suites** and **114/114 cross-origin
  checks** pass, including the new worker-realm round trip.
- `git diff --check` is clean.

## Still open

`TextDecoderStream` and `TextEncoderStream` require the Streams interfaces,
which this engine does not yet provide. The document's `<meta charset>`
prescan remains limited to its first 1,024 bytes. Neither item is changed by
this worker-realm coverage phase.
