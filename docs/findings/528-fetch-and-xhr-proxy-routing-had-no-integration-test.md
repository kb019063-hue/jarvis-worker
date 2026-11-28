# Finding 528 — Fetch and XHR proxy routing had no integration test

**Severity:** interoperability and regression coverage. The open task list still
said Fetch/XHR lacked HTTP proxy support after Phase 492 added proxy routing to
the shared `ResourceLoader`, but the existing proxy tests called that loader
directly.

## What was measured

With inherited proxy variables cleared and `HTTP_PROXY` pointed at a local
loopback proxy, a page on `http://page.invalid/` sent POSTs to reserved
`http://target.invalid/` URLs through both `fetch()` and `XMLHttpRequest`. The
proxy received the absolute-form URLs and exact request bodies and returned a
CORS-allowed response. Direct access cannot resolve the reserved destination.

The permanent regression is
`tests/suites/528-fetch-and-xhr-use-http-proxy.cpp`. It checks both API paths
and verifies the proxy observed `POST`, the complete absolute-form target, and
the unmodified body for each. The suite passes on the current implementation:
Fetch and XHR already share `ResourceLoader::load_for_page()`; no production
code change was needed.

## Resolution and remaining scope

Phase 528 closes the Fetch/XHR HTTP proxy support gap as a test-coverage gap.
It does not add a new proxy implementation or expand the Phase 492 guarantees.
Streaming Fetch bodies and HTML form-navigation submissions remain separate.
