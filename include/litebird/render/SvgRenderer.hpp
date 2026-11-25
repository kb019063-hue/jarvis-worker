#pragma once

#include "litebird/render/ImageDecoder.hpp"
#include "litebird/render/Path2D.hpp"
#include "litebird/dom/Element.hpp"
#include <memory>
#include <string_view>

namespace litebird {

/// The box one SVG element draws, in its own user space.
///
/// `SVGGraphicsElement.getBBox()` (SVG 2 §8.10) is the box of the geometry an
/// element renders, with the element's own `transform` excluded and its
/// children's transforms included. That is not a layout box: an SVG element
/// has none. This is answered from the renderer's own geometry code, so the
/// box `getBBox()` reports and the box the painter draws cannot disagree.
///
/// `element` is a graphics element. A shape answers its own box; a container
/// the union of its children's, each mapped through that child's transform.
/// Anything that draws nothing where it stands (`defs`, `symbol`, `mask`,
/// `clipPath`, a gradient element, `title`, `desc`, `metadata`, `style`,
/// `script`) contributes nothing from inside a box walk either.
struct SvgUserBBox {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};
    /// False when the element draws nothing at all: an empty path, a zero
    /// `width`, a container with no drawn descendant. A box that is invalid is
    /// reported as all zeroes, which is what a browser answers.
    bool valid{false};
};

SvgUserBBox svg_element_bbox(const std::shared_ptr<Element>& element);

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

}  // namespace litebird
