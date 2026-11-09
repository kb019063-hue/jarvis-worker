// TEMPORARY diagnostic: prints what the stack probe sees, so a CI failure that
// does not reproduce locally can be understood. Remove once resolved.
#include "litebird/common/StackLimit.hpp"
#include <cstdio>
#include <cstdlib>

using namespace litebird;

int main() {
    const bool ok = detail::init_thread_stack_base();
    auto& s = detail::thread_stack_base();
    const auto sp = detail::current_stack_pointer();
    const auto floor = stack_floor();

    std::printf("probe_active=%d\n", static_cast<int>(ok));
    std::printf("base=%zx limit=%zu\n", static_cast<size_t>(s.base), s.limit);
    std::printf("sp=%zx floor=%zx\n", static_cast<size_t>(sp), static_cast<size_t>(floor));
    std::printf("headroom=%ld\n", static_cast<long>(sp) - static_cast<long>(floor));
    std::printf("exhausted=%d\n", static_cast<int>(stack_is_exhausted()));

    // Frame cost of a trivial recursion, to show the build's per-frame padding.
    struct R {
        static int go(int d) { return stack_is_exhausted() ? d : go(d + 1); }
    };
    std::printf("trivial_depth=%d\n", R::go(0));
    return 0;
}
