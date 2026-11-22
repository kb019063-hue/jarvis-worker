#pragma once

#include "litebird/common/Color.hpp"
#include "litebird/common/Geometry.hpp"
#include "litebird/layout/LayoutBox.hpp"
#include <optional>
#include <string>
#include <vector>
#include <variant>
#include <memory>

namespace litebird {

/// Paint the current document's solid canvas across the target viewport.
/// Unlike a box, the canvas does not move with the document scroll transform.
/// Embedded documents render to their own target, so their canvas cannot spill
/// outside the iframe. An enclosing clip, if any, still applies.
struct DrawCanvasCommand {
    Color color;
};

/// The UA's `::backdrop`: a colour over the whole viewport, painted behind a
/// modal dialog and in front of everything the document drew. The page cannot
/// style it — there is no `::backdrop` selector — so the colour is the UA's.
struct DrawBackdropCommand {
    Color color;
};

struct DrawRectCommand {
    Rect rect;
    Color color;
    float border_radius{0.0f};
};

struct DrawShadowCommand {
    Rect rect;
    float x_offset{0.0f};
    float y_offset{0.0f};
    float blur{0.0f};
    float border_radius{0.0f};
    Color color;
};

struct DrawBorderCommand {
    Rect rect;
    EdgeSizes width;
    Color color;
    float border_radius{0.0f};
    /// When set, each edge is drawn in its own colour (collapsed tables: the
    /// shared edge belongs to the winning border, colour included).
    std::optional<EdgeColors> edge_colors;
};

struct DrawTextCommand {
    Point position;
    std::string text;
    Color color;
    float font_size{16.0f};
    int font_weight{400};
    /// The `font-family` list the text was laid out in. Carried through from
    /// the style rather than resolved here, so that painting measures and
    /// draws with exactly the face layout measured with.
    std::string font_family;
    /// `font-style: italic` or `oblique`, carried the same way and for the same
    /// reason: the painter has to draw the face layout measured with, and a
    /// synthesised oblique is a different bitmap of the same glyph.
    bool font_italic{false};
    /// `font-kerning` reduced to the question the painter asks: whether the
    /// face's pair adjustments are applied to this run. Carried rather than
    /// re-resolved for the same reason as the family and the slant — the
    /// painter must draw the text layout measured, and `font-kerning: none`
    /// changes the width of a run, not only its appearance.
    bool font_kerning{true};
    /// True when UAX #9 bidi already wrote this run's characters in visual
    /// left-to-right order (mirrored and reversed); `FontRenderer` restores
    /// logical order for OpenType shaping before reordering clusters visually.
    bool is_rtl_visual{false};
};

struct DrawImageCommand {
    Rect rect{};
    std::shared_ptr<ImageData> image{nullptr};
    float border_radius{0.0f};

    DrawImageCommand() : rect{}, image{nullptr}, border_radius{0.0f} {}
    DrawImageCommand(const Rect& r, std::shared_ptr<ImageData> img, float br = 0.0f)
        : rect(r), image(std::move(img)), border_radius(br) {}
};

struct PushTransformCommand {
    TransformMatrix2D matrix;
};

struct PopTransformCommand {};

/// Clips every following command to `rect` until the matching PopClipCommand.
/// Emitted for `overflow: hidden|auto|scroll` (CSS 2.1 11.1.1) and for
/// `contain: paint`; before Phase 18 `overflow` only established a block
/// formatting context and content still painted outside the box.
struct PushClipCommand {
    Rect rect;
    float border_radius{0.0f};
};

struct PopClipCommand {};

using PaintCommand = std::variant<DrawCanvasCommand, DrawBackdropCommand, DrawRectCommand, DrawShadowCommand, DrawBorderCommand, DrawTextCommand, DrawImageCommand, PushTransformCommand, PopTransformCommand, PushClipCommand, PopClipCommand>;

class DisplayList {
public:
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    void add(PaintCommand cmd) {
        m_commands.push_back(std::move(cmd));
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

    const std::vector<PaintCommand>& commands() const { return m_commands; }

    /// `top_layer` is the element a modal dialog puts above everything else —
    /// its box is left out of the document walk and painted last, behind a
    /// backdrop. Null when no modal dialog is open.
    static DisplayList build(const std::shared_ptr<LayoutBox>& root_box, const Node* top_layer = nullptr);

private:
    std::vector<PaintCommand> m_commands;
};

} // namespace litebird
