// `TextDecoder` and `TextEncoder` — the WHATWG Encoding Standard's script API.
//
// The two halves underneath have been here for a while: all 40 decoders and the
// 228 labels since Phase 47, and the encoders since Phase 110. What was missing
// was the interface, and it was blocked on typed arrays: `TextEncoder.encode()`
// answers a `Uint8Array`, `encodeInto()` writes into one, and
// `TextDecoder.decode()` takes an `ArrayBufferView`. Phase 106 made those exist,
// so this file is only the interface: labels copied out of the object, a byte
// list read out of a view in `TypedArray.cpp`, and the standard's error modes
// mapped onto the C++ calls that already had them.
//
// Two things are deliberately not here, and are rows of AGENTS.md:
//   - the `<meta charset>` prescan is one pass over the first 1,024 bytes, with
//     no streaming, and `TextDecoder` has no `stream` option either: an option
//     that is accepted and ignored would be worse than one that is absent;
//   - `x-user-defined` and `replacement` are decodable but `replacement` cannot
//     be encoded, and the standard says a `TextDecoder` may not have either as
//     its encoding unless it was reached through a label that maps to it.

#include "litebird/net/TextEncoding.hpp"

#include "litebird/js/Heap.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/js/JsString.hpp"
#include "litebird/js/TypedArray.hpp"

#include <functional>
#include <string>
#include <vector>

namespace litebird {

namespace {

using Args = const std::vector<JsValue>&;

// An Encoding Standard `RangeError` names the label it was given.
[[noreturn]] void throw_bad_label(const std::string& label) {
    js_throw_range_error("The encoding label provided ('" + label + "') is invalid.");
}

/// The encoding a `TextDecoder` constructor argument names, with the two rules
/// the standard adds on top of "get an encoding": an encoding the *decoder* may
/// not use is a RangeError, and `replacement` is one of them because a decoder
/// that answers U+FFFD for every byte is not something a page can ask for by
/// name (`TextDecoder` still gets it through the BOM sniff, which is why the
/// decoder side keeps it).
EncodingId decoder_encoding_from(const JsValue& label) {
    // `new TextDecoder()` and `new TextDecoder(undefined)` both mean utf-8; the
    // standard's `label = "utf-8"` default, applied before ToString.
    const std::string text = label.is_undefined() ? std::string("utf-8") : js_to_string(label);
    EncodingId id = EncodingId::Utf8;
    if (!get_encoding_from_label(text, id)) throw_bad_label(text);
    if (id == EncodingId::Replacement) throw_bad_label(text);
    return id;
}

/// The bytes a `TextDecoder.decode()` argument stands for. The standard takes an
/// `AllowSharedBufferSource`: an `ArrayBuffer`, or any view of one. A plain
/// array is not one, and is a TypeError, which is what browsers answer too.
std::string bytes_from(const JsValue& source) {
    std::string bytes;
    if (source.is_undefined()) return bytes;
    if (typed_array_bytes(source, bytes)) return bytes;
    js_throw_type_error("Failed to execute 'decode' on 'TextDecoder': The provided value is not of type '(ArrayBuffer or ArrayBufferView)'.");
}

/// `fatal` and `ignoreBOM` out of the options dictionary. Both are read with
/// ToBoolean, and an options argument that is not an object is ignored, as the
/// standard's dictionary conversion says.
struct DecoderOptions {
    bool fatal{false};
    bool ignore_bom{false};
};

DecoderOptions read_decoder_options(const JsValue& options) {
    DecoderOptions out;
    if (!options.is_object_like()) return out;
    out.fatal = options.get_property("fatal").is_truthy();
    out.ignore_bom = options.get_property("ignoreBOM").is_truthy();
    return out;
}

// ---------------------------------------------------------------------------
// TextDecoder
// ---------------------------------------------------------------------------

struct DecoderState : JsExotic {
    EncodingId encoding{EncodingId::Utf8};
    bool fatal{false};
    bool ignore_bom{false};
    /// The canonical name, computed once: `encoding` is an attribute of the
    /// object and the standard gives it back as a string.
    std::string name;
};

/// The exotic of an object, or null: the same test `state_of` makes in
/// DomBindings.hpp, written here so this file does not depend on the DOM's.
template <typename T>
T* exotic_of(const JsValue& value) {
    if (value.type != JsType::Object || !value.object_val || !value.object_val->exotic) return nullptr;
    return dynamic_cast<T*>(value.object_val->exotic.get());
}

DecoderState* decoder_of(const char* what) {
    DecoderState* state = exotic_of<DecoderState>(JsValue::current_this());
    if (!state) {
        js_throw_type_error(std::string("Failed to execute '") + what +
                            "' on 'TextDecoder': Illegal invocation");
    }
    return state;
}

JsValue make_decoder(const DecoderState& state, const JsValue* derived);

JsValue construct_decoder(Args args) {
    // Both codecs are [[Construct]]-only: `TextDecoder()` in sloppy code is a
    // TypeError, not a codec with a default encoding.
    if (!JsValue::current_is_construct()) {
        js_throw_type_error("Class constructor TextDecoder cannot be invoked without 'new'");
    }
    DecoderState state;
    state.encoding = decoder_encoding_from(args.empty() ? JsValue::make_undefined() : args[0]);
    const DecoderOptions options = read_decoder_options(args.size() > 1 ? args[1] : JsValue::make_undefined());
    state.fatal = options.fatal;
    state.ignore_bom = options.ignore_bom;
    // `encoding` is the canonical name *lowercased*: the tables say `UTF-8`
    // and every browser answers `utf-8`, which is what a page compares against.
    state.name = std::string(canonical_encoding_name(state.encoding));
    for (char& c : state.name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return make_decoder(state, nullptr);
}

void setup_decoder(JsObject* proto, JsValue& constructor);

JsValue make_decoder(const DecoderState& state, const JsValue* derived) {
    (void)derived;
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = JsIntrinsics::get().text_decoder_proto;
    cell->exotic = std::make_shared<DecoderState>(state);
    return JsValue::make_object(cell);
}

/// What `walk_units` consumed: UTF-16 code units of the source and bytes
/// emitted.
struct WalkResult {
    size_t read = 0;
    size_t written = 0;
};

/// Walk `text` as UTF-16 code units, handing each code point to `emit`, and
/// report how many units were consumed and how many bytes were emitted before
/// the walk stopped. A lone surrogate is U+FFFD, as §8 says: `TextEncoder` does
/// not throw it away and it does not throw, it replaces.
///
/// Both `encode` and `encodeInto` go through here. `encode` passes a sink that
/// never fills, so it consumes the whole string; `encodeInto` passes one that
/// refuses a code point when the destination has no room, which is exactly the
/// point it must stop at — a partial code point is never written.
WalkResult walk_units(const JsString& units, const std::function<bool(const std::string&)>& emit) {
    WalkResult result;
    while (result.read < units.length()) {
        uint32_t code_point = units.unit_at(result.read);
        size_t consumed = 1;
        if (code_point >= 0xD800 && code_point <= 0xDBFF) {
            if (result.read + 1 < units.length()) {
                const uint32_t low = units.unit_at(result.read + 1);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
                    consumed = 2;
                } else {
                    code_point = 0xFFFD;
                }
            } else {
                code_point = 0xFFFD;
            }
        } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
            code_point = 0xFFFD;
        }
        std::string encoded;
        if (!encode_code_point(EncodingId::Utf8, code_point, encoded)) break;
        if (!emit(encoded)) break;
        result.read += consumed;
        result.written += encoded.size();
    }
    return result;
}

void setup_decoder(JsObject* proto, JsValue& constructor) {
    js_define_getter(proto, "encoding", [](Args) {
        return JsValue::make_string(decoder_of("get encoding")->name);
    });
    js_define_getter(proto, "fatal", [](Args) {
        return JsValue::make_boolean(decoder_of("get fatal")->fatal);
    });
    js_define_getter(proto, "ignoreBOM", [](Args) {
        return JsValue::make_boolean(decoder_of("get ignoreBOM")->ignore_bom);
    });

    js_define_method(proto, "decode", 1, [](Args args) {
        DecoderState* state = decoder_of("decode");
        std::string bytes = bytes_from(args.empty() ? JsValue::make_undefined() : args[0]);
        // `ignoreBOM` false (the default) strips one leading BOM, and an
        // encoding the buffer itself names overrides the constructor's, which
        // is the standard's "BOM sniff" step.
        if (!state->ignore_bom) {
            EncodingId bom = EncodingId::Utf8;
            size_t bom_length = 0;
            if (sniff_bom_encoding(bytes, bom, bom_length)) bytes.erase(0, bom_length);
        }
        std::string out;
        const bool ok = decode_with_encoding(bytes, state->encoding, out, state->fatal);
        if (!ok) {
            js_throw_type_error("Failed to execute 'decode' on 'TextDecoder': The encoded data was not valid.");
        }
        return JsValue::make_string(out);
    });

    // `@@toStringTag`, so `Object.prototype.toString.call(d)` says what it is.
    proto->properties.entry("@@toStringTag").second = JsValue::make_string("TextDecoder");
    proto->properties.entry("@@toStringTag").flags = PropertyMap::Configurable;

    constructor.define_property("prototype", JsValue::make_object(proto), 0);
}

// ---------------------------------------------------------------------------
// TextEncoder
// ---------------------------------------------------------------------------

struct EncoderState : JsExotic {};

EncoderState* encoder_of(const char* what) {
    EncoderState* state = exotic_of<EncoderState>(JsValue::current_this());
    if (!state) {
        js_throw_type_error(std::string("Failed to execute '") + what +
                            "' on 'TextEncoder': Illegal invocation");
    }
    return state;
}

JsValue make_encoder(const JsValue* derived) {
    (void)derived;
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = JsIntrinsics::get().text_encoder_proto;
    cell->exotic = std::make_shared<EncoderState>();
    return JsValue::make_object(cell);
}

void setup_encoder(JsObject* proto, JsValue& constructor) {
    js_define_getter(proto, "encoding", [](Args) {
        encoder_of("get encoding");
        return JsValue::make_string("utf-8");
    });

    // `encode(input)`: every string to UTF-8, which never fails. The argument is
    // converted with ToString, so `encode(65)` is the bytes of "65".
    js_define_method(proto, "encode", 0, [](Args args) {
        encoder_of("encode");
        // `input` is optional *with a default*: an explicit `encode(undefined)`
        // is the empty string, not the nine bytes of "undefined". Only a value
        // that is actually present goes through ToString, which is why
        // `encode(null)` is "null".
        const JsValue source = js_arg(args, 0);
        const JsString units = source.is_undefined()
                                   ? JsString()
                                   : (source.type == JsType::String ? source.string_val
                                                                    : JsString(js_to_string(source)));
        std::string bytes;
        (void)walk_units(units, [&bytes](const std::string& piece) {
            bytes += piece;
            return true;
        });
        return typed_array_from_bytes(bytes);
    });

    // `encodeInto(source, destination)`: writes as much as fits and answers
    // `{read, written}`. A code point that would be split by the end of the
    // buffer is not written at all, so a two-byte character in a one-byte gap
    // gives `{0, 0}`.
    js_define_method(proto, "encodeInto", 2, [](Args args) {
        encoder_of("encodeInto");
        // `read` counts UTF-16 code units of the source, which is what
        // `String.prototype.length` counts; `written` counts bytes. A code point
        // that does not fit whole is not written at all, and `read` does not
        // advance past it: that is the case a caller retries with a bigger
        // destination.
        const JsValue source = args.empty() ? JsValue::make_undefined() : args[0];
        const std::string text = js_to_string(source);
        const JsString units = source.type == JsType::String ? source.string_val : JsString(text);
        const JsValue destination = args.size() > 1 ? args[1] : JsValue::make_undefined();
        std::string room;
        if (!typed_array_bytes(destination, room)) {
            js_throw_type_error("Failed to execute 'encodeInto' on 'TextEncoder': The provided value is not of type '(ArrayBuffer or ArrayBufferView)'.");
        }
        // The same walk `encode` uses, with a sink that stops at the first code
        // point that does not fit. Stopping mid-code-point is the trap: the
        // bytes of half a character are never written, and the counts stay at
        // the last complete code point.
        std::vector<uint8_t> out;
        const WalkResult counts = walk_units(units, [&](const std::string& piece) {
            if (out.size() + piece.size() > room.size()) return false;
            out.insert(out.end(), piece.begin(), piece.end());
            return true;
        });
        const size_t read = counts.read;
        const size_t written = counts.written;
        // The bytes go into the destination the caller passed: `encodeInto` is
        // the one place this file writes through a view rather than answering a
        // new one.
        if (written > 0 && !typed_array_write(destination, out.data(), out.size())) {
            js_throw_type_error("Failed to execute 'encodeInto' on 'TextEncoder': The destination is detached.");
        }
        JsValue result = JsValue::make_object(Heap::default_heap().allocate_object());
        result.define_property("read", JsValue::make_number(static_cast<double>(read)), PropertyMap::Default);
        result.define_property("written", JsValue::make_number(static_cast<double>(written)), PropertyMap::Default);
        return result;
    });

    proto->properties.entry("@@toStringTag").second = JsValue::make_string("TextEncoder");
    proto->properties.entry("@@toStringTag").flags = PropertyMap::Configurable;

    constructor.define_property("prototype", JsValue::make_object(proto), 0);
}

} // namespace

void install_text_codec_builtins(JsEnvironment& global) {
    JsIntrinsics& intrinsics = JsIntrinsics::get();

    // TextDecoder
    {
        JsObject* proto = Heap::default_heap().allocate_object();
        proto->prototype = intrinsics.object_proto;
        intrinsics.text_decoder_proto = proto;
        JsValue constructor = js_make_native("TextDecoder", 0, construct_decoder);
        setup_decoder(proto, constructor);
        js_define_hidden(proto, "constructor", constructor);
        global.define("TextDecoder", std::move(constructor));
    }

    // TextEncoder
    {
        JsObject* proto = Heap::default_heap().allocate_object();
        proto->prototype = intrinsics.object_proto;
        intrinsics.text_encoder_proto = proto;
        JsValue constructor = js_make_native("TextEncoder", 0, [](Args) -> JsValue {
            if (!JsValue::current_is_construct()) {
                js_throw_type_error("Class constructor TextEncoder cannot be invoked without 'new'");
            }
            return make_encoder(nullptr);
        });
        setup_encoder(proto, constructor);
        js_define_hidden(proto, "constructor", constructor);
        global.define("TextEncoder", std::move(constructor));
    }
}

} // namespace litebird
