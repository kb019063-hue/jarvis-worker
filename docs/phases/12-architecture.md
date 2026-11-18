# Phase 12 Architecture

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 12 Architecture` section.
> The text below is the section as it stood, unchanged.


### Pillar 1: Web Workers & Concurrent JS Execution
- **Dedicated Execution Threads (`include/litebird/js/Worker.hpp`, `src/js/Worker.cpp`):**
  - `WorkerThread`: Spawns a dedicated OS thread hosting its own isolated `JsInterpreter` environment, runtime heaps, and microtask queues, preventing worker operations from blocking the main UI thread.
  - Script loading supports `data:text/javascript,...` and goes through `ResourceLoader::load_for_page` with the creator's context (Phase 33): a web page cannot load a local worker script, a `file:` document can, and a failed load is an error event rather than the path executed as source.
  - Thread-local isolation ensures `JsValue` function callers (`s_function_caller`) remain thread-local and re-entrant across worker contexts without cross-thread clobbering.
- **Inter-Process/Thread Communication (IPC):**
  - Thread-safe queues (`std::mutex`, `std::condition_variable`) transmit deep-cloned JSON messages between the main thread and worker threads.
  - `postMessage(data)` / `onmessage = function(e)` semantics replicate the standard browser Web Worker API.
  - Worker lifecycle management: `worker.terminate()` halts the worker thread event loop cleanly and joins worker threads.
  - Global scope exposure: Worker scripts access `self`, `self.postMessage`, and `self.onmessage`.

### Pillar 2: CSS Modern Grid & Flexbox Refinements
- **CSS Grid Modern Syntax (`include/litebird/css/StyleResolver.hpp`, `src/css/StyleResolver.cpp`):**
  - Parsing and track resolution for `repeat(count, track_spec)` and `minmax(min_val, max_val)`:
    - Fixed track count repetition and `repeat(auto-fit, minmax(...))` calculations based on container dimensions.
    - Track definition expansion supporting `px`, `%`, `fr` fractions, and `auto`.
- **Flexbox and Grid `gap` Property Support (`src/layout/LayoutBox.cpp`):**
  - `row-gap` and `column-gap` (or shorthand `gap`) integrated across flexbox and grid layouts.
  - Flex layout calculates inter-item distribution accounting for gaps without altering individual item base sizes or margins.
  - Grid layout spaces tracks and grid cells with explicit row and column gaps during cell positioning.

### Pillar 3: RFC 6455 WebSocket Client
- **WebSocket Protocol Architecture (`include/litebird/net/WebSocket.hpp`, `src/net/WebSocket.cpp`):**
  - RFC 6455 framing and state machine (`CONNECTING`, `OPEN`, `CLOSING`, `CLOSED`).
  - Cryptographic handshake implementation using zero-dependency standard C++20 SHA-1 (RFC 3174) and standard Base64 encoding for `Sec-WebSocket-Key` and `Sec-WebSocket-Accept` validation.
  - Client-to-server 4-byte XOR frame masking and server frame unmasking.
  - Opcode handling: Continuous (`0x0`), Text (`0x1`), Binary (`0x2`), Close (`0x8`), Ping (`0x9`), Pong (`0xA`).
  - Background network reader thread handles non-blocking socket stream reception and frame defragmentation.
- **JavaScript WebSocket Bindings:**
  - Standard JavaScript interface: `new WebSocket(url, [protocols])`.
  - Event dispatch: `onopen`, `onmessage`, `onerror`, `onclose` triggered asynchronously via the main thread `EventLoop`.
  - Transmission methods: `ws.send(data)` frames payload with client masking; `ws.close(code, reason)` negotiates graceful RFC 6455 connection teardown.
  - Built-in POSIX TCP socket loopback testing server for zero-dependency verified end-to-end WebSocket roundtrips.

---
