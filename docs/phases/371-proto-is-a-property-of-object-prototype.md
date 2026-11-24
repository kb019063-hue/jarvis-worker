# Phase 371 — `__proto__` is a property of `Object.prototype`

*Finding 371.* Twenty test262 cases read `__proto__` through a reflection API
and were told the property did not exist. They were not wrong about the name
being usable: `({}).__proto__` answered, `o.__proto__ = p` changed the
prototype, and the object-literal form `{ __proto__: p }` set it. Every way of
*asking about* the property said there was none:

```
Object.getOwnPropertyDescriptor(Object.prototype, "__proto__")     undefined
Object.prototype.hasOwnProperty("__proto__")                       false
"__proto__" in Object.prototype                                    false
Object.getOwnPropertyNames(Object.prototype)                       [.. no __proto__ ..]
```

Node v22.22.3 answers, for the same four, a descriptor
`{get: f, set: f, enumerable: false, configurable: true}`, and then `true`,
`true`, and a list with `__proto__` in it.

## Why it was nowhere, and why it still worked

Annex B.2.2.1 puts `__proto__` on `Object.prototype` as an accessor. This
engine did not. It synthesised the name instead, in two places, before any
property table was consulted:

- `JsValue::get_property` returned the receiver's `[[Prototype]]` (or `null`)
  for the name `__proto__`, for every object, ahead of own properties, the
  prototype chain, and everything else;
- `JsValue::set_property` treated an assignment to `__proto__` as a prototype
  change, ahead of the same walk.

That is why the *behaviour* was right and the *property* was not: the two
APIs a script uses to ask what a property is — `[[GetOwnProperty]]` and the
key enumeration — go to the property table, and the table had nothing in it.
The symptom the suite reported was not about `__proto__` at all:
`propertyHelper.js` reads the descriptor first and then `.enumerable` off it,
so a missing descriptor surfaced as *Cannot read properties of undefined*, and
the case that failed named a `verifyProperty` expectation rather than the
missing property.

## What changed

Three things, and the third is the one that matters.

**The accessor exists.** `install_object_builtins` installs a real
`{get, set}` pair on `Object.prototype`, enumerable: false, configurable: true.
The getter answers the receiver's `[[Prototype]]` through
`prototype_object()` and returns `undefined` for a primitive receiver after
`RequireObjectCoercible`; the setter runs `RequireObjectCoercible`, ignores
anything that is not an object or `null`, and changes `[[Prototype]]` through
`js_set_prototype`, throwing the same `TypeError` for a cycle or a
non-extensible object that the synthesis threw.

**The synthesis stands aside.** Both fast paths now run only while they still
stand for the property they were a stand-in for:

- a read synthesises only when the receiver has no own `__proto__` entry,
  `Object.prototype`'s accessor is still the one that was installed, and the
  receiver's chain reaches `Object.prototype` at all — that last clause is what
  makes `o.__proto__ = null; o.__proto__` answer `undefined` and not `null`,
  as Node does, because the accessor went with the prototype;
- an assignment synthesises under the same three conditions, so a setter a
  script defined — own to the object, or replacing the one on
  `Object.prototype` — is the one that runs.

Without that guard the change would have been a regression dressed as a fix:
`Object.defineProperty(o, "__proto__", …)` would install a property that no
read could reach, and `Object.defineProperty(Object.prototype, "__proto__",
{ get() { return "replaced" } })` would appear to work while reads answered
the internal slot.

**The name is not hidden.** `is_hidden_key` treats `__name__` as engine
bookkeeping — `__get_x__`, `__set_x__` and the rest — and filters those out of
`own_keys`. `__proto__` matched the pattern, so even after it was installed
`Object.getOwnPropertyNames(Object.prototype)` left it out while
`getOwnPropertyDescriptor` answered for it. The rule now exempts that one
name. (The wider divergence — Node does *not* hide a script's `__secret__`,
and this engine does — is older than this phase and is untouched by it.)

## Measured

Two builds of the same tree, `make all`, differing only in this change:

| | before | after | |
|---|---|---|---|
| `built-ins/Object/prototype/__proto__` | 3 / 15 | **12 / 15** | +9 |
| `language/expressions/object` + `destructuring` | 1,157 / 1,177 | **1,161 / 1,177** | +4 |
| test262, full suite | 15,143 / 15,686 | **15,156 / 15,686** | **+13** |
| `make js-conformance` | 1,146 / 1,158 | **1,150 / 1,158** | +4 |
| `make dom-conformance` | 678 / 678 | 678 / 678 | — |
| engine suites | 430 | **431** | +1 |

Nothing that passed before fails now: the differential runner reports
`0 pass in before and not in now` for each of the three runs.

The three cases still failing in the directory are `get-abrupt`, `set-abrupt`
and `set-cycle-shadowed`, and all three need `Proxy` — an abrupt
`[[GetPrototypeOf]]`, an abrupt `[[SetPrototypeOf]]`, and a cycle that is only
*not* detected because a `Proxy` sits in the chain. The engine has no `Proxy`,
so they are a different row, not a residue of this one.

## What is still open

The rest of the row this came from — `defineProperty` and `defineProperties`,
190 cases whose symptoms are *"0 descriptor should not be writable"* and
*"`arr.hasOwnProperty('0')` !== true"* — is **not** addressed here and its
mechanism is still unverified. Phase 199's own text says so, including that the
obvious explanation (an array index backed by no property entry) was
contradicted by the source. It remains open.
