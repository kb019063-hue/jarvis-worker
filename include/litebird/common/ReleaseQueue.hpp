#pragma once

// Destruction that does not recurse.
//
// A chain of closures, each holding the next (`f = wrap(f)` in a loop), is a
// linked list of reference-counted C++ objects: function payload -> scope ->
// binding -> function payload -> ... Dropping the last reference to the head
// runs the first destructor, which releases the second object, whose
// destructor releases the third, and so on: one set of native frames per link.
// A hundred thousand links overflow the stack and the process dies in the
// middle of an assignment. The same shape exists for promise chains and for
// the syntax tree of a long `a + b + c + ...`.
//
// A stack check cannot help here the way it does for the recursive walkers
// (see StackLimit.hpp), because a destructor has nowhere to report failure.
// So the recursion is removed instead. While one object of a kind is being
// destroyed on a thread, any further object of that kind that dies does not
// destroy its contents in place: it hands them to a queue, and the outermost
// destructor empties the queue in a loop. Depth stays constant however long
// the chain is.
//
// The queue lives on the stack of the outermost destructor and the per-thread
// state is one raw pointer, so there is nothing to construct or tear down per
// thread, no allocation unless something is actually deferred, and no order
// of static destruction to get wrong.

#include <utility>
#include <vector>

namespace litebird {

/// `Item` is whatever a dying object owns that could itself own more objects
/// of the same kind: it is moved into the queue and destroyed later.
template <typename Item>
class ReleaseQueue {
public:
    /// True while an object of this kind is being destroyed on this thread.
    static bool busy() { return pending() != nullptr; }

    /// Hands `item` to the destructor that is already running. Only valid
    /// while busy().
    static void defer(Item&& item) { pending()->push_back(std::move(item)); }

    /// Declared by the outermost destructor before it releases anything.
    /// Everything deferred in the meantime is destroyed, one item at a time,
    /// when this goes out of scope.
    class Drain {
    public:
        Drain() { pending() = &m_items; }
        ~Drain() {
            while (!m_items.empty()) {
                Item next = std::move(m_items.back());
                m_items.pop_back();
                // `next` dies here. Whatever that releases is appended to
                // m_items rather than destroyed underneath this frame.
            }
            pending() = nullptr;
        }
        Drain(const Drain&) = delete;
        Drain& operator=(const Drain&) = delete;
    private:
        std::vector<Item> m_items;
    };

private:
    static std::vector<Item>*& pending() {
        static thread_local std::vector<Item>* t_pending = nullptr;
        return t_pending;
    }
};

} // namespace litebird
