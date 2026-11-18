# 18. ✅ A script was cut off at the first unknown character, and could run twice

> Moved out of `AUDIT.md` — written there as a `## 18. ✅ A script was cut off at the first unknown character, and could run twice` section.
> The text below is the section as it stood, unchanged.


Found in Phase 23 by running the engine against an outside corpus
(`tests/js-conformance/`, expected output recorded from Node.js). The engine
passed its own unit tests and 25 of 90 everyday scripts.

- The lexer's default case turned any character it did not recognise into the
  end-of-file token. `?`, `&`, `|`, `^` and `~` were among them, so a script
  using a ternary or a bitwise operator stopped there, without an error, and
  everything after it never ran.
- `eval()` ran the bytecode VM and, on any exception, ran the whole source
  again in the tree-walker. `console.log("side effect"); undefinedFunction()`
  printed twice. Any script that touched the DOM or the network before failing
  did so twice.

Both are gone with the rewrite: the lexer reports what it cannot tokenise as a
`SyntaxError`, a script that does not parse does not run, and there is one
execution path.
