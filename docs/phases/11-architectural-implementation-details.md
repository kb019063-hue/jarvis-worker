# 9. Phase 11 Architectural Implementation Details

> Moved out of `ARCHITECTURE.md` — written there as a `## 9. Phase 11 Architectural Implementation Details` section.
> The text below is the section as it stood, unchanged.


### Pillar 1: CSS Variables & Custom Property Cascade (`var()`)
- **Data Representation (`include/litebird/css/StyleResolver.hpp`):**
  - `ComputedStyle` maintains `custom_properties`: a map of string property names (`--*`) to their string values.
  - Inheritance: Custom properties inherit down the DOM tree by default. In `StyleResolver::resolve()`, child element styles copy their parent's `custom_properties` before matching rules are applied.
- **Substitution Engine (`src/css/StyleResolver.cpp`):**
  - `StyleResolver::resolve_var_functions(val_str, computed_style)` scans declarations for `var(--name, fallback)`.
  - Nested comma handling balances parentheses to extract optional fallback expressions.
  - Recursively resolves variable chains and fallback expressions; unrecognized variables without fallbacks yield empty values.

### Pillar 2: CSS `calc()` Math Expression Engine
- **Recursive Descent Parser (`src/css/StyleResolver.cpp`):**
  - `CalcLexer`: Lexes tokens for numbers, pixel lengths (`px`), percentages (`%`), operators (`+`, `-`, `*`, `/`), and parentheses `(` / `)`.
  - Operator Precedence Grammar:
    - Addition/Subtraction: `parse_additive()`
    - Multiplication/Division: `parse_multiplicative()`
    - Primaries & Subexpressions: `parse_primary()` handles parenthesized sub-expressions, lengths, percentages, and pure scalar numbers.
- **Unit and Reference Resolution:**
  - `evaluate_calc_expression(expr, base_ref, style)` resolves lengths to absolute pixels using reference bases (e.g., container width/height or font size for percentages).
  - Integrates seamlessly with `resolve_var_functions()` so expressions like `calc(var(--card-size) * 2 + 10px)` resolve variable tokens prior to arithmetic reduction.

### Pillar 3: Modern Fetch API (`fetch`, `Response`, `Headers`)
- **Data Structures (`include/litebird/net/Fetch.hpp`, `src/net/Fetch.cpp`):**
  - `HeadersObject`: Implements standard Fetch headers with case-insensitive ASCII name lookups, `get(name)`, `set(name, value)`, `append(name, value)`, `has(name)`, and `delete(name)`.
  - `ResponseObject`: Wraps HTTP / data response metadata:
    - `status` (integer code, e.g. 200), `statusText` ("OK"), `ok` (boolean, status in range 200-299), `url` (string), and `headers` (`HeadersObject`).
    - Body readers: `text()` returning a Promise resolving to raw body string, and `json()` returning a Promise resolving to parsed JavaScript object/array via `Json::parse()`.
- **Async Execution & Microtask Integration:**
  - `FetchAPI::fetch(input, init, context)` creates an unfulfilled Promise, decodes `RequestInit` options (`method`, `headers`, `body`), and dispatches through `ResourceLoader::load_for_page` with the document context captured at call time (Phase 33).
  - Seamlessly integrates with the microtask scheduler and JavaScript `async` / `await` workflows.

### Pillar 4: Standard `URL` & `URLSearchParams` APIs
- **Data Structures (`include/litebird/net/URL.hpp`, `src/net/URL.cpp`):**
  - `URLSearchParamsObject`: Stores ordered pairs of key-value query parameters. Implements `get()`, `getAll()`, `set()`, `append()`, `has()`, `delete()`, and `toString()` with RFC 3986 percent-encoding/decoding (`+` for spaces, `%XX` sequences).
  - `URLObject`: Full WHATWG URL decomposition parsing `href`, `origin`, `protocol`, `host`, `hostname`, `port`, `pathname`, `search`, and `hash`.
- **Live Bidirectional Synchronization:**
  - Modifying `url.searchParams` automatically notifies `URLObject` through an `on_change` callback, regenerating `url.search` and `url.href`.
  - Assigning a new search query or updating search parameters recomputes `url.href` consistently across both the JavaScript interface and native model.

---
