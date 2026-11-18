# Finding 94 — A private name was a property called `#p`

**Severity:** high — a feature the engine claimed took the language's guarantee
away. `#p` was a string key in the property table, so any code could read and
write it with `obj['#p']`, `'#p' in obj` was true, and a second class could read
the first's field through a shared prototype method. A page that used private
fields to keep a secret kept none.

**Found by:** the "A private name is a property whose name begins with `#`" row
of the open-task table in `AGENTS.md`, which the audit had carried since
Phase 43 and which test262 measures in 3,579 tests across the default
directories.

## 1. What was wrong

`JsLexer` produced a `PrivateName` token (Phase 43) and `JsValue::is_hidden_key`
knew the `#` prefix, so a private field was stored as an ordinary property named
`#x` and merely hidden from `Object.keys`. Four things followed:

1. `c['#x']` read the field and `c['#x'] = 2` wrote it — the field was hidden
   from enumeration, not from access.
2. `'#x' in c` was `true`, so the one question a string can ask answered "yes".
3. Two classes that both declared `#x` shared the slot: `class A { #x = 1 }` and
   `class B { #x = 2 }` stored under the same key, so `A.prototype.get.call(new
   B(), a)` read `a`'s field. A private name was a spelling, and every class
   that used the spelling shared it.
4. Reading `o.#x` on an object without the field answered `undefined`, and
   assigning to it created `#x` on that object, where both are a `TypeError`.

`#x in o` did not parse, `#x` used as an expression did not parse, and
`class A { #x; #x }` was accepted — the parse-time half of the guarantee was
missing too.

## 2. What changed

A private name is now an identity made when the class is evaluated. `JsObject`
gained a private table of its own (`private_slots`), unreachable from every
string-keyed path in `JsValue`; each class evaluation mints a serial prefix and
declares its names as bindings in the class scope, so `#x` in a method resolves
through the class it was written in (Phase 88, "The model"). Reading and writing
go through `get_private`/`set_private`, which check the brand and throw a
`TypeError` instead of answering `undefined`. Methods, getters and setters are
installed on each instance as part of the brand; static ones live on the
constructor. The parser keeps the names of every enclosing class body and makes
an undeclared, duplicated, deleted or `super`'d private name a `SyntaxError`,
including the forward reference a method makes to a field written below it.

## 3. How it was verified

```
make test                     # 235 suites: private_names_are_identities_and_not_strings
make test262                  # before 12657 (80.7%) -> now 13586 (86.6%) of 15,686
                              # 929 pass in now and not in before, 0 lost
```

and a 21-case battery run in Node.js 22 and in `build/litebird --run-js`, whose
outputs are identical: no test262 case that passed before this change fails
after it.

## 4. What is still open

- `eval` text in a class body does not raise the early errors for `super`,
  `arguments` and `new.target`; that is the direct-eval row of the task table.
- `Proxy` and decorators do not exist, so their private-name tests still fail.
- The engine's own `%` slots are reachable as string keys, which is the same
  hole one layer down; it is filed as a row of its own.
