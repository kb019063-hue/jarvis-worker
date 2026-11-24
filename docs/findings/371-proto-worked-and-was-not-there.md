# Finding 371 — `__proto__` worked and was not there

The engine answered every question about what `__proto__` *does* correctly and
every question about what it *is* incorrectly. The four ways a script asks
whether a property exists, and the engine's answer before this phase:

```
Object.getOwnPropertyDescriptor(Object.prototype, "__proto__")     undefined
Object.prototype.hasOwnProperty("__proto__")                       false
"__proto__" in Object.prototype                                    false
Object.getOwnPropertyNames(Object.prototype)                       __defineGetter__ … valueOf
```

and yet `({}).__proto__ === Object.prototype` was `true`, and
`o.__proto__ = p; o.marker` found `p`'s properties.

## Two mechanisms for one name

Annex B.2.2.1 specifies `__proto__` as an accessor property of
`Object.prototype`. This engine has no such entry. It special-cases the
*name* in two places instead:

```cpp
// JsValue::get_property, before this phase
if (obj) {
    if (name == "__proto__") {
        if (obj->prototype) return JsValue::make_object(obj->prototype);
        return JsValue::make_null();
    }
    …
```

and the mirror of it in `set_property`, under `sets_prototype`. Both run before
own properties, before the prototype chain, before the exotic hooks. So
`__proto__` was not a property that anything could shadow, replace, describe
or enumerate: it was a rule about a string.

The two views diverge for a reason that is worth naming, because it is the
reason the defect survived so long: a script that *uses* `__proto__` could not
tell. The prototype changed, the inherited properties appeared, the cycle threw
`TypeError`. Only a script that *asks about* the property could tell, and the
one that does — `propertyHelper.js` — asks in an order that hides it:

```js
assert(__hasOwnProperty(obj, name), …);        // a missing property fails HERE
var originalDesc = __getOwnPropertyDescriptor(obj, name);
… originalDesc.enumerable …                    // and then throws on undefined
```

with `name` being something else, on an object whose prototype chain behaviour
the case is testing. The failure read *Cannot read properties of undefined
(reading 'enumerable')* and named neither `__proto__` nor this defect.

## What made it a real fix rather than a swap

Giving the name a descriptor while leaving the two fast paths in place would
have made the reflection APIs agree with a property that still could not be
shadowed:

```js
var o = {};
Object.defineProperty(o, "__proto__", { value: 42, configurable: true });
o.__proto__;                       // 42 is what got defined; this answered Object.prototype
```

so both fast paths had to learn to stand aside, under three conditions:

1. the receiver has no own `__proto__` entry (a script defined one);
2. `Object.prototype`'s accessor is still the installed one (a script replaced
   it, which is legal — it is configurable);
3. the receiver's chain reaches `Object.prototype` at all.

The third is not obvious and is the one Node settles:

```js
var o = {};
o.__proto__ = null;
o.__proto__;                       // undefined, not null
```

The prototype is `null`, so the accessor is no longer *on the chain*, so there
is nothing to call, so the answer is `undefined`. A synthesis that answered the
internal slot would say `null`. That case is in the suite because the first
version of this phase got it wrong.

## The hidden-key rule, found by the test not the probe

Installing the accessor made the descriptor, `hasOwnProperty` and `in` all
correct, and `getOwnPropertyNames` *still* left the name out. The cause was a
second, unrelated filter:

```cpp
bool is_hidden_key(const std::string& key) {
    …
    return key.size() >= 5 && key[0] == '_' && key[1] == '_' &&
           key[key.size() - 1] == '_' && key[key.size() - 2] == '_';
}
```

`__proto__` matches `__name__` exactly. That rule is for the engine's own
bookkeeping (`__get_x__`, `__set_x__`), and the fix is a one-line exemption
rather than a change to the pattern, because the pattern is right for every key
it was written for.

The divergence it leaves behind is real and older than this phase: Node does
not hide a script's `Object.defineProperty(o, "__secret__", …)` either, and
this engine does. That is a separate question with a different answer
(the rule needs "keys the engine made" rather than "names shaped like
`__name__`"), and changing it here would have moved every internal key into
every enumeration. It is recorded, not fixed.

## Counted

Differential, two `make all` builds of one tree:

- `built-ins/Object/prototype/__proto__` — 3/15 → **12/15**
- `language/expressions/object`, `language/expressions/assignment/destructuring`
  — 1,157/1,177 → **1,161/1,177**
- full test262 — 15,143/15,686 → **15,156/15,686**, `+13`, `0` lost
- `make js-conformance` 1,146 → **1,150** of 1,158; `dom-conformance` 678/678
  unchanged; engine suites 430 → **431**

The three remaining failures in the directory (`get-abrupt`, `set-abrupt`,
`set-cycle-shadowed`) all require `Proxy`, which the engine does not have.
