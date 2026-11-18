# Phase 88 — A private name is an identity, not a property called `#p`

`class C { #x = 1; read() { return this.#x; } }` parsed before this phase, and
ran. It just was not private. The lexer has produced a `PrivateName` token with
the `#` in its text since Phase 43, and the parser then did what it does with
every other property name: it stored `"#x"` in the `MemberExpr` and handed it to
the ordinary property lookup. So:

- `c['#x']` read the field, and `c['#x'] = 2` wrote it;
- `'#x' in c` was `true`;
- `o['#x'] = 99; C.prototype.read.call(o)` returned `99`, so one class could
  read and write another class's field;
- `o.#x` on an object without the field was `undefined` and assigning to it
  added one, where both are a `TypeError`;
- `#x in o` did not parse at all, and neither did `#x` written anywhere a
  property name is not expected.

A private name is not a property. It is an identity that a class *makes* when it
is evaluated, which is why `#x` in two classes is two names, why one evaluation
of a class expression cannot read another evaluation's fields, and why the only
way to ask about it is `#x in o`, which cannot be spelled with a string.

## The model

An object's private members live in a table of their own, not in its property
map:

```cpp
class JsObject {
    PropertyMap properties;                     // what script can reach
    std::unique_ptr<PropertyMap> private_slots; // what only the class can
};
```

`private_slots` is null for nearly every object: it is made on the first private
write, so an object that has no private members costs one pointer. Nothing
string-keyed reaches it — `get_property`, `set_property`, `has_property`,
`delete_property`, `define_property`, `own_keys` and the `for`…`in` walk never
look at it — so `c['#x']` is an ordinary absent property rather than a weaker
route to the same slot.

The slots are keyed by what the class minted, not by what the source says:

```cpp
std::string prefix = "\x01" + std::to_string(++m_next_private_id) + ":";
class_env->define(member.key, JsValue::make_string(prefix + member.key));
```

`m_next_private_id` belongs to the interpreter, so every evaluation of a class
gets its own prefix, and the names it declares are *bindings in the class
scope* — `#x` → `"\x011:#x"`. A method's body is a closure over that scope, so
`this.#x` resolves the name exactly the way `x` resolves any lexical name: out
through the class it was written in. That one decision gives the rest for free:

- a nested class written inside a method can read the outer class's field;
- a subclass's method cannot read the parent's `#x`, because the name is not in
  scope there, and the parser says so before anything runs;
- two evaluations of one class expression make two prefixes, so a method of the
  second cannot read the first's instances;
- a direct eval written in the class body *can* name the field (`v =
  eval("this.#m()")`), because the names are in the scope the eval inherits.

A `Reference` gained `is_private` and the resolved key, so `this.#x = 1`,
`this.#x++`, `this.#x += 1` and `[this.#a, this.#b] = [1, 2]` all go through one
`set_private` with the brand check, and the plain read through `get_private`.

## The brand

Private methods, getters and setters are installed on every instance when it is
made, before its fields, from a table the class leaves on its constructor
(`%privates`): a method as a non-writable slot, an accessor as a getter/setter
pair sharing one slot. They are not properties of the prototype, which is what
the specification asks for and what three behaviours depend on:

- `#m in o` is `true` for an instance of the class and of its subclasses, and
  `false` for `Object.create(C.prototype)`;
- `C.prototype.#m()` is a `TypeError`, because the prototype was never
  constructed;
- a static private field belongs to the constructor that declared it:
  `class B extends A {}` has no `A`'s `#s`, and `A.get(B)` is a `TypeError`
  while `A.get(A)` is not.

Reading a private member of an object without the brand is a `TypeError`, not
`undefined`; writing one is a `TypeError`, not the creation of a slot; `#p in 1`
is a `TypeError` (a private name is not a property, so `in` is not
`HasProperty`); a getter without a setter refuses the write, a setter without a
getter refuses the read, and a method refuses both.

Private fields are also not affected by `Object.freeze`: they are not
properties, so `Object.freeze(this); this.#x = 2` works, which is what
test262 asks.

## The syntax

The parser keeps a stack of the private names of every class body it is inside
and checks each use against it:

- a name used in a member access or in `#p in o` that no enclosing class
  declares is a `SyntaxError` — but only once the enclosing class body has been
  read to its closing brace, because a method may read a field written below it;
- `#p` written anywhere else is a `SyntaxError`: as a primary expression, as an
  object-literal key, after `super.`, and in `delete obj.#p`;
- one class may declare a name once. A getter and a setter may share it, on the
  same side of `static`, and nothing else may: `class A { #x; #x }`,
  `class A { static #x; #x }` and `class A { static get #x(){} set #x(v){} }` are
  all `SyntaxError`s;
- `#constructor` is a `SyntaxError`.

`#p in o` is parsed where the `in` operator is, as a `PrivateInExpr`: the right
operand is a shift expression, so `#a in #b in c` is a `SyntaxError` rather
than a nested brand test.

The last trace of the old model went with it. While a private field *was* a
property called `#p`, every `#`-prefixed key had to be hidden from enumeration
for the field to stay private, so a script could not name a property `#p`
either: `var o = {}; o['#p'] = 1` gave `Object.keys(o)` of `[]`,
`getOwnPropertyNames(o).length` of 0 and `JSON.stringify(o)` of `{}`, where
Node gives `['#p']`, 1 and `{"#p":1}`. Members are in their own table now, so
`is_hidden_key` stops treating `#` as a prefix of its own and the property is
ordinary again. The `%` prefix stays hidden, which is a row of its own below.

## Verification

`make test` — one new suite, `private_names_are_identities_and_not_strings`,
which asserts all of the above (the field is not a property, the brand, the
cross-class and cross-evaluation isolation, the accessor directions, and eleven
syntax errors). 236 suites in all, and the direct-eval suite #103 added is
one of them.

`make test262` — the pinned suite over its 21 default directories, against a
build of the commit this phase started from:

```
python3 tools/conformance/run_test262.py \
    --binary before=<build of the parent commit> --binary now=build/litebird
before  12657 pass (80.7%)
now     13630 pass (86.9%)
973 pass in now and not in before; 0 pass in before and not in now
```

The three directories the private-name tests live in, on their own:
`language/statements/class`, `language/expressions/class` and
`language/expressions/in` go from **6,832 / 8,551 (79.9%)** to **7,776 / 8,551
(90.9%)** — 908 tests, none lost.

Node.js was the oracle for the behaviour, on a 21-case battery (inherited
fields, shadowing, accessor order, the four refusal directions, static brands,
enumeration, arrows, nested classes, `++`/compound assignment, destructuring,
freeze, initialiser order, static blocks): the two engines print the same
output, byte for byte.

`make js-conformance` stays at 1128/1152.

### The dispatcher's frame

The private-name work made `JsInterpreter::evaluate` — the frame that every
nested expression pays for, several times per call — 96 bytes larger, and that
alone cost the engine 7% of its native recursion depth. Measured with
`function d(n) { return n === 0 ? 0 : 1 + d(n - 1) }`, the deepest `d(2000)`
that fits:

```
before this branch (main)          2,089 frames
this branch as merged              1,931 frames   -> `d(2000)` is a RangeError
this branch with the split below   2,181 frames
```

`d(2000)` passing is an expectation in `tests/js-conformance/functions.dat`, so
the branch that made it fail would have dropped the corpus one case. The fix is
to keep the dispatcher small: the branches of `evaluate` that hold a large
local — a `Reference`, a vector, a `std::string` — moved out of line
(`eval_update`, `eval_member`, `eval_index`, `eval_private_in`,
`eval_sequence`, `eval_array_literal`, `eval_template_literal`,
`eval_super_member`), which brings the frame from 528 bytes to 384, below the
432 it was before this phase. A rarely-taken branch can no longer make every
expression on the stack deeper, which is what made the depth a side effect of
where the code was placed.

## What is still open

- **Indirect `eval` written in a class body.** `super()`, `super.x`,
  `arguments` and `new.target` inside eval text in a field initialiser must be
  a `SyntaxError` before anything runs. Phase 80 (merged as #103 while this was
  in review) gave *direct* eval the caller's grammar permissions, which is the
  half a class body can seed; 93 of the 775 failures left in the three class
  directories are the eval-shaped remainder — the `indirect-eval-*` variants,
  and `arguments`/`new.target` inside arrows in a field initialiser, which are
  early errors of the arrow body and not of the eval.
- **A private name in a class evaluated twice through `eval`**, and private
  names inside `Function` constructor bodies: both follow from the same
  seeding of the parser that a direct eval already does; test262 does not ask.
- **Decorators** are not parsed at all, so the two `decorator/*private*` cases
  stay failing.
- **`Proxy`** does not exist, so the three `private-*-proxy-default-handler`
  tests stay failing.
- The engine's own `%` slots are still reachable as string keys
  (`el['%classList'] = 5`), which is the same shape of hole one layer down. It
  is a row of its own: private members are now unreachable that way, and the
  `%` slots are not.
