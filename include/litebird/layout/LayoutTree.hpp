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
        const ComputedStyle* parent_style
    );
};

} // namespace litebird
