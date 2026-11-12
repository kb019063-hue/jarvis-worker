#pragma once

// Walking a tree of nodes without recursion.
//
// Every walk over the DOM used to be written the obvious way: a function that
// visits a node and calls itself for each child. That uses one set of native
// stack frames per level of nesting, and nothing in the DOM limits nesting: a
// script can append a child to a child to a child two hundred thousand times.
// On an 8 MB stack the recursion then dies at some tens of thousands of levels
// and takes the process with it, whichever walk happens to run first: looking
// up the <title>, reading textContent, serializing innerHTML, matching a
// selector, firing connectedCallback, or the destructor.
//
// A stack check (StackLimit.hpp) is the right tool where a walk can stop and
// say so: layout skips what is too deep to lay out. It is the wrong tool here.
// `querySelectorAll` that silently misses matches below some depth, or
// `textContent` that returns part of the text, would be wrong answers that
// depend on how the binary was compiled. These walks keep their place in a
// vector instead, so their depth is limited by memory, like the tree itself.
//
// Nothing here may be called with a tree that changes during the walk: the
// frames hold raw pointers. Walks that run script (custom element callbacks)
// first collect the nodes they will visit; see subtree_elements().

#include "litebird/dom/Node.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace litebird {

/// Calls `visit(Node&)` for every node below `root`, in tree order (a parent
/// before its children, children in order). `root` itself is not visited.
/// `visit` returns false to end the walk.
///
/// Returns false if the walk was ended by `visit`.
template <typename Visit>
bool for_each_descendant(const Node& root, Visit&& visit) {
    struct Frame {
        const Node* node;
        std::size_t next;
    };
    std::vector<Frame> open;
    open.push_back({&root, 0});
    while (!open.empty()) {
        Frame& top = open.back();
        const auto& children = top.node->children();
        if (top.next >= children.size()) {
            open.pop_back();
            continue;
        }
        Node* child = children[top.next++].get();
        if (!child) continue;
        if (!visit(*child)) return false;
        // `top` is not used after this: push_back may move the frames.
        if (!child->children().empty()) open.push_back({child, 0});
    }
    return true;
}

/// The elements of the subtree rooted at `root` (including `root` when it is
/// an element), in tree order, each kept alive by the returned vector.
///
/// For walks that call into script once per element: script may move or
/// remove the very nodes the walk is standing on, so the list is fixed first.
std::vector<std::shared_ptr<Element>> subtree_elements(const std::shared_ptr<Node>& root);

} // namespace litebird
