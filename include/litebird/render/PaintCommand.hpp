#pragma once

#include "litebird/common/Color.hpp"
#include "litebird/common/Geometry.hpp"
#include "litebird/layout/LayoutBox.hpp"
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
};

struct DrawTextCommand {
    Point position;
    std::string text;
    Color color;
    float font_size{16.0f};
    int font_weight{400};
};

using PaintCommand = std::variant<DrawRectCommand, DrawShadowCommand, DrawBorderCommand, DrawTextCommand>;

class DisplayList {
public:
    void add(PaintCommand cmd) {
        m_commands.push_back(std::move(cmd));
    }

    const std::vector<PaintCommand>& commands() const { return m_commands; }

    static DisplayList build(const std::shared_ptr<LayoutBox>& root_box);

private:
    static void build_recursive(const std::shared_ptr<LayoutBox>& box, DisplayList& list);
    std::vector<PaintCommand> m_commands;
};

} // namespace litebird
