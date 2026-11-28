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

// Self-contained FIPS 180-4 SHA-1.
std::vector<uint8_t> sha1_bytes(const uint8_t* data, std::size_t len) {
    auto rotl = [](uint32_t x, uint32_t n) { return (x << n) | (x >> (32u - n)); };
    uint32_t h[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};
    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80u);
    while ((msg.size() % 64u) != 56u) msg.push_back(0u);
    const uint64_t bit_len = static_cast<uint64_t>(len) * 8ull;
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xffu));

    for (std::size_t off = 0; off < msg.size(); off += 64u) {
        uint32_t w[80]{};
        for (int i = 0; i < 16; ++i) {
            const std::size_t j = off + static_cast<std::size_t>(i) * 4u;
            w[i] = (static_cast<uint32_t>(msg[j]) << 24) | (static_cast<uint32_t>(msg[j + 1]) << 16) |
                   (static_cast<uint32_t>(msg[j + 2]) << 8) | static_cast<uint32_t>(msg[j + 3]);
        }
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0, k = 0;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5a827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcu; }
            else { f = b ^ c ^ d; k = 0xca62c1d6u; }
            uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = temp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::vector<uint8_t> out(20);
    for (int i = 0; i < 5; ++i) {
        out[static_cast<std::size_t>(i) * 4u + 0u] = static_cast<uint8_t>((h[i] >> 24) & 0xffu);
        out[static_cast<std::size_t>(i) * 4u + 1u] = static_cast<uint8_t>((h[i] >> 16) & 0xffu);
        out[static_cast<std::size_t>(i) * 4u + 2u] = static_cast<uint8_t>((h[i] >> 8) & 0xffu);
        out[static_cast<std::size_t>(i) * 4u + 3u] = static_cast<uint8_t>(h[i] & 0xffu);
    }
    return out;
}

// Self-contained FIPS 180-4 SHA-384 / SHA-512.
std::vector<uint8_t> sha512_family_bytes(const uint8_t* data, std::size_t len, bool is_384) {
    static constexpr uint64_t kK[80] = {
        0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
        0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
        0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
        0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
        0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
        0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
        0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
        0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
        0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
        0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
        0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
        0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
        0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
        0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
        0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
        0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
        0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
        0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
        0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
        0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull,
    };
    auto rotr = [](uint64_t x, uint64_t n) { return (x >> n) | (x << (64u - n)); };
    uint64_t h[8] = {
        is_384 ? 0xcbbb9d5dc1059ed8ull : 0x6a09e667f3bcc908ull,
        is_384 ? 0x629a292a367cd507ull : 0xbb67ae8584caa73bull,
        is_384 ? 0x9159015a3070dd17ull : 0x3c6ef372fe94f82bull,
        is_384 ? 0x152fecd8f70e5939ull : 0xa54ff53a5f1d36f1ull,
        is_384 ? 0x67332667ffc00b31ull : 0x510e527fade682d1ull,
        is_384 ? 0x8eb44a8768581511ull : 0x9b05688c2b3e6c1full,
        is_384 ? 0xdb0c2e0d64f98fa7ull : 0x1f83d9abfb41bd6bull,
        is_384 ? 0x47b5481dbefa4fa4ull : 0x5be0cd19137e2179ull,
    };
    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80u);
    while ((msg.size() % 128u) != 112u) msg.push_back(0u);
    for (int i = 0; i < 8; ++i) msg.push_back(0u);
    const uint64_t bit_len = static_cast<uint64_t>(len) * 8ull;
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xffu));

    for (std::size_t off = 0; off < msg.size(); off += 128u) {
        uint64_t w[80]{};
        for (int i = 0; i < 16; ++i) {
            const std::size_t j = off + static_cast<std::size_t>(i) * 8u;
            w[i] = (static_cast<uint64_t>(msg[j]) << 56) | (static_cast<uint64_t>(msg[j + 1]) << 48) |
                   (static_cast<uint64_t>(msg[j + 2]) << 40) | (static_cast<uint64_t>(msg[j + 3]) << 32) |
                   (static_cast<uint64_t>(msg[j + 4]) << 24) | (static_cast<uint64_t>(msg[j + 5]) << 16) |
                   (static_cast<uint64_t>(msg[j + 6]) << 8) | static_cast<uint64_t>(msg[j + 7]);
        }
        for (int i = 16; i < 80; ++i) {
            const uint64_t s0 = rotr(w[i - 15], 1) ^ rotr(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const uint64_t s1 = rotr(w[i - 2], 19) ^ rotr(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 80; ++i) {
            const uint64_t S1 = rotr(e, 14) ^ rotr(e, 18) ^ rotr(e, 41);
            const uint64_t ch = (e & f) ^ ((~e) & g);
            const uint64_t temp1 = hh + S1 + ch + kK[i] + w[i];
            const uint64_t S0 = rotr(a, 28) ^ rotr(a, 34) ^ rotr(a, 39);
            const uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint64_t temp2 = S0 + maj;
            hh = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    const int words = is_384 ? 6 : 8;
    std::vector<uint8_t> out(static_cast<std::size_t>(words) * 8u);
    for (int i = 0; i < words; ++i) {
        for (int b = 0; b < 8; ++b) {
            out[static_cast<std::size_t>(i) * 8u + static_cast<std::size_t>(b)] =
                static_cast<uint8_t>((h[i] >> ((7 - b) * 8)) & 0xffu);
        }
    }
    return out;
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

std::vector<uint8_t> digest_for_alg(const std::string& alg, const uint8_t* data, std::size_t len) {
    if (alg == "sha-1") return sha1_bytes(data, len);
    if (alg == "sha-256") {
        auto d = sha256_bytes(data, len);
        return std::vector<uint8_t>(d.begin(), d.end());
    }
    if (alg == "sha-384") return sha512_family_bytes(data, len, true);
    return sha512_family_bytes(data, len, false);
}

std::vector<uint8_t> hmac_bytes(const std::string& hash_alg, const std::vector<uint8_t>& raw_key,
                                const std::vector<uint8_t>& msg) {
    const std::size_t block_size = (hash_alg == "sha-384" || hash_alg == "sha-512") ? 128u : 64u;
    std::vector<uint8_t> k = raw_key;
    if (k.size() > block_size) {
        k = digest_for_alg(hash_alg, k.data(), k.size());
    }
    k.resize(block_size, 0u);
    std::vector<uint8_t> inner(block_size + msg.size());
    std::vector<uint8_t> outer(block_size);
    for (std::size_t i = 0; i < block_size; ++i) {
        inner[i] = static_cast<uint8_t>(k[i] ^ 0x36u);
        outer[i] = static_cast<uint8_t>(k[i] ^ 0x5cu);
    }
    for (std::size_t i = 0; i < msg.size(); ++i) inner[block_size + i] = msg[i];
    std::vector<uint8_t> inner_hash = digest_for_alg(hash_alg, inner.data(), inner.size());
    outer.insert(outer.end(), inner_hash.begin(), inner_hash.end());
    return digest_for_alg(hash_alg, outer.data(), outer.size());
}

std::vector<uint8_t> pbkdf2_bytes(const std::string& hash_alg, const std::vector<uint8_t>& password,
                                  const std::vector<uint8_t>& salt, uint32_t iterations, std::size_t dk_len) {
    std::vector<uint8_t> out;
    out.reserve(dk_len);
    uint32_t block_index = 1;
    while (out.size() < dk_len) {
        std::vector<uint8_t> s_block = salt;
        s_block.push_back(static_cast<uint8_t>((block_index >> 24) & 0xffu));
        s_block.push_back(static_cast<uint8_t>((block_index >> 16) & 0xffu));
        s_block.push_back(static_cast<uint8_t>((block_index >> 8) & 0xffu));
        s_block.push_back(static_cast<uint8_t>(block_index & 0xffu));
        std::vector<uint8_t> u = hmac_bytes(hash_alg, password, s_block);
        std::vector<uint8_t> t = u;
        for (uint32_t i = 1; i < iterations; ++i) {
            u = hmac_bytes(hash_alg, password, u);
            for (std::size_t j = 0; j < t.size(); ++j) t[j] ^= u[j];
        }
        const std::size_t need = std::min(t.size(), dk_len - out.size());
        out.insert(out.end(), t.begin(), t.begin() + static_cast<std::ptrdiff_t>(need));
        ++block_index;
    }
    return out;
}

std::vector<uint8_t> hkdf_bytes(const std::string& hash_alg, const std::vector<uint8_t>& ikm,
                                const std::vector<uint8_t>& salt, const std::vector<uint8_t>& info,
                                std::size_t okm_len) {
    std::size_t hash_len = 32u;
    if (hash_alg == "sha-1") hash_len = 20u;
    else if (hash_alg == "sha-384") hash_len = 48u;
    else if (hash_alg == "sha-512") hash_len = 64u;

    std::vector<uint8_t> effective_salt = salt.empty() ? std::vector<uint8_t>(hash_len, 0u) : salt;
    std::vector<uint8_t> prk = hmac_bytes(hash_alg, effective_salt, ikm);

    std::vector<uint8_t> out;
    out.reserve(okm_len);
    std::vector<uint8_t> t_prev;
    uint8_t counter = 1;
    while (out.size() < okm_len) {
        std::vector<uint8_t> input = t_prev;
        input.insert(input.end(), info.begin(), info.end());
        input.push_back(counter);
        t_prev = hmac_bytes(hash_alg, prk, input);
        const std::size_t need = std::min(t_prev.size(), okm_len - out.size());
        out.insert(out.end(), t_prev.begin(), t_prev.begin() + static_cast<std::ptrdiff_t>(need));
        ++counter;
    }
    return out;
}

// Self-contained FIPS 197 AES-128/192/256 block cipher + NIST SP 800-38A CTR mode.
void aes_encrypt_block(const uint8_t* key, std::size_t key_len, const uint8_t in[16], uint8_t out[16]) {
    static constexpr uint8_t kSbox[256] = {
        0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
        0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
        0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
        0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
        0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
        0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
        0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
        0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
        0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
        0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
        0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
        0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
        0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
        0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
        0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
        0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
    };
    static constexpr uint8_t kRcon[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
    const int nk = static_cast<int>(key_len / 4u);
    const int nr = nk + 6;
    uint8_t rk[240]{};
    for (std::size_t i = 0; i < key_len; ++i) rk[i] = key[i];
    for (int i = nk; i < 4 * (nr + 1); ++i) {
        uint8_t t[4] = {rk[4*(i-1)], rk[4*(i-1)+1], rk[4*(i-1)+2], rk[4*(i-1)+3]};
        if (i % nk == 0) {
            uint8_t tmp = t[0];
            t[0] = kSbox[t[1]] ^ kRcon[i / nk]; t[1] = kSbox[t[2]]; t[2] = kSbox[t[3]]; t[3] = kSbox[tmp];
        } else if (nk > 6 && (i % nk) == 4) {
            for (int j = 0; j < 4; ++j) t[j] = kSbox[t[j]];
        }
        for (int j = 0; j < 4; ++j) rk[4*i + j] = rk[4*(i - nk) + j] ^ t[j];
    }
    uint8_t s[16];
    for (int i = 0; i < 16; ++i) s[i] = in[i] ^ rk[i];
    auto xtime = [](uint8_t x) -> uint8_t { return static_cast<uint8_t>((x << 1) ^ ((x & 0x80u) ? 0x1bu : 0x00u)); };
    for (int r = 1; r <= nr; ++r) {
        for (int i = 0; i < 16; ++i) s[i] = kSbox[s[i]];
        uint8_t t1 = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t1;
        uint8_t t2 = s[2], t6 = s[6]; s[2] = s[10]; s[6] = s[14]; s[10] = t2; s[14] = t6;
        uint8_t t15 = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t15;
        if (r < nr) {
            for (int c = 0; c < 4; ++c) {
                uint8_t a0 = s[4*c], a1 = s[4*c+1], a2 = s[4*c+2], a3 = s[4*c+3];
                uint8_t xr = a0 ^ a1 ^ a2 ^ a3;
                s[4*c]   ^= xr ^ xtime(a0 ^ a1);
                s[4*c+1] ^= xr ^ xtime(a1 ^ a2);
                s[4*c+2] ^= xr ^ xtime(a2 ^ a3);
                s[4*c+3] ^= xr ^ xtime(a3 ^ a0);
            }
        }
        for (int i = 0; i < 16; ++i) s[i] ^= rk[16*r + i];
    }
    for (int i = 0; i < 16; ++i) out[i] = s[i];
}

void aes_decrypt_block(const uint8_t* key, std::size_t key_len, const uint8_t in[16], uint8_t out[16]) {
    static constexpr uint8_t kSbox[256] = {
        0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
        0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
        0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
        0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
        0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
        0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
        0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
        0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
        0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
        0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
        0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
        0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
        0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
        0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
        0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
        0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
    };
    static constexpr uint8_t kInvSbox[256] = {
        0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
        0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
        0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
        0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
        0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
        0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
        0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
        0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
        0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
        0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
        0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
        0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
        0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
        0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
        0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
        0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
    };
    static constexpr uint8_t kRcon[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
    const int nk = static_cast<int>(key_len / 4u);
    const int nr = nk + 6;
    uint8_t rk[240]{};
    for (std::size_t i = 0; i < key_len; ++i) rk[i] = key[i];
    for (int i = nk; i < 4 * (nr + 1); ++i) {
        uint8_t t[4] = {rk[4*(i-1)], rk[4*(i-1)+1], rk[4*(i-1)+2], rk[4*(i-1)+3]};
        if (i % nk == 0) {
            uint8_t tmp = t[0];
            t[0] = kSbox[t[1]] ^ kRcon[i / nk]; t[1] = kSbox[t[2]]; t[2] = kSbox[t[3]]; t[3] = kSbox[tmp];
        } else if (nk > 6 && (i % nk) == 4) {
            for (int j = 0; j < 4; ++j) t[j] = kSbox[t[j]];
        }
        for (int j = 0; j < 4; ++j) rk[4*i + j] = rk[4*(i - nk) + j] ^ t[j];
    }
    auto xtime = [](uint8_t x) -> uint8_t { return static_cast<uint8_t>((x << 1) ^ ((x & 0x80u) ? 0x1bu : 0x00u)); };
    auto mul = [&](uint8_t a, uint8_t b) -> uint8_t {
        uint8_t r = 0;
        for (int i = 0; i < 8; ++i) {
            if (b & 1u) r ^= a;
            a = xtime(a);
            b >>= 1;
        }
        return r;
    };
    uint8_t s[16];
    for (int i = 0; i < 16; ++i) s[i] = in[i] ^ rk[16 * nr + i];
    for (int r = nr - 1; r >= 0; --r) {
        uint8_t t13 = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t13;
        uint8_t t2 = s[2], t6 = s[6]; s[2] = s[10]; s[6] = s[14]; s[10] = t2; s[14] = t6;
        uint8_t t3 = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t3;
        for (int i = 0; i < 16; ++i) s[i] = kInvSbox[s[i]];
        for (int i = 0; i < 16; ++i) s[i] ^= rk[16 * r + i];
        if (r > 0) {
            for (int c = 0; c < 4; ++c) {
                uint8_t a0 = s[4*c], a1 = s[4*c+1], a2 = s[4*c+2], a3 = s[4*c+3];
                s[4*c]   = mul(a0, 0x0e) ^ mul(a1, 0x0b) ^ mul(a2, 0x0d) ^ mul(a3, 0x09);
                s[4*c+1] = mul(a0, 0x09) ^ mul(a1, 0x0e) ^ mul(a2, 0x0b) ^ mul(a3, 0x0d);
                s[4*c+2] = mul(a0, 0x0d) ^ mul(a1, 0x09) ^ mul(a2, 0x0e) ^ mul(a3, 0x0b);
                s[4*c+3] = mul(a0, 0x0b) ^ mul(a1, 0x0d) ^ mul(a2, 0x09) ^ mul(a3, 0x0e);
            }
        }
    }
    for (int i = 0; i < 16; ++i) out[i] = s[i];
}

std::vector<uint8_t> aes_cbc_encrypt(const std::vector<uint8_t>& key, const std::vector<uint8_t>& iv,
                                     const std::vector<uint8_t>& pt) {
    const std::size_t pad = 16u - (pt.size() % 16u);
    std::vector<uint8_t> padded = pt;
    padded.resize(pt.size() + pad, static_cast<uint8_t>(pad));
    std::vector<uint8_t> out(padded.size());
    uint8_t prev[16];
    for (int i = 0; i < 16; ++i) prev[i] = iv[static_cast<std::size_t>(i)];
    for (std::size_t off = 0; off < padded.size(); off += 16u) {
        uint8_t blk[16];
        for (int i = 0; i < 16; ++i) blk[i] = padded[off + static_cast<std::size_t>(i)] ^ prev[i];
        aes_encrypt_block(key.data(), key.size(), blk, &out[off]);
        for (int i = 0; i < 16; ++i) prev[i] = out[off + static_cast<std::size_t>(i)];
    }
    return out;
}

bool aes_cbc_decrypt(const std::vector<uint8_t>& key, const std::vector<uint8_t>& iv,
                     const std::vector<uint8_t>& ct, std::vector<uint8_t>& pt_out) {
    if (ct.empty() || (ct.size() % 16u) != 0) return false;
    pt_out.resize(ct.size());
    uint8_t prev[16];
    for (int i = 0; i < 16; ++i) prev[i] = iv[static_cast<std::size_t>(i)];
    for (std::size_t off = 0; off < ct.size(); off += 16u) {
        uint8_t dec[16];
        aes_decrypt_block(key.data(), key.size(), &ct[off], dec);
        for (int i = 0; i < 16; ++i) {
            pt_out[off + static_cast<std::size_t>(i)] = dec[i] ^ prev[i];
            prev[i] = ct[off + static_cast<std::size_t>(i)];
        }
    }
    const uint8_t pad = pt_out.back();
    if (pad == 0 || pad > 16u || pad > pt_out.size()) return false;
    for (std::size_t i = pt_out.size() - pad; i < pt_out.size(); ++i) {
        if (pt_out[i] != pad) return false;
    }
    pt_out.resize(pt_out.size() - pad);
    return true;
}

std::vector<uint8_t> aes_ctr_crypt(const std::vector<uint8_t>& key, const std::vector<uint8_t>& counter_in,
                                   int length_bits, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ctr = counter_in;
    std::vector<uint8_t> out(data.size());
    for (std::size_t off = 0; off < data.size(); off += 16u) {
        uint8_t ks[16]{};
        aes_encrypt_block(key.data(), key.size(), ctr.data(), ks);
        const std::size_t n = std::min<std::size_t>(16u, data.size() - off);
        for (std::size_t i = 0; i < n; ++i) out[off + i] = data[off + i] ^ ks[i];
        int rem = length_bits;
        for (int i = 15; i >= 0 && rem > 0; --i) {
            if (rem >= 8) {
                uint16_t sum = static_cast<uint16_t>(ctr[static_cast<std::size_t>(i)]) + 1u;
                ctr[static_cast<std::size_t>(i)] = static_cast<uint8_t>(sum & 0xffu);
                if ((sum & 0x100u) == 0) break;
                rem -= 8;
            } else {
                uint8_t mask = static_cast<uint8_t>((1u << rem) - 1u);
                uint8_t low = static_cast<uint8_t>((ctr[static_cast<std::size_t>(i)] + 1u) & mask);
                ctr[static_cast<std::size_t>(i)] = static_cast<uint8_t>((ctr[static_cast<std::size_t>(i)] & ~mask) | low);
                break;
            }
        }
    }
    return out;
}

// NIST SP 800-38D AES-GCM: GF(2^128) multiply, GHASH, and GCTR.
void gcm_gf_mul(const uint8_t x[16], const uint8_t y[16], uint8_t z[16]) {
    uint8_t v[16];
    for (int i = 0; i < 16; ++i) { z[i] = 0; v[i] = y[i]; }
    for (int i = 0; i < 128; ++i) {
        if ((x[i / 8] >> (7 - (i % 8))) & 1u) {
            for (int j = 0; j < 16; ++j) z[j] ^= v[j];
        }
        const uint8_t lsb = v[15] & 1u;
        for (int j = 15; j > 0; --j) v[j] = static_cast<uint8_t>((v[j] >> 1) | ((v[j - 1] & 1u) << 7));
        v[0] = static_cast<uint8_t>(v[0] >> 1);
        if (lsb) v[0] ^= 0xe1u;
    }
}

void gcm_ghash_update(const uint8_t h[16], uint8_t y[16], const uint8_t* data, std::size_t len) {
    for (std::size_t off = 0; off < len; off += 16u) {
        const std::size_t n = std::min<std::size_t>(16u, len - off);
        for (std::size_t i = 0; i < n; ++i) y[i] ^= data[off + i];
        uint8_t next[16]{};
        gcm_gf_mul(y, h, next);
        for (int i = 0; i < 16; ++i) y[i] = next[i];
    }
}

void gcm_ghash(const uint8_t h[16], const std::vector<uint8_t>& aad, const std::vector<uint8_t>& ct, uint8_t out[16]) {
    for (int i = 0; i < 16; ++i) out[i] = 0;
    gcm_ghash_update(h, out, aad.data(), aad.size());
    gcm_ghash_update(h, out, ct.data(), ct.size());
    uint8_t len_block[16]{};
    const uint64_t aad_bits = static_cast<uint64_t>(aad.size()) * 8ull;
    const uint64_t ct_bits = static_cast<uint64_t>(ct.size()) * 8ull;
    for (int i = 0; i < 8; ++i) {
        len_block[i] = static_cast<uint8_t>((aad_bits >> ((7 - i) * 8)) & 0xffu);
        len_block[8 + i] = static_cast<uint8_t>((ct_bits >> ((7 - i) * 8)) & 0xffu);
    }
    gcm_ghash_update(h, out, len_block, 16u);
}

void gcm_inc32(uint8_t ctr[16]) {
    for (int i = 15; i >= 12; --i) {
        if (++ctr[i] != 0) break;
    }
}

std::vector<uint8_t> aes_gcm_encrypt(const std::vector<uint8_t>& key, const std::vector<uint8_t>& iv,
                                     const std::vector<uint8_t>& aad, const std::vector<uint8_t>& pt,
                                     std::size_t tag_bytes) {
    uint8_t h[16]{};
    const uint8_t zero[16]{};
    aes_encrypt_block(key.data(), key.size(), zero, h);

    uint8_t j0[16]{};
    if (iv.size() == 12u) {
        for (int i = 0; i < 12; ++i) j0[i] = iv[static_cast<std::size_t>(i)];
        j0[15] = 1u;
    } else {
        gcm_ghash(h, {}, iv, j0);
    }

    uint8_t ctr[16];
    for (int i = 0; i < 16; ++i) ctr[i] = j0[i];
    std::vector<uint8_t> out(pt.size() + tag_bytes);
    for (std::size_t off = 0; off < pt.size(); off += 16u) {
        gcm_inc32(ctr);
        uint8_t ks[16]{};
        aes_encrypt_block(key.data(), key.size(), ctr, ks);
        const std::size_t n = std::min<std::size_t>(16u, pt.size() - off);
        for (std::size_t i = 0; i < n; ++i) out[off + i] = pt[off + i] ^ ks[i];
    }

    std::vector<uint8_t> ct_only(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(pt.size()));
    uint8_t s[16]{}, t_mask[16]{};
    gcm_ghash(h, aad, ct_only, s);
    aes_encrypt_block(key.data(), key.size(), j0, t_mask);
    for (std::size_t i = 0; i < tag_bytes; ++i) {
        out[pt.size() + i] = s[i] ^ t_mask[i];
    }
    return out;
}

bool aes_gcm_decrypt(const std::vector<uint8_t>& key, const std::vector<uint8_t>& iv,
                     const std::vector<uint8_t>& aad, const std::vector<uint8_t>& ct_and_tag,
                     std::size_t tag_bytes, std::vector<uint8_t>& pt_out) {
    if (ct_and_tag.size() < tag_bytes) return false;
    const std::size_t ct_len = ct_and_tag.size() - tag_bytes;
    std::vector<uint8_t> ct(ct_and_tag.begin(), ct_and_tag.begin() + static_cast<std::ptrdiff_t>(ct_len));

    uint8_t h[16]{};
    const uint8_t zero[16]{};
    aes_encrypt_block(key.data(), key.size(), zero, h);

    uint8_t j0[16]{};
    if (iv.size() == 12u) {
        for (int i = 0; i < 12; ++i) j0[i] = iv[static_cast<std::size_t>(i)];
        j0[15] = 1u;
    } else {
        gcm_ghash(h, {}, iv, j0);
    }

    uint8_t s[16]{}, t_mask[16]{};
    gcm_ghash(h, aad, ct, s);
    aes_encrypt_block(key.data(), key.size(), j0, t_mask);
    uint8_t diff = 0;
    for (std::size_t i = 0; i < tag_bytes; ++i) {
        diff |= static_cast<uint8_t>((s[i] ^ t_mask[i]) ^ ct_and_tag[ct_len + i]);
    }
    if (diff != 0) return false;

    uint8_t ctr[16];
    for (int i = 0; i < 16; ++i) ctr[i] = j0[i];
    pt_out.resize(ct_len);
    for (std::size_t off = 0; off < ct_len; off += 16u) {
        gcm_inc32(ctr);
        uint8_t ks[16]{};
        aes_encrypt_block(key.data(), key.size(), ctr, ks);
        const std::size_t n = std::min<std::size_t>(16u, ct_len - off);
        for (std::size_t i = 0; i < n; ++i) pt_out[off + i] = ct[off + i] ^ ks[i];
    }
    return true;
}

std::string base64url_encode(const std::vector<uint8_t>& data) {
    static constexpr char kTab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve(((data.size() + 2u) / 3u) * 4u);
    for (std::size_t i = 0; i < data.size(); i += 3u) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = (i + 1u < data.size()) ? data[i + 1u] : 0u;
        const uint32_t b2 = (i + 2u < data.size()) ? data[i + 2u] : 0u;
        const uint32_t v = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kTab[(v >> 18) & 63u]);
        out.push_back(kTab[(v >> 12) & 63u]);
        if (i + 1u < data.size()) out.push_back(kTab[(v >> 6) & 63u]);
        if (i + 2u < data.size()) out.push_back(kTab[v & 63u]);
    }
    return out;
}

bool base64url_decode(const std::string& s, std::vector<uint8_t>& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '-' || c == '+') return 62;
        if (c == '_' || c == '/') return 63;
        return -1;
    };
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : s) {
        if (c == '=') break;
        int v = val(c);
        if (v < 0) return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xffu));
        }
    }
    return true;
}

// NIST P-256 (secp256r1) ECDH primitives.
namespace p256 {
struct Fe { uint64_t v[4]; }; // little-endian limbs mod p

static constexpr Fe kP = {{0xffffffffffffffffull, 0x00000000ffffffffull, 0x0000000000000000ull, 0xffffffff00000001ull}};
static constexpr Fe kB = {{0x3bce3c3e27d2604bull, 0x651d06b0cc53b0f6ull, 0xb3ebbd55769886bcull, 0x5ac635d8aa3a93e7ull}};
static constexpr Fe kGx = {{0xf4a13945d898c296ull, 0x77037d812deb33a0ull, 0xf8bce6e563a440f2ull, 0x6b17d1f2e12c4247ull}};
static constexpr Fe kGy = {{0xcbb6406837bf51f5ull, 0x2bce33576b315eceull, 0x8ee7eb4a7c0f9e16ull, 0x4fe342e2fe1a7f9bull}};

inline bool is_zero(const Fe& a) { return (a.v[0] | a.v[1] | a.v[2] | a.v[3]) == 0; }

inline void add(const Fe& a, const Fe& b, Fe& r) {
    unsigned __int128 c = 0;
    for (int i = 0; i < 4; ++i) {
        c += static_cast<unsigned __int128>(a.v[i]) + b.v[i];
        r.v[i] = static_cast<uint64_t>(c);
        c >>= 64;
    }
    uint64_t borrow = 0;
    Fe t{};
    for (int i = 0; i < 4; ++i) {
        unsigned __int128 diff = static_cast<unsigned __int128>(r.v[i]) - kP.v[i] - borrow;
        t.v[i] = static_cast<uint64_t>(diff);
        borrow = (diff >> 127) & 1u;
    }
    if (c || !borrow) r = t;
}

inline void sub(const Fe& a, const Fe& b, Fe& r) {
    uint64_t borrow = 0;
    for (int i = 0; i < 4; ++i) {
        unsigned __int128 diff = static_cast<unsigned __int128>(a.v[i]) - b.v[i] - borrow;
        r.v[i] = static_cast<uint64_t>(diff);
        borrow = (diff >> 127) & 1u;
    }
    if (borrow) {
        unsigned __int128 c = 0;
        for (int i = 0; i < 4; ++i) {
            c += static_cast<unsigned __int128>(r.v[i]) + kP.v[i];
            r.v[i] = static_cast<uint64_t>(c);
            c >>= 64;
        }
    }
}

inline void mul(const Fe& a, const Fe& b, Fe& r) {
    Fe acc{};
    Fe x = a;
    for (int i = 0; i < 4; ++i) {
        for (int bit = 0; bit < 64; ++bit) {
            if ((b.v[i] >> bit) & 1ull) add(acc, x, acc);
            add(x, x, x);
        }
    }
    r = acc;
}

inline void sqr(const Fe& a, Fe& r) { mul(a, a, r); }

inline void inv(const Fe& a, Fe& r) {
    // a^(p-2) mod p
    Fe base = a;
    r = {{1, 0, 0, 0}};
    Fe exp = kP;
    exp.v[0] -= 2ull;
    for (int i = 0; i < 4; ++i) {
        for (int b = 0; b < 64; ++b) {
            if ((exp.v[i] >> b) & 1ull) mul(r, base, r);
            sqr(base, base);
        }
    }
}

struct Jac { Fe x, y, z; };

inline void point_double(const Jac& p, Jac& r) {
    if (is_zero(p.z) || is_zero(p.y)) { r = {{0,0,0,0}, {1,0,0,0}, {0,0,0,0}}; return; }
    Fe s, m, t1, t2, z2;
    sqr(p.y, t1); mul(p.x, t1, s); add(s, s, s); add(s, s, s); // S = 4*X*Y^2
    sqr(p.z, z2);
    sub(p.x, z2, t1); add(p.x, z2, t2); mul(t1, t2, m);
    add(m, m, t1); add(t1, m, m); // M = 3*(X-Z^2)*(X+Z^2)
    sqr(m, r.x); sub(r.x, s, r.x); sub(r.x, s, r.x); // X' = M^2 - 2*S
    sub(s, r.x, t1); mul(m, t1, t2);
    sqr(p.y, t1); sqr(t1, t1);
    for (int i = 0; i < 3; ++i) add(t1, t1, t1); // 8*Y^4
    mul(p.y, p.z, r.z); add(r.z, r.z, r.z);
    sub(t2, t1, r.y);
}

inline void point_add(const Jac& p, const Jac& q, Jac& r) {
    if (is_zero(p.z)) { r = q; return; }
    if (is_zero(q.z)) { r = p; return; }
    Fe z1z1, z2z2, u1, u2, s1, s2, h, hh, hhh, r_val, v, t1;
    sqr(p.z, z1z1); sqr(q.z, z2z2);
    mul(p.x, z2z2, u1); mul(q.x, z1z1, u2);
    mul(p.y, q.z, s1); mul(s1, z2z2, s1);
    mul(q.y, p.z, s2); mul(s2, z1z1, s2);
    sub(u2, u1, h);
    sub(s2, s1, r_val);
    if (is_zero(h)) {
        if (is_zero(r_val)) point_double(p, r);
        else r = {{0,0,0,0}, {1,0,0,0}, {0,0,0,0}};
        return;
    }
    sqr(h, hh); mul(hh, h, hhh); mul(u1, hh, v);
    sqr(r_val, r.x); sub(r.x, hhh, r.x); sub(r.x, v, r.x); sub(r.x, v, r.x);
    sub(v, r.x, t1); mul(r_val, t1, r.y); mul(s1, hhh, t1); sub(r.y, t1, r.y);
    mul(p.z, q.z, r.z); mul(r.z, h, r.z);
}

inline void scalar_mul(const uint8_t k_be[32], const Fe& px, const Fe& py, Fe& rx, Fe& ry) {
    Jac acc{{0,0,0,0}, {1,0,0,0}, {0,0,0,0}};
    Jac base{px, py, {{1,0,0,0}}};
    for (int i = 0; i < 32; ++i) {
        for (int b = 7; b >= 0; --b) {
            point_double(acc, acc);
            if ((k_be[i] >> b) & 1u) point_add(acc, base, acc);
        }
    }
    Fe zinv, zinv2, zinv3;
    inv(acc.z, zinv);
    sqr(zinv, zinv2);
    mul(zinv2, zinv, zinv3);
    mul(acc.x, zinv2, rx);
    mul(acc.y, zinv3, ry);
}

inline void fe_to_be(const Fe& a, uint8_t out[32]) {
    for (int i = 0; i < 4; ++i) {
        uint64_t w = a.v[3 - i];
        for (int j = 0; j < 8; ++j) out[i * 8 + j] = static_cast<uint8_t>((w >> ((7 - j) * 8)) & 0xffu);
    }
}

inline void be_to_fe(const uint8_t in[32], Fe& a) {
    for (int i = 0; i < 4; ++i) {
        uint64_t w = 0;
        for (int j = 0; j < 8; ++j) w = (w << 8) | in[i * 8 + j];
        a.v[3 - i] = w;
    }
}

static constexpr Fe kN = {{0xf3b9cac2fc632551ull, 0xbce6faada7179e84ull, 0xffffffffffffffffull, 0xffffffff00000000ull}};

inline void add_n(const Fe& a, const Fe& b, Fe& r) {
    unsigned __int128 c = 0;
    for (int i = 0; i < 4; ++i) {
        c += static_cast<unsigned __int128>(a.v[i]) + b.v[i];
        r.v[i] = static_cast<uint64_t>(c);
        c >>= 64;
    }
    uint64_t borrow = 0;
    Fe t{};
    for (int i = 0; i < 4; ++i) {
        unsigned __int128 diff = static_cast<unsigned __int128>(r.v[i]) - kN.v[i] - borrow;
        t.v[i] = static_cast<uint64_t>(diff);
        borrow = (diff >> 127) & 1u;
    }
    if (c || !borrow) r = t;
}

inline void mul_n(const Fe& a, const Fe& b, Fe& r) {
    Fe acc{};
    Fe x = a;
    for (int i = 0; i < 4; ++i) {
        for (int bit = 0; bit < 64; ++bit) {
            if ((b.v[i] >> bit) & 1ull) add_n(acc, x, acc);
            add_n(x, x, x);
        }
    }
    r = acc;
}

inline void inv_n(const Fe& a, Fe& r) {
    Fe base = a;
    r = {{1, 0, 0, 0}};
    Fe exp = kN;
    exp.v[0] -= 2ull;
    for (int i = 0; i < 4; ++i) {
        for (int b = 0; b < 64; ++b) {
            if ((exp.v[i] >> b) & 1ull) mul_n(r, base, r);
            mul_n(base, base, base);
        }
    }
}

inline bool ecdsa_sign(const uint8_t priv[32], const uint8_t hash32[32], uint8_t sig64[64],
                       const uint8_t k_be[32]) {
    Fe k, rx, ry;
    be_to_fe(k_be, k);
    if (is_zero(k)) return false;
    scalar_mul(k_be, kGx, kGy, rx, ry);
    Fe r_fe = rx;
    Fe zero{};
    add_n(r_fe, zero, r_fe);
    if (is_zero(r_fe)) return false;

    Fe z, d, kinv, rd, s;
    be_to_fe(hash32, z);
    add_n(z, zero, z);
    be_to_fe(priv, d);
    mul_n(r_fe, d, rd);
    add_n(z, rd, s);
    inv_n(k, kinv);
    mul_n(kinv, s, s);
    if (is_zero(s)) return false;
    fe_to_be(r_fe, sig64);
    fe_to_be(s, sig64 + 32);
    return true;
}

inline bool ecdsa_verify(const uint8_t pub65[65], const uint8_t hash32[32], const uint8_t sig64[64]) {
    if (pub65[0] != 0x04u) return false;
    Fe qx, qy, r_fe, s_fe, z;
    be_to_fe(pub65 + 1, qx);
    be_to_fe(pub65 + 33, qy);
    be_to_fe(sig64, r_fe);
    be_to_fe(sig64 + 32, s_fe);
    if (is_zero(r_fe) || is_zero(s_fe)) return false;
    be_to_fe(hash32, z);
    Fe zero{};
    add_n(z, zero, z);

    Fe w, u1, u2;
    inv_n(s_fe, w);
    mul_n(z, w, u1);
    mul_n(r_fe, w, u2);
    uint8_t u1_be[32], u2_be[32];
    fe_to_be(u1, u1_be);
    fe_to_be(u2, u2_be);

    Fe p1x, p1y, p2x, p2y;
    scalar_mul(u1_be, kGx, kGy, p1x, p1y);
    scalar_mul(u2_be, qx, qy, p2x, p2y);
    Jac j1{p1x, p1y, {{1, 0, 0, 0}}};
    Jac j2{p2x, p2y, {{1, 0, 0, 0}}};
    Jac sum{};
    point_add(j1, j2, sum);
    if (is_zero(sum.z)) return false;
    Fe zinv, zinv2, vx;
    inv(sum.z, zinv);
    sqr(zinv, zinv2);
    mul(sum.x, zinv2, vx);
    add_n(vx, zero, vx);
    return vx.v[0] == r_fe.v[0] && vx.v[1] == r_fe.v[1] && vx.v[2] == r_fe.v[2] && vx.v[3] == r_fe.v[3];
}
} // namespace p256

// RFC 3394 AES Key Wrap.
std::vector<uint8_t> aes_kw_wrap(const std::vector<uint8_t>& key, const std::vector<uint8_t>& pt) {
    const std::size_t n = pt.size() / 8u;
    std::vector<uint8_t> out(8u + pt.size());
    for (int i = 0; i < 8; ++i) out[static_cast<std::size_t>(i)] = 0xa6u;
    for (std::size_t i = 0; i < pt.size(); ++i) out[8u + i] = pt[i];
    for (std::size_t j = 0; j < 6u; ++j) {
        for (std::size_t i = 1; i <= n; ++i) {
            uint8_t blk[16];
            for (int k = 0; k < 8; ++k) blk[k] = out[static_cast<std::size_t>(k)];
            for (int k = 0; k < 8; ++k) blk[8 + k] = out[i * 8u + static_cast<std::size_t>(k)];
            aes_encrypt_block(key.data(), key.size(), blk, blk);
            const uint64_t t = static_cast<uint64_t>(n * j + i);
            for (int k = 0; k < 8; ++k) {
                out[static_cast<std::size_t>(k)] = blk[k] ^ static_cast<uint8_t>((t >> ((7 - k) * 8)) & 0xffu);
                out[i * 8u + static_cast<std::size_t>(k)] = blk[8 + k];
            }
        }
    }
    return out;
}

bool aes_kw_unwrap(const std::vector<uint8_t>& key, const std::vector<uint8_t>& ct, std::vector<uint8_t>& pt_out) {
    if (ct.size() < 24u || (ct.size() % 8u) != 0) return false;
    const std::size_t n = (ct.size() / 8u) - 1u;
    uint8_t a[8];
    for (int i = 0; i < 8; ++i) a[i] = ct[static_cast<std::size_t>(i)];
    pt_out.assign(ct.begin() + 8, ct.end());
    for (std::size_t j_rev = 6u; j_rev > 0; --j_rev) {
        const std::size_t j = j_rev - 1u;
        for (std::size_t i = n; i >= 1; --i) {
            const uint64_t t = static_cast<uint64_t>(n * j + i);
            uint8_t blk[16];
            for (int k = 0; k < 8; ++k) {
                blk[k] = a[k] ^ static_cast<uint8_t>((t >> ((7 - k) * 8)) & 0xffu);
                blk[8 + k] = pt_out[(i - 1u) * 8u + static_cast<std::size_t>(k)];
            }
            aes_decrypt_block(key.data(), key.size(), blk, blk);
            for (int k = 0; k < 8; ++k) {
                a[k] = blk[k];
                pt_out[(i - 1u) * 8u + static_cast<std::size_t>(k)] = blk[8 + k];
            }
        }
    }
    uint8_t diff = 0;
    for (int i = 0; i < 8; ++i) diff |= static_cast<uint8_t>(a[i] ^ 0xa6u);
    return diff == 0;
}

struct CryptoKeyState : JsExotic {
    std::string type = "secret";
    bool extractable = false;
    std::string hash_alg;
    std::vector<uint8_t> key_bytes;
    std::vector<std::string> usages;
};

static std::vector<std::string> read_usages(const JsValue& arr) {
    std::vector<std::string> out;
    if (!arr.is_object_like()) return out;
    std::size_t n = static_cast<std::size_t>(to_double(arr.get_property("length")));
    for (std::size_t i = 0; i < n; ++i) out.push_back(to_str(arr.get_property(std::to_string(i))));
    return out;
}

void setup_crypto_key(Iface& I) {
    I.getter("type", [](Realm&) { return str(this_state<CryptoKeyState>("CryptoKey").type); });
    I.getter("extractable", [](Realm&) { return boolean(this_state<CryptoKeyState>("CryptoKey").extractable); });
    I.getter("algorithm", [](Realm& r) {
        const auto& st = this_state<CryptoKeyState>("CryptoKey");
        JsValue obj_ctor = r.window.is_object() ? r.window.get_property("Object") : JsValue();
        JsValue alg = js_construct(obj_ctor, {});
        if (st.hash_alg == "aes-gcm" || st.hash_alg == "aes-cbc" || st.hash_alg == "aes-ctr" || st.hash_alg == "aes-kw") {
            std::string name = st.hash_alg == "aes-gcm" ? "AES-GCM"
                             : st.hash_alg == "aes-cbc" ? "AES-CBC"
                             : st.hash_alg == "aes-ctr" ? "AES-CTR" : "AES-KW";
            alg.set_property("name", str(name));
            alg.set_property("length", num(static_cast<double>(st.key_bytes.size() * 8u)));
        } else if (st.hash_alg == "ecdh" || st.hash_alg == "ecdsa") {
            alg.set_property("name", str(st.hash_alg == "ecdh" ? "ECDH" : "ECDSA"));
            alg.set_property("namedCurve", str("P-256"));
        } else if (st.hash_alg == "pbkdf2" || st.hash_alg == "hkdf") {
            alg.set_property("name", str(st.hash_alg == "pbkdf2" ? "PBKDF2" : "HKDF"));
        } else {
            alg.set_property("name", str("HMAC"));
            JsValue h = js_construct(obj_ctor, {});
            std::string hn = st.hash_alg == "sha-1" ? "SHA-1"
                           : st.hash_alg == "sha-384" ? "SHA-384"
                           : st.hash_alg == "sha-512" ? "SHA-512" : "SHA-256";
            h.set_property("name", str(hn));
            alg.set_property("hash", h);
            alg.set_property("length", num(static_cast<double>(st.key_bytes.size() * 8u)));
        }
        return alg;
    });
    I.getter("usages", [](Realm& r) {
        const auto& st = this_state<CryptoKeyState>("CryptoKey");
        JsValue arr_ctor = r.window.is_object() ? r.window.get_property("Array") : JsValue();
        JsValue out = js_construct(arr_ctor, {});
        for (std::size_t i = 0; i < st.usages.size(); ++i) {
            out.set_property(std::to_string(i), str(st.usages[i]));
        }
        return out;
    });
}

std::string ascii_lower_str(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool read_buffer_source(Realm& r, const JsValue& src, std::vector<uint8_t>& out) {
    if (!src.is_object_like() || src.get_property("byteLength").type != JsType::Number) return false;
    JsValue u8_ctor = r.window.is_object() ? r.window.get_property("Uint8Array") : JsValue();
    if (!u8_ctor.is_function()) return false;
    JsValue view = src.get_property("buffer").is_object_like()
        ? js_construct(u8_ctor, {src.get_property("buffer"), src.get_property("byteOffset"), src.get_property("byteLength")})
        : js_construct(u8_ctor, {src});
    const std::size_t len = static_cast<std::size_t>(to_double(view.get_property("byteLength")));
    out.resize(len);
    for (std::size_t i = 0; i < len; ++i) {
        out[i] = static_cast<uint8_t>(static_cast<int>(to_double(view.get_property(std::to_string(i)))) & 0xff);
    }
    return true;
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
        if (alg != "sha-1" && alg != "sha-256" && alg != "sha-384" && alg != "sha-512") {
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
            ? js_construct(u8_ctor, {src.get_property("buffer"), src.get_property("byteOffset"), src.get_property("byteLength")})
            : js_construct(u8_ctor, {src});
        const std::size_t len = static_cast<std::size_t>(to_double(view.get_property("byteLength")));
        std::vector<uint8_t> in(len);
        for (std::size_t i = 0; i < len; ++i) {
            in[i] = static_cast<uint8_t>(static_cast<int>(to_double(view.get_property(std::to_string(i)))) & 0xff);
        }
        std::vector<uint8_t> digest;
        if (alg == "sha-1") {
            digest = sha1_bytes(in.data(), in.size());
        } else if (alg == "sha-256") {
            auto d = sha256_bytes(in.data(), in.size());
            digest.assign(d.begin(), d.end());
        } else if (alg == "sha-384") {
            digest = sha512_family_bytes(in.data(), in.size(), true);
        } else {
            digest = sha512_family_bytes(in.data(), in.size(), false);
        }
        JsValue out_ab = js_construct(ab_ctor, {num(static_cast<double>(digest.size()))});
        JsValue out_u8 = js_construct(u8_ctor, {out_ab});
        for (std::size_t i = 0; i < digest.size(); ++i) {
            out_u8.set_property(std::to_string(i), num(static_cast<double>(digest[i])));
        }
        if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {out_ab});
        return undefined();
    });

    I.method("importKey", 5, [](Realm& r, Args args) {
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        auto reject_with = [&](const std::string& name, const std::string& msg) {
            JsValue err = make_dom_exception(r, name, msg);
            if (promise.is_function()) return js_call(promise.get_property("reject"), promise, {err});
            return undefined();
        };
        if (args.size() < 5) return reject_with("TypeError", "SubtleCrypto.importKey: 5 arguments required.");
        std::string fmt = to_str(args[0]);
        if (fmt != "raw" && fmt != "jwk" && fmt != "spki" && fmt != "pkcs8") {
            return reject_with("NotSupportedError", "SubtleCrypto.importKey: only 'raw', 'jwk', 'spki', and 'pkcs8' formats are supported.");
        }
        std::vector<uint8_t> key_bytes;
        std::string ec_key_type;
        std::string jwk_alg;
        if (fmt == "jwk") {
            if (!args[1].is_object_like()) {
                return reject_with("TypeError", "SubtleCrypto.importKey: jwk keyData must be an object.");
            }
            JsValue ext_val = args[1].get_property("ext");
            if (ext_val.type == JsType::Boolean && !ext_val.is_truthy() && args[3].is_truthy()) {
                return reject_with("DataError", "SubtleCrypto.importKey: JWK ext is false but extractable is true.");
            }
            JsValue jwk_alg_val = args[1].get_property("alg");
            jwk_alg = (!jwk_alg_val.is_undefined() && !jwk_alg_val.is_nullish()) ? to_str(jwk_alg_val) : "";
            std::string kty = to_str(args[1].get_property("kty"));
            if (kty == "EC") {
                if (to_str(args[1].get_property("crv")) != "P-256") {
                    return reject_with("NotSupportedError", "SubtleCrypto.importKey: only P-256 EC JWK is supported.");
                }
                std::vector<uint8_t> x_bytes, y_bytes, d_bytes;
                if (!base64url_decode(to_str(args[1].get_property("x")), x_bytes) || x_bytes.size() != 32u ||
                    !base64url_decode(to_str(args[1].get_property("y")), y_bytes) || y_bytes.size() != 32u) {
                    return reject_with("DataError", "SubtleCrypto.importKey: invalid EC JWK x/y coordinates.");
                }
                JsValue d_val = args[1].get_property("d");
                if (!d_val.is_undefined() && !d_val.is_nullish()) {
                    if (!base64url_decode(to_str(d_val), d_bytes) || d_bytes.size() != 32u) {
                        return reject_with("DataError", "SubtleCrypto.importKey: invalid EC JWK 'd' field.");
                    }
                    ec_key_type = "private";
                    key_bytes = std::move(d_bytes);
                } else {
                    ec_key_type = "public";
                    key_bytes.reserve(65u);
                    key_bytes.push_back(0x04u);
                    key_bytes.insert(key_bytes.end(), x_bytes.begin(), x_bytes.end());
                    key_bytes.insert(key_bytes.end(), y_bytes.begin(), y_bytes.end());
                }
            } else if (kty == "oct") {
                std::string k_str = to_str(args[1].get_property("k"));
                if (k_str.empty() || !base64url_decode(k_str, key_bytes)) {
                    return reject_with("DataError", "SubtleCrypto.importKey: invalid JWK 'k' field.");
                }
            } else {
                return reject_with("DataError", "SubtleCrypto.importKey: only kty 'oct' or 'EC' JWK is supported.");
            }
        } else if (!read_buffer_source(r, args[1], key_bytes)) {
            return reject_with("TypeError", "SubtleCrypto.importKey: keyData must be a BufferSource.");
        }
        const JsValue& alg_val = args[2];
        std::string alg_name = alg_val.is_object_like() ? ascii_lower_str(to_str(alg_val.get_property("name")))
                                                        : ascii_lower_str(to_str(alg_val));
        if (alg_name == "ecdh" || alg_name == "ecdsa") {
            static constexpr uint8_t kSpkiPrefix[26] = {
                0x30,0x59,0x30,0x13,0x06,0x07,0x2a,0x86,0x48,0xce,0x3d,0x02,0x01,
                0x06,0x08,0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07,0x03,0x42,0x00
            };
            static constexpr uint8_t kPkcs8Prefix[35] = {
                0x30,0x41,0x02,0x01,0x00,0x30,0x13,0x06,0x07,0x2a,0x86,0x48,0xce,0x3d,0x02,0x01,
                0x06,0x08,0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07,0x04,0x27,0x30,0x25,0x02,0x01,0x01,0x04,0x20
            };
            if (fmt == "jwk") {
                if (ec_key_type.empty()) {
                    return reject_with("DataError", "SubtleCrypto.importKey: EC algorithm requires kty 'EC' JWK.");
                }
                if (!jwk_alg.empty() && alg_name == "ecdsa" && jwk_alg != "ES256") {
                    return reject_with("DataError", "SubtleCrypto.importKey: JWK alg '" + jwk_alg + "' does not match ES256.");
                }
            } else if (fmt == "spki") {
                if (key_bytes.size() != 91u || !std::equal(kSpkiPrefix, kSpkiPrefix + 26u, key_bytes.begin()) || key_bytes[26] != 0x04u) {
                    return reject_with("DataError", "SubtleCrypto.importKey: invalid P-256 SPKI public key.");
                }
                key_bytes.erase(key_bytes.begin(), key_bytes.begin() + 26);
                ec_key_type = "public";
            } else if (fmt == "pkcs8") {
                if (key_bytes.size() != 67u || !std::equal(kPkcs8Prefix, kPkcs8Prefix + 35u, key_bytes.begin())) {
                    return reject_with("DataError", "SubtleCrypto.importKey: invalid P-256 PKCS8 private key.");
                }
                key_bytes.erase(key_bytes.begin(), key_bytes.begin() + 35);
                ec_key_type = "private";
            } else if (fmt != "raw" || key_bytes.size() != 65u || key_bytes[0] != 0x04u) {
                return reject_with("DataError", "SubtleCrypto.importKey: EC raw public key must be 65 bytes (0x04 || X || Y).");
            } else {
                ec_key_type = "public";
            }
            auto state = std::make_shared<CryptoKeyState>();
            state->type = ec_key_type;
            state->extractable = args[3].is_truthy();
            state->hash_alg = alg_name;
            state->key_bytes = std::move(key_bytes);
            state->usages = read_usages(args[4]);
            JsValue key = r.make("CryptoKey", state);
            if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {key});
            return undefined();
        }
        if (alg_name == "pbkdf2" || alg_name == "hkdf") {
            auto state = std::make_shared<CryptoKeyState>();
            state->extractable = args[3].is_truthy();
            state->hash_alg = alg_name;
            state->key_bytes = std::move(key_bytes);
            state->usages = read_usages(args[4]);
            JsValue key = r.make("CryptoKey", state);
            if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {key});
            return undefined();
        }
        if (alg_name == "aes-ctr" || alg_name == "aes-gcm" || alg_name == "aes-cbc" || alg_name == "aes-kw") {
            if (key_bytes.size() != 16u && key_bytes.size() != 24u && key_bytes.size() != 32u) {
                return reject_with("DataError", "SubtleCrypto.importKey: AES key must be 128, 192, or 256 bits.");
            }
            if (fmt == "jwk" && !jwk_alg.empty()) {
                std::string bits = std::to_string(key_bytes.size() * 8u);
                std::string expected = "A" + bits + (alg_name == "aes-gcm" ? "GCM" : alg_name == "aes-cbc" ? "CBC" : alg_name == "aes-ctr" ? "CTR" : "KW");
                if (jwk_alg != expected) {
                    return reject_with("DataError", "SubtleCrypto.importKey: JWK alg '" + jwk_alg + "' does not match " + expected + ".");
                }
            }
            auto state = std::make_shared<CryptoKeyState>();
            state->extractable = args[3].is_truthy();
            state->hash_alg = alg_name;
            state->key_bytes = std::move(key_bytes);
            state->usages = read_usages(args[4]);
            JsValue key = r.make("CryptoKey", state);
            if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {key});
            return undefined();
        }
        if (alg_name != "hmac" || !alg_val.is_object_like()) {
            return reject_with("NotSupportedError", "SubtleCrypto.importKey: only HMAC, PBKDF2, HKDF, AES-CTR, and AES-GCM are supported.");
        }
        JsValue hash_val = alg_val.get_property("hash");
        std::string hash_alg = hash_val.is_object_like() ? ascii_lower_str(to_str(hash_val.get_property("name")))
                                                         : ascii_lower_str(to_str(hash_val));
        if (hash_alg != "sha-1" && hash_alg != "sha-256" && hash_alg != "sha-384" && hash_alg != "sha-512") {
            return reject_with("NotSupportedError", "SubtleCrypto.importKey: unsupported HMAC hash '" + hash_alg + "'.");
        }
        if (fmt == "jwk" && !jwk_alg.empty()) {
            std::string expected = hash_alg == "sha-1" ? "HS1"
                                 : hash_alg == "sha-256" ? "HS256"
                                 : hash_alg == "sha-384" ? "HS384" : "HS512";
            if (jwk_alg != expected) {
                return reject_with("DataError", "SubtleCrypto.importKey: JWK alg '" + jwk_alg + "' does not match " + expected + ".");
            }
        }
        auto state = std::make_shared<CryptoKeyState>();
        state->extractable = args[3].is_truthy();
        state->hash_alg = hash_alg;
        state->key_bytes = std::move(key_bytes);
        JsValue key = r.make("CryptoKey", state);
        if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {key});
        return undefined();
    });

    I.method("sign", 3, [](Realm& r, Args args) {
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        auto reject_with = [&](const std::string& name, const std::string& msg) {
            JsValue err = make_dom_exception(r, name, msg);
            if (promise.is_function()) return js_call(promise.get_property("reject"), promise, {err});
            return undefined();
        };
        if (args.size() < 3) return reject_with("TypeError", "SubtleCrypto.sign: 3 arguments required.");
        std::string alg = args[0].is_object_like() ? ascii_lower_str(to_str(args[0].get_property("name")))
                                                   : ascii_lower_str(to_str(args[0]));
        if (alg == "ecdsa") {
            CryptoKeyState* key = state_of<CryptoKeyState>(args[1]);
            if (!key || key->hash_alg != "ecdsa" || key->type != "private" || key->key_bytes.size() != 32u) {
                return reject_with("InvalidAccessError", "SubtleCrypto.sign: ECDSA requires a private ECDSA CryptoKey.");
            }
            std::vector<uint8_t> data;
            if (!read_buffer_source(r, args[2], data)) {
                return reject_with("TypeError", "SubtleCrypto.sign: data must be a BufferSource.");
            }
            JsValue hash_val = args[0].is_object_like() ? args[0].get_property("hash") : JsValue();
            std::string h_name = hash_val.is_object_like() ? ascii_lower_str(to_str(hash_val.get_property("name")))
                                                           : ascii_lower_str(to_str(hash_val));
            if (h_name.empty()) h_name = "sha-256";
            std::vector<uint8_t> digest;
            if (h_name == "sha-1") {
                auto a = sha1_bytes(data.data(), data.size());
                digest.assign(32u, 0u);
                std::copy(a.begin(), a.end(), digest.begin() + 12);
            }
            else if (h_name == "sha-256") { auto a = sha256_bytes(data.data(), data.size()); digest.assign(a.begin(), a.end()); }
            else if (h_name == "sha-384") digest = sha512_family_bytes(data.data(), data.size(), true);
            else if (h_name == "sha-512") digest = sha512_family_bytes(data.data(), data.size(), false);
            else return reject_with("NotSupportedError", "SubtleCrypto.sign: unsupported ECDSA hash '" + h_name + "'.");
            uint8_t sig[64]{};
            for (int attempt = 0; attempt < 16; ++attempt) {
                uint8_t k_be[32];
                fill_random_bytes(k_be, 32u);
                k_be[0] &= 0x7fu;
                k_be[31] |= 0x01u;
                if (p256::ecdsa_sign(key->key_bytes.data(), digest.data(), sig, k_be)) break;
            }
            JsValue u8_ctor = r.window.is_object() ? r.window.get_property("Uint8Array") : JsValue();
            JsValue ab_ctor = r.window.is_object() ? r.window.get_property("ArrayBuffer") : JsValue();
            JsValue out_ab = js_construct(ab_ctor, {num(64.0)});
            JsValue out_u8 = js_construct(u8_ctor, {out_ab});
            for (std::size_t i = 0; i < 64u; ++i) {
                out_u8.set_property(std::to_string(i), num(static_cast<double>(sig[i])));
            }
            if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {out_ab});
            return undefined();
        }
        if (alg != "hmac") return reject_with("NotSupportedError", "SubtleCrypto.sign: only HMAC and ECDSA are supported.");
        CryptoKeyState* key = state_of<CryptoKeyState>(args[1]);
        if (!key) return reject_with("TypeError", "SubtleCrypto.sign: key must be a CryptoKey.");
        std::vector<uint8_t> data;
        if (!read_buffer_source(r, args[2], data)) {
            return reject_with("TypeError", "SubtleCrypto.sign: data must be a BufferSource.");
        }
        std::vector<uint8_t> tag = hmac_bytes(key->hash_alg, key->key_bytes, data);
        JsValue u8_ctor = r.window.is_object() ? r.window.get_property("Uint8Array") : JsValue();
        JsValue ab_ctor = r.window.is_object() ? r.window.get_property("ArrayBuffer") : JsValue();
        JsValue out_ab = js_construct(ab_ctor, {num(static_cast<double>(tag.size()))});
        JsValue out_u8 = js_construct(u8_ctor, {out_ab});
        for (std::size_t i = 0; i < tag.size(); ++i) {
            out_u8.set_property(std::to_string(i), num(static_cast<double>(tag[i])));
        }
        if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {out_ab});
        return undefined();
    });

    I.method("verify", 4, [](Realm& r, Args args) {
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        auto reject_with = [&](const std::string& name, const std::string& msg) {
            JsValue err = make_dom_exception(r, name, msg);
            if (promise.is_function()) return js_call(promise.get_property("reject"), promise, {err});
            return undefined();
        };
        if (args.size() < 4) return reject_with("TypeError", "SubtleCrypto.verify: 4 arguments required.");
        std::string alg = args[0].is_object_like() ? ascii_lower_str(to_str(args[0].get_property("name")))
                                                   : ascii_lower_str(to_str(args[0]));
        if (alg == "ecdsa") {
            CryptoKeyState* key = state_of<CryptoKeyState>(args[1]);
            if (!key || key->hash_alg != "ecdsa" || key->type != "public" || key->key_bytes.size() != 65u) {
                return reject_with("InvalidAccessError", "SubtleCrypto.verify: ECDSA requires a public ECDSA CryptoKey.");
            }
            std::vector<uint8_t> sig, data;
            if (!read_buffer_source(r, args[2], sig) || !read_buffer_source(r, args[3], data)) {
                return reject_with("TypeError", "SubtleCrypto.verify: signature and data must be BufferSources.");
            }
            JsValue hash_val = args[0].is_object_like() ? args[0].get_property("hash") : JsValue();
            std::string h_name = hash_val.is_object_like() ? ascii_lower_str(to_str(hash_val.get_property("name")))
                                                           : ascii_lower_str(to_str(hash_val));
            if (h_name.empty()) h_name = "sha-256";
            std::vector<uint8_t> digest;
            if (h_name == "sha-1") {
                auto a = sha1_bytes(data.data(), data.size());
                digest.assign(32u, 0u);
                std::copy(a.begin(), a.end(), digest.begin() + 12);
            }
            else if (h_name == "sha-256") { auto a = sha256_bytes(data.data(), data.size()); digest.assign(a.begin(), a.end()); }
            else if (h_name == "sha-384") digest = sha512_family_bytes(data.data(), data.size(), true);
            else if (h_name == "sha-512") digest = sha512_family_bytes(data.data(), data.size(), false);
            else return reject_with("NotSupportedError", "SubtleCrypto.verify: unsupported ECDSA hash '" + h_name + "'.");
            bool ok = false;
            if (sig.size() == 64u) {
                ok = p256::ecdsa_verify(key->key_bytes.data(), digest.data(), sig.data());
            }
            if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {boolean(ok)});
            return undefined();
        }
        if (alg != "hmac") return reject_with("NotSupportedError", "SubtleCrypto.verify: only HMAC and ECDSA are supported.");
        CryptoKeyState* key = state_of<CryptoKeyState>(args[1]);
        if (!key) return reject_with("TypeError", "SubtleCrypto.verify: key must be a CryptoKey.");
        std::vector<uint8_t> sig, data;
        if (!read_buffer_source(r, args[2], sig) || !read_buffer_source(r, args[3], data)) {
            return reject_with("TypeError", "SubtleCrypto.verify: signature and data must be BufferSources.");
        }
        std::vector<uint8_t> expected = hmac_bytes(key->hash_alg, key->key_bytes, data);
        bool ok = (sig.size() == expected.size());
        if (ok) {
            uint8_t diff = 0;
            for (std::size_t i = 0; i < expected.size(); ++i) diff |= static_cast<uint8_t>(sig[i] ^ expected[i]);
            ok = (diff == 0);
        }
        if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {boolean(ok)});
        return undefined();
    });

    I.method("exportKey", 2, [](Realm& r, Args args) {
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        auto reject_with = [&](const std::string& name, const std::string& msg) {
            JsValue err = make_dom_exception(r, name, msg);
            if (promise.is_function()) return js_call(promise.get_property("reject"), promise, {err});
            return undefined();
        };
        if (args.size() < 2) return reject_with("TypeError", "SubtleCrypto.exportKey: 2 arguments required.");
        std::string fmt = to_str(args[0]);
        if (fmt != "raw" && fmt != "jwk" && fmt != "spki" && fmt != "pkcs8") {
            return reject_with("NotSupportedError", "SubtleCrypto.exportKey: only 'raw', 'jwk', 'spki', and 'pkcs8' formats are supported.");
        }
        CryptoKeyState* key = state_of<CryptoKeyState>(args[1]);
        if (!key) return reject_with("TypeError", "SubtleCrypto.exportKey: key must be a CryptoKey.");
        if (!key->extractable) {
            return reject_with("InvalidAccessError", "SubtleCrypto.exportKey: key is not extractable.");
        }
        if (fmt == "spki") {
            if ((key->hash_alg != "ecdh" && key->hash_alg != "ecdsa") || key->type != "public" || key->key_bytes.size() != 65u) {
                return reject_with("InvalidAccessError", "SubtleCrypto.exportKey: 'spki' requires a public EC key.");
            }
            static constexpr uint8_t kSpkiPrefix[26] = {
                0x30,0x59,0x30,0x13,0x06,0x07,0x2a,0x86,0x48,0xce,0x3d,0x02,0x01,
                0x06,0x08,0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07,0x03,0x42,0x00
            };
            JsValue u8_ctor = r.window.is_object() ? r.window.get_property("Uint8Array") : JsValue();
            JsValue ab_ctor = r.window.is_object() ? r.window.get_property("ArrayBuffer") : JsValue();
            JsValue out_ab = js_construct(ab_ctor, {num(91.0)});
            JsValue out_u8 = js_construct(u8_ctor, {out_ab});
            for (std::size_t i = 0; i < 26u; ++i) out_u8.set_property(std::to_string(i), num(static_cast<double>(kSpkiPrefix[i])));
            for (std::size_t i = 0; i < 65u; ++i) out_u8.set_property(std::to_string(26u + i), num(static_cast<double>(key->key_bytes[i])));
            if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {out_ab});
            return undefined();
        }
        if (fmt == "pkcs8") {
            if ((key->hash_alg != "ecdh" && key->hash_alg != "ecdsa") || key->type != "private" || key->key_bytes.size() != 32u) {
                return reject_with("InvalidAccessError", "SubtleCrypto.exportKey: 'pkcs8' requires a private EC key.");
            }
            static constexpr uint8_t kPkcs8Prefix[35] = {
                0x30,0x41,0x02,0x01,0x00,0x30,0x13,0x06,0x07,0x2a,0x86,0x48,0xce,0x3d,0x02,0x01,
                0x06,0x08,0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07,0x04,0x27,0x30,0x25,0x02,0x01,0x01,0x04,0x20
            };
            JsValue u8_ctor = r.window.is_object() ? r.window.get_property("Uint8Array") : JsValue();
            JsValue ab_ctor = r.window.is_object() ? r.window.get_property("ArrayBuffer") : JsValue();
            JsValue out_ab = js_construct(ab_ctor, {num(67.0)});
            JsValue out_u8 = js_construct(u8_ctor, {out_ab});
            for (std::size_t i = 0; i < 35u; ++i) out_u8.set_property(std::to_string(i), num(static_cast<double>(kPkcs8Prefix[i])));
            for (std::size_t i = 0; i < 32u; ++i) out_u8.set_property(std::to_string(35u + i), num(static_cast<double>(key->key_bytes[i])));
            if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {out_ab});
            return undefined();
        }
        if (fmt == "jwk") {
            JsValue obj_ctor = r.window.is_object() ? r.window.get_property("Object") : JsValue();
            JsValue arr_ctor = r.window.is_object() ? r.window.get_property("Array") : JsValue();
            JsValue jwk = js_construct(obj_ctor, {});
            JsValue key_ops = js_construct(arr_ctor, {});
            for (std::size_t i = 0; i < key->usages.size(); ++i) {
                key_ops.set_property(std::to_string(i), str(key->usages[i]));
            }
            jwk.set_property("key_ops", key_ops);
            if (key->hash_alg == "ecdh" || key->hash_alg == "ecdsa") {
                jwk.set_property("kty", str("EC"));
                jwk.set_property("crv", str("P-256"));
                if (key->hash_alg == "ecdsa") jwk.set_property("alg", str("ES256"));
                jwk.set_property("ext", boolean(key->extractable));
                if (key->type == "public" && key->key_bytes.size() == 65u) {
                    jwk.set_property("x", str(base64url_encode(std::vector<uint8_t>(key->key_bytes.begin() + 1, key->key_bytes.begin() + 33))));
                    jwk.set_property("y", str(base64url_encode(std::vector<uint8_t>(key->key_bytes.begin() + 33, key->key_bytes.end()))));
                } else if (key->type == "private" && key->key_bytes.size() == 32u) {
                    p256::Fe rx, ry;
                    p256::scalar_mul(key->key_bytes.data(), p256::kGx, p256::kGy, rx, ry);
                    std::vector<uint8_t> xb(32), yb(32);
                    p256::fe_to_be(rx, xb.data());
                    p256::fe_to_be(ry, yb.data());
                    jwk.set_property("x", str(base64url_encode(xb)));
                    jwk.set_property("y", str(base64url_encode(yb)));
                    jwk.set_property("d", str(base64url_encode(key->key_bytes)));
                }
                if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {jwk});
                return undefined();
            }
            jwk.set_property("kty", str("oct"));
            jwk.set_property("k", str(base64url_encode(key->key_bytes)));
            jwk.set_property("ext", boolean(key->extractable));
            if (key->hash_alg == "sha-1") jwk.set_property("alg", str("HS1"));
            else if (key->hash_alg == "sha-256") jwk.set_property("alg", str("HS256"));
            else if (key->hash_alg == "sha-384") jwk.set_property("alg", str("HS384"));
            else if (key->hash_alg == "sha-512") jwk.set_property("alg", str("HS512"));
            else if (key->hash_alg == "aes-gcm") {
                jwk.set_property("alg", str(key->key_bytes.size() == 16u ? "A128GCM" : key->key_bytes.size() == 24u ? "A192GCM" : "A256GCM"));
            } else if (key->hash_alg == "aes-cbc") {
                jwk.set_property("alg", str(key->key_bytes.size() == 16u ? "A128CBC" : key->key_bytes.size() == 24u ? "A192CBC" : "A256CBC"));
            } else if (key->hash_alg == "aes-ctr") {
                jwk.set_property("alg", str(key->key_bytes.size() == 16u ? "A128CTR" : key->key_bytes.size() == 24u ? "A192CTR" : "A256CTR"));
            } else if (key->hash_alg == "aes-kw") {
                jwk.set_property("alg", str(key->key_bytes.size() == 16u ? "A128KW" : key->key_bytes.size() == 24u ? "A192KW" : "A256KW"));
            }
            if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {jwk});
            return undefined();
        }
        JsValue u8_ctor = r.window.is_object() ? r.window.get_property("Uint8Array") : JsValue();
        JsValue ab_ctor = r.window.is_object() ? r.window.get_property("ArrayBuffer") : JsValue();
        JsValue out_ab = js_construct(ab_ctor, {num(static_cast<double>(key->key_bytes.size()))});
        JsValue out_u8 = js_construct(u8_ctor, {out_ab});
        for (std::size_t i = 0; i < key->key_bytes.size(); ++i) {
            out_u8.set_property(std::to_string(i), num(static_cast<double>(key->key_bytes[i])));
        }
        if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {out_ab});
        return undefined();
    });

    I.method("generateKey", 3, [](Realm& r, Args args) {
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        auto reject_with = [&](const std::string& name, const std::string& msg) {
            JsValue err = make_dom_exception(r, name, msg);
            if (promise.is_function()) return js_call(promise.get_property("reject"), promise, {err});
            return undefined();
        };
        if (args.size() < 3) return reject_with("TypeError", "SubtleCrypto.generateKey: 3 arguments required.");
        const JsValue& alg_val = args[0];
        std::string alg_name = alg_val.is_object_like() ? ascii_lower_str(to_str(alg_val.get_property("name")))
                                                        : ascii_lower_str(to_str(alg_val));
        if (alg_name == "ecdh" || alg_name == "ecdsa") {
            std::string curve = alg_val.is_object_like() ? ascii_lower_str(to_str(alg_val.get_property("namedCurve"))) : "";
            if (curve != "p-256") {
                return reject_with("NotSupportedError", "SubtleCrypto.generateKey: only P-256 is supported.");
            }
            uint8_t priv[32];
            fill_random_bytes(priv, 32u);
            priv[0] &= 0x7fu; // ensure < n
            priv[31] |= 0x01u; // ensure != 0
            p256::Fe rx, ry;
            p256::scalar_mul(priv, p256::kGx, p256::kGy, rx, ry);
            std::vector<uint8_t> pub(65u);
            pub[0] = 0x04u;
            p256::fe_to_be(rx, &pub[1]);
            p256::fe_to_be(ry, &pub[33]);

            auto priv_state = std::make_shared<CryptoKeyState>();
            priv_state->type = "private";
            priv_state->extractable = args[1].is_truthy();
            priv_state->hash_alg = alg_name;
            priv_state->key_bytes.assign(priv, priv + 32);
            priv_state->usages = read_usages(args[2]);

            auto pub_state = std::make_shared<CryptoKeyState>();
            pub_state->type = "public";
            pub_state->extractable = true;
            pub_state->hash_alg = alg_name;
            pub_state->key_bytes = std::move(pub);
            pub_state->usages = read_usages(args[2]);

            JsValue obj_ctor = r.window.is_object() ? r.window.get_property("Object") : JsValue();
            JsValue pair = js_construct(obj_ctor, {});
            pair.set_property("privateKey", r.make("CryptoKey", priv_state));
            pair.set_property("publicKey", r.make("CryptoKey", pub_state));
            if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {pair});
            return undefined();
        }
        if ((alg_name == "aes-ctr" || alg_name == "aes-gcm" || alg_name == "aes-cbc" || alg_name == "aes-kw") && alg_val.is_object_like()) {
            const int len_bits = static_cast<int>(to_double(alg_val.get_property("length")));
            if (len_bits != 128 && len_bits != 192 && len_bits != 256) {
                return reject_with("OperationError", "SubtleCrypto.generateKey: AES length must be 128, 192, or 256.");
            }
            auto state = std::make_shared<CryptoKeyState>();
            state->extractable = args[1].is_truthy();
            state->hash_alg = alg_name;
            state->key_bytes.resize(static_cast<std::size_t>(len_bits / 8));
            fill_random_bytes(state->key_bytes.data(), state->key_bytes.size());
            state->usages = read_usages(args[2]);
            JsValue key = r.make("CryptoKey", state);
            if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {key});
            return undefined();
        }
        if (alg_name != "hmac" || !alg_val.is_object_like()) {
            return reject_with("NotSupportedError", "SubtleCrypto.generateKey: only HMAC, AES-CTR, and AES-GCM are supported.");
        }
        JsValue hash_val = alg_val.get_property("hash");
        std::string hash_alg = hash_val.is_object_like() ? ascii_lower_str(to_str(hash_val.get_property("name")))
                                                         : ascii_lower_str(to_str(hash_val));
        if (hash_alg != "sha-1" && hash_alg != "sha-256" && hash_alg != "sha-384" && hash_alg != "sha-512") {
            return reject_with("NotSupportedError", "SubtleCrypto.generateKey: unsupported HMAC hash '" + hash_alg + "'.");
        }
        std::size_t key_bytes_len = (hash_alg == "sha-384" || hash_alg == "sha-512") ? 128u : 64u;
        JsValue len_val = alg_val.get_property("length");
        if (len_val.type == JsType::Number && to_double(len_val) > 0) {
            key_bytes_len = static_cast<std::size_t>((static_cast<int>(to_double(len_val)) + 7) / 8);
        }
        auto state = std::make_shared<CryptoKeyState>();
        state->extractable = args[1].is_truthy();
        state->hash_alg = hash_alg;
        state->key_bytes.resize(key_bytes_len);
        fill_random_bytes(state->key_bytes.data(), state->key_bytes.size());
        JsValue key = r.make("CryptoKey", state);
        if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {key});
        return undefined();
    });

    I.method("deriveBits", 3, [](Realm& r, Args args) {
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        auto reject_with = [&](const std::string& name, const std::string& msg) {
            JsValue err = make_dom_exception(r, name, msg);
            if (promise.is_function()) return js_call(promise.get_property("reject"), promise, {err});
            return undefined();
        };
        if (args.size() < 3) return reject_with("TypeError", "SubtleCrypto.deriveBits: 3 arguments required.");
        const JsValue& alg_val = args[0];
        const std::string alg_name = alg_val.is_object_like()
            ? ascii_lower_str(to_str(alg_val.get_property("name"))) : std::string();
        if (alg_name == "ecdh") {
            CryptoKeyState* base_key = state_of<CryptoKeyState>(args[1]);
            if (!base_key || base_key->hash_alg != "ecdh" || base_key->type != "private" || base_key->key_bytes.size() != 32u) {
                return reject_with("InvalidAccessError", "SubtleCrypto.deriveBits: ECDH requires a private ECDH CryptoKey.");
            }
            CryptoKeyState* pub_key = state_of<CryptoKeyState>(alg_val.get_property("public"));
            if (!pub_key || pub_key->hash_alg != "ecdh" || pub_key->type != "public" || pub_key->key_bytes.size() != 65u) {
                return reject_with("InvalidAccessError", "SubtleCrypto.deriveBits: ECDH public key must be a public ECDH CryptoKey.");
            }
            const int len_bits = static_cast<int>(to_double(args[2]));
            if (len_bits < 0 || (len_bits % 8) != 0 || len_bits > 256) {
                return reject_with("OperationError", "SubtleCrypto.deriveBits: ECDH P-256 length must be 0..256 and a multiple of 8.");
            }
            p256::Fe px, py, sx, sy;
            p256::be_to_fe(&pub_key->key_bytes[1], px);
            p256::be_to_fe(&pub_key->key_bytes[33], py);
            p256::scalar_mul(base_key->key_bytes.data(), px, py, sx, sy);
            uint8_t shared[32];
            p256::fe_to_be(sx, shared);
            const std::size_t out_bytes = static_cast<std::size_t>(len_bits / 8);
            JsValue u8_ctor = r.window.is_object() ? r.window.get_property("Uint8Array") : JsValue();
            JsValue ab_ctor = r.window.is_object() ? r.window.get_property("ArrayBuffer") : JsValue();
            JsValue out_ab = js_construct(ab_ctor, {num(static_cast<double>(out_bytes))});
            JsValue out_u8 = js_construct(u8_ctor, {out_ab});
            for (std::size_t i = 0; i < out_bytes; ++i) {
                out_u8.set_property(std::to_string(i), num(static_cast<double>(shared[i])));
            }
            if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {out_ab});
            return undefined();
        }
        if (alg_name != "pbkdf2" && alg_name != "hkdf") {
            return reject_with("NotSupportedError", "SubtleCrypto.deriveBits: only PBKDF2 and HKDF are supported.");
        }
        CryptoKeyState* base_key = state_of<CryptoKeyState>(args[1]);
        if (!base_key || base_key->hash_alg != alg_name) {
            return reject_with("InvalidAccessError", "SubtleCrypto.deriveBits: baseKey algorithm mismatch.");
        }
        std::vector<uint8_t> salt;
        if (!read_buffer_source(r, alg_val.get_property("salt"), salt)) {
            return reject_with("TypeError", "SubtleCrypto.deriveBits: salt must be a BufferSource.");
        }
        JsValue hash_val = alg_val.get_property("hash");
        std::string hash_alg = hash_val.is_object_like() ? ascii_lower_str(to_str(hash_val.get_property("name")))
                                                         : ascii_lower_str(to_str(hash_val));
        if (hash_alg != "sha-1" && hash_alg != "sha-256" && hash_alg != "sha-384" && hash_alg != "sha-512") {
            return reject_with("NotSupportedError", "SubtleCrypto.deriveBits: unsupported hash '" + hash_alg + "'.");
        }
        const double len_bits_d = to_double(args[2]);
        const int len_bits = static_cast<int>(len_bits_d);
        if (len_bits < 0 || (len_bits % 8) != 0) {
            return reject_with("OperationError", "SubtleCrypto.deriveBits: length must be a non-negative multiple of 8.");
        }
        const std::size_t dk_len = static_cast<std::size_t>(len_bits / 8);
        std::vector<uint8_t> derived;
        if (alg_name == "pbkdf2") {
            const double iter_d = to_double(alg_val.get_property("iterations"));
            if (!(iter_d >= 1.0)) {
                return reject_with("OperationError", "SubtleCrypto.deriveBits: iterations must be >= 1.");
            }
            derived = pbkdf2_bytes(hash_alg, base_key->key_bytes, salt, static_cast<uint32_t>(iter_d), dk_len);
        } else {
            std::vector<uint8_t> info;
            if (!read_buffer_source(r, alg_val.get_property("info"), info)) {
                return reject_with("TypeError", "SubtleCrypto.deriveBits: info must be a BufferSource.");
            }
            derived = hkdf_bytes(hash_alg, base_key->key_bytes, salt, info, dk_len);
        }
        JsValue u8_ctor = r.window.is_object() ? r.window.get_property("Uint8Array") : JsValue();
        JsValue ab_ctor = r.window.is_object() ? r.window.get_property("ArrayBuffer") : JsValue();
        JsValue out_ab = js_construct(ab_ctor, {num(static_cast<double>(derived.size()))});
        JsValue out_u8 = js_construct(u8_ctor, {out_ab});
        for (std::size_t i = 0; i < derived.size(); ++i) {
            out_u8.set_property(std::to_string(i), num(static_cast<double>(derived[i])));
        }
        if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {out_ab});
        return undefined();
    });

    I.method("deriveKey", 5, [](Realm& r, Args args) {
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        auto reject_with = [&](const std::string& name, const std::string& msg) {
            JsValue err = make_dom_exception(r, name, msg);
            if (promise.is_function()) return js_call(promise.get_property("reject"), promise, {err});
            return undefined();
        };
        if (args.size() < 5) return reject_with("TypeError", "SubtleCrypto.deriveKey: 5 arguments required.");
        const JsValue& alg_val = args[0];
        const std::string alg_name = alg_val.is_object_like()
            ? ascii_lower_str(to_str(alg_val.get_property("name"))) : std::string();
        if (alg_name == "ecdh") {
            CryptoKeyState* base_key = state_of<CryptoKeyState>(args[1]);
            if (!base_key || base_key->hash_alg != "ecdh" || base_key->type != "private" || base_key->key_bytes.size() != 32u) {
                return reject_with("InvalidAccessError", "SubtleCrypto.deriveKey: ECDH requires a private ECDH CryptoKey.");
            }
            CryptoKeyState* pub_key = state_of<CryptoKeyState>(alg_val.get_property("public"));
            if (!pub_key || pub_key->hash_alg != "ecdh" || pub_key->type != "public" || pub_key->key_bytes.size() != 65u) {
                return reject_with("InvalidAccessError", "SubtleCrypto.deriveKey: ECDH public key must be a public ECDH CryptoKey.");
            }
            const JsValue& target_alg = args[2];
            const std::string target_name = target_alg.is_object_like()
                ? ascii_lower_str(to_str(target_alg.get_property("name"))) : std::string();
            int len_bits = 0;
            std::string key_hash_alg;
            if (target_name == "aes-gcm" || target_name == "aes-cbc" || target_name == "aes-ctr" || target_name == "aes-kw") {
                len_bits = static_cast<int>(to_double(target_alg.get_property("length")));
                if (len_bits != 128 && len_bits != 192 && len_bits != 256) {
                    return reject_with("OperationError", "SubtleCrypto.deriveKey: AES key length must be 128, 192, or 256.");
                }
                key_hash_alg = target_name;
            } else if (target_name == "hmac") {
                JsValue th = target_alg.get_property("hash");
                std::string th_name = th.is_object_like() ? ascii_lower_str(to_str(th.get_property("name")))
                                                          : ascii_lower_str(to_str(th));
                if (th_name != "sha-1" && th_name != "sha-256" && th_name != "sha-384" && th_name != "sha-512") {
                    return reject_with("NotSupportedError", "SubtleCrypto.deriveKey: unsupported HMAC hash.");
                }
                len_bits = 256;
                key_hash_alg = th_name;
            } else {
                return reject_with("NotSupportedError", "SubtleCrypto.deriveKey: unsupported derivedKeyType '" + target_name + "'.");
            }
            p256::Fe px, py, sx, sy;
            p256::be_to_fe(&pub_key->key_bytes[1], px);
            p256::be_to_fe(&pub_key->key_bytes[33], py);
            p256::scalar_mul(base_key->key_bytes.data(), px, py, sx, sy);
            uint8_t shared[32];
            p256::fe_to_be(sx, shared);
            auto state = std::make_shared<CryptoKeyState>();
            state->extractable = args[3].is_truthy();
            state->hash_alg = key_hash_alg;
            state->key_bytes.assign(shared, shared + static_cast<std::size_t>(len_bits / 8));
            state->usages = read_usages(args[4]);
            JsValue key = r.make("CryptoKey", state);
            if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {key});
            return undefined();
        }
        if (alg_name != "pbkdf2" && alg_name != "hkdf") {
            return reject_with("NotSupportedError", "SubtleCrypto.deriveKey: only PBKDF2, HKDF, and ECDH are supported.");
        }
        CryptoKeyState* base_key = state_of<CryptoKeyState>(args[1]);
        if (!base_key || base_key->hash_alg != alg_name) {
            return reject_with("InvalidAccessError", "SubtleCrypto.deriveKey: baseKey algorithm mismatch.");
        }
        std::vector<uint8_t> salt;
        if (!read_buffer_source(r, alg_val.get_property("salt"), salt)) {
            return reject_with("TypeError", "SubtleCrypto.deriveKey: salt must be a BufferSource.");
        }
        JsValue hash_val = alg_val.get_property("hash");
        std::string hash_alg = hash_val.is_object_like() ? ascii_lower_str(to_str(hash_val.get_property("name")))
                                                         : ascii_lower_str(to_str(hash_val));
        if (hash_alg != "sha-1" && hash_alg != "sha-256" && hash_alg != "sha-384" && hash_alg != "sha-512") {
            return reject_with("NotSupportedError", "SubtleCrypto.deriveKey: unsupported hash '" + hash_alg + "'.");
        }
        const JsValue& derived_type = args[2];
        std::string derived_name = derived_type.is_object_like() ? ascii_lower_str(to_str(derived_type.get_property("name"))) : std::string();
        if (derived_name != "hmac" && derived_name != "aes-kw") {
            return reject_with("NotSupportedError", "SubtleCrypto.deriveKey: only HMAC and AES-KW derivedKeyType are supported.");
        }
        std::string dhash_alg;
        std::size_t dk_len = 0;
        if (derived_name == "aes-kw") {
            const int len_bits = static_cast<int>(to_double(derived_type.get_property("length")));
            if (len_bits != 128 && len_bits != 192 && len_bits != 256) {
                return reject_with("OperationError", "SubtleCrypto.deriveKey: AES-KW key length must be 128, 192, or 256.");
            }
            dhash_alg = "aes-kw";
            dk_len = static_cast<std::size_t>(len_bits / 8);
        } else {
            JsValue dhash_val = derived_type.get_property("hash");
            dhash_alg = dhash_val.is_object_like() ? ascii_lower_str(to_str(dhash_val.get_property("name")))
                                                   : ascii_lower_str(to_str(dhash_val));
            if (dhash_alg != "sha-1" && dhash_alg != "sha-256" && dhash_alg != "sha-384" && dhash_alg != "sha-512") {
                return reject_with("NotSupportedError", "SubtleCrypto.deriveKey: unsupported HMAC hash '" + dhash_alg + "'.");
            }
            dk_len = (dhash_alg == "sha-384" || dhash_alg == "sha-512") ? 128u : 64u;
            JsValue len_val = derived_type.get_property("length");
            if (len_val.type == JsType::Number && to_double(len_val) > 0) {
                dk_len = static_cast<std::size_t>((static_cast<int>(to_double(len_val)) + 7) / 8);
            }
        }
        auto state = std::make_shared<CryptoKeyState>();
        state->extractable = args[3].is_truthy();
        state->hash_alg = dhash_alg;
        if (alg_name == "pbkdf2") {
            const double iter_d = to_double(alg_val.get_property("iterations"));
            if (!(iter_d >= 1.0)) {
                return reject_with("OperationError", "SubtleCrypto.deriveKey: iterations must be >= 1.");
            }
            state->key_bytes = pbkdf2_bytes(hash_alg, base_key->key_bytes, salt, static_cast<uint32_t>(iter_d), dk_len);
        } else {
            std::vector<uint8_t> info;
            if (!read_buffer_source(r, alg_val.get_property("info"), info)) {
                return reject_with("TypeError", "SubtleCrypto.deriveKey: info must be a BufferSource.");
            }
            state->key_bytes = hkdf_bytes(hash_alg, base_key->key_bytes, salt, info, dk_len);
        }
        JsValue key = r.make("CryptoKey", state);
        if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {key});
        return undefined();
    });

    auto aes_cipher_op = [](Realm& r, Args args, bool is_encrypt) -> JsValue {
        const char* op_name = is_encrypt ? "encrypt" : "decrypt";
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        auto reject_with = [&](const std::string& name, const std::string& msg) {
            JsValue err = make_dom_exception(r, name, msg);
            if (promise.is_function()) return js_call(promise.get_property("reject"), promise, {err});
            return undefined();
        };
        if (args.size() < 3) return reject_with("TypeError", std::string("SubtleCrypto.") + op_name + ": 3 arguments required.");
        const JsValue& alg_val = args[0];
        const std::string alg_name = alg_val.is_object_like()
            ? ascii_lower_str(to_str(alg_val.get_property("name"))) : std::string();
        if (alg_name != "aes-ctr" && alg_name != "aes-gcm" && alg_name != "aes-cbc" && alg_name != "aes-kw") {
            return reject_with("NotSupportedError", std::string("SubtleCrypto.") + op_name + ": only AES-CTR, AES-GCM, AES-CBC, and AES-KW are supported.");
        }
        CryptoKeyState* key = state_of<CryptoKeyState>(args[1]);
        if (!key || key->hash_alg != alg_name) {
            return reject_with("InvalidAccessError", std::string("SubtleCrypto.") + op_name + ": key algorithm mismatch.");
        }
        std::vector<uint8_t> data;
        if (!read_buffer_source(r, args[2], data)) {
            return reject_with("TypeError", std::string("SubtleCrypto.") + op_name + ": data must be a BufferSource.");
        }
        std::vector<uint8_t> out;
        if (alg_name == "aes-ctr") {
            std::vector<uint8_t> counter;
            if (!read_buffer_source(r, alg_val.get_property("counter"), counter) || counter.size() != 16u) {
                return reject_with("OperationError", std::string("SubtleCrypto.") + op_name + ": counter must be 16 bytes.");
            }
            const int length_bits = static_cast<int>(to_double(alg_val.get_property("length")));
            if (length_bits < 1 || length_bits > 128) {
                return reject_with("OperationError", std::string("SubtleCrypto.") + op_name + ": counter length must be 1..128.");
            }
            out = aes_ctr_crypt(key->key_bytes, counter, length_bits, data);
        } else if (alg_name == "aes-cbc") {
            std::vector<uint8_t> iv;
            if (!read_buffer_source(r, alg_val.get_property("iv"), iv) || iv.size() != 16u) {
                return reject_with("OperationError", std::string("SubtleCrypto.") + op_name + ": AES-CBC iv must be 16 bytes.");
            }
            if (is_encrypt) {
                out = aes_cbc_encrypt(key->key_bytes, iv, data);
            } else if (!aes_cbc_decrypt(key->key_bytes, iv, data, out)) {
                return reject_with("OperationError", "SubtleCrypto.decrypt: AES-CBC invalid padding or ciphertext.");
            }
        } else if (alg_name == "aes-kw") {
            if (is_encrypt) {
                if (data.size() < 16u || (data.size() % 8u) != 0) {
                    return reject_with("OperationError", "SubtleCrypto.wrapKey: AES-KW plaintext must be >= 16 bytes and a multiple of 8.");
                }
                out = aes_kw_wrap(key->key_bytes, data);
            } else if (!aes_kw_unwrap(key->key_bytes, data, out)) {
                return reject_with("OperationError", "SubtleCrypto.unwrapKey: AES-KW integrity check failed.");
            }
        } else {
            std::vector<uint8_t> iv, aad;
            if (!read_buffer_source(r, alg_val.get_property("iv"), iv) || iv.empty()) {
                return reject_with("OperationError", std::string("SubtleCrypto.") + op_name + ": iv must be a non-empty BufferSource.");
            }
            JsValue aad_val = alg_val.get_property("additionalData");
            if (!aad_val.is_undefined() && !read_buffer_source(r, aad_val, aad)) {
                return reject_with("TypeError", std::string("SubtleCrypto.") + op_name + ": additionalData must be a BufferSource.");
            }
            int tag_bits = 128;
            JsValue tl_val = alg_val.get_property("tagLength");
            if (!tl_val.is_undefined()) tag_bits = static_cast<int>(to_double(tl_val));
            if (tag_bits != 32 && tag_bits != 64 && tag_bits != 96 && tag_bits != 104 &&
                tag_bits != 112 && tag_bits != 120 && tag_bits != 128) {
                return reject_with("OperationError", std::string("SubtleCrypto.") + op_name + ": invalid tagLength.");
            }
            const std::size_t tag_bytes = static_cast<std::size_t>(tag_bits / 8);
            if (is_encrypt) {
                out = aes_gcm_encrypt(key->key_bytes, iv, aad, data, tag_bytes);
            } else if (!aes_gcm_decrypt(key->key_bytes, iv, aad, data, tag_bytes, out)) {
                return reject_with("OperationError", "SubtleCrypto.decrypt: AES-GCM authentication tag mismatch.");
            }
        }
        JsValue u8_ctor = r.window.is_object() ? r.window.get_property("Uint8Array") : JsValue();
        JsValue ab_ctor = r.window.is_object() ? r.window.get_property("ArrayBuffer") : JsValue();
        JsValue out_ab = js_construct(ab_ctor, {num(static_cast<double>(out.size()))});
        JsValue out_u8 = js_construct(u8_ctor, {out_ab});
        for (std::size_t i = 0; i < out.size(); ++i) {
            out_u8.set_property(std::to_string(i), num(static_cast<double>(out[i])));
        }
        if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {out_ab});
        return undefined();
    };

    I.method("encrypt", 3, [aes_cipher_op](Realm& r, Args args) { return aes_cipher_op(r, args, true); });
    I.method("decrypt", 3, [aes_cipher_op](Realm& r, Args args) { return aes_cipher_op(r, args, false); });

    I.method("wrapKey", 4, [aes_cipher_op](Realm& r, Args args) {
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        auto reject_with = [&](const char* name, const std::string& msg) -> JsValue {
            JsValue err = make_dom_exception(r, msg, name);
            if (promise.is_function()) return js_call(promise.get_property("reject"), promise, {err});
            return undefined();
        };
        if (args.size() < 4) return reject_with("TypeError", "SubtleCrypto.wrapKey: 4 arguments required.");
        std::string fmt = ascii_lower(to_str(args[0]));
        if (fmt != "raw" && fmt != "jwk") {
            return reject_with("NotSupportedError", "SubtleCrypto.wrapKey: only 'raw' and 'jwk' formats are supported.");
        }
        auto* key = state_of<CryptoKeyState>(args[1]);
        auto* wrap_key = state_of<CryptoKeyState>(args[2]);
        if (!key || !wrap_key) return reject_with("TypeError", "SubtleCrypto.wrapKey: CryptoKey required.");
        if (!key->extractable) return reject_with("InvalidAccessError", "SubtleCrypto.wrapKey: key is not extractable.");

        std::vector<uint8_t> payload = key->key_bytes;
        if (fmt == "jwk") {
            JsValue obj_ctor = r.window.is_object() ? r.window.get_property("Object") : JsValue();
            JsValue jwk = js_construct(obj_ctor, {});
            if (key->hash_alg == "ecdh" || key->hash_alg == "ecdsa") {
                jwk.set_property("kty", str("EC"));
                jwk.set_property("crv", str("P-256"));
                jwk.set_property("ext", boolean(key->extractable));
                if (key->type == "public" && key->key_bytes.size() == 65u) {
                    jwk.set_property("x", str(base64url_encode(std::vector<uint8_t>(key->key_bytes.begin() + 1, key->key_bytes.begin() + 33))));
                    jwk.set_property("y", str(base64url_encode(std::vector<uint8_t>(key->key_bytes.begin() + 33, key->key_bytes.end()))));
                } else if (key->type == "private" && key->key_bytes.size() == 32u) {
                    p256::Fe rx, ry;
                    p256::scalar_mul(key->key_bytes.data(), p256::kGx, p256::kGy, rx, ry);
                    std::vector<uint8_t> xb(32), yb(32);
                    p256::fe_to_be(rx, xb.data());
                    p256::fe_to_be(ry, yb.data());
                    jwk.set_property("x", str(base64url_encode(xb)));
                    jwk.set_property("y", str(base64url_encode(yb)));
                    jwk.set_property("d", str(base64url_encode(key->key_bytes)));
                }
            } else {
                jwk.set_property("kty", str("oct"));
                jwk.set_property("k", str(base64url_encode(key->key_bytes)));
                jwk.set_property("ext", boolean(key->extractable));
            }
            JsValue json_obj = r.window.is_object() ? r.window.get_property("JSON") : JsValue();
            std::string json_str = to_str(js_call(json_obj.get_property("stringify"), json_obj, {jwk}));
            payload.assign(json_str.begin(), json_str.end());
        }

        JsValue u8_ctor = r.window.is_object() ? r.window.get_property("Uint8Array") : JsValue();
        JsValue ab_ctor = r.window.is_object() ? r.window.get_property("ArrayBuffer") : JsValue();
        JsValue raw_ab = js_construct(ab_ctor, {num(static_cast<double>(payload.size()))});
        JsValue raw_u8 = js_construct(u8_ctor, {raw_ab});
        for (std::size_t i = 0; i < payload.size(); ++i) {
            raw_u8.set_property(std::to_string(i), num(static_cast<double>(payload[i])));
        }
        std::vector<JsValue> enc_args = {args[3], args[2], raw_ab};
        return aes_cipher_op(r, enc_args, true);
    });

    I.method("unwrapKey", 7, [](Realm& r, Args args) {
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        auto reject_with = [&](const char* name, const std::string& msg) -> JsValue {
            JsValue err = make_dom_exception(r, msg, name);
            if (promise.is_function()) return js_call(promise.get_property("reject"), promise, {err});
            return undefined();
        };
        if (args.size() < 7) return reject_with("TypeError", "SubtleCrypto.unwrapKey: 7 arguments required.");
        std::string fmt = ascii_lower(to_str(args[0]));
        if (fmt != "raw" && fmt != "jwk") {
            return reject_with("NotSupportedError", "SubtleCrypto.unwrapKey: only 'raw' and 'jwk' formats are supported.");
        }
        std::vector<uint8_t> wrapped;
        if (!read_buffer_source(r, args[1], wrapped)) {
            return reject_with("TypeError", "SubtleCrypto.unwrapKey: wrappedKey must be a BufferSource.");
        }
        auto* unwrap_key = state_of<CryptoKeyState>(args[2]);
        if (!unwrap_key) return reject_with("TypeError", "SubtleCrypto.unwrapKey: unwrappingKey must be a CryptoKey.");

        JsValue wrap_alg = args[3];
        std::string wrap_name = ascii_lower(wrap_alg.is_object_like() ? to_str(wrap_alg.get_property("name")) : to_str(wrap_alg));

        std::vector<uint8_t> raw_bytes;
        if (wrap_name == "aes-ctr") {
            std::vector<uint8_t> counter;
            if (!read_buffer_source(r, wrap_alg.get_property("counter"), counter) || counter.size() != 16u) {
                return reject_with("OperationError", "SubtleCrypto.unwrapKey: AES-CTR counter must be 16 bytes.");
            }
            JsValue len_val = wrap_alg.get_property("length");
            double len_d = !len_val.is_undefined() ? len_val.to_number() : 0.0;
            std::size_t length_bits = (len_d >= 1.0 && len_d <= 128.0) ? static_cast<std::size_t>(len_d) : 0u;
            if (length_bits == 0u) return reject_with("OperationError", "SubtleCrypto.unwrapKey: AES-CTR length must be 1..128.");
            raw_bytes = aes_ctr_crypt(unwrap_key->key_bytes, counter, length_bits, wrapped);
        } else if (wrap_name == "aes-cbc") {
            std::vector<uint8_t> iv;
            if (!read_buffer_source(r, wrap_alg.get_property("iv"), iv) || iv.size() != 16u) {
                return reject_with("OperationError", "SubtleCrypto.unwrapKey: AES-CBC iv must be 16 bytes.");
            }
            if (!aes_cbc_decrypt(unwrap_key->key_bytes, iv, wrapped, raw_bytes)) {
                return reject_with("OperationError", "SubtleCrypto.unwrapKey: AES-CBC invalid padding or ciphertext.");
            }
        } else if (wrap_name == "aes-gcm") {
            std::vector<uint8_t> iv;
            if (!read_buffer_source(r, wrap_alg.get_property("iv"), iv) || iv.empty()) {
                return reject_with("OperationError", "SubtleCrypto.unwrapKey: AES-GCM iv must be non-empty.");
            }
            std::vector<uint8_t> aad;
            JsValue aad_val = wrap_alg.get_property("additionalData");
            if (!aad_val.is_nullish()) {
                if (!read_buffer_source(r, aad_val, aad)) {
                    return reject_with("TypeError", "SubtleCrypto.unwrapKey: additionalData must be a BufferSource.");
                }
            }
            std::size_t tag_bits = 128u;
            JsValue tl_val = wrap_alg.get_property("tagLength");
            if (!tl_val.is_undefined()) tag_bits = static_cast<std::size_t>(tl_val.to_number());
            if (tag_bits != 32u && tag_bits != 64u && tag_bits != 96u && tag_bits != 104u &&
                tag_bits != 112u && tag_bits != 120u && tag_bits != 128u) {
                return reject_with("OperationError", "SubtleCrypto.unwrapKey: invalid AES-GCM tagLength.");
            }
            if (!aes_gcm_decrypt(unwrap_key->key_bytes, iv, aad, wrapped, tag_bits / 8u, raw_bytes)) {
                return reject_with("OperationError", "SubtleCrypto.unwrapKey: AES-GCM authentication tag mismatch.");
            }
        } else if (wrap_name == "aes-kw") {
            if (!aes_kw_unwrap(unwrap_key->key_bytes, wrapped, raw_bytes)) {
                return reject_with("OperationError", "SubtleCrypto.unwrapKey: AES-KW integrity check failed.");
            }
        } else {
            return reject_with("NotSupportedError", "SubtleCrypto.unwrapKey: unsupported wrapping algorithm.");
        }

        JsValue target_alg = args[4];
        std::string target_name = ascii_lower(target_alg.is_object_like() ? to_str(target_alg.get_property("name")) : to_str(target_alg));
        std::string canonical_alg;
        if (target_name == "aes-gcm") canonical_alg = "AES-GCM";
        else if (target_name == "aes-cbc") canonical_alg = "AES-CBC";
        else if (target_name == "aes-ctr") canonical_alg = "AES-CTR";
        else if (target_name == "hmac") canonical_alg = "HMAC";
        else if (target_name == "ecdsa") canonical_alg = "ECDSA";
        else if (target_name == "ecdh") canonical_alg = "ECDH";
        else return reject_with("NotSupportedError", "SubtleCrypto.unwrapKey: unsupported unwrappedKeyAlgorithm.");

        std::string ec_type = "secret";
        if (fmt == "jwk") {
            std::string json_str(raw_bytes.begin(), raw_bytes.end());
            JsValue json_obj = r.window.is_object() ? r.window.get_property("JSON") : JsValue();
            JsValue jwk = js_call(json_obj.get_property("parse"), json_obj, {str(json_str)});
            if (!jwk.is_object_like()) {
                return reject_with("DataError", "SubtleCrypto.unwrapKey: invalid JWK payload.");
            }
            std::string kty = to_str(jwk.get_property("kty"));
            if (kty == "EC") {
                std::vector<uint8_t> xb, yb, db;
                if (!base64url_decode(to_str(jwk.get_property("x")), xb) || xb.size() != 32u ||
                    !base64url_decode(to_str(jwk.get_property("y")), yb) || yb.size() != 32u) {
                    return reject_with("DataError", "SubtleCrypto.unwrapKey: invalid EC JWK coordinates.");
                }
                JsValue d_val = jwk.get_property("d");
                if (!d_val.is_undefined() && !d_val.is_nullish()) {
                    if (!base64url_decode(to_str(d_val), db) || db.size() != 32u) {
                        return reject_with("DataError", "SubtleCrypto.unwrapKey: invalid EC JWK 'd'.");
                    }
                    ec_type = "private";
                    raw_bytes = std::move(db);
                } else {
                    ec_type = "public";
                    raw_bytes.clear();
                    raw_bytes.push_back(0x04u);
                    raw_bytes.insert(raw_bytes.end(), xb.begin(), xb.end());
                    raw_bytes.insert(raw_bytes.end(), yb.begin(), yb.end());
                }
            } else if (kty == "oct") {
                if (!base64url_decode(to_str(jwk.get_property("k")), raw_bytes)) {
                    return reject_with("DataError", "SubtleCrypto.unwrapKey: invalid oct JWK 'k'.");
                }
            } else {
                return reject_with("DataError", "SubtleCrypto.unwrapKey: unsupported JWK kty.");
            }
        }

        if ((canonical_alg == "AES-GCM" || canonical_alg == "AES-CBC" || canonical_alg == "AES-CTR") &&
            raw_bytes.size() != 16u && raw_bytes.size() != 24u && raw_bytes.size() != 32u) {
            return reject_with("DataError", "SubtleCrypto.unwrapKey: AES key must be 128, 192, or 256 bits.");
        }

        auto state = std::make_shared<CryptoKeyState>();
        state->type = ec_type;
        state->extractable = args[5].is_truthy();
        state->key_bytes = std::move(raw_bytes);
        if (canonical_alg == "HMAC" && target_alg.is_object_like()) {
            JsValue hash_val = target_alg.get_property("hash");
            std::string h = ascii_lower(hash_val.is_object_like() ? to_str(hash_val.get_property("name")) : to_str(hash_val));
            if (h == "sha-1") state->hash_alg = "sha-1";
            else if (h == "sha-384") state->hash_alg = "sha-384";
            else if (h == "sha-512") state->hash_alg = "sha-512";
            else state->hash_alg = "sha-256";
        } else {
            state->hash_alg = ascii_lower(canonical_alg);
        }
        state->usages = read_usages(args[6]);
        JsValue key_obj = r.make("CryptoKey", state);
        if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {key_obj});
        return key_obj;
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
    register_interface({"CryptoKey", nullptr, setup_crypto_key, nullptr, 0, nullptr});
    register_interface({"SubtleCrypto", nullptr, setup_subtle_crypto, nullptr, 0, nullptr});
    register_interface({"Crypto", nullptr, setup_crypto, nullptr, 0, nullptr});
}

} // namespace dom
} // namespace litebird
