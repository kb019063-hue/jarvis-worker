#include "litebird/render/TerminalRenderer.hpp"
#include "litebird/render/TerminalWidthTables.hpp"
#include <algorithm>
#include <cmath>

namespace litebird {

namespace {
// A cell is a character, not a byte (Phase 241). The bytes of a UTF-8 sequence
// used to be one cell each and every one of them outside `0x20..0x7e` was
// replaced by `?`, so no page in any language but English could be shown.
//
// The stream still never carries an untrusted byte: a cluster is written out
// only when it is a scalar value, and a control character, a lone surrogate or
// malformed UTF-8 becomes U+FFFD, which no terminal reads as a command.

/// Whether `cp` may be written into the terminal stream at all. C0 (including
/// ESC, BEL and CR), DEL, the C1 range that 8-bit terminals read as commands,
/// and the two Unicode line/paragraph separators are not text a page may put in
/// front of a reader's terminal.
bool is_forbidden_in_terminal(uint32_t cp) {
    if (cp < 0x20) return true;
    if (cp == 0x7F) return true;
    if (cp >= 0x80 && cp <= 0x9F) return true;
    if (cp == 0x2028 || cp == 0x2029) return true;
    return false;
}

/// `cp` as UTF-8. The replacement character and every character that gets this
/// far is a scalar value, so there is no error case.
std::string utf8_encode(uint32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
    return out;
}

/// Decodes one code point from `bytes` at `at`. Answers the number of bytes
/// consumed (1 for an error) and sets `cp` to U+FFFD when the sequence is
/// malformed, so a bad byte advances the walk by exactly one and cannot loop.
size_t decode_cluster_codepoint(std::string_view bytes, size_t at, uint32_t& cp) {
    const auto lead = static_cast<unsigned char>(bytes[at]);
    if (lead < 0x80) { cp = lead; return 1; }
    size_t need = 0;
    uint32_t value = 0;
    uint32_t minimum = 0;
    if ((lead & 0xE0) == 0xC0) { need = 1; value = lead & 0x1Fu; minimum = 0x80u; }
    else if ((lead & 0xF0) == 0xE0) { need = 2; value = lead & 0x0Fu; minimum = 0x800u; }
    else if ((lead & 0xF8) == 0xF0) { need = 3; value = lead & 0x07u; minimum = 0x10000u; }
    else { cp = 0xFFFD; return 1; }
    if (at + need >= bytes.size()) { cp = 0xFFFD; return 1; }
    for (size_t i = 1; i <= need; ++i) {
        const auto byte = static_cast<unsigned char>(bytes[at + i]);
        if ((byte & 0xC0) != 0x80) { cp = 0xFFFD; return 1; }
        value = (value << 6) | (byte & 0x3Fu);
    }
    if (value < minimum || value > 0x10FFFFu || (value >= 0xD800u && value <= 0xDFFFu)) {
        cp = 0xFFFD;
        return 1;
    }
    cp = value;
    return need + 1;
}

/// Whether `next` extends the cluster `base` belongs to, in the terms of
/// UAX #29 that this renderer needs: a combining mark joins what it follows,
/// a zero-width joiner joins the character after it (an emoji sequence), a
/// variation selector joins, and a regional indicator pairs with one other.
///
/// The full annex also joins Hangul jamo by their own classes and Indic
/// conjuncts by their syllabic category; a renderer that only has to decide a
/// column count is served by the marks it can see in the character data, and
/// what it does not join it draws as separate cells rather than merging them.
bool extends_cluster(uint32_t base, uint32_t next) {
    if (next == 0xFE0E || next == 0xFE0F) return true;          // variation selectors
    if (terminal_width_of(next) == 0) return true;  // a mark, or a zero-width format character
    if (next == 0x200D) return true;                            // ZWJ: the next one joins
    if (base == 0x200D) return true;                            // ... and then the next one
    if (next >= 0x1F3FB && next <= 0x1F3FF) return true;        // skin-tone modifier
    return false;
}

/// The cluster width: the base character's, and never less than one for a
/// cluster that starts with something visible. A mark that has joined a
/// neighbour contributes nothing, which is the whole point of joining it.
int cluster_columns(uint32_t base) {
    return terminal_width_of(base);
}

Color over_opaque(Color src, Color dst) {
    const unsigned a = src.a;
    auto over = [a](unsigned s, unsigned d) -> uint8_t {
        return static_cast<uint8_t>((s * a + d * (255u - a) + 127u) / 255u);
    };
    return Color(over(src.r, dst.r), over(src.g, dst.g), over(src.b, dst.b));
}

// map_rect takes extrema of four transformed corners; std::min/max can hide
// a NaN in one corner, so reject invalid transforms before mapping geometry.
bool finite_transform(const TransformMatrix2D& t) {
    return std::isfinite(t.a) && std::isfinite(t.b) && std::isfinite(t.c) &&
           std::isfinite(t.d) && std::isfinite(t.e) && std::isfinite(t.f);
}

struct CellRange { int first_col, last_col, first_row, last_row; };

// Intersect in double precision before converting to integers: huge and
// non-finite geometry must not overflow an integer or get clamped onto an
// edge cell. Right and bottom are exclusive, as for bitmap rectangles.
bool intersecting_cells(const Rect& rect, float scroll_x, float scroll_y,
                        int cols, int rows, float cell_w, float cell_h,
                        CellRange& cells) {
    if (cols <= 0 || rows <= 0 || cell_w <= 0 || cell_h <= 0 ||
        !std::isfinite(rect.x) || !std::isfinite(rect.y) ||
        !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
        !std::isfinite(scroll_x) || !std::isfinite(scroll_y) ||
        !std::isfinite(cell_w) || !std::isfinite(cell_h) ||
        rect.width <= 0 || rect.height <= 0) return false;

    const double left = (static_cast<double>(rect.x) - scroll_x) / cell_w;
    const double right = (static_cast<double>(rect.x) + rect.width - scroll_x) / cell_w;
    const double top = (static_cast<double>(rect.y) - scroll_y) / cell_h;
    const double bottom = (static_cast<double>(rect.y) + rect.height - scroll_y) / cell_h;
    if (right <= 0 || bottom <= 0 || left >= cols || top >= rows) return false;
    cells = {static_cast<int>(std::floor(std::clamp(left, 0.0, static_cast<double>(cols)))),
             static_cast<int>(std::ceil(std::clamp(right, 0.0, static_cast<double>(cols)))) - 1,
             static_cast<int>(std::floor(std::clamp(top, 0.0, static_cast<double>(rows)))),
             static_cast<int>(std::ceil(std::clamp(bottom, 0.0, static_cast<double>(rows)))) - 1};
    return cells.first_col <= cells.last_col && cells.first_row <= cells.last_row;
}

struct CellOutline {
    CellRange visible;
    double left, right, top, bottom; // original edges, not clamped to the viewport
};

bool intersecting_outline(const Rect& rect, float scroll_x, float scroll_y,
                          int cols, int rows, float cell_w, float cell_h,
                          CellOutline& outline) {
    if (!intersecting_cells(rect, scroll_x, scroll_y, cols, rows, cell_w, cell_h,
                            outline.visible)) return false;
    outline.left = std::floor((static_cast<double>(rect.x) - scroll_x) / cell_w);
    outline.right = std::ceil((static_cast<double>(rect.x) + rect.width - scroll_x) / cell_w) - 1;
    outline.top = std::floor((static_cast<double>(rect.y) - scroll_y) / cell_h);
    outline.bottom = std::ceil((static_cast<double>(rect.y) + rect.height - scroll_y) / cell_h) - 1;
    return true;
}

enum class OutlineSide { Top, Right, Bottom, Left };
struct OutlineSides { bool top, right, bottom, left; };

// Visit only real outline edges. Clamping the edges themselves would invent
// borders at the viewport boundary when a box is partially or wholly outside.
// The visitor performs clipping and chooses what to paint in each cell.
template <typename PaintCell>
void visit_outline(const CellOutline& o, OutlineSides sides, int cols, int rows, PaintCell paint) {
    auto on_screen = [](double value, int size) { return value >= 0 && value < size; };
    const bool has_top = sides.top && on_screen(o.top, rows);
    const bool has_bottom = sides.bottom && on_screen(o.bottom, rows);
    const bool has_left = sides.left && on_screen(o.left, cols);
    const bool has_right = sides.right && on_screen(o.right, cols);
    if (has_top) for (int c = o.visible.first_col; c <= o.visible.last_col; ++c)
        paint(c, static_cast<int>(o.top), '-', OutlineSide::Top);
    if (has_bottom) for (int c = o.visible.first_col; c <= o.visible.last_col; ++c)
        paint(c, static_cast<int>(o.bottom), '-', OutlineSide::Bottom);
    if (has_left) for (int r = o.visible.first_row; r <= o.visible.last_row; ++r)
        paint(static_cast<int>(o.left), r, '|', OutlineSide::Left);
    if (has_right) for (int r = o.visible.first_row; r <= o.visible.last_row; ++r)
        paint(static_cast<int>(o.right), r, '|', OutlineSide::Right);
    // At a visible intersection the horizontal edge owns the corner color.
    if (has_top && has_left) paint(static_cast<int>(o.left), static_cast<int>(o.top), '+', OutlineSide::Top);
    if (has_top && has_right) paint(static_cast<int>(o.right), static_cast<int>(o.top), '+', OutlineSide::Top);
    if (has_bottom && has_left) paint(static_cast<int>(o.left), static_cast<int>(o.bottom), '+', OutlineSide::Bottom);
    if (has_bottom && has_right) paint(static_cast<int>(o.right), static_cast<int>(o.bottom), '+', OutlineSide::Bottom);
}
} // namespace

TerminalRenderer::TerminalRenderer(int cols, int rows, float cell_w, float cell_h)
    : m_cols(std::max(cols, 0)), m_rows(std::max(rows, 0)), m_cell_w(cell_w), m_cell_h(cell_h),
      m_grid(static_cast<size_t>(m_cols) * static_cast<size_t>(m_rows)) {
    // Fill with default background
    for (auto& cell : m_grid) {
        cell.cluster = " ";
        cell.codepoint = ' ';
        cell.width = 1;
        cell.fg = Color(220, 220, 220);
        cell.bg = Color(30, 30, 30);
    }
}

bool TerminalRenderer::inside_clip(int col, int row) const {
    if (m_clip_stack.empty()) return true;
    const double px = static_cast<double>(col) * m_cell_w + m_scroll_x;
    const double py = static_cast<double>(row) * m_cell_h + m_scroll_y;
    if (!std::isfinite(px) || !std::isfinite(py)) return false;
    for (const auto& clip : m_clip_stack) {
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
            !std::isfinite(clip.width) || !std::isfinite(clip.height) ||
            clip.width <= 0 || clip.height <= 0 || px < clip.x || py < clip.y ||
            px >= static_cast<double>(clip.x) + clip.width ||
            py >= static_cast<double>(clip.y) + clip.height) return false;
    }
    return true;
}

void TerminalRenderer::execute_command(const PaintCommand& cmd) {
    std::visit([this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, DrawCanvasCommand>) {
            // A canvas covers the viewport even after terminal scrolling. Its
            // transparent channels composite over the ordinary white canvas,
            // just as they do in the bitmap/PNG renderer.
            const Color background = over_opaque(arg.color, Color::white());
            for (int row = 0; row < m_rows; ++row) {
                for (int col = 0; col < m_cols; ++col) {
                    if (inside_clip(col, row)) m_grid[row * m_cols + col].bg = background;
                }
            }
        } else if constexpr (std::is_same_v<T, DrawBackdropCommand>) {
            // A terminal cell has no alpha to blend with, so the backdrop's
            // colour is composited over whatever each cell already shows. The
            // UA colour is 10% black, which is what makes a modal dialog's
            // surroundings visibly dimmer here too.
            for (int row = 0; row < m_rows; ++row) {
                for (int col = 0; col < m_cols; ++col) {
                    if (!inside_clip(col, row)) continue;
                    auto& cell = m_grid[row * m_cols + col];
                    cell.bg = over_opaque(arg.color, cell.bg);
                }
            }
        } else if constexpr (std::is_same_v<T, PushTransformCommand>) {
            auto curr = current_transform();
            m_transform_stack.push_back(curr.multiply(arg.matrix));
        } else if constexpr (std::is_same_v<T, PopTransformCommand>) {
            if (!m_transform_stack.empty()) m_transform_stack.pop_back();
        } else if constexpr (std::is_same_v<T, PushClipCommand>) {
            if (!finite_transform(current_transform()) ||
                !std::isfinite(arg.rect.x) || !std::isfinite(arg.rect.y) ||
                !std::isfinite(arg.rect.width) || !std::isfinite(arg.rect.height) ||
                arg.rect.width <= 0 || arg.rect.height <= 0) {
                m_clip_stack.emplace_back(); // an invalid/empty clip admits no ink
            } else {
                m_clip_stack.push_back(current_transform().map_rect(arg.rect));
            }
        } else if constexpr (std::is_same_v<T, PopClipCommand>) {
            if (!m_clip_stack.empty()) m_clip_stack.pop_back();
        } else if constexpr (std::is_same_v<T, DrawShadowCommand>) {
            if (!finite_transform(current_transform()) ||
                !std::isfinite(arg.rect.x) || !std::isfinite(arg.rect.y) ||
                !std::isfinite(arg.rect.width) || !std::isfinite(arg.rect.height) ||
                arg.rect.width <= 0 || arg.rect.height <= 0) return;
            Rect tr = current_transform().map_rect(arg.rect);
            tr.x += arg.x_offset;
            tr.y += arg.y_offset;
            CellRange cells;
            if (!intersecting_cells(tr, m_scroll_x, m_scroll_y, m_cols, m_rows,
                                    m_cell_w, m_cell_h, cells)) return;
            // The bitmap painter uses 0.6 of the shadow's alpha at its core.
            // A terminal has no blur kernel: approximate only that core, with
            // the specified color, and still honor its enclosing clips.
            Color shadow = arg.color;
            shadow.a = static_cast<uint8_t>(shadow.a * 0.6f);
            if (shadow.is_transparent()) return;
            for (int r = cells.first_row; r <= cells.last_row; ++r) {
                for (int c = cells.first_col; c <= cells.last_col; ++c) {
                    if (!inside_clip(c, r)) continue;
                    auto& cell = m_grid[static_cast<size_t>(r) * m_cols + c];
                    cell.bg = over_opaque(shadow, cell.bg);
                }
            }
        } else if constexpr (std::is_same_v<T, DrawRectCommand>) {
            Rect tr = current_transform().map_rect(arg.rect);
            CellRange cells;
            if (!intersecting_cells(tr, m_scroll_x, m_scroll_y, m_cols, m_rows,
                                    m_cell_w, m_cell_h, cells)) return;
            for (int r = cells.first_row; r <= cells.last_row; ++r) {
                for (int c = cells.first_col; c <= cells.last_col; ++c) {
                    if (!inside_clip(c, r)) continue;
                    auto& cell = m_grid[r * m_cols + c];
                    cell.bg = over_opaque(arg.color, cell.bg);
                }
            }
        } else if constexpr (std::is_same_v<T, DrawBorderCommand>) {
            if (!finite_transform(current_transform()) ||
                !std::isfinite(arg.rect.x) || !std::isfinite(arg.rect.y) ||
                !std::isfinite(arg.rect.width) || !std::isfinite(arg.rect.height) ||
                arg.rect.width <= 0 || arg.rect.height <= 0) return;
            const Rect tr = current_transform().map_rect(arg.rect);
            CellOutline outline;
            if (!intersecting_outline(tr, m_scroll_x, m_scroll_y, m_cols, m_rows,
                                      m_cell_w, m_cell_h, outline)) return;
            const EdgeColors colors = arg.edge_colors.value_or(EdgeColors(arg.color));
            const OutlineSides sides{
                arg.width.top > 0 && !colors.top.is_transparent(),
                arg.width.right > 0 && !colors.right.is_transparent(),
                arg.width.bottom > 0 && !colors.bottom.is_transparent(),
                arg.width.left > 0 && !colors.left.is_transparent()};
            visit_outline(outline, sides, m_cols, m_rows, [&](int c, int r, char ch, OutlineSide side) {
                if (!inside_clip(c, r)) return;
                Color color = colors.top;
                if (side == OutlineSide::Right) color = colors.right;
                else if (side == OutlineSide::Bottom) color = colors.bottom;
                else if (side == OutlineSide::Left) color = colors.left;
                auto& cell = m_grid[static_cast<size_t>(r) * m_cols + c];
                // The box-drawing characters this renderer writes are all
                // single-column, so the outline needs no width table.
                cell.cluster.assign(1, ch);
                cell.codepoint = static_cast<unsigned char>(ch);
                cell.width = 1;
                cell.fg = over_opaque(color, cell.bg);
            });
        } else if constexpr (std::is_same_v<T, DrawTextCommand>) {
            if (m_cols <= 0 || m_rows <= 0 || m_cell_w <= 0 || m_cell_h <= 0 ||
                !std::isfinite(m_cell_w) || !std::isfinite(m_cell_h) ||
                !std::isfinite(m_scroll_x) || !std::isfinite(m_scroll_y) ||
                !finite_transform(current_transform()) || arg.color.is_transparent()) return;
            const Point tp = current_transform().map_point(arg.position);
            if (!std::isfinite(tp.x) || !std::isfinite(tp.y)) return;
            const double col0 = std::floor((static_cast<double>(tp.x) - m_scroll_x) / m_cell_w);
            const double row0 = std::floor((static_cast<double>(tp.y) - m_scroll_y) / m_cell_h);
            if (row0 < 0 || row0 >= m_rows || col0 >= m_cols ||
                col0 + static_cast<double>(arg.text.size()) <= 0) return;
            const int row = static_cast<int>(row0);
            // The text is a UTF-8 string and a cell is a character, so the walk
            // is over clusters, not bytes (Phase 241). `col` starts at the
            // device column the text begins at, which may be left of the
            // viewport; clusters before it are still measured, so the ones that
            // are visible land in the column the layout gave them.
            int col = static_cast<int>(col0);
            size_t at = 0;
            while (at < arg.text.size()) {
                uint32_t base = 0;
                const size_t lead = decode_cluster_codepoint(arg.text, at, base);
                // A control character is not text a terminal may be given: C0,
                // DEL and C1 can act as commands, and U+2028/U+2029 split the
                // grid a row at a time. Every one of them becomes U+FFFD, which
                // prints as a replacement glyph and commands nothing. This is
                // the rule the old byte-at-a-time version enforced by keeping
                // only `0x20..0x7e`; it is still enforced, one character at a
                // time instead of one byte (Phase 241).
                if (is_forbidden_in_terminal(base)) base = 0xFFFD;
                std::string cluster = utf8_encode(base);
                at += lead;
                // Everything that joins this character joins its cell.
                while (at < arg.text.size()) {
                    uint32_t next = 0;
                    const size_t size = decode_cluster_codepoint(arg.text, at, next);
                    if (is_forbidden_in_terminal(next)) break;  // a control byte ends the cluster
                    if (!extends_cluster(base, next)) break;
                    cluster.append(utf8_encode(next));
                    // A joiner makes the character after it part of the cluster,
                    // but the base for the *next* decision is that character.
                    if (next != 0x200D) base = next;
                    at += size;
                    if (next == 0x200D) base = 0x200D;
                }
                const int width = cluster_columns(base == 0x200D ? cluster.front() : base);
                // A character is drawn whole or not at all. A two-column
                // character that starts in the last column has nowhere for its
                // second half, and writing it there would push the rest of the
                // row one column right (Phase 241).
                if (col >= 0 && col + std::max(1, width) <= m_cols) {
                    if (inside_clip(col, row)) {
                        place_cluster(col, row, cluster, over_opaque(arg.color,
                                      m_grid[static_cast<size_t>(row) * m_cols + col].bg),
                                      arg.font_weight >= 700);
                    }
                }
                col += std::max(1, width);
            }
        } else if constexpr (std::is_same_v<T, DrawImageCommand>) {
            if (arg.image && arg.image->is_valid()) {
                Rect tr = current_transform().map_rect(arg.rect);
                CellRange cells;
                if (!intersecting_cells(tr, m_scroll_x, m_scroll_y, m_cols, m_rows,
                                        m_cell_w, m_cell_h, cells)) return;
                for (int r = cells.first_row; r <= cells.last_row; ++r) {
                    for (int c = cells.first_col; c <= cells.last_col; ++c) {
                        if (!inside_clip(c, r)) continue;
                        // Sample in the image's own coordinates, not relative
                        // to the clamped first cell. That matters when a frame
                        // has been partially scrolled out of the viewport.
                        const float u = std::clamp((c * m_cell_w + m_scroll_x - tr.x) / tr.width, 0.0f, 1.0f);
                        const float v = std::clamp((r * m_cell_h + m_scroll_y - tr.y) / tr.height, 0.0f, 1.0f);
                        int sx = std::min(static_cast<int>(u * arg.image->width), arg.image->width - 1);
                        int sy = std::min(static_cast<int>(v * arg.image->height), arg.image->height - 1);
                        Color pixel = arg.image->get_pixel_color(sx, sy);
                        if (pixel.a == 0) continue;
                        auto& cell = m_grid[r * m_cols + c];
                        cell.bg = over_opaque(pixel, cell.bg);
                        cell.cluster = " ";
                        cell.codepoint = ' ';
                        cell.width = 1;
                    }
                }
            }
        }
    }, cmd);
}

void TerminalRenderer::draw_focus_ring(const Rect& rect, Color color) {
    CellOutline outline;
    if (color.is_transparent() ||
        !intersecting_outline(rect, m_scroll_x, m_scroll_y, m_cols, m_rows,
                              m_cell_w, m_cell_h, outline)) return;
    visit_outline(outline, {true, true, true, true}, m_cols, m_rows,
                  [&](int c, int r, char, OutlineSide) {
        if (!inside_clip(c, r)) return;
        auto& cell = m_grid[static_cast<size_t>(r) * m_cols + c];
        cell.fg = over_opaque(color, cell.bg);
        cell.bold = true;
    });
}

int TerminalRenderer::cluster_width(const std::string& cluster) {
    if (cluster.empty()) return 1;
    uint32_t base = 0;
    decode_cluster_codepoint(cluster, 0, base);
    // A cluster whose first code point is a mark takes no columns of its own;
    // one that starts with a joiner is malformed and still draws something.
    if (base == 0x200D) return 0;
    return terminal_width_of(base);
}

int TerminalRenderer::place_cluster(int col, int row, const std::string& cluster, Color fg, bool bold) {
    if (col < 0 || col >= m_cols || row < 0 || row >= m_rows) return col + 1;
    uint32_t base = 0;
    decode_cluster_codepoint(cluster, 0, base);
    const int width = cluster_width(cluster);
    auto& cell = m_grid[static_cast<size_t>(row) * m_cols + col];
    // A mark with nothing to sit on still has to draw something, or the column
    // would be silently blank; it is narrow, and it is its own cell.
    cell.cluster = (width == 0 && col == 0) ? cluster : (width == 0 ? std::string() : cluster);
    if (width == 0) {
        // Joined to the cell on its left: no character of its own, but it still
        // takes the background of the run it was written in.
        cell.fg = fg;
        cell.bold = bold;
        return col;
    }
    cell.codepoint = base;
    cell.width = width;
    cell.fg = fg;
    cell.bold = bold;
    // The second half of a wide character is a cell of its own that draws
    // nothing: a terminal that is told "this character is two columns" advances
    // twice, and the grid has to agree with that or the row shifts.
    if (width == 2 && col + 1 < m_cols) {
        auto& next = m_grid[static_cast<size_t>(row) * m_cols + col + 1];
        // The second half draws nothing of its own — the terminal moved the
        // cursor to here when it drew the wide character — but it is a cell in
        // the stream all the same, so it is written as one space in the same
        // colours. Without it the row would end one column short.
        next.cluster = " ";
        next.codepoint = ' ';
        next.width = 2;
        next.continuation = true;
        next.fg = fg;
        next.bold = bold;
    }
    return col + width;
}

void TerminalRenderer::render(const DisplayList& display_list, std::ostream& os) {
    for (const auto& cmd : display_list.commands()) {
        execute_command(cmd);
    }

    // Render ANSI grid
    for (int r = 0; r < m_rows; ++r) {
        for (int c = 0; c < m_cols; ++c) {
            const auto& cell = m_grid[r * m_cols + c];
            // The second column of a wide character is not written at all: the
            // terminal moved two columns when it drew that character, and it
            // paints both columns with the background it was given, so a cell
            // here would add a third. It stays in the grid because the columns
            // of a row are indexed, not counted (Phase 241).
            if (cell.continuation) continue;
            // Set ANSI colors
            os << "\033[38;2;" << (int)cell.fg.r << ";" << (int)cell.fg.g << ";" << (int)cell.fg.b << "m";
            os << "\033[48;2;" << (int)cell.bg.r << ";" << (int)cell.bg.g << ";" << (int)cell.bg.b << "m";
            if (cell.bold) os << "\033[1m";
            // A wide character advances the terminal by two columns on its
            // own, so the cell after it is not written: writing the space it
            // holds would add a third column and push the rest of the row to
            // the right. Its colour is the wide character's, which is why it
            // is painted at all (Phase 241).
            os << (cell.cluster.empty() ? std::string(" ") : cell.cluster);
            if (cell.bold) os << "\033[22m";
        }
        // Reset and newline
        os << "\033[0m\n";
    }
    os << "\033[0m" << std::flush;
}

std::string TerminalRenderer::to_ansi_string(const DisplayList& display_list) {
    for (const auto& cmd : display_list.commands()) {
        execute_command(cmd);
    }

    std::ostringstream ss;
    for (int r = 0; r < m_rows; ++r) {
        for (int c = 0; c < m_cols; ++c) {
            const auto& cell = m_grid[r * m_cols + c];
            if (cell.continuation) continue;
            ss << "\033[38;2;" << (int)cell.fg.r << ";" << (int)cell.fg.g << ";" << (int)cell.fg.b << "m";
            ss << "\033[48;2;" << (int)cell.bg.r << ";" << (int)cell.bg.g << ";" << (int)cell.bg.b << "m";
            if (cell.bold) ss << "\033[1m";
            // A wide character advances the terminal by two columns on its
            // own, so the cell after it is not written: writing the space it
            // holds would add a third column and push the rest of the row to
            // the right. Its colour is the wide character's, which is why it
            // is painted at all (Phase 241).
            ss << (cell.cluster.empty() ? std::string(" ") : cell.cluster);
            if (cell.bold) ss << "\033[22m";
        }
        ss << "\033[0m\n";
    }
    ss << "\033[0m";
    return ss.str();
}

} // namespace litebird
