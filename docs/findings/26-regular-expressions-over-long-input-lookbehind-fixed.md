# 26. ✅ Regular expressions over long input: lookbehind fixed (Phase 24), bytecode VM removes repeated-group recursion (Phase 37)

> Moved out of `AUDIT.md` — written there as a `## 26. ✅ Regular expressions over long input: lookbehind fixed (Phase 24), bytecode VM removes repeated-group recursion (Phase 37)` section.
> The text below is the section as it stood, unchanged.


Found while testing finding 25. With the string no longer the cost, the
matcher's own limits showed.

**Fixed: lookbehind.** `(?<=x)` was implemented by trying its body forward
from every earlier position in the subject, so each lookbehind that failed
cost the length of everything before it. `/(?<=,)\w+/g` over 120,000
characters took 50.6 s where Node takes 37 ms, and `/(?<!,)c/g` over the same
text threw `RangeError: Regular expression too complex`. The body is now
matched right to left from the current position, which is how the language
defines it: the last term first, each character read from before the
position, and a lookahead inside it left to right again. The two expressions
take 40 ms and 9 ms.

That also changes results. A greedy group inside a lookbehind takes its text
from the right: `/(?<=(\d+)(\d+))$/.exec("1053")` captures `"1"` and `"053"`
in every engine, and captured `"105"` and `"3"` here. And in unicode mode a
match can no longer begin between the halves of a surrogate pair:
`/\uDE00/u.test("😀")` was true. Of 1,500 results from 75 lookbehind patterns
over 20 subjects, all agree with Node; the engine before agreed on 1,411.

**Open: a group repeated many times.** The matcher is a recursive
backtracker. Each pass through `(?:...)*` nests several native frames, and
none of them returns until the whole match is decided. It checks the stack
and gives up cleanly, but early:

| Expression | Repetitions before `RangeError` | Node |
| :--- | ---: | :--- |
| `/^(?:a\|b)*$/` | about 21,800 | 200,000 in 2 ms |
| `/^(ab)+$/` | about 12,000 | 200,000 in 2 ms |
| `/^(?:\w+\s)+$/` | about 12,700 | 200,000 in 1 ms |
| `/^(?:[^"\\]\|\\.)*$/`, the body of a string literal | about 12,700 | not measured |

A single character or class under `*` or `+` (`[^"]*`, `.*`, `\w+`) is a loop
and has no such limit; it is groups that recurse. The fix is to keep the
backtracking state on the heap instead of the native stack, which means
compiling the expression and running it on an explicit stack: a rewrite of
the matcher, not a patch.

**Fixed in Phase 37 (finding 42).** `src/js/BuiltinsRegExp.cpp` now compiles
patterns to bytecode instructions (`InstOp`) executed iteratively by `exec_vm()`
over an explicit heap `backtrack_stack`, with iterative AST node destruction so
deeply nested non-capturing groups (`(?:` × 250) and 100,000+ repeated-group
matches succeed without `RangeError`. Patterns nested deeper than
`kMaxCompilerDepth` (1,000) still raise `SyntaxError`, and the 30-million
`kMaxMatchSteps` guard remains.

**The step limit (kept intentionally).** A match is abandoned after 30 million steps with
the same `RangeError`. `/.*y/` on a line of 20,000 characters with no `y`
needs 200 million; it throws after 0.2 s here and answers `false` after 0.46 s
in Node. (Node is quadratic there too: 2 s at 40,000 characters, 39 s at
200,000.) The limit is deliberate, so that one expression cannot hang a page,
but it is lower than what a real engine will sit through.

---
