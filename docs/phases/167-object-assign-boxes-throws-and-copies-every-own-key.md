# Phase 167 — `Object.assign` boxes, throws, and copies every own key

Task: `built-ins/Object/assign`, the one self-contained builtin in the census
that was failing 21 of its 38 cases. Finding 169.

The implementation was six lines, and every line was a different part of
§20.1.2.1 missing:

```cpp
JsValue target = js_arg(args, 0);
if (target.is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
for (size_t i = 1; i < args.size(); ++i) {
    if (args[i].is_nullish()) continue;
    for (const auto& key : reflected_own_keys(args[i], false))
        target.set_property(key, args[i].get_property(key));
}
return target;
```

## 1. Three defects, from three sentences of the spec

| what §20.1.2.1 says | what the code did | what a script saw |
| :--- | :--- | :--- |
| `to = ToObject(target)` | a nullish check, then the primitive itself | `Object.assign("test", {a: 1})` returned `"test"` and **dropped the write**; `r.a` was `undefined` |
| `Set(to, nextKey, propValue, true)` | `set_property`'s `bool` was discarded | a non-writable, frozen, sealed or non-extensible target **silently refused**, and a frozen *accessor* was never called |
| `[[OwnPropertyKeys]]` | `own_keys`, which reports **strings only** | a symbol-keyed property was never copied, and `strings-and-symbol-order.js` saw `[get a, get b]` where the language has the symbols after them |

The middle one is the interesting one, because it is not a bug in `assign` at
all: `set_property` answers `false` when `[[Set]]` refuses, which is exactly
right for sloppy-mode assignment, and every other caller already turns that
`false` into a `TypeError` when the code is strict. `assign` was the one caller
that did not, because the spec's `throw` argument is a `true` that reads like
noise in the signature:

```
Set(to, nextKey, propValue, true)
```

Two of the three are one line each. The third was not.

## 2. `own_keys` never had the symbols, and that is not a bug

`JsValue::own_keys` walks the property table and skips anything
`is_hidden_key` names — `@@…` (symbols and the well-known keys), `%…`
(internal slots), `__x__` (host markers). Symbol keys are stored in the table
under their internal text, so `own_keys` cannot distinguish "a symbol the script
made" from `@@iterator` from `@@toStringTag`, and it answers with none of them.

That is the right answer for every caller it had until now:

``` 
Object.keys({ [s]: 1 })        -> []        for...in   JSON.stringify
spread                         -> 0 symbols
Reflect.ownKeys                -> strings only
```

So the fix is not in `own_keys` — it is a new `assign_keys` beside it, which
takes the strings in `own_keys`' order and then appends the enumerable symbol
keys in creation order, which is the order `[[OwnPropertyKeys]]` specifies
(strings by index, then strings by creation, then symbols by creation). The
symbols are recovered with `js_symbol_from_key`, which already existed for
`Object.getOwnPropertySymbols`.

**This leaves the other copy paths inconsistent, on purpose and visibly.**
Spread (`{...o}`), rest destructuring and `Object.getOwnPropertyDescriptors`
still drop symbol keys, and `Reflect.ownKeys` still reports strings only. Those
are the same omission in three more places and each needs its own decision about
what `own_keys` should mean for it; the note in `AGENTS.md` records them rather
than pretending this phase covered them.

## 3. Order and the missing-key case

`Object.assign` reads the *whole* key list before it reads any value
(`[[OwnPropertyKeys]]`, then `[[GetOwnProperty]]`/`[[Get]]` per key), and a key
that was in the list but is gone by the time the value is asked for is skipped
rather than copied as `undefined`. Nothing in this engine can currently make a
key vanish between the two — that is a `Proxy` trap — but `has_own_property` is
one call and makes the loop correct by construction, so the loop checks.

The order the tests care about is the *read* order, which is what a getter sees:

```
Object.defineProperty(o, s1, {get(){log.push('sym1')}, enumerable: true});
Object.defineProperty(o, 'a',  {get(){log.push('a')},    enumerable: true});
Object.assign({}, o)   ->  ['a', 'sym1']      (Node 22, and now here)
```

## 4. The scoreboard

Two binaries, test262 at `2e58f87d`:

```
built-ins/Object/assign
  before     17 pass (44.7%)  [fail 21]
  now        34 pass (89.5%)  [fail 4]
```

and the whole suite:

```
before     14872 pass (94.8%)  [fail 814]
now        14889 pass (94.9%)  [fail 797]
17 pass in now and not in before; 0 pass in before and not in now
```

The four left are all `ReferenceError: Proxy is not defined` —
`source-own-prop-desc-missing`, `source-own-prop-error`,
`source-own-prop-keys-error` and `strings-and-symbol-order-proxy`. The realistic
ceiling for this directory was 34, not 38, and it is reached.

Everything else unchanged: `make test` **319/319** including the suite added
here.

The engine's answers were checked against Node 22 line by line — a script of
nine target/refusal cases and three key-order cases printed identical output
from both.

## 5. What is still open

- **Symbol keys are dropped by every other copy path**: object spread
  (`JsEval.cpp`, the `evaluate_named`/object-literal path around `:2761`),
  rest destructuring (`:1844`), `Object.getOwnPropertyDescriptors`, and
  `Reflect.ownKeys` (`BuiltinsObject.cpp:957`). Each is `assign_keys` away, but
  each also touches a caller that may have deliberately asked for strings only.
- **`Proxy` — 101 failing cases across test262, five of them in this
  directory.** A `Proxy` is what makes `Object.assign`'s key-vs-value skew (and
  the trap errors that four remaining tests assert) observable at all.
