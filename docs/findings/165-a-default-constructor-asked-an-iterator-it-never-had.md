# Finding 165 — a default constructor asked an iterator it never had

## What was wrong

A class with no constructor gets one from the parser:

```js
constructor(...args) { super(...args); }
```

Since Phase 63 a spread asks its argument for an iterator, so that synthesised
`super(...args)` called `Array.prototype[Symbol.iterator]`. The language defines
the default constructor as forwarding the arguments list, not as iterating it
(ECMA-262 15.7.14, "does not use argument evaluation" in test262's words), so
the iterator must not be consulted at all. A script that replaced
`Array.prototype[Symbol.iterator]` was called once for each default constructor
on the way up — and a replacement that throws made every such `new` throw.

Measured against Node 22:

```
                          node   litebird (before)
default derived ctor        0            1     <- the bug
default, no extends         0            0
script-written ctor         0            0
ordinary spread             1            1
```

test262's `language/statements/class/subclass/default-constructor-spread-override.js`
failed on `main` with `Uncaught Error: @@iterator invoked`. It passed before
Phase 63, when nothing asked an array.

## What changed

`SpreadExpr` carries a `synthesised` flag, set only where `parse_class` builds
the implicit constructor. `eval_arguments` copies the elements of a marked
spread directly instead of calling `js_iterate`.

The mark is on the node, not derived from the constructor's shape, and that is
the whole point. A fix keyed on shape — "it has a rest parameter and a super
call, so do not iterate" — would also silence a constructor the *script* wrote
with the same text, which must keep asking. The provenance of the spread is
what decides, not what it looks like.

## Verification

`tools/conformance/verify_default_constructor.py` runs nine cases through both
Node and the engine and compares the output. Two deliberate negative controls
were run against it:

- **The unfixed engine**: 3 of 9 cases fail — the three where a default
  constructor must stay silent.
- **A shape-based fix**, written on purpose to make the first case pass: the
  harness reports `FAIL script-written super spread still asks`. That is the
  check that distinguishes a correct fix from a plausible one; without it the
  wrong fix would have gone green.

Wider runs:

- `make test`: 317/317.
- js-conformance: 1145/1158, against 1144/1158 on the same machine with the fix
  stashed — a gain of one case, no loss.
- test262, two binaries, 15686 tests: **14856 → 14857 pass, 0 regressions.**
  The one gain is the case the AGENTS.md row names.

## What this does not cover

Only the spread the parser synthesises is marked. A default constructor that
reaches a class through a path the parser does not build (none is known) would
keep the old behaviour. `super(...args)` written by a script is unchanged, as it
must be.
