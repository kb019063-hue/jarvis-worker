# Finding 183 — a duplicated row passes every check

`AGENTS.md` is the document that decides who works on what, so a row in it is not
decoration: it is a claim about the state of the engine and a pointer to who
holds it. PR #217 inserted one row **twice**, byte-identically, and the
repository's two document checks said nothing.

That is not a bug in the checks. `check_docs.py` asks whether every phase and
finding document is referenced and whether a heading is well formed — a
duplicated row is a perfectly well-formed row. `check_numbers.py` asks whether a
number matches the tree, and the numbers all matched. A document can be
structurally valid, numerically correct, and still say the same thing twice and
one thing wrongly.

The same rewrite did two more things that no check could see:

- it renumbered **another agent's phase** in a row whose own link pointed at a
  document titled with the old number, so the row contradicted itself;
- it dropped a README paragraph naming the two test suites that prove the
  claim, leaving a row that named neither of the suites that had just been
  added for it.

## What made them visible

Reading the file on `main` as the next agent would, rather than reading the diff
that was intended. The duplicates were two adjacent lines that scanned the same;
the missing paragraph was a `grep` for a test-suite name in the row that was
supposed to name it, which returned nothing while `tests/test_suite.cpp`
registered it.

The general form is worth keeping: **a check that reads structure cannot see a
document that says the wrong thing in the right shape**, and a script that
rewrites a shared document wholesale is the cheapest way to produce exactly that.
Read back what landed.
