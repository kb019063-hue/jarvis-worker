#pragma once

#include "litebird/render/ImageDecoder.hpp"
#include "litebird/render/Path2D.hpp"
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

/// §4.12.5.1.7: SVG path data parsed into a `Path2D`, through the same walk of the
/// same grammar an SVG `d` attribute takes. Answers an empty path for empty data
/// (valid in SVG) and never null.
std::shared_ptr<Path2D> parse_svg_path_data(std::string_view d);

} // namespace litebird
