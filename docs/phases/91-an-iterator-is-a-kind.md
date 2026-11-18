# Phase 91 — An iterator is a kind, not an object with a `next`

## 1. What was wrong

`js_make_iterator()` gave every iterator object three things it does not have in
the language: a `next` of its own, `%IteratorPrototype%` as its prototype, and
no name. `%IteratorPrototype%` carried `@@iterator` and nothing else, so
`Object.getPrototypeOf(it).next` was `undefined`, `Object.prototype.toString`
called every iterator `[object Object]`, and the one property a script is
supposed to be able to replace — the `next` of a kind's iterator prototype —
did not exist to replace.

Two more things were wrong in the same place, and one of them is why this
phase exists at all:

- **`Map.prototype[Symbol.iterator]` and `Map.prototype.entries` were two
  different functions.** So were `Set.prototype[Symbol.iterator]` and
  `Set.prototype.values`, and `Set.prototype.keys` and `values`. In the
  language each pair is *one function object*: `Map.prototype[@@iterator]` is
  ``the initial value of Map.prototype.entries``, and `Set`'s `values`, `keys`
  and `@@iterator` are one function named `values`. `Map.prototype.entries` was
  registered under three names by three calls to `js_define_method`, which
  makes three functions that iterate alike instead of one function under three
  keys.
- **A replaced `next` would have been ignored even if it had existed.** The
  iteration fast paths (`js_iterates_as_builtin`, `method_is_installed`,
  `js_iterate`, `js_iterable_to_vector`) read an array's elements and a
  string's code points directly, without making an iterator at all, and the
  comment above `js_iterates_as_builtin` said the direct read was safe
  *because* `next` was the iterator's own property. Move `next` to a prototype
  and that reasoning stops holding: a loop over `[1, 2, 3]` must call the
  prototype's `next` if a script replaced it, and the fast path never calls
  anything.

The JavaScript corpus already asked all of this.
`iteration.dat: Array.prototype[Symbol.iterator] is Array.prototype.values, and
the others likewise` and `iteration.dat: array iterators take next from one
prototype` were two of the eleven failing cases of that file.

## 2. What changed

### The kinds have prototypes (`JsOps.hpp`, `JsOps.cpp`, `JsBuiltins.hpp`)

`JsIntrinsics` now allocates six prototypes between `%IteratorPrototype%` and
the iterators — `%ArrayIteratorPrototype%`, `%MapIteratorPrototype%`,
`%SetIteratorPrototype%`, `%StringIteratorPrototype%`,
`%RegExpStringIteratorPrototype%` and one for the Web IDL iterators
(`FormData`, `URLSearchParams`) — and holds two of the installed `next`
functions for the questions below.

`js_install_iterator_prototype(proto, tag)` writes the prototype's `next` — one
function per kind, which reads the step out of `this` — and a
`Symbol.toStringTag` of `"Array Iterator"`, `"Map Iterator"`, `"Set Iterator"`,
`"String Iterator"` or `"RegExp String Iterator"` (`{ writable: false }`,
configurable, like the spec's). `%IteratorPrototype%` keeps only `@@iterator`,
as the language has it.

`js_make_iterator(step, roots, proto)` now puts the iterator's state — the step
and whether it has finished — in the cell's `internal`, which is where a
`Date`'s time value and a `Map`'s storage live, and gives the object the
prototype of its kind. It has no own properties at all, so
`Object.getOwnPropertyNames(it)` is `[]`.

The six call sites pass a kind: arrays (`BuiltinsArray.cpp`), `Map` and `Set`
(`BuiltinsCollections.cpp`), strings (`BuiltinsString.cpp`), `String.prototype.
matchAll` (`BuiltinsRegExp.cpp`), and `FormData`/`URLSearchParams`
(`DomFormBindings.cpp`, `DomUrlBindings.cpp`).

### One function under each pair of names (`BuiltinsCollections.cpp`)

`Map.prototype.entries` and `Map.prototype[@@iterator]` are now one function
object named `entries`; `Set.prototype.values`, `Set.prototype.keys` and
`Set.prototype[@@iterator]` are one function object named `values`. `Array`'s
pair has been one function named `values` since #77, and `String`'s stays a
method of its own named `[Symbol.iterator]`, as the language names it.

### The fast path asks whether `next` is still the one install() wrote (`JsOps.cpp`)

`holds_installed_next(proto, installed)` answers the question the comment used
to answer by assumption. `js_iterates_as_builtin()` and `method_is_installed()`
now require, for an array and for a string, that the kind's prototype still
holds the function `install()` put there; when a script has replaced it, the
loop takes the long way round — `js_iterator_method`, the method, `next` — and
calls the replacement once per step, as the language does. A replaced `next`
for arrays does not affect strings, because the two kinds have different
prototypes, and a test says so.

## 3. How it was verified

- **`make js-conformance`:** **1130 / 1152 (98.1%)**, up from 1128. The two
  gains are exactly the cases above; the 22 that still fail are unchanged and
  named (eight from Phase 34, nine from Phase 63, the rest from later phases).
- **`make test`:** **242 / 242** suites, including the new
  `js_iterators_are_kinds_with_prototypes`, which asserts the identities and
  names, the shape of an iterator (`next` not own, its prototype shared by
  `keys()` and `values()` and different for a `Map`), the four `toString`
  tags, `next` throwing on a wrong receiver, and — the part that made the
  fast-path change necessary — that replacing `%ArrayIteratorPrototype%.next`
  makes `[...[1, 2, 3]]` and `for…of` call it (`calls` is 6: three values and
  the step that says done, twice over) and that restoring it takes the direct
  read back. 114 / 114 cross-origin checks still pass.
- **`make conformance`:** 1967 / 1973, unchanged. **`make wpt`:** 9567 / 9824
  across 190 files, unchanged.
- **`python3 tools/ci/check_numbers.py`:** 0 failures, with the README's suite
  and corpus counts brought up to date.

## 4. What is still open

- **A symbol is a string in this engine.** Property keys are
  `std::string`, and `Symbol.iterator` is the key `"@@iterator"`, so
  `"@@iterator" in []` is `true` and `Object.getOwnPropertyNames(Array.
  prototype).indexOf("Symbol.iterator")` is not `-1`. `iteration.dat: no
  property is named 'Symbol.iterator' or '@@iterator'` still fails, and
  cannot pass until a key can be a symbol. `BuiltinsArray.cpp` registers the
  array's iterator under both spellings for the same reason.
- **Array destructuring and `Array.from` still decide for themselves what is
  iterable** (the rows above this one in `AGENTS.md`): nine cases of
  `iteration.dat` remain, and none of them is about the prototypes.
- **`%IteratorPrototype%` has no helpers** (`map`, `filter`, `take`, `drop`),
  generator objects still carry their own `next` and no `%GeneratorPrototype%`
  of a kind, and `RegExp String Iterator` has a prototype but no `return`.
- **No `@@asyncIterator` prototype chain**: async generators keep what they
  had.
