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
// One thing this comment used to say was wrong, and it misled a row of
// AGENTS.md for a hundred phases: it claimed `TextDecoder` had no `stream`
// option. It has had one since Phase 163 -- `read_stream_option` below reads
// it and `DecoderState::stream_bytes` holds the partial bytes -- and a
// three-byte character split across two chunks comes out exactly as Node
// prints it. Phase 276 settled that with a harness rather than an argument.
//
// Two things are deliberately not here, and are rows of AGENTS.md:
//   - the `<meta charset>` prescan is one pass over the first 1,024 bytes, with
//     no streaming across documents;
//   - `TextDecoderStream`/`TextEncoderStream` are absent: they are built on
//     `ReadableStream`/`WritableStream`/`TransformStream`, and this engine has
//     no Streams at all. Half-building them would be worse than their absence;
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

/// The `{ stream }` member of a `decode()` options dictionary, read with
/// ToBoolean. An argument that is not an object is the standard's "empty
/// dictionary", so it means false.
bool read_stream_option(const JsValue& options) {
    if (!options.is_object_like()) return false;
    return options.get_property("stream").is_truthy();
}

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
    /// Bytes held back from a `{stream: true}` decode that ended inside a
    /// character: §11.1.1's *stream* keeps them in the decoder so the next
    /// chunk completes them. Empty between streams, which is how a one-shot
    /// `decode()` stays what it was.
    std::string stream_bytes;
    /// §7.1 "BOM seen": the first scalar value a *decoder* produces is skipped
    /// when it is U+FEFF, and only that one. It is not a byte prefix: the
    /// bytes `FE FF` are a byte order mark under UTF-16BE and the character
    /// U+FFFE under UTF-16LE, and no other encoding has one at all.
    bool bom_seen{false};
    /// §7.2 "do not flush": true while a `{stream: true}` decode is in
    /// progress. It is what decides whether the next `decode()` continues the
    /// decoder it has or takes a new one — a new one has nothing held back, no
    /// ISO-2022-JP mode and no BOM seen.
    bool do_not_flush{false};
    /// The mode an ISO-2022-JP stream ended its last chunk in: 0 ASCII,
    /// 1 Roman, 2 Katakana, 3 double-byte. The only decoder state that
    /// crosses a chunk boundary (§11.1.1), because an escape sequence governs
    /// the bytes after it and those can arrive later.
    int iso_mode{0};
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

// ---------------------------------------------------------------------------
// Streaming, and the bytes a chunk ends inside of
// ---------------------------------------------------------------------------
//
// The decoders in `TextEncoding.cpp` are the standard's one-shot algorithms:
// handed a string that ends halfway through a character, they answer U+FFFD
// (or fail, in fatal mode), because that is what §4's "process an item" says
// at end-of-queue. A *stream* is the same decoder asked to keep the last few
// bytes instead (§11.1.1). So a `{stream: true}` decode splits the chunk at the
// last point where the encoding can still be mid-character, decodes the part
// before it, and holds the rest.
//
// How many trailing bytes an encoding can be inside a character depends on the
// encoding, and the rule for each is the grammar its decoder already follows:
//
//   UTF-8        a lead byte and up to three continuations (1..3 held)
//   UTF-16       one byte of a code unit, or a complete high surrogate (1..3)
//   gb18030      1..3: a lead byte, `lead 0x30-0x39`, or `lead digit lead`
//   Big5         one lead byte (0x81..0xFE)
//   Shift_JIS    one lead byte (0x81..0x9F, 0xE0..0xFC)
//   EUC-JP       one lead byte, or `0x8F` and a lead byte (1..2)
//   EUC-KR       one lead byte (0x81..0xFE)
//   ISO-2022-JP  an escape sequence, or the second byte of a double-byte pair:
//                the decoder's own state machine is an escape, so up to four
//                trailing bytes are looked at (1..4)
//   single-byte  nothing: every byte is a whole character
//
// Anything these rules hold back is a byte no character has yet, so holding it
// cannot change what the decoder would have said about the bytes before it.

/// How many bytes at the end of `bytes` the encoding is still inside a
/// character after — the count a `{stream: true}` decode holds back.
///
/// The walk is forward, not backwards, because a trailing byte alone cannot
/// say what it is: `0xA1` is a lead byte in EUC-JP and a trail byte in EUC-KR,
/// and the same byte can be either in one encoding. So this reads the chunk
/// from its start, taking one whole character at a time exactly as the decoder
/// does, and stops at the first character the chunk does not finish. Bytes
/// already held back from an earlier chunk are always at the front, so the
/// boundary is never in doubt.
std::size_t pending_tail(EncodingId encoding, std::string_view bytes, int iso_mode_in = 0) {
    const std::size_t n = bytes.size();
    auto byte = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
    // ISO-2022-JP is the only one that carries a mode between characters. The
    // mode used to be a local starting at ASCII, which was wrong twice over:
    // a chunk that continues a stream does not start in ASCII, and the held
    // bytes this chunk is prefixed with are the *rest of a character* whose
    // lead byte is in that mode. Both made the scan read a double-byte pair's
    // trailing byte as a byte of its own, so the whole pair was passed to the
    // decoder and decoded in the wrong mode (finding 213).
    enum class IsoMode { Ascii, Double } iso_mode =
        iso_mode_in == 3 ? IsoMode::Double : IsoMode::Ascii;
    std::size_t i = 0;
    while (i < n) {
        std::size_t need = 1;
        switch (encoding) {
        case EncodingId::Utf8: {
            const unsigned char lead = byte(i);
            if (lead >= 0xC2u && lead <= 0xDFu) need = 2;
            else if (lead >= 0xE0u && lead <= 0xEFu) need = 3;
            else if (lead >= 0xF0u && lead <= 0xF4u) need = 4;
            break;
        }
        case EncodingId::Utf16LE:
        case EncodingId::Utf16BE: {
            need = 2;
            if (i + 2 <= n) {
                const unsigned char a = byte(i), b = byte(i + 1);
                const uint32_t unit = encoding == EncodingId::Utf16LE
                    ? (static_cast<uint32_t>(b) << 8 | a)
                    : (static_cast<uint32_t>(a) << 8 | b);
                // A high surrogate is half a character; the low one follows.
                if (unit >= 0xD800u && unit <= 0xDBFFu) need = 4;
            }
            break;
        }
        case EncodingId::Gb18030: {
            const unsigned char lead = byte(i);
            if (lead >= 0x81u && lead <= 0xFEu) {
                need = 2;
                if (i + 1 < n && byte(i + 1) >= 0x30u && byte(i + 1) <= 0x39u) need = 4;
            }
            break;
        }
        case EncodingId::Big5: {
            const unsigned char lead = byte(i);
            if (lead >= 0x81u && lead <= 0xFEu) need = 2;
            break;
        }
        case EncodingId::ShiftJis: {
            const unsigned char lead = byte(i);
            if ((lead >= 0x81u && lead <= 0x9Fu) || (lead >= 0xE0u && lead <= 0xFCu)) need = 2;
            break;
        }
        case EncodingId::EucJp: {
            const unsigned char lead = byte(i);
            if (lead == 0x8Fu) need = 3;
            else if (lead == 0x8Eu) need = 2;
            else if (lead >= 0xA1u && lead <= 0xFEu) need = 2;
            break;
        }
        case EncodingId::EucKr: {
            const unsigned char lead = byte(i);
            if (lead >= 0x81u && lead <= 0xFEu) need = 2;
            break;
        }
        case EncodingId::Iso2022Jp: {
            const unsigned char lead = byte(i);
            if (lead == 0x1Bu) {
                // 1B 24 40, 1B 24 42, 1B 28 42, 1B 28 4A, 1B 28 49 — all
                // three bytes. A shorter one is held until it is complete.
                need = 3;
                if (i + 2 < n && byte(i + 1) == 0x24u && (byte(i + 2) == 0x40u || byte(i + 2) == 0x42u)) {
                    iso_mode = IsoMode::Double;
                } else if (i + 2 < n && byte(i + 1) == 0x28u) {
                    iso_mode = IsoMode::Ascii;
                }
            } else if (iso_mode == IsoMode::Double && lead >= 0x21u && lead <= 0x7Eu) {
                need = 2;
            }
            break;
        }
        default:
            break;     // single-byte encodings and x-user-defined: one byte, one character
        }
        if (i + need > n) return n - i;
        i += need;
    }
    return 0;
}

/// Whether this encoding is one whose decoder can produce a byte order mark:
/// U+FEFF, which §7.1 says is skipped, is the UTF-8 BOM's scalar value and the
/// UTF-16 one's, and nothing else in the standard decodes to it.
bool has_byte_order_mark(EncodingId encoding) {
    return encoding == EncodingId::Utf8 || encoding == EncodingId::Utf16BE ||
           encoding == EncodingId::Utf16LE;
}

/// §7.1 "serialize I/O queue": the BOM is dropped after it is decoded, not
/// before, because which bytes are one depends on the encoding — and it is
/// dropped only from the first scalar value of a decoder, once.
void strip_byte_order_mark(DecoderState& state, std::string& out) {
    if (state.ignore_bom || state.bom_seen || out.empty()) return;
    if (!has_byte_order_mark(state.encoding)) return;
    state.bom_seen = true;
    // U+FEFF in the UTF-8 the decoder answers in.
    if (out.size() >= 3 && out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);
}

/// A `decode()` that does not continue a stream starts from nothing: §7.2's
/// first step builds a new decoder, a new I/O queue and sets "BOM seen" to
/// false whenever "do not flush" was false on entry.
void restart_decoder(DecoderState& state) {
    state.stream_bytes.clear();
    state.bom_seen = false;
    state.iso_mode = 0;
}

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

    // The Encoding Standard's table gives `TextDecoder.prototype.decode`
    // **length 0**: its one argument is optional, and `length` counts the
    // parameters before the first optional one. This said 1, and nothing was
    // checking -- `new TextDecoder().decode.length` answered 1 where Node and
    // the table both say 0.
    js_define_method(proto, "decode", 0, [](Args args) {
        DecoderState* state = decoder_of("decode");
        const bool stream = read_stream_option(args.size() > 1 ? args[1] : JsValue::make_undefined());
        std::string bytes = bytes_from(args.empty() ? JsValue::make_undefined() : args[0]);

        // §7.2 step 1: a decode that does not continue a stream is a new
        // decoder, whatever the last one was in the middle of.
        if (!state->do_not_flush) restart_decoder(*state);
        state->do_not_flush = stream;

        // §11.1.1: the bytes a previous chunk ended inside of are this chunk's
        // beginning. A BOM split across two chunks needs no rule of its own:
        // an incomplete character is held back by `pending_tail` below.
        if (!state->stream_bytes.empty()) {
            bytes = state->stream_bytes + bytes;
            state->stream_bytes.clear();
        }

        // The part the encoding is not inside of, and what is left over.
        std::string head = bytes;
        if (stream) {
            const std::size_t tail = pending_tail(state->encoding, bytes, state->iso_mode);
            head.assign(bytes, 0, bytes.size() - tail);
            state->stream_bytes.assign(bytes, bytes.size() - tail, tail);
        }

        std::string out;
        const bool ok = decode_stream_chunk(head, state->encoding, out, state->fatal, state->iso_mode,
                                            &state->iso_mode);
        if (!ok) {
            // §7.2 step 5: "do not flush" is cleared before the throw, so the
            // next call starts a new decoder rather than continuing a stream
            // that has already failed.
            state->do_not_flush = false;
            restart_decoder(*state);
            js_throw_type_error("Failed to execute 'decode' on 'TextDecoder': The encoded data was not valid.");
        }
        strip_byte_order_mark(*state, out);
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
