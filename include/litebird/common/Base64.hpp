#pragma once

// RFC 4648 §4 base64, the alphabet `data:` URLs use.
//
// There were two copies of this loop before — `Blob`/`FileReader` in
// `DomFileBindings.cpp` and `btoa` in `DomWindowBindings.cpp` — and a third
// would have been written for `HTMLCanvasElement.toDataURL`. One function is
// enough: what differs between the three callers is what they accept as bytes
// and what they do with an unencodable character, neither of which is this
// loop's business.

#include <cstddef>
#include <string>
#include <string_view>

namespace litebird {

inline std::string base64_encode(const void* data, std::size_t size) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (std::size_t i = 0; i < size; i += 3) {
        const unsigned b0 = bytes[i];
        const unsigned b1 = i + 1 < size ? bytes[i + 1] : 0;
        const unsigned b2 = i + 2 < size ? bytes[i + 2] : 0;
        out += kAlphabet[b0 >> 2];
        out += kAlphabet[((b0 & 3) << 4) | (b1 >> 4)];
        out += i + 1 < size ? kAlphabet[((b1 & 15) << 2) | (b2 >> 6)] : '=';
        out += i + 2 < size ? kAlphabet[b2 & 63] : '=';
    }
    return out;
}

inline std::string base64_encode(std::string_view bytes) {
    return base64_encode(bytes.data(), bytes.size());
}

} // namespace litebird
