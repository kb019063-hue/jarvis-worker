# Phase 159 — a definition names the function it makes

Task: the `name` half of the census, which Phase 158's message-first grouping
had left behind — `verifyProperty(f, "name", …)` failing on a value the
language specifies exactly. Finding 164.

## 1. The cluster

Sixteen test262 cases, all of them the same assertion:

```
Uncaught Error: name descriptor value should be [method]; name value should be [method]
Uncaught Error: name descriptor value should be get id; name value should be get id
```

and Node 22 is the oracle, without ambiguity:

| source | this engine, before | Node 22 |
| :--- | :--- | :--- |
| `({ [Symbol("m")]() {} })[Symbol("m")].name` | `""` | `"[m]"` |
| `({ [Symbol()]() {} })[Symbol()].name` | `""` | `""` |
| `Object.getOwnPropertyDescriptor({ get id() {} }, "id").get.name` | `"id"` | `"get id"` |
| `Object.getOwnPropertyDescriptor({ set id(v) {} }, "id").set.name` | `"id"` | `"set id"` |
| `({ [Symbol("t")]: function () {} })[Symbol("t")].name` | `""` | `"[t]"` |
| `(f) = function () {}; f.name` | `"f"` | `""` |

## 2. Why a hint could not reach the function

`make_function` takes a `name_hint` and uses it only when the function node has
no name of its own — and the parser gives every method and accessor its key as
a name already, because that is right by default. So the definition sites were
passing a hint that was silently ignored, which is why the getter changed from
`id` to `get id` in the source and printed `id` anyway.

Two things were missing besides the prefix, and neither is reachable from a
hint at all:

- **the computed key's value.** By the time the site has a key it has
  `js_to_property_key(...)`, and a symbol's string form is its opaque internal
  text (`@@sym:7`). The symbol itself is what carries the description, so the
  evaluated value is now kept beside its string form and handed to the namer.
- **the name of a function written as a *value*** (`{ [sym]: function () {} }`),
  which goes through `evaluate_named` and had no symbol case.

So the fix is a namer — `defined_name` — and an override — `name_function` —
and the four definition sites call it. The parser's own naming stays: it is the
default, and a site that knows better now says so instead of hoping a hint gets
through.

`Symbol.prototype.description` is read through the symbol's own property table,
which is where `make_symbol` keeps it and where `BuiltinsObject.cpp`'s getter
reads it. A symbol with no description contributes nothing at all, so
`[Symbol()]() {}` is named `""` rather than `"[]"`.

## 3. Two things that went wrong, both caught by the scoreboard

**Private names came out mangled.** The class path stores a private member's
key as `\x01<classId>:#m` so it cannot collide with a string property, and I
handed that to the namer. Five suites went from pass to fail with
`Expected SameValue(«"\u00011:#m"», «"#m"»)`. A private name is spelled as it
was written, and that is now stated at the call site rather than being a
property of the key.

**I gated the wrong parenthesis.** `(f) = function () {}` leaves the function
anonymous, because §8.6.2's IsAnonymousFunctionDefinition is about the
*assignment target* and parentheses make that a parenthesised expression rather
than an IdentifierReference. The parser already records `explicitly_grouped`
for exactly this, so I used it — in `evaluate_named`, which also serves
parameter defaults and pattern initialisers. Four `class/dstr` suites had been
passing on the opposite rule: test262 asserts `[cover = (function () {})]` names
the function `cover`. The gate belongs on the assignment path only, so it moved
to `eval_assign`; `evaluate_named` is back to naming whatever it is given. That
is the shape worth remembering: one helper with two callers and two rules.

## 4. The scoreboard

Two binaries, test262 at `2e58f87d`:

```
before     14856 pass (94.7%)  [fail 830]
now        14871 pass (94.8%)  [fail 815]
15 pass in now and not in before; 0 pass in before and not in now
```

Sixteen cases were the target; fifteen moved, and the four `class/dstr` cases
that briefly regressed are back. The sixteenth (`fn-name-lhs-cover`) needed the
grouped-target rule, which is the same phase, and it is one of the fifteen.

Everything else unchanged: `make test` **318/318** including the suite added
here, `make js-conformance` **1144/1158**, `make dom-conformance` **678/678**.

## 5. What is still open

- **`export default function () {}` is named `default`** by §16.2.3.6, and
  modules are not parsed here at all, so there is nothing to name.
- **`Symbol.prototype.description` on an undescribed symbol** answers `""` where
  the language has `undefined`. It is one line in `BuiltinsObject.cpp` and a
  scoreboard of its own (`Symbol/prototype/description/*`); it was used by this
  phase only in the sense that a missing description contributes nothing to a
  name, which is true either way.
