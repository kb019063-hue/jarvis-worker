# Phase 231 — path references use the URL parser

Task row: Phase 221 left the path-bearing branch of
`ResourceLoader::resolve_url` separate. Finding 235.

## 1. What was wrong

The helper split a serialized base at its last slash and normalized the
result as a list of slash-delimited strings. That conflated a host, path,
query and fragment. For example, against `http://example.test/dir/page.html`,
`../../up` became `http://up`, changing the host instead of staying at
`http://example.test/up`. A slash in the old query could select the wrong
directory, `//cdn.test/file` failed to replace the authority, trailing and
repeated slashes were lost, and query and fragment content could be treated as
path segments. The browser's URL API already had a parser for these parts, but
this helper did not use it for path-bearing references.

## 2. What changed

For a relative path-bearing reference and a valid hierarchical absolute base,
`ResourceLoader::resolve_url` uses the existing WHATWG `Url` parser and its
serializer. A reference the parser rejects returns an empty string, rather
than falling through to a potentially different valid host. This branch
resolves path, query, fragment and authority together; dot segments cannot
climb above the authority. Phase 221's pathless path remains in place.

The existing absolute-reference passthrough (including opaque `data:` bytes)
and the legacy fallback for non-URL filesystem-style bases are deliberately
unchanged. This is not a general redesign of the string-valued API or a claim
of full URL conformance. Callers that need strict validation still parse the
result; opaque and invalid bases remain outside this correction.

The suite checks fixed reference cases and real hit-tested links using two
file-backed HTTP origins: one has a slash in its old query, the other changes
authority with `//`. No socket is needed. CI runs an independent Node URL
comparison over the helper itself.

## 3. Verification

```
make test
# 375 engine suites pass; cross-origin suite: 114 checks, 0 failures.
make verify-path-resolution
# Path reference oracle: 132/132 agree with Node URL
make verify-relative-navigation
# Pathless reference oracle: 50/50 agree with Node URL
```

The path matrix is six http/https/file bases by 22 references, including
network-path references, dots, percent-encoded dots, query and fragment slashes,
backslashes, whitespace, repeated and trailing slashes, and invalid file
hosts with ports. Node rejection corresponds to the empty error result. The
same matrix against a fixture linked with the old `ResourceLoader.cpp` from
`103b966` agrees on **22/132** and exits 1. To reproduce without changing the
tracked tree after `make`:

```
git show 103b966:src/net/ResourceLoader.cpp > /tmp/old-resource.cpp
g++ -std=c++20 -O2 -Iinclude -c /tmp/old-resource.cpp -o /tmp/old-resource.o
g++ $(find build -name '*.o' ! -path 'build/net/ResourceLoader.o' ! -name main.o -print) /tmp/old-resource.o tools/conformance/relative_navigation_fixture.cpp -Iinclude -std=c++20 -o /tmp/old-url-fixture
python3 tools/conformance/verify_path_resolution.py --binary /tmp/old-url-fixture
# Path reference oracle: 22/132 agree with Node URL; exit 1
```

`make conformance`, JS/DOM/URL/XML conformance, WPT, render conformance,
`make diff-trees` and `make diff-xml` pass their existing floors. This matrix
is a targeted differential, not an exhaustive security or conformance audit.

## 4. What remains open

The legacy relative filesystem-style fallback and opaque/invalid base behavior
are not converted to the URL parser; absolute references are returned as they
were supplied, not canonicalized here. The pathless invalid-reference fallback
also retains Phase 221's string-valued limitations. This does not implement
full same-document fragment navigation, test all URL Standard cases or repair
other real-page discrepancies.
