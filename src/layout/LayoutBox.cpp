#include "litebird/layout/LayoutBox.hpp"
#include "litebird/common/StackLimit.hpp"
#include "litebird/render/FontRenderer.hpp"
#include "litebird/text/Bidi.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <numeric>
#include <unordered_map>
#include <map>
#include <deque>
#include <limits>
#include <optional>
#include <vector>

namespace litebird {

namespace {

/// One fragment's text, decoded, with the byte offset each code point starts
/// at (so a run can be cut back out of the UTF-8 without re-encoding it).
struct DecodedFragment {
    std::vector<uint32_t> codepoints;
    std::vector<std::size_t> byte_at;
};

DecodedFragment decode_utf8_with_offsets(std::string_view text) {
    DecodedFragment out;
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        uint32_t cp = lead;
        if ((lead & 0xE0) == 0xC0) {
            length = 2;
            cp = lead & 0x1Fu;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            cp = lead & 0x0Fu;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            cp = lead & 0x07u;
        } else if (lead >= 0x80) {
            out.codepoints.push_back(0xFFFD);
            out.byte_at.push_back(i);
            ++i;
            continue;
        }
        if (i + length > text.size()) length = 1;
        if (length > 1) {
            bool valid = true;
            for (std::size_t k = 1; k < length; ++k) {
                const unsigned char continuation = static_cast<unsigned char>(text[i + k]);
                if ((continuation & 0xC0) != 0x80) {
                    valid = false;
                    break;
                }
                cp = (cp << 6) | (continuation & 0x3Fu);
            }
            if (!valid) {
                out.codepoints.push_back(0xFFFD);
                out.byte_at.push_back(i);
                ++i;
                continue;
            }
        }
        out.codepoints.push_back(cp);
        out.byte_at.push_back(i);
        i += length;
    }
    return out;
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/// UAX #9 rule L2 over items whose levels are uniform: from the highest level
/// down to the lowest odd level, reverse every run at or above it.
std::vector<std::size_t> reorder_by_level(const std::vector<int>& levels) {
    std::vector<std::size_t> order(levels.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    if (order.size() < 2) return order;
    int highest = 0;
    int lowest_odd = 1 << 20;
    for (int level : levels) {
        highest = std::max(highest, level);
        if (level % 2 == 1) lowest_odd = std::min(lowest_odd, level);
    }
    for (int level = highest; level >= lowest_odd; --level) {
        for (std::size_t k = 0; k < order.size();) {
            if (levels[order[k]] < level) {
                ++k;
                continue;
            }
            std::size_t end = k;
            while (end + 1 < order.size() && levels[order[end + 1]] >= level) ++end;
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(k),
                         order.begin() + static_cast<std::ptrdiff_t>(end + 1));
            k = end + 1;
        }
    }
    return order;
}

/// UAX #9 for one laid-out line, rules L2 and L4.
///
/// The inline formatting context places fragments in logical order, left to
/// right, which is right for every line of Latin script and wrong for a Hebrew
/// word (one fragment, drawn mirrored) and for a line that mixes directions
/// (the runs themselves come out in the wrong order). This splits a fragment
/// at a change of level, reorders the pieces by their levels, and mirrors the
/// characters of the odd ones. A line with no right-to-left character in it
/// returns here without touching anything, which is why 99% of pages are
/// unaffected by this pass.
void apply_bidi_to_line(LineBox& line) {
    if (line.fragments.empty()) return;
    // Every right-to-left script is above U+0080, and the Latin pages that
    // make up almost all of what this engine lays out are pure ASCII. One
    // scan of the bytes answers for them without decoding anything.
    bool non_ascii = false;
    for (const InlineFragment& fragment : line.fragments) {
        for (char c : fragment.text) {
            if (static_cast<unsigned char>(c) >= 0x80) {
                non_ascii = true;
                break;
            }
        }
        if (non_ascii) break;
    }
    if (!non_ascii) return;

    std::vector<DecodedFragment> decoded(line.fragments.size());
    std::u32string text;
    std::vector<std::size_t> fragment_of;
    std::vector<std::size_t> char_in;
    for (std::size_t f = 0; f < line.fragments.size(); ++f) {
        const InlineFragment& fragment = line.fragments[f];
        if (fragment.is_text) {
            decoded[f] = decode_utf8_with_offsets(fragment.text);
        } else {
            // CSS 2.1 asks for U+FFFC in place of a non-text inline: it is one
            // neutral character as wide as the box.
            decoded[f].codepoints.push_back(0xFFFC);
            decoded[f].byte_at.push_back(0);
        }
        for (std::size_t k = 0; k < decoded[f].codepoints.size(); ++k) {
            text.push_back(decoded[f].codepoints[k]);
            fragment_of.push_back(f);
            char_in.push_back(k);
        }
    }
    if (text.empty()) return;

    const BidiParagraph paragraph = bidi_paragraph(text);
    // UAX #9 X9 marks BN as removed for bidi resolution, including ZWNJ/ZWJ,
    // which OpenType shaping still needs inside Arabic/Indic runs.
    auto is_bidi_removed = [&](std::size_t idx) {
        return paragraph.removed[idx] && text[idx] != 0x200C && text[idx] != 0x200D;
    };
    bool right_to_left = false;
    for (std::size_t k = 0; k < text.size(); ++k) {
        if (is_bidi_removed(k)) continue;
        if (paragraph.levels[k] % 2 == 1) {
            right_to_left = true;
            break;
        }
    }
    if (!right_to_left) return;

    struct Run {
        std::size_t fragment;
        std::size_t first_char;
        std::size_t last_char;  // exclusive
        int level;
        float width{0.0f};
    };
    std::vector<Run> runs;
    for (std::size_t k = 0; k < text.size();) {
        if (is_bidi_removed(k)) {
            ++k;
            continue;
        }
        const std::size_t f = fragment_of[k];
        const int level = paragraph.levels[k];
        std::size_t end = k + 1;
        while (end < text.size() && !is_bidi_removed(end) && fragment_of[end] == f &&
               paragraph.levels[end] == level) {
            ++end;
        }
        runs.push_back(Run{f, char_in[k], char_in[end - 1] + 1, level, 0.0f});
        k = end;
    }
    if (runs.empty()) return;

    FontRenderer& font_renderer = FontRenderer::instance();
    std::vector<float> measured(line.fragments.size(), 0.0f);
    for (Run& run : runs) {
        const InlineFragment& fragment = line.fragments[run.fragment];
        const DecodedFragment& source = decoded[run.fragment];
        const bool whole = run.first_char == 0 && run.last_char == source.codepoints.size();
        if (!fragment.is_text || whole) {
            run.width = fragment.rect.width;
            measured[run.fragment] += run.width;
            continue;
        }
        const std::size_t first_byte = source.byte_at[run.first_char];
        const std::size_t last_byte = run.last_char < source.byte_at.size()
                                          ? source.byte_at[run.last_char]
                                          : fragment.text.size();
        const auto& style = *fragment.style;
        run.width = font_renderer.measure_text(
            std::string_view(fragment.text).substr(first_byte, last_byte - first_byte),
            style.font_size, style.font_weight, style.font_family,
            style.font_style != FontStyle::Normal, style.kerning_enabled());
        measured[run.fragment] += run.width;
    }
    // A fragment cut into runs must still occupy the width the line was broken
    // for, so the pieces share the fragment's own width in proportion.
    for (Run& run : runs) {
        const float total = measured[run.fragment];
        if (total <= 0.0f) continue;
        run.width *= line.fragments[run.fragment].rect.width / total;
    }

    std::vector<int> levels;
    levels.reserve(runs.size());
    for (const Run& run : runs) levels.push_back(run.level);
    const std::vector<std::size_t> order = reorder_by_level(levels);

    float cursor = line.fragments.front().rect.x;
    for (const InlineFragment& fragment : line.fragments) cursor = std::min(cursor, fragment.rect.x);

    std::vector<InlineFragment> visual;
    visual.reserve(runs.size());
    for (std::size_t index : order) {
        const Run& run = runs[index];
        const InlineFragment& source = line.fragments[run.fragment];
        InlineFragment piece = source;
        piece.rect.x = cursor;
        piece.rect.width = run.width;
        cursor += run.width;
        if (source.is_text && run.level % 2 == 1) {
            // The painter draws a run's characters left to right, so an
            // odd-level run is written out backwards (that is L2 inside the
            // run) with each character mirrored (that is L4). `is_rtl_visual`
            // lets the OpenType shaper reverse back to logical order before
            // joining/anchoring Arabic/Hebrew glyphs and then reorder clusters.
            const DecodedFragment& decoded_source = decoded[run.fragment];
            piece.text.clear();
            for (std::size_t k = run.last_char; k-- > run.first_char;) {
                append_utf8(piece.text, bidi_mirror(decoded_source.codepoints[k]));
            }
            piece.is_rtl_visual = true;
        } else {
            piece.is_rtl_visual = false;
        }
        visual.push_back(std::move(piece));
    }
    line.fragments = std::move(visual);
}

/// Where a float is placed and how much of each side it takes intruded at
/// that position (the running maxima at the placement y, not before it).
struct FloatPlacement {
    float top{0.0f};
    float left_intrusion{0.0f};
    float right_intrusion{0.0f};
};

/// An ordered partition of the vertical axis. Each edge holds the running
/// maximum intrusions up to the next edge; the treap augments every subtree
/// with its largest available width. A placement finds the first fitting edge
/// after y0 without revisiting earlier rows. Adding a band only touches the
/// intervals it actually overlaps (rather than sorting every previous band).
/// The partition is local to one block's layout and does not change the bands
/// exposed to inline layout or the accounting of zero-height floats.
class FloatPlacementIndex {
    struct Edge {
        float y, left = 0.0f, right = 0.0f;
        bool occupied = false;
        float capacity, max_capacity;
        unsigned priority;
        Edge *parent = nullptr, *low = nullptr, *high = nullptr;
    };
    float width_;
    unsigned seed_ = 0x7548a301u;
    std::deque<Edge> storage_;
    std::map<float, Edge*> edges_;
    Edge* root_ = nullptr;

    unsigned next_priority() {
        seed_ ^= seed_ << 13;
        seed_ ^= seed_ >> 17;
        seed_ ^= seed_ << 5;
        return seed_;
    }
    static void refresh(Edge* e) {
        e->max_capacity = e->capacity;
        if (e->low) e->max_capacity = std::max(e->max_capacity, e->low->max_capacity);
        if (e->high) e->max_capacity = std::max(e->max_capacity, e->high->max_capacity);
    }
    static void refresh_up(Edge* e) {
        while (e) { refresh(e); e = e->parent; }
    }
    void rotate(Edge* e) {
        Edge* p = e->parent;
        Edge* g = p->parent;
        if (e == p->low) {
            p->low = e->high;
            if (p->low) p->low->parent = p;
            e->high = p;
        } else {
            p->high = e->low;
            if (p->high) p->high->parent = p;
            e->low = p;
        }
        p->parent = e;
        e->parent = g;
        if (!g) root_ = e;
        else if (g->low == p) g->low = e;
        else g->high = e;
        refresh(p);
        refresh_up(e);
    }
    Edge* edge_at(float y) {
        auto it = edges_.lower_bound(y);
        if (it != edges_.end() && it->first == y) return it->second;
        const Edge* previous = std::prev(it)->second; // lowest() sentinel
        storage_.push_back(Edge{y, previous->left, previous->right, previous->occupied,
                                previous->capacity, previous->capacity, next_priority()});
        Edge* e = &storage_.back();
        edges_.emplace_hint(it, y, e);
        Edge* p = root_;
        while (true) {
            Edge*& slot = y < p->y ? p->low : p->high;
            if (!slot) { slot = e; e->parent = p; break; }
            p = slot;
        }
        refresh_up(p);
        while (e->parent && e->priority < e->parent->priority) rotate(e);
        return e;
    }
    static Edge* first_fit_after(Edge* e, float y, float needed) {
        if (!e || e->max_capacity < needed) return nullptr;
        if (e->y <= y) return first_fit_after(e->high, y, needed);
        if (Edge* candidate = first_fit_after(e->low, y, needed)) return candidate;
        if (e->capacity >= needed) return e;
        return first_fit_after(e->high, y, needed);
    }
public:
    explicit FloatPlacementIndex(float width) : width_(width) {
        const float start = std::numeric_limits<float>::lowest();
        storage_.push_back(Edge{start, 0, 0, false, width, width, next_priority()});
        root_ = &storage_.back();
        edges_.emplace(start, root_);
    }
    FloatPlacement place(float y0, float margin_box_w) const {
        const Edge* at = std::prev(edges_.upper_bound(y0))->second;
        FloatPlacement result{y0, at->left, at->right};
        if (at->capacity >= margin_box_w) return result;
        if (Edge* fit = first_fit_after(root_, y0, margin_box_w)) {
            return {fit->y, fit->left, fit->right};
        }
        // Wider than the containing block: keep y0 and overflow.
        return result;
    }
    void add(const FloatBand& band) {
        if (band.bottom <= band.top) return;
        edge_at(band.top);
        edge_at(band.bottom);
        for (auto it = edges_.lower_bound(band.top); it != edges_.lower_bound(band.bottom); ++it) {
            Edge* e = it->second;
            e->left = e->occupied ? std::max(e->left, band.left) : band.left;
            e->right = e->occupied ? std::max(e->right, band.right) : band.right;
            e->occupied = true;
            e->capacity = width_ - e->left - e->right;
            refresh_up(e);
        }
    }
};

} // namespace

void LayoutBox::apply_style_edges() {
    const ComputedStyle& s = *style;
    dimensions.margin = s.margin;
    dimensions.padding = s.padding;
    dimensions.border = border_override ? *border_override : s.border_width;
    if (border_scale != 1.0f) {
        auto scaled = [&](float value) { return std::ceil(value * border_scale); };
        dimensions.border = EdgeSizes(scaled(dimensions.border.top),
                                      scaled(dimensions.border.right),
                                      scaled(dimensions.border.bottom),
                                      scaled(dimensions.border.left));
    }
}

void LayoutBox::resolve_percentages(float containing_width, float containing_height) {
    if (!style->has_percent_lengths()) {
        return;
    }

    // CSS 2.1: percentage widths, padding and margins resolve against the
    // containing block's *width*; percentage heights need a definite
    // containing height, and fall back to `auto` otherwise (which is what
    // happens for the usual auto-height ancestor).
    const float width_basis = std::max(0.0f, (width_basis_override >= 0.0f)
                                                ? width_basis_override
                                                : containing_width);
    const float height_basis = (containing_height > 0.0f) ? containing_height : -1.0f;

    ComputedStyle updated = *style;
    auto apply = [](float& pixels, float percent, float basis) {
        if (percent < 0.0f) return;
        pixels = (basis >= 0.0f) ? basis * percent / 100.0f : -1.0f;
    };
    auto apply_edges = [&](EdgeSizes& pixels, const EdgeSizes& percent, float basis) {
        apply(pixels.top, percent.top, basis);
        apply(pixels.right, percent.right, basis);
        apply(pixels.bottom, percent.bottom, basis);
        apply(pixels.left, percent.left, basis);
    };

    apply(updated.width, updated.width_percent, width_basis);
    apply(updated.min_width, updated.min_width_percent, width_basis);
    apply(updated.max_width, updated.max_width_percent, width_basis);
    apply(updated.height, updated.height_percent, height_basis);
    apply(updated.min_height, updated.min_height_percent, height_basis);
    apply(updated.max_height, updated.max_height_percent, height_basis);
    apply_edges(updated.margin, updated.margin_percent, width_basis);
    apply_edges(updated.padding, updated.padding_percent, width_basis);

    if (!(updated == *style)) {
        style = std::make_shared<const ComputedStyle>(std::move(updated));
    }
}

void LayoutBox::layout(float containing_width, float containing_height) {
    (void)containing_height;
    if (!needs_layout && !child_needs_layout) {
        return;
    }

    // Every way layout goes one level deeper passes through here (block, flex,
    // grid, table and inline layout all call layout() on their children), so
    // this is the one place that has to ask whether there is stack left for
    // another level. LayoutTree::build_box asks the same question when it
    // builds the tree, but a level of layout does not cost what a level of
    // tree building costs, so a tree that could be built is not thereby a
    // tree that can be laid out. A box that is too deep keeps its zero size.
    if (stack_is_exhausted()) return;

    resolve_percentages(containing_width, containing_height);
    apply_style_edges();

    switch (box_type) {
        case BoxType::FlexNode:
            layout_flex(containing_width);
            break;
        case BoxType::GridNode:
            layout_grid(containing_width);
            break;
        case BoxType::BlockNode:
        case BoxType::AnonymousBlock:
            layout_block(containing_width);
            break;
        case BoxType::InlineNode:
            layout_inline(containing_width);
            break;
        case BoxType::ImageNode:
            layout_image(containing_width);
            break;
        case BoxType::TextNode:
            layout_text(containing_width);
            break;
        case BoxType::TableNode:
            layout_table(containing_width);
            break;
        case BoxType::TableRowNode:
            layout_table_row(containing_width);
            break;
        case BoxType::TableCellNode:
            layout_table_cell(containing_width);
            break;
    }

    clear_layout_flags();
}

bool LayoutBox::establishes_ifc() const {
    if (box_type != BoxType::BlockNode && box_type != BoxType::AnonymousBlock &&
        box_type != BoxType::TableCellNode) {
        return false;
    }
    if (style->is_table() || style->is_table_row()) return false;
    if (children.empty()) return false;
    bool has_inline_content = false;
    for (const auto& child : children) {
        // Out-of-flow children do not take part in inline flow, and floats are
        // handled by the formatting context itself.
        if (child->style->is_out_of_flow() || child->style->is_floated()) continue;
        if (child->box_type == BoxType::BlockNode ||
            child->box_type == BoxType::FlexNode ||
            child->box_type == BoxType::GridNode ||
            child->box_type == BoxType::AnonymousBlock ||
            child->box_type == BoxType::TableNode ||
            child->box_type == BoxType::TableRowNode ||
            child->box_type == BoxType::TableCellNode) {
            return false;
        }
        has_inline_content = true;
    }
    // A box holding nothing but out-of-flow or floated children has no inline
    // content to format; it needs the block path so its floats can be placed.
    return has_inline_content;
}

void LayoutBox::layout_block(float containing_width) {
    if (establishes_ifc()) {
        layout_inline_formatting_context(containing_width);
        return;
    }

    const ComputedStyle& s = *style;
    float h_padding_border = dimensions.border.left + dimensions.border.right +
                            dimensions.padding.left + dimensions.padding.right;
    float v_padding_border = dimensions.border.top + dimensions.border.bottom +
                            dimensions.padding.top + dimensions.padding.bottom;
    float total_horizontal_spacing = dimensions.margin.left + dimensions.margin.right + h_padding_border;

    // 1. Calculate Content Width with BoxSizing & Min/Max constraints
    if (s.width >= 0.0f && !ignore_own_width) {
        if (s.box_sizing == BoxSizing::BorderBox) {
            dimensions.content.width = std::max(0.0f, s.width - h_padding_border);
        } else {
            dimensions.content.width = s.width;
        }
    } else {
        dimensions.content.width = std::max(0.0f, containing_width - total_horizontal_spacing);
    }

    if (s.min_width >= 0.0f) {
        float min_w = (s.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, s.min_width - h_padding_border) : s.min_width;
        dimensions.content.width = std::max(dimensions.content.width, min_w);
    }
    if (s.max_width >= 0.0f) {
        float max_w = (s.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, s.max_width - h_padding_border) : s.max_width;
        dimensions.content.width = std::min(dimensions.content.width, max_w);
    }

    // Margin: auto centering
    float remaining_h = std::max(0.0f, containing_width - (dimensions.content.width + h_padding_border));
    if (s.margin_left_auto && s.margin_right_auto) {
        dimensions.margin.left = remaining_h / 2.0f;
        dimensions.margin.right = remaining_h / 2.0f;
    } else if (s.margin_left_auto) {
        dimensions.margin.left = remaining_h;
    } else if (s.margin_right_auto) {
        dimensions.margin.right = remaining_h;
    }

    // A definite height gives descendants a basis for percentage heights.
    const float definite_height =
        (s.height >= 0.0f)
            ? ((s.box_sizing == BoxSizing::BorderBox)
                   ? std::max(0.0f, s.height - v_padding_border)
                   : s.height)
            : -1.0f;

    // 2. Layout Children, placing floats and propagating the float context
    float y_cursor = 0.0f;
    float_bands.clear();
    std::optional<FloatPlacementIndex> placement_index;

    // `float_bands` holds the bands that can intrude on something. A band
    // of no height covers no position, so no question about a position is
    // ever answered by it. Before indexing, every float looked at every
    // band: with these kept, 200,000 empty floats took 20 seconds. What
    // such a band is still counted for is noted here as the floats are placed:
    // that this box has floats at all, and where the lowest float of each
    // side ends (for `clear`, and for the height of a box that contains its
    // floats).
    bool has_floats = false;
    float lowest_float = -1.0f;         // bottom of the lowest float; -1 without one, as clearance_y says it
    float lowest_left_float = -1.0f;
    float lowest_right_float = -1.0f;
    auto add_band = [&](const FloatBand& band) {
        has_floats = true;
        lowest_float = std::max(lowest_float, band.bottom);
        if (band.left > 0.0f) lowest_left_float = std::max(lowest_left_float, band.bottom);
        if (band.right > 0.0f) lowest_right_float = std::max(lowest_right_float, band.bottom);
        if (band.bottom > band.top) {
            float_bands.push_back(band);
            placement_index->add(band);
        }
    };

    for (auto& child : children) {
        const ComputedStyle& cs = *child->style;
        if (cs.is_out_of_flow()) {
            continue; // Handled in out-of-flow pass
        }

        // --- floated children are taken out of the normal flow -------------
        if (cs.is_floated()) {
            child->layout_float(dimensions.content.width);

            const float margin_box_w = child->dimensions.margin_box().width;
            const float margin_box_h = child->dimensions.margin_box().height;
            const float inset_x = child->dimensions.margin.left + child->dimensions.border.left +
                                  child->dimensions.padding.left;

            // Place at the first fitting band edge (CSS 2.1 9.5.1).
            if (!placement_index) placement_index.emplace(dimensions.content.width);
            const FloatPlacement placement = placement_index->place(y_cursor, margin_box_w);
            const float float_top = placement.top;
            const float left_intrusion = placement.left_intrusion;
            const float right_intrusion = placement.right_intrusion;

            float float_x;
            if (cs.float_type == FloatType::Left) {
                float_x = dimensions.content.x + left_intrusion + inset_x;
                add_band(FloatBand{float_top, float_top + margin_box_h,
                                   left_intrusion + margin_box_w, 0.0f});
            } else {
                float_x = dimensions.content.x + dimensions.content.width - right_intrusion -
                          margin_box_w + inset_x;
                add_band(FloatBand{float_top, float_top + margin_box_h,
                                   0.0f, right_intrusion + margin_box_w});
            }

            const float float_y = dimensions.content.y + float_top +
                                  child->dimensions.margin.top + child->dimensions.border.top +
                                  child->dimensions.padding.top;
            child->shift_position(float_x - child->dimensions.content.x,
                                  float_y - child->dimensions.content.y);
            continue;  // floats never advance the flow cursor
        }

        // --- clearance ------------------------------------------------------
        if (cs.clear_type != ClearType::None) {
            // What clearance_y() computes over the bands, kept as they came.
            const float clear_to = cs.clear_type == ClearType::Both ? lowest_float
                                 : cs.clear_type == ClearType::Left ? lowest_left_float
                                                                    : lowest_right_float;
            if (clear_to > y_cursor) y_cursor = clear_to;
        }

        // --- inline content wraps around this box's floats ------------------
        const std::vector<FloatBand>* child_bands = has_floats ? &float_bands : inherited_bands;
        child->inherited_bands = child_bands;
        child->inherited_bands_offset_y =
            (has_floats ? 0.0f : inherited_bands_offset_y) + y_cursor +
            cs.margin.top + cs.border_width.top + cs.padding.top;

        child->layout(dimensions.content.width, definite_height);

        float new_x = dimensions.content.x +
                      child->dimensions.margin.left +
                      child->dimensions.border.left +
                      child->dimensions.padding.left;

        float new_y = dimensions.content.y + y_cursor +
                      child->dimensions.margin.top +
                      child->dimensions.border.top +
                      child->dimensions.padding.top;

        // Relative positioning offset
        const ComputedStyle& post_cs = *child->style;
        if (post_cs.position == Position::Relative) {
            const auto inset = post_cs.used_insets(dimensions.content.width, std::max(0.0f, definite_height));
            if (post_cs.has_left) new_x += inset.left;
            else if (post_cs.has_right) new_x -= inset.right;
            if (post_cs.has_top) new_y += inset.top;
            else if (post_cs.has_bottom) new_y -= inset.bottom;
        }

        float dx = new_x - child->dimensions.content.x;
        float dy = new_y - child->dimensions.content.y;
        child->shift_position(dx, dy);

        y_cursor += child->dimensions.margin_box().height;
    }

    // 3. Content Height with BoxSizing & Min/Max constraints
    if (s.height >= 0.0f) {
        if (s.box_sizing == BoxSizing::BorderBox) {
            dimensions.content.height = std::max(0.0f, s.height - v_padding_border);
        } else {
            dimensions.content.height = s.height;
        }
    } else {
        dimensions.content.height = y_cursor;
    }

    // A block formatting context contains its floats: grow to the lowest one.
    if (has_floats && establishes_bfc()) {
        dimensions.content.height = std::max(dimensions.content.height, std::max(0.0f, lowest_float));
    }

    if (s.min_height >= 0.0f) {
        float min_h = (s.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, s.min_height - v_padding_border) : s.min_height;
        dimensions.content.height = std::max(dimensions.content.height, min_h);
    }
    if (s.max_height >= 0.0f) {
        float max_h = (s.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, s.max_height - v_padding_border) : s.max_height;
        dimensions.content.height = std::min(dimensions.content.height, max_h);
    }
}

void LayoutBox::layout_flex(float containing_width) {
    const ComputedStyle& s = *style;
    float h_padding_border = dimensions.border.left + dimensions.border.right +
                            dimensions.padding.left + dimensions.padding.right;
    float v_padding_border = dimensions.border.top + dimensions.border.bottom +
                            dimensions.padding.top + dimensions.padding.bottom;
    float total_h_spacing = dimensions.margin.left + dimensions.margin.right + h_padding_border;

    if (s.width >= 0.0f) {
        if (s.box_sizing == BoxSizing::BorderBox) {
            dimensions.content.width = std::max(0.0f, s.width - h_padding_border);
        } else {
            dimensions.content.width = s.width;
        }
    } else {
        dimensions.content.width = std::max(0.0f, containing_width - total_h_spacing);
    }

    if (s.min_width >= 0.0f) {
        float min_w = (s.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, s.min_width - h_padding_border) : s.min_width;
        dimensions.content.width = std::max(dimensions.content.width, min_w);
    }
    if (s.max_width >= 0.0f) {
        float max_w = (s.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, s.max_width - h_padding_border) : s.max_width;
        dimensions.content.width = std::min(dimensions.content.width, max_w);
    }

    // Filter in-flow children
    std::vector<std::shared_ptr<LayoutBox>> in_flow;
    for (const auto& child : children) {
        if (!child->style->is_out_of_flow()) {
            in_flow.push_back(child);
        }
    }

    if (in_flow.empty()) {
        float h = (s.height >= 0.0f) ? s.height : 0.0f;
        if (s.box_sizing == BoxSizing::BorderBox && s.height >= 0.0f) {
            h = std::max(0.0f, h - v_padding_border);
        }
        dimensions.content.height = h;
        return;
    }

    bool is_row = (s.flex_direction == FlexDirection::Row || s.flex_direction == FlexDirection::RowReverse);

    float col_gap = s.grid_column_gap > 0.0f ? s.grid_column_gap : s.gap;
    float row_gap = s.grid_row_gap > 0.0f ? s.grid_row_gap : s.gap;

    if (is_row) {
        struct FlexLine {
            std::vector<std::shared_ptr<LayoutBox>> items;
            float line_items_width{0.0f};
            float total_grow{0.0f};
            float max_height{0.0f};
        };

        std::vector<FlexLine> lines;
        if (s.flex_wrap == FlexWrap::Wrap) {
            FlexLine cur_line;
            for (auto& child : in_flow) {
                const ComputedStyle& cs = *child->style;
                float child_avail_w = (cs.width >= 0.0f)
                                          ? cs.width
                                          : (cs.is_inline_block() ? dimensions.content.width
                                                                  : (dimensions.content.width / in_flow.size()));
                child->layout(child_avail_w);
                float item_w = child->dimensions.margin_box().width;
                float item_h = child->dimensions.margin_box().height;

                float test_w = cur_line.items.empty() ? item_w : (cur_line.line_items_width + (cur_line.items.size() * col_gap) + item_w);

                if (!cur_line.items.empty() && test_w > dimensions.content.width) {
                    lines.push_back(std::move(cur_line));
                    cur_line = FlexLine();
                }

                cur_line.items.push_back(child);
                cur_line.line_items_width += item_w;
                cur_line.total_grow += child->style->flex_grow;
                cur_line.max_height = std::max(cur_line.max_height, item_h);
            }
            if (!cur_line.items.empty()) {
                lines.push_back(std::move(cur_line));
            }
        } else {
            FlexLine single;
            for (auto& child : in_flow) {
                const ComputedStyle& cs = *child->style;
                float child_avail_w = (cs.width >= 0.0f)
                                          ? cs.width
                                          : (cs.is_inline_block() ? dimensions.content.width
                                                                  : (dimensions.content.width / in_flow.size()));
                child->layout(child_avail_w);
                single.items.push_back(child);
                single.line_items_width += child->dimensions.margin_box().width;
                single.total_grow += child->style->flex_grow;
                single.max_height = std::max(single.max_height, child->dimensions.margin_box().height);
            }
            lines.push_back(std::move(single));
        }

        // The cross size the lines are asked to fill. A definite height on the
        // container is one; `align-content` being `stretch` by default means a
        // lone line takes it (CSS Flexbox §9.4, "the cross size of a
        // single-line flex container's line is the flex container's inner
        // cross size"). Without it there is nothing for `align-items` to
        // align against: the line's height is its tallest item, so centring in
        // it moves nothing and stretching to it makes no item any taller.
        float container_cross = -1.0f;
        if (s.height >= 0.0f) {
            container_cross = (s.box_sizing == BoxSizing::BorderBox)
                                  ? std::max(0.0f, s.height - v_padding_border)
                                  : s.height;
        }

        float y_line_cursor = 0.0f;
        for (auto& line : lines) {
            float total_gaps = (line.items.size() > 1) ? (line.items.size() - 1) * s.gap : 0.0f;
            float free_space = dimensions.content.width - (line.line_items_width + total_gaps);

            if (free_space > 0.0f && line.total_grow > 0.0f) {
                for (auto& child : line.items) {
                    if (child->style->flex_grow > 0.0f) {
                        float extra = (child->style->flex_grow / line.total_grow) * free_space;
                        child->dimensions.content.width += extra;
                        child->layout(child->dimensions.content.width);
                    }
                }
            }

            float total_used_w = 0.0f;
            float cur_line_max_h = 0.0f;
            for (const auto& child : line.items) {
                total_used_w += child->dimensions.margin_box().width;
                cur_line_max_h = std::max(cur_line_max_h, child->dimensions.margin_box().height);
            }
            float remaining = dimensions.content.width - (total_used_w + total_gaps);

            // One line in a container of definite height fills the container;
            // several lines are only as tall as their tallest item until
            // `align-content` is implemented and shares the height out.
            float line_cross = cur_line_max_h;
            if (lines.size() == 1 && container_cross >= 0.0f) {
                line_cross = std::max(line_cross, container_cross);
            }

            float x_cursor = 0.0f;
            float gap_spacing = col_gap;

            switch (s.justify_content) {
                case JustifyContent::FlexStart:
                    x_cursor = 0.0f;
                    break;
                case JustifyContent::FlexEnd:
                    x_cursor = std::max(0.0f, remaining);
                    break;
                case JustifyContent::Center:
                    x_cursor = std::max(0.0f, remaining / 2.0f);
                    break;
                case JustifyContent::SpaceBetween:
                    if (line.items.size() > 1) {
                        gap_spacing += std::max(0.0f, remaining / (line.items.size() - 1));
                    }
                    break;
                case JustifyContent::SpaceAround:
                    if (!line.items.empty()) {
                        float space = std::max(0.0f, remaining / line.items.size());
                        x_cursor = space / 2.0f;
                        gap_spacing += space;
                    }
                    break;
                case JustifyContent::SpaceEvenly:
                    if (!line.items.empty()) {
                        float space = std::max(0.0f, remaining / (line.items.size() + 1));
                        x_cursor = space;
                        gap_spacing += space;
                    }
                    break;
            }

            for (auto& child : line.items) {
                const ComputedStyle& cs = *child->style;
                float y_offset = 0.0f;
                float child_h = child->dimensions.margin_box().height;

                switch (s.align_items) {
                    case AlignItems::Stretch:
                        if (cs.height < 0.0f && line_cross > 0.0f) {
                            float child_v_extra = child->dimensions.margin.top + child->dimensions.margin.bottom +
                                                  child->dimensions.border.top + child->dimensions.border.bottom +
                                                  child->dimensions.padding.top + child->dimensions.padding.bottom;
                            child->dimensions.content.height = std::max(0.0f, line_cross - child_v_extra);
                        }
                        break;
                    case AlignItems::FlexEnd:
                        y_offset = line_cross - child_h;
                        break;
                    case AlignItems::Center:
                        y_offset = (line_cross - child_h) / 2.0f;
                        break;
                    case AlignItems::FlexStart:
                    default:
                        y_offset = 0.0f;
                        break;
                }

                float new_x = dimensions.content.x + x_cursor +
                              child->dimensions.margin.left + child->dimensions.border.left + child->dimensions.padding.left;
                float new_y = dimensions.content.y + y_line_cursor + y_offset +
                              child->dimensions.margin.top + child->dimensions.border.top + child->dimensions.padding.top;

                if (cs.position == Position::Relative) {
                    const auto inset = cs.used_insets(dimensions.content.width, std::max(0.0f, s.height));
                    if (cs.has_left) new_x += inset.left;
                    else if (cs.has_right) new_x -= inset.right;
                    if (cs.has_top) new_y += inset.top;
                    else if (cs.has_bottom) new_y -= inset.bottom;
                }

                float dx = new_x - child->dimensions.content.x;
                float dy = new_y - child->dimensions.content.y;
                child->shift_position(dx, dy);

                x_cursor += child->dimensions.margin_box().width + gap_spacing;
            }

            y_line_cursor += line_cross + row_gap;
        }

        float total_h = y_line_cursor > 0.0f ? (y_line_cursor - row_gap) : 0.0f;
        float final_h = (s.height >= 0.0f) ? s.height : total_h;
        if (s.box_sizing == BoxSizing::BorderBox && s.height >= 0.0f) {
            final_h = std::max(0.0f, final_h - v_padding_border);
        }
        dimensions.content.height = final_h;
    } else {
        // Column layout: main axis is vertical (Y), cross axis is horizontal (X)
        float y_cursor = 0.0f;
        float max_child_w = 0.0f;
        float cross_available = dimensions.content.width;

        for (auto& child : in_flow) {
            child->layout(dimensions.content.width);
            max_child_w = std::max(max_child_w, child->dimensions.margin_box().width);

            // `align-items` on a column container places the item across the
            // width. Stretch — the default — fills an auto width: a block item
            // has already done that in layout_block, an inline-block item has
            // not, and an item that states a width keeps it, because stretch
            // only answers a cross size that is auto.
            const ComputedStyle& cross_cs = *child->style;
            if (s.align_items == AlignItems::Stretch && cross_cs.width < 0.0f) {
                float child_h_extra = child->dimensions.margin.left + child->dimensions.margin.right +
                                      child->dimensions.border.left + child->dimensions.border.right +
                                      child->dimensions.padding.left + child->dimensions.padding.right;
                child->dimensions.content.width = std::max(0.0f, cross_available - child_h_extra);
            }

            float x_offset = 0.0f;
            switch (s.align_items) {
                case AlignItems::Center:
                    x_offset = (cross_available - child->dimensions.margin_box().width) / 2.0f;
                    break;
                case AlignItems::FlexEnd:
                    x_offset = cross_available - child->dimensions.margin_box().width;
                    break;
                case AlignItems::Stretch:
                case AlignItems::FlexStart:
                default:
                    x_offset = 0.0f;
                    break;
            }

            float new_x = dimensions.content.x + x_offset +
                          child->dimensions.margin.left + child->dimensions.border.left + child->dimensions.padding.left;
            float new_y = dimensions.content.y + y_cursor +
                          child->dimensions.margin.top + child->dimensions.border.top + child->dimensions.padding.top;

            const ComputedStyle& cs = *child->style;
            if (cs.position == Position::Relative) {
                const auto inset = cs.used_insets(dimensions.content.width, std::max(0.0f, s.height));
                if (cs.has_left) new_x += inset.left;
                else if (cs.has_right) new_x -= inset.right;
                if (cs.has_top) new_y += inset.top;
                else if (cs.has_bottom) new_y -= inset.bottom;
            }

            float dx = new_x - child->dimensions.content.x;
            float dy = new_y - child->dimensions.content.y;
            child->shift_position(dx, dy);

            y_cursor += child->dimensions.margin_box().height + row_gap;
        }

        float total_h = y_cursor > 0.0f ? (y_cursor - row_gap) : 0.0f;
        float final_h = (s.height >= 0.0f) ? s.height : total_h;
        if (s.box_sizing == BoxSizing::BorderBox && s.height >= 0.0f) {
            final_h = std::max(0.0f, final_h - v_padding_border);
        }
        dimensions.content.height = final_h;
    }
}

void LayoutBox::layout_grid(float containing_width) {
    const ComputedStyle& s = *style;
    float h_padding_border = dimensions.border.left + dimensions.border.right +
                            dimensions.padding.left + dimensions.padding.right;
    float v_padding_border = dimensions.border.top + dimensions.border.bottom +
                            dimensions.padding.top + dimensions.padding.bottom;
    float total_h_spacing = dimensions.margin.left + dimensions.margin.right + h_padding_border;

    if (s.width >= 0.0f) {
        if (s.box_sizing == BoxSizing::BorderBox) {
            dimensions.content.width = std::max(0.0f, s.width - h_padding_border);
        } else {
            dimensions.content.width = s.width;
        }
    } else {
        dimensions.content.width = std::max(0.0f, containing_width - total_h_spacing);
    }

    if (s.min_width >= 0.0f) {
        float min_w = (s.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, s.min_width - h_padding_border) : s.min_width;
        dimensions.content.width = std::max(dimensions.content.width, min_w);
    }
    if (s.max_width >= 0.0f) {
        float max_w = (s.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, s.max_width - h_padding_border) : s.max_width;
        dimensions.content.width = std::min(dimensions.content.width, max_w);
    }

    // Filter in-flow children
    std::vector<std::shared_ptr<LayoutBox>> in_flow;
    for (const auto& child : children) {
        if (!child->style->is_out_of_flow()) {
            in_flow.push_back(child);
        }
    }

    if (in_flow.empty()) {
        float h = (s.height >= 0.0f) ? s.height : 0.0f;
        if (s.box_sizing == BoxSizing::BorderBox && s.height >= 0.0f) {
            h = std::max(0.0f, h - v_padding_border);
        }
        dimensions.content.height = h;
        return;
    }

    // Compute column track widths
    float col_gap = s.grid_column_gap > 0.0f ? s.grid_column_gap : s.gap;
    float row_gap = s.grid_row_gap > 0.0f ? s.grid_row_gap : s.gap;

    std::vector<ComputedStyle::GridTrackDef> tracks = s.grid_col_track_defs;
    if (s.grid_auto_fit_columns) {
        float min_track = s.grid_auto_fit_def.min_px > 0.0f ? s.grid_auto_fit_def.min_px : 100.0f;
        int num_fit = std::max(1, static_cast<int>(std::floor((dimensions.content.width + col_gap) / (min_track + col_gap))));
        tracks.clear();
        for (int k = 0; k < num_fit; ++k) {
            tracks.push_back(s.grid_auto_fit_def);
        }
    } else if (tracks.empty()) {
        if (!s.grid_template_columns.empty()) {
            for (float t : s.grid_template_columns) {
                ComputedStyle::GridTrackDef d;
                if (t < 0.0f) d.max_fr = -t;
                else { d.min_px = t; d.max_px = t; }
                tracks.push_back(d);
            }
        } else {
            ComputedStyle::GridTrackDef d;
            d.max_fr = 1.0f;
            tracks.push_back(d);
        }
    }

    int max_area_col = 0;
    int max_area_row = 0;
    for (const auto& [name, rect] : s.grid_named_areas) {
        max_area_col = std::max(max_area_col, rect.col_end - 1);
        max_area_row = std::max(max_area_row, rect.row_end - 1);
    }
    while (tracks.size() < static_cast<size_t>(max_area_col)) {
        ComputedStyle::GridTrackDef d;
        d.max_fr = 1.0f;
        tracks.push_back(d);
    }

    size_t num_cols = tracks.size();
    float total_gaps = (num_cols > 1) ? (num_cols - 1) * col_gap : 0.0f;
    float avail_for_tracks = std::max(0.0f, dimensions.content.width - total_gaps);

    float sum_min = 0.0f;
    float total_fr = 0.0f;
    for (const auto& tr : tracks) {
        sum_min += tr.min_px;
        total_fr += tr.max_fr;
    }

    float fr_space = std::max(0.0f, avail_for_tracks - sum_min);
    std::vector<float> col_widths;
    for (const auto& tr : tracks) {
        float w = tr.min_px;
        if (total_fr > 0.0f && tr.max_fr > 0.0f) {
            w += (tr.max_fr / total_fr) * fr_space;
        } else if (tr.max_px > tr.min_px) {
            w = std::min(tr.max_px, tr.min_px + fr_space);
        }
        col_widths.push_back(w);
    }

    std::vector<float> col_x(num_cols, 0.0f);
    float cur_x = 0.0f;
    for (size_t c = 0; c < num_cols; ++c) {
        col_x[c] = cur_x;
        cur_x += col_widths[c] + col_gap;
    }

    struct ItemPlacement {
        size_t c_start{0};
        size_t c_end{1};
        size_t r_start{0};
        size_t r_end{1};
    };
    std::vector<ItemPlacement> placements(in_flow.size());
    size_t auto_c = 0;
    size_t auto_r = 0;
    size_t max_placed_row = (max_area_row > 0) ? static_cast<size_t>(max_area_row) : ((in_flow.size() + num_cols - 1) / num_cols);

    for (size_t i = 0; i < in_flow.size(); ++i) {
        auto& child = in_flow[i];
        const ComputedStyle& cs = *child->style;
        bool placed = false;
        if (!cs.grid_area_name.empty()) {
            auto it = s.grid_named_areas.find(cs.grid_area_name);
            if (it != s.grid_named_areas.end()) {
                placements[i].c_start = static_cast<size_t>(std::max(0, it->second.col_start - 1));
                placements[i].c_end = std::max(placements[i].c_start + 1, static_cast<size_t>(it->second.col_end - 1));
                placements[i].r_start = static_cast<size_t>(std::max(0, it->second.row_start - 1));
                placements[i].r_end = std::max(placements[i].r_start + 1, static_cast<size_t>(it->second.row_end - 1));
                placed = true;
            }
        }
        if (!placed && (cs.grid_column_start > 0 || cs.grid_row_start > 0)) {
            size_t cs_idx = cs.grid_column_start > 0 ? static_cast<size_t>(cs.grid_column_start - 1) : 0;
            size_t ce_idx = cs.grid_column_end > cs.grid_column_start ? static_cast<size_t>(cs.grid_column_end - 1) : cs_idx + 1;
            size_t rs_idx = cs.grid_row_start > 0 ? static_cast<size_t>(cs.grid_row_start - 1) : 0;
            size_t re_idx = cs.grid_row_end > cs.grid_row_start ? static_cast<size_t>(cs.grid_row_end - 1) : rs_idx + 1;
            placements[i] = {cs_idx, ce_idx, rs_idx, re_idx};
            placed = true;
        }
        if (!placed) {
            placements[i] = {auto_c, auto_c + 1, auto_r, auto_r + 1};
            auto_c++;
            if (auto_c >= num_cols) {
                auto_c = 0;
                auto_r++;
            }
        }
        max_placed_row = std::max(max_placed_row, placements[i].r_end);
    }

    size_t num_rows = std::max(size_t(1), max_placed_row);
    std::vector<float> row_heights(num_rows, 0.0f);

    for (size_t i = 0; i < in_flow.size(); ++i) {
        size_t cs = std::min(placements[i].c_start, num_cols - 1);
        size_t ce = std::min(placements[i].c_end, num_cols);
        float item_w = 0.0f;
        for (size_t c = cs; c < ce; ++c) {
            item_w += col_widths[c];
            if (c + 1 < ce) item_w += col_gap;
        }

        in_flow[i]->layout(item_w);
        float item_h = in_flow[i]->dimensions.margin_box().height;
        size_t rs = std::min(placements[i].r_start, num_rows - 1);
        size_t span = std::max(size_t(1), placements[i].r_end - rs);
        float per_row_h = item_h / span;
        for (size_t r = rs; r < std::min(placements[i].r_end, num_rows); ++r) {
            row_heights[r] = std::max(row_heights[r], per_row_h);
        }
    }

    for (size_t r = 0; r < num_rows && r < s.grid_template_rows.size(); ++r) {
        float explicit_h = s.grid_template_rows[r];
        if (explicit_h >= 0.0f) {
            row_heights[r] = std::max(row_heights[r], explicit_h);
        }
    }

    std::vector<float> row_y(num_rows, 0.0f);
    float cur_y = 0.0f;
    for (size_t r = 0; r < num_rows; ++r) {
        row_y[r] = cur_y;
        cur_y += row_heights[r] + row_gap;
    }

    for (size_t i = 0; i < in_flow.size(); ++i) {
        size_t cs = std::min(placements[i].c_start, num_cols - 1);
        size_t rs = std::min(placements[i].r_start, num_rows - 1);

        auto& child = in_flow[i];
        const ComputedStyle& item_cs = *child->style;

        // `align-items` in a grid container is the block axis, and stretch —
        // the default — is what makes a cell painted: the item's area is its
        // rows' heights, and an item with no height of its own fills it. The
        // rows are already settled by now (an item's height is what asked for
        // the row to be that tall), so the stretch happens after the tracks.
        size_t re = std::min(placements[i].r_end, num_rows);
        float area_h = 0.0f;
        for (size_t r = rs; r < re; ++r) {
            area_h += row_heights[r];
            if (r + 1 < re) area_h += row_gap;
        }

        float y_offset = 0.0f;
        switch (s.align_items) {
            case AlignItems::Stretch:
                if (item_cs.height < 0.0f) {
                    float item_v_extra = child->dimensions.margin.top + child->dimensions.margin.bottom +
                                         child->dimensions.border.top + child->dimensions.border.bottom +
                                         child->dimensions.padding.top + child->dimensions.padding.bottom;
                    child->dimensions.content.height = std::max(0.0f, area_h - item_v_extra);
                }
                break;
            case AlignItems::Center:
                y_offset = (area_h - child->dimensions.margin_box().height) / 2.0f;
                break;
            case AlignItems::FlexEnd:
                y_offset = area_h - child->dimensions.margin_box().height;
                break;
            case AlignItems::FlexStart:
            default:
                y_offset = 0.0f;
                break;
        }

        float new_x = dimensions.content.x + col_x[cs] +
                      child->dimensions.margin.left + child->dimensions.border.left + child->dimensions.padding.left;
        float new_y = dimensions.content.y + row_y[rs] + y_offset +
                      child->dimensions.margin.top + child->dimensions.border.top + child->dimensions.padding.top;

        const ComputedStyle& post_cs = *child->style;
        if (post_cs.position == Position::Relative) {
            const size_t ce = std::min(placements[i].c_end, num_cols);
            const float area_w = ce > cs ? col_x[ce-1] + col_widths[ce-1] - col_x[cs] : 0.0f;
            const auto inset = post_cs.used_insets(area_w, area_h);
            if (post_cs.has_left) new_x += inset.left;
            else if (post_cs.has_right) new_x -= inset.right;
            if (post_cs.has_top) new_y += inset.top;
            else if (post_cs.has_bottom) new_y -= inset.bottom;
        }

        float dx = new_x - child->dimensions.content.x;
        float dy = new_y - child->dimensions.content.y;
        child->shift_position(dx, dy);
    }

    float total_grid_h = cur_y > 0.0f ? (cur_y - row_gap) : 0.0f;
    float final_h = (s.height >= 0.0f) ? s.height : total_grid_h;
    if (s.box_sizing == BoxSizing::BorderBox && s.height >= 0.0f) {
        final_h = std::max(0.0f, final_h - v_padding_border);
    }
    dimensions.content.height = final_h;
}

void LayoutBox::layout_inline(float containing_width) {
    float x_cursor = 0.0f;
    float max_child_height = 0.0f;

    for (auto& child : children) {
        child->layout(containing_width);
        child->dimensions.content.x = dimensions.content.x + x_cursor + child->dimensions.margin.left;
        child->dimensions.content.y = dimensions.content.y + child->dimensions.margin.top;

        x_cursor += child->dimensions.margin_box().width;
        max_child_height = std::max(max_child_height, child->dimensions.margin_box().height);
    }

    dimensions.content.width = x_cursor;
    dimensions.content.height = max_child_height;
}

void LayoutBox::layout_text(float containing_width) {
    lines.clear();
    const ComputedStyle& s = *style;
    FontRenderer& font_renderer = FontRenderer::instance();
    const bool italic = s.font_style != FontStyle::Normal;
    // `font-kerning: none` has to reach the pen, or the property is a word in a
    // stylesheet and nothing else: the box would be measured with the pairs and
    // its text drawn without them, which is the drift findings 84 and 97 are
    // both about. Measured and painted with the same flag, from the same style.
    const bool kerning = s.kerning_enabled();
    const float space_advance = font_renderer.measure_text(" ", s.font_size, s.font_weight, s.font_family, italic, kerning);
    float line_h = s.font_size * 1.25f;

    if (containing_width <= 0.0f) containing_width = 800.0f;

    // Word wrap
    std::stringstream ss(text);
    std::string word;
    std::string current_line;
    float current_w = 0.0f;
    float max_w = 0.0f;

    while (ss >> word) {
        const float word_w = font_renderer.measure_text(word, s.font_size, s.font_weight, s.font_family, italic, kerning);
        const float space_w = current_line.empty() ? 0.0f : space_advance;

        if (!current_line.empty() && (current_w + space_w + word_w) > containing_width) {
            lines.push_back(current_line);
            max_w = std::max(max_w, current_w);
            current_line = word;
            current_w = word_w;
        } else {
            if (!current_line.empty()) {
                current_line += " ";
                current_w += space_w;
            }
            current_line += word;
            current_w += word_w;
        }
    }

    if (!current_line.empty()) {
        lines.push_back(current_line);
        max_w = std::max(max_w, current_w);
    }

    if (lines.empty()) {
        lines.push_back(text);
        max_w = font_renderer.measure_text(text, s.font_size, s.font_weight, s.font_family, italic, kerning);
    }

    dimensions.content.width = max_w;
    dimensions.content.height = lines.size() * line_h;
}

struct InlineItem {
    enum class Type { Text, Space, LineBreak };
    Type type{Type::Text};
    std::string text;
    float width{0.0f};
    float height{0.0f};
    SharedComputedStyle style;
    std::shared_ptr<LayoutBox> source_box;
};

using InlineOverrideCache = std::unordered_map<
    const ComputedStyle*,
    std::unordered_map<const ComputedStyle*, SharedComputedStyle>>;

static void collect_items(
    const std::shared_ptr<LayoutBox>& box,
    std::vector<InlineItem>& items,
    InlineOverrideCache& override_cache
) {
    if (!box) return;
    if (stack_is_exhausted()) return;   // inline boxes nested too deeply to follow

    if (box->box_type == BoxType::TextNode) {
        const SharedComputedStyle& effective_style = box->style;
        const ComputedStyle& es = *effective_style;
        FontRenderer& font_renderer = FontRenderer::instance();
        const bool kerning = es.kerning_enabled();
        const float space_w = font_renderer.measure_text(" ", es.font_size, es.font_weight,
                                                         es.font_family,
                                                         es.font_style != FontStyle::Normal,
                                                         kerning);
        float line_h = es.effective_line_height();

        size_t i = 0;
        const std::string& str = box->text;
        while (i < str.length()) {
            if (std::isspace(static_cast<unsigned char>(str[i]))) {
                while (i < str.length() && std::isspace(static_cast<unsigned char>(str[i]))) {
                    ++i;
                }
                if (!items.empty() && items.back().type == InlineItem::Type::Space) {
                    continue;
                }
                InlineItem item;
                item.type = InlineItem::Type::Space;
                item.text = " ";
                item.width = space_w;
                item.height = line_h;
                item.style = effective_style;
                item.source_box = box;
                items.push_back(std::move(item));
            } else {
                size_t start = i;
                while (i < str.length() && !std::isspace(static_cast<unsigned char>(str[i]))) {
                    ++i;
                }
                std::string word = str.substr(start, i - start);
                InlineItem item;
                item.type = InlineItem::Type::Text;
                item.width = font_renderer.measure_text(word, es.font_size, es.font_weight,
                                                        es.font_family,
                                                        es.font_style != FontStyle::Normal,
                                                        kerning);
                item.text = std::move(word);
                item.height = line_h;
                item.style = effective_style;
                item.source_box = box;
                items.push_back(std::move(item));
            }
        }
    } else if (box->box_type == BoxType::InlineNode) {
        const ComputedStyle& bs = *box->style;
        if (box->node && box->node->is_element() && !box->node->as_element()->is_foreign() &&
            box->node->as_element()->tag_name() == "br") {
            InlineItem br;
            br.type = InlineItem::Type::LineBreak;
            br.height = bs.font_size * 1.25f;
            br.style = box->style;
            br.source_box = box;
            items.push_back(std::move(br));
            return;
        }
        size_t initial_count = items.size();
        for (const auto& ch : box->children) {
            collect_items(ch, items, override_cache);
        }

        if (items.size() > initial_count) {
            float extra_left = bs.padding.left + bs.border_width.left;
            float extra_right = bs.padding.right + bs.border_width.right;

            if (extra_left > 0.0f) {
                items[initial_count].width += extra_left;
            }
            if (extra_right > 0.0f) {
                items.back().width += extra_right;
            }

            const bool has_bg = !bs.background_color.is_transparent();
            const bool has_rad = bs.border_radius > 0.0f;
            const bool has_dec = bs.has_text_decoration();
            const bool has_bdr = bs.border_width.left > 0.0f || bs.border_width.top > 0.0f ||
                                 bs.border_width.right > 0.0f || bs.border_width.bottom > 0.0f;

            if (has_bg || has_rad || has_dec || has_bdr) {
                const ComputedStyle* inline_key = box->style.get();
                auto& inner_cache = override_cache[inline_key];
                for (size_t idx = initial_count; idx < items.size(); ++idx) {
                    const ComputedStyle* item_key = items[idx].style.get();
                    const ComputedStyle& cur = *item_key;
                    const bool already_matches =
                        (!has_bg || cur.background_color == bs.background_color) &&
                        (!has_rad || cur.border_radius == bs.border_radius) &&
                        (!has_dec || cur.text_decoration == bs.text_decoration) &&
                        (!has_bdr || (cur.border_width == bs.border_width && cur.border_color == bs.border_color));
                    if (already_matches) {
                        continue;
                    }
                    auto it = inner_cache.find(item_key);
                    if (it != inner_cache.end()) {
                        items[idx].style = it->second;
                    } else {
                        ComputedStyle merged = cur;
                        if (has_bg) merged.background_color = bs.background_color;
                        if (has_rad) merged.border_radius = bs.border_radius;
                        if (has_dec) merged.text_decoration = bs.text_decoration;
                        if (has_bdr) {
                            merged.border_width = bs.border_width;
                            merged.border_color = bs.border_color;
                        }
                        auto shared = std::make_shared<const ComputedStyle>(std::move(merged));
                        inner_cache.emplace(item_key, shared);
                        items[idx].style = std::move(shared);
                    }
                }
            }
        }
    } else if (box->box_type == BoxType::ImageNode) {
        box->layout(0.0f);
        InlineItem item;
        item.type = InlineItem::Type::Text;
        item.width = box->dimensions.content.width;
        item.height = box->dimensions.content.height;
        item.style = box->style;
        item.source_box = box;
        items.push_back(std::move(item));
    }
}

void LayoutBox::layout_inline_formatting_context(float containing_width) {
    line_boxes.clear();
    const ComputedStyle& s = *style;

    // 1. Calculate Content Width
    float h_padding_border = dimensions.border.left + dimensions.border.right +
                            dimensions.padding.left + dimensions.padding.right;
    float total_horizontal_spacing = dimensions.margin.left + dimensions.margin.right + h_padding_border;

    if (s.width >= 0.0f && !ignore_own_width) {
        dimensions.content.width = (s.box_sizing == BoxSizing::BorderBox)
                                       ? std::max(0.0f, s.width - h_padding_border)
                                       : s.width;
    } else {
        dimensions.content.width = std::max(0.0f, containing_width - total_horizontal_spacing);
    }
    if (s.min_width >= 0.0f) {
        float min_w = (s.box_sizing == BoxSizing::BorderBox)
                          ? std::max(0.0f, s.min_width - h_padding_border)
                          : s.min_width;
        dimensions.content.width = std::max(dimensions.content.width, min_w);
    }
    if (s.max_width >= 0.0f) {
        float max_w = (s.box_sizing == BoxSizing::BorderBox)
                          ? std::max(0.0f, s.max_width - h_padding_border)
                          : s.max_width;
        dimensions.content.width = std::min(dimensions.content.width, max_w);
    }

    float available_width = dimensions.content.width;
    if (available_width <= 0.0f) available_width = 800.0f;

    // 2. Collect Inline Items
    std::vector<InlineItem> items;
    InlineOverrideCache override_cache;
    for (const auto& ch : children) {
        collect_items(ch, items, override_cache);
    }

    if (items.empty()) {
        if (s.is_inline_block() && s.width < 0.0f) {
            dimensions.content.width = 0.0f;
        }
        dimensions.content.height = (s.height >= 0.0f) ? s.height : 0.0f;
        return;
    }

    // 2b. Floats that share this formatting context are placed first, so the
    //     line boxes can wrap around them.
    //
    //     They all start at the top, so how far the floats before this one
    //     reach in from each side is a running maximum; asking the bands
    //     each time made n floats in a paragraph cost n * n. A float of no
    //     height covers no line and leaves no band (see layout_block).
    float_bands.clear();
    bool has_floats = false;
    float left_reach = 0.0f;
    float right_reach = 0.0f;
    for (auto& child : children) {
        if (!child->style->is_floated()) continue;

        child->layout_float(dimensions.content.width);

        const float margin_box_w = child->dimensions.margin_box().width;
        const float margin_box_h = child->dimensions.margin_box().height;
        const float inset_x = child->dimensions.margin.left + child->dimensions.border.left +
                              child->dimensions.padding.left;

        const float left_intrusion = left_reach;
        const float right_intrusion = right_reach;

        float float_x;
        FloatBand band;
        if (child->style->float_type == FloatType::Left) {
            float_x = dimensions.content.x + left_intrusion + inset_x;
            band = FloatBand{0.0f, margin_box_h, left_intrusion + margin_box_w, 0.0f};
        } else {
            float_x = dimensions.content.x + dimensions.content.width - right_intrusion -
                      margin_box_w + inset_x;
            band = FloatBand{0.0f, margin_box_h, 0.0f, right_intrusion + margin_box_w};
        }
        has_floats = true;
        if (band.bottom > band.top) {
            // What band_at(float_bands, 0, ...) would add to its answer.
            left_reach = std::max(left_reach, band.left);
            right_reach = std::max(right_reach, band.right);
            float_bands.push_back(band);
        }

        child->shift_position(float_x - child->dimensions.content.x,
                              dimensions.content.y - child->dimensions.content.y +
                                  child->dimensions.margin.top + child->dimensions.border.top +
                                  child->dimensions.padding.top);
    }

    // 3. Line Breaking, wrapping around any floats in scope
    const std::vector<FloatBand>* bands = has_floats ? &float_bands : inherited_bands;
    const float bands_offset = has_floats ? 0.0f : inherited_bands_offset_y;
    auto measure_band = [&](float y, float& left, float& right) -> float {
        if (!bands) {
            left = 0.0f;
            right = 0.0f;
            return available_width;
        }
        LayoutBox::band_at(*bands, bands_offset + y, left, right);
        return std::max(1.0f, available_width - left - right);
    };

    LineBox current_line;
    float current_x = 0.0f;
    float line_top = 0.0f;              // running y used for band queries
    float line_left = 0.0f;
    float line_right = 0.0f;
    float line_capacity = measure_band(line_top, line_left, line_right);

    const float estimated_line_height = s.effective_line_height();

    auto finish_line = [&](LineBox& line, float width) {
        while (!line.fragments.empty() && line.fragments.back().is_space) {
            width -= line.fragments.back().rect.width;
            line.fragments.pop_back();
        }
        if (!line.fragments.empty()) {
            line.rect.width = width;
            float max_h = s.effective_line_height();
            for (const auto& f : line.fragments) {
                // CSS 2.1, 10.8.1: the line box's height is at least the
                // largest of the inline boxes on it, strut included.
                max_h = std::max(max_h, f.style->effective_line_height());
                max_h = std::max(max_h, f.rect.height);
            }
            line.rect.height = max_h;
            line_boxes.push_back(std::move(line));
        }
        line = LineBox();
    };

    for (auto& item : items) {
        if (item.type == InlineItem::Type::LineBreak) {
            if (!current_line.fragments.empty()) {
                finish_line(current_line, current_x);
            } else {
                // Empty line created by consecutive `<br><br>`
                LineBox empty_line;
                empty_line.rect.width = 0.0f;
                empty_line.rect.height = std::max(estimated_line_height, item.height);
                empty_line.left_intrusion = line_left;
                empty_line.right_intrusion = line_right;
                line_boxes.push_back(std::move(empty_line));
            }
            current_x = 0.0f;
            line_top += estimated_line_height;
            line_capacity = measure_band(line_top, line_left, line_right);
            continue;
        }

        if (current_line.fragments.empty() && item.type == InlineItem::Type::Space) {
            // Drop leading space at line start
            continue;
        }

        if (!current_line.fragments.empty() && (current_x + item.width > line_capacity)) {
            // Line break: the next line may have a different float shape.
            finish_line(current_line, current_x);
            current_x = 0.0f;
            line_top += estimated_line_height;
            line_capacity = measure_band(line_top, line_left, line_right);

            if (item.type == InlineItem::Type::Space) {
                continue;
            }
        }

        InlineFragment frag;
        frag.rect.x = current_x;
        frag.rect.y = 0.0f;
        frag.rect.width = item.width;
        frag.rect.height = item.height;
        frag.text = std::move(item.text);
        frag.style = std::move(item.style);
        frag.box = std::move(item.source_box);
        frag.is_text = (item.type == InlineItem::Type::Text);
        frag.is_space = (item.type == InlineItem::Type::Space);

        current_line.fragments.push_back(std::move(frag));
        current_line.left_intrusion = line_left;
        current_line.right_intrusion = line_right;
        current_x += item.width;
    }

    if (!current_line.fragments.empty()) {
        finish_line(current_line, current_x);
    }

    if (s.is_inline_block() && s.width < 0.0f) {
        float max_line_w = 0.0f;
        for (const auto& line : line_boxes) {
            max_line_w = std::max(max_line_w, line.rect.width);
        }
        dimensions.content.width = max_line_w;
        if (s.min_width >= 0.0f) {
            float min_w = (s.box_sizing == BoxSizing::BorderBox)
                              ? std::max(0.0f, s.min_width - h_padding_border)
                              : s.min_width;
            dimensions.content.width = std::max(dimensions.content.width, min_w);
        }
        if (s.max_width >= 0.0f) {
            float max_w = (s.box_sizing == BoxSizing::BorderBox)
                              ? std::max(0.0f, s.max_width - h_padding_border)
                              : s.max_width;
            dimensions.content.width = std::min(dimensions.content.width, max_w);
        }
        available_width = dimensions.content.width;
    }

    // 4. Vertical Placement, text-align and float offsets
    float y_cursor = 0.0f;
    for (auto& line : line_boxes) {
        float left = 0.0f;
        float right = 0.0f;
        const float capacity = measure_band(y_cursor, left, right);

        float free_space = std::max(0.0f, capacity - line.rect.width);
        float x_offset = left;

        if (s.text_align == TextAlign::Center) {
            x_offset = left + free_space / 2.0f;
        } else if (s.text_align == TextAlign::Right || s.text_align == TextAlign::End) {
            x_offset = left + free_space;
        }

        line.rect.x = dimensions.content.x + x_offset;
        line.rect.y = dimensions.content.y + y_cursor;
        line.left_intrusion = left;
        line.right_intrusion = right;

        for (auto& frag : line.fragments) {
            frag.rect.x = dimensions.content.x + x_offset + frag.rect.x;
            frag.rect.y = dimensions.content.y + y_cursor;
            // Vertically center inside line if smaller than line height
            if (frag.rect.height < line.rect.height) {
                frag.rect.y += (line.rect.height - frag.rect.height) * 0.5f;
            }
        }
        apply_bidi_to_line(line);

        y_cursor += line.rect.height;
    }

    dimensions.content.height = (s.height >= 0.0f) ? s.height : y_cursor;

    // 5. Populate child TextNodes for backward-compatibility & inspection
    //
    // One pass over the fragments notes, for each box, what it has on each
    // line; the children then pick up their own. Each child used to search
    // every fragment of every line for itself, so a paragraph of n images,
    // or of n short runs of text between <br> tags, cost n * n: 50,000
    // images took 6.7 seconds to lay out, 200,000 two minutes.
    struct OnLines {
        std::vector<std::string> text;          // the box's text on each line it has text on
        Rect image;                             // its first fragment on the last line it is on
        size_t last_text_line{static_cast<size_t>(-1)};
        size_t last_line{static_cast<size_t>(-1)};
        bool placed{false};
    };
    std::unordered_map<const LayoutBox*, OnLines> on_lines;
    for (size_t line_index = 0; line_index < line_boxes.size(); ++line_index) {
        for (const auto& frag : line_boxes[line_index].fragments) {
            if (!frag.box) continue;
            if (frag.box->box_type == BoxType::TextNode) {
                if (frag.text.empty()) continue;
                OnLines& on = on_lines[frag.box.get()];
                if (on.last_text_line != line_index) {
                    on.text.emplace_back();
                    on.last_text_line = line_index;
                }
                on.text.back() += frag.text;
            } else if (frag.box->box_type == BoxType::ImageNode) {
                OnLines& on = on_lines[frag.box.get()];
                if (on.last_line != line_index) {
                    on.image = frag.rect;
                    on.last_line = line_index;
                    on.placed = true;
                }
            }
        }
    }
    for (auto& child : children) {
        if (child->box_type == BoxType::TextNode) {
            child->lines.clear();
            if (auto found = on_lines.find(child.get()); found != on_lines.end()) {
                child->lines = std::move(found->second.text);
            }
            if (child->lines.empty() && !child->text.empty()) {
                child->lines.push_back(child->text);
            }
            child->dimensions = dimensions;
        } else if (child->box_type == BoxType::ImageNode) {
            if (auto found = on_lines.find(child.get()); found != on_lines.end() && found->second.placed) {
                child->dimensions.content = found->second.image;
            }
        }
    }
}

void LayoutBox::resolve_out_of_flow_positions(const std::shared_ptr<LayoutBox>& root, float viewport_w, float viewport_h) {
    if (!root) return;

    std::function<void(const std::shared_ptr<LayoutBox>&, const std::shared_ptr<LayoutBox>&)> pass =
        [&](const std::shared_ptr<LayoutBox>& box, const std::shared_ptr<LayoutBox>& positioned_ancestor) {
        if (!box) return;
        if (stack_is_exhausted()) return;

        if (box->style->is_out_of_flow()) {
            Rect cb;
            // Without a positioned ancestor, absolute offsets and sizes use the
            // initial containing block, not the height of in-flow body content.
            if (box->style->position == Position::Fixed || !positioned_ancestor) {
                cb = Rect(0.0f, 0.0f, viewport_w, viewport_h);
            } else {
                const auto& cb_box = positioned_ancestor;
                cb.x = cb_box->dimensions.content.x - cb_box->dimensions.padding.left;
                cb.y = cb_box->dimensions.content.y - cb_box->dimensions.padding.top;
                cb.width = cb_box->dimensions.padding_box().width;
                cb.height = cb_box->dimensions.padding_box().height;
            }

            box->layout(cb.width, cb.height);
            const ComputedStyle& bs = *box->style;

            const auto inset = bs.used_insets(cb.width, cb.height);

            // If both insets are given and dimension is auto, size to insets
            if (bs.has_left && bs.has_right && bs.width < 0.0f) {
                float total_h = box->dimensions.margin.left + box->dimensions.margin.right +
                                box->dimensions.border.left + box->dimensions.border.right +
                                box->dimensions.padding.left + box->dimensions.padding.right;
                box->dimensions.content.width = std::max(0.0f, cb.width - inset.left - inset.right - total_h);
            }
            if (bs.has_top && bs.has_bottom && bs.height < 0.0f) {
                float total_v = box->dimensions.margin.top + box->dimensions.margin.bottom +
                                box->dimensions.border.top + box->dimensions.border.bottom +
                                box->dimensions.padding.top + box->dimensions.padding.bottom;
                box->dimensions.content.height = std::max(0.0f, cb.height - inset.top - inset.bottom - total_v);
            }

            const auto& d = box->dimensions;
            float x = cb.x + d.margin.left + d.border.left + d.padding.left;
            float y = cb.y + d.margin.top + d.border.top + d.padding.top;
            if (bs.has_left) x += inset.left;
            else if (bs.has_right) x = (cb.x + cb.width) - inset.right - d.margin.right - d.border.right - d.padding.right - d.content.width;
            if (bs.has_top) y += inset.top;
            else if (bs.has_bottom) y = (cb.y + cb.height) - inset.bottom - d.margin.bottom - d.border.bottom - d.padding.bottom - d.content.height;
            box->shift_position(x - box->dimensions.content.x, y - box->dimensions.content.y);
        }

        std::shared_ptr<LayoutBox> next_positioned = positioned_ancestor;
        if (box->style->is_positioned()) {
            next_positioned = box;
        }

        for (const auto& child : box->children) {
            pass(child, next_positioned);
        }
    };

    std::shared_ptr<LayoutBox> root_pos = root->style->is_positioned() ? root : nullptr;
    for (const auto& child : root->children) {
        pass(child, root_pos);
    }
}

void LayoutBox::shift_position(float dx, float dy) {
    if (dx == 0.0f && dy == 0.0f) return;
    if (stack_is_exhausted()) return;
    dimensions.content.x += dx;
    dimensions.content.y += dy;
    for (auto& line : line_boxes) {
        line.rect.x += dx;
        line.rect.y += dy;
        for (auto& frag : line.fragments) {
            frag.rect.x += dx;
            frag.rect.y += dy;
        }
    }
    for (auto& child : children) {
        if (!child->style->is_out_of_flow()) {
            child->shift_position(dx, dy);
        }
    }
}

void LayoutBox::layout_image(float containing_width) {
    (void)containing_width;
    const ComputedStyle& s = *style;
    float w = (s.width >= 0.0f) ? s.width : (image ? static_cast<float>(image->width) : 0.0f);
    float h = (s.height >= 0.0f) ? s.height : (image ? static_cast<float>(image->height) : 0.0f);

    if (s.box_sizing == BoxSizing::BorderBox) {
        float h_pb = dimensions.border.left + dimensions.border.right + dimensions.padding.left + dimensions.padding.right;
        float v_pb = dimensions.border.top + dimensions.border.bottom + dimensions.padding.top + dimensions.padding.bottom;
        w = std::max(0.0f, w - h_pb);
        h = std::max(0.0f, h - v_pb);
    }
    dimensions.content.width = w;
    dimensions.content.height = h;
}

void LayoutBox::dump(std::ostream& os, int indent) const {
    std::string spaces(indent * 2, ' ');
    if (stack_is_exhausted()) {
        os << spaces << "... (nested too deeply to print)\n";
        return;
    }
    os << spaces;

    switch (box_type) {
        case BoxType::FlexNode: os << "FlexBox"; break;
        case BoxType::GridNode: os << "GridBox"; break;
        case BoxType::BlockNode: os << "BlockBox"; break;
        case BoxType::InlineNode: os << "InlineBox"; break;
        case BoxType::ImageNode: os << "ImageBox"; break;
        case BoxType::AnonymousBlock: os << "AnonymousBlock"; break;
        case BoxType::TextNode: os << "TextBox"; break;
        case BoxType::TableNode: os << "TableBox"; break;
        case BoxType::TableRowNode: os << "TableRowBox"; break;
        case BoxType::TableCellNode: os << "TableCellBox"; break;
    }

    if (node && node->is_element() && !node->as_element()->id().empty()) {
        os << " #" << node->as_element()->id();
    }

    os << " [x=" << dimensions.content.x
       << " y=" << dimensions.content.y
       << " w=" << dimensions.content.width
       << " h=" << dimensions.content.height << "]";

    if (!line_boxes.empty()) {
        os << " IFC lines=" << line_boxes.size()
           << " first_line=(" << line_boxes.front().rect.x << "," << line_boxes.front().rect.y
           << " w=" << line_boxes.front().rect.width
           << " intrude=" << line_boxes.front().left_intrusion << ")";
    } else if (!lines.empty()) {
        os << " lines=" << lines.size() << " text=\"" << lines[0] << (lines.size() > 1 ? "...\"" : "\"");
    } else if (!text.empty()) {
        os << " text=\"" << text << "\"";
    }

    os << "\n";
    for (const auto& ch : children) {
        ch->dump(os, indent + 1);
    }
}

} // namespace litebird
