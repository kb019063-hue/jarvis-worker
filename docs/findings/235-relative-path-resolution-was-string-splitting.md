# Finding 235 — relative path resolution was string splitting

Phase 231. Status: **hierarchical absolute bases corrected; legacy bases open**.

## 1. What was wrong

The path-bearing resolver treated every slash in a serialized base as a path
separator. Query and fragment slashes could move a resource to the wrong
directory; `//host` did not replace the authority; `../../up` could turn the
host into `up`.

## 2. What changed

For valid hierarchical absolute bases, relative path references go through
the existing URL parser. An invalid reference returns an empty result rather
than falling back to a different host. Absolute reference passthrough and
legacy filesystem-style base handling remain unchanged.

## 3. Evidence

`make verify-path-resolution` matches Node's URL on 132/132 path-bearing
cases, compared with 22/132 using the prior resolver; the earlier pathless
matrix still matches 50/50. `make test` passes 375 engine suites, including
two real shell clicks against separately mapped origins. See
`docs/phases/231-path-references-use-the-url-parser.md` for reproducible
commands and the cases covered.

## 4. What remains wrong

This is a selected oracle, not full URL conformance. Relative filesystem-style
bases, opaque or invalid bases, canonicalizing absolute references, pathless
invalid-reference errors and same-document fragment navigation remain open.
