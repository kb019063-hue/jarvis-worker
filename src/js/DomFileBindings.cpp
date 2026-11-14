// Blob, File and FileReader.
//
// A blob here is a run of bytes kept as a string. The engine has no
// ArrayBuffer, so a blob is made from strings and other blobs and read back
// as text or as a data: URL; `arrayBuffer()` and `stream()` do not exist,
// which a page can test for.

#include "litebird/js/DomBindings.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Promise.hpp"

#include <algorithm>
#include <cmath>

namespace litebird {
namespace dom {

namespace {

struct BlobState : JsExotic {
    std::string bytes;
    std::string type;
    // File only.
    bool is_file{false};
    std::string name;
    double last_modified{0};
};

BlobState& this_blob(const char* iface = "Blob") {
    return this_state<BlobState>(iface);
}

// A media type as a blob keeps it: lower case, and nothing at all when it
// has a character outside printable ASCII.
std::string normalized_type(const std::string& type) {
    for (unsigned char c : type) {
        if (c < 0x20 || c > 0x7E) return std::string();
    }
    return ascii_lower(type);
}

// The bytes of the parts given to a constructor: strings as UTF-8, blobs as
// their bytes, anything else as the string it converts to.
std::string bytes_of_parts(const JsValue& parts, const char* what) {
    std::string out;
    if (parts.is_undefined()) return out;
    if (!parts.is_object_like()) {
        js_throw_type_error(std::string("Failed to construct '") + what + "': the parts must be given as a sequence.");
    }
    for (const JsValue& part : js_iterable_to_vector(parts)) {
        if (const BlobState* blob = state_of<BlobState>(part)) out += blob->bytes;
        else out += to_str(part);
    }
    return out;
}

JsValue construct_blob(Realm& realm, const InterfaceDef&, Args args, const JsValue* derived) {
    auto state = std::make_shared<BlobState>();
    state->bytes = bytes_of_parts(js_arg(args, 0), "Blob");
    const JsValue type = member(js_arg(args, 1), "type");
    if (!type.is_undefined()) state->type = normalized_type(to_str(type));
    return realm.make("Blob", state, derived);
}

JsValue construct_file(Realm& realm, const InterfaceDef&, Args args, const JsValue* derived) {
    require_args(args, 2, "File");
    auto state = std::make_shared<BlobState>();
    state->is_file = true;
    state->bytes = bytes_of_parts(args[0], "File");
    state->name = to_str(args[1]);
    const JsValue type = member(js_arg(args, 2), "type");
    if (!type.is_undefined()) state->type = normalized_type(to_str(type));
    const JsValue modified = member(js_arg(args, 2), "lastModified");
    state->last_modified = modified.is_undefined() ? std::floor(js_current_time_ms()) : std::trunc(to_double(modified));
    if (state->last_modified != state->last_modified) state->last_modified = 0;
    return realm.make("File", state, derived);
}

// slice(start, end): negative positions count from the end, as in arrays.
std::size_t clamp_position(const JsValue& value, std::size_t size, std::size_t fallback) {
    if (value.is_undefined()) return fallback;
    double position = to_double(value);
    if (position != position) position = 0;
    position = std::trunc(position);
    if (position < 0) position = std::max(0.0, static_cast<double>(size) + position);
    return static_cast<std::size_t>(std::min(position, static_cast<double>(size)));
}

void setup_blob(Iface& I) {
    I.getter("size", [](Realm&) { return num(static_cast<double>(this_blob().bytes.size())); });
    I.getter("type", [](Realm&) { return str(this_blob().type); });
    I.method("slice", 0, [](Realm& r, Args args) {
        const BlobState& source = this_blob();
        const std::size_t size = source.bytes.size();
        const std::size_t start = clamp_position(js_arg(args, 0), size, 0);
        const std::size_t end = clamp_position(js_arg(args, 1), size, size);
        auto state = std::make_shared<BlobState>();
        if (end > start) state->bytes = source.bytes.substr(start, end - start);
        if (!js_arg(args, 2).is_undefined()) state->type = normalized_type(to_str(args[2]));
        return r.make("Blob", state);
    });
    I.method("text", 0, [](Realm&, Args) { return Promise::make_resolved(str(this_blob().bytes)); });
}

void setup_file(Iface& I) {
    I.getter("name", [](Realm&) { return str(this_blob("File").name); });
    I.getter("lastModified", [](Realm&) { return num(this_blob("File").last_modified); });
    I.getter("webkitRelativePath", [](Realm&) {
        this_blob("File");
        return str("");
    });
}

// --- FileReader -------------------------------------------------------------------

struct ReaderState : JsExotic {
    int ready_state{0};         // EMPTY 0, LOADING 1, DONE 2
    JsValue result = JsValue::make_null();
    JsValue error = JsValue::make_null();
    /// Counts the reads started: a read that was aborted, or replaced, finds
    /// that it is no longer the current one and reports nothing.
    uint64_t generation{0};
    void trace(Heap& heap) override {
        heap.mark_value(result);
        heap.mark_value(error);
    }
};

ReaderState& this_reader() {
    return this_state<ReaderState>("FileReader");
}

JsValue construct_reader(Realm& realm, const InterfaceDef&, Args, const JsValue* derived) {
    return realm.make("FileReader", std::make_shared<ReaderState>(), derived);
}

const char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(const std::string& bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned b0 = static_cast<unsigned char>(bytes[i]);
        const unsigned b1 = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0;
        const unsigned b2 = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0;
        out += kBase64Alphabet[b0 >> 2];
        out += kBase64Alphabet[((b0 & 3) << 4) | (b1 >> 4)];
        out += i + 1 < bytes.size() ? kBase64Alphabet[((b1 & 15) << 2) | (b2 >> 6)] : '=';
        out += i + 2 < bytes.size() ? kBase64Alphabet[b2 & 63] : '=';
    }
    return out;
}

enum class ReadAs { Text, DataUrl, BinaryString };

JsValue progress_event(Realm& realm, const char* type, std::size_t loaded, std::size_t total) {
    return make_event(realm, "ProgressEvent", type, false, false,
                      {{"lengthComputable", boolean(true)},
                       {"loaded", num(static_cast<double>(loaded))},
                       {"total", num(static_cast<double>(total))}});
}

// A read: `loadstart` now, the result and `load`, `loadend` in a later task.
JsValue start_read(Realm& r, Args args, ReadAs how, const char* what) {
    const JsValue self = JsValue::current_this();
    ReaderState& state = this_reader();
    require_args(args, 1, what);
    const BlobState* blob = state_of<BlobState>(args[0]);
    if (!blob) js_throw_type_error(std::string("Failed to execute '") + what + "': parameter 1 is not of type 'Blob'.");
    if (state.ready_state == 1) throw_dom(r, "InvalidStateError", "The FileReader is already reading.");
    state.ready_state = 1;
    state.result = null();
    state.error = null();
    const uint64_t generation = ++state.generation;
    // The bytes as they are now: a blob never changes.
    const std::string bytes = blob->bytes;
    const std::string type = blob->type;
    Realm* realm = &r;
    EventLoop::default_loop().post_task([realm, self, generation, bytes, type, how]() {
        ReaderState* reader = state_of<ReaderState>(self);
        if (!realm->alive || !reader || reader->generation != generation || reader->ready_state != 1) return;
        dispatch(*realm, self, progress_event(*realm, "loadstart", 0, bytes.size()), true);
        if (reader->generation != generation || reader->ready_state != 1) return;       // aborted by a listener
        reader->ready_state = 2;
        switch (how) {
            case ReadAs::Text:
                reader->result = str(bytes);
                break;
            case ReadAs::DataUrl:
                reader->result = str("data:" + (type.empty() ? std::string("application/octet-stream") : type) +
                                     ";base64," + base64(bytes));
                break;
            case ReadAs::BinaryString: {
                // One character per byte.
                std::u16string units;
                units.reserve(bytes.size());
                for (unsigned char c : bytes) units += static_cast<char16_t>(c);
                reader->result = str(js_utf16_to_utf8(units));
                break;
            }
        }
        dispatch(*realm, self, progress_event(*realm, "progress", bytes.size(), bytes.size()), true);
        dispatch(*realm, self, progress_event(*realm, "load", bytes.size(), bytes.size()), true);
        // A load listener may have started the next read; that one ends itself.
        if (reader->generation == generation) {
            dispatch(*realm, self, progress_event(*realm, "loadend", bytes.size(), bytes.size()), true);
        }
    });
    return undefined();
}

void setup_reader(Iface& I) {
    static const std::pair<const char*, int> kStates[] = {{"EMPTY", 0}, {"LOADING", 1}, {"DONE", 2}};
    for (const auto& entry : kStates) I.constant(entry.first, entry.second);
    I.getter("readyState", [](Realm&) { return num(this_reader().ready_state); });
    I.getter("result", [](Realm&) { return this_reader().result; });
    I.getter("error", [](Realm&) { return this_reader().error; });
    I.method("readAsText", 1, [](Realm& r, Args args) { return start_read(r, args, ReadAs::Text, "readAsText"); });
    I.method("readAsDataURL", 1, [](Realm& r, Args args) { return start_read(r, args, ReadAs::DataUrl, "readAsDataURL"); });
    I.method("readAsBinaryString", 1, [](Realm& r, Args args) {
        return start_read(r, args, ReadAs::BinaryString, "readAsBinaryString");
    });
    I.method("abort", 0, [](Realm& r, Args) {
        const JsValue self = JsValue::current_this();
        ReaderState& state = this_reader();
        if (state.ready_state != 1) return undefined();
        state.ready_state = 2;
        state.result = null();
        ++state.generation;
        dispatch(r, self, progress_event(r, "abort", 0, 0), true);
        if (state_of<ReaderState>(self)->ready_state != 1) dispatch(r, self, progress_event(r, "loadend", 0, 0), true);
        return undefined();
    });
    for (const char* type : {"loadstart", "progress", "load", "abort", "error", "loadend"}) {
        const std::string event_type = type;
        const std::string name = "on" + event_type;
        define_accessor_value(I.proto, name.c_str(),
            make_function(I.realm, name.c_str(), 0, [event_type](Realm& r, Args) -> JsValue {
                this_reader();
                return get_event_handler(r, JsValue::current_this(), event_type);
            }),
            make_function(I.realm, name.c_str(), 1, [event_type](Realm& r, Args args) -> JsValue {
                this_reader();
                set_event_handler(r, JsValue::current_this(), event_type, js_arg(args, 0));
                return undefined();
            }));
    }
}

} // namespace

bool is_blob(const JsValue& value) {
    return state_of<BlobState>(value) != nullptr;
}

bool is_file(const JsValue& value) {
    const BlobState* state = state_of<BlobState>(value);
    return state && state->is_file;
}

JsValue make_blob(Realm& realm, const std::string& bytes, const std::string& type) {
    auto state = std::make_shared<BlobState>();
    state->bytes = bytes;
    state->type = normalized_type(type);
    return realm.make("Blob", state);
}

JsValue make_file(Realm& realm, const std::string& bytes, const std::string& name, const std::string& type) {
    auto state = std::make_shared<BlobState>();
    state->is_file = true;
    state->bytes = bytes;
    state->name = name;
    state->type = normalized_type(type);
    state->last_modified = std::floor(js_current_time_ms());
    return realm.make("File", state);
}

JsValue file_from_blob(Realm& realm, const JsValue& blob, const std::string& name) {
    const BlobState* source = state_of<BlobState>(blob);
    if (!source) return undefined();
    auto state = std::make_shared<BlobState>();
    state->is_file = true;
    state->bytes = source->bytes;
    state->type = source->type;
    state->name = name;
    state->last_modified = source->is_file ? source->last_modified : std::floor(js_current_time_ms());
    return realm.make("File", state);
}

std::string file_name(const JsValue& file) {
    const BlobState* state = state_of<BlobState>(file);
    return state ? state->name : std::string();
}

void register_file_interfaces() {
    register_interface({"Blob", nullptr, setup_blob, construct_blob, 0, nullptr});
    register_interface({"File", "Blob", setup_file, construct_file, 2, nullptr});
    register_interface({"FileReader", "EventTarget", setup_reader, construct_reader, 0, nullptr});
}

} // namespace dom
} // namespace litebird
