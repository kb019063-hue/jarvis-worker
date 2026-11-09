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
//
// The first version of this file trusted `pthread_attr_getstack` outright and
// broke on the CI runner, where the call reported a region that did not contain
// the running frame (a 117 GB gap between the reported base and the live stack
// pointer). Every layout then computed a "floor" above itself, concluded the
// stack was exhausted, and the engine produced empty box trees -- a diagnostic
// that kills the engine is worse than no diagnostic. Two defences are in place
// now, and both matter:
//
//   1. The reported region is only accepted if it actually brackets the address
//      of a local in the calling frame.
//   2. If it does not, the bounds are derived from RLIMIT_STACK instead, so a
//      host with an unusual stack setup degrades to a slightly wrong limit
//      rather than to a dead engine.
//
// Note also that thread stack sizes vary a lot between environments -- 8 MB is
// the common Linux default, but the CI runner uses 16 MB. Nothing here may
// assume a particular size.

#include <cstddef>
#include <cstdint>

#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#include <sys/resource.h>
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
    std::uintptr_t top{0};    ///< highest address the stack can grow to
    std::size_t limit{0};     ///< size of the usable stack region in bytes
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

/// Stack address of the outermost frame seen on this thread. Initialised on
/// first use, which is close enough to the thread's start for the fallback path.
inline std::uintptr_t& thread_stack_base_origin() {
    thread_local std::uintptr_t origin = 0;
    if (origin == 0) origin = current_stack_pointer();
    return origin;
}


/// Fallback for when the pthread query is missing or unusable: derive the top
/// of the stack from the address of a local in the *outermost* frame we can
/// see, and take the size from RLIMIT_STACK.
///
/// `origin` is the stack address the program started from (captured once, in
/// `thread_stack_base()`), which is the best approximation of the stack top
/// available without OS help.
inline bool init_from_rlimit(std::uintptr_t origin, ThreadStackBase& state) {
    std::size_t size = 8u * 1024u * 1024u;  // the usual default
#if defined(RLIMIT_STACK)
    struct rlimit rl {};
    if (getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY &&
        rl.rlim_cur > 0 && rl.rlim_cur <= (1ull << 32)) {
        size = static_cast<std::size_t>(rl.rlim_cur);
    }
#endif
    if (origin == 0 || size < 64u * 1024u) return false;
    // Align the top up to a page and keep the whole region below `origin`.
    state.top = origin & ~static_cast<std::uintptr_t>(4095);
    state.limit = size;
    state.active = true;
    return true;
}

inline bool init_thread_stack_base() {
    ThreadStackBase& state = thread_stack_base();
    if (state.active) return true;

    // The address of a local in this frame: any real stack bound we are handed
    // has to be consistent with it, which is how a bad pthread result gets
    // caught instead of trusted.
    const std::uintptr_t here = current_stack_pointer();

#if defined(LITEBIRD_HAVE_STACK_PROBE)
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        void* stack_addr = nullptr;
        std::size_t stack_size = 0;
        if (pthread_attr_getstack(&attr, &stack_addr, &stack_size) == 0 &&
            stack_addr && stack_size >= 64u * 1024u) {
            const auto low = reinterpret_cast<std::uintptr_t>(stack_addr);
            const auto high = low + stack_size;
            // Accept only a region that actually brackets this frame. On some
            // hosts `pthread_attr_getstack` reports a stale or unrelated mapping,
            // which would otherwise put the "floor" above the live stack and make
            // the walkers refuse to do anything at all.
            if (here >= low && here < high) {
                state.top = high;
                state.limit = stack_size;
                state.active = true;
            }
        }
        pthread_attr_destroy(&attr);
    }
#endif

    if (!state.active) init_from_rlimit(thread_stack_base_origin(), state);

    // If nothing could be established the walkers keep their fixed depth caps;
    // a guessed stack bound is worse than a coarse cap.
    return state.active;
}

} // namespace detail

/// Lowest stack address the recursive walkers may descend to. Relies on the
/// stack growing downwards, which holds on every target this engine builds for
/// (x86-64 and AArch64 under Linux and macOS).
inline std::uintptr_t stack_floor() {
    const detail::ThreadStackBase& state = detail::thread_stack_base();
    if (state.limit == 0) return 0;

    // Leave kStackReserveBytes untouched at the bottom of the region; cap how
    // much of the rest the walkers may claim on very large thread stacks.
    std::size_t offset;
    if (state.limit <= kStackReserveBytes) {
        // A stack smaller than the reserve (exotic, but legal) would otherwise
        // put the floor above its own top and refuse every level of recursion.
        offset = state.limit / 2;
    } else {
        const std::size_t available = state.limit - kStackReserveBytes;
        const std::size_t claimable = (available > kStackBudgetBytes) ? kStackBudgetBytes : available;
        offset = kStackReserveBytes + (available - claimable);
    }
    return state.top - (state.limit - offset);
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
