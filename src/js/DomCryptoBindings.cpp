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

// Self-contained FIPS 180-4 SHA-256.
std::array<uint8_t, 32> sha256_bytes(const uint8_t* data, std::size_t len) {
    static constexpr uint32_t kK[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    auto rotr = [](uint32_t x, uint32_t n) { return (x >> n) | (x << (32u - n)); };
    uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80u);
    while ((msg.size() % 64u) != 56u) msg.push_back(0u);
    const uint64_t bit_len = static_cast<uint64_t>(len) * 8ull;
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xffu));

    for (std::size_t off = 0; off < msg.size(); off += 64u) {
        uint32_t w[64]{};
        for (int i = 0; i < 16; ++i) {
            const std::size_t j = off + static_cast<std::size_t>(i) * 4u;
            w[i] = (static_cast<uint32_t>(msg[j]) << 24) | (static_cast<uint32_t>(msg[j + 1]) << 16) |
                   (static_cast<uint32_t>(msg[j + 2]) << 8) | static_cast<uint32_t>(msg[j + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = hh + S1 + ch + kK[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = S0 + maj;
            hh = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
        out[static_cast<std::size_t>(i) * 4u + 0u] = static_cast<uint8_t>((h[i] >> 24) & 0xffu);
        out[static_cast<std::size_t>(i) * 4u + 1u] = static_cast<uint8_t>((h[i] >> 16) & 0xffu);
        out[static_cast<std::size_t>(i) * 4u + 2u] = static_cast<uint8_t>((h[i] >> 8) & 0xffu);
        out[static_cast<std::size_t>(i) * 4u + 3u] = static_cast<uint8_t>(h[i] & 0xffu);
    }
    return out;
}

void setup_subtle_crypto(Iface& I) {
    I.method("digest", 2, [](Realm& r, Args args) {
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        auto reject_with = [&](const std::string& name, const std::string& msg) {
            JsValue err = make_dom_exception(r, name, msg);
            if (promise.is_function()) return js_call(promise.get_property("reject"), promise, {err});
            return undefined();
        };
        if (args.size() < 2) {
            return reject_with("TypeError", "SubtleCrypto.digest: 2 arguments required.");
        }
        std::string alg = args[0].is_object_like() ? to_str(args[0].get_property("name")) : to_str(args[0]);
        for (char& c : alg) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        if (alg != "sha-256") {
            return reject_with("NotSupportedError", "SubtleCrypto.digest: unsupported algorithm '" + alg + "'.");
        }
        const JsValue& src = args[1];
        if (!src.is_object_like() || src.get_property("byteLength").type != JsType::Number) {
            return reject_with("TypeError", "SubtleCrypto.digest: data must be a BufferSource.");
        }
        JsValue u8_ctor = r.window.is_object() ? r.window.get_property("Uint8Array") : JsValue();
        JsValue ab_ctor = r.window.is_object() ? r.window.get_property("ArrayBuffer") : JsValue();
        if (!u8_ctor.is_function() || !ab_ctor.is_function()) {
            return reject_with("OperationError", "SubtleCrypto.digest: ArrayBuffer/Uint8Array unavailable.");
        }
        JsValue view = src.get_property("buffer").is_object_like()
            ? js_call(u8_ctor, u8_ctor, {src.get_property("buffer"), src.get_property("byteOffset"), src.get_property("byteLength")})
            : js_call(u8_ctor, u8_ctor, {src});
        const std::size_t len = static_cast<std::size_t>(to_double(view.get_property("byteLength")));
        std::vector<uint8_t> in(len);
        for (std::size_t i = 0; i < len; ++i) {
            in[i] = static_cast<uint8_t>(static_cast<int>(to_double(view.get_property(std::to_string(i)))) & 0xff);
        }
        auto digest = sha256_bytes(in.data(), in.size());
        JsValue out_ab = js_construct(ab_ctor, {num(32.0)});
        JsValue out_u8 = js_construct(u8_ctor, {out_ab});
        for (std::size_t i = 0; i < 32; ++i) {
            out_u8.set_property(std::to_string(i), num(static_cast<double>(digest[i])));
        }
        if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {out_ab});
        return undefined();
    });
}

void setup_crypto(Iface& I) {
    I.getter("subtle", [](Realm& r) { return r.make("SubtleCrypto"); });
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
    register_interface({"SubtleCrypto", nullptr, setup_subtle_crypto, nullptr, 0, nullptr});
    register_interface({"Crypto", nullptr, setup_crypto, nullptr, 0, nullptr});
}

} // namespace dom
} // namespace litebird
