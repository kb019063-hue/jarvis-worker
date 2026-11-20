#pragma once

#include <optional>
#include "litebird/dom/Node.hpp"
#include "litebird/css/StyleResolver.hpp"
#include "litebird/layout/Dimensions.hpp"
#include "litebird/render/ImageDecoder.hpp"
#include <vector>
#include <memory>
#include <string>
#include <iostream>

namespace litebird {

enum class BoxType {
    BlockNode,
    InlineNode,
    FlexNode,
    GridNode,
    ImageNode,
    AnonymousBlock,
    TextNode,
    TableNode,
    TableRowNode,
    TableCellNode
};

/// A horizontal exclusion created by a float, in the coordinate space of the
/// box that owns the float context. `left`/`right` are the intrusion depths
/// from the content edges.
struct FloatBand {
    float top{0.0f};
    float bottom{0.0f};
    float left{0.0f};
    float right{0.0f};
};

class LayoutBox;

struct InlineFragment {
    Rect rect;                      // Exact bounds of fragment (x, y, w, h)
    std::string text;               // Word or text content
    SharedComputedStyle style{default_computed_style()}; // Visual styling for this fragment
    std::shared_ptr<LayoutBox> box; // Originating layout box
    bool is_text{true};
    bool is_space{false};
    bool is_rtl_visual{false};      // True when `apply_bidi_to_line` rewrote an odd-level run visually
};

struct LineBox {
    Rect rect;                            // Overall line bounds (x, y, w, h)
    float baseline{0.0f};                 // Distance from top of line to font baseline
    float left_intrusion{0.0f};           // Space taken by floats on this line
    float right_intrusion{0.0f};
    std::vector<InlineFragment> fragments; // Inline fragments forming this line
};

class LayoutBox : public std::enable_shared_from_this<LayoutBox> {
public:
    BoxType box_type{BoxType::BlockNode};
    BoxDimensions dimensions;
    SharedComputedStyle style;
    std::shared_ptr<Node> node;
    std::string text;
    std::shared_ptr<ImageData> image;
    std::vector<std::string> lines; // Multi-line wrapped text
    std::vector<LineBox> line_boxes; // Populated for block boxes establishing an IFC
    std::vector<std::shared_ptr<LayoutBox>> children;
    std::weak_ptr<LayoutBox> parent;

    LayoutBox(BoxType type, std::shared_ptr<Node> n, SharedComputedStyle s)
        : box_type(type),
          style(s ? std::move(s) : default_computed_style()),
          node(std::move(n)) {}

    static std::shared_ptr<LayoutBox> create_block(std::shared_ptr<Node> n, SharedComputedStyle s) {
        return std::make_shared<LayoutBox>(BoxType::BlockNode, std::move(n), std::move(s));
    }

    static std::shared_ptr<LayoutBox> create_flex(std::shared_ptr<Node> n, SharedComputedStyle s) {
        return std::make_shared<LayoutBox>(BoxType::FlexNode, std::move(n), std::move(s));
    }

    static std::shared_ptr<LayoutBox> create_grid(std::shared_ptr<Node> n, SharedComputedStyle s) {
        return std::make_shared<LayoutBox>(BoxType::GridNode, std::move(n), std::move(s));
    }

    static std::shared_ptr<LayoutBox> create_table(std::shared_ptr<Node> n, SharedComputedStyle s) {
        return std::make_shared<LayoutBox>(BoxType::TableNode, std::move(n), std::move(s));
    }

    static std::shared_ptr<LayoutBox> create_table_row(std::shared_ptr<Node> n, SharedComputedStyle s) {
        return std::make_shared<LayoutBox>(BoxType::TableRowNode, std::move(n), std::move(s));
    }

    static std::shared_ptr<LayoutBox> create_table_cell(std::shared_ptr<Node> n, SharedComputedStyle s) {
        return std::make_shared<LayoutBox>(BoxType::TableCellNode, std::move(n), std::move(s));
    }

    static std::shared_ptr<LayoutBox> create_inline(std::shared_ptr<Node> n, SharedComputedStyle s) {
        return std::make_shared<LayoutBox>(BoxType::InlineNode, std::move(n), std::move(s));
    }

    static std::shared_ptr<LayoutBox> create_image(std::shared_ptr<Node> n, SharedComputedStyle s, std::shared_ptr<ImageData> img = nullptr) {
        auto box = std::make_shared<LayoutBox>(BoxType::ImageNode, std::move(n), std::move(s));
        box->image = std::move(img);
        return box;
    }

    static std::shared_ptr<LayoutBox> create_anonymous(SharedComputedStyle s) {
        return std::make_shared<LayoutBox>(BoxType::AnonymousBlock, nullptr, std::move(s));
    }

    static std::shared_ptr<LayoutBox> create_text(std::shared_ptr<Node> n, std::string t, SharedComputedStyle s) {
        auto box = std::make_shared<LayoutBox>(BoxType::TextNode, std::move(n), std::move(s));
        box->text = std::move(t);
        return box;
    }

    void append_child(std::shared_ptr<LayoutBox> child) {
        if (!child) return;
        child->parent = shared_from_this();
        children.push_back(child);
    }

    bool establishes_ifc() const;

    /// True when this box establishes a block formatting context, which means
    /// it contains its own floats (CSS 2.1 section 9.4.1).
    bool establishes_bfc() const;

    /// Table cells in a collapsed-border table share their borders with their
    /// neighbours, so each one only paints half. Layout scales the used border
    /// width by this factor (always recomputed from the style, so relayout is
    /// idempotent).
    float border_scale{1.0f};
    /// Used border widths for one box, or empty to use style.border_width.
    /// Collapsed tables resolve the shared edges once (widest wins) and hand
    /// the winner to every cell it touches; the half-paint model stays.
    std::optional<EdgeSizes> border_override{};
    /// Winning border colours per edge (collapsed border resolution hands the
    /// winner its colour along with its width).
    std::optional<EdgeColors> border_color_override{};
    /// Force the used width from layout() to fill the containing width,
    /// ignoring style.width. Fixed table layout: a cell's own width must
    /// not resize its column after the first row was read.
    bool ignore_own_width{false};

    /// A table cell resolves percentage lengths against the *table's* width,
    /// not against the column width it happens to be laid out with (CSS 2.1
    /// 17.5.2). Negative means "use the containing block".
    float width_basis_override{-1.0f};

    // --- float context -----------------------------------------------------
    // Floats declared inside this box, in this box's content coordinates.
    // A float of no height covers no position and has no band here.
    std::vector<FloatBand> float_bands;
    // Bands inherited from the nearest ancestor that owns floats, plus this
    // box's content origin relative to that ancestor.
    const std::vector<FloatBand>* inherited_bands{nullptr};
    float inherited_bands_offset_y{0.0f};

    /// Horizontal intrusions at content-relative `y`.
    static void band_at(const std::vector<FloatBand>& bands, float y, float& left, float& right);
    void layout(float containing_width, float containing_height = 0.0f);
    void dump(std::ostream& os, int indent = 0) const;
    void shift_position(float dx, float dy);

    bool needs_layout{true};
    bool child_needs_layout{true};

    void mark_needs_layout() {
        needs_layout = true;
        auto p = parent.lock();
        while (p) {
            if (p->style->contains_layout()) {
                p->child_needs_layout = true;
                break;
            }
            if (p->child_needs_layout) break;
            p->child_needs_layout = true;
            p = p->parent.lock();
        }
    }

    void clear_layout_flags() {
        needs_layout = false;
        child_needs_layout = false;
    }

    static void resolve_out_of_flow_positions(const std::shared_ptr<LayoutBox>& root, float viewport_w, float viewport_h);

private:
    /// Turns percentage lengths into pixels against this box's containing
    /// block. The percentage fields are kept, so a relayout at a different
    /// size recomputes them instead of reusing a stale pixel value.
    void resolve_percentages(float containing_width, float containing_height);

    /// Copies margin/padding/border from the computed style onto the used
    /// dimensions, scaling the border by `border_scale` (collapsed table
    /// cells). Idempotent. Only `layout()` paths used to do this, which is
    /// why a float or a table cell laid out directly lost its edges.
    void apply_style_edges();

    void layout_block(float containing_width);
    void layout_table(float containing_width);
    void layout_table_row(float containing_width);
    void layout_table_cell(float containing_width);
    /// Lays a floated child out with a shrink-to-fit width.
    void layout_float(float containing_width);
    /// Largest unbreakable inline content width, used as a table column floor.
    static float min_content_width(const LayoutBox& box, float fallback);
    void layout_flex(float containing_width);
    void layout_grid(float containing_width);
    void layout_image(float containing_width);
    void layout_inline(float containing_width);
    void layout_text(float containing_width);
    void layout_inline_formatting_context(float containing_width);
};

} // namespace litebird
