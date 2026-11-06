#pragma once

#include "litebird/render/ImageDecoder.hpp"
#include "litebird/dom/Element.hpp"
#include <memory>
#include <string_view>

namespace litebird {

class SvgRenderer {
public:
    static std::shared_ptr<ImageData> render(
        const std::shared_ptr<Element>& svg_element,
        float target_width = -1.0f,
        float target_height = -1.0f
    );
};

} // namespace litebird
