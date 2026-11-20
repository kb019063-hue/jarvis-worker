# Phase 125 — `Object.isFrozen` and `Object.isSealed` ask every property

[`Object.isFrozen` and `Object.isSealed` answer `false` for an object that is
not extensible and has no properties](../../AGENTS.md) was the row. The
finding is [`docs/findings/130`](../findings/130-the-integrity-level-was-a-bit-and-not-a-question.md).

## 1. What was wrong

The engine kept two booleans on every object, `frozen` and `sealed`, that only
`Object.freeze()` and `Object.seal()` ever set. `Object.isFrozen` and
`Object.isSealed` read those bits back:

```cpp
// before
bool is_frozen(const JsValue& object) { ... return cell->frozen; }
```

The language does not define the words that way. ECMA-262 §20.1.2.13
(`Object.isFrozen`) and §20.1.2.15 (`Object.isSealed`) are both one call to
§7.3.15 TestIntegrityLevel(O, level), which is a *question about the present
state* of an object and not about how it got there:

1. If `Type(O)` is not Object, return **true**.
2. If `O.[[IsExtensible]]()` is **true**, return **false**.
3. For each own key `k`: let `desc` be `O.[[GetOwnProperty]](k)`. If `desc`
   is `undefined`, continue. At level `frozen`, if `desc.[[Writable]]` is
   **true**, return **false**. If `desc.[[Configurable]]` is **true**, return
   **false**.
4. Return **true**.

Two distinct things follow from that, and the engine got both wrong:

- An object that was never frozen can still *be* frozen. `Object.seal({})`
  seals an object with nothing in it; nothing in it is writable or
  configurable, and it is not extensible, so it is frozen as well as sealed —
  in the language, `Object.isFrozen(Object.seal({}))` is `true`. The engine
  said `false`, because `seal()` set `sealed` and not `frozen`.
- The same holds for `Object.preventExtensions({})`, which sets neither bit.

The row was found while writing the Phase 124 suite (finding 129), which wrote
the case down in a comment and did not assert it, because the bug was not in
that task's files. This is the task that owns them.

## 2. What changed

One helper in `src/js/BuiltinsObject.cpp`, a transcription of §7.3.15, and both
builtins routed through it:

```cpp
/// ECMA-262 §7.3.15 TestIntegrityLevel(object, level). Both Object.isFrozen
/// (§20.1.2.13) and Object.isSealed (§20.1.2.15) are this question with a
/// different `level`, so neither is a bit an earlier call may have left
/// behind: the answer is asked of the object as it is now.
bool test_integrity_level(const JsValue& object, bool frozen) {
    if (!object.is_object_like()) return true;              // step 1
    const JsObject* cell = object.property_cell();
    const bool extensible = !cell || cell->extensible;       // step 2
    if (extensible) return false;
    for (const std::string& key : reflected_own_keys(object, false)) {
        const JsValue descriptor = descriptor_for(object, key);
        if (descriptor.is_undefined()) continue;
        if (frozen) {
            const JsValue writable = descriptor.get_property("writable");
            // An accessor has no `writable` at all, which reads as
            // undefined and is not a failure.
            if (writable.type == JsType::Boolean && writable.bool_val) return false;
        }
        const JsValue configurable = descriptor.get_property("configurable");
        if (configurable.type == JsType::Boolean && configurable.bool_val) return false;
    }
    return true;
}
```

Two details in it are the spec and not a shortcut:

- **`reflected_own_keys(object, false)`** — the `false` is the
  non-enumerable option. A non-enumerable property is still an own property,
  and §7.3.15 step 3 says "for each own key", not "for each enumerable key". A
  frozen object with only a hidden property is frozen.
- **`frozen` gates only the writable test.** At the sealed level a writable
  property is fine; a configurable one never is. That single asymmetry is the
  whole difference between the two builtins.

`Object.isFrozen` became `return js_boolean(test_integrity_level(object, true))`
and `Object.isSealed` `test_integrity_level(object, false)`. The `frozen` and
`sealed` bits on `JsObject` are untouched: they are what `freeze()` and
`seal()` *did* (and what the property map and the extensibility flag agree
with), and `push`/`defineProperty` still read them. They are no longer an
answer to a question about the present.

Nothing else changed. In particular the descriptors `seal()` and `freeze()`
write were already right, which the probe confirmed before the fix — `seal`
left `writable` true and cleared `configurable`, `freeze` cleared both — so
the levels now read a state that was correct all along.

## 3. How it was verified

**Node.js v22.22.3 first, the engine second, and the same twelve cases.** The
probe below was run under both; the two outputs are identical line for line,
including the two cases the row did not mention.

| object | isFrozen | isSealed | engine before |
|---|---|---|---|
| `preventExtensions({})` | true | true | `false false` |
| `preventExtensions({a:1})` | false | false | `false false` |
| `preventExtensions` + non-writable, non-configurable `a` | true | true | **`false false`** |
| `preventExtensions` + non-writable, configurable `a` | false | false | `false false` |
| `preventExtensions` + **writable**, non-configurable `a` | false | **true** | **`false false`** |
| `preventExtensions` + non-configurable accessor | true | true | **`false false`** |
| `seal({})` | **true** | true | **`false true`** |
| `freeze({})` | true | true | `true true` |
| `{}` (plain) | false | false | `false false` |
| `seal([1,2])` | false | true | `false true` |
| `freeze([1,2])` | true | true | `true true` |
| `seal([1,,3])` | false | true | `false true` |

Four rows were wrong, not one. The row is written about the empty object, and
the empty object is the *easiest* case of the same fault: any non-extensible
object whose properties happen to be non-writable and non-configurable is
frozen, and the engine could only ever answer `true` if someone had called
`freeze()` on it. `preventExtensions` over a complete `defineProperty` was
wrong too, and `preventExtensions` over a writable non-configurable property
is **frozen `false`, sealed `true`** — an answer the old code could not
produce at all, because it read one bit for each question and both bits were
false.

It also cut the other way: `Object.seal({})` was `frozen = false`, which
matters because that is the object a page gets from
`Object.seal(Object.create(null))` or from sealing a lookup table it filled
earlier.

**test262, the two directories, before and after:**

```
python3 tools/conformance/run_test262.py built-ins/Object/isFrozen built-ins/Object/isSealed
  before   85 pass (92.4%)  [fail 7]
  after    88 pass (95.7%)  [fail 4]
```

The whole corpus went from 13,935 pass to 13,938 pass — the same three tests.
The four that still fail are not this row and are named here so that nobody
reads 95.7% as a shortfall of this change:

- `isFrozen/not-a-constructor.js` and `isSealed/not-a-constructor.js` — the
  engine's builtin functions still answer `true` to the "is a constructor"
  question. That is the open construction row, not an integrity level.
- `isFrozen/proxy-no-ownkeys-returned-keys-order.js` and the `isSealed` one —
  `Proxy` is not defined in this engine yet.

**The suite** is `object_integrity_levels_ask_every_property` in
`tests/test_suite.cpp`: eighteen checks, every expected string taken from
Node's output for the same script, covering the table above plus the
descriptor effects of `seal` and `freeze`, a sealed array with a hole, an
extensible object that looks sealed, and the step-1 primitive case
(`Object.isFrozen(1)` is `true`, which surprises people and is the spec). All
282 engine suites pass; JS conformance 1143/1158, DOM 678/678, URL
1188/1188, XML 1718/1718, WPT 9567/9824 — no movement, as expected from a
change to two predicates.

## 4. What is still open

**Arrays: the mutators do not ask `[[IsExtensible]]`.** Found while probing
this row and deliberately *not* fixed here, because it lives in
`src/js/BuiltinsArray.cpp`, which an open pull request owns. On the same
range of objects, Node and this engine disagree seven times out of eight:

| call | Node | this engine |
|---|---|---|
| `Object.seal([1,2]).push(3)` | TypeError | **succeeds, length 3** |
| `Object.preventExtensions([1,2]).push(3)` | TypeError | **succeeds, length 3** |
| `Object.seal([1,2]).unshift(0)` | TypeError | **succeeds** |
| `Object.seal([1,2]).splice(1,0,9)` | TypeError | **succeeds** |
| `Object.freeze([3,1]).sort()` | TypeError | **succeeds** (silently unsorted-back) |
| `Object.seal([1,2]).pop()` | TypeError | **succeeds** |
| `Object.freeze([1,2]).push(3)` | TypeError | TypeError (right, by accident: it tests `frozen`) |

`push` tests `object_val->frozen`, and a sealed or `preventExtensions` array is
not frozen by that flag — the very substitution this phase removed from the
two builtins, in a different file. The fix is the guard this phase's helper
suggests (`[[IsExtensible]]` for a grow, and `delete` of a configurable
`configurable:false` element for `pop`) plus `length` write protection, which
is its own row; it is written up in `AGENTS.md` for the phase that owns the
file.

The two `not-a-constructor` failures above are the construction row, and the
two `Proxy` failures are the `Proxy` row. Neither is new.
