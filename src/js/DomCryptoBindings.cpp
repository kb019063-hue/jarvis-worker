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

struct CryptoKeyState : JsExotic {
    std::string type = "secret";
    bool extractable = false;
    std::string hash_alg;
    std::vector<uint8_t> key_bytes;
};

void setup_crypto_key(Iface& I) {
    I.getter("type", [](Realm&) { return str(this_state<CryptoKeyState>("CryptoKey").type); });
    I.getter("extractable", [](Realm&) { return boolean(this_state<CryptoKeyState>("CryptoKey").extractable); });
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
        ? js_call(u8_ctor, u8_ctor, {src.get_property("buffer"), src.get_property("byteOffset"), src.get_property("byteLength")})
        : js_call(u8_ctor, u8_ctor, {src});
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
            ? js_call(u8_ctor, u8_ctor, {src.get_property("buffer"), src.get_property("byteOffset"), src.get_property("byteLength")})
            : js_call(u8_ctor, u8_ctor, {src});
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
        if (to_str(args[0]) != "raw") {
            return reject_with("NotSupportedError", "SubtleCrypto.importKey: only 'raw' format is supported.");
        }
        std::vector<uint8_t> key_bytes;
        if (!read_buffer_source(r, args[1], key_bytes)) {
            return reject_with("TypeError", "SubtleCrypto.importKey: keyData must be a BufferSource.");
        }
        const JsValue& alg_val = args[2];
        std::string alg_name = alg_val.is_object_like() ? ascii_lower_str(to_str(alg_val.get_property("name")))
                                                        : ascii_lower_str(to_str(alg_val));
        if (alg_name == "pbkdf2" || alg_name == "hkdf") {
            auto state = std::make_shared<CryptoKeyState>();
            state->extractable = args[3].is_truthy();
            state->hash_alg = alg_name;
            state->key_bytes = std::move(key_bytes);
            JsValue key = r.make("CryptoKey", state);
            if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {key});
            return undefined();
        }
        if (alg_name == "aes-ctr") {
            if (key_bytes.size() != 16u && key_bytes.size() != 24u && key_bytes.size() != 32u) {
                return reject_with("DataError", "SubtleCrypto.importKey: AES key must be 128, 192, or 256 bits.");
            }
            auto state = std::make_shared<CryptoKeyState>();
            state->extractable = args[3].is_truthy();
            state->hash_alg = "aes-ctr";
            state->key_bytes = std::move(key_bytes);
            JsValue key = r.make("CryptoKey", state);
            if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {key});
            return undefined();
        }
        if (alg_name != "hmac" || !alg_val.is_object_like()) {
            return reject_with("NotSupportedError", "SubtleCrypto.importKey: only HMAC, PBKDF2, HKDF, and AES-CTR are supported.");
        }
        JsValue hash_val = alg_val.get_property("hash");
        std::string hash_alg = hash_val.is_object_like() ? ascii_lower_str(to_str(hash_val.get_property("name")))
                                                         : ascii_lower_str(to_str(hash_val));
        if (hash_alg != "sha-1" && hash_alg != "sha-256" && hash_alg != "sha-384" && hash_alg != "sha-512") {
            return reject_with("NotSupportedError", "SubtleCrypto.importKey: unsupported HMAC hash '" + hash_alg + "'.");
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
        if (alg != "hmac") return reject_with("NotSupportedError", "SubtleCrypto.sign: only HMAC is supported.");
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
        if (alg != "hmac") return reject_with("NotSupportedError", "SubtleCrypto.verify: only HMAC is supported.");
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
        if (to_str(args[0]) != "raw") {
            return reject_with("NotSupportedError", "SubtleCrypto.exportKey: only 'raw' format is supported.");
        }
        CryptoKeyState* key = state_of<CryptoKeyState>(args[1]);
        if (!key) return reject_with("TypeError", "SubtleCrypto.exportKey: key must be a CryptoKey.");
        if (!key->extractable) {
            return reject_with("InvalidAccessError", "SubtleCrypto.exportKey: key is not extractable.");
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
        if (alg_name == "aes-ctr" && alg_val.is_object_like()) {
            const int len_bits = static_cast<int>(to_double(alg_val.get_property("length")));
            if (len_bits != 128 && len_bits != 192 && len_bits != 256) {
                return reject_with("OperationError", "SubtleCrypto.generateKey: AES length must be 128, 192, or 256.");
            }
            auto state = std::make_shared<CryptoKeyState>();
            state->extractable = args[1].is_truthy();
            state->hash_alg = "aes-ctr";
            state->key_bytes.resize(static_cast<std::size_t>(len_bits / 8));
            fill_random_bytes(state->key_bytes.data(), state->key_bytes.size());
            JsValue key = r.make("CryptoKey", state);
            if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {key});
            return undefined();
        }
        if (alg_name != "hmac" || !alg_val.is_object_like()) {
            return reject_with("NotSupportedError", "SubtleCrypto.generateKey: only HMAC and AES-CTR are supported.");
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
        if (alg_name != "pbkdf2" && alg_name != "hkdf") {
            return reject_with("NotSupportedError", "SubtleCrypto.deriveKey: only PBKDF2 and HKDF are supported.");
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
        if (!derived_type.is_object_like() || ascii_lower_str(to_str(derived_type.get_property("name"))) != "hmac") {
            return reject_with("NotSupportedError", "SubtleCrypto.deriveKey: only HMAC derivedKeyType is supported.");
        }
        JsValue dhash_val = derived_type.get_property("hash");
        std::string dhash_alg = dhash_val.is_object_like() ? ascii_lower_str(to_str(dhash_val.get_property("name")))
                                                           : ascii_lower_str(to_str(dhash_val));
        if (dhash_alg != "sha-1" && dhash_alg != "sha-256" && dhash_alg != "sha-384" && dhash_alg != "sha-512") {
            return reject_with("NotSupportedError", "SubtleCrypto.deriveKey: unsupported HMAC hash '" + dhash_alg + "'.");
        }
        std::size_t dk_len = (dhash_alg == "sha-384" || dhash_alg == "sha-512") ? 128u : 64u;
        JsValue len_val = derived_type.get_property("length");
        if (len_val.type == JsType::Number && to_double(len_val) > 0) {
            dk_len = static_cast<std::size_t>((static_cast<int>(to_double(len_val)) + 7) / 8);
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

    auto aes_ctr_op = [](Realm& r, Args args, const char* op_name) -> JsValue {
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        auto reject_with = [&](const std::string& name, const std::string& msg) {
            JsValue err = make_dom_exception(r, name, msg);
            if (promise.is_function()) return js_call(promise.get_property("reject"), promise, {err});
            return undefined();
        };
        if (args.size() < 3) return reject_with("TypeError", std::string("SubtleCrypto.") + op_name + ": 3 arguments required.");
        const JsValue& alg_val = args[0];
        if (!alg_val.is_object_like() || ascii_lower_str(to_str(alg_val.get_property("name"))) != "aes-ctr") {
            return reject_with("NotSupportedError", std::string("SubtleCrypto.") + op_name + ": only AES-CTR is supported.");
        }
        CryptoKeyState* key = state_of<CryptoKeyState>(args[1]);
        if (!key || key->hash_alg != "aes-ctr") {
            return reject_with("InvalidAccessError", std::string("SubtleCrypto.") + op_name + ": key must be an AES-CTR CryptoKey.");
        }
        std::vector<uint8_t> counter, data;
        if (!read_buffer_source(r, alg_val.get_property("counter"), counter) || counter.size() != 16u) {
            return reject_with("OperationError", std::string("SubtleCrypto.") + op_name + ": counter must be 16 bytes.");
        }
        const int length_bits = static_cast<int>(to_double(alg_val.get_property("length")));
        if (length_bits < 1 || length_bits > 128) {
            return reject_with("OperationError", std::string("SubtleCrypto.") + op_name + ": counter length must be 1..128.");
        }
        if (!read_buffer_source(r, args[2], data)) {
            return reject_with("TypeError", std::string("SubtleCrypto.") + op_name + ": data must be a BufferSource.");
        }
        std::vector<uint8_t> out = aes_ctr_crypt(key->key_bytes, counter, length_bits, data);
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

    I.method("encrypt", 3, [aes_ctr_op](Realm& r, Args args) { return aes_ctr_op(r, args, "encrypt"); });
    I.method("decrypt", 3, [aes_ctr_op](Realm& r, Args args) { return aes_ctr_op(r, args, "decrypt"); });
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
