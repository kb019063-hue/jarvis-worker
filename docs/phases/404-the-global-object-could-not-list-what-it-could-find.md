# Phase 404 — the global object could not list what it could find

*Finding 404.* The global object answered for every library global, one name at
a time, and could not say what it owned. Measured on `426af75`:

```
Object.getOwnPropertyNames(globalThis).length    5       (Node: 129)
Object.keys(globalThis).length                   5       (Node: 15)
'Object' in globalThis                           true
hasOwnProperty(globalThis, 'Object')             true
getOwnPropertyDescriptor(globalThis, 'Object')   {writable: true,
                                                  enumerable: false,
                                                  configurable: true}
delete globalThis.Error                          true    (and Error stayed)
```

The five listed were the names that happened to be *property table* entries.
Every other global was reachable, described correctly, and invisible to
enumeration.

## One name at a time was enough, until it was not

`JsIntrinsics::add_global` keeps the library's globals in a vector beside the
property map, and the global object reaches them through hooks on its cell:

```cpp
std::shared_ptr<std::function<bool(const std::string&, JsValue&)>> lookup_fallback;
std::shared_ptr<std::function<bool(const std::string&, const JsValue&)>> assign_hook;
```

Both take **a name the caller already has** — *read this one*, *write this
one*. Nothing asked *what names are there*, so nothing had to answer, and the
gap was invisible to any script that named what it wanted.

Three operations are that question:

- `Object.getOwnPropertyNames`, `Object.keys`, `Object.assign`, spread,
  `for…in` — `[[OwnPropertyKeys]]`;
- `delete obj.name` — `[[Delete]]`, which on a miss must answer for *this
  object's* ownership, not for the table's;
- `Object.prototype.hasOwnProperty` — which had the same hole.

## The delete that lied

`delete globalThis.Error` answered `true` and `Error` was still a function.

That is worse than a wrong boolean. `[[Delete]]` answering `true` means *the
property is gone*; `false` means *it is still there*. A script that checks —
and checking is the standard's own pattern — was told the operation succeeded.
The name was never in the table the delete looked in, and `return true` at the
bottom of `JsValue::delete_property` is the right answer for an ordinary object
that never had the property. The global object is not one: it owned the name.

## What changed

**`own_keys_hook`** — asked by `JsValue::own_keys`, which `[[OwnPropertyKeys]]`
is built from. It returns the names the object owns outside its table;
`own_keys` appends those the table did not already give, so a name that is in
both is reported once.

**`own_property_hook`** — asked by `[[Delete]]` and `has_own_property` when the
name is not in the table. It answers `false` for a delete of a name the object
owns, which is what makes `delete globalThis.Error` both refuse and report the
refusal.

**`Binding::is_builtin`** marks what the engine declared before any script ran:
the library, the host globals, `undefined`/`NaN`/`Infinity`, and a page's
`document`. `mark_all_builtin()` is called once, at the end of the interpreter's
construction, when exactly that set is in the environment. It keeps the two
lists apart:

| the name | `getOwnPropertyNames` | `Object.keys` | delete |
|---|---|---|---|
| a built-in (`Object`, `console`, `undefined`) | yes | **no** | `false` |
| a script's `var` or function declaration | yes | **yes** | `false` at top level |
| a script's `let`/`const`/`class` | **no** | no | — |

That table is the part that needed care. Without the flag the first version
reported every name in `Object.keys` too — one wrong list replaced by another.
`let`/`const`/`class` stay out through the existing `is_lexical` bit, which the
same hook reads.

## Measured

Two `make all` builds of one tree, differing only in this change:

| | before | after | |
|---|---|---|---|
| `Object.getOwnPropertyNames(globalThis).length` | 5 | **78** | +73 |
| test262, full | 15,177 / 15,686 | **15,179 / 15,686** | +2 |
| `make js-conformance` | 1,152 / 1,158 | 1,152 / 1,158 | — |
| `make dom-conformance` | 678 / 678 | 678 / 678 | — |
| engine suites | 460 | **461** | +1 |

`0 pass in before and not in now`. The 78 is not Node's 129: the difference is
host globals this engine does not have (`process`, `Buffer`, `URL`), not
globals it has and still cannot list.

## What is still open

The `%`-slot half of the row — a script reading and writing `el['%classList']`
— is a different mechanism (names the engine uses as its own storage, filtered
by `is_hidden_key`) and is untouched here. Moving those slots off the property
table is what that half asks for, not this one.
