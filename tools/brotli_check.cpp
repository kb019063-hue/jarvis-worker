#include "litebird/net/Brotli.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool decode_hex(std::string_view hex, std::vector<uint8_t>& out) {
    out.clear();
    if (hex.size() % 2 != 0) return false;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hex_val(hex[i]);
        const int lo = hex_val(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

void print_hex(const std::vector<uint8_t>& bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(bytes.size() * 2);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        out[2 * i] = kHex[(bytes[i] >> 4) & 0x0f];
        out[2 * i + 1] = kHex[bytes[i] & 0x0f];
    }
    std::cout << out;
}

} // namespace

int main(int argc, char** argv) {
    using namespace litebird;
    if (argc < 2 || std::string_view(argv[1]) != "--batch") {
        std::cerr << "Usage: brotli_check --batch\n"
                  << "Reads lines: <max_out>\\t<hex_input>\n";
        return 2;
    }

    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::string line;
    std::vector<uint8_t> in;
    std::vector<uint8_t> out;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            std::cout << "ERR\n";
            continue;
        }
        std::size_t max_out = Brotli::DefaultMaxOutputBytes;
        try {
            max_out = std::stoull(line.substr(0, tab));
        } catch (...) {
            std::cout << "ERR\n";
            continue;
        }

        if (!decode_hex(std::string_view(line).substr(tab + 1), in)) {
            std::cout << "ERR\n";
            continue;
        }

        std::size_t consumed = 0;
        if (!Brotli::decompress(in.data(), in.size(), out, max_out, &consumed)) {
            std::cout << "ERR\n";
            continue;
        }
        std::cout << "OK\t" << consumed << "\t";
        print_hex(out);
        std::cout << "\n";
    }
    return 0;
}
