# Finding 225 — query references lost the filename

Phase 221. Status: **pathless references fixed; broader URL handling remains**.

## 1. What was wrong

Appending a query or fragment through `ResourceLoader::resolve_url` first
removed the base's last path segment. Holdout-02 H4's `?firstName=…` link
therefore tried to navigate to a mapped directory instead of the original
`index.html`.

## 2. What changed

Query-only, fragment-only and empty references keep the document path. Valid
absolute bases use the existing URL parser; relative filesystem-style bases
use suffix-preserving lexical resolution. The separate path-bearing branch
is unchanged.

## 3. Evidence

`make verify-relative-navigation` agrees with Node's URL on 50/50 cases; a
binary with the old resolver agrees on 1/50. `make test` passes 363 engine
suites, including a mapped-origin click and keyboard activation. In a rerun
of the exposed MDN H4 holdout the actual link click now produces the expected
sorted and decoded parameter rows instead of the navigation error. See
`docs/phases/221-query-references-keep-the-document.md`.

## 4. What remains wrong

The test does not establish general URL conformance or fix the path-bearing
legacy branch, invalid/opaque-base error reporting, shell fragment navigation,
or unrelated real-page and pixel discrepancies.
