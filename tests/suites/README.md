# A suite is a file of its own

A new engine test suite is one new file in this directory, and nothing else:
no line in `tests/test_suite.cpp`, in `main()` or anywhere.

```cpp
// Phase 218, finding 222: what the suite holds the engine to.
TEST(a_sentence_that_says_what_must_be_true) {
    TestPage page;
    JsInterpreter& js = page.js;
    assert(js.eval("1 + 1").to_string() == "2");
}
```

- **The name of the file** is the phase and a few words:
  `218-text-decoration-lines.cpp`. The phase is yours alone
  (`python3 tools/ci/check_pr.py claim N`), so the name is too.
- **One `TEST(name)` in a file**, at the start of a line. Two suites are two
  files. `make test` runs it: `TEST` puts the suite into a list, and `main()`
  runs the suites of this directory first, in the order of their file names,
  and then its own list. The suite count is the files here plus the
  `RUN_TEST` lines of `tests/test_suite.cpp`.
- **No `#include`.** The file is read at the end of `tests/test_suite.cpp`,
  so it sees every header, every helper (`TestPage`, …) and
  `using namespace litebird` of that file. A header no suite has needed
  before is added to the list at the top of `tests/test_suite.cpp`.
- **Helpers are yours.** The file is read inside a namespace named after it,
  so a function or a struct beside the `TEST` cannot collide with one of the
  same name in another file.
- `python3 tools/ci/check_suites.py` says when a file here is not in this
  form, and, since 28 November 2026, when a pull request has written a new
  suite into `tests/test_suite.cpp` instead: CI's "Documentation shape"
  fails on it, and the line says what to move where.

## Why

Until 23 November 2026 every suite was written into `tests/test_suite.cpp`,
in front of `main()`, and got a `RUN_TEST` line at the end of `main()`. Two
pull requests that each do that change the same two places, and the second
cannot be merged after the first. In the 90 pull requests before that day,
their branches merged `main` 285 times; 120 of those merges were in
conflict, and 44 of the 120 in that one file: 22 times at the end of
`main()`, 22 times just in front of it, 18 times further up where two suites
of one subject had been put at the same place. While a watch is on, a pull
request that is in conflict is sent back to its agent, and goes round CI
again.

A file nobody else has is in conflict with nothing.

The suites that were written before stay where they are, in
`tests/test_suite.cpp`, each with its `RUN_TEST` line. Nothing is added to
them: a new assertion for an old suite is still an edit there.

That goes for the 26 suites that were written there after that day as well
(23 to 28 November, twenty of them one agent's, each beside the last). No
check said so until the 28th; they stay, and the next one is a file here.
