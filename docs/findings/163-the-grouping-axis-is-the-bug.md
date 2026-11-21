# Finding 163 — the grouping axis is the bug

## What was seen

884 test262 failures. Grouped by directory, the tree looked like this:

```
110  built-ins/Object/defineProperty
 82  built-ins/Object/defineProperties
 31  language/statements/class/elements
 30  language/statements/function
 24  language/expressions/object
```

A directory is where a test *is*, not what it is about. Those two `Object`
directories are 192 cases of one known, deep problem — array-index attribute
storage — and the next four are five different problems that happen to sit in
the same folders as tests about something else.

Grouped by the message the case failed with, the same 884 look like this:

```
113  Expected a TypeError to be thrown but no exception was thrown at all
101  Proxy is not defined
 55  $262 is not defined
 51  isConstructor(<some built-in method>) must return false
```

The fourth line is 51 cases that agree, which is a signal a directory cannot
give: a directory is a *topic*, and a message is a *claim*. Fifty-one tests
making the same complaint about fifty-one different functions means the
complaint is about the test helper and the engine's model of what a function
is, not about `Object.defineProperty`.

## Why the message is the better axis

Because it is what the test already did the work of stating. `assert.throws`
and `assert.sameValue` produce a sentence, and that sentence is a summary the
test author wrote: "isConstructor(Object.defineProperty) must return false".
Sorting by it clusters by *cause*, and the cause usually turns out to be one
line.

It also protects against the opposite mistake — reading 113 "expected a
TypeError" failures as one problem. They are 113 different expectations that
share a shape ("the engine should have refused"), spread across `bind`,
`defineProperty`, `assign`, classes and regexps. The same sentence, once you
read past it, is the one case where the axis misleads, and only because it is
the message the harness writes when *any* throw was expected.

## The rule

Group by directory to decide what a phase is *about*. Group by failure message
to decide what to *fix*. The two answers are rarely the same, and the second
one is the one that moves the score.
