# Phase 221 — query references keep the document

Task row: `ResourceLoader::resolve_url` loses the document filename for
query-only references. Finding 225.

## 1. What was wrong

For an ordinary link to `?firstName=Ayse&food=rice%20pudding`, the legacy
resolver truncated the base at its last slash, then appended the query. From
`http://snapshot.test/url-params/index.html` it produced `/url-params/?…`,
which is a directory in the mapped holdout and fails navigation. Fragment-only
references also lost the filename; empty references retained an old fragment.
Holdout-02 H4 found this via an actual click, not a synthetic URL API call.

## 2. What changed

The no-path cases (`?…`, `#…` and the empty reference) are resolved before
the last-slash path branch. For a parseable absolute base the existing WHATWG
`Url` parser resolves and serializes the reference, so queries replace queries,
fragments replace fragments, an empty reference removes the old fragment, and
encoding and empty authority paths follow the URL implementation. For a
relative filesystem-style base the helper retains the existing path and query
where appropriate and strips only the replaced suffix. Path-bearing references
and the rest of the navigation stack are not rewritten.

The unit suite checks both a real shell hit-tested click and keyboard activation
against a mapped origin, as well as explicit query, fragment and empty cases.
`make verify-relative-navigation` compares the resolver itself with Node's
`URL` on five absolute bases and ten no-path references each; CI runs it.

## 3. Verification

```
make test
# 365 engine suites pass; cross-origin suite: 114 checks, 0 failures.
make verify-relative-navigation
# Pathless reference oracle: 50/50 agree with Node URL
```

The same 50-case oracle run against a binary linked with the old
`ResourceLoader.cpp` from `803046b` agreed on **1/50**. It covers http, https,
file, a host with no path, an existing query and fragment, empty delimiters,
query-internal slashes and spaces; the old file path even lost `file:///`.
These numbers are about this selected matrix, not all URL resolution.

The pinned MDN `dom-examples` snapshot and the original holdout-02 H4 driver,
with sockets denied, were rerun with the same frozen steps (no driver fix):

```
python3 tools/real_pages/holdout-02/capture.py /tmp/cedar-holdout
make build/holdout_02_driver
python3 tools/real_pages/holdout-02/rerun_h4.py /tmp/cedar-holdout build/holdout_02_driver
# Holdout-02 H4: 6/6 frozen reads match after the real click
```

Before: the first read was 1 header row; the click reached the link but
navigation failed and the driver reported `BLOCKED`. After: the link is hit,
and the reads are **1, 3, firstName, rice pudding, food, true**, exactly the
H4 gates. This is a rerun of an exposed example, not a fresh holdout or a
claim that its pixels match Chromium. All existing conformance corpora and
render reftests pass their recorded floors; `make diff-trees` and
`make diff-xml` pass.

## 4. What remains open

The old path-bearing branch is not a full WHATWG resolver, and opaque or
invalid bases have no error return in this string-valued helper. Relative
filesystem-style strings are handled lexically rather than converted to file
URLs. The shell's fragment-click behavior is a separate same-document
navigation problem; this change only resolves the reference. Other holdout-02
gaps and rendering discrepancies remain.
