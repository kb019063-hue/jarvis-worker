# Phase 271 — a generator return closes a suspended destructuring iterator

The Open tasks row that started this phase belongs to Phase 176 / finding 181:
iterator closing during array destructuring. The remaining hole was where
`yield` suspends while an assignment pattern is evaluating its target or its
default, then the consumer resumes the generator with `.return(value)`.

## What was wrong

The binding loop treated every exceptional exit as an existing **Throw**
completion: it called `IterationCursor::close_quietly()`, which correctly
preserves a pattern's own throw even when `iterator.return()` throws or returns
a primitive. But `.return(value)` injects a **Return** completion. Under
ECMAScript `IteratorClose` (§7.4.11), an error from the iterator's `return`
method replaces a Return completion; only an existing Throw completion wins
over that error. The previous catch swallowed both cases alike.

There was a second ordering defect in the same binding path. In an assignment
pattern, a simple member target such as `object[key]` is evaluated before the
iterator step for that element; an assignment-rest member target is evaluated
before the remaining iterator is drained. `bind_pattern` pulled first, so a
`yield` inside either target was never reached when `next` was absent or threw.
That prevented the suspended frame from receiving the consumer's return at the
very point where the pattern must close the still-open iterator.

## What changed

`bind_pattern` now resolves a simple member target in assignment patterns
before pulling its element, and resolves a simple member target of an
assignment-rest element before draining the iterator. It passes
`throw_on_nullish=false` to `resolve_reference`, then checks the base only when
the value (and any default) is ready. This preserves the ordering of a normal
plain assignment and lets a target's own side effects or abrupt completion run
before `IteratorStep`.

The array-pattern unwind now handles `GeneratorReturnException` separately:
it calls `close()` so a throwing `return`, a non-callable `return`, or a
non-object result can replace the pending Return completion, then rethrows the
original completion if close succeeds. Other abrupt completions continue to
use `close_quietly()`, so an existing pattern throw remains the error the
script observes. A `next` that throws still does not trigger IteratorClose.

The regression suite covers successful `.return(value)`, close receiver and
argument count, no extra `next`, member and rest-member targets that suspend
before `next`, a throwing close, a primitive close result, and preservation of
an existing Throw completion. The earlier Phase 176 count assertion was also
corrected: a target that throws in the second element runs only one `next`,
because that second target is evaluated before its own iterator step.

## Verification

- `make test`: **409 engine suites passed**; **114 cross-origin checks, 0
  failures**.
- `python3 tools/conformance/run_test262.py language/expressions/assignment/dstr
  --show 40 --timeout 10 --jobs 2`: **368/368 pass**. A differential against
  the pre-change `JsEval.cpp` from `598c4cf` went from **349 to 368** (**19
  gained, 0 lost**).
- `python3 tools/conformance/run_test262.py language/expressions/assignment
  --show 20 --timeout 10 --jobs 2`: **466/485 pass (96.1%)**. The same
  pre-change comparison went from **447 to 466** (**19 gained, 0 lost**).

## Still open

The broader assignment directory still has 19 failures, all outside `dstr`:
Proxy-dependent cases, two cases blocked by unsupported BigInt literals, and
other assignment/property-reference semantics. This phase adds no
async-generator-specific test coverage and does not claim the rest of
assignment evaluation is complete.
