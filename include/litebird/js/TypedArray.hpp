#pragma once

#include "litebird/js/JsInterpreter.hpp"

#include <cstdint>
#include <string>

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

/// `TextDecoder` and `TextEncoder` (`TextCodec.cpp`). Installed with the typed
/// arrays because they are the same subject: `encode()` answers a `Uint8Array`
/// and `decode()` takes a view, so the two cannot exist apart.
void install_text_codec_builtins(JsEnvironment& global);

/// The bytes of an `ArrayBuffer` or of any typed-array view of one, copied out:
/// the whole buffer for a buffer, and the view's window of it for a view. A
/// `DataView` is not one of these types yet. False when `value` is neither, or
/// when the buffer has been detached.
bool typed_array_bytes(const JsValue& value, std::string& out);

/// A `Uint8Array` over a copy of `bytes`: the object `new Uint8Array([...])`
/// makes. This is what `TextEncoder.prototype.encode` answers with, and what a
/// future `Response` body and `ImageData` want, so it lives beside the
/// constructor it belongs to rather than in each caller.
JsValue typed_array_from_bytes(const std::string& bytes);

/// Writes `count` raw bytes into a typed-array view at its start, which is what
/// `TextEncoder.prototype.encodeInto` does with what it encoded. False when the
/// value is not a view, its buffer is detached, or it has no room — the caller
/// has already decided how much fits, so a false answer is a bug or a detached
/// buffer, not a short write.
bool typed_array_write(const JsValue& view, const uint8_t* bytes, std::size_t count);

} // namespace litebird
