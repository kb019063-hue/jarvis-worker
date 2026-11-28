# Phase 528 — Fetch and XMLHttpRequest honor HTTP_PROXY

## What was open

Phase 492 added HTTP proxy routing to `ResourceLoader`, including HTTP absolute-form requests and `NO_PROXY`, and an open task remained for Fetch/XHR. The proxy suite exercised `ResourceLoader` directly; it did not prove that the two script-facing APIs actually reached the shared routing path.

## What changed

Added `tests/suites/528-fetch-and-xhr-use-http-proxy.cpp`. The suite starts a local HTTP proxy, clears inherited proxy variables, sets only `HTTP_PROXY`, and sends a POST to a reserved `.invalid` destination through both `fetch()` and `XMLHttpRequest`. The proxy returns a CORS-allowed response and records each request. The assertions require both absolute-form destination URLs and both request bodies to arrive at the proxy. A direct connection cannot resolve the destination, and an origin-form target would fail the exact-target assertion.

The test passes without another production-code change: both API paths already call `ResourceLoader::load_for_page()`, which applies the proxy routing added in Phase 492. The previously open item was a missing integration test, not a missing proxy implementation.

## Verification

- `make test`: all 560 engine suites pass, followed by 114/114 cross-origin Window/Location and MessageChannel checks.
- `python3 tools/ci/check_suites.py`, `python3 tools/ci/check_docs.py`, `make check-numbers`, `make -j2 all`, and `git diff --check` pass.

## Still open

This suite covers HTTP proxy routing for Fetch/XHR POSTs with ordinary text bodies. `NO_PROXY`, proxy authentication and HTTPS CONNECT are implemented and covered by the shared proxy tests from Phase 492; additional Fetch/XHR-specific combinations are not claimed here. Streaming Fetch request bodies and HTML form-navigation multipart bodies remain separate tasks.
