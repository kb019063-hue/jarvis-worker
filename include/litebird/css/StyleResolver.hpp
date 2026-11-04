#pragma once

#include "litebird/common/Color.hpp"
#include "litebird/common/Geometry.hpp"
#include "litebird/dom/Node.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/css/StyleSheet.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace litebird {

enum class Display {
    Inline,
    Block,
    Flex,
    None
};

enum class FlexDirection {
    Row,
    Column,
    RowReverse,
    ColumnReverse
};

enum class JustifyContent {
    FlexStart,
    FlexEnd,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
};

enum class AlignItems {
    Stretch,
    FlexStart,
    FlexEnd,
    Center
};

struct BoxShadow {
    float x_offset{0.0f};
    float y_offset{0.0f};
    float blur{0.0f};
    Color color{Color::transparent()};
    bool has_shadow{false};
};

struct ComputedStyle {
    Display display{Display::Inline};
    Color color{Color::black()};
    Color background_color{Color::transparent()};
    float width{-1.0f};  // -1 indicates auto
    float height{-1.0f}; // -1 indicates auto
    EdgeSizes margin{0.0f};
    EdgeSizes padding{0.0f};
    EdgeSizes border_width{0.0f};
    Color border_color{Color::black()};
    float border_radius{0.0f};
    BoxShadow box_shadow;
    float font_size{16.0f};
    int font_weight{400};
    std::string text_align{"left"};

    // Flexbox properties
    FlexDirection flex_direction{FlexDirection::Row};
    JustifyContent justify_content{JustifyContent::FlexStart};
    AlignItems align_items{AlignItems::Stretch};
    float gap{0.0f};
    float flex_grow{0.0f};
    float flex_shrink{1.0f};
    float flex_basis{-1.0f}; // -1 indicates auto

    bool is_block() const { return display == Display::Block; }
    bool is_inline() const { return display == Display::Inline; }
    bool is_flex() const { return display == Display::Flex; }
    bool is_none() const { return display == Display::None; }
};

class StyleResolver {
public:
    StyleResolver();

    void add_stylesheet(const StyleSheet& sheet);

    ComputedStyle resolve_style(const std::shared_ptr<Element>& element, const ComputedStyle* parent_style = nullptr) const;

private:
    static StyleSheet default_user_agent_stylesheet();
    bool matches_selector(const std::shared_ptr<Element>& element, const Selector& selector) const;
    bool matches_simple_selector(const std::shared_ptr<Element>& element, const SimpleSelector& sel) const;

    void apply_declaration(ComputedStyle& style, const std::string& prop, const std::string& val) const;
    float parse_length(std::string_view val, float default_val = 0.0f) const;
    EdgeSizes parse_box_edges(std::string_view val) const;

    StyleSheet m_ua_sheet;
    std::vector<StyleSheet> m_author_sheets;
};

} // namespace litebird
