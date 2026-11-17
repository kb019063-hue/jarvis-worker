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

    /// The family and subfamily a font file says it is.
    ///
    /// `font-family` turns on this question: to decide whether to load
    /// DejaVuSerif.ttf for `font-family: serif`, something has to know that
    /// that file *is* DejaVu Serif, and it has to know it before loading it —
    /// a host can have hundreds of faces and a page asks for two. So this is
    /// static and reads the `name` table out of the file without parsing the
    /// outlines: the table directory and one table, not the whole font.
    struct Names {
        std::string family;     ///< name id 16 (typographic family) when present, else 1
        std::string subfamily;  ///< name id 17 (typographic subfamily) when present, else 2
    };

    /// Reads `out` from the `name` table of `path`. False when the file is not
    /// a font, has no `name` table, or has no record this can decode — a font
    /// nobody can name is a font nobody can ask for by name, and falls back.
    static bool read_names(const std::string& path, Names& out);

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
    ///
    /// `family_id` is a family resolved by `family_id_for`; `kDefaultFamily`
    /// means "the element asked for nothing in particular", which is every
    /// call site that has not been taught about `font-family` yet.
    const TrueTypeFont* face_for(uint32_t codepoint, bool bold, int family_id = kDefaultFamily) const;

    // --- families -----------------------------------------------------------
    //
    // `font-family` was parsed and kept and never read. It is read now: a face
    // is chosen for the family an element names.

    /// The family of whatever the primary regular face turned out to be. Every
    /// call that names no family gets this one, so a `font-family` nobody can
    /// resolve is not an error — it is the font the page would have had anyway.
    static constexpr int kDefaultFamily = 0;

    /// The id of the family a CSS `font-family` list asks for.
    ///
    /// The list is tried left to right: the first entry that names a family on
    /// this host, or a generic family this host has a face for, wins.
    /// `kDefaultFamily` when nothing in the list can be had — which is what CSS
    /// asks for, and why this cannot fail.
    ///
    /// Quotes, case and repeated spaces are all irrelevant to matching, as CSS
    /// says they are. Resolution is cached: an element asks about its own list
    /// once per word of text, and the answer does not change.
    int family_id_for(std::string_view css_family_list) const;

    /// Adds a font file to the registry, under the family name it declares.
    /// For tests, which have to say which font they mean rather than depend on
    /// what a host happens to have; and for a host that keeps its fonts
    /// somewhere the search does not look.
    void register_font_file(const std::string& path);

    /// The families known, by id. `family_names()[id]` is family `id`; empty
    /// when no face has one, which is a host with no `.ttf` files at all.
    const std::vector<std::string>& family_names() const { return m_family_names; }

    /// Measures exact horizontal advance of UTF-8 text in pixels.
    ///
    /// `css_family` is the element's `font-family` list, resolved here and the
    /// answer cached, so a caller can hand over the string it already has
    /// rather than keep an id of its own.
    float measure_text(std::string_view utf8_text, float font_size, int font_weight = 400,
                       std::string_view css_family = {});

    /// Scaled em metrics for `font_size` px: the ascent above the baseline and
    /// the descent below it (positive). Uses the bold font when one is loaded
    /// and `font_weight` asks for bold. Falls back to (0.8em, 0.2em) when no
    /// font is available, matching the embedded 8x8 fallback.
    ///
    /// `css_family` says which font's metrics: a line box is the element's own
    /// font's, and DejaVu Serif and DejaVu Sans do not agree on where a
    /// baseline sits. It stays the *element's* family and not the family of
    /// whichever fallback face happened to supply a rare codepoint inside the
    /// line — CSS asks for one line box per line, from one font.
    std::pair<float, float> em_ascent_descent(float font_size, int font_weight = 400,
                                              std::string_view css_family = {}) const;

    /// Horizontal advance of one codepoint at `font_size` px (exact size, not
    /// rounded to a whole pixel size). Sums to `measure_text` for a string.
    float glyph_advance_px(uint32_t codepoint, float font_size, int font_weight = 400,
                           std::string_view css_family = {}) const;

    /// Draws anti-aliased UTF-8 text whose line-box top is `(top_x, top_y)`.
    /// Calls `emit_pixel(px, py, coverage)` for every covered pixel (`coverage` in 1..255).
    void draw_text(
        float top_x,
        float top_y,
        std::string_view utf8_text,
        float font_size,
        int font_weight,
        const std::function<void(int px, int py, uint8_t coverage)>& emit_pixel,
        std::string_view css_family = {}
    );

    /// The rasterized glyph, cached by (family, size, bold, codepoint). Takes a
    /// resolved `family_id` and not a list, because it is the one call here that
    /// runs per glyph of every line of every page.
    const GlyphBitmap& get_glyph(uint32_t codepoint, int pixel_size, bool bold,
                                 int family_id = kDefaultFamily);

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

    // --- the family registry ------------------------------------------------
    //
    // What `font-family` is matched against. A face is described before it is
    // loaded, from its `name` table alone: the point of the registry is to
    // decide which file to load, so it cannot load them all first.

    /// One font file, as described by its own `name` table.
    struct FaceEntry {
        std::string path;
        std::string family;      ///< as the font states it
        std::string key;         ///< lower-cased, spaces collapsed: what is matched
        std::string subfamily;   ///< "Book", "Bold", "Bold Italic"...
        int weight{400};
        bool italic{false};
    };

    /// One family: a name and the faces that carry it, at several weights.
    struct Family {
        std::string name;
        std::string key;
        std::vector<std::size_t> faces;   ///< indices into `m_face_entries`
    };

    /// How many faces of named families are held loaded at once. A page uses
    /// two or three; the cap is what stops a hostile page that cycles through
    /// every family on the host from holding all of them parsed at once.
    static constexpr std::size_t kMaxLoadedFamilyFaces = 8;

    static FaceEntry describe(const std::string& path);   ///< empty when the file says no name

    /// Which of a family's faces best answers `weight` and `bold`.
    std::size_t pick_face(const Family&, int weight, bool bold) const;

    /// The loaded face of `family_id` at `weight`, parsing it if this is the
    /// first time it has been asked for. Null when the family has no face that
    /// could be loaded.
    const TrueTypeFont* family_face(int family_id, int weight, bool bold) const;

    /// `glyph_advance_px` for a family already resolved, so that `measure_text`
    /// resolves once for the whole string and not once per codepoint.
    float advance_in_family(uint32_t codepoint, float font_size, bool want_bold,
                            int family_id) const;

    std::vector<FaceEntry> m_face_entries;
    std::vector<Family> m_families;
    std::vector<std::string> m_family_names;                    ///< by id, for `family_names()`
    std::unordered_map<std::string, int> m_family_by_key;       ///< "dejavu sans mono" -> id

    /// What a generic family name means on this host, or `kNoFamily` when
    /// nothing here fits it.
    ///
    /// `kNoFamily` and not `kDefaultFamily`, which is family 0 and a family a
    /// generic is perfectly entitled to name: using a real id as the "not
    /// chosen yet" marker is how `sans-serif` came out as DejaVu Sans Mono,
    /// because DejaVu Sans — the right answer — is family 0 and so looked
    /// unchosen forever.
    static constexpr int kNoFamily = -1;
    int m_generic_serif{kNoFamily};
    int m_generic_sans{kNoFamily};
    int m_generic_mono{kNoFamily};
    mutable std::unordered_map<std::string, int> m_resolved;    ///< a whole list -> id
    mutable std::vector<TrueTypeFont> m_family_faces;           ///< grown lazily
    mutable std::unordered_map<uint64_t, std::size_t> m_family_face_index;
};

} // namespace litebird
