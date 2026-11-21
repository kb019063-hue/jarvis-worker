// ArrayBuffer and the typed arrays.
//
// An ArrayBuffer is a byte buffer a script cannot read; a typed array is a
// window of fixed-width elements on one, and every write through it stores the
// element type's conversion of the value. The bytes live in C++ and not in the
// property table, so an instance carries a JsExotic that answers the indexed
// reads and writes, the index keys, `in`, `delete` and the key order — the hook
// Heap.hpp keeps for exactly this ("a NodeList also answers list[3]"). No file
// outside this one and JsInterpreter.cpp changes for the indexed behaviour,
// which is what let this phase be done beside the pull requests holding
// JsValue.cpp, Heap.cpp and JsEval.cpp.
//
// Elements are stored little-endian, as every browser does.

#include "litebird/js/TypedArray.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/js/Heap.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace litebird {

namespace {

using Args = const std::vector<JsValue>&;

// ---------------------------------------------------------------------------
// Element types
// ---------------------------------------------------------------------------

enum class ElementType : uint8_t {
    Int8 = 0, Uint8, Uint8Clamped, Int16, Uint16, Int32, Uint32, Float32, Float64
};

constexpr int kElementTypeCount = 9;

constexpr int element_size(ElementType type) {
    switch (type) {
        case ElementType::Int8:
        case ElementType::Uint8:
        case ElementType::Uint8Clamped: return 1;
        case ElementType::Int16:
        case ElementType::Uint16: return 2;
        case ElementType::Int32:
        case ElementType::Uint32:
        case ElementType::Float32: return 4;
        case ElementType::Float64: return 8;
    }
    return 1;
}

const char* element_name(ElementType type) {
    switch (type) {
        case ElementType::Int8: return "Int8Array";
        case ElementType::Uint8: return "Uint8Array";
        case ElementType::Uint8Clamped: return "Uint8ClampedArray";
        case ElementType::Int16: return "Int16Array";
        case ElementType::Uint16: return "Uint16Array";
        case ElementType::Int32: return "Int32Array";
        case ElementType::Uint32: return "Uint32Array";
        case ElementType::Float32: return "Float32Array";
        case ElementType::Float64: return "Float64Array";
    }
    return "Uint8Array";
}

double to_integer_or_infinity(double value) {
    if (std::isnan(value)) return 0;
    if (std::isinf(value)) return value;
    return std::trunc(value);
}

uint32_t to_uint32(double value) {
    if (!std::isfinite(value) || value == 0) return 0;
    double wrapped = std::fmod(std::trunc(value), 4294967296.0);
    if (wrapped < 0) wrapped += 4294967296.0;
    return static_cast<uint32_t>(wrapped);
}

int32_t to_int32(double value) { return static_cast<int32_t>(to_uint32(value)); }
int16_t to_int16(double value) { return static_cast<int16_t>(static_cast<uint16_t>(to_uint32(value))); }
int8_t to_int8(double value) { return static_cast<int8_t>(static_cast<uint8_t>(to_uint32(value))); }
uint8_t to_uint8(double value) { return static_cast<uint8_t>(to_uint32(value)); }

// Uint8ClampedArray: round half to even, then clamp. Never wraps: 300 is 255
// and -4 is 0, where Uint8Array wraps both.
uint8_t to_uint8_clamped(double value) {
    if (std::isnan(value) || value <= 0) return 0;
    if (value >= 255) return 255;
    return static_cast<uint8_t>(std::nearbyint(value));
}

// ---------------------------------------------------------------------------
// Bytes, little-endian
// ---------------------------------------------------------------------------

double load_element(const uint8_t* base, ElementType type) {
    switch (type) {
        case ElementType::Int8: {
            int8_t v = 0;
            std::memcpy(&v, base, 1);
            return static_cast<double>(v);
        }
        case ElementType::Uint8:
        case ElementType::Uint8Clamped: {
            uint8_t v = 0;
            std::memcpy(&v, base, 1);
            return static_cast<double>(v);
        }
        case ElementType::Int16: {
            uint16_t raw = static_cast<uint16_t>(base[0]) | (static_cast<uint16_t>(base[1]) << 8);
            return static_cast<double>(static_cast<int16_t>(raw));
        }
        case ElementType::Uint16: {
            uint16_t raw = static_cast<uint16_t>(base[0]) | (static_cast<uint16_t>(base[1]) << 8);
            return static_cast<double>(raw);
        }
        case ElementType::Int32:
        case ElementType::Uint32: {
            uint32_t raw = static_cast<uint32_t>(base[0]) | (static_cast<uint32_t>(base[1]) << 8) |
                           (static_cast<uint32_t>(base[2]) << 16) | (static_cast<uint32_t>(base[3]) << 24);
            if (type == ElementType::Int32) return static_cast<double>(static_cast<int32_t>(raw));
            return static_cast<double>(raw);
        }
        case ElementType::Float32: {
            uint32_t raw = static_cast<uint32_t>(base[0]) | (static_cast<uint32_t>(base[1]) << 8) |
                           (static_cast<uint32_t>(base[2]) << 16) | (static_cast<uint32_t>(base[3]) << 24);
            float v = 0;
            std::memcpy(&v, &raw, 4);
            return static_cast<double>(v);
        }
        case ElementType::Float64: {
            uint64_t raw = 0;
            for (int i = 7; i >= 0; --i) raw = (raw << 8) | base[i];
            double v = 0;
            std::memcpy(&v, &raw, 8);
            return v;
        }
    }
    return 0;
}

void store_element(uint8_t* base, ElementType type, double value) {
    switch (type) {
        case ElementType::Int8: {
            int8_t v = to_int8(value);
            std::memcpy(base, &v, 1);
            return;
        }
        case ElementType::Uint8: {
            uint8_t v = to_uint8(value);
            std::memcpy(base, &v, 1);
            return;
        }
        case ElementType::Uint8Clamped: {
            uint8_t v = to_uint8_clamped(value);
            std::memcpy(base, &v, 1);
            return;
        }
        case ElementType::Int16: {
            uint16_t raw = static_cast<uint16_t>(to_int16(value));
            base[0] = static_cast<uint8_t>(raw & 0xFF);
            base[1] = static_cast<uint8_t>((raw >> 8) & 0xFF);
            return;
        }
        case ElementType::Uint16: {
            uint16_t raw = static_cast<uint16_t>(to_uint32(value) & 0xFFFF);
            base[0] = static_cast<uint8_t>(raw & 0xFF);
            base[1] = static_cast<uint8_t>((raw >> 8) & 0xFF);
            return;
        }
        case ElementType::Int32:
        case ElementType::Uint32: {
            uint32_t raw = static_cast<uint32_t>(to_int32(value));
            base[0] = static_cast<uint8_t>(raw & 0xFF);
            base[1] = static_cast<uint8_t>((raw >> 8) & 0xFF);
            base[2] = static_cast<uint8_t>((raw >> 16) & 0xFF);
            base[3] = static_cast<uint8_t>((raw >> 24) & 0xFF);
            return;
        }
        case ElementType::Float32: {
            float v = static_cast<float>(value);
            uint32_t raw = 0;
            std::memcpy(&raw, &v, 4);
            base[0] = static_cast<uint8_t>(raw & 0xFF);
            base[1] = static_cast<uint8_t>((raw >> 8) & 0xFF);
            base[2] = static_cast<uint8_t>((raw >> 16) & 0xFF);
            base[3] = static_cast<uint8_t>((raw >> 24) & 0xFF);
            return;
        }
        case ElementType::Float64: {
            uint64_t raw = 0;
            std::memcpy(&raw, &value, 8);
            for (int i = 0; i < 8; ++i) base[i] = static_cast<uint8_t>((raw >> (8 * i)) & 0xFF);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// State behind the objects
// ---------------------------------------------------------------------------

struct ArrayBufferData {
    std::vector<uint8_t> bytes;
    bool detached{false};
    /// The one script object for these bytes, once one has been made: a buffer
    /// and the `buffer` of a view of it are the same object, as they are in a
    /// browser. Marked by every view that can still reach it, so the cell
    /// cannot be freed while a view of this buffer is alive.
    JsObject* owner_object{nullptr};
};

struct TypedArrayData {
    std::shared_ptr<ArrayBufferData> buffer;
    std::size_t byte_offset{0};
    std::size_t length{0};  // in elements
    ElementType type{ElementType::Uint8};
};

/// A DataView: a window on a buffer with no element type — every read and
/// write names its own width, offset and byte order.
struct DataViewData {
    std::shared_ptr<ArrayBufferData> buffer;
    std::size_t byte_offset{0};
    std::size_t byte_length{0};
};

uint8_t* elements_of(const TypedArrayData& data) {
    return data.buffer->bytes.data() + data.byte_offset;
}

const uint8_t* elements_of_const(const TypedArrayData& data) {
    return data.buffer->bytes.data() + data.byte_offset;
}

// A canonical array index: digits only, no leading zero unless it is "0", and
// within 2^53-1. Anything else is an ordinary named property.
std::optional<std::size_t> parse_index(const std::string& name) {
    if (name.empty() || name.size() > 16) return std::nullopt;
    if (name.size() > 1 && name[0] == '0') return std::nullopt;
    std::size_t value = 0;
    for (char c : name) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + static_cast<std::size_t>(c - '0');
    }
    if (value > 9007199254740991ull) return std::nullopt;
    return value;
}

std::shared_ptr<ArrayBufferData> buffer_of(const JsValue& value);
std::shared_ptr<TypedArrayData> view_of(const JsValue& value);
std::shared_ptr<DataViewData> dataview_of(const JsValue& value);
JsValue make_buffer_object(const std::shared_ptr<ArrayBufferData>& bytes);
/// The one script object for a buffer, so two views of it are `===`.
JsValue buffer_object_for(const std::shared_ptr<ArrayBufferData>& bytes);

// ---------------------------------------------------------------------------
// The exotics
// ---------------------------------------------------------------------------

/// The exotic every typed array carries: indexed access, `length`,
/// `byteLength`, `byteOffset`, key order, `in`, and the refusal to delete.
class TypedArrayView : public JsExotic {
public:
    explicit TypedArrayView(std::shared_ptr<TypedArrayData> data) : m_data(std::move(data)) {}

    const std::shared_ptr<TypedArrayData>& data() const { return m_data; }

    void trace(Heap& heap) override {
        // The buffer's script object, if there is one, is reachable from here:
        // `view.buffer` has to answer the same object every time.
        if (m_data->buffer->owner_object) heap.mark_object(m_data->buffer->owner_object);
    }

    bool get(JsObject* self, const std::string& name, JsValue& out) override {
        (void)self;
        if (const auto index = parse_index(name)) {
            if (*index >= m_data->length || m_data->buffer->detached) return false;
            out = JsValue::make_number(
                load_element(elements_of_const(*m_data) + *index * element_size(m_data->type), m_data->type));
            return true;
        }
        if (name == "length") {
            out = JsValue::make_number(m_data->buffer->detached
                                           ? 0.0
                                           : static_cast<double>(m_data->length));
            return true;
        }
        if (name == "byteLength") {
            out = JsValue::make_number(
                m_data->buffer->detached
                    ? 0.0
                    : static_cast<double>(m_data->length) * element_size(m_data->type));
            return true;
        }
        if (name == "byteOffset") {
            out = JsValue::make_number(static_cast<double>(m_data->byte_offset));
            return true;
        }
        if (name == "buffer") {
            out = buffer_object_for(m_data->buffer);
            return true;
        }
        return false;
    }

    SetResult set(JsObject* self, const std::string& name, const JsValue& value) override {
        (void)self;
        const auto index = parse_index(name);
        if (!index) return SetResult::NotHandled;
        // A canonical index past the end is dealt with and dropped, as the
        // standard says: `ta[5] = 9` on a length-2 view does not make a
        // property, so `ta[5]` stays undefined. A non-index name such as
        // `ta[-1]` is an ordinary property and is stored.
        if (*index >= m_data->length || m_data->buffer->detached) return SetResult::Handled;
        store_element(elements_of(*m_data) + *index * element_size(m_data->type), m_data->type,
                      js_to_number(value));
        return SetResult::Handled;
    }

    bool has(JsObject* self, const std::string& name) override {
        (void)self;
        const auto index = parse_index(name);
        return index && *index < m_data->length && !m_data->buffer->detached;
    }

    bool remove(JsObject* self, const std::string& name, bool& result) override {
        (void)self;
        (void)name;
        // An element is not configurable: `delete ta[0]` is false, and a
        // strict-mode delete throws on it.
        result = false;
        return true;
    }

    void keys(JsObject* self, std::vector<std::string>& out) override {
        (void)self;
        if (m_data->buffer->detached) return;
        out.reserve(out.size() + m_data->length);
        for (std::size_t i = 0; i < m_data->length; ++i) out.push_back(std::to_string(i));
    }

    bool computed_property(JsObject* self, const std::string& name, bool& enumerable,
                           bool& writable) override {
        (void)self;
        const auto index = parse_index(name);
        if (!index || *index >= m_data->length) return false;
        enumerable = true;
        writable = true;
        return true;
    }

private:
    std::shared_ptr<TypedArrayData> m_data;
};

/// The exotic an ArrayBuffer carries: `byteLength` and the bytes. It answers no
/// index, because a script cannot read a buffer directly — that is what a view
/// is for.
class ArrayBufferHolder : public JsExotic {
public:
    explicit ArrayBufferHolder(std::shared_ptr<ArrayBufferData> data) : m_data(std::move(data)) {}

    const std::shared_ptr<ArrayBufferData>& data() const { return m_data; }

    void trace(Heap& heap) override {
        if (m_data->owner_object) heap.mark_object(m_data->owner_object);
    }

    bool get(JsObject* self, const std::string& name, JsValue& out) override {
        (void)self;
        if (name == "byteLength") {
            out = JsValue::make_number(m_data->detached ? 0.0
                                                        : static_cast<double>(m_data->bytes.size()));
            return true;
        }
        return false;
    }

private:
    std::shared_ptr<ArrayBufferData> m_data;
};

std::shared_ptr<TypedArrayData> view_of(const JsValue& value) {
    if (!value.is_object_like() || !value.object_val || !value.object_val->exotic) return nullptr;
    auto view = std::dynamic_pointer_cast<TypedArrayView>(value.object_val->exotic);
    return view ? view->data() : nullptr;
}

std::shared_ptr<ArrayBufferData> buffer_of(const JsValue& value) {
    if (!value.is_object_like() || !value.object_val || !value.object_val->exotic) return nullptr;
    auto holder = std::dynamic_pointer_cast<ArrayBufferHolder>(value.object_val->exotic);
    return holder ? holder->data() : nullptr;
}

// ---------------------------------------------------------------------------
// Making the objects
// ---------------------------------------------------------------------------

struct TypedArrayRealm {
    JsObject* array_buffer_proto{nullptr};
    JsObject* dataview_proto{nullptr};
    JsObject* typed_array_proto{nullptr};
    JsObject* protos[kElementTypeCount]{};
    JsValue constructors[kElementTypeCount];
    JsValue typed_array_ctor;  // %TypedArray%, the abstract class
};

// The realm's typed-array objects. A function-local static so it is torn down
// before the heap it points into, in the same order the other built-ins use.
TypedArrayRealm& realm() {
    static TypedArrayRealm instance;
    return instance;
}

JsValue allocate_buffer_object(const std::shared_ptr<ArrayBufferData>& bytes) {
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = realm().array_buffer_proto;
    bytes->owner_object = cell;
    cell->exotic = std::make_shared<ArrayBufferHolder>(bytes);
    return JsValue::make_object(cell);
}

// `new ArrayBuffer(4)` and the `buffer` of a view of it are the same object:
// every path to a buffer goes through `buffer_object_for`, which is the only
// place one is made.
JsValue make_buffer_object(const std::shared_ptr<ArrayBufferData>& bytes);

// A buffer has one script object however many views point at it, and whether
// it was reached by `new ArrayBuffer` or by `view.buffer`. The pointer lives on
// the buffer's own data, so there is no table to grow and nothing to prune:
// every view marks the object while it can still reach it (`trace`), which is
// what keeps `dv.buffer === buf` true after the caller drops `buf`.
JsValue buffer_object_for(const std::shared_ptr<ArrayBufferData>& bytes) {
    if (bytes->owner_object) return JsValue::make_object(bytes->owner_object);
    return allocate_buffer_object(bytes);
}

JsValue make_buffer_object(const std::shared_ptr<ArrayBufferData>& bytes) {
    return buffer_object_for(bytes);
}

JsValue make_view_object(std::shared_ptr<TypedArrayData> data, JsObject* proto) {
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = proto;
    cell->exotic = std::make_shared<TypedArrayView>(data);
    return JsValue::make_object(cell);
}

std::shared_ptr<TypedArrayData> require_view(const JsValue& value, const char* what) {
    auto data = view_of(value);
    if (!data) js_throw_type_error(std::string(what) + ": receiver is not a typed array");
    if (data->buffer->detached) js_throw_type_error(std::string(what) + ": the buffer was detached");
    return data;
}

double require_count(const JsValue& argument, const char* what) {
    const double integer = to_integer_or_infinity(js_to_number(argument));
    if (integer < 0) js_throw_type_error(std::string(what) + ": length must not be negative");
    return integer;
}

/// The begin/end pair of a method whose first range argument is at `first`.
/// `slice(1)` has begin at 0; `fill(7, 1)` has it at 1, because 7 is the value.
void clamp_range(Args args, std::size_t first, std::size_t length, std::size_t& begin,
                 std::size_t& end) {
    const double relative_begin = args.size() > first && !args[first].is_undefined()
                                      ? to_integer_or_infinity(js_to_number(args[first]))
                                      : 0;
    const double relative_end = args.size() > first + 1 && !args[first + 1].is_undefined()
                                    ? to_integer_or_infinity(js_to_number(args[first + 1]))
                                    : static_cast<double>(length);
    // A relative index counts from the end when it is negative, and is then
    // clamped to [0, length] — the standard's RelativeIndex+Clamp.
    auto clamp = [length](double value) -> std::size_t {
        double index = value;
        if (index < 0) index += static_cast<double>(length);
        if (index < 0) index = 0;
        if (index > static_cast<double>(length)) index = static_cast<double>(length);
        return static_cast<std::size_t>(index);
    };
    begin = clamp(relative_begin);
    end = clamp(relative_end);
    if (end < begin) end = begin;
}

/// A view over a whole buffer (or a window of it), checked as the standard does.
std::shared_ptr<TypedArrayData> make_view_data(std::shared_ptr<ArrayBufferData> buffer,
                                               std::size_t byte_offset, std::size_t length,
                                               ElementType type, const char* what) {
    const std::size_t size = static_cast<std::size_t>(element_size(type));
    if (buffer->detached) js_throw_type_error(std::string(what) + ": the buffer was detached");
    if (byte_offset % size != 0)
        js_throw_type_error(std::string(what) + ": the offset is not a multiple of the element size");
    const std::size_t available = buffer->bytes.size() > byte_offset ? buffer->bytes.size() - byte_offset : 0;
    if (buffer->bytes.size() < byte_offset || length > available / size)
        js_throw_type_error(std::string(what) + ": the view is larger than the buffer");
    auto data = std::make_shared<TypedArrayData>();
    data->buffer = std::move(buffer);
    data->byte_offset = byte_offset;
    data->length = length;
    data->type = type;
    return data;
}

ElementType registered_type(const JsValue& constructor, ElementType fallback) {
    JsValue marker = constructor.get_property("@@typedarray-kind");
    if (marker.type != JsType::Number) return fallback;
    const int kind = static_cast<int>(marker.number_val);
    if (kind < 0 || kind >= kElementTypeCount) return fallback;
    return static_cast<ElementType>(kind);
}

/// The element type of the current construction: from `new.target` where there
/// is one (so `new List()` for `class List extends Uint8Array` is a Uint8Array
/// view with List's prototype), else from the constructor itself.
ElementType current_element_type(ElementType self) {
    const JsValue* target = JsValue::current_new_target();
    if (target && !target->is_undefined()) return registered_type(*target, self);
    return self;
}

JsValue construct_view(Args args, ElementType self_type) {
    const ElementType type = current_element_type(self_type);
    const char* what = element_name(type);
    const JsValue& first = js_arg(args, 0);

    // A derived construction already has its object; a plain one is made here.
    const JsValue* derived = JsValue::current_is_construct() ? JsValue::current_derived_this() : nullptr;
    JsObject* proto = derived && derived->object_val ? derived->object_val->prototype
                                                    : realm().protos[static_cast<int>(type)];

    auto finish = [&](std::shared_ptr<TypedArrayData> data) {
        if (derived && derived->object_val) {
            JsObject* cell = derived->object_val.get();
            cell->exotic = std::make_shared<TypedArrayView>(data);
            return JsValue::make_object(cell);
        }
        return make_view_object(std::move(data), proto);
    };

    if (first.is_undefined()) {
        auto data = std::make_shared<TypedArrayData>();
        data->buffer = std::make_shared<ArrayBufferData>();
        data->type = type;
        return finish(std::move(data));
    }
    if (auto buffer = buffer_of(first)) {
        const std::size_t size = static_cast<std::size_t>(element_size(type));
        const double offset_number = args.size() > 1 ? require_count(args[1], what) : 0;
        const std::size_t offset = static_cast<std::size_t>(offset_number);
        std::size_t length = 0;
        if (args.size() > 2 && !args[2].is_undefined()) {
            length = static_cast<std::size_t>(require_count(args[2], what));
        } else {
            if (buffer->bytes.size() < offset || (buffer->bytes.size() - offset) % size != 0)
                js_throw_type_error(std::string(what) +
                                    ": the buffer length is not a multiple of the element size");
            length = (buffer->bytes.size() - offset) / size;
        }
        return finish(make_view_data(std::move(buffer), offset, length, type, what));
    }
    if (auto other = view_of(first)) {
        auto data = std::make_shared<TypedArrayData>();
        data->buffer = std::make_shared<ArrayBufferData>();
        data->length = other->length;
        data->type = type;
        data->buffer->bytes.resize(other->length * static_cast<std::size_t>(element_size(type)));
        const std::size_t size = static_cast<std::size_t>(element_size(type));
        for (std::size_t i = 0; i < other->length; ++i) {
            store_element(elements_of(*data) + i * size, type,
                          load_element(elements_of_const(*other) + i * element_size(other->type),
                                       other->type));
        }
        return finish(std::move(data));
    }
    if (first.is_object_like()) {
        // A sequence or an array-like: the length is known only after the copy,
        // so take the elements first and size the buffer to them.
        std::vector<double> values;
        const bool sequenced = first.is_array() || first.get_property("@@iterator").is_function();
        if (sequenced) {
            js_iterate(first, [&](const JsValue& value) {
                values.push_back(js_to_number(value));
                return true;
            });
        } else {
            const double count = to_integer_or_infinity(js_to_number(first.get_property("length")));
            for (double i = 0; i < count; ++i) values.push_back(js_to_number(first.get_property(js_number_to_string(i))));
        }
        auto data = std::make_shared<TypedArrayData>();
        data->buffer = std::make_shared<ArrayBufferData>();
        data->length = values.size();
        data->type = type;
        data->buffer->bytes.resize(values.size() * static_cast<std::size_t>(element_size(type)));
        for (std::size_t i = 0; i < values.size(); ++i)
            store_element(elements_of(*data) + i * element_size(type), type, values[i]);
        return finish(std::move(data));
    }

    const double length_number = require_count(first, what);
    if (std::isinf(length_number))
        js_throw_range_error(std::string(what) + ": the length is too large");
    auto data = std::make_shared<TypedArrayData>();
    data->buffer = std::make_shared<ArrayBufferData>();
    data->length = static_cast<std::size_t>(length_number);
    data->type = type;
    data->buffer->bytes.assign(data->length * static_cast<std::size_t>(element_size(type)), 0);
    return finish(std::move(data));
}

/// The first argument of a method that iterates: a function, or a TypeError
/// naming the method that wanted one.
JsValue require_callback(Args args, const char* what) {
    const JsValue callback = js_arg(args, 0);
    if (!callback.is_function())
        js_throw_type_error(std::string(what) + ": the callback is not callable");
    return callback;
}

JsValue make_iterator(const std::shared_ptr<TypedArrayData>& data, int kind) {
    // A plain object with its own `next`, as the engine's other iterators are,
    // so the loop fast paths see an ordinary iterator.
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = JsIntrinsics::get().object_proto;
    JsValue iterator = JsValue::make_object(cell);
    auto position = std::make_shared<std::size_t>(0);
    iterator.set_property("next", js_make_native("next", 0, [data, position, kind](Args) {
        JsObject* result_cell = Heap::default_heap().allocate_object();
        result_cell->prototype = JsIntrinsics::get().object_proto;
        JsValue out = JsValue::make_object(result_cell);
        if (*position >= data->length || data->buffer->detached) {
            out.set_property("value", JsValue::make_undefined());
            out.set_property("done", JsValue::make_boolean(true));
            return out;
        }
        const std::size_t index = (*position)++;
        const double element =
            load_element(elements_of_const(*data) + index * element_size(data->type), data->type);
        JsValue value;
        if (kind == 0) {
            value = JsValue::make_number(static_cast<double>(index));
        } else if (kind == 1) {
            value = JsValue::make_number(element);
        } else {
            value = JsValue::make_array(
                {JsValue::make_number(static_cast<double>(index)), JsValue::make_number(element)});
        }
        out.set_property("value", value);
        out.set_property("done", JsValue::make_boolean(false));
        return out;
    }));
    JsValue itself = js_make_native("@@iterator", 0, [](Args) { return JsValue::current_this(); });
    iterator.set_property("@@iterator", itself);
    iterator.set_property("Symbol.iterator", itself);
    return iterator;
}

// ---------------------------------------------------------------------------
// DataView
// ---------------------------------------------------------------------------

// A typed array fixes three things at construction: its element width, its
// offset and the fact that its bytes are little-endian. A DataView fixes none
// of them — every call names its own offset, and anything wider than a byte
// also names its byte order — so it is the only way to read a buffer that some
// other program wrote big-endian, or to read a `float` at offset 3.

std::uint64_t assemble(const std::uint8_t* base, std::size_t width, bool little_endian) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i) {
        // Big-endian puts the first byte in the most significant position;
        // little-endian puts it in the least, so it is read last.
        const std::size_t at = little_endian ? width - 1 - i : i;
        value = (value << 8) | base[at];
    }
    return value;
}

void scatter(std::uint8_t* base, std::size_t width, std::uint64_t value, bool little_endian) {
    for (std::size_t i = 0; i < width; ++i) {
        const std::size_t at = little_endian ? i : width - 1 - i;
        base[at] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
    }
}

// The bytes are assembled into one unsigned integer in the order asked for,
// and only then reinterpreted, so the two byte orders differ in one place.
double dataview_load(const std::uint8_t* base, ElementType type, bool little_endian) {
    const std::uint64_t raw = assemble(base, static_cast<std::size_t>(element_size(type)), little_endian);
    switch (type) {
        case ElementType::Int8:
            return static_cast<double>(static_cast<std::int8_t>(raw));
        case ElementType::Uint8:
        case ElementType::Uint8Clamped:
            return static_cast<double>(static_cast<std::uint8_t>(raw));
        case ElementType::Int16:
            return static_cast<double>(static_cast<std::int16_t>(raw));
        case ElementType::Uint16:
            return static_cast<double>(static_cast<std::uint16_t>(raw));
        case ElementType::Int32:
            return static_cast<double>(static_cast<std::int32_t>(raw));
        case ElementType::Uint32:
            return static_cast<double>(static_cast<std::uint32_t>(raw));
        case ElementType::Float32: {
            const std::uint32_t bits = static_cast<std::uint32_t>(raw);
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof value);
            return static_cast<double>(value);
        }
        case ElementType::Float64: {
            double value = 0.0;
            std::memcpy(&value, &raw, sizeof value);
            return value;
        }
    }
    return 0.0;
}

// The write side uses the same conversions a typed array of that element type
// uses, so `setUint8(0, 258)` stores 2 and `setInt8(0, 200)` stores -56: a
// DataView has no element type, but a setter names one.
void dataview_store(std::uint8_t* base, ElementType type, double value, bool little_endian) {
    std::uint64_t raw = 0;
    switch (type) {
        case ElementType::Int8:
            raw = static_cast<std::uint8_t>(to_int8(value));
            break;
        case ElementType::Uint8:
            raw = to_uint8(value);
            break;
        case ElementType::Uint8Clamped:
            raw = to_uint8_clamped(value);
            break;
        case ElementType::Int16:
            raw = static_cast<std::uint16_t>(to_int16(value));
            break;
        case ElementType::Uint16:
            raw = static_cast<std::uint16_t>(to_uint32(value));
            break;
        case ElementType::Int32:
            raw = static_cast<std::uint32_t>(to_int32(value));
            break;
        case ElementType::Uint32:
            raw = to_uint32(value);
            break;
        case ElementType::Float32: {
            const float narrowed = static_cast<float>(value);
            std::uint32_t bits = 0;
            std::memcpy(&bits, &narrowed, sizeof bits);
            raw = bits;
            break;
        }
        case ElementType::Float64:
            std::memcpy(&raw, &value, sizeof raw);
            break;
    }
    scatter(base, static_cast<std::size_t>(element_size(type)), raw, little_endian);
}

/// The exotic a DataView carries: `byteLength`, `byteOffset` and `buffer`, and
/// no indexed access at all — a buffer is read through the methods, never by
/// indexing the view.
class DataViewHolder : public JsExotic {
public:
    explicit DataViewHolder(std::shared_ptr<DataViewData> data) : m_data(std::move(data)) {}

    const std::shared_ptr<DataViewData>& data() const { return m_data; }

    void trace(Heap& heap) override {
        if (m_data->buffer->owner_object) heap.mark_object(m_data->buffer->owner_object);
    }

    bool get(JsObject* self, const std::string& name, JsValue& out) override {
        (void)self;
        const bool detached = m_data->buffer->detached;
        if (name == "byteLength") {
            out = JsValue::make_number(detached ? 0.0 : static_cast<double>(m_data->byte_length));
            return true;
        }
        if (name == "byteOffset") {
            out = JsValue::make_number(static_cast<double>(m_data->byte_offset));
            return true;
        }
        if (name == "buffer") {
            out = buffer_object_for(m_data->buffer);
            return true;
        }
        return false;
    }

private:
    std::shared_ptr<DataViewData> m_data;
};

std::shared_ptr<DataViewData> dataview_of(const JsValue& value) {
    if (!value.is_object_like() || !value.object_val || !value.object_val->exotic) return nullptr;
    auto holder = std::dynamic_pointer_cast<DataViewHolder>(value.object_val->exotic);
    return holder ? holder->data() : nullptr;
}

std::shared_ptr<DataViewData> require_dataview(const JsValue& value, const char* what) {
    auto data = dataview_of(value);
    if (!data) js_throw_type_error(std::string(what) + ": receiver is not a DataView");
    if (data->buffer->detached) js_throw_type_error(std::string(what) + ": the buffer was detached");
    return data;
}

/// ToIndex for an offset: NaN is 0, a negative or an infinite value is a
/// RangeError, and the result is a whole number of bytes.
std::size_t to_index(const JsValue& value, const char* what) {
    const double number = js_to_number(value);
    if (std::isnan(number)) return 0;
    const double integer = std::trunc(number);
    if (integer < 0)
        js_throw_range_error(std::string(what) + ": the offset must not be negative");
    if (integer > 9007199254740991.0)
        js_throw_range_error(std::string(what) + ": the offset is out of range");
    return static_cast<std::size_t>(integer);
}

bool little_endian_of(Args args, std::size_t index) {
    return args.size() > index && args[index].is_truthy();
}

uint8_t* dataview_bytes(const DataViewData& data) {
    return data.buffer->bytes.data() + data.byte_offset;
}

void check_dataview_range(const DataViewData& data, std::size_t offset, std::size_t width,
                          const char* what) {
    if (offset + width > data.byte_length)
        js_throw_range_error(std::string(what) + ": the offset is past the end of the view");
}

JsValue construct_data_view(Args args) {
    auto buffer = buffer_of(js_arg(args, 0));
    if (!buffer)
        js_throw_type_error("DataView: the first argument must be an ArrayBuffer");
    const std::size_t offset =
        args.size() > 1 && !args[1].is_undefined() ? to_index(args[1], "DataView") : 0;
    if (offset > buffer->bytes.size())
        js_throw_range_error("DataView: the byteOffset is past the end of the buffer");
    std::size_t length = buffer->bytes.size() - offset;
    if (args.size() > 2 && !args[2].is_undefined()) {
        length = to_index(args[2], "DataView");
        if (offset + length > buffer->bytes.size())
            js_throw_range_error("DataView: the byteLength runs past the end of the buffer");
    }
    auto data = std::make_shared<DataViewData>();
    data->buffer = std::move(buffer);
    data->byte_offset = offset;
    data->byte_length = length;
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = realm().dataview_proto;
    cell->exotic = std::make_shared<DataViewHolder>(data);
    return JsValue::make_object(cell);
}

void install_data_view(JsIntrinsics& intrinsics, JsEnvironment& global) {
    JsObject* proto = Heap::default_heap().allocate_object();
    proto->prototype = intrinsics.object_proto;
    realm().dataview_proto = proto;
    JsValue proto_value = JsValue::make_object(proto);

    JsValue constructor = js_make_native("DataView", 3, [](Args args) -> JsValue {
        return construct_data_view(args);
    });
    constructor.define_property("prototype", proto_value, 0);
    js_define_hidden(proto, "constructor", constructor);
    js_define_hidden(proto, "@@toStringTag", JsValue::make_string("DataView"));

    // Sixteen getters and setter pairs, all one shape: the offset, the value
    // for a setter, and the byte order for anything wider than a byte.
    auto define_get = [&](const char* name, ElementType type) {
        js_define_method(proto, name, 1, [type, name](Args args) {
            auto data = require_dataview(JsValue::current_this(), name);
            const std::size_t offset = to_index(js_arg(args, 0), name);
            const std::size_t width = static_cast<std::size_t>(element_size(type));
            check_dataview_range(*data, offset, width, name);
            const bool little = width > 1 && little_endian_of(args, 1);
            return JsValue::make_number(dataview_load(dataview_bytes(*data) + offset, type, little));
        });
    };
    auto define_set = [&](const char* name, ElementType type) {
        js_define_method(proto, name, 2, [type, name](Args args) {
            auto data = require_dataview(JsValue::current_this(), name);
            const std::size_t offset = to_index(js_arg(args, 0), name);
            const std::size_t width = static_cast<std::size_t>(element_size(type));
            check_dataview_range(*data, offset, width, name);
            const bool little = width > 1 && little_endian_of(args, 2);
            dataview_store(dataview_bytes(*data) + offset, type, js_to_number(js_arg(args, 1)), little);
            return JsValue::make_undefined();
        });
    };

    define_get("getInt8", ElementType::Int8);
    define_get("getUint8", ElementType::Uint8);
    define_get("getInt16", ElementType::Int16);
    define_get("getUint16", ElementType::Uint16);
    define_get("getInt32", ElementType::Int32);
    define_get("getUint32", ElementType::Uint32);
    define_get("getFloat32", ElementType::Float32);
    define_get("getFloat64", ElementType::Float64);
    define_set("setInt8", ElementType::Int8);
    define_set("setUint8", ElementType::Uint8);
    define_set("setInt16", ElementType::Int16);
    define_set("setUint16", ElementType::Uint16);
    define_set("setInt32", ElementType::Int32);
    define_set("setUint32", ElementType::Uint32);
    define_set("setFloat32", ElementType::Float32);
    define_set("setFloat64", ElementType::Float64);

    global.define("DataView", constructor);
}

// ---------------------------------------------------------------------------
// ArrayBuffer
// ---------------------------------------------------------------------------

void install_array_buffer(JsIntrinsics& intrinsics, JsEnvironment& global) {
    JsObject* proto = Heap::default_heap().allocate_object();
    proto->prototype = intrinsics.object_proto;
    realm().array_buffer_proto = proto;
    JsValue proto_value = JsValue::make_object(proto);

    JsValue constructor = js_make_native("ArrayBuffer", 1, [](Args args) {
        const double length = require_count(js_arg(args, 0), "ArrayBuffer");
        if (std::isinf(length))
            js_throw_range_error("ArrayBuffer: the length is too large");
        auto bytes = std::make_shared<ArrayBufferData>();
        bytes->bytes.assign(static_cast<std::size_t>(length), 0);
        return make_buffer_object(bytes);
    });
    constructor.define_property("prototype", proto_value, 0);
    js_define_hidden(proto, "constructor", constructor);
    js_define_hidden(proto, "@@toStringTag", JsValue::make_string("ArrayBuffer"));

    js_define_method(proto, "slice", 2, [](Args args) {
        auto buffer = buffer_of(JsValue::current_this());
        if (!buffer) js_throw_type_error("ArrayBuffer.prototype.slice: receiver is not an ArrayBuffer");
        if (buffer->detached) js_throw_type_error("ArrayBuffer.prototype.slice: the buffer was detached");
        std::size_t begin = 0;
        std::size_t end = 0;
        clamp_range(args, 0, buffer->bytes.size(), begin, end);
        auto copy = std::make_shared<ArrayBufferData>();
        copy->bytes.assign(buffer->bytes.begin() + static_cast<ptrdiff_t>(begin),
                           buffer->bytes.begin() + static_cast<ptrdiff_t>(end));
        return make_buffer_object(copy);
    });

    constructor.define_property("isView", js_make_native("isView", 1, [](Args args) {
        const JsValue& value = js_arg(args, 0);
        return JsValue::make_boolean(view_of(value) != nullptr || dataview_of(value) != nullptr);
    }), 0);

    global.define("ArrayBuffer", constructor);
}

// ---------------------------------------------------------------------------
// %TypedArray%
// ---------------------------------------------------------------------------

void install_typed_array_prototype(JsIntrinsics& intrinsics, JsEnvironment& global) {
    JsObject* proto = Heap::default_heap().allocate_object();
    proto->prototype = intrinsics.object_proto;
    realm().typed_array_proto = proto;
    JsValue proto_value = JsValue::make_object(proto);
    (void)global;

    // %TypedArray% is a function no script can construct: `new
    // Object.getPrototypeOf(Uint8Array)` is a TypeError, as it is in a browser.
    JsValue constructor = js_make_native("TypedArray", 0, [](Args) -> JsValue {
        js_throw_type_error("Abstract class TypedArray not directly constructable");
    });
    // `Object.getPrototypeOf(Uint8Array) === TypedArray`: the typed-array
    // constructors are ordinary objects whose prototype is this function. The
    // name `TypedArray` is not a global, so it is installed under an internal
    // key the interpreter's realm knows.
    constructor.set_property("@@typedarray-base", JsValue::make_boolean(true));
    constructor.define_property("prototype", proto_value, 0);
    js_define_hidden(proto, "constructor", constructor);

    js_define_method(proto, "set", 1, [](Args args) {
        auto target = require_view(JsValue::current_this(), "TypedArray.prototype.set");
        const JsValue& source = js_arg(args, 0);
        if (!source.is_object_like())
            js_throw_type_error("TypedArray.prototype.set: the source is not an object");
        const double offset_number = args.size() > 1 ? to_integer_or_infinity(js_to_number(args[1])) : 0;
        if (offset_number < 0)
            js_throw_range_error("TypedArray.prototype.set: the offset must not be negative");
        const std::size_t offset = static_cast<std::size_t>(offset_number);
        // A view of the same buffer is read before it is written, so an
        // overlapping copy is as if it went through a temporary.
        std::vector<double> values;
        if (auto other = view_of(source)) {
            values.reserve(other->length);
            for (std::size_t i = 0; i < other->length; ++i)
                values.push_back(load_element(elements_of_const(*other) + i * element_size(other->type),
                                              other->type));
        } else {
            const double count = to_integer_or_infinity(js_to_number(source.get_property("length")));
            for (double i = 0; i < count; ++i)
                values.push_back(js_to_number(source.get_property(js_number_to_string(i))));
        }
        if (offset + values.size() > target->length)
            js_throw_range_error("TypedArray.prototype.set: the source is larger than the space left");
        for (std::size_t i = 0; i < values.size(); ++i)
            store_element(elements_of(*target) + (offset + i) * element_size(target->type),
                          target->type, values[i]);
        return JsValue::make_undefined();
    });

    js_define_method(proto, "subarray", 2, [](Args args) {
        auto data = require_view(JsValue::current_this(), "TypedArray.prototype.subarray");
        std::size_t begin = 0;
        std::size_t end = 0;
        clamp_range(args, 0, data->length, begin, end);
        auto window = std::make_shared<TypedArrayData>(*data);
        window->byte_offset = data->byte_offset + begin * static_cast<std::size_t>(element_size(data->type));
        window->length = end - begin;
        JsObject* self = JsValue::current_this().object_val.get();
        JsObject* proto_of = self && self->prototype ? self->prototype : realm().protos[static_cast<int>(data->type)];
        return make_view_object(std::move(window), proto_of);
    });

    js_define_method(proto, "slice", 2, [](Args args) {
        auto data = require_view(JsValue::current_this(), "TypedArray.prototype.slice");
        std::size_t begin = 0;
        std::size_t end = 0;
        clamp_range(args, 0, data->length, begin, end);
        auto copy = std::make_shared<TypedArrayData>();
        copy->buffer = std::make_shared<ArrayBufferData>();
        copy->length = end - begin;
        copy->type = data->type;
        const std::size_t size = static_cast<std::size_t>(element_size(data->type));
        copy->buffer->bytes.assign((end - begin) * size, 0);
        std::memcpy(elements_of(*copy), elements_of_const(*data) + begin * size, (end - begin) * size);
        return make_view_object(std::move(copy), realm().protos[static_cast<int>(data->type)]);
    });

    js_define_method(proto, "fill", 3, [](Args args) {
        auto data = require_view(JsValue::current_this(), "TypedArray.prototype.fill");
        const double value = js_to_number(js_arg(args, 0));
        std::size_t begin = 0;
        std::size_t end = 0;
        clamp_range(args, 1, data->length, begin, end);
        for (std::size_t i = begin; i < end; ++i)
            store_element(elements_of(*data) + i * element_size(data->type), data->type, value);
        return JsValue::current_this();
    });

    js_define_method(proto, "copyWithin", 3, [](Args args) {
        auto data = require_view(JsValue::current_this(), "TypedArray.prototype.copyWithin");
        // A negative relative index counts from the end and is then clamped:
        // copyWithin(0, -2) copies the last two elements to the front.
        const double length = static_cast<double>(data->length);
        auto relative = [length](const JsValue& value) {
            double index = to_integer_or_infinity(js_to_number(value));
            if (index < 0) index += length;
            if (index < 0) index = 0;
            if (index > length) index = length;
            return static_cast<std::size_t>(index);
        };
        const std::size_t target_begin = args.empty() ? 0 : relative(args[0]);
        const std::size_t source_begin = args.size() > 1 ? relative(args[1]) : 0;
        const std::size_t source_end =
            args.size() > 2 && !args[2].is_undefined() ? relative(args[2]) : data->length;
        const std::size_t count = std::min(source_end - source_begin, data->length - target_begin);
        std::vector<double> values;
        for (std::size_t i = 0; i < count; ++i)
            values.push_back(load_element(elements_of_const(*data) + (source_begin + i) * element_size(data->type),
                                          data->type));
        for (std::size_t i = 0; i < count; ++i)
            store_element(elements_of(*data) + (target_begin + i) * element_size(data->type), data->type,
                          values[i]);
        return JsValue::current_this();
    });

    js_define_method(proto, "indexOf", 1, [](Args args) {
        auto data = require_view(JsValue::current_this(), "TypedArray.prototype.indexOf");
        const double wanted = js_to_number(js_arg(args, 0));
        const double from = args.size() > 1 ? to_integer_or_infinity(js_to_number(args[1])) : 0;
        std::size_t start = 0;
        if (from >= 0) {
            if (from > static_cast<double>(data->length)) return JsValue::make_number(-1);
            start = static_cast<std::size_t>(from);
        } else {
            const double sum = static_cast<double>(data->length) + from;
            start = sum < 0 ? 0 : static_cast<std::size_t>(sum);
        }
        for (std::size_t i = start; i < data->length; ++i) {
            const double element = load_element(elements_of_const(*data) + i * element_size(data->type), data->type);
            if (element == wanted) return JsValue::make_number(static_cast<double>(i));
        }
        return JsValue::make_number(-1);
    });

    js_define_method(proto, "lastIndexOf", 1, [](Args args) {
        auto data = require_view(JsValue::current_this(), "TypedArray.prototype.lastIndexOf");
        const double wanted = js_to_number(js_arg(args, 0));
        std::size_t start = data->length == 0 ? 0 : data->length - 1;
        if (data->length == 0) return JsValue::make_number(-1);
        if (args.size() > 1) {
            const double from = to_integer_or_infinity(js_to_number(args[1]));
            if (from < 0) {
                const double sum = static_cast<double>(data->length) + from;
                if (sum < 0) return JsValue::make_number(-1);
                start = static_cast<std::size_t>(sum);
            } else {
                start = static_cast<std::size_t>(std::min(from, static_cast<double>(data->length - 1)));
            }
        }
        for (std::size_t i = start + 1; i-- > 0;) {
            const double element = load_element(elements_of_const(*data) + i * element_size(data->type), data->type);
            if (element == wanted) return JsValue::make_number(static_cast<double>(i));
        }
        return JsValue::make_number(-1);
    });

    js_define_method(proto, "includes", 1, [](Args args) {
        auto data = require_view(JsValue::current_this(), "TypedArray.prototype.includes");
        const JsValue& wanted = js_arg(args, 0);
        const double wanted_number = js_to_number(wanted);
        for (std::size_t i = 0; i < data->length; ++i) {
            const double element = load_element(elements_of_const(*data) + i * element_size(data->type), data->type);
            // SameValueZero: NaN is found by includes, where indexOf misses it.
            if (element == wanted_number || (std::isnan(element) && std::isnan(wanted_number)))
                return JsValue::make_boolean(true);
        }
        return JsValue::make_boolean(false);
    });

    js_define_method(proto, "toString", 0, [](Args) {
        auto data = require_view(JsValue::current_this(), "TypedArray.prototype.toString");
        std::string out;
        for (std::size_t i = 0; i < data->length; ++i) {
            if (i) out += ",";
            const double element =
                load_element(elements_of_const(*data) + i * element_size(data->type), data->type);
            // A typed array has no holes, so a NaN is a value to print and
            // not an empty slot: `new Float32Array([NaN]).join()` is "NaN".
            out += js_number_to_string(element);
        }
        return JsValue::make_string(out);
    });

    js_define_method(proto, "join", 1, [](Args args) {
        auto data = require_view(JsValue::current_this(), "TypedArray.prototype.join");
        const std::string separator =
            args.empty() || args[0].is_undefined() ? "," : js_to_string(args[0]);
        std::string out;
        for (std::size_t i = 0; i < data->length; ++i) {
            if (i) out += separator;
            const double element = load_element(elements_of_const(*data) + i * element_size(data->type), data->type);
            // A typed array has no holes, so a NaN is a value to print and
            // not an empty slot: `new Float32Array([NaN]).join()` is "NaN".
            out += js_number_to_string(element);
        }
        return JsValue::make_string(out);
    });

    js_define_method(proto, "reverse", 0, [](Args) {
        auto data = require_view(JsValue::current_this(), "TypedArray.prototype.reverse");
        const std::size_t size = static_cast<std::size_t>(element_size(data->type));
        for (std::size_t i = 0, j = data->length ? data->length - 1 : 0; i < j; ++i, --j) {
            std::vector<uint8_t> left(size), right(size);
            std::memcpy(left.data(), elements_of_const(*data) + i * size, size);
            std::memcpy(right.data(), elements_of_const(*data) + j * size, size);
            std::memcpy(elements_of(*data) + i * size, right.data(), size);
            std::memcpy(elements_of(*data) + j * size, left.data(), size);
        }
        return JsValue::current_this();
    });

    // The iterating half of the prototype. Every callback takes
    // `(value, index, typedArray)`, and every one that makes a new view makes
    // it of the receiver's own element type and prototype, so a subclass stays
    // a subclass. There is no `Symbol.species` in the engine yet, so the
    // receiver stands in for what a species constructor would have returned;
    // the phase write-up records that rather than pretending otherwise.
    auto receiver_proto = [](const std::shared_ptr<TypedArrayData>& data) -> JsObject* {
        JsObject* self = JsValue::current_this().object_val.get();
        if (self && self->prototype) return self->prototype;
        return realm().protos[static_cast<int>(data->type)];
    };
    auto copy_like = [](const TypedArrayData& data, const std::vector<double>& values,
                        JsObject* proto) {
        auto out = std::make_shared<TypedArrayData>();
        out->buffer = std::make_shared<ArrayBufferData>();
        out->type = data.type;
        out->length = values.size();
        const std::size_t size = static_cast<std::size_t>(element_size(data.type));
        out->buffer->bytes.assign(values.size() * size, 0);
        for (std::size_t i = 0; i < values.size(); ++i)
            store_element(elements_of(*out) + i * size, out->type, values[i]);
        return make_view_object(std::move(out), proto);
    };
    auto collect = [](const TypedArrayData& data) {
        std::vector<double> values;
        values.reserve(data.length);
        for (std::size_t i = 0; i < data.length; ++i)
            values.push_back(
                load_element(elements_of_const(data) + i * element_size(data.type), data.type));
        return values;
    };

    js_define_method(proto, "at", 1, [](Args args) {
        auto data = require_view(JsValue::current_this(), "TypedArray.prototype.at");
        const double relative = to_integer_or_infinity(js_to_number(js_arg(args, 0)));
        const double index = relative < 0 ? relative + static_cast<double>(data->length) : relative;
        if (index < 0 || index >= static_cast<double>(data->length))
            return JsValue::make_undefined();
        return JsValue::make_number(
            load_element(elements_of_const(*data) + static_cast<std::size_t>(index) * element_size(data->type),
                         data->type));
    });

    js_define_method(proto, "map", 1, [receiver_proto, collect, copy_like](Args args) {
        const JsValue self = JsValue::current_this();
        auto data = require_view(self, "TypedArray.prototype.map");
        const JsValue callback = require_callback(args, "TypedArray.prototype.map");
        std::vector<double> values = collect(*data);
        for (std::size_t i = 0; i < values.size(); ++i) {
            values[i] = js_to_number(js_call(callback, js_arg(args, 1),
                                             {JsValue::make_number(values[i]),
                                              JsValue::make_number(static_cast<double>(i)), self}));
        }
        return copy_like(*data, values, receiver_proto(data));
    });

    js_define_method(proto, "filter", 1, [receiver_proto, collect, copy_like](Args args) {
        const JsValue self = JsValue::current_this();
        auto data = require_view(self, "TypedArray.prototype.filter");
        const JsValue callback = require_callback(args, "TypedArray.prototype.filter");
        const std::vector<double> elements = collect(*data);
        std::vector<double> kept;
        for (std::size_t i = 0; i < elements.size(); ++i) {
            const JsValue decision = js_call(callback, js_arg(args, 1),
                                             {JsValue::make_number(elements[i]),
                                              JsValue::make_number(static_cast<double>(i)), self});
            if (decision.is_truthy()) kept.push_back(elements[i]);
        }
        return copy_like(*data, kept, receiver_proto(data));
    });

    js_define_method(proto, "forEach", 1, [collect](Args args) {
        const JsValue self = JsValue::current_this();
        auto data = require_view(self, "TypedArray.prototype.forEach");
        const JsValue callback = require_callback(args, "TypedArray.prototype.forEach");
        const std::vector<double> values = collect(*data);
        for (std::size_t i = 0; i < values.size(); ++i)
            js_call(callback, js_arg(args, 1),
                    {JsValue::make_number(values[i]), JsValue::make_number(static_cast<double>(i)), self});
        return JsValue::make_undefined();
    });

    js_define_method(proto, "some", 1, [collect](Args args) {
        const JsValue self = JsValue::current_this();
        auto data = require_view(self, "TypedArray.prototype.some");
        const JsValue callback = require_callback(args, "TypedArray.prototype.some");
        const std::vector<double> values = collect(*data);
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (js_call(callback, js_arg(args, 1),
                        {JsValue::make_number(values[i]), JsValue::make_number(static_cast<double>(i)), self})
                    .is_truthy())
                return JsValue::make_boolean(true);
        }
        return JsValue::make_boolean(false);
    });

    js_define_method(proto, "every", 1, [collect](Args args) {
        const JsValue self = JsValue::current_this();
        auto data = require_view(self, "TypedArray.prototype.every");
        const JsValue callback = require_callback(args, "TypedArray.prototype.every");
        const std::vector<double> values = collect(*data);
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (!js_call(callback, js_arg(args, 1),
                         {JsValue::make_number(values[i]), JsValue::make_number(static_cast<double>(i)), self})
                     .is_truthy())
                return JsValue::make_boolean(false);
        }
        return JsValue::make_boolean(true);
    });

    js_define_method(proto, "find", 1, [collect](Args args) {
        const JsValue self = JsValue::current_this();
        auto data = require_view(self, "TypedArray.prototype.find");
        const JsValue callback = require_callback(args, "TypedArray.prototype.find");
        const std::vector<double> values = collect(*data);
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (js_call(callback, js_arg(args, 1),
                        {JsValue::make_number(values[i]), JsValue::make_number(static_cast<double>(i)), self})
                    .is_truthy())
                return JsValue::make_number(values[i]);
        }
        return JsValue::make_undefined();
    });

    js_define_method(proto, "findIndex", 1, [collect](Args args) {
        const JsValue self = JsValue::current_this();
        auto data = require_view(self, "TypedArray.prototype.findIndex");
        const JsValue callback = require_callback(args, "TypedArray.prototype.findIndex");
        const std::vector<double> values = collect(*data);
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (js_call(callback, js_arg(args, 1),
                        {JsValue::make_number(values[i]), JsValue::make_number(static_cast<double>(i)), self})
                    .is_truthy())
                return JsValue::make_number(static_cast<double>(i));
        }
        return JsValue::make_number(-1);
    });

    js_define_method(proto, "reduce", 1, [collect](Args args) {
        const JsValue self = JsValue::current_this();
        auto data = require_view(self, "TypedArray.prototype.reduce");
        const JsValue callback = require_callback(args, "TypedArray.prototype.reduce");
        const std::vector<double> values = collect(*data);
        std::size_t i = 0;
        JsValue accumulator;
        if (args.size() > 1) {
            accumulator = args[1];
        } else {
            if (values.empty())
                js_throw_type_error("TypedArray.prototype.reduce: the array is empty and no initial value was given");
            accumulator = JsValue::make_number(values[0]);
            i = 1;
        }
        for (; i < values.size(); ++i)
            accumulator = js_call(callback, JsValue::make_undefined(),
                                  {accumulator, JsValue::make_number(values[i]),
                                   JsValue::make_number(static_cast<double>(i)), self});
        return accumulator;
    });

    js_define_method(proto, "reduceRight", 1, [collect](Args args) {
        const JsValue self = JsValue::current_this();
        auto data = require_view(self, "TypedArray.prototype.reduceRight");
        const JsValue callback = require_callback(args, "TypedArray.prototype.reduceRight");
        const std::vector<double> values = collect(*data);
        if (values.empty() && args.size() <= 1)
            js_throw_type_error("TypedArray.prototype.reduceRight: the array is empty and no initial value was given");
        std::size_t i = values.size();
        JsValue accumulator;
        if (args.size() > 1) {
            accumulator = args[1];
        } else {
            --i;
            accumulator = JsValue::make_number(values[i]);
        }
        while (i > 0) {
            --i;
            accumulator = js_call(callback, JsValue::make_undefined(),
                                  {accumulator, JsValue::make_number(values[i]),
                                   JsValue::make_number(static_cast<double>(i)), self});
        }
        return accumulator;
    });

    // The default comparison is numeric, not the string comparison an Array
    // sort falls back to: `Float64Array.of(9, 10).sort()` is 9, 10 where
    // `[9, 10].sort()` is 10, 9. NaN compares greater than every number, and
    // the sort is stable either way.
    auto typed_sort = [](std::vector<double>& values, const JsValue& comparator) {
        if (comparator.is_function()) {
            std::stable_sort(values.begin(), values.end(), [&comparator](double left, double right) {
                return js_to_number(js_call(comparator, JsValue::make_undefined(),
                                            {JsValue::make_number(left), JsValue::make_number(right)})) < 0;
            });
            return;
        }
        std::stable_sort(values.begin(), values.end(), [](double left, double right) {
            const bool left_nan = std::isnan(left);
            const bool right_nan = std::isnan(right);
            if (left_nan || right_nan) return !left_nan && right_nan;
            return left < right;
        });
    };
    auto sorted_copy = [typed_sort, collect, copy_like, receiver_proto](Args args, const char* what) {
        auto data = require_view(JsValue::current_this(), what);
        const JsValue comparator = js_arg(args, 0);
        if (!comparator.is_undefined() && !comparator.is_function())
            js_throw_type_error(std::string(what) + ": the comparator is not callable");
        std::vector<double> values = collect(*data);
        typed_sort(values, comparator);
        return copy_like(*data, values, receiver_proto(data));
    };

    js_define_method(proto, "sort", 1, [typed_sort, collect](Args args) {
        const JsValue self = JsValue::current_this();
        auto data = require_view(self, "TypedArray.prototype.sort");
        const JsValue comparator = js_arg(args, 0);
        if (!comparator.is_undefined() && !comparator.is_function())
            js_throw_type_error("TypedArray.prototype.sort: the comparator is not callable");
        std::vector<double> values = collect(*data);
        typed_sort(values, comparator);
        const std::size_t size = static_cast<std::size_t>(element_size(data->type));
        for (std::size_t i = 0; i < values.size(); ++i)
            store_element(elements_of(*data) + i * size, data->type, values[i]);
        return self;
    });

    js_define_method(proto, "toSorted", 1, [sorted_copy](Args args) {
        return sorted_copy(args, "TypedArray.prototype.toSorted");
    });

    js_define_method(proto, "toReversed", 0, [collect, copy_like, receiver_proto](Args) {
        auto data = require_view(JsValue::current_this(), "TypedArray.prototype.toReversed");
        std::vector<double> values = collect(*data);
        std::reverse(values.begin(), values.end());
        return copy_like(*data, values, receiver_proto(data));
    });

    js_define_method(proto, "with", 2, [collect, copy_like, receiver_proto](Args args) {
        auto data = require_view(JsValue::current_this(), "TypedArray.prototype.with");
        const double relative = to_integer_or_infinity(js_to_number(js_arg(args, 0)));
        const double index = relative < 0 ? relative + static_cast<double>(data->length) : relative;
        if (index < 0 || index >= static_cast<double>(data->length))
            js_throw_range_error("TypedArray.prototype.with: the index is out of range");
        std::vector<double> values = collect(*data);
        values[static_cast<std::size_t>(index)] = js_to_number(js_arg(args, 1));
        return copy_like(*data, values, receiver_proto(data));
    });

    js_define_method(proto, "keys", 0, [](Args) {
        return make_iterator(require_view(JsValue::current_this(), "TypedArray.prototype.keys"), 0);
    });
    js_define_method(proto, "values", 0, [](Args) {
        return make_iterator(require_view(JsValue::current_this(), "TypedArray.prototype.values"), 1);
    });
    js_define_method(proto, "entries", 0, [](Args) {
        return make_iterator(require_view(JsValue::current_this(), "TypedArray.prototype.entries"), 2);
    });
    js_define_method(proto, "@@iterator", 0, [](Args) {
        return make_iterator(require_view(JsValue::current_this(), "TypedArray.prototype[Symbol.iterator]"), 1);
    });

    realm().typed_array_ctor = constructor;
}

// ---------------------------------------------------------------------------
// Each typed-array constructor
// ---------------------------------------------------------------------------

void install_one(JsEnvironment& global, ElementType type) {
    const int index = static_cast<int>(type);
    JsObject* proto = Heap::default_heap().allocate_object();
    proto->prototype = realm().typed_array_proto;
    realm().protos[index] = proto;
    JsValue proto_value = JsValue::make_object(proto);

    const char* name = element_name(type);
    JsValue constructor = js_make_native(name, 3, [type](Args args) -> JsValue {
        return construct_view(args, type);
    });
    // The chain the standard has: the constructor inherits from %TypedArray%,
    // so `Object.getPrototypeOf(Uint8Array)` is the abstract class. A native
    // function keeps its properties in a table and has no heap cell until
    // function_cell() makes one, which is what a prototype link needs.
    if (realm().typed_array_ctor.is_function()) {
        if (JsObject* cell = constructor.function_cell())
            cell->prototype = realm().typed_array_ctor.function_cell();
    }
    // `@@typedarray-kind` is how a subclass's `new.target` finds its element
    // type: `class L extends Int16Array {}` makes an Int16Array view.
    constructor.define_property("@@typedarray-kind", JsValue::make_number(index), 0);
    constructor.define_property("prototype", proto_value, 0);
    constructor.define_property("BYTES_PER_ELEMENT", JsValue::make_number(element_size(type)), 0);
    js_define_hidden(proto, "constructor", constructor);
    js_define_hidden(proto, "@@toStringTag", JsValue::make_string(name));
    proto_value.define_property("BYTES_PER_ELEMENT", JsValue::make_number(element_size(type)), 0);

    constructor.define_property("from", js_make_native("from", 1, [type](Args args) {
        const JsValue& source = js_arg(args, 0);
        std::vector<double> values;
        if (source.is_array() || source.get_property("@@iterator").is_function()) {
            const bool map = args.size() > 1 && args[1].is_function();
            if (map) {
                JsValue mapper = args[1];
                js_iterate(source, [&](const JsValue& value) {
                    values.push_back(js_to_number(js_call(mapper, JsValue::make_undefined(), {value, JsValue::make_number(values.size())})));
                    return true;
                });
            } else {
                js_iterate(source, [&](const JsValue& value) {
                    values.push_back(js_to_number(value));
                    return true;
                });
            }
        } else {
            const double count = to_integer_or_infinity(js_to_number(source.get_property("length")));
            const bool map = args.size() > 1 && args[1].is_function();
            JsValue mapper = map ? args[1] : JsValue::make_undefined();
            for (double i = 0; i < count; ++i) {
                JsValue value = source.get_property(js_number_to_string(i));
                if (map) value = js_call(mapper, JsValue::make_undefined(), {value, JsValue::make_number(i)});
                values.push_back(js_to_number(value));
            }
        }
        auto data = std::make_shared<TypedArrayData>();
        data->buffer = std::make_shared<ArrayBufferData>();
        data->length = values.size();
        data->type = type;
        data->buffer->bytes.assign(values.size() * static_cast<std::size_t>(element_size(type)), 0);
        for (std::size_t i = 0; i < values.size(); ++i)
            store_element(elements_of(*data) + i * element_size(type), type, values[i]);
        return make_view_object(std::move(data), realm().protos[static_cast<int>(type)]);
    }), 0);

    constructor.define_property("of", js_make_native("of", 0, [type](Args args) {
        auto data = std::make_shared<TypedArrayData>();
        data->buffer = std::make_shared<ArrayBufferData>();
        data->length = args.size();
        data->type = type;
        data->buffer->bytes.assign(args.size() * static_cast<std::size_t>(element_size(type)), 0);
        for (std::size_t i = 0; i < args.size(); ++i)
            store_element(elements_of(*data) + i * element_size(type), type, js_to_number(args[i]));
        return make_view_object(std::move(data), realm().protos[static_cast<int>(type)]);
    }), 0);

    realm().constructors[index] = constructor;
    global.define(name, std::move(constructor));
}

} // namespace

void install_typed_array_builtins(JsEnvironment& global) {
    JsIntrinsics& intrinsics = JsIntrinsics::get();
    install_array_buffer(intrinsics, global);
    install_data_view(intrinsics, global);
    install_typed_array_prototype(intrinsics, global);
    install_one(global, ElementType::Int8);
    install_one(global, ElementType::Uint8);
    install_one(global, ElementType::Uint8Clamped);
    install_one(global, ElementType::Int16);
    install_one(global, ElementType::Uint16);
    install_one(global, ElementType::Int32);
    install_one(global, ElementType::Uint32);
    install_one(global, ElementType::Float32);
    install_one(global, ElementType::Float64);
}

} // namespace litebird
