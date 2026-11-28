# Phase 510 — a nullish reference does not convert its key

Task: the tree-walker computed-write and delete follow-up left by Phase 496;
finding 510; agent Vega.

## What was wrong

A computed property reference evaluates its base and key expression, but
`ToPropertyKey` must not call an object key's conversion when the base is
null or undefined. `null[key] = rhs` ran the key object's `toString` before
`rhs`, and compound assignment, update and `delete` also ran it before their
TypeError. A throwing conversion could replace the required exception.
Node v22.22.3 instead evaluates the key expression, then: for simple
assignment evaluates the RHS before throwing; for compound/update and delete
throws before the RHS or update. Neither path converts the object key.

## What changed

`computed_reference_key` in `src/js/JsEval.cpp` keeps object keys away from
conversion when the base is nullish. The existing `resolve_reference` still
checks the base immediately for reads of a compound/update reference, but
plain assignment and destructuring defer the error until the assigned value is
ready. The `delete` path uses the same key rule and retains its own nullish
TypeError. Non-nullish bases keep the existing conversion path; primitive key
diagnostics remain as before. For a nullish object key, `<computed>` is a
diagnostic label, not a conversion of the object.

## Verification

`tests/suites/510-nullish-computed-writes.cpp` failed on the first witness
before the fix and passes afterwards. It covers both nullish bases, key and
RHS side effects and exceptions, `Symbol.toPrimitive`, compound and logical
assignment, prefix/postfix updates, destructuring defaults and iterator
closing, `delete`, and ordinary-object mutation. Node confirms the traces.
`make test` passed 548 engine suites and 114 cross-origin checks;
`make verify-bytecode` reports 172/172 agree and 10/10 still refused.
HTML conformance is 1973/1973; JS is 1153/1158 and the everyday probe 90/90;
DOM is 678/678, URL 1188/1188, XML 1718/1718, and WPT 9570/9824 with no
unlisted failures. `diff-trees`, `diff-xml`, `check-numbers`, `check-suites`
and `check_docs.py` passed.

## Still open

This narrowly fixes nullish references. Non-nullish object keys in plain
assignment are still converted before the RHS, whereas Node converts after;
compound/update references convert once here versus twice in Node. Optional
`delete` on a non-nullish base also does not delete its property. VM nullish
writes and non-callable calls remain separate work. No external conformance
score or speed improvement is claimed.

`make asan` built the instrumented engine, but the unit-suite compiler
exhausted a 3.3 GB address-space limit imposed to protect this 3.8 GB sandbox;
the full sanitizer run did not complete. All 19 dedicated JavaScript
witnesses passed individually in the instrumented engine. The local `make
deep` run was started after that, then stopped before completion; the full
sanitizer and deep checks remain required in CI.
`make fuzz` subsequently passed with the instrumented engine.

After merging main, `make test` passes 553 suites and 114 cross-origin
checks; the bytecode oracle remains 172/172, JS conformance 1153/1158,
and the other conformance, differential and documentation checks pass.
