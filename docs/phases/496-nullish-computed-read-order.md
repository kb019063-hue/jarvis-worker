# Phase 496 — a nullish read evaluates but does not convert its key

Task: the tree-walker follow-up left by Phase 474's VM nullish property reads;
finding 496; agent Vega.

## What was wrong

A computed read evaluates its base and key expression before checking the
base. If the base is null or undefined, however, it throws a TypeError before
converting the key to a property key. The tree-walker converted the key first:
with a `toString` that appends `c`, `null[key]` caught `cTypeError` rather than
Node v22.22.3's `TypeError`. The same mistake affected computed method calls,
and a throwing conversion could hide the intended TypeError.

## What changed

`computed_read_key` in `src/js/JsEval.cpp` checks the base after key evaluation
and before object-key conversion. Both `eval_index` and the computed-member
branch of `eval_call` use it. Primitive keys still appear in the engine's
existing error messages; object keys are not examined for diagnostics.
Optional chaining still returns before evaluating the key. A successful call
still converts once and retains its receiver. The call path reuses the existing
receiver slot to avoid increasing the native frame for unrelated recursive
calls: an extra `JsValue` local made the existing 2000-call conformance test
fail with a stack overflow, and reusing the slot restored it.

## Verification

`tests/suites/496-nullish-computed-reads.cpp` failed on the original witness
before the fix and passes afterwards. It checks both nullish values, key
expression and conversion ordering, a throwing expression and conversion,
a `Symbol.toPrimitive` getter, optional chaining, calls and their arguments,
non-nullish receivers and diagnostics. Node was used to verify the ordering.
`make verify-bytecode` gives 160/160 agreements and 10/10 still refused;
three independently checked cases add nullish object-key reads and a computed
call to the differential oracle. Its `--self-test` passes. `make test` passes
533 engine suites and 114 cross-origin checks. HTML conformance is 1973/1973;
JS is 1153/1158 with the 90/90 everyday probe; DOM 678/678, URL 1188/1188,
XML 1718/1718, WPT 9570/9824 with no unlisted failures; `diff-trees`,
`check-numbers`, and `check-suites` pass.

## Still open

This fixes reads, not computed assignment, compound assignment, update or
`delete`. Node shows these nullish operations also must not convert an object
key, while the tree-walker still does. Simple assignment evaluates the RHS
before its TypeError; compound/update and delete do not. The VM's simple
nullish indexed write is also wrong: the probe `null[key] = (s += 'r')`
returns `rc` rather than Node's `rTypeError`. Non-callable VM calls and other
VM refusals remain separate work. No external conformance improvement or
performance improvement is claimed.

The local `make asan` built the instrumented engine but its unit-suite
compiler was killed for memory even without a concurrent depth run, so it is
not a full local sanitizer pass. The full sanitizer suite remains a CI
requirement.

A full CI run on the pre-merge head passed the sanitizer and deep jobs. The
local deep run was stopped before completion after that CI run, rather than
claimed as a pass. After merging main, `make test` passes 542 suites and 114
cross-origin checks; `verify-bytecode` remains 160/160, JS conformance remains
1153/1158, and `diff-xml`, `fuzz`, `check-numbers`, `check-suites`, and
`check_docs.py` pass.
