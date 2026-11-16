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

using PaintCommand = std::variant<DrawRectCommand, DrawShadowCommand, DrawBorderCommand, DrawTextCommand, DrawImageCommand, PushTransformCommand, PopTransformCommand, PushClipCommand, PopClipCommand>;

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

    static DisplayList build(const std::shared_ptr<LayoutBox>& root_box);

private:
    static void build_recursive(const std::shared_ptr<LayoutBox>& box, DisplayList& list);
    std::vector<PaintCommand> m_commands;
};

} // namespace litebird
