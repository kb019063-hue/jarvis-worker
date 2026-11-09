#pragma once

#include "litebird/dom/Node.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/css/StyleResolver.hpp"
#include "litebird/layout/LayoutBox.hpp"
#include "litebird/common/StackLimit.hpp"
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

    /// Fixed depth ceiling, used only where the native stack bounds cannot be
    /// queried (see StackLimit.hpp). On Linux and macOS the real limit is the
    /// remaining stack: the native cost of one level varies about 12x between
    /// an -O2 build (~2.8 KB) and an AddressSanitizer build (~34 KB), so a
    /// constant that is safe under ASan truncates ordinary documents while one
    /// that is generous there crashes ASan. Depth is therefore bounded by
    /// `stack_is_exhausted()` and this value is just a backstop.
    static constexpr int kMaxBuildDepth = 20000;
};

} // namespace litebird
