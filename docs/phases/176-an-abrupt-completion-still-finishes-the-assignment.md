# Phase 176 — an abrupt completion still finishes the assignment

Task: the `language/expressions/assignment` cluster — 50 failing cases out of
485, of which 25 were one theme. Finding 180.

The two defects here are both about what happens when an assignment **jumps
out before it is finished**. The standard has a rule for that in each case, and
this engine skipped it.

## 1. The right-hand side runs before a nullish base is reported

```js
var count = 0, base = null;
base.prop = count += 1;      // TypeError — and `count` is 1
```

§13.15.2 evaluates the left-hand side first and the right-hand side second, and
`PutValue` is what complains about a nullish base — *after* the value is in
hand. `resolve_reference` complained while resolving, so the RHS never ran:

| source | this engine, before | Node 22 |
| :--- | :--- | :--- |
| `base.prop = count += 1` with `base` null | TypeError, `count` 0 | TypeError, `count` 1 |
| `base.prop = count += 1` with `base` undefined | TypeError, `count` 0 | TypeError, `count` 1 |
| `(null).p = (c += 1)` | TypeError, `c` 0 | TypeError, `c` 1 |

The fix is a flag, not a reordering: `resolve_reference` takes
`throw_on_nullish`, which is `false` only for a plain `=`, and `check_reference_base`
makes the same call once the value exists. Every other use — compound
assignment, `++`/`--`, pattern targets — keeps the eager refusal, because there
the read of the current value comes first and the base has to be usable.

`reference.base` is left nullish rather than being replaced by a placeholder, so
`check_reference_base` is the single place that words the error. The messages
are byte-identical to before (`Cannot set properties of null (setting 'p')`),
which is what kept the `js-conformance` corpus unchanged.

## 2. A pattern that fails part way through closes the iterator

`IterationCursor` has had a `close()` since the pattern binder was written, and
the normal path called it. No error path did:

| source | before | Node 22 |
| :--- | :--- | :--- |
| `[ y = thrower() ] = iterable` | `return` called 0 times | 1 |
| `[ {}[thrower()] ] = iterable` | 0 | 1 |
| the same, `return` throws | 0, and the close error would win | 1, pattern's error wins |
| `[ , ] = iterable` whose `next` throws | 0 | **0** — 7.4.8 step 7 |

The last row is why this is not simply "call `close()` in a destructor". A
`next` that throws is a completion of the *step*, not of the iteration: the
standard's own `IteratorStepValue`/`IteratorClose` split means the iterator is
not told anything, and `array-elision-iter-abpt.js` asks for exactly that. My
first attempt closed unconditionally and lost seven cases; the census caught it
immediately, because they are the seven that assert `returnCount` is 0.

So `pull()` records that `next` itself threw, and the binder's `catch` closes
only when it did not. The close itself is `close_quietly()`: 7.4.11 with an
*existing* abrupt completion says the original throw wins, so a throwing
`return`, a non-callable `return`, and a `return` that answers a primitive are
all swallowed on that path — while the pattern's own final `close()` still
reports them, which is what a pattern that simply bound fewer names than the
iterator holds needs.

## 3. The scoreboard

Two binaries, test262 at `2e58f87d`:

```
language/expressions/assignment
  before     435 pass (89.7%)  [fail 50]
  now        441 pass (90.9%)  [fail 44]
6 pass in now and not in before; 0 pass in before and not in now

the whole suite
  before     14889 pass (94.9%)  [fail 797]
  now        14895 pass (95.0%)  [fail 791]
```

The six: `target-member-identifier-reference-null`,
`target-member-identifier-reference-undefined`, `array-elem-iter-rtrn-close`,
`array-elem-trlg-iter-list-rtrn-close` (the reference-order fix), and
`default-expr-throws-iterator-return-get-throws`,
`target-assign-throws-iterator-return-get-throws` (the close fix).

Everything else unchanged: `make test` **327/327** including the suite added
here, js-conformance **1145/1158**, dom-conformance **678/678**.

The two suite assertions I got wrong first — both about an *exhausted* iterator,
which is never closed — were corrected against Node 22 before the test was
committed, and Node was asked again afterwards for all four counts.

## 4. What is still open

Most of the remaining `dstr` cases need `generator.return()` to close a
suspended pattern:

```js
function* g() { var v; [ v = yield ] = iterable; }
var it = g(); it.next(); it.return(777);     // `return` must be called once
```

That is the generator frame, not the binder: `return()` has to deliver a
completion into a suspended frame and let the frame's own abrupt-completion
handling run the close. Same rule, different machinery, and a different file —
it is recorded in `AGENTS.md` as the next step for this row rather than being
half-done here.

Two more that are *not* this row: `array-elem-iter-rtrn-close-null.js` (`return`
answering null from a generator frame) and the `iterator-return-is-not-callable`
pair, which need `BigInt` to parse at all.
