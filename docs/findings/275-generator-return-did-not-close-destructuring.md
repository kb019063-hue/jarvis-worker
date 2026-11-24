# Finding 275 — a generator return was treated as a throw by destructuring

**Severity:** correctness. A generator suspended inside an array destructuring
assignment did not finish `IteratorClose` according to the kind of completion
that resumed it. This affected observable calls to `iterator.return()` and
whether its error or the generator's requested return value was delivered.

**Found in:** the remaining `dstr` tests for Phase 176's IteratorClose row. At
the pinned test262 revision, `language/expressions/assignment/dstr` had 19
failures, including `array-elem-iter-rtrn-close-null.js` and the return-error
variants.

## Two completion types, two answers

`bind_pattern` had one catch-all path for abrupt completion. It called
`close_quietly()`, which is correct when destructuring has already thrown: the
pattern's throw wins even if the iterator's `return()` throws, is not callable,
or returns a primitive. But `generator.return(value)` resumes a suspended
yield with a **Return** completion, not a Throw completion. For a Return,
`IteratorClose` calls the iterator's `return()` and then:

- preserves the generator's return value if closing succeeds;
- propagates an error from `return()`;
- throws `TypeError` if `return()` returns a non-object.

Swallowing the close error made a generator appear to close successfully when
the standard requires the iterator's error to replace the pending Return.

## A return that could not reach its suspension point

Assignment targets have observable evaluation order too. In
`result = [ {}[yield] ] = iterable`, the computed member target is evaluated
before requesting the iterator's next value. A `yield` there can suspend the
generator while the iterator is still open. The engine instead called `next`
first; an iterator without `next` failed before the generator yielded, so a
subsequent `.return()` could not close it. A simple member target in an
assignment-rest element has the same ordering relative to draining the rest.

## The fix

In `src/js/JsEval.cpp`, `bind_pattern` now resolves simple member assignment
targets before `IterationCursor::pull()` and before draining an assignment-rest
iterator. It delays the nullish-base write check until the value/default is
ready. On unwind, `GeneratorReturnException` uses `close()` rather than
`close_quietly()`; if close succeeds, the original Return completion continues,
and if close fails, that failure replaces it. Existing Throw completions still
use the quiet close.

The regression is in
`tests/suites/271-generator-return-closes-pattern.cpp`. The pre-existing Phase
176 suite also had expected two `next` calls for a throwing second target; the
standard evaluates that target first, so the correct count is one, and the
assertion was corrected in `tests/test_suite.cpp`.

## Result

The pinned test262 directory `language/expressions/assignment/dstr` now passes
**368/368**, compared with **349/368** on the pre-change `JsEval.cpp` (19 gained,
0 lost). The entire `language/expressions/assignment` directory passes
**466/485**, compared with **447/485** before the change (19 gained, 0 lost).
`make test` passes all **410 engine suites** and **114 cross-origin checks**.

The 19 remaining failures in the wider assignment directory are outside `dstr`;
they include Proxy-only coverage and cases blocked by features this engine does
not implement. This finding claims neither all assignment evaluation nor
async-generator coverage.
