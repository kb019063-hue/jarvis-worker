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
    Grid,
    None
};

enum class FlexDirection {
    Row,
    Column,
    RowReverse,
    ColumnReverse
};

enum class FlexWrap {
    NoWrap,
    Wrap,
    WrapReverse
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

enum class AlignContent {
    Stretch,
    FlexStart,
    FlexEnd,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
};

enum class Position {
    Static,
    Relative,
    Absolute,
    Fixed
};

enum class BoxSizing {
    ContentBox,
    BorderBox
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
    std::string text_decoration{"none"}; // "none", "underline"

    // Positioning properties
    Position position{Position::Static};
    float top{0.0f};
    float right{0.0f};
    float bottom{0.0f};
    float left{0.0f};
    bool has_top{false};
    bool has_right{false};
    bool has_bottom{false};
    bool has_left{false};
    int z_index{0};
    bool has_z_index{false};

    // Box Sizing & Constraints
    BoxSizing box_sizing{BoxSizing::ContentBox};
    float min_width{-1.0f};
    float max_width{-1.0f};
    float min_height{-1.0f};
    float max_height{-1.0f};
    bool margin_top_auto{false};
    bool margin_bottom_auto{false};
    bool margin_left_auto{false};
    bool margin_right_auto{false};

    // Flexbox properties
    FlexDirection flex_direction{FlexDirection::Row};
    FlexWrap flex_wrap{FlexWrap::NoWrap};
    JustifyContent justify_content{JustifyContent::FlexStart};
    AlignItems align_items{AlignItems::Stretch};
    AlignContent align_content{AlignContent::Stretch};
    float gap{0.0f};
    float flex_grow{0.0f};
    float flex_shrink{1.0f};
    float flex_basis{-1.0f}; // -1 indicates auto

    // Grid properties
    std::vector<float> grid_template_columns; // >0: fixed px, <0: fr units (-1.0f = 1fr)
    std::vector<float> grid_template_rows;
    float grid_column_gap{0.0f};
    float grid_row_gap{0.0f};

    // Transitions and Animations
    float opacity{1.0f};
    std::string animation_name;
    float animation_duration{0.0f}; // in seconds
    std::string animation_timing_function{"ease"};
    float animation_iteration_count{1.0f}; // -1 for infinite
    std::string transition_property;
    float transition_duration{0.0f};
    std::string transition_timing_function{"ease"};
    float transition_delay{0.0f};

    // 2D Transforms
    bool has_transform{false};
    TransformMatrix2D transform_matrix{TransformMatrix2D::identity()};
    std::string transform_str;
    std::string transform_origin{"50% 50%"};

    // CSS Custom Properties (Variables)
    std::unordered_map<std::string, std::string> custom_properties;

    bool is_block() const { return display == Display::Block; }
    bool is_inline() const { return display == Display::Inline; }
    bool is_flex() const { return display == Display::Flex; }
    bool is_grid() const { return display == Display::Grid; }
    bool is_none() const { return display == Display::None; }

    bool is_positioned() const { return position != Position::Static; }
    bool is_out_of_flow() const { return position == Position::Absolute || position == Position::Fixed; }
};

class StyleResolver {
public:
    StyleResolver();

    void add_stylesheet(const StyleSheet& sheet);
    void set_viewport(float w, float h) { m_viewport_width = w; m_viewport_height = h; }
    float viewport_width() const { return m_viewport_width; }
    float viewport_height() const { return m_viewport_height; }

    ComputedStyle resolve_style(const std::shared_ptr<Element>& element, const ComputedStyle* parent_style = nullptr) const;

    const KeyframesRule* find_keyframes(const std::string& name) const;
    void apply_animation(ComputedStyle& style, float elapsed_seconds) const;
    static void interpolate_styles(ComputedStyle& target, const ComputedStyle& from, const ComputedStyle& to, float t);

    // CSS Variables and Calc Resolvers
    std::string resolve_var_functions(const std::string& value, const std::unordered_map<std::string, std::string>& custom_props, int depth = 0) const;
    float evaluate_calc_expression(std::string_view expr, float reference_size = 0.0f) const;
    std::string resolve_calc_functions(const std::string& value, float reference_size = 0.0f) const;

private:
    static StyleSheet default_user_agent_stylesheet();
    bool matches_selector(const std::shared_ptr<Element>& element, const Selector& selector) const;
    bool matches_simple_selector(const std::shared_ptr<Element>& element, const SimpleSelector& sel) const;

    void apply_declaration(ComputedStyle& style, const std::string& prop, const std::string& val) const;
    float parse_length(std::string_view val, float default_val = 0.0f) const;
    EdgeSizes parse_box_edges(std::string_view val) const;

    StyleSheet m_ua_sheet;
    std::vector<StyleSheet> m_author_sheets;
    float m_viewport_width{800.0f};
    float m_viewport_height{600.0f};
};

} // namespace litebird
