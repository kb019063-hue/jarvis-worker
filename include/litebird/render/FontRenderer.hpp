#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace litebird {

struct GlyphBitmap {
    int width{0};
    int height{0};
    int bearing_x{0};   ///< Offset from pen X to left edge of bitmap
    int bearing_y{0};   ///< Offset from baseline Y to top edge of bitmap (positive = above baseline)
    float advance{0.0f};
    std::vector<uint8_t> coverage; ///< width * height alpha [0..255]
};

/// Pure-C++20 TrueType (.ttf) vector font parser and 4x supersampled scanline
/// anti-aliased UTF-8 glyph rasterizer with automatic system font discovery
/// and embedded bitmap fallback when no `.ttf` files exist on disk.
class TrueTypeFont {
public:
    bool load_file(const std::string& path);
    bool load_memory(std::vector<uint8_t> data);

    bool is_loaded() const { return m_loaded; }
    uint16_t units_per_em() const { return m_units_per_em; }
    int16_t ascent() const { return m_ascent; }
    int16_t descent() const { return m_descent; }

    uint16_t glyph_index(uint32_t codepoint) const;
    float glyph_advance(uint32_t codepoint, float font_size_px) const;
    GlyphBitmap rasterize_glyph(uint32_t codepoint, int pixel_size) const;

    /// True when this face has a glyph for `codepoint` that is not `.notdef`.
    ///
    /// Per-glyph fallback turns on this question. Before it existed the one
    /// loaded face was asked for every codepoint, and a codepoint outside its
    /// `cmap` rasterized to an empty bitmap — the glyph was not drawn wrong, it
    /// was not drawn at all. Asking first is what lets another face answer.
    bool has_glyph(uint32_t codepoint) const { return glyph_index(codepoint) != 0; }

private:
    struct Vec2 {
        float x{0.0f};
        float y{0.0f};
    };
    struct Edge {
        float x0{0.0f}, y0{0.0f};
        float x1{0.0f}, y1{0.0f};
    };

    bool find_table(const char tag[4], uint32_t& offset_out, uint32_t& length_out) const;
    bool glyph_data_range(uint16_t glyph_id, uint32_t& start_out, uint32_t& end_out) const;
    void append_glyph_edges(
        uint16_t glyph_id,
        float scale,
        float baseline_y,
        float a, float b, float c, float d,
        float tx_units, float ty_units,
        int depth,
        std::vector<Edge>& edges_out
    ) const;

    std::vector<uint8_t> m_data;
    bool m_loaded{false};
    uint16_t m_units_per_em{2048};
    int16_t m_ascent{1638};
    int16_t m_descent{-410};
    int16_t m_loc_format{0};
    uint16_t m_num_glyphs{0};
    uint16_t m_num_hmetrics{0};

    uint32_t m_cmap_subtable{0};
    uint16_t m_cmap_format{0};
    uint32_t m_loca_offset{0};
    uint32_t m_loca_length{0};
    uint32_t m_glyf_offset{0};
    uint32_t m_glyf_length{0};

    std::vector<uint16_t> m_advance_widths;
    uint16_t m_ascii_glyphs[128]{};
};

class FontRenderer {
public:
    /// How many faces fallback will load at most, however many the host has.
    /// Loading is lazy — a face is parsed when a codepoint needs it — so this
    /// caps the worst case of a page that asks for a face nobody has.
    static constexpr std::size_t kMaxFallbackFaces = 24;

    static FontRenderer& instance();

    /// True if at least the regular system TrueType font was loaded.
    bool has_truetype_font() const { return m_regular.is_loaded(); }

    /// The faces tried, in order, for a codepoint the primary face does not
    /// cover. Discovery fills this with the system's `.ttf` files at
    /// construction; `set_fallbacks` replaces it. The faces themselves are
    /// loaded as they are needed and not up front, because a host can have
    /// hundreds of fonts and a page usually needs none of them.
    void set_fallbacks(std::vector<std::string> paths);
    const std::vector<std::string>& fallback_paths() const { return m_fallback_paths; }

    /// Which face would draw `codepoint`: the primary when it covers it, else
    /// the first fallback that does, else the primary — so that a codepoint no
    /// face covers still draws one `.notdef` box, from a predictable face,
    /// rather than nothing. Null when no face is loaded at all.
    ///
    /// The line box is *not* taken from this face: `em_ascent_descent` stays on
    /// the primary, which is what CSS asks for — one line box per line, from
    /// the element's own font, not from whichever face happened to supply a
    /// rare codepoint inside it.
    const TrueTypeFont* face_for(uint32_t codepoint, bool bold) const;

    /// Measures exact horizontal advance of UTF-8 text in pixels.
    float measure_text(std::string_view utf8_text, float font_size, int font_weight = 400);

    /// Scaled em metrics for `font_size` px: the ascent above the baseline and
    /// the descent below it (positive). Uses the bold font when one is loaded
    /// and `font_weight` asks for bold. Falls back to (0.8em, 0.2em) when no
    /// font is available, matching the embedded 8x8 fallback.
    std::pair<float, float> em_ascent_descent(float font_size, int font_weight = 400) const;

    /// Horizontal advance of one codepoint at `font_size` px (exact size, not
    /// rounded to a whole pixel size). Sums to `measure_text` for a string.
    float glyph_advance_px(uint32_t codepoint, float font_size, int font_weight = 400) const;

    /// Draws anti-aliased UTF-8 text whose line-box top is `(top_x, top_y)`.
    /// Calls `emit_pixel(px, py, coverage)` for every covered pixel (`coverage` in 1..255).
    void draw_text(
        float top_x,
        float top_y,
        std::string_view utf8_text,
        float font_size,
        int font_weight,
        const std::function<void(int px, int py, uint8_t coverage)>& emit_pixel
    );

    const GlyphBitmap& get_glyph(uint32_t codepoint, int pixel_size, bool bold);

private:
    FontRenderer();

    TrueTypeFont m_regular;
    TrueTypeFont m_bold;
    std::string m_regular_path;  ///< where `m_regular` came from; not a fallback for itself
    std::string m_bold_path;
    std::vector<std::string> m_fallback_paths;
    mutable std::vector<TrueTypeFont> m_fallbacks;  ///< grown lazily, on the first miss
    mutable std::size_t m_next_fallback{0};
    std::unordered_map<uint64_t, GlyphBitmap> m_cache;
    static constexpr std::size_t kMaxCachedGlyphs = 4096;
};

} // namespace litebird
