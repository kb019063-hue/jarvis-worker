#pragma once

#include "litebird/dom/Node.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/css/StyleResolver.hpp"
#include "litebird/layout/LayoutBox.hpp"
#include <memory>

namespace litebird {

class LayoutTree {
public:
    static std::shared_ptr<LayoutBox> build(
        const std::shared_ptr<Node>& root_node,
        const StyleResolver& resolver,
        float viewport_width = 800.0f,
        float viewport_height = 600.0f
    );

private:
    static std::shared_ptr<LayoutBox> build_box(
        const std::shared_ptr<Node>& node,
        const StyleResolver& resolver,
        const ComputedStyle* parent_style,
        int depth = 0
    );

    /// Maximum box-tree depth. Every nested element costs a `build_box` stack
    /// frame, and each frame keeps a 920-byte ComputedStyle alive across the
    /// recursive call, so an ordinary 8 MB stack ran out at roughly 3,000 levels
    /// of nesting. Real browsers cap DOM depth for exactly this reason (HTML's
    /// stack of open elements is bounded at 512). Subtrees past the limit are
    /// dropped rather than crashing the process.
    static constexpr int kMaxBuildDepth = 512;
};

} // namespace litebird
