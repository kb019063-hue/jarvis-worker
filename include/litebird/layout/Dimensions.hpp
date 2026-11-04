#pragma once

#include "litebird/common/Geometry.hpp"

namespace litebird {

struct BoxDimensions {
    Rect content{0.0f, 0.0f, 0.0f, 0.0f};
    EdgeSizes padding{0.0f};
    EdgeSizes border{0.0f};
    EdgeSizes margin{0.0f};

    Rect padding_box() const {
        return Rect(
            content.x - padding.left,
            content.y - padding.top,
            content.width + padding.left + padding.right,
            content.height + padding.top + padding.bottom
        );
    }

    Rect border_box() const {
        auto pad = padding_box();
        return Rect(
            pad.x - border.left,
            pad.y - border.top,
            pad.width + border.left + border.right,
            pad.height + border.top + border.bottom
        );
    }

    Rect margin_box() const {
        auto b = border_box();
        return Rect(
            b.x - margin.left,
            b.y - margin.top,
            b.width + margin.left + margin.right,
            b.height + margin.top + margin.bottom
        );
    }
};

} // namespace litebird
