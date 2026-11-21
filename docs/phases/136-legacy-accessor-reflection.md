# Phase 136 — `Object.prototype`'s legacy accessor methods

Task row: **The four `__defineGetter__` relatives do not exist** — the `Reflect`
half was already completed in Phase 124. Finding 141:
[legacy accessors were invisible](../findings/141-legacy-accessors-were-invisible.md).

## 1. What was missing

`Object.prototype.__defineGetter__`, `__defineSetter__`, `__lookupGetter__`, and
`__lookupSetter__` were all `undefined`. They are the legacy accessor methods
specified for web compatibility by ECMA-262 Annex B.3.3. The engine already
had the machinery they need: `define_from_descriptor` validates and applies
accessor descriptors, `descriptor_for` reports them, and an object cell links
to its prototype.

Node.js v22.22.3 gives the four functions these names, arities, and property
attributes:

| Method | `length` | own property on `Object.prototype` |
| :--- | :---: | :--- |
| `__defineGetter__` | 2 | writable, non-enumerable, configurable |
| `__defineSetter__` | 2 | writable, non-enumerable, configurable |
| `__lookupGetter__` | 1 | writable, non-enumerable, configurable |
| `__lookupSetter__` | 1 | writable, non-enumerable, configurable |

## 2. What changed

`src/js/BuiltinsObject.cpp` installs the four methods on `Object.prototype`.

- `__defineGetter__` and `__defineSetter__` first `ToObject` their receiver,
  reject a non-callable second argument, then convert the property key. They
  use `define_from_descriptor` with an enumerable, configurable accessor
  descriptor and return `undefined`. Using the existing descriptor path
  preserves the other half of an accessor when a getter is added to an
  existing setter, or vice versa, and applies the ordinary refusal for a
  non-configurable property.
- `__lookupGetter__` and `__lookupSetter__` convert the receiver and key, then
  inspect each own descriptor from the receiver up its prototype chain. They
  return the requested accessor function at the first own property; if that
  first property is data, they return `undefined` rather than looking past it.

The methods are installed with the regular built-in helper, so their own
`name`/`length` properties and the non-enumerable, writable, configurable
entries on `Object.prototype` match the other native methods.

## 3. Verification

Eleven probes were run in Node.js v22.22.3 first and then as the identical
script in the engine CLI; both reported every expectation passing. The suite
`object_prototype_legacy_accessor_reflection` records them: function metadata,
getter/setter creation and preservation, inherited lookup, stopping at an own
data property, data-to-accessor conversion, return values and descriptor
attributes, key conversion order, non-callable and non-configurable errors,
a null receiver, and inherited setter lookup.

- `make test`: **305 engine suites passed** after the latest `main` sync, including the new suite.
- test262's four directories for these methods: **44/54 pass**. The ten
  remaining tests all fail because the engine has no `Proxy` global; they do
  not reach the methods under test.
- The standalone Node/engine CLI probe produced identical answers.
- `make check-numbers` and `python3 tools/ci/check_docs.py` are run again on the
  final tree after merging `main`, as the claim process requires.

## 4. Scope

This completes the four legacy methods only. It does not add `Proxy`, alter the
ordinary descriptor algorithms, or change any other part of the reflection
API.
