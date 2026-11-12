// The JsString stress tests on their own, for the sanitizers: `make tsan`.
//
// JsString says that handles can be copied, read, appended to and dropped
// from several threads at once without a lock. ThreadSanitizer is the tool
// that can check that claim, and building the whole engine under it is not
// needed: the string type stands alone. The same binary built with
// AddressSanitizer checks that every block of text is freed, which the
// engine-wide `make asan` cannot do because it has to leave leak checking off.
//
// The one thing JsString calls outside itself is the function that raises
// RangeError for a string that is too long, which is stood in for here.

#undef NDEBUG
#include "js_string_stress.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace litebird {
[[noreturn]] void js_throw_range_error(const std::string& message) { throw std::length_error(message); }
} // namespace litebird

int main(int argc, char** argv) {
    const bool only_threads = argc > 1 && std::strcmp(argv[1], "threads") == 0;
    const bool only_model = argc > 1 && std::strcmp(argv[1], "model") == 0;
    if (!only_threads) {
        litebird_tests::js_string_model_test();
        std::puts("JsString model test passed");
    }
    if (!only_model) {
        litebird_tests::js_string_thread_test();
        std::puts("JsString thread test passed");
    }
    return 0;
}
