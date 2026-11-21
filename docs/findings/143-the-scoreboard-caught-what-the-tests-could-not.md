# Finding 143 — the scoreboard caught what the test suite could not

## What happened

Phase 138 gave an anonymous class expression the name of the binding it is
evaluated for. The version that shipped passes all 118 test262 cases the phase
was aimed at. The version *before* it also passed all 118, and was wrong.

The difference was one line. The first cut did this:

```cpp
if (!cls->name.empty()) class_env->initialize_lexical(cls->name, ctor, true);
else if (!inferred.empty()) class_env->define_readonly(inferred, ctor);
```

— it installed the inferred name as a *binding* inside the class, so the class
body could refer to itself by it. The language says an inferred name is a name
and not a binding: only a class that spells a name gets a scope for it.

Nothing in the 118 tests could see the difference. `name` was correct either
way. What noticed was `make js-conformance`, going from 1143 to 1142, and
naming a case that had nothing to do with classes as a name — a 20,000-deep
chain, whose `static get who() { return this === C; }` answers against the
outer `C`, which the extra binding had shadowed for every class in the chain.

## Why the second reading nearly went wrong

The case is one of 16 that the corpus has been failing for a while, so the
first reflex on seeing it listed is "pre-existing, not mine". That reflex is
usually right — a change that moves a score by −1 against a background of 16
is more likely to have surfaced a known failure than to have caused one.

It was wrong, and the step that settled it was not more reading: it was
building the binary *without* the change and running the one case. The case
passes before, fails after. A scoreboard number says "look here"; it does not
say who is responsible, and no amount of staring at the diff substitutes for
the two-binary comparison the test262 runner already has a mode for.

## The general shape

There are two ways a change can be wrong and only one of them is visible where
the change is:

- **absent** — the feature is not there, and the tests written for it fail;
- **over-present** — the feature is there and does more than the language
  asked, and every test written for it passes.

The second is the one that survives review, because the tests an author writes
are tests of the behaviour they intended. The only defence in this repository
is a scoreboard with cases nobody wrote for this change, graded on *both*
directions — which is why the test262 runner prints "0 pass in before and not
in now" beside its total, and why a one-number movement in
`js-conformance` was worth an afternoon.

## What it cost

One rebuild cycle to find, one line to remove, and a paragraph in the phase
write-up. The alternative — shipping it — would have been 118 test262 cases
richer and a real, silent regression in code that builds deep class chains,
which is the kind of thing a page does and a test suite does not.
