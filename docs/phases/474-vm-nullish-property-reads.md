# Phase 474 — the VM rejects nullish property reads

Task: the nullish property-read gap in the bytecode VM differential-oracle
row; finding 474; agent Vega.

## What was wrong

`try { null.foo; } catch (e) { e.name; }` answers `TypeError` in Node
v22.22.3 and the tree-walker. The bytecode VM instead let `JsValue::get_property`
answer `undefined`, so its catch was never reached. The same applied to
`undefined`, dotted and indexed reads, and reads hidden under `typeof` or a
call. Before the fix `tests/suites/474-nullish-property-reads.cpp` failed on
the first witness.

## What changed

`OpGetProperty` rejects null and undefined with a JavaScript `TypeError`
including the property name. `OpGetIndex` runs the computed key expression
first, then rejects a nullish base before coercing the key; it includes a
string key in the message without executing user code. Other bases and their
property lookups continue through the existing path. The VM's existing
exception unwinder makes the error catchable, including across a function call
and an iterator body, where it closes the iterator on abrupt completion.

The order matters: with an object key whose `toString` appends `c`, Node
answers `TypeError`, not `cTypeError`, for the trace in the suite. The
current tree-walker still converts the object key first and answers
`cTypeError`; that separate mismatch is not silently used as the oracle.

## Verification

The suite was run before the fix and failed on the original witness, then
passed in the normal build. It covers both nullish bases and both read forms,
string-key messages, primitive and ordinary-object reads, a key-expression
side effect, `typeof`, a call, catch, an uncaught JavaScript throw, iterator
cleanup and the object-key order against Node. `make verify-bytecode` reports
143/143 agreement and 8/8 still refused; seven new independently observed
cases are included. `python3 tools/conformance/verify_bytecode_agreement.py
--self-test` passes. After merging main, `make test` passes 524/524 suites
and 114 cross-origin checks; HTML, JS, DOM, URL and XML conformance, WPT,
`diff-trees`, `diff-xml`, `check-numbers` and `check-suites` pass.

A 1332-run deep-page check had one timeout while a sanitizer compilation was
running at the same time. The timed-out terminal case passed by itself in
13.4 seconds. A sanitizer build of the engine and an ASan/UBSan probe for
three nullish-read cases passed; the full local `make asan` could not compile
the very large test suite while the deep check was running, because this
3.8 GiB sandbox killed the compiler for lack of memory. With no deep run in
parallel the sanitizer engine and suite built on the merged tree, and the
new suite passed in its own ASan/UBSan stretch. `make fuzz` then passed.
The complete sanitizer and deep checks must also run in CI; neither is
claimed as a full local pass here.

## Still open

Nullish writes and other VM operations are separate work. The tree-walker's
computed nullish read still coerces object keys too early. The VM remains an
experimental engine and optional chaining, full receiver handling and the
other differential-oracle refusals remain incomplete. No external
conformance score or speed improvement is claimed.
