# Finding 133 — 500 of test262's failures were one kind of question

## What was seen

`make test262` on this tree: **13,935 / 15,686 (88.8%)**, 1,751 failures. The
runner lists a failing test by its own message, and the largest group by far
was:

```
expected SyntaxError; exit 1 Uncaught Test262: This statement should not be evaluated.
```

500 of them. Read separately that is a broken parser in five hundred places.
Read by directory it is one task:

```
190  language/statements/class
174  language/expressions/class
 60  language/expressions/object
 38  language/statements/function
 26  language/expressions/function
 10  language/expressions/assignment
  2  language/expressions/delete
```

Every one of these tests is written the same way — a `negative` header naming
the phase, then a `$DONOTEVALUATE()` marker at the top of the body. The marker
is a function that throws if it is ever called. So "the statement should not
have been evaluated" is not a complaint about a wrong answer; it is the sound
of the engine **running a program the language says cannot exist**.

## Why it is worth a finding rather than a to-do

Three things about this cluster were not obvious from the outside.

**A missed early error cannot be fixed by throwing later.** The instinct on
seeing "expected SyntaxError, got a runtime error" is to move the check. For
`super()` in a method that is wrong in both directions: Node does not throw
`ReferenceError` at runtime for `class C extends Object { m() { super(); } }`,
it refuses to parse it, because a method's `FunctionBody` does not contain its
own `super()` at all. The construct is not in the grammar there. Any fix that
waits until the call is evaluated cannot pass the test, because the test never
runs.

**The failures cluster by rule, not by test.** Five rules accounted for 364 of
the 500, and each rule is a sentence in §15.7. The remaining 136 are the same
*kind* of failure in three other directories. That ratio — 500 occurrences,
eight sentences — is why this was worth doing before the 151 runtime-`TypeError`
failures, which really are separate bugs.

**A narrowing change can raise a score while widening a bug.** The first cut of
the constructor rule refused every instance method of every class. test262's
pass count still went *up*, because the change gained far more than it lost;
`make test` is what caught it, in the engine's own suites, within seconds. A
change whose purpose is to reject source cannot be graded on how much it
rejects.

## What it says about the scoreboard

The repository's own position is that test262 "is not a scoreboard and has no
floor" (`Makefile`, `make test262`), and this finding is an argument for that
being the right call rather than a hedge: 1,751 failures were not 1,751 units
of work, they were roughly a dozen mechanisms seen through many tests. The
useful number is the one the runner prints beside it —

```
173 pass in now and not in before; 0 pass in before and not in now
```

— because a parser change that gains 173 and loses 3 is a different event from
one that gains 176, and only the two-binary run distinguishes them.
