# 21. ✅ Deeply nested input killed the process

> Moved out of `AUDIT.md` — written there as a `## 21. ✅ Deeply nested input killed the process` section.
> The text below is the section as it stood, unchanged.


Found by generating stress pages rather than by reading code: one script per
page, each nesting one construct 200,000 deep, followed by a second script that
reports whether it ran. 28 of 46 pages ended in `SIGSEGV`. A page could take the
browser down with one long line such as `((((...1...))))`.

The engine recurses over what a script gives it, and only function calls were
guarded (finding 6). Four separate recursions were not:

- **The parser.** Every nested expression, statement, pattern or function body
  is a level of recursive descent.
- **The evaluator**, on nested statements and patterns, and on long operator
  chains: `a+b+c+...` parses into a tree as deep as the chain is long.
- **Library code**: `join` calling `toString` calling `join`, `flat`,
  `structuredClone`, `JSON.stringify` and the JSON parser.
- **Destructors.** Freeing a syntax tree, a chain of closures (each scope owns
  its parent), or a chain of promises ran one destructor inside another, once
  per link. This one needs no deep syntax at all: a loop that builds a linked
  list of scopes is enough, and it crashes when the list is dropped.

Fixes:

- `js_check_stack()` measures the native stack that is left
  (`common/StackLimit.hpp`) and throws a `RangeError` the script can catch. It
  is called at the recursive entry points of the parser, the evaluator and the
  library functions above. Depth counters were not used: how much stack one
  level costs differs by a factor of 50 between an optimised and a sanitizer
  build.
- Operator chains of 8 or more are evaluated in a loop
  (`eval_binary_chain`), so a long `a+b+c+...` is not an error.
- Destruction is iterative. `common/ReleaseQueue.hpp` collects what a destructor
  would otherwise destroy recursively and drains it in a loop; syntax tree nodes
  are deleted through it (`ast_delete`), scopes release their parent chain in a
  loop, and so do promises and function payloads.

After the fix no stress page crashes, in the normal build or under
AddressSanitizer. Deep syntax reports `Uncaught RangeError: Maximum call stack
size exceeded` and the next script on the page runs, which is what V8 does.

**Tests:** `tests/js-conformance/deep-nesting.dat` (63 cases, expectations from
Node) and the unit test `deep_nesting_never_exhausts_the_native_stack`.

Generators and async functions recurse without using the stack: a frame that
resumes another frame waits for it on its own thread. `function* g() { yield*
g(); }` took threads until the system refused one, 15,724 on the test machine,
and under AddressSanitizer ran out of memory first. Frames may now nest 2,000
deep; beyond that the script gets the same `RangeError`.

**Still open.** One stress page does not crash but is killed for memory:
100,000 chained `bind` calls (the name `bound bound ...` grows quadratically;
20,000 take 1.3 s and 1.2 GB). The other one that was, a string built from two
million `+` terms, gave no answer in two minutes; since finding 25 was fixed
it finishes in 3.7 s (Node: 0.5 s), though the syntax tree of that 12 MB
script takes 1 GB. And the DOM is not covered by any of this: see finding 24.
