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

        /// `OS/2.usWeightClass`, or 0 when the file has no `OS/2` table that
        /// says. It is the same 1..1000 scale CSS uses, which is the point:
        /// 400 means 400 on both sides, and no word has to be translated.
        ///
        /// The subfamily name is the fallback, not the source. A foundry
        /// writes whatever it likes there — "Halbfett", "Gras", "Bold
        /// Condensed Headline" — and matching those words is a guess that
        /// happens to be right for the fonts on this machine and wrong for
        /// somebody else's.
        int weight{0};

        /// The file says it is slanted: `OS/2.fsSelection` bit 0 or
        /// `head.macStyle` bit 1. Either will do — fonts disagree about which,
        /// and a face that sets one of them is italic.
        bool italic{false};
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
    /// `synthetic_oblique`, when given, is set when the face returned is
    /// upright but a slant was asked for and none could be had: see
    /// `family_face`. A fallback face that supplies a rare codepoint is upright
    /// however the line was set, but only the named family's slant is the
    /// element's business.
    ///
    /// `font_weight` is the CSS weight, 1..1000 — not "whether it is bold". A
    /// bool here would mean the registry is only ever asked two questions, and
    /// it was: every request collapsed to 400 or 700 before a face was chosen,
    /// so a family with a 300 face could not be asked for it and a page's
    /// `font-weight: 300` silently got the 400.
    const TrueTypeFont* face_for(uint32_t codepoint, int font_weight, int family_id = kDefaultFamily,
                                 bool italic = false, bool* synthetic_oblique = nullptr) const;

    // --- families -----------------------------------------------------------
    //
    // `font-family` was parsed and kept and never read. It is read now: a face
    // is chosen for the family an element names.

    /// "No family was named", which is not the same as "family number 0".
    ///
    /// It is negative so that it cannot collide with a real id. It was `0`
    /// once, and `family_face` rejected `family_id <= kDefaultFamily`, which
    /// meant the first family the registry ever saw — on a host with no system
    /// fonts, the one a test registers by hand — could not be selected by
    /// name at all: `font-family: Ahem` drew the default face, silently. The
    /// third time this sentinel has been the bug, in three different places.
    static constexpr int kDefaultFamily = -1;

    /// "This generic family has no face on this host", which is not the same
    /// as "it resolved to the default one". Public because `generic_family_id`
    /// returns it and a caller has to be able to compare against it.
    ///
    /// And not `kDefaultFamily`: a generic is perfectly entitled to name the
    /// default family, and using a real id as the "not chosen yet" marker is
    /// how `sans-serif` came out as DejaVu Sans Mono, because DejaVu Sans —
    /// the right answer — looked unchosen forever.
    static constexpr int kNoFamily = -2;

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

    /// The family a generic name — `serif`, `sans-serif`, `monospace` — means
    /// on this host, or `kNoFamily` when nothing here fits it.
    ///
    /// This is the question `family_id_for` cannot answer: when a generic has
    /// no face it returns `kDefaultFamily`, which is also what it returns for a
    /// family that resolved to the default one. A test has to be able to tell
    /// "this host has no serif" from "this host's serif *is* the default", or
    /// it ends up asserting a fact about the machine it was written on.
    int generic_family_id(std::string_view generic) const;

    /// The family `kDefaultFamily` stands for: whichever one the primary
    /// regular face belongs to. `kNoFamily` when there is no primary face.
    int default_family_id() const { return m_default_family; }

    /// Adds a font file to the registry, under the family name it declares, and
    /// returns the id of the family it joined. For tests, which have to say
    /// which font they mean rather than depend on what a host happens to have;
    /// and for a host that keeps its fonts somewhere the search does not look.
    ///
    /// `kNoFamily` when the file is not a font or declares no name. The id is
    /// returned rather than left for the caller to look up, because looking it
    /// up by name is what a caller cannot do when the answer is `0`: `0` used
    /// to read as "no family" and such a family was unselectable.
    int register_font_file(const std::string& path);

    /// The families known, by id. `family_names()[id]` is family `id`; empty
    /// when no face has one, which is a host with no `.ttf` files at all.
    const std::vector<std::string>& family_names() const { return m_family_names; }

    /// Registers a face the page brought with it, under the family name the
    /// page gave it, and returns the id of the family it joined.
    ///
    /// `fetch` is called the first time something wants to draw in this face
    /// and not before, because a page that declares twenty families usually
    /// uses one, and a webfont is a network fetch. It returns the bytes, or
    /// empty when they could not be had — a face that will not load is skipped
    /// in favour of the next one, which is what a browser does and what keeps
    /// a page legible when its font server is down.
    ///
    /// The family is the one the rule names and not the one inside the file:
    /// a page may call a font anything it likes, and several rules may serve
    /// several weights of one family from several files. A webfont also takes
    /// precedence over a family of the same name already on the host, which is
    /// the CSS rule — the page's font is the answer, and the system's is what
    /// it falls back to when the page's does not load.
    ///
    /// `fetch` takes no arguments and returns `std::vector<std::uint8_t>`, so
    /// that this class never has to know what a network is: the caller that
    /// parsed the rule knows the URL, the origin and the CORS mode, and
    /// captures them.
    /// The bytes of a webfont, produced on demand. Empty when they could not
    /// be had, which is the answer that makes the next source in the list be
    /// tried.
    using WebfontFetcher = std::function<std::vector<std::uint8_t>()>;

    /// `sources` is one `@font-face` rule's `src` list, in order: a fetcher per
    /// source, each returning that source's bytes or nothing. They are called
    /// only when the face is first wanted, and only until one parses.
    int register_webfont(std::string family, std::vector<WebfontFetcher> sources,
                         int weight_min = 400, int weight_max = 400,
                         bool italic = false);

    /// The path of the face a `font-family` list resolves to, for a caller
    /// that needs the file itself.
    ///
    /// `local()` in a `@font-face` `src` names a font already on the host, so
    /// resolving one means reading a file the registry knows about rather than
    /// fetching a URL. Empty when the list names nothing here -- which is the
    /// signal to move on to the next source.
    std::string face_path(std::string_view css_family_list) const;

    /// What a face of a family is, as the registry described it: the weight
    /// range it answers for, and whether it is slanted.
    ///
    /// This is the question `pick_face` asks internally, asked out loud. Two
    /// callers need it: one deciding whether to synthesise a bold or a slant --
    /// which it cannot decide from the family's name, because a name does not
    /// say what the family offers -- and a test that has to see which face the
    /// registry believes a file is.
    struct FaceFacts {
        int weight_min{400};
        int weight_max{400};
        bool italic{false};
        bool webfont{false};
    };

    /// The faces of `css_family_list`, in the order they are considered. Empty
    /// when the list names no family here.
    std::vector<FaceFacts> family_face_facts(std::string_view css_family_list) const;

    /// Forgets every face registered by `register_webfont`, and releases any of
    /// them already loaded.
    ///
    /// A webfont belongs to the document that declared it. The registry is a
    /// process-wide singleton because the rasterizer is, so without this a
    /// second page could ask for a family by name and be given the first
    /// page's font — and worse, a name both pages use would silently resolve
    /// to whichever page was laid out first.
    void clear_webfonts();

    /// Measures exact horizontal advance of UTF-8 text in pixels.
    ///
    /// `css_family` is the element's `font-family` list, resolved here and the
    /// answer cached, so a caller can hand over the string it already has
    /// rather than keep an id of its own.
    ///
    /// `italic` is the element's `font-style: italic` or `oblique`, which is
    /// *not* only a drawing matter: an italic or oblique face has its own
    /// advances, and they are usually narrower than the upright ones. Measuring
    /// upright and drawing slanted would put the last word of every italic line
    /// past the edge of the box layout gave it. When no slanted face can be
    /// had the advance is the upright face's, which is what a synthesised
    /// oblique is — a shear changes where the ink goes, not how far the pen
    /// travels.
    float measure_text(std::string_view utf8_text, float font_size, int font_weight = 400,
                       std::string_view css_family = {}, bool italic = false);

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
                                              std::string_view css_family = {},
                                              bool italic = false) const;

    /// Horizontal advance of one codepoint at `font_size` px (exact size, not
    /// rounded to a whole pixel size). Sums to `measure_text` for a string.
    float glyph_advance_px(uint32_t codepoint, float font_size, int font_weight = 400,
                           std::string_view css_family = {}, bool italic = false) const;

    /// Draws anti-aliased UTF-8 text whose line-box top is `(top_x, top_y)`.
    /// Calls `emit_pixel(px, py, coverage)` for every covered pixel (`coverage` in 1..255).
    ///
    /// The pen advances by the advance of the face the glyph came from, not by
    /// the primary face's: those are the numbers layout measured with, and a
    /// run of monospace text drawn at proportional advances overlaps itself.
    void draw_text(
        float top_x,
        float top_y,
        std::string_view utf8_text,
        float font_size,
        int font_weight,
        const std::function<void(int px, int py, uint8_t coverage)>& emit_pixel,
        std::string_view css_family = {},
        bool italic = false
    );

    /// The rasterized glyph, cached by (family, size, bold, italic, codepoint).
    /// Takes a resolved `family_id` and not a list, because it is the one call
    /// here that runs per glyph of every line of every page.
    ///
    /// `italic` reaches the cache key because a synthesised oblique is a
    /// *different bitmap* of the same glyph: keyed without it, whichever of the
    /// two was rasterized first would answer for both, and half the page would
    /// come out slanted. The weight is in the key for the same reason and one
    /// more: a family with a 300 face and a 500 face draws the glyph
    /// differently at each, so a key that said only "bold" would answer one
    /// with the other.
    const GlyphBitmap& get_glyph(uint32_t codepoint, int pixel_size, int font_weight,
                                 int family_id = kDefaultFamily, bool italic = false);

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
        /// Weight range this face answers for. A face on disk declares `400`
        /// or `700`; a webfont declares whatever its rule says, which may be a
        /// range -- `font-weight: 100 400` -- and a face is eligible only when
        /// the weight asked for falls inside it.
        int weight_min{400};
        int weight_max{400};
        bool italic{false};
        /// True for a face a page brought with it. A webfont outranks a family
        /// of the same name that was already on the host: the page's font is
        /// the answer, and the system's is the fallback.
        bool webfont{false};
        /// Where the bytes come from, for a webfont, in the order the rule
        /// gave them. Each is called in turn the first time the face is
        /// wanted, until one returns bytes that parse: `src` is a fallback
        /// list, and `url(a.woff), url(a.ttf)` means "the second, if the
        /// first is no use". Empty bytes from one is not failure of the face,
        /// it is an instruction to try the next.
        std::vector<WebfontFetcher> sources;
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

    /// Which of a family's faces best answers `weight`, `bold` and `italic`.
    ///
    /// A slanted face is only eligible when the slant was asked for, and an
    /// upright one is passed over for it when the family has both — but a
    /// family that has *only* upright faces still answers, because a browser
    /// does not drop to another family when the italic is missing: it slants
    /// what it has. That is the second half of this function's job, and the
    /// reason it returns an index rather than a face.
    std::size_t pick_face(const Family&, int weight, bool bold, bool italic) const;

    /// The loaded face of `family_id` at `weight`, parsing it if this is the
    /// first time it has been asked for. Null when the family has no face that
    /// could be loaded.
    ///
    /// `synthetic_oblique`, when given, is set to true when the slant was
    /// asked for and the face that answered is upright: the caller is to shear
    /// it. Reported rather than assumed, because "the family had no italic" and
    /// "the family's italic is the one we got" both answer with a face.
    const TrueTypeFont* family_face(int family_id, int weight, bool bold, bool italic = false,
                                    bool* synthetic_oblique = nullptr) const;

    /// `glyph_advance_px` for a family already resolved, so that `measure_text`
    /// resolves once for the whole string and not once per codepoint. Takes the
    /// weight and not a bool for the reason `face_for` does.
    float advance_in_family(uint32_t codepoint, float font_size, int font_weight,
                            int family_id, bool italic = false) const;

    std::vector<FaceEntry> m_face_entries;
    std::vector<Family> m_families;
    std::vector<std::string> m_family_names;                    ///< by id, for `family_names()`
    std::unordered_map<std::string, int> m_family_by_key;       ///< "dejavu sans mono" -> id

    /// The family the primary regular face belongs to, which is what
    /// `kDefaultFamily` actually names. `kNoFamily` when the primary did not
    /// load or declares no family.
    ///
    /// Needed because "the default" is not a claim about a font, it is a claim
    /// about whichever file the host had: on a host whose only face is
    /// monospace, the default *is* monospace, and a caller that assumes the
    /// default is proportional is asserting a fact about the machine.
    int m_default_family{kNoFamily};

    int m_generic_serif{kNoFamily};
    int m_generic_sans{kNoFamily};
    int m_generic_mono{kNoFamily};
    mutable std::unordered_map<std::string, int> m_resolved;    ///< a whole list -> id
    mutable std::vector<TrueTypeFont> m_family_faces;           ///< grown lazily
    mutable std::unordered_map<uint64_t, std::size_t> m_family_face_index;
};

} // namespace litebird
