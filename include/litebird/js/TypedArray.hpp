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

/// True when `value` is an `ArrayBuffer`, a typed-array view, or a `DataView`
/// (even if its backing buffer is detached or the view is out of bounds).
bool is_buffer_source(const JsValue& value);

/// The bytes of an `ArrayBuffer`, any typed-array view, or a `DataView`, copied
/// out: the whole buffer for a buffer, and the view's live window of it for a
/// view. False when `value` is none of these, or when the buffer has been
/// detached or resized out of bounds.
bool typed_array_bytes(const JsValue& value, std::string& out);

/// A fixed-length `ArrayBuffer` holding a copy of `bytes` in the current realm.
JsValue array_buffer_from_bytes(const std::string& bytes);

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

/// What a byte-carrying object is, for structured cloning and the transfer
/// list: the three shapes the standard gives them, and the element type and
/// geometry a typed-array view keeps.
///
/// `kind` is 0 for none, 1 for an `ArrayBuffer`, 2 for a typed-array view and
/// 3 for a `DataView`. `element_type` is the view's index into
/// `%TypedArray%`'s constructors (0 is Int8, 1 Uint8 … 7 Float64), and
/// `byte_offset`/`byte_length`/`length_tracking` are the view's window. A
/// detached buffer answers false: there are no bytes to copy, and the
/// standard's structured clone throws for one.
struct BufferViewInfo {
    int kind{0};
    int element_type{0};
    std::size_t byte_offset{0};
    std::size_t byte_length{0};
    bool length_tracking{false};
};
bool buffer_view_info(const JsValue& value, BufferViewInfo& out);

/// `new <element_type>Array(buffer, byte_offset, length)`, and
/// `new DataView(buffer, byte_offset, byte_length)`, for a buffer that is
/// already an object of this realm. A structured clone uses these so that a
/// view and the views beside it share the one cloned buffer.
JsValue typed_array_view_over(int element_type, const JsValue& buffer,
                              std::size_t byte_offset, std::size_t byte_length,
                              bool length_tracking);
JsValue data_view_over(const JsValue& buffer, std::size_t byte_offset,
                       std::size_t byte_length);

/// `ArrayBufferCopyAndDetach` (ECMA-262 §25.1.3.4) with no length argument: the
/// bytes leave `value` and a fresh buffer holds them, which is what a transfer
/// does. False when `value` is not a buffer or is already detached.
bool array_buffer_transfer(const JsValue& value, JsValue& out);

/// The prototype each typed-array constructor's instances have, by element
/// type. Used where a clone has to name the constructor the bytes belong to.
JsObject* typed_array_prototype_for(int element_type);

} // namespace litebird
