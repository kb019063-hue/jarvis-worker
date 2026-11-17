# Phase 14 Architecture: Modern Browser Capabilities & Advanced Engine Platform

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 14 Architecture: Modern Browser Capabilities & Advanced Engine Platform` section.
> The text below is the section as it stood, unchanged.


### Pillar 1: Observers & Client-Side SPA Routing

- **IntersectionObserver API (`include/litebird/dom/IntersectionObserver.hpp`, `src/dom/IntersectionObserver.cpp`):**
  - `IntersectionObserverInit`: Holds root element (or viewport root if null), parsed root margins (`rootMargin`), and threshold thresholds list (defaults to `[0.0]`).
  - `IntersectionObserverEntry`: Encapsulates intersection geometry:
    - `time`: High-resolution timestamp of intersection record.
    - `rootBounds`: Bounding rect of intersection root.
    - `boundingClientRect`: Target element bounding box.
    - `intersectionRect`: Clipped intersection rectangle.
    - `isIntersecting`: Boolean flag indicating non-zero intersection area.
    - `intersectionRatio`: Exact floating-point ratio ($0.0 \le \text{ratio} \le 1.0$).
    - `target`: Target DOM element.
  - Geometry & Threshold Crossing Detection: `check_intersections(root_bounds)` calculates intersection clipped rects, detects threshold boundaries crossed since prior checks, and queues records.
  - Microtask Batching: Pending entries are queued and dispatched via `EventLoop::post_microtask` ensuring asynchronous, low-overhead microtask execution.

- **ResizeObserver API (`include/litebird/dom/ResizeObserver.hpp`, `src/dom/ResizeObserver.cpp`):**
  - `ResizeObserver`: Manages target DOM element tracking and registration.
  - `ResizeObserverEntry`: Reports `contentRect`, `borderBoxSize`, and `contentBoxSize` dimensions.
  - Delta Detection: Compares current element dimensions against cached dimensions, queuing records on size alterations and dispatching via microtasks.

- **History API & Client-Side SPA Routing (`include/litebird/dom/History.hpp`, `src/dom/History.cpp`):**
  - `HistoryItem`: Stores state object (`JsValue`), title, and URL for each session stack entry.
  - Session Stack Management:
    - `pushState(state, title, url)`: Truncates forward history past current index, appends new item, updates document URL, and fires navigation listeners.
    - `replaceState(state, title, url)`: Replaces current history item without mutating stack depth.
    - `back()`, `forward()`, `go(delta)`: Navigates session history, clamps bounds, and dispatches standard W3C `popstate` events with cloned state objects to the `window` target.

### Pillar 2: CSS Visual Filters & Canvas Blending

- **CSS Visual Image Filters (`include/litebird/render/ImageFilter.hpp`, `src/render/ImageFilter.cpp`):**
  - Software image filter pipeline operating directly on 32-bit ARGB pixel buffers:
    - `blur(radius)`: Optimized two-pass separable horizontal and vertical box blur approximating Gaussian distribution.
    - `brightness(amount)`: Scales RGB color channels by factor.
    - `contrast(amount)`: Centers contrast expansion around midpoint 128.
    - `grayscale(amount)`: Rec. 601 luma weighted transformation ($0.299 R + 0.587 G + 0.114 B$).
    - `sepia(amount)`: Matrix transformation simulating antique photographic tones.
    - `invert(amount)`: Inverts RGB channels ($255 - C$).
    - `opacity(amount)`: Multiplies existing alpha values.
    - `drop-shadow(dx, dy, blur, color)`: Allocates expanded shadow buffer, applies separable box blur, and blends original content over offset shadow.
  - Parser & Style Resolver Integration: Parses CSS filter functions from `filter` and `backdrop-filter` rules, binding to `ComputedStyle::filters` and applying before blitting to render targets.

- **Canvas 2D Blend Modes (`include/litebird/render/Canvas2D.hpp`, `src/render/Canvas2D.cpp`):**
  - `globalCompositeOperation` Property: Dynamic JavaScript property binding on `CanvasRenderingContext2D` supporting:
    - `"source-over"`: Standard alpha compositing ($S + D \cdot (1 - S_a)$).
    - `"copy"`: Direct pixel overwrite ignoring destination alpha.
    - `"multiply"`: Per-channel multiplicative blending ($C_r = S_c \cdot D_c / 255$).
    - `"screen"`: Additive inverted multiplication ($C_r = 255 - ((255 - S_c)(255 - D_c) / 255)$).
    - `"lighter"`: Clamped channel addition ($C_r = \min(255, S_c + D_c)$).
    - `"destination-over"`: Under-blending existing pixel content beneath destination.

### Pillar 3: JS Generators, Iterators & AbortController

- **JavaScript Coroutine Generators (`include/litebird/js/Generator.hpp`, `src/js/Generator.cpp`, `src/js/JsParser.cpp`):**
  - Parser Support: `function*` declarations, function expressions, `yield` expressions, and delegated `yield*`.
  - Coroutine Engine (`GeneratorData`, `GeneratorContext`):
    - Implemented with lightweight cooperatively suspended execution contexts.
    - Running generator functions on dedicated worker threads synchronized with mutexes and condition variables.
    - Stepping with `.next(value)` passes values into yield points, resumes execution until the next yield or return, and produces `{ value, done }` objects.
    - Thread-local active context pointers enable nested generator yield suspension across function call stacks.
  - Iteration Protocols & `for...of`:
    - Array, string, and custom iterable support via `Symbol.iterator`.
    - AST and runtime support for `for (const x of iterable) { ... }` executing iteration protocol step loops natively.

- **AbortController & Network Cancellation (`include/litebird/net/AbortController.hpp`, `src/net/AbortController.cpp`, `src/net/Fetch.cpp`):**
  - Standard DOM API: `controller = new AbortController()`, `controller.signal`.
  - `AbortSignal`:
    - `aborted`: Dynamic property returning abort status.
    - `reason`: Stores custom abort reason or defaults to `DOMException("AbortError")`.
    - `addEventListener("abort", listener)`: Event target dispatching on abort.
    - `throwIfAborted()`: Throws active abort reason if already aborted.
    - `AbortSignal.timeout(ms)`: Factory creating an automatically aborting signal with `TimeoutError` via the `EventLoop`.
  - Fetch Cancellation Integration: `fetch(url, { signal })` registers abort listeners; triggering abort immediately terminates sockets, stops background transfers, and rejects promises with `AbortError`.

### Pillar 4: CSS Grid Named Areas & Layout Containment

- **CSS Grid Named Areas (`include/litebird/css/StyleResolver.hpp`, `src/layout/LayoutBox.cpp`):**
  - `grid-template-areas`: ASCII grid template syntax parsing (e.g., `\"header header\" \"sidebar content\" \"footer footer\"`).
  - Validation: Enforces rectangular area constraints; non-rectangular definitions are safely rejected.
  - `grid-area`: Assigns grid items to named areas (e.g. `grid-area: header`) or explicit numerical line indexes (`row-start / col-start / row-end / col-end`).
  - Layout Grid Placement: Area coordinates map items to 1-based start and end row/column tracks, seamlessly handling multi-row and multi-column spans.

- **CSS Layout Containment (`contain`):**
  - Full parser support for `contain: none | strict | content | layout | paint | size`.
  - Layout Isolation: Elements with `contain: layout` or `contain: strict` act as independent layout boundaries. Internal mutations mark dirty bits only up to the containment root, preventing unnecessary layout passes across parent trees.
  - Paint Isolation: Precludes child elements from rendering outside containment clipping bounds.
