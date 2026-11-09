#pragma once

// Stack headroom checks for the recursive walkers in the engine.
//
// Two places recurse once per unit of *document* nesting or per JS call:
// `LayoutTree::build_box` and the AST interpreter's `eval_expr`. Both used to
// run until the C++ stack died, which took the whole process down with no
// diagnostic.
//
// The obvious fix -- a fixed depth constant -- does not work. The native cost
// of one level of recursion depends on how the frame was compiled: a
// `build_box` level is roughly 2.8 KB in an -O2 build but roughly 34 KB under
// AddressSanitizer, which pads stack frames. That is a 12x difference over the
// same 8 MB stack. Any constant low enough to survive ASan truncates ordinary
// pages in a normal build, and any constant generous enough to be safe there
// crashes ASan -- which is exactly how the sanitizer CI job caught the first
// version of this fix, at depth 243 of a nominal 512.
//
// So instead of guessing a depth, ask the real question: is there enough stack
// left for another level? This is what real engines do (V8's `StackGuard`,
// SpiderMonkey's `CheckRecursionLimit`). It adapts automatically to the build
// flags, the platform, and whichever thread the code runs on.

#include <cstddef>
#include <cstdint>

#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#include <unistd.h>
#define LITEBIRD_HAVE_STACK_PROBE 1
#endif

namespace litebird {

/// Stack that must still be free below the walking code's floor. It covers the
/// error path that unwinds out of the walkers and anything the caller still has
/// to do afterwards, and it is large enough to absorb a single level of
/// recursion even in an AddressSanitizer build (~34 KB), so the depth at which
/// we stop never depends on the per-frame size.
///
/// Everything above this reserve is fair game for recursion. That matters: a
/// fixed *small* budget would be "safe" in every build but would cap an ASan
/// run at a few dozen levels, which is too shallow to lay out or evaluate
/// anything real. Letting the walkers use the stack they actually have keeps
/// normal builds deep (thousands of levels) and shrinks the ASan limit in
/// proportion to its fatter frames, which is the honest trade-off.
inline constexpr std::size_t kStackReserveBytes = 1u * 1024u * 1024u;

/// Never let the walkers consume more than this, even on a thread whose stack
/// is unusually large, so a runaway document cannot silently eat tens of
/// megabytes of address space.
inline constexpr std::size_t kStackBudgetBytes = 64u * 1024u * 1024u;

namespace detail {

/// Per-thread bounds of the native stack. `active` is false when the platform
/// could not report them, in which case callers fall back to a fixed depth cap.
struct ThreadStackBase {
    std::uintptr_t base{0};   ///< lowest address in the stack mapping
    std::size_t limit{0};     ///< size of the stack mapping in bytes
    bool active{false};
};

inline ThreadStackBase& thread_stack_base() {
    thread_local ThreadStackBase state{};
    return state;
}

/// Address of a local in the calling frame, i.e. the current stack pointer.
/// `noinline` stops the compiler folding the probe into a caller and thereby
/// measuring some other frame.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
inline std::uintptr_t current_stack_pointer() {
    volatile char probe = 0;
    return reinterpret_cast<std::uintptr_t>(&probe);
}

inline bool init_thread_stack_base() {
    ThreadStackBase& state = thread_stack_base();
    if (state.active) return true;

#if defined(LITEBIRD_HAVE_STACK_PROBE)
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        void* stack_addr = nullptr;
        std::size_t stack_size = 0;
        if (pthread_attr_getstack(&attr, &stack_addr, &stack_size) == 0 &&
            stack_addr && stack_size) {
            state.base = reinterpret_cast<std::uintptr_t>(stack_addr);
            state.limit = stack_size;
            state.active = true;
        }
        pthread_attr_destroy(&attr);
    }
#endif

    // If the query is refused we leave `active` false and the callers use their
    // fixed depth cap. A guessed stack bound is worse than a coarse depth cap.
    return state.active;
}

} // namespace detail

/// Lowest stack address the recursive walkers may descend to. Relies on the
/// stack growing downwards, which holds on every target this engine builds for
/// (x86-64 and AArch64 under Linux and macOS).
inline std::uintptr_t stack_floor() {
    const detail::ThreadStackBase& state = detail::thread_stack_base();
    if (state.limit == 0) return 0;

    // Leave the reserve untouched at the bottom; cap how much of the rest the
    // walkers may claim on very large thread stacks.
    std::size_t offset = kStackReserveBytes;
    if (state.limit <= kStackReserveBytes) {
        // A thread with a stack smaller than the reserve (an exotic setup, but
        // legal) would otherwise get a floor above its own top and refuse every
        // level of recursion. Fall back to claiming half of what exists.
        offset = state.limit / 2;
    } else {
        const std::size_t available = state.limit - kStackReserveBytes;
        const std::size_t claimable = (available > kStackBudgetBytes) ? kStackBudgetBytes : available;
        offset = state.limit - claimable;
    }
    return state.base + offset;
}

/// True when the calling thread has run out of its recursion budget and the
/// recursive walkers must stop descending.
///
/// Returns false (i.e. "no limit known") on platforms that cannot report stack
/// bounds, so callers keep a fixed depth cap as a backstop.
inline bool stack_is_exhausted() {
    detail::ThreadStackBase& state = detail::thread_stack_base();
    if (!state.active) {
        if (!detail::init_thread_stack_base()) return false;
    }
    return detail::current_stack_pointer() <= stack_floor();
}

} // namespace litebird
