#pragma once

#include "litebird/js/JsInterpreter.hpp"

namespace litebird {

/// `ArrayBuffer` and the typed arrays (`Uint8Array` … `Float64Array`).
///
/// An `ArrayBuffer` is a byte buffer with no way to read it: a script gets at
/// its bytes through a view. A typed array is one such view — a window of
/// `length` elements of one element type on a shared buffer — and every write
/// through it stores the element type's conversion of the value, which is what
/// makes `new Uint8Array(1)[0] = 258` read back `2` and `new Int8Array(1)[0] =
/// 200` read back `-56`.
///
/// The bytes live in C++ (`TypedArrayData`), not in the property table, so the
/// object carries a `JsExotic` that answers the indexed reads and writes, the
/// index keys, `in`, `delete` and the key order: see `JsObject::exotic`, which
/// exists for exactly this (`a NodeList also answers list[3]`). Nothing in
/// `JsValue.cpp` or `Heap.cpp` changes for it, which is why this work could be
/// done beside the pull requests that hold those two files.
///
/// Installs `ArrayBuffer`, every typed-array constructor, `%TypedArray%` (as
/// `Object.getPrototypeOf(Uint8Array)`) and `%TypedArray%.prototype` into the
/// realm's global environment.
void install_typed_array_builtins(JsEnvironment& global);

} // namespace litebird
