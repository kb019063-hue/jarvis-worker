#pragma once

#include "litebird/common/Color.hpp"
#include "litebird/common/Geometry.hpp"
#include "litebird/dom/Node.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/css/StyleSheet.hpp"
#include "litebird/render/ImageFilter.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace litebird {

enum class Containment : uint8_t {
    None = 0,
    Layout = 1 << 0,
    Paint = 1 << 1,
    Size = 1 << 2,
    Style = 1 << 3,
    Strict = Layout | Paint | Size,
    Content = Layout | Paint | Style
};

enum class Display {
    Inline,
    Block,
    Flex,
    Grid,
    None,
    InlineBlock,
    Table,
    TableRow,
    TableCell,
    TableRowGroup
};

/// CSS `overflow` values (only "does this establish a BFC" matters to layout).
enum class Overflow {
    Visible,
    Hidden,
    Auto,
    Scroll
};

/// `border-collapse` (CSS 2.1 17.6). Collapsed tables share borders between
/// adjacent cells instead of giving each cell its own.
enum class BorderCollapse {
    Separate,
    Collapse
};

/// CSS `float` values.
enum class FloatType {
    None,
    Left,
    Right
};

/// CSS `clear` values.
enum class ClearType {
    None,
    Left,
    Right,
    Both
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

    // Percentages cannot be resolved in the cascade -- they need a containing
    // block -- so they are stored here as the raw percentage (0-100) while the
    // pixel field above stays -1 (auto). A value < 0 means "not a percentage".
    // Phase 18: before this, `width: 50%` silently became 50 *pixels*.
    float width_percent{-1.0f};
    float height_percent{-1.0f};
    float min_width_percent{-1.0f};
    float max_width_percent{-1.0f};
    float min_height_percent{-1.0f};
    float max_height_percent{-1.0f};
    EdgeSizes margin_percent{-1.0f};
    EdgeSizes padding_percent{-1.0f};
    EdgeSizes border_width{0.0f};
    Color border_color{Color::black()};
    float border_radius{0.0f};
    float border_radius_percent{-1.0f};
    BoxShadow box_shadow;
    float font_size{16.0f};
    int font_weight{400};
    // Line height: a -1 sentinel means "not declared" (normal; layout falls
    // back to 1.25 * font_size). line_height holds a computed px length;
    // line_height_number holds a bare unitless multiplier. Both inherit per
    // CSS: a declared multiplier is inherited as a multiplier, a declared
    // length or percentage is inherited as a length.
    float line_height{-1.0f};
    float line_height_number{-1.0f};
    std::string text_align{"left"};
    std::string text_decoration{"none"}; // "none", "underline"

    // The line height layout should use for this element.
    float effective_line_height() const {
        if (line_height_number >= 0.0f) return line_height_number * font_size;
        if (line_height >= 0.0f) return line_height;
        return font_size * 1.25f;
    }

    // Floats and clearance (CSS 2.1, sections 9.5 and 9.5.2)
    FloatType float_type{FloatType::None};
    ClearType clear_type{ClearType::None};
    Overflow overflow{Overflow::Visible};

    // Tables: border model and the space between cells (CSS 2.1 17.6.1-2).
    BorderCollapse border_collapse{BorderCollapse::Separate};
    float border_spacing_x{0.0f};
    float border_spacing_y{0.0f};

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

    // Modern Grid Track definition (minmax, auto-fit/auto-fill, repeat)
    struct GridTrackDef {
        float min_px{0.0f};
        float max_px{0.0f};
        float max_fr{0.0f}; // if > 0, fractional track
        bool is_auto_fit{false};
        bool is_auto_fill{false};
    };

    std::vector<GridTrackDef> grid_col_track_defs;
    std::vector<GridTrackDef> grid_row_track_defs;
    bool grid_auto_fit_columns{false};
    GridTrackDef grid_auto_fit_def;

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

    // Filters
    std::vector<FilterFunction> filters;
    std::vector<FilterFunction> backdrop_filters;

    // Grid Template Areas
    struct GridAreaRect {
        int row_start{1};
        int row_end{2};
        int col_start{1};
        int col_end{2};
    };
    std::unordered_map<std::string, GridAreaRect> grid_named_areas;
    std::string grid_area_name;
    int grid_row_start{0};
    int grid_row_end{0};
    int grid_column_start{0};
    int grid_column_end{0};

    // Containment
    uint8_t containment{0};
    bool contains_layout() const { return (containment & static_cast<uint8_t>(Containment::Layout)) != 0; }
    bool contains_paint() const { return (containment & static_cast<uint8_t>(Containment::Paint)) != 0; }
    bool contains_size() const { return (containment & static_cast<uint8_t>(Containment::Size)) != 0; }

    bool is_block() const { return display == Display::Block; }
    bool is_inline_block() const { return display == Display::InlineBlock; }
    bool is_table() const { return display == Display::Table; }
    bool is_table_row() const { return display == Display::TableRow; }
    bool is_table_cell() const { return display == Display::TableCell; }
    bool is_table_row_group() const { return display == Display::TableRowGroup; }
    bool is_table_internal() const {
        return is_table_row() || is_table_cell() || is_table_row_group();
    }
    /// True for boxes that participate as block-level boxes in normal flow.
    bool is_block_level() const {
        return display == Display::Block || display == Display::Flex ||
               display == Display::Grid || display == Display::Table ||
               display == Display::TableRow || display == Display::TableCell ||
               display == Display::TableRowGroup;
    }
    bool is_floated() const { return float_type != FloatType::None; }
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
    /// `valid`, when given, is set to false for an expression that cannot be
    /// evaluated (it nests more deeply than the parser follows).
    float evaluate_calc_expression(std::string_view expr, float reference_size = 0.0f, bool* valid = nullptr) const;
    std::string resolve_calc_functions(const std::string& value, float reference_size = 0.0f) const;

private:
    struct IndexedSelector {
        std::size_t sheet_index;
        std::size_t rule_index;
        std::size_t selector_index;
        std::size_t source_order;
    };
    struct SelectorIndex {
        std::unordered_map<std::string, std::vector<IndexedSelector>> by_id;
        std::unordered_map<std::string, std::vector<IndexedSelector>> by_class;
        std::unordered_map<std::string, std::vector<IndexedSelector>> by_tag;
        std::vector<IndexedSelector> universal;
    };

    static StyleSheet default_user_agent_stylesheet();
    static void index_sheet(SelectorIndex& index, const StyleSheet& sheet,
                            std::size_t sheet_index, std::size_t& source_order);
    std::vector<IndexedSelector> indexed_candidates(const std::shared_ptr<Element>& element) const;
    bool matches_selector(const std::shared_ptr<Element>& element, const Selector& selector) const;
    bool match_selector_impl(const std::shared_ptr<Element>& element, const Selector& selector, Specificity* out_spec) const;
    bool matches_simple_selector(const std::shared_ptr<Element>& element, const SimpleSelector& sel) const;

    void apply_declaration(ComputedStyle& style, const std::string& prop, const std::string& val) const;
    /// Applies a single longhand property (no shorthand expansion).
    void apply_longhand(ComputedStyle& style, const std::string& prop, const std::string& val) const;
    /// Expands CSS shorthands (margin, padding, border, background, font, ...)
    /// into longhand declarations. Returns false when `prop` is not a shorthand.
    bool expand_shorthand(const std::string& prop, const std::string& val,
                          std::vector<std::pair<std::string, std::string>>& out) const;
    float parse_length(std::string_view val, float default_val = 0.0f) const;
    EdgeSizes parse_box_edges(std::string_view val, EdgeSizes* percent_out = nullptr) const;

    StyleSheet m_ua_sheet;
    std::vector<StyleSheet> m_author_sheets;
    SelectorIndex m_author_selector_index;
    std::vector<std::string> m_layer_order;
    std::size_t m_next_author_selector_order{0};
    float m_viewport_width{800.0f};
    float m_viewport_height{600.0f};
    // Font size used to resolve em/ex/ch units for the element being resolved.
    mutable float m_em_base{16.0f};
    mutable float m_root_font_size{16.0f};
    struct DescPrefixEntry {
        const Node* parent{nullptr};
        std::size_t prefix{0};
    };
    mutable std::unordered_map<const Element*, std::size_t> m_depth_cache;
    mutable std::unordered_map<const Selector*, std::unordered_map<const Element*, DescPrefixEntry>> m_desc_prefix_cache;

    int layer_rank(const std::string& layer_name) const;
};

} // namespace litebird
