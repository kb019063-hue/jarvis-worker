# Phase 128 — a class body knew three early errors out of nine

Task row: **the regular grammar rules over a class body**, taken from what
test262 said rather than from the table: 1,751 of its cases failed, 500 of
them with `expected SyntaxError; exit 1 Uncaught Test262: This statement
should not be evaluated`, and 364 of those 500 were class bodies. Finding 133.

## 1. Where the number came from

`make test262` runs twenty-one directories of the language's own suite and
printed **13,935 / 15,686 (88.8%)**. Grouping the failures by cause rather than
by file is what made this a task instead of a backlog:

| cause of failure | cases |
| :--- | :--- |
| an early error that was never checked (`$DONOTEVALUATE` ran) | 500 |
| `Proxy is not defined` | 96 |
| a runtime `TypeError` that was not thrown | 151 |
| `$262` (the host harness object) is not defined | 53 |
| everything else | 951 |

500 of 1,751 is 28%, and they were not 500 separate bugs:

| directory | early-error failures |
| :--- | :--- |
| `language/statements/class` | 190 |
| `language/expressions/class` | 174 |
| `language/expressions/object` | 60 |
| `language/statements/function` | 38 |
| `language/expressions/function` | 26 |
| `language/expressions/assignment` | 10 |
| `language/expressions/delete` | 2 |

The class half decomposes into five regular rules of §15.7 that the parser
never asked about, plus one that follows from them. That is what this phase
took; the rest of the table is untouched.

## 2. What was wrong

`parse_class` handled three early errors — a second constructor, a duplicate
private name, and `#constructor` — and then parsed whatever else the body
offered. Every case below was **accepted by this engine and rejected by
Node.js 22.22.3**, which is the oracle used:

| source | Node.js | this engine, before |
| :--- | :--- | :--- |
| `class C extends Object { m() { super(); } }` | SyntaxError | accepted, threw at runtime |
| `class C extends Object { x = super(); }` | SyntaxError | accepted |
| `class C extends Object { static { super(); } }` | SyntaxError | accepted |
| `class C { get constructor() {} }` | SyntaxError | accepted |
| `class C { async constructor() {} }` | SyntaxError | accepted |
| `class C { constructor; }` | SyntaxError | accepted |
| `class C { static prototype = 1; }` | SyntaxError | accepted |
| `class C { static get prototype() {} }` | SyntaxError | accepted |

None of these was a missing runtime check. Each is `negative: phase: parse` in
the suite, so the only way to pass is to refuse the source while parsing it.

## 3. The six rules, and the shape they took

**`super()` is legal in exactly one place.** `allow_super_call` was already on
`FunctionContext` and already set for a derived constructor, so the rule was
already expressible — it was simply never asked. `SuperExpr` now refuses a call
when `allow_super_call` is false:

```cpp
if (is_call && !allows_call) fail("'super' keyword unexpected here");
```

This covers three of the eight cases above with one line: a method's body is a
`FunctionBody`, and Contains SuperCall is **false** for a FunctionBody, so
`super()` in a method is not a runtime error but something the grammar does not
produce. A field initialiser and a static block are the opposite — they *do*
contain their own `super()` — and they are already marked `in_class_field`, so
the same refusal reaches them.

**`super.x` is a different question and stays allowed.** A field initialiser
and a static block have a home object. Checking this against Node took one
probe, and it is why the phase does not simply forbid `super` in both: the
first draft did, and it broke `class C extends Object { x = super.toString; }`.

**The two name rules are static semantics, not spellings.** They are checked on
the element as parsed, after `parse_property_name` has resolved the key:

```cpp
const bool named = !member.computed_key && !member.is_private;
if (named && member.is_static && member.key == "prototype") fail(...);
if (named && !member.is_static && member.key == "constructor" && !check(OpenParen)) fail(...);
```

`named` is what makes `static ["prototype"] = 1` and `static #prototype = 1`
legal, which they are: the rule is on the `PropName` the source *spells*, and a
computed name is not spelled. `check(OpenParen)` is what separates a field from
a method of the same name — `constructor;` is a FieldDefinition and is refused,
`constructor() {}` is the constructor.

**The constructor-name rule needed `is_constructor` to lose two terms.** A
MethodDefinition named `constructor` had been "the constructor", full stop.
A getter, a setter, an async method and a generator are none of them, and the
static-semantics rule rejects all four on the instance side while allowing all
four as statics and as computed names:

```cpp
const bool is_constructor = member.kind == ClassMethod && !member.is_static &&
                            !member.computed_key && member.key == "constructor" &&
                            !is_async && !is_generator;
```

## 4. The bug this phase introduced, and how it was caught

The first cut of the constructor rule was written as

```cpp
if (named && !member.is_static && !is_constructor) fail(...);   // WRONG
```

— the `member.key == "constructor"` test had been dropped while widening
`is_constructor`. Every **instance method of every class** became a syntax
error. `make test` caught it in seconds (282 suites, one failing), and so did
a 30-case probe run against Node.

It is worth recording why the probe caught it and the test262 comparison alone
would not have: the change is a *narrowing*, and a widening bug inside a
narrowing change makes the pass count go **up**. The suite added here asserts
the legal half — `class C { static get constructor() {} }` parses, `class C
{ m() {} }` parses — so the next cut cannot repeat it quietly.

## 5. The scoreboard

Measured with two binaries from the same tree, `before` at the phase's base
commit and `now` after it, on test262 at `2e58f87d`:

```
before     13935 pass (88.8%)  [fail 1751]
now        14108 pass (89.9%)  [fail 1578]
173 pass in now and not in before; 0 pass in before and not in now
```

The suite's whole point is the second half of that line: a parser change can
easily gain a hundred cases and lose three. The two class directories alone
went 7,836 → 7,989 (+153) with 0 lost.

Everything else is unchanged, which is the result this phase wanted:

- `make test` → **283 / 283** suites, including the one added here.
- `make js-conformance` → **1143 / 1158 (98.7%)**, `make dom-conformance` →
  **678 / 678**, everyday probes 90/90 and 68/68.

## 6. What is still open

- **The other 136 early-error failures** are the rest of §15.7 and §15.8, not
  this phase's five rules: `language/expressions/object` (60), the two
  function directories (64), assignment and delete (12). The object-literal
  method ones are the same Contains-SuperCall family.
- **`Proxy` (96 cases)** and **`$262` (53)** are whole features, not repairs.
  `$262` is the suite's own host object — `detachArrayBuffer`, `createRealm`,
  `gc` — and is a harness question rather than a language one.
- **`class/dstr`** (144 failures) is destructuring in *parameter* position, a
  parser area of its own, and was left entirely alone here.
- **`super.x` on a class with no heritage** is a runtime `ReferenceError` in
  Node, and this engine now parses it the same way; that is not asserted in
  the suite yet.
