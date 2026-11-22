# Phase 179 — two rows the merge damaged, and what they cost

Task row: none of its own. This repairs damage **PR #217** (Phase 177) did to
`AGENTS.md` and `README.md`, found by reading the file on `main` the way the next
agent would. Finding 183.

## What was wrong on `main`

Three things, all mine, all from the same PR:

1. **One row was inserted twice.** `A method's this is boxed before the call`
   appears at two consecutive-ish places with byte-identical text. The
   insertion script ran with a stale line index, so the same paragraph was
   written at the anchor and again one line below it. Both copies were
   committed, the doc checks passed (a duplicated row is not an invalid row),
   and it landed.
2. **Another agent's row was renumbered.** Phase 176's row (`An abrupt
   completion still finishes the assignment`) was rewritten to say "since Phase
   177" because a blanket `Phase 176` → `Phase 177` replacement was run across
   `AGENTS.md` when *my* phase moved. It pointed at
   `docs/phases/176-an-abrupt-completion-still-finishes-the-assignment.md` — a
   file whose own first line says "Phase 176" — so the row contradicted its own
   link. Every reference to that phase is now correct again.
3. **A README row was edited into a stale claim.** The `ArrayBuffer` and typed
   arrays row said "after Phases 126, 157 and 176" in its heading while its body
   cited Phase 177, and the paragraph describing what Phase 177 added was
   **missing entirely**: both new suites were registered in
   `tests/test_suite.cpp` and neither was named in the row that is supposed to
   name the suites that prove a claim.

## Why the checks did not catch it

`check_docs.py` asks whether every phase and finding document is referenced and
whether headings are well formed. A duplicated row is a well-formed row; a row
that says the wrong number in one place and cites the right document in another
is still a link that resolves. `check_numbers.py` asks whether the suite *count*
matches, and it did: 335 suites in the tree, 335 in README.

None of these three is a number. All three are the kind of error that a number
check cannot see and a link check cannot see, and all three came from a script
that rewrote a document wholesale — which is exactly what the repository's own
notes warn about, and what the last four phases kept doing.

## The repair

- the duplicate row is deleted, keeping the first, which is the one the
  surrounding order belongs to;
- the abrupt-completion row says "since Phase 176" again, which is what its
  document says;
- the typed-arrays row says "after Phases 126, 157 and 177" and names
  `a_typed_array_to_locale_string_asks_every_element` and
  `a_typed_array_toString_is_arrays_own_function` among the suites that prove
  it, with the +14 measurement.

Verified after the repair, on a full build: `make test` 335/335, both new suites
PASSED, `check_numbers.py` and `check_docs.py` 0 failures, and the Phase 177
behaviour is still identical to Node v22.22.3 character for character
(`TA=X,X NaN=X identity=true plain=1,2 len9=9|1,2,3`).

## What this says about the process

Every one of these came from a **wholesale rewrite of a shared document by a
script**, and the repository's checks are structural, so the damage was invisible
to them. The cheap protection is what found it here: read the file on `main` back
and look at it as the next agent will, instead of trusting that the diff that
merged was the diff that was intended.

Two related losses were found and are **not** repaired here, because they belong
to other pull requests: `docs/phases/173-fetch-request-constructor.md` and
`docs/phases/174-offscreencanvas.md` are both on `main` and referenced by no
document, and PR #215 (OffscreenCanvas) touched `README.md` only to move the
suite count from 334 to 335 — its prose row never arrived. Those are their
authors' rows to write; this phase records them so they are not lost.

There is **one more duplicated row on `main` and it is not from this PR**: the
`align-self` and `align-content` row appears twice at lines 588 and 589, and
both copies were already there at `8188aeb` — before Phase 172, 176 and 177 ever
touched the file. `git log -S` puts the text's arrival at Phases 133 and 135.
Every row in `AGENTS.md` was compared for duplicates by sorting the table lines,
which is how that one surfaced; it is left alone here because a repair PR that
also edits an unrelated row is the thing this phase is complaining about, and it
is recorded as its own row instead.
