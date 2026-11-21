# Phase 132 — boxed `String` objects describe their character indices

Task row: **A `String` object's own index properties are not described.**
Finding 137: [boxed strings lost their index descriptors](../findings/137-boxed-strings-lost-their-index-descriptors.md).

## 1. What was wrong

A String exotic object has an own property for each UTF-16 code unit in the
string it wraps. Under ECMA-262 §10.4.3, an index in range is a data property
whose value is that code unit, `[[Writable]]` is false, `[[Enumerable]]` is
true, and `[[Configurable]]` is false. The wrapper's `length` was already
stored and described correctly.

The read path and key list already knew about the indices:

```js
var s = new String('ab');
s[1];                    // 'b'
Object.keys(s);           // ['0', '1']
s.hasOwnProperty('1');    // true
```

But `descriptor_for` only had a special case for the primitive `JsType::String`.
A boxed string is an object with `ObjectClass::String`, whose wrapped primitive
is held in the object's internal slot; its virtual character indices are not
entries in the ordinary property map. The descriptor path therefore returned
`undefined` for an index that the other own-property operations could see.
That also made `propertyIsEnumerable` return false and left
`Object.getOwnPropertyDescriptors` without the index descriptors. The
primitive call `Object.getOwnPropertyDescriptor('ab', '1')` was already right;
`Reflect.getOwnPropertyDescriptor` correctly refuses a primitive target, but
failed to describe the boxed one.

| Probe | Node.js v22.22.3 | Engine before |
| :--- | :--- | :--- |
| `Object.getOwnPropertyDescriptor(new String('ab'), '1')` | `{value: 'b', writable: false, enumerable: true, configurable: false}` | `undefined` |
| `Reflect.getOwnPropertyDescriptor(new String('ab'), '0')` | `{value: 'a', writable: false, enumerable: true, configurable: false}` | `undefined` |
| `new String('ab').propertyIsEnumerable('1')` | `true` | `false` |
| `Object.keys(new String('ab'))` | `['0', '1']` | `['0', '1']` |

## 2. What changed

In `src/js/BuiltinsObject.cpp`, `descriptor_for` now recognizes
`ObjectClass::String` before it searches the ordinary property map. It asks the
wrapper for its wrapped primitive, checks that the key is a canonical index
within the string's UTF-16 length, and returns the value already exposed by
`get_property` with the String exotic attributes: non-writable, enumerable,
and non-configurable.

That one descriptor path is shared by `Object.getOwnPropertyDescriptor`,
`Reflect.getOwnPropertyDescriptor`, `Object.getOwnPropertyDescriptors`, and
`Object.prototype.propertyIsEnumerable`, so all four now describe the same
property. Indices beyond the wrapped string's length still fall through to
ordinary properties on the wrapper; the primitive-string path and the stored
`length` property are unchanged.

## 3. Verification

The expectations were run in Node.js v22.22.3 before being added to
`boxed_string_own_index_descriptors` in `tests/test_suite.cpp`. The suite checks
both descriptor APIs, enumerability, `Object.getOwnPropertyDescriptors`, a
surrogate-pair code unit (`0xD83D` at index `0`), an ordinary index past the
wrapped length, a non-index miss, and the primitive-string descriptor.

The standalone CLI probe was run in Node and the engine; both printed exactly:

```text
["b",false,true,false,"a","0,1,length",true]
```

`make test` passed all **296 engine test suites**, including the new suite;
`make metrics` reported zero number failures, and
`python3 tools/ci/check_docs.py` reported zero documentation failures on this
tree. The same checks are repeated after merging `main`, as the claim process
requires.

## 4. Scope

This closes only the descriptor gap for the virtual index properties that
String exotic objects already read and enumerate. It does not change the
string wrapper's storage, `length`, primitive-string reflection, or the
ordinary-property behavior of indices outside the wrapped text.
