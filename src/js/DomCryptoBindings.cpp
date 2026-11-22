// Web Crypto API subset: Crypto.getRandomValues() and Crypto.randomUUID().

#include "litebird/js/DomBindings.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

namespace litebird {
namespace dom {

namespace {

void fill_random_bytes(uint8_t* dst, std::size_t len) {
    if (len == 0) return;
    int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        std::size_t off = 0;
        while (off < len) {
            ssize_t n = ::read(fd, dst + off, len - off);
            if (n <= 0) break;
            off += static_cast<std::size_t>(n);
        }
        ::close(fd);
        if (off == len) return;
    }
    std::random_device rd;
    for (std::size_t i = 0; i < len; ++i) {
        dst[i] = static_cast<uint8_t>(rd() & 0xffu);
    }
}

bool is_integer_typed_array(Realm& r, const JsValue& val, int& elem_bytes, bool& is_signed) {
    if (!val.is_object_like()) return false;
    struct Kind {
        const char* name;
        int bytes;
        bool sig;
    };
    static constexpr Kind kKinds[] = {
        {"Int8Array", 1, true},
        {"Uint8Array", 1, false},
        {"Uint8ClampedArray", 1, false},
        {"Int16Array", 2, true},
        {"Uint16Array", 2, false},
        {"Int32Array", 4, true},
        {"Uint32Array", 4, false},
    };
    for (const auto& k : kKinds) {
        JsValue ctor = r.window.is_object() ? r.window.get_property(k.name) : JsValue();
        if (ctor.is_function() && val.get_property("constructor").object_val == ctor.object_val) {
            elem_bytes = k.bytes;
            is_signed = k.sig;
            return true;
        }
    }
    return false;
}

void setup_crypto(Iface& I) {
    I.method("getRandomValues", 1, [](Realm& r, Args args) {
        require_args(args, 1, "Crypto.getRandomValues");
        JsValue arr = args[0];
        int elem_bytes = 0;
        bool is_signed = false;
        if (!is_integer_typed_array(r, arr, elem_bytes, is_signed)) {
            js_throw_type_error(
                "Failed to execute 'getRandomValues' on 'Crypto': The provided ArrayBufferView is not an integer TypedArray.");
        }
        JsValue byte_len_val = arr.get_property("byteLength");
        const std::size_t byte_len = byte_len_val.type == JsType::Number
            ? static_cast<std::size_t>(to_double(byte_len_val))
            : 0;
        if (byte_len > 65536) {
            js_throw_range_error(
                "Failed to execute 'getRandomValues' on 'Crypto': The ArrayBufferView's byte length exceeds 65536.");
        }
        std::vector<uint8_t> bytes(byte_len);
        fill_random_bytes(bytes.data(), bytes.size());
        const std::size_t count = byte_len / static_cast<std::size_t>(elem_bytes);
        for (std::size_t i = 0; i < count; ++i) {
            uint32_t u = 0;
            for (int b = 0; b < elem_bytes; ++b) {
                u |= static_cast<uint32_t>(bytes[i * static_cast<std::size_t>(elem_bytes) + static_cast<std::size_t>(b)])
                     << (8 * b);
            }
            double v = 0.0;
            if (is_signed) {
                if (elem_bytes == 1) v = static_cast<double>(static_cast<int8_t>(u));
                else if (elem_bytes == 2) v = static_cast<double>(static_cast<int16_t>(u));
                else v = static_cast<double>(static_cast<int32_t>(u));
            } else {
                v = static_cast<double>(u);
            }
            arr.set_property(std::to_string(i), num(v));
        }
        return arr;
    });

    I.method("randomUUID", 0, [](Realm&, Args) {
        std::array<uint8_t, 16> b{};
        fill_random_bytes(b.data(), b.size());
        b[6] = static_cast<uint8_t>((b[6] & 0x0fu) | 0x40u);
        b[8] = static_cast<uint8_t>((b[8] & 0x3fu) | 0x80u);
        char buf[37];
        std::snprintf(buf, sizeof(buf),
                      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                      b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                      b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
        return str(buf);
    });
}

} // namespace

void register_crypto_interfaces() {
    register_interface({"Crypto", nullptr, setup_crypto, nullptr, 0, nullptr});
}

} // namespace dom
} // namespace litebird
