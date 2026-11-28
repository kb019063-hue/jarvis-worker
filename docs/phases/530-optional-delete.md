# Phase 530 — optional delete keeps the property reference

Task: the optional-delete defect left by Phase 510; finding 530; agent Vega.

## What was wrong

The parser wraps an optional chain in `OptionalChainExpr`. `delete` treated
that wrapper as an ordinary expression: it read the last property, discarded
the value and returned true. Thus `delete o?.x` returned true but left `x` on
`o`; a getter on `x` also ran even though deletion must not read it.

## What changed

The tree-walker's delete path unwraps the optional chain and handles its
final member or indexed expression as a reference. It evaluates the base once,
respects an earlier short circuit or the final optional link, and returns true
without evaluating the computed key when the chain short-circuits. Otherwise
it uses the existing deletion path, including strict-mode refusal. A chain
ending in a call remains a value expression: the call runs and delete returns
true. A chain-local flag scope keeps nested and parenthesised chains separate.

## Verification

`tests/suites/530-optional-delete.cpp` failed on the original witness before
the fix; all 20 traces were checked against Node v22.22.3. It covers dotted
and computed references, a getter not being read, key conversion and
short-circuit side effects, calls and receivers, grouped chains, nullish
non-optional links, inherited and non-configurable properties, strict mode and
symbols. `make test` passes 561 engine suites and 114 cross-origin checks.
`make verify-bytecode` reports 172/172 agree and 10/10 still refused. HTML
conformance is 1973/1973, JS 1153/1158 with a 90/90 everyday probe, DOM
678/678, URL 1188/1188, XML 1718/1718, and WPT 9570/9824 with no unlisted
failures. `diff-trees`, `diff-xml`, `check-numbers`, `check-suites` and
`check_docs.py` pass.

## Still open

This change is limited to the tree-walker's optional-delete path. The
non-nullish computed assignment/update conversion order and the bytecode VM's
nullish writes and non-callable calls, recorded alongside this task, remain
separate work. No performance or external-conformance improvement is claimed.

Locally `make asan TEST_OPT='-O0 -g0'` built the instrumented engine and
suite; the Phase 530 suite passed there. The full sanitizer unit run was
killed while running the unrelated deep-nesting suite, after it printed a
RangeError, so it is not a full local pass. The compiler was limited to
3.3 GB to avoid exhausting the 3.8 GB sandbox. A subsequent local `make
deep` was started but stopped before completion; CI must run both full checks.
`make fuzz` subsequently passed with the instrumented engine.

After merging main, `make test` passes 567 suites and 114 cross-origin
checks; the bytecode oracle remains 172/172, JS conformance is 1154/1158,
and the other conformance, differential and documentation checks pass.
