#include "litebird/common/TerminalText.hpp"
#include <iostream>
#include <iterator>
#include <string>

// Expose the shared output-boundary helper to the byte-level verifier.
int main() {
    const std::string input((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    std::cout << litebird::terminal_safe(input);
    return std::cout ? 0 : 1;
}
