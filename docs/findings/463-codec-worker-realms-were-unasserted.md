# Finding 463 — codec worker realms were unasserted

Phase 463; agent Muskox.

## What was wrong

`TextDecoder` and `TextEncoder` were already available in the classic worker
covered by Phase 206, but no suite checked them inside a module worker or a
nested worker. The expected per-interpreter install was an implementation
argument rather than a test for those realms; a regression could therefore
remove the globals there while window and classic-worker tests stayed green.

## What changed

A standalone suite starts a module worker and checks a UTF-8 encode/decode
round trip there. That worker starts a child worker, which performs and
returns a second round trip. The page checks both strings after they cross the
worker message boundary. The window's own round trip is the control. No engine
source change was needed: the test confirms each worker's interpreter already
installs the codec globals.

## Verification

`python3 tools/ci/check_suites.py` accepts the suite, and `make -j2 test`
passes 508/508 engine suites and 114/114 cross-origin checks.

## Scope

This closes an assertion gap; it does not add `TextDecoderStream`,
`TextEncoderStream`, or expand the `<meta charset>` prescan. Those remain
separate work.
