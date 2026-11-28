#pragma once

#include "litebird/common/Color.hpp"
#include "litebird/common/Geometry.hpp"
#include "litebird/dom/Node.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/css/StyleSheet.hpp"
#include "litebird/render/ImageFilter.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

enum class Display : uint8_t {
    Inline,
    Block,
    Flex,
    Grid,
    None,
    InlineBlock,
    Table,
    TableRow,
    TableCell,
    TableRowGroup,
    TableColumn,
    TableColumnGroup,
    TableCaption,
    /// CSS Display 3 §2.7: the element generates no box of its own and its
    /// children take part in its parent's formatting context, as if the
    /// element had been replaced by them. It is not `None` -- `None` throws the
    /// children away too -- and it is not `Block`, which is what the engine used
    /// to fall back to because the value was never parsed.
    Contents
};

/// CSS `overflow` values (only "does this establish a BFC" matters to layout).
enum class Overflow : uint8_t {
    Visible,
    Hidden,
    Auto,
    Scroll
};

/// `border-collapse` (CSS 2.1 17.6). Collapsed tables share borders between
/// adjacent cells instead of giving each cell its own.
enum class BorderCollapse : uint8_t {
    Separate,
    Collapse
};

/// CSS `table-layout` (CSS 2.1 17.5.2).
enum class TableLayout : uint8_t {
    Auto,
    Fixed
};

/// CSS `empty-cells` (CSS 2.1 17.5.1.2).
enum class EmptyCells : uint8_t {
    Show,
    Hide
};

/// CSS `border-style` / `outline-style` values (CSS 2.1 8.5.3). `none` and
/// `hidden` both paint nothing. `hidden`'s table-model suppression is not
/// implemented (see phase 512).
enum class BorderStyle : uint8_t {
    None,
    Hidden,
    Dotted,
    Dashed,
    Solid,
    Double,
    Groove,
    Ridge,
    Inset,
    Outset
};

/// One style per edge, parallel to EdgeSizes. Initial value `none` on every
/// edge (CSS 2.1 8.5.3).
struct EdgeStyles {
    BorderStyle top{BorderStyle::None};
    BorderStyle right{BorderStyle::None};
    BorderStyle bottom{BorderStyle::None};
    BorderStyle left{BorderStyle::None};

    constexpr EdgeStyles() = default;
    constexpr EdgeStyles(BorderStyle t, BorderStyle r, BorderStyle b, BorderStyle l)
        : top(t), right(r), bottom(b), left(l) {}
    constexpr explicit EdgeStyles(BorderStyle all) : top(all), right(all), bottom(all), left(all) {}

    bool operator==(const EdgeStyles& other) const = default;
};

/// Whether a style draws anything at all (`none` and `hidden` do not).
constexpr bool border_style_draws(BorderStyle style) {
    return style != BorderStyle::None && style != BorderStyle::Hidden;
}

/// CSS `caption-side` (CSS 2.1 17.4.1, `top` and `bottom` only).
enum class CaptionSide : uint8_t {
    Top,
    Bottom
};

/// CSS `float` values.
enum class FloatType : uint8_t {
    None,
    Left,
    Right
};

/// CSS `clear` values.
enum class ClearType : uint8_t {
    None,
    Left,
    Right,
    Both
};

enum class FlexDirection : uint8_t {
    Row,
    Column,
    RowReverse,
    ColumnReverse
};

enum class FlexWrap : uint8_t {
    NoWrap,
    Wrap,
    WrapReverse
};

enum class JustifyContent : uint8_t {
    FlexStart,
    FlexEnd,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
};

enum class AlignItems : uint8_t {
    Stretch,
    FlexStart,
    FlexEnd,
    Center
};

enum class AlignContent : uint8_t {
    Stretch,
    FlexStart,
    FlexEnd,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
};

enum class AlignSelf : uint8_t {
    Auto,
    Stretch,
    FlexStart,
    FlexEnd,
    Center
};

enum class Position : uint8_t {
    Static,
    Relative,
    Absolute,
    Fixed
};

enum class BoxSizing : uint8_t {
    ContentBox,
    BorderBox
};

/// CSS `font-style`.
///
/// `Oblique` is kept apart from `Italic` even though the renderer treats them
/// alike, because they are different questions to a font: a face declares
/// itself one or the other, and `@font-face { font-style: oblique }` must find
/// the oblique face and not the italic one. They are answered alike by the
/// *fallback* path only -- neither can be had, so both are synthesised by the
/// same shear.
enum class FontStyle : uint8_t {
    Normal,
    Italic,
    Oblique
};

/// CSS `font-kerning`: `auto | normal | none`.
///
/// The three values are two behaviours. `auto` lets the user agent decide and
/// `normal` demands kerning; this engine applies the `kern` table in both, which
/// is a decision `auto` explicitly permits. `none` turns it off, and that is the
/// only value with a visible difference — it is why the property is worth
/// carrying down to the pen at all.
///
/// Kept as the specified value rather than a bool because `getComputedStyle`
/// has to answer with the word the author wrote, and because the difference
/// between `auto` and `normal` is one a browser can legitimately resolve either
/// way.
enum class FontKerning : uint8_t {
    Auto,
    Normal,
    None
};

enum class TextAlign : uint8_t {
    Left,
    Center,
    Right,
    Justify,
    Start,
    End
};

enum class WhiteSpace : uint8_t {
    Normal,
    Nowrap,
    Pre,
    PreWrap,
    PreLine,
    BreakSpaces
};

enum class VerticalAlign : uint8_t {
    Baseline,
    Top,
    Bottom,
    Middle,
    TextTop,
    TextBottom,
    Sub,
    Super
};

enum class Visibility : uint8_t {
    Visible,
    Hidden,
    Collapse
};

struct GradientStop {
    Color color{Color::transparent()};
    float position{-1.0f}; // Normalized 0..1 stop position, or -1 if unspecified

    bool operator==(const GradientStop& other) const = default;
};

struct LinearGradient {
    /// CSS gradient angle in degrees: 0deg = to top, 90deg = to right,
    /// 180deg = to bottom (the CSS Images 3 default), 270deg = to left.
    float angle_deg{180.0f};
    std::vector<GradientStop> stops;

    bool operator==(const LinearGradient& other) const = default;
};

enum class TextDecoration : uint8_t {
    None = 0,
    Underline = 1 << 0,
    LineThrough = 1 << 1,
    Overline = 1 << 2
};

inline TextDecoration operator|(TextDecoration a, TextDecoration b) {
    return static_cast<TextDecoration>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline TextDecoration& operator|=(TextDecoration& a, TextDecoration b) {
    a = a | b;
    return a;
}

enum class TimingFunction : uint8_t {
    Ease = 0,
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    StepStart,
    StepEnd
};

struct BoxShadow {
    float x_offset{0.0f};
    float y_offset{0.0f};
    float blur{0.0f};
    Color color{Color::transparent()};
    bool has_shadow{false};

    bool operator==(const BoxShadow& other) const = default;
};

struct CounterOp {
    std::string name;
    int value{0};
    bool operator==(const CounterOp& other) const = default;
};

struct ComputedStyle {
    Display display{Display::Inline};
    /// CSS Generated Content 3 `content`. Only a string value is represented:
    /// empty means `normal`/`none` (no box is generated), which is what the
    /// property computes to when the page says nothing. Phase 215 makes
    /// `::before`/`::after` with a non-empty string generate an inline run.
    std::string content;
    /// Raw `content` value before `counter()` expansion, kept for layout-time
    /// counter evaluation (Phase 379). Empty when `content` was not declared.
    std::string content_raw;
    /// CSS Counter Styles: `counter-reset`, `counter-increment`, `counter-set`
    /// (Phase 379). Each is a list of name+value pairs; `none` is empty.
    std::vector<CounterOp> counter_reset;
    std::vector<CounterOp> counter_increment;
    std::vector<CounterOp> counter_set;
    Color color{Color::black()};
    Color background_color{Color::transparent()};
    bool has_background_image{false};
    std::optional<LinearGradient> background_gradient;
    bool dark_color_scheme{false};
    float width{-1.0f};  // -1 indicates auto
    float height{-1.0f}; // -1 indicates auto
    /// `width: fit-content`. CSS Sizing 3 clamps the max-content size between
    /// the min-content size and the available width. This cannot be computed in
    /// the cascade, so `width` itself stays auto and layout measures both
    /// intrinsic bounds. Added for the dialog UA rule (Phase 223), where the
    /// standard uses this sizing keyword.
    bool width_fit_content{false};
    /// `height: fit-content`: the box is as tall as its content, and does not
    /// stretch between `top` and `bottom`. The two together are how the HTML
    /// rendering standard centres a modal dialog.
    bool height_fit_content{false};
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
    /// `border-style`, one per edge, parallel to `border_width`.
    EdgeStyles border_style{};
    Color border_color{Color::black()};
    /// `outline`, which a box draws around its border box without taking any
    /// room (`outline-offset` moves it out; a negative one pulls it in). It is
    /// not part of the box model, so it is not an `EdgeSizes`: it is one ring
    /// of one width, and `outline_style` is the `outline-style` that decides
    /// whether there is a ring at all (`none` draws nothing, whatever the
    /// width says) and how it is painted.
    float outline_width{0.0f};
    float outline_offset{0.0f};
    BorderStyle outline_style{BorderStyle::None};
    Color outline_color{Color::black()};
    float border_radius{0.0f};
    float border_radius_percent{-1.0f};
    BoxShadow box_shadow;
    float font_size{16.0f};
    int font_weight{400};
    /// `font-style`. Inherited, like every other font property — which is why
    /// leaving it out of `inherited_only_style` would make `<em>` upright for
    /// exactly one line of text and slanted in the next. Ask Phase 74's
    /// `font-family` how that goes.
    FontStyle font_style{FontStyle::Normal};
    /// `font-kerning`. Inherited, like the rest of the font group: `none` on a
    /// `<p>` means its `<span>`s are unkerned too, and a property that stopped
    /// at the element would leave the very children that hold the text kerning.
    FontKerning font_kerning{FontKerning::Auto};

    /// Whether the pair adjustments of the face are applied to this text.
    bool kerning_enabled() const { return font_kerning != FontKerning::None; }
    /// The `font-family` list exactly as it was written — quotes, commas and
    /// all. It is not resolved here: which file on the host answers to a name
    /// is the font layer's business, and layout is where it is asked, once per
    /// measured run of text. Empty means "inherit whichever family the parent
    /// got", which for the root is the engine's default face.
    std::string font_family;
    // Line height: a -1 sentinel means "not declared" (normal; layout falls
    // back to 1.25 * font_size). line_height holds a computed px length;
    // line_height_number holds a bare unitless multiplier. Both inherit per
    // CSS: a declared multiplier is inherited as a multiplier, a declared
    // length or percentage is inherited as a length.
    float line_height{-1.0f};
    float line_height_number{-1.0f};
    TextAlign text_align{TextAlign::Left};
    TextDecoration text_decoration{TextDecoration::None};
    WhiteSpace white_space{WhiteSpace::Normal};
    VerticalAlign vertical_align{VerticalAlign::Baseline};
    Visibility visibility{Visibility::Visible};

    bool is_visible() const { return visibility == Visibility::Visible; }
    bool preserves_newlines() const {
        return white_space == WhiteSpace::Pre || white_space == WhiteSpace::PreWrap ||
               white_space == WhiteSpace::PreLine || white_space == WhiteSpace::BreakSpaces;
    }
    bool preserves_spaces() const {
        return white_space == WhiteSpace::Pre || white_space == WhiteSpace::PreWrap ||
               white_space == WhiteSpace::BreakSpaces;
    }

    bool has_underline() const {
        return (static_cast<uint8_t>(text_decoration) & static_cast<uint8_t>(TextDecoration::Underline)) != 0;
    }
    bool has_line_through() const {
        return (static_cast<uint8_t>(text_decoration) & static_cast<uint8_t>(TextDecoration::LineThrough)) != 0;
    }
    bool has_overline() const {
        return (static_cast<uint8_t>(text_decoration) & static_cast<uint8_t>(TextDecoration::Overline)) != 0;
    }
    bool has_text_decoration() const {
        return text_decoration != TextDecoration::None;
    }

    // The line height layout should use for this element.
    float effective_line_height() const {
        if (line_height_number >= 0.0f) return line_height_number * font_size;
        if (line_height >= 0.0f) return line_height;
        return font_size * 1.25f;
    }

    bool has_percent_lengths() const {
        return width_percent >= 0.0f || height_percent >= 0.0f ||
               min_width_percent >= 0.0f || max_width_percent >= 0.0f ||
               min_height_percent >= 0.0f || max_height_percent >= 0.0f ||
               margin_percent.top >= 0.0f || margin_percent.right >= 0.0f ||
               margin_percent.bottom >= 0.0f || margin_percent.left >= 0.0f ||
               padding_percent.top >= 0.0f || padding_percent.right >= 0.0f ||
               padding_percent.bottom >= 0.0f || padding_percent.left >= 0.0f;
    }

    // Floats and clearance (CSS 2.1, sections 9.5 and 9.5.2)
    FloatType float_type{FloatType::None};
    ClearType clear_type{ClearType::None};
    Overflow overflow{Overflow::Visible};

    // Tables: border model and the space between cells (CSS 2.1 17.6.1-2).
    BorderCollapse border_collapse{BorderCollapse::Separate};
    float border_spacing_x{0.0f};
    float border_spacing_y{0.0f};
    TableLayout table_layout{TableLayout::Auto};
    EmptyCells empty_cells{EmptyCells::Show};
    CaptionSide caption_side{CaptionSide::Top};

    // Positioning properties
    Position position{Position::Static};
    float top{0.0f};
    float right{0.0f};
    float bottom{0.0f};
    float left{0.0f};
    // Keep the signed percentage component until the containing block is known.
    EdgeSizes inset_percent{};
    EdgeSizes used_insets(float width, float height) const {
        return {top + height * inset_percent.top / 100, right + width * inset_percent.right / 100,
                bottom + height * inset_percent.bottom / 100, left + width * inset_percent.left / 100};
    }
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
    AlignSelf align_self{AlignSelf::Auto};
    AlignItems justify_items{AlignItems::Stretch};
    AlignSelf justify_self{AlignSelf::Auto};
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

        bool operator==(const GridTrackDef& other) const = default;
    };

    std::vector<GridTrackDef> grid_col_track_defs;
    std::vector<GridTrackDef> grid_row_track_defs;
    bool grid_auto_fit_columns{false};
    GridTrackDef grid_auto_fit_def;

    // Transitions and Animations
    float opacity{1.0f};
    std::string animation_name;
    float animation_duration{0.0f}; // in seconds
    TimingFunction animation_timing_function{TimingFunction::Ease};
    float animation_iteration_count{1.0f}; // -1 for infinite
    std::string transition_property;
    float transition_duration{0.0f};
    TimingFunction transition_timing_function{TimingFunction::Ease};
    float transition_delay{0.0f};

    // 2D Transforms
    bool has_transform{false};
    TransformMatrix2D transform_matrix{TransformMatrix2D::identity()};
    std::string transform_str;
    std::string transform_origin; // empty means default "50% 50%"

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

        bool operator==(const GridAreaRect& other) const = default;
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
    bool is_table_caption() const { return display == Display::TableCaption; }
    bool is_table_row_group() const { return display == Display::TableRowGroup; }
    bool is_table_column() const { return display == Display::TableColumn; }
    bool is_table_column_group() const { return display == Display::TableColumnGroup; }
    bool is_table_internal() const {
        return is_table_row() || is_table_cell() || is_table_row_group() ||
               is_table_column() || is_table_column_group();
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
    /// `display: contents` -- no box of its own, children spliced into the
    /// parent. See `Display::Contents`.
    bool is_contents() const { return display == Display::Contents; }

    bool is_positioned() const { return position != Position::Static; }
    bool is_out_of_flow() const { return position == Position::Absolute || position == Position::Fixed; }

    // Every member, in the order they are declared: the compiler writes it,
    // so a property added to this struct cannot be left out of it.
    // ComputedStylePool shares one style between boxes whose styles compare
    // equal; while this was written by hand, a member missing here was a
    // property that every such box silently took from another (font_style,
    // when Phase 78 met Phase 71). hash() is still by hand, and a member
    // missing there costs a longer bucket and nothing else.
    bool operator==(const ComputedStyle& other) const = default;
    std::size_t hash() const;
};

using SharedComputedStyle = std::shared_ptr<const ComputedStyle>;

/// Process-lifetime canonical empty/default ComputedStyle shared across empty
/// boxes, fragments, and unstyled text nodes.
const SharedComputedStyle& default_computed_style();

/// Deduplicates identical ComputedStyle objects during layout tree construction
/// and inline formatting so repeated elements and fragments share one immutable
/// style instance instead of copying the full struct per box and word.
class ComputedStylePool {
public:
    ComputedStylePool();

    SharedComputedStyle intern(ComputedStyle&& style);
    SharedComputedStyle intern(const ComputedStyle& style);
    SharedComputedStyle inherited_only(const SharedComputedStyle& parent, Display display = Display::Inline);

    std::size_t unique_count() const;

private:
    std::unordered_map<std::size_t, std::vector<SharedComputedStyle>> m_buckets;
    std::unordered_map<const ComputedStyle*, SharedComputedStyle> m_inherited_inline_cache;
    std::unordered_map<const ComputedStyle*, SharedComputedStyle> m_inherited_block_cache;
};

class StyleResolver {
public:
    StyleResolver();

    void add_stylesheet(const StyleSheet& sheet);
    void set_viewport(float w, float h) { m_viewport_width = w; m_viewport_height = h; }
    float viewport_width() const { return m_viewport_width; }
    float viewport_height() const { return m_viewport_height; }

    ComputedStyle resolve_style(const std::shared_ptr<Element>& element, const ComputedStyle* parent_style = nullptr) const;

    /// The style of `element`'s `::before` (`which` 1) or `::after` (`which` 2)
    /// box, computed against the element's own style: what the standard calls
    /// the pseudo-element's computed values. Only selectors that name that
    /// pseudo-element take part, which is why the ordinary matcher cannot be
    /// used -- it rejects every `::` selector (Phase 215). `base` is the
    /// element's style and everything generated inherits through it.
    ComputedStyle resolve_pseudo_element(const std::shared_ptr<Element>& element, int which,
                                         const ComputedStyle& base) const;
    /// Whether `selector` -- which must name pseudo-element `which` -- applies
    /// to `element`. The ordinary matcher refuses every `::` selector.
    bool matches_selector_for_pseudo(const std::shared_ptr<Element>& element,
                                     const Selector& selector, int which) const;

    const KeyframesRule* find_keyframes(const std::string& name) const;
    void apply_animation(ComputedStyle& style, float elapsed_seconds) const;
    static void interpolate_styles(ComputedStyle& target, const ComputedStyle& from, const ComputedStyle& to, float t);

    // CSS Variables and Calc Resolvers
    std::string resolve_var_functions(const std::string& value, const std::unordered_map<std::string, std::string>& custom_props, int depth = 0) const;
    /// `valid`, when given, is set to false for an expression that cannot be
    /// evaluated (it nests more deeply than the parser follows).
    float evaluate_calc_expression(std::string_view expr, float reference_size = 0.0f, bool* valid = nullptr) const;
    std::string resolve_calc_functions(const std::string& value, float reference_size = 0.0f) const;
    /// Evaluate a `content` declaration for a pseudo-element. Handles a list of
    /// strings and `attr()` (with optional fallback), `open-quote` /
    /// `close-quote`, and skips `url()` / `counter()` items rather than
    /// treating the whole declaration as invalid (Phase 364).
    std::string evaluate_content(const std::string& raw, const Element* element,
                                 const std::unordered_map<std::string, std::string>& vars) const;
    /// Counter-aware evaluation: `counter()` and `counters()` are resolved
    /// against `counters` (each counter is a stack of ints for nesting).
    /// When `counters` is null, counter items are skipped (Phase 364 compat).
    std::string evaluate_content(const std::string& raw, const Element* element,
                                 const std::unordered_map<std::string, std::string>& vars,
                                 const std::unordered_map<std::string, std::vector<int>>* counters) const;
    /// Parse `counter-*` properties into `ComputedStyle` (Phase 379).
    static std::vector<CounterOp> parse_counter_ops(const std::string& raw, int default_value);

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

    // The legacy UA sheet declares an opaque white body, although the initial
    // CSS background is transparent. Normalize it at the same UA origin: an
    // author rule (even `*`, or one inside a layer) must still be able to set
    // white deliberately. Keep this correction beside the UA sheet rather
    // than treating a computed white color as evidence of where it came from.
    // Once the old declaration is removed from default_user_agent_stylesheet,
    // this one-rule normalization can be removed too.
    struct UserAgentSheet : StyleSheet {
        explicit UserAgentSheet(StyleSheet sheet) : StyleSheet(std::move(sheet)) {}
    };

    UserAgentSheet m_ua_sheet;
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
