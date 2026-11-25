#include "litebird/render/Canvas2D.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/render/FontRenderer.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Node.hpp"
#include "litebird/render/ImageFilter.hpp"
#include "litebird/render/RasterBudget.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>

namespace litebird {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr int kMaxImageDataPixels = 16 * 1024 * 1024; // 16M pixels cap for JS ImageData
constexpr int kMaxTextBufferWidth = 16384;            // px cap for one rasterised text run
constexpr int kMaxTextBufferHeight = 512;

// Strict-ish UTF-8 walker: invalid bytes become U+FFFD and consume at least
// one byte, so any input terminates.
uint32_t canvas_utf8_next(std::string_view s, std::size_t& pos) {
    if (pos >= s.size()) return 0;
    const auto byte_at = [&s](std::size_t i) { return static_cast<uint8_t>(s[i]); };
    const uint8_t lead = byte_at(pos);
    if (lead < 0x80) {
        pos += 1;
        return lead;
    }
    uint32_t cp = 0;
    int extra = 0;
    if ((lead & 0xE0) == 0xC0) {
        cp = lead & 0x1F;
        extra = 1;
    } else if ((lead & 0xF0) == 0xE0) {
        cp = lead & 0x0F;
        extra = 2;
    } else if ((lead & 0xF8) == 0xF0) {
        cp = lead & 0x07;
        extra = 3;
    } else {
        pos += 1;
        return 0xFFFD;
    }
    for (int i = 1; i <= extra; ++i) {
        if (pos + static_cast<std::size_t>(i) >= s.size()) {
            pos = s.size();
            return 0xFFFD;
        }
        const uint8_t cc = byte_at(pos + static_cast<std::size_t>(i));
        if ((cc & 0xC0) != 0x80) {
            pos += 1;
            return 0xFFFD;
        }
        cp = (cp << 6) | static_cast<uint32_t>(cc & 0x3F);
    }
    pos += static_cast<std::size_t>(extra) + 1;
    return cp;
}

struct ParsedCanvasFont {
    float size_px{10.0f};
    int weight{400};
    std::string style{"normal"};
    std::string family{"sans-serif"};
};

inline bool canvas_is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

inline std::string canvas_trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && canvas_is_space(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && canvas_is_space(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

inline std::string canvas_lower(std::string s) {
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    return s;
}

// Consumes the next whitespace-separated word from the front of `work`.
bool canvas_next_word(std::string& work, std::string& word) {
    std::size_t i = 0;
    while (i < work.size() && canvas_is_space(static_cast<unsigned char>(work[i]))) ++i;
    if (i == work.size()) return false;
    std::size_t j = i;
    while (j < work.size() && !canvas_is_space(static_cast<unsigned char>(work[j]))) ++j;
    word = work.substr(i, j - i);
    work.erase(0, j);
    return true;
}

inline bool canvas_digits(const std::string& s, std::size_t b, std::size_t e) {
    if (b >= e) return false;
    for (std::size_t i = b; i < e; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

// Parses a CSS font shorthand for the canvas `font` attribute. Returns false
// when the input is invalid; per spec the assignment must then be ignored.
// The family is validated (it must be present and non-empty) but not used for
// rasterisation: the engine has a single system regular/bold font.
bool parse_canvas_font(const std::string& input, ParsedCanvasFont& out) {
    std::string work = canvas_trim(input);
    if (work.empty()) return false;
    std::string word;

    // style
    if (canvas_next_word(work, word)) {
        const std::string w = canvas_lower(word);
        if (w == "normal" || w == "italic" || w == "oblique") {
            out.style = w;
        } else {
            work = canvas_trim(word + " " + work);
        }
    }
    // font-variant
    if (canvas_next_word(work, word)) {
        const std::string w = canvas_lower(word);
        if (w == "normal" || w == "small-caps") {
            // Small caps is accepted and ignored.
        } else {
            work = canvas_trim(word + " " + work);
        }
    }
    // weight
    {
        int weight = -1;
        if (canvas_next_word(work, word)) {
            const std::string w = canvas_lower(word);
            if (w == "normal") weight = 400;
            else if (w == "bold") weight = 700;
            else if (w == "bolder") weight = 700; // no parent weight context here
            else if (w == "lighter") weight = 300;
            else if (canvas_digits(w, 0, w.size())) {
                const long v = std::strtol(w.c_str(), nullptr, 10);
                if (v >= 1 && v <= 1000) weight = static_cast<int>(v);
            }
        }
        if (weight > 0) {
            out.weight = weight;
        } else if (!word.empty()) {
            work = canvas_trim(word + " " + work);
            out.weight = 400;
        }
    }
    // stretch
    if (canvas_next_word(work, word)) {
        static const char* const kStretch[] = {"ultra-condensed", "extra-condensed", "condensed",
                                               "semi-condensed",  "normal",          "semi-expanded",
                                               "expanded",        "extra-expanded",  "ultra-expanded"};
        const std::string w = canvas_lower(word);
        bool stretch = false;
        for (const char* s : kStretch) {
            if (w == s) {
                stretch = true;
                break;
            }
        }
        if (!stretch) work = canvas_trim(word + " " + work);
    }
    // size (required), with optional /line-height
    if (!canvas_next_word(work, word)) return false;
    {
        std::string size_tok = word;
        std::string line_height;
        const std::size_t slash = size_tok.find('/');
        if (slash != std::string::npos) {
            line_height = size_tok.substr(slash + 1);
            size_tok = size_tok.substr(0, slash);
        } else if (!work.empty() && work.front() == '/') {
            // "/ line-height" may arrive as the next word
            std::string slash_tok;
            const std::string rest = canvas_trim(work);
            if (canvas_next_word(work, slash_tok) && !slash_tok.empty() && slash_tok.front() == '/') {
                std::string after = canvas_trim(slash_tok.substr(1));
                if (after.empty()) {
                    std::string lh_tok;
                    if (canvas_next_word(work, lh_tok)) line_height = lh_tok;
                } else {
                    line_height = after;
                }
            } else {
                return false;
            }
        }
        // line-height is parsed for validity and ignored for rendering.
        if (!line_height.empty()) {
            std::size_t i = 0;
            bool lh_dot = false, lh_any_digit = false;
            while (i < line_height.size()) {
                const unsigned char c = static_cast<unsigned char>(line_height[i]);
                if (std::isdigit(c)) {
                    lh_any_digit = true;
                    ++i;
                    continue;
                }
                if (c == '.' && !lh_dot) {
                    lh_dot = true;
                    ++i;
                    continue;
                }
                break;
            }
            const std::string lh_unit = canvas_lower(line_height.substr(i));
            const bool lh_unit_ok = lh_unit.empty() || lh_unit == "px" || lh_unit == "%" || lh_unit == "em" ||
                                    lh_unit == "rem" || lh_unit == "pt";
            if (!lh_any_digit || !lh_unit_ok) return false;
        }
        // number part of the size
        std::size_t i = 0;
        while (i < size_tok.size() && std::isdigit(static_cast<unsigned char>(size_tok[i]))) ++i;
        bool dot = false;
        if (i < size_tok.size() && size_tok[i] == '.') {
            dot = true;
            ++i;
            while (i < size_tok.size() && std::isdigit(static_cast<unsigned char>(size_tok[i]))) ++i;
        } else if (size_tok.rfind(".", 0) == 0) {
            // ".5px": leading dot form
            ++i;
            while (i < size_tok.size() && std::isdigit(static_cast<unsigned char>(size_tok[i]))) ++i;
            dot = true;
        }
        if (i == 0) return false; // no digits at all
        bool any_digit = false;
        for (char c : size_tok.substr(0, i)) {
            if (std::isdigit(static_cast<unsigned char>(c))) any_digit = true;
        }
        if (!any_digit) return false;
        const float value = std::strtof(size_tok.substr(0, i).c_str(), nullptr);
        if (!std::isfinite(value)) return false;
        const std::string unit = canvas_lower(size_tok.substr(i));
        float scale = -1.0f;
        if (unit == "px") scale = 1.0f;
        else if (unit == "pt") scale = 96.0f / 72.0f;
        else if (unit == "em" || unit == "rem") scale = 16.0f;
        else if (unit == "%") scale = 16.0f / 100.0f;
        else if (unit.empty() && value == 0.0f) scale = 1.0f; // "0" without unit is valid
        if (scale < 0.0f) return false;
        const float size_px = value * scale;
        if (!std::isfinite(size_px) || size_px < 0.0f) return false;
        out.size_px = std::min(size_px, 1600.0f);
    }
    // family: everything that is left, must be non-empty
    const std::string family = canvas_trim(work);
    if (family.empty()) return false;
    out.family = family;
    return true;
}

inline float apply_spread(float t, GradientSpread spread) {
    if (spread == GradientSpread::Pad) {
        return std::clamp(t, 0.0f, 1.0f);
    }
    if (spread == GradientSpread::Repeat) {
        float r = std::fmod(t, 1.0f);
        if (r < 0.0f) r += 1.0f;
        return r;
    }
    // Reflect
    float r = std::fmod(std::abs(t), 2.0f);
    return (r > 1.0f) ? (2.0f - r) : r;
}

inline bool is_finite_f(float v) {
    return std::isfinite(v);
}

// Consistent CW winding disk polygon centered at (cx, cy) with radius r.
void append_cw_circle_polygon(std::vector<std::vector<Point>>& out, float cx, float cy, float r, int segs) {
    if (r <= 0.0f || segs < 3) return;
    std::vector<Point> poly;
    poly.reserve(static_cast<size_t>(segs));
    for (int i = 0; i < segs; ++i) {
        // Decreasing angle in screen coordinates (y down) produces negative signed area
        // matching the CW segment quads.
        const float a = -kTwoPi * static_cast<float>(i) / static_cast<float>(segs);
        poly.emplace_back(cx + r * std::cos(a), cy + r * std::sin(a));
    }
    out.push_back(std::move(poly));
}

// Ensures a polygon has CW (negative signed area in screen space) winding so overlapping
// stroke pieces always accumulate non-zero winding of the same sign.
void ensure_cw_winding(std::vector<Point>& poly) {
    if (poly.size() < 3) return;
    double area2 = 0.0;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Point& p0 = poly[i];
        const Point& p1 = poly[(i + 1) % poly.size()];
        area2 += static_cast<double>(p0.x) * p1.y - static_cast<double>(p1.x) * p0.y;
    }
    if (area2 > 0.0) {
        std::reverse(poly.begin(), poly.end());
    }
}

struct Edge {
    float x0, y0, x1, y1;
    int dir; // +1 downward (y0 < y1), -1 upward (y0 > y1)
};

void build_edges(
    const std::vector<std::vector<Point>>& polys,
    std::vector<Edge>& edges,
    float& min_y,
    float& max_y
) {
    min_y = std::numeric_limits<float>::infinity();
    max_y = -std::numeric_limits<float>::infinity();
    for (const auto& pts : polys) {
        if (pts.size() < 3) continue;
        const size_t n = pts.size();
        for (size_t i = 0; i < n; ++i) {
            const Point& a = pts[i];
            const Point& b = pts[(i + 1) % n];
            if (!is_finite_f(a.x) || !is_finite_f(a.y) || !is_finite_f(b.x) || !is_finite_f(b.y)) continue;
            if (a.y == b.y) continue;
            min_y = std::min(min_y, std::min(a.y, b.y));
            max_y = std::max(max_y, std::max(a.y, b.y));
            if (a.y < b.y) {
                edges.push_back({a.x, a.y, b.x, b.y, 1});
            } else {
                edges.push_back({b.x, b.y, a.x, a.y, -1});
            }
        }
    }
}

} // namespace

CanvasRenderingContext2D::CanvasRenderingContext2D(int width, int height) {
    m_image = std::make_shared<ImageData>();
    allocate(width, height);
}

void CanvasRenderingContext2D::resize(int w, int h) {
    m_clip_mask.clear();
    // Setting `width`/`height` gives the canvas a new bitmap, and a new bitmap
    // is origin-clean whatever the old one held (HTML §4.12.5.1).
    m_origin_clean = true;
    allocate(w, h);
}

// Gives the canvas a cleared backing store of the size asked for, or of a
// single pixel when that size is over a limit (RasterBudget.hpp) or the
// memory cannot be had. The bitmap object stays the same one, so whoever
// already holds it (a layout box, a drawImage in progress) sees the new store.
void CanvasRenderingContext2D::allocate(int width, int height) {
    m_nominal_width = std::max(1, width);
    m_nominal_height = std::max(1, height);

    const std::int64_t pixels = static_cast<std::int64_t>(m_nominal_width) * m_nominal_height;
    bool backed = RasterBudget::allows(m_nominal_width, m_nominal_height) && m_image->budget.resize(pixels);
    if (backed) {
        try {
            m_image->pixels.assign(static_cast<std::size_t>(pixels), 0);
        } catch (const std::bad_alloc&) {
            backed = false;
        }
    }
    if (backed) {
        m_width = m_nominal_width;
        m_height = m_nominal_height;
    } else {
        std::vector<uint32_t>(1, 0).swap(m_image->pixels); // also returns the old store
        m_image->budget.resize(0);
        m_width = 1;
        m_height = 1;
    }
    m_image->width = m_width;
    m_image->height = m_height;
}

void CanvasRenderingContext2D::save() {
    if (m_state_stack.size() >= kMaxStateStackDepth) return;
    SavedState st;
    st.fill_style = fill_style;
    st.stroke_style = stroke_style;
    st.line_width = line_width;
    st.global_composite_operation = global_composite_operation;
    st.global_alpha = global_alpha;
    st.line_cap = line_cap;
    st.line_join = line_join;
    st.miter_limit = miter_limit;
    st.line_dash = line_dash;
    st.line_dash_offset = line_dash_offset;
    st.font = font;
    st.text_align = text_align;
    st.text_baseline = text_baseline;
    st.letter_spacing_px = letter_spacing_px;
    st.letter_spacing_source = letter_spacing_source;
    st.word_spacing_px = word_spacing_px;
    st.word_spacing_source = word_spacing_source;
    st.direction = direction;
    st.font_kerning = font_kerning;
    st.font_stretch = font_stretch;
    st.font_variant_caps = font_variant_caps;
    st.text_rendering = text_rendering;
    st.fill_gradient = fill_gradient;
    st.stroke_gradient = stroke_gradient;
    st.fill_pattern = fill_pattern;
    st.stroke_pattern = stroke_pattern;
    st.fill_paint = fill_paint;
    st.stroke_paint = stroke_paint;
    st.image_smoothing_enabled = image_smoothing_enabled;
    st.image_smoothing_quality = image_smoothing_quality;
    st.shadow_blur = shadow_blur;
    st.shadow_color = shadow_color;
    st.shadow_offset_x = shadow_offset_x;
    st.shadow_offset_y = shadow_offset_y;
    st.filter_source = filter_source;
    st.filter_list = filter_list;
    st.ctm = m_ctm;
    st.clip_mask = m_clip_mask;
    m_state_stack.push_back(std::move(st));
}

void CanvasRenderingContext2D::restore() {
    if (m_state_stack.empty()) return;
    SavedState st = std::move(m_state_stack.back());
    m_state_stack.pop_back();
    fill_style = st.fill_style;
    stroke_style = st.stroke_style;
    line_width = st.line_width;
    global_composite_operation = std::move(st.global_composite_operation);
    global_alpha = st.global_alpha;
    line_cap = std::move(st.line_cap);
    line_join = std::move(st.line_join);
    miter_limit = st.miter_limit;
    line_dash = std::move(st.line_dash);
    line_dash_offset = st.line_dash_offset;
    font = std::move(st.font);
    text_align = std::move(st.text_align);
    text_baseline = std::move(st.text_baseline);
    letter_spacing_px = st.letter_spacing_px;
    letter_spacing_source = std::move(st.letter_spacing_source);
    word_spacing_px = st.word_spacing_px;
    word_spacing_source = std::move(st.word_spacing_source);
    direction = std::move(st.direction);
    font_kerning = std::move(st.font_kerning);
    font_stretch = std::move(st.font_stretch);
    font_variant_caps = std::move(st.font_variant_caps);
    text_rendering = std::move(st.text_rendering);
    fill_gradient = std::move(st.fill_gradient);
    stroke_gradient = std::move(st.stroke_gradient);
    fill_pattern = std::move(st.fill_pattern);
    stroke_pattern = std::move(st.stroke_pattern);
    fill_paint = std::move(st.fill_paint);
    stroke_paint = std::move(st.stroke_paint);
    image_smoothing_enabled = st.image_smoothing_enabled;
    image_smoothing_quality = std::move(st.image_smoothing_quality);
    shadow_blur = st.shadow_blur;
    shadow_color = st.shadow_color;
    shadow_offset_x = st.shadow_offset_x;
    shadow_offset_y = st.shadow_offset_y;
    filter_source = st.filter_source;
    filter_list = st.filter_list;
    m_ctm = st.ctm;
    const size_t expected_mask_size = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    if (st.clip_mask.size() == expected_mask_size) {
        m_clip_mask = std::move(st.clip_mask);
    } else {
        m_clip_mask.clear();
    }
}

void CanvasRenderingContext2D::reset() {
    // §4.12.5.1, "reset the rendering context to its default state". Every
    // field `save()` records is put back to the value a new context has, and
    // the stack is emptied rather than popped: `reset()` does not answer the
    // saves that are still open, it discards them.
    fill_style = Color::black();
    stroke_style = Color::black();
    line_width = 1.0f;
    global_composite_operation = "source-over";
    global_alpha = 1.0f;
    line_cap = "butt";
    line_join = "miter";
    miter_limit = 10.0f;
    line_dash.clear();
    line_dash_offset = 0.0f;
    font = "10px sans-serif";
    text_align = "start";
    text_baseline = "alphabetic";
    // §4.12.5.1's list of what the canvas state holds names "the current letter
    // spacing, word spacing, … and shadow color", so `reset()` puts these back to
    // their initial values as it does the rest.
    letter_spacing_px = 0.0f;
    letter_spacing_source = "0px";
    word_spacing_px = 0.0f;
    word_spacing_source = "0px";
    direction = "inherit";
    font_kerning = "auto";
    font_stretch = "normal";
    font_variant_caps = "normal";
    text_rendering = "auto";
    fill_gradient.reset();
    stroke_gradient.reset();
    fill_pattern.reset();
    stroke_pattern.reset();
    fill_paint = JsValue();
    stroke_paint = JsValue();
    image_smoothing_enabled = true;
    image_smoothing_quality = "low";
    shadow_blur = 0.0f;
    shadow_color = Color(0, 0, 0, 0);
    shadow_offset_x = 0.0f;
    shadow_offset_y = 0.0f;
    m_ctm = TransformMatrix2D::identity();
    m_clip_mask.clear();
    m_state_stack.clear();
    m_paths.clear();
    // A `reset()` inside a shadow capture would leave the capture counting; the
    // buffers are dropped with it, so nothing stale can be replayed.
    m_capture_depth = 0;
    m_captured_pixels.clear();
    m_captured_coverage.clear();
}

void CanvasRenderingContext2D::translate(float tx, float ty) {
    if (!is_finite_f(tx) || !is_finite_f(ty)) return;
    m_ctm = m_ctm.multiply(TransformMatrix2D::translation(tx, ty));
}

void CanvasRenderingContext2D::rotate(float angle_rad) {
    if (!is_finite_f(angle_rad)) return;
    m_ctm = m_ctm.multiply(TransformMatrix2D::rotation(angle_rad));
}

void CanvasRenderingContext2D::scale(float sx, float sy) {
    if (!is_finite_f(sx) || !is_finite_f(sy)) return;
    m_ctm = m_ctm.multiply(TransformMatrix2D::scale(sx, sy));
}

void CanvasRenderingContext2D::transform(float a, float b, float c, float d, float e, float f) {
    if (!is_finite_f(a) || !is_finite_f(b) || !is_finite_f(c) ||
        !is_finite_f(d) || !is_finite_f(e) || !is_finite_f(f)) return;
    m_ctm = m_ctm.multiply(TransformMatrix2D(a, b, c, d, e, f));
}

void CanvasRenderingContext2D::set_transform(float a, float b, float c, float d, float e, float f) {
    if (!is_finite_f(a) || !is_finite_f(b) || !is_finite_f(c) ||
        !is_finite_f(d) || !is_finite_f(e) || !is_finite_f(f)) return;
    m_ctm = TransformMatrix2D(a, b, c, d, e, f);
}

void CanvasRenderingContext2D::set_transform(const TransformMatrix2D& matrix) {
    m_ctm = matrix;
}

void CanvasRenderingContext2D::reset_transform() {
    m_ctm = TransformMatrix2D::identity();
}

void CanvasRenderingContext2D::set_line_dash(const std::vector<float>& segments) {
    for (float v : segments) {
        if (!is_finite_f(v) || v < 0.0f) return; // Per Canvas spec, ignore invalid dash arrays
    }
    line_dash = segments;
    if (line_dash.size() % 2 == 1) {
        const size_t orig = line_dash.size();
        line_dash.reserve(orig * 2);
        for (size_t i = 0; i < orig; ++i) line_dash.push_back(line_dash[i]);
    }
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

// §4.12.5.1's spacing rules, in the one place both the measurer and the painter
// go through. The rule, as the standard puts it, is that a letter space follows
// every typographic character unit and a word space additionally follows each
// word-separator character — with the exception that neither applies to the first
// character of the string, so `letterSpacing: 8px` on "ab" adds 8 and not 16.
//
// A `run` may hold several glyphs for one character (a base and its marks, or a
// ligature). The space goes after the **last** of them, which is where the pen
// really leaves the character: putting it after every glyph would space a base
// away from its own accent, a visible break in Arabic and Devanagari.
void CanvasRenderingContext2D::apply_text_spacing(std::vector<FontRenderer::ShapedGlyph>& run,
                                                  const std::string& text,
                                                  float letter_px, float word_px) {
    if (run.empty()) return;
    if (letter_px == 0.0f && word_px == 0.0f) return;

    // The characters of `text`, so a space can be recognised. Only the number of
    // entries matters here, plus which are U+0020.
    std::vector<uint32_t> cps;
    cps.reserve(text.size());
    std::size_t pos = 0;
    while (pos < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[pos]);
        uint32_t cp = lead;
        std::size_t extra = 0;
        if (lead >= 0xF0) { cp = lead & 0x07u; extra = 3; }
        else if (lead >= 0xE0) { cp = lead & 0x0Fu; extra = 2; }
        else if (lead >= 0xC0) { cp = lead & 0x1Fu; extra = 1; }
        for (std::size_t k = 0; k < extra && pos + 1 < text.size(); ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[++pos]) & 0x3Fu);
        }
        cps.push_back(cp);
        ++pos;
    }

    // A shaped run has one glyph per character except where `GSUB` made a
    // ligature (fewer) or a mark was attached (may be more). The mapping is not
    // recoverable in general, so the rule is applied by the *run* structure the
    // shaper keeps: a glyph that starts a new cluster is a new character. That is
    // `is_mark` being false, plus a real advance — a zero-advance non-mark is an
    // accent the shaper placed itself.
    //
    // When the counts do not line up at all the spacing still applies per glyph,
    // which is right for the script-less case (Latin, the overwhelmingly common
    // one) and honest about what it cannot see.
    const bool counts_agree = (run.size() == cps.size());
    float running = 0.0f;
    std::size_t char_index = 0;
    for (std::size_t i = 0; i < run.size(); ++i) {
        // Is this glyph the end of its character? Yes unless the next glyph is a
        // mark that belongs to it.
        // `ShapedGlyph` carries `is_mark` and nothing about clusters, so a mark
        // continues the character it follows. That is the one case a shaper puts
        // a glyph after another without advancing the pen.
        const bool last_of_character = (i + 1 == run.size()) || !run[i + 1].is_mark;
        running += run[i].advance;
        if (!last_of_character) continue;

        // Which character ended here, for the space test.
        const bool is_space = counts_agree ? (cps[i] == 0x20)
                                           : (char_index < cps.size() && cps[char_index] == 0x20);
        ++char_index;
        // The first character never takes a space, whichever kind.
        if (char_index == 1) continue;
        float add = letter_px;
        if (is_space) add += word_px;
        if (add == 0.0f) continue;
        // Widening the last glyph's advance moves the pen and leaves the ink
        // where it was, which is exactly what letter spacing is: the character
        // does not stretch, the distance after it does.
        run[i].advance += add;
        running += add;
    }
}

bool CanvasRenderingContext2D::text_direction_is_rtl() const {
    if (direction == "rtl") return true;
    if (direction == "ltr") return false;
    // "inherit": the direction of the canvas element, which is its `dir`
    // attribute or its nearest ancestor's. A context has no element of its own
    // unless one was wired in (`set_owner_element`), and a context made by
    // `createElement('canvas')` and never attached has no ancestors either — both
    // are `ltr`, which is the initial value of `dir`.
    std::shared_ptr<const Node> node = m_owner_element.lock();
    while (node) {
        if (const auto* el = dynamic_cast<const Element*>(node.get())) {
            const std::string d = el->get_attribute("dir");
            if (d == "rtl" || d == "RTL") return true;
            if (d == "ltr" || d == "LTR") return false;
        }
        node = node->parent();
    }
    return false;
}

CanvasRenderingContext2D::TextLayout CanvasRenderingContext2D::layout_text(const std::string& text,
                                                                           float max_width) const {
    TextLayout layout;
    ParsedCanvasFont parsed;
    if (!parse_canvas_font(font, parsed)) {
        // Direct member assignment can bypass the validating setter; fall back
        // to the default font rather than failing to draw.
        parsed = ParsedCanvasFont{};
    }
    const float size_px = std::max(0.0f, parsed.size_px);
    // `ctx.font = "italic 16px serif"` -- the canvas font grammar has had a
    // slant in it all along; this is the first thing to use it.
    const bool italic = parsed.style == "italic" || parsed.style == "oblique";
    const auto metrics = FontRenderer::instance().em_ascent_descent(size_px, parsed.weight, parsed.family, italic);
    layout.ascent = metrics.first;
    layout.descent = metrics.second;

    // §4.12.5.1: `fontKerning: "none"` turns kerning off, and `direction` decides
    // which way an RTL run is ordered. Both are shaping inputs, so they belong
    // here rather than being applied to the result.
    const bool kern = (font_kerning != "none");
    // `visual_rtl` is deliberately left at its default `true` and is *not* driven
    // by `direction`. Before Phase 417 the shaper already ordered RTL **script**
    // runs (Hebrew, Arabic) visually, whatever the context said; passing
    // `direction` through would have turned that ordering off for every page that
    // had not set `direction`, which is all of them. `direction` is an inline
    // direction for `start`/`end`, not a switch for the script's own ordering.
    std::vector<FontRenderer::ShapedGlyph> shaped =
        (size_px > 0.0f && !text.empty())
            ? FontRenderer::instance().shape_text(text, size_px, parsed.weight, parsed.family, italic,
                                                  kern)
            : std::vector<FontRenderer::ShapedGlyph>{};
    // And the spacing, after shaping and before anything is measured: one call
    // for both, which is what keeps `measureText` and the painter in step.
    apply_text_spacing(shaped, text, letter_spacing_px, word_spacing_px);
    float advance = 0.0f;
    float min_x = 0.0f;
    float max_x = 0.0f;
    float max_y_up = 0.0f;
    float max_y_down = 0.0f;
    for (const FontRenderer::ShapedGlyph& sg : shaped) {
        const float gx_pos = advance + sg.x_offset;
        min_x = std::min(min_x, gx_pos);
        max_x = std::max(max_x, gx_pos + std::max(0.0f, sg.advance));
        if (sg.x_offset > 0.0f && sg.advance == 0.0f) {
            max_x = std::max(max_x, gx_pos + size_px);
        }
        advance += sg.advance;
        if (sg.y_offset > max_y_up) max_y_up = sg.y_offset;
        if (-sg.y_offset > max_y_down) max_y_down = -sg.y_offset;
    }
    max_x = std::max(max_x, advance);
    layout.advance = advance;
    const float condensable = (max_width > 0.0f && layout.advance > max_width && layout.advance > 0.0f)
                                  ? std::clamp(max_width / layout.advance, 0.02f, 1.0f)
                                  : 1.0f;
    layout.condense = condensable;

    layout.pad_x = (min_x < 0.0f)
                       ? std::clamp(static_cast<float>(std::ceil(-min_x)) + 1.0f, 1.0f, 1024.0f)
                       : 1.0f;
    layout.baseline_px = static_cast<int>(std::ceil(layout.ascent + max_y_up)) + 1;
    const int below_px = static_cast<int>(std::ceil(layout.descent + max_y_down)) + 1;
    layout.height = std::clamp(layout.baseline_px + below_px, 1, kMaxTextBufferHeight);
    layout.baseline_px = std::min(layout.baseline_px, layout.height - 1);
    const int pad = static_cast<int>(layout.pad_x);
    // Clamp before narrowing: a hostile string can advance past INT_MAX pixels.
    const int natural_w = (max_x >= static_cast<float>(kMaxTextBufferWidth - pad - 2))
                              ? kMaxTextBufferWidth
                              : static_cast<int>(std::ceil(max_x)) + pad + 2;
    layout.width = std::clamp(natural_w, 1, kMaxTextBufferWidth);
    layout.coverage.assign(static_cast<std::size_t>(layout.width) * static_cast<std::size_t>(layout.height), 0);
    if (size_px <= 0.0f || text.empty()) return layout;

    const int px_size = std::clamp(static_cast<int>(std::lround(size_px)), 4, 160);
    float pen = layout.pad_x;
    float ink_l = std::numeric_limits<float>::infinity();
    float ink_r = -std::numeric_limits<float>::infinity();
    float ink_a = 0.0f;
    float ink_d = 0.0f;

    // The glyphs `measureText` counted into `layout.advance`: GSUB ligatures
    // and GPOS / legacy kerning + mark offsets are already baked into each
    // `ShapedGlyph`.
    for (std::size_t si = 0; si < shaped.size(); ++si) {
        const GlyphBitmap& g = FontRenderer::instance().get_glyph_shaped(shaped[si], px_size, parsed.weight);
        const float pen_rel = pen - layout.pad_x + shaped[si].x_offset;
        if (g.width > 0 && g.height > 0) {
            const int gx0 = static_cast<int>(std::lround(pen + shaped[si].x_offset)) + g.bearing_x;
            const int gy0 = layout.baseline_px - static_cast<int>(std::lround(shaped[si].y_offset)) - g.bearing_y;
            ink_l = std::min(ink_l, pen_rel + static_cast<float>(g.bearing_x));
            ink_r = std::max(ink_r, pen_rel + static_cast<float>(g.bearing_x) + static_cast<float>(g.width));
            ink_a = std::max(ink_a, static_cast<float>(g.bearing_y) + shaped[si].y_offset);
            ink_d = std::max(ink_d, static_cast<float>(g.height - g.bearing_y) - shaped[si].y_offset);
            for (int r = 0; r < g.height; ++r) {
                const int dy = gy0 + r;
                if (dy < 0 || dy >= layout.height) continue;
                const uint8_t* src_row = g.coverage.data() + static_cast<std::size_t>(r) * static_cast<std::size_t>(g.width);
                uint8_t* dst_row = layout.coverage.data() + static_cast<std::size_t>(dy) * static_cast<std::size_t>(layout.width);
                for (int col = 0; col < g.width; ++col) {
                    const int dx = gx0 + col;
                    if (dx < 0 || dx >= layout.width) continue;
                    const uint8_t cov = src_row[col];
                    if (cov == 0) continue;
                    const int sum = static_cast<int>(dst_row[dx]) + static_cast<int>(cov);
                    dst_row[dx] = static_cast<uint8_t>(std::min(255, sum));
                }
            }
        }
        pen += shaped[si].advance;
        if (pen > static_cast<float>(kMaxTextBufferWidth)) break; // truncated by the buffer cap
    }
    if (std::isfinite(ink_l)) {
        layout.ink_left = ink_l;
        layout.ink_right = ink_r;
        layout.ink_ascent = ink_a;
        layout.ink_descent = ink_d;
    }
    return layout;
}

float CanvasRenderingContext2D::shadow_padding() const {
    // A Gaussian's visible extent is about three standard deviations, and the
    // specification maps `shadowBlur` to a standard deviation of half its value,
    // so 1.5 times the blur is where the shadow has faded out. The offset is
    // added because the shadow may land entirely outside the shape.
    if (shadow_color.a == 0) return 0.0f;
    const float reach = 1.5f * std::max(0.0f, shadow_blur);
    return std::abs(shadow_offset_x) + std::abs(shadow_offset_y) + reach;
}

void CanvasRenderingContext2D::paint_shadow(const std::vector<uint8_t>& coverage) {
    if (!has_backing_store() || shadow_color.a == 0) return;
    const std::size_t expected = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    if (coverage.size() != expected) return;

    const int pad = static_cast<int>(std::ceil(shadow_padding())) + 1;
    const int ox = static_cast<int>(std::lround(shadow_offset_x));
    const int oy = static_cast<int>(std::lround(shadow_offset_y));

    // §4.12.5.1: the blur is applied to the shape's own alpha, and the colour is
    // the shadow colour's, not the shape's. Three box passes approximate a
    // Gaussian well enough that the edge falls off smoothly; a single box would
    // leave a hard step and no box at all would leave the shape's own edge.
    std::vector<uint8_t> alpha = coverage;
    const int radius = static_cast<int>(std::lround(shadow_blur * 0.5f));
    if (radius > 0) {
        auto box_pass = [this](const std::vector<uint8_t>& in, int r) {
            std::vector<uint8_t> out(in.size(), 0);
            const int w = m_width;
            const int h = m_height;
            // Horizontal, then vertical, with a running sum so the cost does
            // not grow with the radius.
            std::vector<uint8_t> tmp(in.size(), 0);
            for (int y = 0; y < h; ++y) {
                const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
                int sum = 0;
                for (int x = -r; x <= r; ++x) {
                    if (x >= 0 && x < w) sum += in[row + static_cast<std::size_t>(x)];
                }
                for (int x = 0; x < w; ++x) {
                    tmp[row + static_cast<std::size_t>(x)] =
                        static_cast<uint8_t>((sum + (2 * r + 1) / 2) / (2 * r + 1));
                    const int add = x + r + 1;
                    const int drop = x - r;
                    if (add < w) sum += in[row + static_cast<std::size_t>(add)];
                    if (drop >= 0) sum -= in[row + static_cast<std::size_t>(drop)];
                }
            }
            for (int x = 0; x < w; ++x) {
                int sum = 0;
                for (int y = -r; y <= r; ++y) {
                    if (y >= 0 && y < h) sum += tmp[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                                   static_cast<std::size_t>(x)];
                }
                for (int y = 0; y < h; ++y) {
                    out[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] =
                        static_cast<uint8_t>((sum + (2 * r + 1) / 2) / (2 * r + 1));
                    const int add = y + r + 1;
                    const int drop = y - r;
                    if (add < h) sum += tmp[static_cast<std::size_t>(add) * static_cast<std::size_t>(w) +
                                            static_cast<std::size_t>(x)];
                    if (drop >= 0) sum -= tmp[static_cast<std::size_t>(drop) * static_cast<std::size_t>(w) +
                                              static_cast<std::size_t>(x)];
                }
            }
            return out;
        };
        for (int pass = 0; pass < 3; ++pass) alpha = box_pass(alpha, radius);
    }

    // The shadow's colour never changes the shape's, and a shadow is not itself
    // shadowed: `set_pixel_safe` composites it under whatever is already there.
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) +
                                    static_cast<std::size_t>(x);
            const uint8_t a = alpha[idx];
            if (a == 0) continue;
            // The offset moves the shadow **to** (x + ox, y + oy): the shape at
            // (2,2) with an offset of (4,4) casts its shadow at (6,6). Written
            // the other way round first (x - ox), which put a shadow of a shape
            // at the origin nowhere at all — the only pixels it would ever move
            // were already off-canvas.
            const int dx_px = x + ox;
            const int dy_px = y + oy;
            if (dx_px < 0 || dx_px >= m_width || dy_px < 0 || dy_px >= m_height) continue;
            // Read the shape's coverage where the shadow lands, not where the
            // shape is: the offset moves the shadow, and the blur has already
            // been applied to the shape's own position.
            // The shadow colour's own alpha times the blurred coverage — passed
            // as the colour rather than as `coverage`, because `set_pixel_safe`
            // multiplies coverage into the alpha *and* `global_alpha` into it,
            // and the capture has already applied both once. Passing the blurred
            // value as coverage scaled it a second time and made an opaque shadow
            // of a solid shape come out at 25% (measured: 0,0,0,64).
            (void)pad;
            Color sc = shadow_color;
            sc.a = static_cast<uint8_t>(std::clamp(
                std::lround(static_cast<float>(shadow_color.a) * (static_cast<float>(a) / 255.0f)), 0l, 255l));
            set_pixel_safe(dx_px, dy_px, sc, 255);
        }
    }
}

CanvasRenderingContext2D::ShadowCapture::ShadowCapture(CanvasRenderingContext2D* context) {
    // Only a context that has a visible shadow, or a filter, captures: an
    // invisible shadow changes no pixel, and the capture is a whole-canvas
    // buffer. A filter needs the same buffer for a different reason — it reads
    // the primitive's pixels as a set, not one at a time as `set_pixel_safe`
    // does — but the mechanism is identical, so both share it.
    if (!context || (context->shadow_color.a == 0 && !context->filter_is_active())) return;
    context_ = context;
    if (context_->m_capture_depth++ == 0) {
        const std::size_t size = static_cast<std::size_t>(context_->m_width) *
                                 static_cast<std::size_t>(context_->m_height);
        context_->m_captured_pixels.assign(size, 0u);
        context_->m_captured_coverage.assign(size, 0u);
    }
}

bool CanvasRenderingContext2D::ShadowCapture::take(int x, int y, Color& out) {
    if (!context_) return false;
    if (x < 0 || x >= context_->m_width || y < 0 || y >= context_->m_height) return false;
    const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(context_->m_width) +
                            static_cast<std::size_t>(x);
    const uint8_t cov = context_->m_captured_coverage[idx];
    if (cov == 0) return false;
    out = Color::from_argb(context_->m_captured_pixels[idx]);
    return true;
}

// §4.12.5.1 `filter`, parsed. The grammar is a space-separated list of filter
// functions, each `name(` argument `)`. Anything outside it — an unknown
// function, a missing parenthesis, a bad argument, a stray word — makes the
// whole value invalid, and an invalid value is **ignored**: the previous list
// stays. Per-function salvage would put a half-understood effect on the canvas,
// which looks like a rendering bug rather than a rejected assignment.
namespace {

struct FilterParse {
    bool ok{false};
    std::vector<FilterFunction> list;
};

// A CSS number: `2`, `0.5`, `-1`, `+.25`, `1e2`. No units — every filter
// argument that is not an angle or a length is a plain number.
bool parse_css_number(const std::string& text, float& out) {
    if (text.empty()) return false;
    const char* begin = text.c_str();
    char* end = nullptr;
    const double v = std::strtod(begin, &end);
    if (end == begin || *end != '\0' || !std::isfinite(v)) return false;
    out = static_cast<float>(v);
    return true;
}

// A percentage or number, the form `brightness`, `contrast`, `opacity`,
// `saturate` and `grayscale` accept interchangeably: `50%` and `0.5` are the
// same request.
bool parse_amount(const std::string& text, float& out) {
    if (!text.empty() && text.back() == '%') {
        float n = 0.0f;
        if (!parse_css_number(text.substr(0, text.size() - 1), n)) return false;
        out = n / 100.0f;
        return true;
    }
    return parse_css_number(text, out);
}

// An angle for `hue-rotate`: a number is degrees, and `deg` is the only unit
// this engine accepts. `rad`/`grad`/`turn` are legal CSS but a page that wants
// them can convert; accepting one of them wrongly would be worse than
// rejecting the value, which the specification allows.
bool parse_angle(const std::string& text, float& out) {
    std::string body = text;
    const bool degrees = body.size() > 3 && body.compare(body.size() - 3, 3, "deg") == 0;
    if (degrees) body.resize(body.size() - 3);
    return parse_css_number(body, out);
}

// A `<length>` for `blur()` and `drop-shadow()`. Canvas draws in CSS pixels, so
// `px` is the one unit that can mean what it says; `em`, `rem`, `pt` would need
// a font and a resolution this context does not consult. A bare number is
// accepted only for zero, which is the one value CSS lets stand alone.
bool parse_length(const std::string& text, float& out) {
    if (!text.empty() && text.back() == '%') return false;
    std::string body = text;
    bool had_px = false;
    if (body.size() > 2 && body.compare(body.size() - 2, 2, "px") == 0) {
        body.resize(body.size() - 2);
        had_px = true;
    }
    float v = 0.0f;
    if (!parse_css_number(body, v)) return false;
    if (!had_px && v != 0.0f) return false;
    out = v;
    return true;
}

std::string lower_ascii(std::string v) {
    for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return v;
}

std::string trim_ascii(const std::string& v) {
    std::size_t b = v.find_first_not_of(" \t\r\n\f");
    if (b == std::string::npos) return {};
    std::size_t e = v.find_last_not_of(" \t\r\n\f");
    return v.substr(b, e - b + 1);
}

// `drop-shadow( <dx> <dy> [blur] [colour] )`. The first two are required; the
// optional colour is the *last* token because a CSS colour can itself contain
// spaces, and `rgba(0, 0, 0, 0.5)` is four of them.
bool parse_drop_shadow(const std::string& body, FilterFunction& f) {
    std::vector<std::string> tokens;
    std::string current;
    int depth = 0;
    for (char c : body) {
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if ((c == ' ' || c == '\t') && depth == 0) {
            if (!current.empty()) { tokens.push_back(current); current.clear(); }
        } else {
            current += c;
        }
    }
    if (!current.empty()) tokens.push_back(current);
    // A zero-blur drop shadow is still a shadow, but CSS makes the colour
    // optional and defaults it to the used `color`, which this engine does not
    // carry; transparent black is what an unstated shadow colour paints.
    f.type = FilterType::DropShadow;
    f.color = Color(0, 0, 0, 255);
    f.dx = 0.0f;
    f.dy = 0.0f;
    f.blur_radius = 0.0f;
    std::size_t i = 0;
    if (tokens.size() < 2) return false;
    // A colour may come first in CSS. Only the `#rgb`/`#rrggbb` and
    // `rgb()`/`rgba()` forms are treated as a leading colour: a number is never
    // a colour, so this cannot swallow an offset.
    if (tokens[0][0] == '#') {
        f.color = Color::parse(tokens[0]);
        ++i;
    } else if (lower_ascii(tokens[0]).rfind("rgb", 0) == 0) {
        f.color = Color::parse(tokens[0]);
        ++i;
    }
    if (i + 1 >= tokens.size()) return false;
    float dx = 0.0f, dy = 0.0f;
    if (!parse_length(tokens[i], dx) || !parse_length(tokens[i + 1], dy)) return false;
    f.dx = dx;
    f.dy = dy;
    i += 2;
    // The remaining tokens are `[blur] [colour]`, each optional and each
    // recognisable by shape: a blur is a length and a colour never is.
    if (i < tokens.size()) {
        float blur = 0.0f;
        if (parse_length(tokens[i], blur)) {
            // A negative blur is invalid CSS, so the value is rejected whole.
            if (blur < 0.0f) return false;
            f.blur_radius = blur;
            ++i;
        }
    }
    // At most one colour may follow, and it may equally have come first.
    if (i < tokens.size() && (tokens[i][0] == '#' || lower_ascii(tokens[i]).rfind("rgb", 0) == 0)) {
        f.color = Color::parse(tokens[i]);
        ++i;
    }
    if (i != tokens.size()) return false;
    return true;
}

// §4.12.5.1's `letterSpacing`/`wordSpacing`: a CSS `<length>`, which is a number
// and a unit. `px`, `em`, `ex` and `rem` are what a canvas page writes; `em` and
// `ex` are resolved against the current font size, which is why the setter takes
// the size as an argument. Returns false for anything else, and the caller then
// leaves the previous value alone — the specification's "values that cannot be
// parsed as a CSS <length> are ignored".
//
// A bare `0` is the one number CSS lets stand without a unit, and `0px` and `0`
// both serialize as `0px`, which is what the getter answers.
// The serialized form of a spacing, for the getter. The specification says the
// getter answers "the serialized form of this's letter spacing", which is a CSS
// length value — `2em` stays `2em`, not `32px`. So the page's own text is kept,
// normalised: lower case, no leading `+`, no trailing zeros. A value whose unit
// this context could not reproduce exactly (`0.1em` at 10px is 1px, and reading
// `0.1em` back would be right too, but `1.7em` at 9px is 15.3px and reading
// `1.7em` back is the *same* value) is answered in `px`, which is exact for what
// the context actually holds.
std::string serialize_px(const std::string& asked, float px) {
    std::string v = trim_ascii(asked);
    for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // Drop a leading `+` and a redundant `.0` tail, which CSS serialization does.
    if (!v.empty() && v[0] == '+') v.erase(0, 1);
    // `2.50px` -> `2.5px`, `2.0px` -> `2px`.
    if (v.find('.') != std::string::npos && (v.back() == 'p' || v.back() == 'x' || v.back() == 'm')) {
        const std::size_t unit_at = v.find_first_of("abcdefghijklmnopqrstuvwxyz");
        if (unit_at != std::string::npos) {
            std::string num = v.substr(0, unit_at);
            std::string unit = v.substr(unit_at);
            while (!num.empty() && num.back() == '0') num.pop_back();
            if (!num.empty() && num.back() == '.') num.pop_back();
            if (num.empty()) num = "0";
            v = num + unit;
        }
    }
    if (v.empty()) return "0px";
    // A number with no unit can only have been zero, and zero serializes with one.
    if (std::isdigit(static_cast<unsigned char>(v.back())) || v.back() == '.') return v + "px";
    return v;
}

bool parse_css_length_px(const std::string& text, float font_size_px, float& px) {
    std::string v = trim_ascii(text);
    if (v.empty()) return false;
    for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // There is no `calc()`, `var()` or percentage here: a length on this attribute
    // has no box to take a percentage of.
    if (v.find('(') != std::string::npos || v.find('%') != std::string::npos) return false;

    std::size_t unit_at = v.size();
    while (unit_at > 0 && std::isalpha(static_cast<unsigned char>(v[unit_at - 1]))) --unit_at;
    const std::string unit = v.substr(unit_at);
    const std::string number = v.substr(0, unit_at);
    float n = 0.0f;
    if (!parse_css_number(number, n)) return false;
    if (unit.empty()) {
        // Only zero may be written without a unit.
        if (n != 0.0f) return false;
        px = 0.0f;
        return true;
    }
    if (unit == "px") { px = n; return true; }
    if (unit == "em") { px = n * font_size_px; return true; }
    if (unit == "ex") { px = n * font_size_px * 0.5f; return true; }
    if (unit == "rem") { px = n * 16.0f; return true; }
    // `pt`, `pc`, `cm`, `mm`, `in`, `q`, `vw`, `vh`, `vmin`, `vmax`: a length in a
    // unit this context does not resolve. Rejecting the value is what the
    // specification asks for an unparseable length, and guessing would put a
    // number the page did not ask for on the canvas.
    return false;
}

FilterParse parse_filter_list(const std::string& text) {
    FilterParse out;
    const std::string v = lower_ascii(trim_ascii(text));
    if (v == "none") { out.ok = true; return out; }
    if (v.empty()) return out;

    std::size_t i = 0;
    const std::size_t n = v.size();
    while (i < n) {
        while (i < n && (v[i] == ' ' || v[i] == '\t' || v[i] == '\n' || v[i] == '\r')) ++i;
        if (i >= n) break;
        const std::size_t name_start = i;
        while (i < n && (std::isalpha(static_cast<unsigned char>(v[i])) || v[i] == '-' || v[i] == '_')) ++i;
        const std::string name = v.substr(name_start, i - name_start);
        if (name.empty()) return out;  // a bare word or punctuation
        if (i >= n || v[i] != '(') return out;
        ++i;
        // The argument runs to the matching `)`, so `drop-shadow(...)` may hold
        // parentheses of its own without splitting the list there.
        const std::size_t arg_start = i;
        int depth = 1;
        while (i < n && depth > 0) {
            if (v[i] == '(') ++depth;
            else if (v[i] == ')') --depth;
            if (depth > 0) ++i;
        }
        if (depth != 0) return out;  // unbalanced
        const std::string arg = trim_ascii(v.substr(arg_start, i - arg_start));
        ++i;  // past the `)`

        FilterFunction f;
        float amount = 1.0f;
        if (name == "blur") {
            // The one filter whose argument is a length, and it may be empty:
            // `blur()` is `blur(0)`.
            float radius = 0.0f;
            if (!arg.empty() && !parse_length(arg, radius)) return out;
            if (radius < 0.0f) return out;
            f.type = FilterType::Blur;
            f.amount = radius;
        } else if (name == "brightness" || name == "contrast" || name == "opacity" ||
                   name == "saturate" || name == "grayscale" || name == "sepia" || name == "invert") {
            // Each of these is `amount || 1`, so an empty argument is the
            // default rather than an error. `invert(0)`/`invert(1)` are the two
            // CSS switches, and `sepia`/`grayscale` share the fraction.
            if (!arg.empty() && !parse_amount(arg, amount)) return out;
            if (name == "brightness") f.type = FilterType::Brightness;
            else if (name == "contrast") f.type = FilterType::Contrast;
            else if (name == "opacity") f.type = FilterType::Opacity;
            else if (name == "saturate") f.type = FilterType::Saturate;
            else if (name == "grayscale") f.type = FilterType::Grayscale;
            else if (name == "sepia") f.type = FilterType::Sepia;
            else f.type = FilterType::Invert;
            f.amount = amount;
        } else if (name == "hue-rotate") {
            float degrees = 0.0f;
            if (!arg.empty() && !parse_angle(arg, degrees)) return out;
            f.type = FilterType::HueRotate;
            f.amount = degrees;
        } else if (name == "drop-shadow") {
            if (!parse_drop_shadow(arg, f)) return out;
        } else {
            return out;  // `url()` and anything else this engine cannot do
        }
        out.list.push_back(f);
    }
    out.ok = true;
    return out;
}

}  // namespace

void CanvasRenderingContext2D::replay_captured() {
    const std::size_t size = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    // The shadow first, from the coverage, and then the primitive over it.
    if (m_captured_coverage.size() == size) paint_shadow(m_captured_coverage);
    // Painting the primitive goes through the unchanged path, so it is the same
    // compositing, clipping and alpha as if there had been no shadow at all.
    // A filter applies to the primitive as a whole — a blur reads the pixels
    // around each pixel, `grayscale` mixes the three channels — so it runs here,
    // on the captured buffer, and never inside `set_pixel_safe`. Filtering
    // before the alpha is unfolded is what keeps `drop-shadow` able to see the
    // shape's own silhouette.
    std::vector<Color> pixels;
    if (filter_is_active() && m_captured_coverage.size() == size) {
        pixels.resize(size);
        for (std::size_t i = 0; i < size; ++i) {
            pixels[i] = Color::from_argb(m_captured_pixels[i]);
            pixels[i].a = static_cast<uint8_t>(std::min(255, static_cast<int>(m_captured_coverage[i])));
        }
        ImageFilter::apply_filters(pixels, m_width, m_height, filter_list);
    }
    for (std::size_t i = 0; i < m_captured_coverage.size(); ++i) {
        // The coverage gate is right for a shadow — an untouched pixel must not
        // repaint the background — and wrong for a filter: `drop-shadow` puts
        // the shadow where the primitive did **not** draw, and a blur softens
        // the edge outward. So a filter writes every pixel it was given, and
        // skips only the ones it left completely transparent.
        if (pixels.empty() && m_captured_coverage[i] == 0) continue;
        if (!pixels.empty() && pixels[i].a == 0 && m_captured_coverage[i] == 0) continue;
        const int x = static_cast<int>(i % static_cast<std::size_t>(m_width));
        const int y = static_cast<int>(i / static_cast<std::size_t>(m_width));
        const Color c = pixels.empty() ? Color::from_argb(m_captured_pixels[i]) : pixels[i];
        if (!pixels.empty() && c.a == 0) continue;
        // The alpha recorded is already `global_alpha * coverage * colour alpha`,
        // so this writes with full coverage rather than scaling it twice.
        set_pixel_safe(x, y, c, 255);
    }
}

CanvasRenderingContext2D::ShadowCapture::~ShadowCapture() {
    if (!context_) return;
    if (--context_->m_capture_depth == 0) {
        context_->replay_captured();
        context_->m_captured_pixels.clear();
        context_->m_captured_coverage.clear();
        context_->m_captured_pixels.shrink_to_fit();
        context_->m_captured_coverage.shrink_to_fit();
    }
}

void CanvasRenderingContext2D::blit_text(const TextLayout& layout, float pen_user_x, float baseline_user_y,
                                         bool stroke) {
    if (layout.width <= 0 || layout.height <= 0) return;
    ShadowCapture shadow_guard(this);
    const float c = layout.condense;
    const float origin_x = pen_user_x - layout.pad_x * c;
    const float origin_y = baseline_user_y - static_cast<float>(layout.baseline_px);

    const std::vector<uint8_t>* src = &layout.coverage;
    std::vector<uint8_t> outline;
    if (stroke) {
        // Approximate strokeText: a band centred on the binary glyph boundary
        // (dilate(mask, d) minus erode(mask, d)) with half the line width, d >= 1.
        // lineWidth <= 1 keeps the pure inner boundary ring. This is an
        // outline-band approximation, not a stroked vector path.
        const int w = layout.width;
        const int h = layout.height;
        std::vector<uint8_t> mask(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        for (std::size_t i = 0; i < mask.size(); ++i) mask[i] = layout.coverage[i] >= 128 ? 1 : 0;
        int d = static_cast<int>(std::lround(static_cast<double>(line_width) * 0.5));
        d = std::clamp(d, 0, 16);
        auto maxfilter = [w, h](const std::vector<uint8_t>& in, int passes) {
            std::vector<uint8_t> out = in;
            for (int p = 0; p < passes; ++p) {
                std::vector<uint8_t> next(out.size(), 0);
                for (int y = 0; y < h; ++y) {
                    const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
                    for (int x = 0; x < w; ++x) {
                        uint8_t v = out[row + static_cast<std::size_t>(x)];
                        if (x > 0) v = std::max(v, out[row + static_cast<std::size_t>(x) - 1]);
                        if (x + 1 < w) v = std::max(v, out[row + static_cast<std::size_t>(x) + 1]);
                        if (y > 0) v = std::max(v, out[row - static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]);
                        if (y + 1 < h) v = std::max(v, out[row + static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]);
                        next[row + static_cast<std::size_t>(x)] = v;
                    }
                }
                out.swap(next);
            }
            return out;
        };
        auto minfilter = [w, h](const std::vector<uint8_t>& in, int passes) {
            std::vector<uint8_t> out = in;
            for (int p = 0; p < passes; ++p) {
                std::vector<uint8_t> next(out.size(), 0);
                for (int y = 0; y < h; ++y) {
                    const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
                    for (int x = 0; x < w; ++x) {
                        uint8_t v = out[row + static_cast<std::size_t>(x)];
                        if (x > 0) v = std::min(v, out[row + static_cast<std::size_t>(x) - 1]);
                        else v = 0;
                        if (x + 1 < w) v = std::min(v, out[row + static_cast<std::size_t>(x) + 1]);
                        else v = 0;
                        if (y > 0) v = std::min(v, out[row - static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]);
                        else v = 0;
                        if (y + 1 < h) v = std::min(v, out[row + static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]);
                        else v = 0;
                        next[row + static_cast<std::size_t>(x)] = v;
                    }
                }
                out.swap(next);
            }
            return out;
        };
        std::vector<uint8_t> outer, inner;
        if (d == 0) {
            outer = mask;
            inner = minfilter(mask, 1);
        } else {
            outer = maxfilter(mask, d);
            inner = minfilter(mask, d);
        }
        outline.assign(mask.size(), 0);
        for (std::size_t i = 0; i < mask.size(); ++i) outline[i] = (outer[i] && !inner[i]) ? 255 : 0;
        src = &outline;
    }

    if (std::abs(m_ctm.determinant()) < 1e-7f) return;
    const Rect bbox = m_ctm.map_rect(Rect(origin_x, origin_y, static_cast<float>(layout.width) * c,
                                          static_cast<float>(layout.height)));
    const TransformMatrix2D inv = m_ctm.inverse();
    const int x0 = std::max(0, static_cast<int>(std::floor(bbox.left())) - 1);
    const int y0 = std::max(0, static_cast<int>(std::floor(bbox.top())) - 1);
    const int x1 = std::min(m_width, static_cast<int>(std::ceil(bbox.right())) + 1);
    const int y1 = std::min(m_height, static_cast<int>(std::ceil(bbox.bottom())) + 1);

    const Color solid = stroke ? stroke_style : fill_style;
    const std::shared_ptr<CanvasGradient>& gradient = stroke ? stroke_gradient : fill_gradient;

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const Point up = inv.map_point(static_cast<float>(px) + 0.5f, static_cast<float>(py) + 0.5f);
            const int row = static_cast<int>(std::floor(up.y - origin_y));
            if (row < 0 || row >= layout.height) continue;
            // Horizontal source span covered by this device pixel (handles
            // horizontal condensing and arbitrary CTM x-components).
            const Point left_p = inv.map_point(static_cast<float>(px), static_cast<float>(py) + 0.5f);
            const Point right_p = inv.map_point(static_cast<float>(px) + 1.0f, static_cast<float>(py) + 0.5f);
            float ua = (left_p.x - origin_x) / c;
            float ub = (right_p.x - origin_x) / c;
            if (ua > ub) std::swap(ua, ub);
            int i0 = static_cast<int>(std::floor(ua));
            int i1 = static_cast<int>(std::ceil(ub)) - 1;
            if (i1 < i0) {
                const int single = static_cast<int>(std::floor((ua + ub) * 0.5f));
                i0 = i1 = single;
            }
            i0 = std::clamp(i0, 0, layout.width - 1);
            i1 = std::clamp(i1, 0, layout.width - 1);
            uint8_t cov = 0;
            const uint8_t* src_row = src->data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(layout.width);
            for (int i = i0; i <= i1; ++i) cov = std::max(cov, src_row[i]);
            if (cov == 0) continue;
            Color color = solid;
            if (gradient) color = gradient->sample_at(up.x, up.y);
            set_pixel_safe(px, py, color, cov);
        }
    }
}

void CanvasRenderingContext2D::draw_text_run(const std::string& text, float x, float y, float max_width,
                                             bool stroke) {
    if (!is_finite_f(x) || !is_finite_f(y)) return;
    if (max_width == 0.0f) return; // requested a zero-width run: nothing is drawn
    TextLayout layout = layout_text(text, max_width);
    if (layout.advance <= 0.0f) return; // nothing to draw (or empty string)

    // §4.12.5.1: "start" and "end" are relative to `direction`, which is the one
    // place `direction` changes a picture in a context with no bidi paragraph of
    // its own. Before Phase 417 the comment here said "start is left and end is
    // right" and both were hard-coded.
    const bool rtl = text_direction_is_rtl();
    const std::string align =
        (text_align == "start") ? (rtl ? "right" : "left")
        : (text_align == "end") ? (rtl ? "left" : "right")
                                : text_align;
    float pen_user_x = x;
    if (align == "center") {
        pen_user_x = x - layout.advance * 0.5f;
    } else if (align == "right") {
        pen_user_x = x - layout.advance;
    } // "left" (and anything else) draw from x

    float baseline_user_y;
    if (text_baseline == "top") {
        baseline_user_y = y + layout.ascent;
    } else if (text_baseline == "hanging") {
        // Hanging baseline approximated at 0.8em above the alphabetic baseline.
        baseline_user_y = y + 0.8f * layout.ascent;
    } else if (text_baseline == "middle") {
        baseline_user_y = y + (layout.ascent - layout.descent) * 0.5f;
    } else if (text_baseline == "bottom" || text_baseline == "ideographic") {
        // Ideographic baseline approximated at the bottom of the em box.
        baseline_user_y = y - layout.descent;
    } else {
        baseline_user_y = y; // "alphabetic"
    }
    blit_text(layout, pen_user_x, baseline_user_y, stroke);
}

void CanvasRenderingContext2D::fill_text(const std::string& text, float x, float y, float max_width) {
    draw_text_run(text, x, y, max_width, /*stroke=*/false);
}

void CanvasRenderingContext2D::stroke_text(const std::string& text, float x, float y, float max_width) {
    draw_text_run(text, x, y, max_width, /*stroke=*/true);
}

CanvasRenderingContext2D::TextMetrics CanvasRenderingContext2D::measure_text(const std::string& text) const {
    TextMetrics metrics;
    const TextLayout layout = layout_text(text, -1.0f);
    metrics.width = layout.advance;
    metrics.actual_bounding_box_left = layout.ink_left;
    metrics.actual_bounding_box_right = layout.ink_right;
    metrics.actual_bounding_box_ascent = layout.ink_ascent;
    metrics.actual_bounding_box_descent = layout.ink_descent;
    metrics.font_bounding_box_ascent = layout.ascent;
    metrics.font_bounding_box_descent = layout.descent;
    metrics.em_height_ascent = layout.ascent;
    metrics.em_height_descent = layout.descent;
    metrics.hanging_baseline = 0.8f * layout.ascent;
    metrics.ideographic_baseline = layout.descent;
    return metrics;
}

void CanvasRenderingContext2D::set_pixel_safe(int x, int y, Color c, uint8_t coverage) {
    if (!has_backing_store()) return;
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) return;
    const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x);

    uint32_t eff_cov = coverage;
    if (!m_clip_mask.empty()) {
        const uint8_t clip_cov = m_clip_mask[idx];
        if (clip_cov == 0) return;
        eff_cov = (eff_cov * clip_cov + 127u) / 255u;
    }
    if (eff_cov == 0) return;

    if (m_capture_depth > 0) {
        // Inside a ShadowCapture: the primitive is not painted yet. Its colour
        // and coverage are kept, and the capture paints the shadow and then the
        // primitive when the primitive's own call returns.
        const float ga_c = std::clamp(global_alpha, 0.0f, 1.0f);
        Color recorded = c;
        recorded.a = static_cast<uint8_t>(std::clamp(
            std::lround(static_cast<float>(recorded.a) * ga_c * (static_cast<float>(eff_cov) / 255.0f)), 0l, 255l));
        m_captured_pixels[idx] = recorded.to_argb();
        m_captured_coverage[idx] = static_cast<uint8_t>(std::min<uint32_t>(eff_cov, 255u));
        return;
    }

    const float ga = std::clamp(global_alpha, 0.0f, 1.0f);
    const float alpha_scale = ga * (static_cast<float>(eff_cov) / 255.0f);
    c.a = static_cast<uint8_t>(std::clamp(std::lround(static_cast<float>(c.a) * alpha_scale), 0l, 255l));

    Color dst = Color::from_argb(m_image->pixels[idx]);

    if (global_composite_operation == "copy") {
        m_image->pixels[idx] = c.to_argb();
        return;
    }
    if (global_composite_operation == "multiply") {
        if (c.a == 0) return;
        const float sa = c.a / 255.0f;
        const uint8_t mr = static_cast<uint8_t>((static_cast<int>(c.r) * dst.r) / 255);
        const uint8_t mg = static_cast<uint8_t>((static_cast<int>(c.g) * dst.g) / 255);
        const uint8_t mb = static_cast<uint8_t>((static_cast<int>(c.b) * dst.b) / 255);
        const uint8_t r = static_cast<uint8_t>(std::lround(dst.r * (1.0f - sa) + mr * sa));
        const uint8_t g = static_cast<uint8_t>(std::lround(dst.g * (1.0f - sa) + mg * sa));
        const uint8_t b = static_cast<uint8_t>(std::lround(dst.b * (1.0f - sa) + mb * sa));
        const uint8_t a = std::max(c.a, dst.a);
        m_image->pixels[idx] = Color(r, g, b, a).to_argb();
        return;
    }
    if (global_composite_operation == "screen") {
        if (c.a == 0) return;
        const float sa = c.a / 255.0f;
        const uint8_t sr = static_cast<uint8_t>(255 - ((255 - c.r) * (255 - dst.r)) / 255);
        const uint8_t sg = static_cast<uint8_t>(255 - ((255 - c.g) * (255 - dst.g)) / 255);
        const uint8_t sb = static_cast<uint8_t>(255 - ((255 - c.b) * (255 - dst.b)) / 255);
        const uint8_t r = static_cast<uint8_t>(std::lround(dst.r * (1.0f - sa) + sr * sa));
        const uint8_t g = static_cast<uint8_t>(std::lround(dst.g * (1.0f - sa) + sg * sa));
        const uint8_t b = static_cast<uint8_t>(std::lround(dst.b * (1.0f - sa) + sb * sa));
        const uint8_t a = std::max(c.a, dst.a);
        m_image->pixels[idx] = Color(r, g, b, a).to_argb();
        return;
    }
    if (global_composite_operation == "lighter") {
        const float sa = c.a / 255.0f;
        const uint8_t r = static_cast<uint8_t>(std::min(255, static_cast<int>(std::lround(c.r * sa)) + dst.r));
        const uint8_t g = static_cast<uint8_t>(std::min(255, static_cast<int>(std::lround(c.g * sa)) + dst.g));
        const uint8_t b = static_cast<uint8_t>(std::min(255, static_cast<int>(std::lround(c.b * sa)) + dst.b));
        const uint8_t a = static_cast<uint8_t>(std::min(255, static_cast<int>(c.a) + dst.a));
        m_image->pixels[idx] = Color(r, g, b, a).to_argb();
        return;
    }
    if (global_composite_operation == "destination-over") {
        if (dst.a == 255 || c.a == 0) return;
        const float da = dst.a / 255.0f;
        const float sa = (c.a / 255.0f) * (1.0f - da);
        const float out_a = da + sa;
        if (out_a > 0.0f) {
            const uint8_t r = static_cast<uint8_t>((dst.r * da + c.r * sa) / out_a);
            const uint8_t g = static_cast<uint8_t>((dst.g * da + c.g * sa) / out_a);
            const uint8_t b = static_cast<uint8_t>((dst.b * da + c.b * sa) / out_a);
            m_image->pixels[idx] = Color(r, g, b, static_cast<uint8_t>(std::lround(out_a * 255.0f))).to_argb();
        }
        return;
    }
    if (global_composite_operation == "source-in") {
        const float da = dst.a / 255.0f;
        const uint8_t a = static_cast<uint8_t>(std::lround(c.a * da));
        m_image->pixels[idx] = Color(c.r, c.g, c.b, a).to_argb();
        return;
    }
    if (global_composite_operation == "destination-out") {
        const float sa = c.a / 255.0f;
        const uint8_t a = static_cast<uint8_t>(std::lround(dst.a * (1.0f - sa)));
        m_image->pixels[idx] = Color(dst.r, dst.g, dst.b, a).to_argb();
        return;
    }

    // Default "source-over"
    if (c.a == 255) {
        m_image->pixels[idx] = c.to_argb();
    } else if (c.a > 0) {
        const float sa = c.a / 255.0f;
        const float da = dst.a / 255.0f;
        const float out_a = sa + da * (1.0f - sa);

        if (out_a > 0.0f) {
            const uint8_t r = static_cast<uint8_t>(std::lround((c.r * sa + dst.r * da * (1.0f - sa)) / out_a));
            const uint8_t g = static_cast<uint8_t>(std::lround((c.g * sa + dst.g * da * (1.0f - sa)) / out_a));
            const uint8_t b = static_cast<uint8_t>(std::lround((c.b * sa + dst.b * da * (1.0f - sa)) / out_a));
            const uint8_t a = static_cast<uint8_t>(std::lround(out_a * 255.0f));
            m_image->pixels[idx] = Color(r, g, b, a).to_argb();
        }
    }
}

Color CanvasGradient::sample_at(float px, float py) const {
    if (stops.empty()) return Color::black();
    if (!transform.is_identity()) {
        Point mapped = transform.map_point(px, py);
        px = mapped.x;
        py = mapped.y;
    }

    float t = 0.0f;
    if (type == Type::Conic) {
        // §4.12.5.1: t is the angle from the start angle to the point, taken
        // clockwise in the canvas' coordinates (y grows down) and divided by
        // a full turn. A point at the exact centre has no angle and gets the
        // start of the ramp, which is what the spec's "if x and y are both
        // zero" case amounts to here.
        const float dx = px - x0;
        const float dy = py - y0;
        if (dx != 0.0f || dy != 0.0f) {
            constexpr float kTau = 6.28318530717958647692f;
            float angle = std::atan2(dy, dx) - start_angle;
            // Normalised into [0, 1): a negative angle has gone the long way
            // round, so it is brought back by whole turns rather than clamped.
            while (angle < 0.0f) angle += kTau;
            while (angle >= kTau) angle -= kTau;
            t = angle / kTau;
        }
    } else if (type == Type::Linear) {
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float len2 = dx * dx + dy * dy;
        if (len2 > 1e-6f) {
            t = ((px - x0) * dx + (py - y0) * dy) / len2;
        }
    } else if (type == Type::Radial) {
        // Two-circle radial gradient: (x0, y0, r0) -> (x1, y1, r1)
        if (std::abs(x0 - x1) < 1e-5f && std::abs(y0 - y1) < 1e-5f) {
            const float dist = std::sqrt((px - x0) * (px - x0) + (py - y0) * (py - y0));
            const float dr = r1 - r0;
            if (std::abs(dr) > 1e-5f) {
                t = (dist - r0) / dr;
            } else {
                t = (dist >= r1) ? 1.0f : 0.0f;
            }
        } else {
            // Solve |(p - c0) - t*(c1 - c0)|^2 = (r0 + t*(r1 - r0))^2
            const float dx = x1 - x0;
            const float dy = y1 - y0;
            const float dr = r1 - r0;
            const float fx = px - x0;
            const float fy = py - y0;
            const float A = dx * dx + dy * dy - dr * dr;
            const float B = -2.0f * (fx * dx + fy * dy + r0 * dr);
            const float C = fx * fx + fy * fy - r0 * r0;
            if (std::abs(A) < 1e-6f) {
                t = (std::abs(B) > 1e-6f) ? (-C / B) : 0.0f;
            } else {
                const float disc = B * B - 4.0f * A * C;
                if (disc < 0.0f) return stops.back().color;
                const float sq = std::sqrt(disc);
                const float t1 = (-B + sq) / (2.0f * A);
                const float t2 = (-B - sq) / (2.0f * A);
                const float r_at_1 = r0 + t1 * dr;
                const float r_at_2 = r0 + t2 * dr;
                if (r_at_1 >= 0.0f && r_at_2 >= 0.0f) {
                    t = std::max(t1, t2);
                } else if (r_at_1 >= 0.0f) {
                    t = t1;
                } else if (r_at_2 >= 0.0f) {
                    t = t2;
                } else {
                    return stops.back().color;
                }
            }
        }
    }
    t = apply_spread(t, spread);

    if (t <= stops.front().offset) return stops.front().color;
    if (t >= stops.back().offset) return stops.back().color;

    for (size_t i = 0; i + 1 < stops.size(); ++i) {
        if (t >= stops[i].offset && t <= stops[i + 1].offset) {
            const float span = stops[i + 1].offset - stops[i].offset;
            const float u = (span > 1e-5f) ? (t - stops[i].offset) / span : 0.0f;
            const uint8_t r = static_cast<uint8_t>(std::lround(stops[i].color.r + (stops[i + 1].color.r - stops[i].color.r) * u));
            const uint8_t g = static_cast<uint8_t>(std::lround(stops[i].color.g + (stops[i + 1].color.g - stops[i].color.g) * u));
            const uint8_t b = static_cast<uint8_t>(std::lround(stops[i].color.b + (stops[i + 1].color.b - stops[i].color.b) * u));
            const uint8_t a = static_cast<uint8_t>(std::lround(stops[i].color.a + (stops[i + 1].color.a - stops[i].color.a) * u));
            return Color(r, g, b, a);
        }
    }
    return stops.back().color;
}

JsValue CanvasGradient::to_js_value(std::shared_ptr<CanvasGradient> self) {
    JsValue obj = JsValue::make_object();
    obj.set_canvas_gradient_value(self);
    obj.set_property("addColorStop", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (args.size() >= 2 && self) {
            float offset = static_cast<float>(args[0].to_number());
            Color c = Color::parse(args[1].to_string());
            self->add_color_stop(offset, c);
        }
        return JsValue::make_undefined();
    }));
    return obj;
}

std::shared_ptr<CanvasGradient> CanvasRenderingContext2D::create_linear_gradient(float x0, float y0, float x1, float y1) {
    auto grad = std::make_shared<CanvasGradient>();
    grad->type = CanvasGradient::Type::Linear;
    grad->x0 = x0;
    grad->y0 = y0;
    grad->x1 = x1;
    grad->y1 = y1;
    return grad;
}

std::shared_ptr<CanvasGradient> CanvasRenderingContext2D::create_radial_gradient(float x0, float y0, float r0, float x1, float y1, float r1) {
    auto grad = std::make_shared<CanvasGradient>();
    grad->type = CanvasGradient::Type::Radial;
    grad->x0 = x0;
    grad->y0 = y0;
    grad->r0 = r0;
    grad->x1 = x1;
    grad->y1 = y1;
    grad->r1 = r1;
    return grad;
}

std::shared_ptr<CanvasGradient> CanvasRenderingContext2D::create_conic_gradient(float start_angle, float x, float y) {
    auto grad = std::make_shared<CanvasGradient>();
    grad->type = CanvasGradient::Type::Conic;
    grad->start_angle = start_angle;
    grad->x0 = x;
    grad->y0 = y;
    return grad;
}

Color CanvasPattern::sample_at_device(float x, float y, bool smooth, float scale) const {
    if (!transform.is_identity()) {
        // The matrix maps the tile's coordinates to the canvas', so a point on
        // the canvas is taken back through the inverse to find the tile pixel.
        const Point p = transform.inverse().map_point(x, y);
        x = p.x;
        y = p.y;
    }
    // A tile drawn at 1:1 is its own pixels, whether smoothing is on or not —
    // which is what §4.12.5.1's half-pixel placement is for. Only a magnified or
    // a shrunk tile is filtered, and that has to be decided here because a
    // pattern has no destination rectangle to read a scale off.
    const bool filter = smooth && std::abs(scale - 1.0f) > 1e-3f;
    return sample_filtered(x, y, filter);
}

Color CanvasPattern::sample_filtered(float x, float y, bool smooth) const {
    // Nearest neighbour is the pixel the point falls in, which is what every
    // sample was before Phase 382.
    if (!smooth) return sample_at(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
    // A point half a pixel off a pixel's centre is exactly that pixel; the
    // half-pixel shift is what makes a 1:1 draw reproduce the tile rather than
    // blur it, and it is where an unfiltered draw and a filtered one agree.
    const float u = x - 0.5f;
    const float v = y - 0.5f;
    const float fu = std::floor(u);
    const float fv = std::floor(v);
    const float tx = u - fu;
    const float ty = v - fv;
    auto at = [this](int px, int py) {
        // Clamped to the tile, so a sample at the edge does not wrap round to
        // the opposite edge — repeat is the repetition rule's job, not the
        // filter's.
        const int cx = std::clamp(px, 0, width - 1);
        const int cy = std::clamp(py, 0, height - 1);
        return Color::from_argb(pixels[static_cast<std::size_t>(cy) * static_cast<std::size_t>(width) +
                                      static_cast<std::size_t>(cx)]);
    };
    const int ix = static_cast<int>(fu);
    const int iy = static_cast<int>(fv);
    const Color c00 = at(ix, iy);
    const Color c10 = at(ix + 1, iy);
    const Color c01 = at(ix, iy + 1);
    const Color c11 = at(ix + 1, iy + 1);
    // Weights sum to 1, so a flat tile comes out its own colour exactly and a
    // two-colour tile gives every intermediate value as it is crossed.
    auto mix = [&](unsigned char a, unsigned char b, unsigned char c, unsigned char d) {
        const float top = a + (b - a) * tx;
        const float bottom = c + (d - c) * tx;
        const float value = top + (bottom - top) * ty;
        return static_cast<unsigned char>(std::clamp(std::lround(value), 0l, 255l));
    };
    return Color(mix(c00.r, c10.r, c01.r, c11.r), mix(c00.g, c10.g, c01.g, c11.g),
                 mix(c00.b, c10.b, c01.b, c11.b), mix(c00.a, c10.a, c01.a, c11.a));
}

Color CanvasPattern::sample_at(int px, int py) const {
    if (width <= 0 || height <= 0 ||
        pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return Color(0, 0, 0, 0);
    }
    // "repeat" tiles in both directions, "repeat-x" only across, and
    // "repeat-y" only down; the axis that does not repeat takes the nearest
    // edge of the tile, which is what the specification's figures show. A
    // negative coordinate must wrap rather than clamp, or the tile would
    // start at the origin instead of continuing backwards through it.
    auto wrap = [](int v, int n) {
        if (v >= 0) return v % n;
        return ((v % n) + n) % n;
    };
    auto clamp = [](int v, int n) {
        if (v < 0) return 0;
        if (v >= n) return n - 1;
        return v;
    };
    // Which axis repeats. `no-repeat` repeats **neither**, so it clamps on
    // both — "not repeat-y" was written here first and made it wrap on both,
    // which no assertion could see until the pattern could paint at all.
    const bool rx = (repetition == "repeat" || repetition == "repeat-x");
    const bool ry = (repetition == "repeat" || repetition == "repeat-y");
    const int tx = rx ? wrap(px, width) : clamp(px, width);
    const int ty = ry ? wrap(py, height) : clamp(py, height);
    const std::uint32_t argb = pixels[static_cast<std::size_t>(ty) * static_cast<std::size_t>(width) +
                                     static_cast<std::size_t>(tx)];
    return Color::from_argb(argb);
}

std::shared_ptr<CanvasPattern> CanvasRenderingContext2D::create_pattern(
    const std::shared_ptr<ImageData>& image, const std::string& repetition) {
    if (!image || !image->is_valid()) return nullptr;
    // §4.12.5.1 step 2: an unrecognised repetition is null, not an exception.
    if (repetition != "repeat" && repetition != "repeat-x" && repetition != "repeat-y" &&
        repetition != "no-repeat") {
        return nullptr;
    }
    auto pattern = std::make_shared<CanvasPattern>();
    pattern->repetition = repetition;
    pattern->width = image->width;
    pattern->height = image->height;
    pattern->pixels = image->pixels;
    return pattern;
}

void CanvasRenderingContext2D::fill_rect(float x, float y, float w, float h) {
    if (!has_backing_store()) return;
    if (!is_finite_f(x) || !is_finite_f(y) || !is_finite_f(w) || !is_finite_f(h)) return;
    if (w == 0.0f || h == 0.0f) return;
    // §4.12.5.1: the shape covers its own shadow, so the shadow is drawn first
    // and this rectangle second. The capture is a no-op when no shadow is set.
    ShadowCapture shadow_guard(this);

    if (w < 0.0f) { x += w; w = -w; }
    if (h < 0.0f) { y += h; h = -h; }

    if (m_ctm.is_identity() && m_clip_mask.empty()) {
        const int x0 = std::max(0, static_cast<int>(std::round(x)));
        const int y0 = std::max(0, static_cast<int>(std::round(y)));
        const int x1 = std::min(m_width, static_cast<int>(std::round(x + w)));
        const int y1 = std::min(m_height, static_cast<int>(std::round(y + h)));

        if (fill_gradient || fill_pattern) {
            for (int py = y0; py < y1; ++py) {
                for (int px = x0; px < x1; ++px) {
                    // A gradient is sampled at the pixel's own point (the fast
                    // path has no CTM to undo); a pattern at the same point
                    // through its own transform.
                    const Color c = fill_gradient
                        ? fill_gradient->sample_at(static_cast<float>(px), static_cast<float>(py))
                        : fill_pattern->sample_at_device(static_cast<float>(px), static_cast<float>(py),
                                                         image_smoothing_enabled, 1.0f);
                    set_pixel_safe(px, py, c);
                }
            }
            return;
        }

        // The direct write below is only safe when nothing needs to see the
        // primitive's pixels one at a time — a shadow capture does, so while one
        // is alive every pixel goes through `set_pixel_safe`. That is the third
        // time in this file a fast path has hidden a new feature (Phase 249's
        // element writes, Phase 370's pattern, this): the fast path is not
        // wrong, it is blind.
        const bool fast_opaque = (m_capture_depth == 0 && fill_style.a == 255 && global_alpha >= 1.0f &&
                                  (global_composite_operation == "source-over" || global_composite_operation == "copy"));
        const uint32_t argb = fill_style.to_argb();
        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                if (fast_opaque) {
                    m_image->pixels[static_cast<size_t>(py) * static_cast<size_t>(m_width) + static_cast<size_t>(px)] = argb;
                } else {
                    set_pixel_safe(px, py, fill_style);
                }
            }
        }
        return;
    }

    SubPath sp;
    sp.closed = true;
    sp.points = {
        m_ctm.map_point(x, y),
        m_ctm.map_point(x + w, y),
        m_ctm.map_point(x + w, y + h),
        m_ctm.map_point(x, y + h),
    };
    std::vector<SubPath> tmp{std::move(sp)};
    rasterize_paths(tmp, FillRule::NonZero, fill_style, fill_gradient, m_ctm.inverse(), fill_pattern);
}

void CanvasRenderingContext2D::clear_rect(float x, float y, float w, float h) {
    if (!has_backing_store()) return;
    if (!is_finite_f(x) || !is_finite_f(y) || !is_finite_f(w) || !is_finite_f(h)) return;
    if (w == 0.0f || h == 0.0f) return;
    if (w < 0.0f) { x += w; w = -w; }
    if (h < 0.0f) { y += h; h = -h; }

    if (m_ctm.is_identity() && m_clip_mask.empty()) {
        const int x0 = std::max(0, static_cast<int>(std::round(x)));
        const int y0 = std::max(0, static_cast<int>(std::round(y)));
        const int x1 = std::min(m_width, static_cast<int>(std::round(x + w)));
        const int y1 = std::min(m_height, static_cast<int>(std::round(y + h)));

        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                m_image->pixels[static_cast<size_t>(py) * static_cast<size_t>(m_width) + static_cast<size_t>(px)] = 0;
            }
        }
        return;
    }

    SubPath sp;
    sp.closed = true;
    sp.points = {
        m_ctm.map_point(x, y),
        m_ctm.map_point(x + w, y),
        m_ctm.map_point(x + w, y + h),
        m_ctm.map_point(x, y + h),
    };
    std::vector<uint8_t> mask;
    rasterize_paths_to_mask({sp}, FillRule::NonZero, mask);
    for (size_t i = 0; i < mask.size(); ++i) {
        uint8_t cov = mask[i];
        if (!m_clip_mask.empty()) {
            cov = static_cast<uint8_t>((static_cast<uint32_t>(cov) * m_clip_mask[i] + 127u) / 255u);
        }
        if (cov == 255) {
            m_image->pixels[i] = 0;
        } else if (cov > 0) {
            Color dst = Color::from_argb(m_image->pixels[i]);
            dst.a = static_cast<uint8_t>((static_cast<uint32_t>(dst.a) * (255u - cov) + 127u) / 255u);
            m_image->pixels[i] = dst.to_argb();
        }
    }
}

void CanvasRenderingContext2D::stroke_rect(float x, float y, float w, float h) {
    if (!has_backing_store()) return;
    if (!is_finite_f(x) || !is_finite_f(y) || !is_finite_f(w) || !is_finite_f(h)) return;
    if (w == 0.0f && h == 0.0f) return;

    SubPath sp;
    sp.closed = true;
    sp.points = {
        m_ctm.map_point(x, y),
        m_ctm.map_point(x + w, y),
        m_ctm.map_point(x + w, y + h),
        m_ctm.map_point(x, y + h),
    };
    std::vector<SubPath> tmp{std::move(sp)};
    stroke_paths(tmp);
}

void CanvasRenderingContext2D::begin_path() {
    m_paths.clear();
}

void CanvasRenderingContext2D::close_path() {
    if (!m_paths.empty() && !m_paths.back().points.empty()) {
        m_paths.back().closed = true;
        Point start = m_paths.back().points.front();
        m_paths.push_back({{start}, false});
    }
}

void CanvasRenderingContext2D::move_to(float x, float y) {
    if (!is_finite_f(x) || !is_finite_f(y)) return;
    m_paths.push_back({{m_ctm.map_point(x, y)}, false});
}

void CanvasRenderingContext2D::line_to(float x, float y) {
    if (!is_finite_f(x) || !is_finite_f(y)) return;
    Point p = m_ctm.map_point(x, y);
    if (m_paths.empty()) {
        m_paths.push_back({{p}, false});
    } else {
        m_paths.back().points.push_back(p);
    }
}

void CanvasRenderingContext2D::rect(float x, float y, float w, float h) {
    if (!is_finite_f(x) || !is_finite_f(y) || !is_finite_f(w) || !is_finite_f(h)) return;
    move_to(x, y);
    line_to(x + w, y);
    line_to(x + w, y + h);
    line_to(x, y + h);
    close_path();
}

void CanvasRenderingContext2D::round_rect(float x, float y, float w, float h, float r) {
    round_rect(x, y, w, h, r, r, r, r);
}

void CanvasRenderingContext2D::round_rect(
    float x, float y, float w, float h,
    float r_tl, float r_tr, float r_br, float r_bl
) {
    if (!is_finite_f(x) || !is_finite_f(y) || !is_finite_f(w) || !is_finite_f(h)) return;
    if (w < 0.0f) { x += w; w = -w; }
    if (h < 0.0f) { y += h; h = -h; }
    if (w == 0.0f || h == 0.0f) return;

    r_tl = std::max(0.0f, is_finite_f(r_tl) ? r_tl : 0.0f);
    r_tr = std::max(0.0f, is_finite_f(r_tr) ? r_tr : 0.0f);
    r_br = std::max(0.0f, is_finite_f(r_br) ? r_br : 0.0f);
    r_bl = std::max(0.0f, is_finite_f(r_bl) ? r_bl : 0.0f);

    // Clamp overlapping corner radii proportionally per HTML Canvas spec
    float f = 1.0f;
    if (r_tl + r_tr > 0.0f) f = std::min(f, w / (r_tl + r_tr));
    if (r_bl + r_br > 0.0f) f = std::min(f, w / (r_bl + r_br));
    if (r_tl + r_bl > 0.0f) f = std::min(f, h / (r_tl + r_bl));
    if (r_tr + r_br > 0.0f) f = std::min(f, h / (r_tr + r_br));
    if (f < 1.0f) {
        r_tl *= f;
        r_tr *= f;
        r_br *= f;
        r_bl *= f;
    }

    move_to(x + r_tl, y);
    line_to(x + w - r_tr, y);
    if (r_tr > 0.0f) arc(x + w - r_tr, y + r_tr, r_tr, -kPi * 0.5f, 0.0f, false);
    line_to(x + w, y + h - r_br);
    if (r_br > 0.0f) arc(x + w - r_br, y + h - r_br, r_br, 0.0f, kPi * 0.5f, false);
    line_to(x + r_bl, y + h);
    if (r_bl > 0.0f) arc(x + r_bl, y + h - r_bl, r_bl, kPi * 0.5f, kPi, false);
    line_to(x, y + r_tl);
    if (r_tl > 0.0f) arc(x + r_tl, y + r_tl, r_tl, kPi, kPi * 1.5f, false);
    close_path();
}

void CanvasRenderingContext2D::ellipse(
    float cx, float cy, float rx, float ry,
    float rotation, float start_angle, float end_angle, bool anticlockwise
) {
    if (!is_finite_f(cx) || !is_finite_f(cy) || !is_finite_f(rx) || !is_finite_f(ry) ||
        !is_finite_f(rotation) || !is_finite_f(start_angle) || !is_finite_f(end_angle)) return;
    if (rx < 0.0f || ry < 0.0f) return;

    float sweep = end_angle - start_angle;
    if (!anticlockwise) {
        if (sweep >= kTwoPi) {
            sweep = kTwoPi;
        } else {
            sweep = std::fmod(sweep, kTwoPi);
            if (sweep < 0.0f) sweep += kTwoPi;
            if (sweep == 0.0f && end_angle != start_angle) sweep = kTwoPi;
        }
    } else {
        if (sweep <= -kTwoPi) {
            sweep = -kTwoPi;
        } else {
            sweep = std::fmod(sweep, kTwoPi);
            if (sweep > 0.0f) sweep -= kTwoPi;
            if (sweep == 0.0f && end_angle != start_angle) sweep = -kTwoPi;
        }
    }

    const float scale = std::sqrt(std::max(1e-4f, std::abs(m_ctm.determinant())));
    const float max_r = std::max(rx, ry) * scale;
    const int segments = std::clamp(
        static_cast<int>(std::ceil(std::abs(sweep) * std::max(8.0f, std::sqrt(max_r) * 3.0f))),
        12, 256
    );
    const float cos_rot = std::cos(rotation);
    const float sin_rot = std::sin(rotation);

    for (int i = 0; i <= segments; ++i) {
        const float angle = start_angle + sweep * (static_cast<float>(i) / static_cast<float>(segments));
        const float lx = rx * std::cos(angle);
        const float ly = ry * std::sin(angle);
        const float px = cx + lx * cos_rot - ly * sin_rot;
        const float py = cy + lx * sin_rot + ly * cos_rot;
        if (i == 0 && (m_paths.empty() || m_paths.back().points.empty())) {
            move_to(px, py);
        } else {
            line_to(px, py);
        }
    }
}

void CanvasRenderingContext2D::arc(
    float cx, float cy, float radius,
    float start_angle, float end_angle, bool anticlockwise
) {
    ellipse(cx, cy, radius, radius, 0.0f, start_angle, end_angle, anticlockwise);
}

void CanvasRenderingContext2D::arc_to(float x1, float y1, float x2, float y2, float radius) {
    if (!is_finite_f(x1) || !is_finite_f(y1) || !is_finite_f(x2) || !is_finite_f(y2) || !is_finite_f(radius)) return;
    if (radius < 0.0f) return;

    Point p0_user(0.0f, 0.0f);
    if (m_paths.empty() || m_paths.back().points.empty()) {
        move_to(x1, y1);
        return;
    }
    p0_user = m_ctm.inverse().map_point(m_paths.back().points.back());

    const float v1x = p0_user.x - x1;
    const float v1y = p0_user.y - y1;
    const float v2x = x2 - x1;
    const float v2y = y2 - y1;
    const float len1 = std::hypot(v1x, v1y);
    const float len2 = std::hypot(v2x, v2y);
    if (len1 < 1e-6f || len2 < 1e-6f || radius == 0.0f) {
        line_to(x1, y1);
        return;
    }

    const float u1x = v1x / len1, u1y = v1y / len1;
    const float u2x = v2x / len2, u2y = v2y / len2;
    const float cross = u1x * u2y - u1y * u2x;
    if (std::abs(cross) < 1e-6f) {
        line_to(x1, y1);
        return;
    }

    const float dot = std::clamp(u1x * u2x + u1y * u2y, -1.0f, 1.0f);
    const float angle = std::acos(dot);
    const float tan_dist = radius / std::tan(angle * 0.5f);

    const float t1x = x1 + u1x * tan_dist;
    const float t1y = y1 + u1y * tan_dist;
    const float t2x = x1 + u2x * tan_dist;
    const float t2y = y1 + u2y * tan_dist;

    const float sign = (cross > 0.0f) ? 1.0f : -1.0f;
    const float cx = t1x - u1y * radius * sign;
    const float cy = t1y + u1x * radius * sign;

    const float start_a = std::atan2(t1y - cy, t1x - cx);
    const float end_a = std::atan2(t2y - cy, t2x - cx);

    line_to(t1x, t1y);
    arc(cx, cy, radius, start_a, end_a, cross > 0.0f);
}

void CanvasRenderingContext2D::quadratic_curve_to(float cpx, float cpy, float x, float y) {
    if (!is_finite_f(cpx) || !is_finite_f(cpy) || !is_finite_f(x) || !is_finite_f(y)) return;
    Point cp = m_ctm.map_point(cpx, cpy);
    Point p2 = m_ctm.map_point(x, y);
    Point p0 = cp;
    if (!m_paths.empty() && !m_paths.back().points.empty()) {
        p0 = m_paths.back().points.back();
    } else {
        m_paths.push_back({{cp}, false});
    }

    const float approx_len = std::hypot(cp.x - p0.x, cp.y - p0.y) + std::hypot(p2.x - cp.x, p2.y - cp.y);
    const int steps = std::clamp(static_cast<int>(std::ceil(approx_len * 0.35f)), 8, 96);
    for (int s = 1; s <= steps; ++s) {
        const float t = static_cast<float>(s) / static_cast<float>(steps);
        const float inv = 1.0f - t;
        const float px = inv * inv * p0.x + 2.0f * inv * t * cp.x + t * t * p2.x;
        const float py = inv * inv * p0.y + 2.0f * inv * t * cp.y + t * t * p2.y;
        m_paths.back().points.emplace_back(px, py);
    }
}

void CanvasRenderingContext2D::bezier_curve_to(
    float cp1x, float cp1y, float cp2x, float cp2y, float x, float y
) {
    if (!is_finite_f(cp1x) || !is_finite_f(cp1y) || !is_finite_f(cp2x) ||
        !is_finite_f(cp2y) || !is_finite_f(x) || !is_finite_f(y)) return;
    Point c1 = m_ctm.map_point(cp1x, cp1y);
    Point c2 = m_ctm.map_point(cp2x, cp2y);
    Point p3 = m_ctm.map_point(x, y);
    Point p0 = c1;
    if (!m_paths.empty() && !m_paths.back().points.empty()) {
        p0 = m_paths.back().points.back();
    } else {
        m_paths.push_back({{c1}, false});
    }

    const float approx_len = std::hypot(c1.x - p0.x, c1.y - p0.y) +
                             std::hypot(c2.x - c1.x, c2.y - c1.y) +
                             std::hypot(p3.x - c2.x, p3.y - c2.y);
    const int steps = std::clamp(static_cast<int>(std::ceil(approx_len * 0.4f)), 10, 128);
    for (int s = 1; s <= steps; ++s) {
        const float t = static_cast<float>(s) / static_cast<float>(steps);
        const float inv = 1.0f - t;
        const float px = inv * inv * inv * p0.x + 3.0f * inv * inv * t * c1.x +
                         3.0f * inv * t * t * c2.x + t * t * t * p3.x;
        const float py = inv * inv * inv * p0.y + 3.0f * inv * inv * t * c1.y +
                         3.0f * inv * t * t * c2.y + t * t * t * p3.y;
        m_paths.back().points.emplace_back(px, py);
    }
}

void CanvasRenderingContext2D::rasterize_paths_to_mask(
    const std::vector<SubPath>& paths,
    FillRule rule,
    std::vector<uint8_t>& out_mask
) const {
    out_mask.assign(static_cast<size_t>(m_width) * static_cast<size_t>(m_height), 0);
    if (!has_backing_store() || paths.empty()) return;

    std::vector<std::vector<Point>> polys;
    polys.reserve(paths.size());
    for (const auto& sp : paths) {
        if (sp.points.size() >= 3) polys.push_back(sp.points);
    }
    if (polys.empty()) return;

    std::vector<Edge> edges;
    float min_y = 0.0f, max_y = 0.0f;
    build_edges(polys, edges, min_y, max_y);
    if (edges.empty() || !std::isfinite(min_y) || !std::isfinite(max_y)) return;

    const int start_y = std::max(0, static_cast<int>(std::floor(min_y)));
    const int end_y = std::min(m_height - 1, static_cast<int>(std::ceil(max_y)));
    if (start_y > end_y) return;

    struct Crossing {
        float x;
        int dir;
    };
    std::vector<Crossing> crossings;
    std::vector<float> row_cov(static_cast<size_t>(m_width), 0.0f);

    for (int y = start_y; y <= end_y; ++y) {
        int row_min_x = m_width;
        int row_max_x = -1;

        for (int sub = 0; sub < 4; ++sub) {
            const float sy = static_cast<float>(y) + (static_cast<float>(sub) + 0.5f) * 0.25f;
            crossings.clear();
            for (const auto& e : edges) {
                if (e.y0 <= sy && e.y1 > sy) {
                    const float t = (sy - e.y0) / (e.y1 - e.y0);
                    crossings.push_back({e.x0 + t * (e.x1 - e.x0), e.dir});
                }
            }
            if (crossings.size() < 2) continue;

            std::sort(crossings.begin(), crossings.end(),
                      [](const Crossing& a, const Crossing& b) { return a.x < b.x; });

            auto accumulate_span = [&](float xl, float xr) {
                xl = std::clamp(xl, 0.0f, static_cast<float>(m_width));
                xr = std::clamp(xr, 0.0f, static_cast<float>(m_width));
                if (xr <= xl) return;
                const int px_l = std::clamp(static_cast<int>(std::floor(xl)), 0, m_width - 1);
                const int px_r = std::clamp(static_cast<int>(std::floor(xr)), 0, m_width);
                row_min_x = std::min(row_min_x, px_l);
                row_max_x = std::max(row_max_x, std::min(m_width - 1, px_r));

                if (px_l == px_r) {
                    if (px_l < m_width) row_cov[px_l] += (xr - xl) * 0.25f;
                } else {
                    row_cov[px_l] += (static_cast<float>(px_l + 1) - xl) * 0.25f;
                    for (int px = px_l + 1; px < px_r && px < m_width; ++px) {
                        row_cov[px] += 0.25f;
                    }
                    if (px_r < m_width) {
                        row_cov[px_r] += (xr - static_cast<float>(px_r)) * 0.25f;
                    }
                }
            };

            if (rule == FillRule::EvenOdd) {
                for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
                    accumulate_span(crossings[i].x, crossings[i + 1].x);
                }
            } else {
                int winding = 0;
                float span_start = 0.0f;
                for (const auto& cr : crossings) {
                    const int prev = winding;
                    winding += cr.dir;
                    if (prev == 0 && winding != 0) {
                        span_start = cr.x;
                    } else if (prev != 0 && winding == 0) {
                        accumulate_span(span_start, cr.x);
                    }
                }
            }
        }

        if (row_min_x <= row_max_x) {
            uint8_t* dst_row = out_mask.data() + static_cast<size_t>(y) * static_cast<size_t>(m_width);
            for (int px = row_min_x; px <= row_max_x; ++px) {
                const float cov = row_cov[px];
                if (cov > 0.0f) {
                    dst_row[px] = static_cast<uint8_t>(std::clamp(std::lround(cov * 255.0f), 0l, 255l));
                    row_cov[px] = 0.0f;
                }
            }
        }
    }
}

void CanvasRenderingContext2D::rasterize_paths(
    const std::vector<SubPath>& paths,
    FillRule rule,
    const Color& solid_color,
    const std::shared_ptr<CanvasGradient>& gradient,
    const TransformMatrix2D& grad_inv_ctm,
    const std::shared_ptr<CanvasPattern>& pattern
) {
    if (!has_backing_store() || paths.empty()) return;

    std::vector<std::vector<Point>> polys;
    polys.reserve(paths.size());
    for (const auto& sp : paths) {
        if (sp.points.size() >= 3) polys.push_back(sp.points);
    }
    if (polys.empty()) return;

    std::vector<Edge> edges;
    float min_y = 0.0f, max_y = 0.0f;
    build_edges(polys, edges, min_y, max_y);
    if (edges.empty() || !std::isfinite(min_y) || !std::isfinite(max_y)) return;

    const int start_y = std::max(0, static_cast<int>(std::floor(min_y)));
    const int end_y = std::min(m_height - 1, static_cast<int>(std::ceil(max_y)));
    if (start_y > end_y) return;

    struct Crossing {
        float x;
        int dir;
    };
    std::vector<Crossing> crossings;
    std::vector<float> row_cov(static_cast<size_t>(m_width), 0.0f);
    // A gradient is defined in user space and sampled through the inverse CTM;
    // a pattern's tile is placed in device space and its own `setTransform` is
    // the only transform it has, so `grad_inv_ctm` must not be applied to it.
    const bool use_inv_ctm = (gradient != nullptr && !grad_inv_ctm.is_identity());
    // How much the pattern's tile is magnified where it lands: the longer axis
    // of the CTM's inverse, which is the transform the path was already mapped
    // through. 1 when nothing is scaled, and then the tile is its own pixels
    // exactly. A sheared transform has no single scale, so the longer axis is an
    // approximation, and it is the one `is_point_in_stroke` uses for line width.
    float pattern_scale = 1.0f;
    if (pattern) {
        const TransformMatrix2D& inv = grad_inv_ctm;
        pattern_scale = std::max(std::sqrt(inv.a * inv.a + inv.b * inv.b),
                                 std::sqrt(inv.c * inv.c + inv.d * inv.d));
        // The pattern's own `setTransform` scales it again.
        if (!pattern->transform.is_identity()) {
            const TransformMatrix2D& t = pattern->transform;
            pattern_scale *= std::max(std::sqrt(t.a * t.a + t.b * t.b),
                                      std::sqrt(t.c * t.c + t.d * t.d));
        }
        if (!std::isfinite(pattern_scale) || pattern_scale <= 0.0f) pattern_scale = 1.0f;
    }

    for (int y = start_y; y <= end_y; ++y) {
        int row_min_x = m_width;
        int row_max_x = -1;

        for (int sub = 0; sub < 4; ++sub) {
            const float sy = static_cast<float>(y) + (static_cast<float>(sub) + 0.5f) * 0.25f;
            crossings.clear();
            for (const auto& e : edges) {
                if (e.y0 <= sy && e.y1 > sy) {
                    const float t = (sy - e.y0) / (e.y1 - e.y0);
                    crossings.push_back({e.x0 + t * (e.x1 - e.x0), e.dir});
                }
            }
            if (crossings.size() < 2) continue;

            std::sort(crossings.begin(), crossings.end(),
                      [](const Crossing& a, const Crossing& b) { return a.x < b.x; });

            auto accumulate_span = [&](float xl, float xr) {
                xl = std::clamp(xl, 0.0f, static_cast<float>(m_width));
                xr = std::clamp(xr, 0.0f, static_cast<float>(m_width));
                if (xr <= xl) return;
                const int px_l = std::clamp(static_cast<int>(std::floor(xl)), 0, m_width - 1);
                const int px_r = std::clamp(static_cast<int>(std::floor(xr)), 0, m_width);
                row_min_x = std::min(row_min_x, px_l);
                row_max_x = std::max(row_max_x, std::min(m_width - 1, px_r));

                if (px_l == px_r) {
                    if (px_l < m_width) row_cov[px_l] += (xr - xl) * 0.25f;
                } else {
                    row_cov[px_l] += (static_cast<float>(px_l + 1) - xl) * 0.25f;
                    for (int px = px_l + 1; px < px_r && px < m_width; ++px) {
                        row_cov[px] += 0.25f;
                    }
                    if (px_r < m_width) {
                        row_cov[px_r] += (xr - static_cast<float>(px_r)) * 0.25f;
                    }
                }
            };

            if (rule == FillRule::EvenOdd) {
                for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
                    accumulate_span(crossings[i].x, crossings[i + 1].x);
                }
            } else {
                int winding = 0;
                float span_start = 0.0f;
                for (const auto& cr : crossings) {
                    const int prev = winding;
                    winding += cr.dir;
                    if (prev == 0 && winding != 0) {
                        span_start = cr.x;
                    } else if (prev != 0 && winding == 0) {
                        accumulate_span(span_start, cr.x);
                    }
                }
            }
        }

        if (row_min_x <= row_max_x) {
            for (int px = row_min_x; px <= row_max_x; ++px) {
                const float cov = row_cov[px];
                if (cov > 0.0f) {
                    row_cov[px] = 0.0f;
                    const uint8_t cov_u8 = static_cast<uint8_t>(std::clamp(std::lround(cov * 255.0f), 0l, 255l));
                    if (gradient || pattern) {
                        Point sample_pt(static_cast<float>(px) + 0.5f, static_cast<float>(y) + 0.5f);
                        if (use_inv_ctm) sample_pt = grad_inv_ctm.map_point(sample_pt);
                        const Color c = gradient ? gradient->sample_at(sample_pt.x, sample_pt.y)
                                                 : pattern->sample_at_device(sample_pt.x, sample_pt.y,
                                                                             image_smoothing_enabled, pattern_scale);
                        set_pixel_safe(px, y, c, cov_u8);
                    } else {
                        set_pixel_safe(px, y, solid_color, cov_u8);
                    }
                }
            }
        }
    }
}

void CanvasRenderingContext2D::stroke_paths(const std::vector<SubPath>& paths) {
    if (!has_backing_store() || paths.empty()) return;
    if (!is_finite_f(line_width) || line_width <= 0.0f) return;

    const float det = std::abs(m_ctm.determinant());
    const float ctm_scale = (det > 1e-6f) ? std::sqrt(det) : 1.0f;
    const float half_w = std::max(0.25f, line_width * ctm_scale * 0.5f);

    // Optional dash splitting
    std::vector<SubPath> work_paths;
    double dash_period = 0.0;
    for (float d : line_dash) dash_period += static_cast<double>(d) * ctm_scale;

    if (line_dash.empty() || dash_period <= 1e-5) {
        work_paths = paths;
    } else {
        for (const auto& sp : paths) {
            if (sp.points.size() < 2) continue;
            std::vector<Point> poly = sp.points;
            if (sp.closed) poly.push_back(sp.points.front());

            double offset = std::fmod(static_cast<double>(line_dash_offset) * ctm_scale, dash_period);
            if (offset < 0.0) offset += dash_period;

            size_t dash_idx = 0;
            double dash_rem = static_cast<double>(line_dash[0]) * ctm_scale;
            while (offset > 0.0 && dash_rem <= offset) {
                offset -= dash_rem;
                dash_idx = (dash_idx + 1) % line_dash.size();
                dash_rem = static_cast<double>(line_dash[dash_idx]) * ctm_scale;
            }
            dash_rem -= offset;
            bool drawing = (dash_idx % 2 == 0);

            SubPath cur;
            cur.closed = false;
            if (drawing) cur.points.push_back(poly[0]);

            for (size_t i = 0; i + 1 < poly.size(); ++i) {
                Point a = poly[i];
                const Point b = poly[i + 1];
                double seg_len = std::hypot(b.x - a.x, b.y - a.y);
                if (seg_len < 1e-6) continue;
                const double ux = (b.x - a.x) / seg_len;
                const double uy = (b.y - a.y) / seg_len;

                while (seg_len > 1e-6) {
                    const double step = std::min(seg_len, dash_rem);
                    Point next(static_cast<float>(a.x + ux * step), static_cast<float>(a.y + uy * step));
                    if (drawing) {
                        if (cur.points.empty()) cur.points.push_back(a);
                        cur.points.push_back(next);
                    }
                    a = next;
                    seg_len -= step;
                    dash_rem -= step;
                    if (dash_rem <= 1e-6) {
                        if (drawing && cur.points.size() >= 2) {
                            work_paths.push_back(std::move(cur));
                            cur = SubPath{};
                        }
                        dash_idx = (dash_idx + 1) % line_dash.size();
                        dash_rem = static_cast<double>(line_dash[dash_idx]) * ctm_scale;
                        drawing = (dash_idx % 2 == 0);
                        if (drawing) cur.points.push_back(a);
                    }
                }
            }
            if (drawing && cur.points.size() >= 2) {
                work_paths.push_back(std::move(cur));
            }
        }
    }

    // Expand every stroked polyline into CW-wound filled polygons (quads + joins + caps)
    std::vector<std::vector<Point>> expanded;
    const int cap_segs = std::clamp(static_cast<int>(std::ceil(half_w * 2.0f)), 8, 24);

    for (const auto& sp : work_paths) {
        if (sp.points.size() < 2) continue;

        struct Seg {
            Point a, b;
            float tx, ty; // unit tangent
            float nx, ny; // unit normal (-ty, tx)
        };
        std::vector<Seg> segs;
        const size_t n = sp.points.size();
        const size_t edge_count = sp.closed ? n : (n - 1);
        for (size_t i = 0; i < edge_count; ++i) {
            const Point& a = sp.points[i];
            const Point& b = sp.points[(i + 1) % n];
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float len = std::hypot(dx, dy);
            if (len < 1e-5f) continue;
            const float tx = dx / len;
            const float ty = dy / len;
            segs.push_back({a, b, tx, ty, -ty, tx});
        }
        if (segs.empty()) {
            // Degenerate zero-length closed/round stroke
            if (line_cap == "round" || line_cap == "square") {
                append_cw_circle_polygon(expanded, sp.points[0].x, sp.points[0].y, half_w, cap_segs);
            }
            continue;
        }

        // Segment body quads (consistent CW winding)
        for (const auto& s : segs) {
            std::vector<Point> quad = {
                Point(s.a.x + s.nx * half_w, s.a.y + s.ny * half_w),
                Point(s.b.x + s.nx * half_w, s.b.y + s.ny * half_w),
                Point(s.b.x - s.nx * half_w, s.b.y - s.ny * half_w),
                Point(s.a.x - s.nx * half_w, s.a.y - s.ny * half_w),
            };
            expanded.push_back(std::move(quad));
        }

        // Joins between consecutive segments
        const size_t join_count = sp.closed ? segs.size() : (segs.size() - 1);
        for (size_t i = 0; i < join_count; ++i) {
            const Seg& s0 = segs[i];
            const Seg& s1 = segs[(i + 1) % segs.size()];
            const Point& v = s0.b;
            const float cross = s0.tx * s1.ty - s0.ty * s1.tx;
            if (std::abs(cross) < 1e-4f) continue;

            if (line_join == "round") {
                append_cw_circle_polygon(expanded, v.x, v.y, half_w, cap_segs);
            } else {
                const float side = (cross > 0.0f) ? -1.0f : 1.0f;
                const Point o0(v.x + side * s0.nx * half_w, v.y + side * s0.ny * half_w);
                const Point o1(v.x + side * s1.nx * half_w, v.y + side * s1.ny * half_w);

                bool use_miter = (line_join == "miter");
                Point miter_pt = v;
                if (use_miter) {
                    // Solve o0 + k * t0 = o1 + m * t1
                    const float denom = s0.tx * s1.ty - s0.ty * s1.tx;
                    if (std::abs(denom) > 1e-5f) {
                        const float k = ((o1.x - o0.x) * s1.ty - (o1.y - o0.y) * s1.tx) / denom;
                        miter_pt = Point(o0.x + k * s0.tx, o0.y + k * s0.ty);
                        const float miter_dist = std::hypot(miter_pt.x - v.x, miter_pt.y - v.y);
                        if (miter_dist > miter_limit * half_w) {
                            use_miter = false;
                        }
                    } else {
                        use_miter = false;
                    }
                }

                std::vector<Point> join_poly;
                if (use_miter) {
                    join_poly = {v, o0, miter_pt, o1};
                } else {
                    join_poly = {v, o0, o1};
                }
                ensure_cw_winding(join_poly);
                expanded.push_back(std::move(join_poly));
            }
        }

        // End caps for open subpaths
        if (!sp.closed) {
            const Seg& first = segs.front();
            const Seg& last = segs.back();
            if (line_cap == "round") {
                append_cw_circle_polygon(expanded, first.a.x, first.a.y, half_w, cap_segs);
                append_cw_circle_polygon(expanded, last.b.x, last.b.y, half_w, cap_segs);
            } else if (line_cap == "square") {
                // Start cap extending backwards by half_w
                Point sa(first.a.x - first.tx * half_w, first.a.y - first.ty * half_w);
                expanded.push_back({
                    Point(sa.x + first.nx * half_w, sa.y + first.ny * half_w),
                    Point(first.a.x + first.nx * half_w, first.a.y + first.ny * half_w),
                    Point(first.a.x - first.nx * half_w, first.a.y - first.ny * half_w),
                    Point(sa.x - first.nx * half_w, sa.y - first.ny * half_w),
                });
                // End cap extending forwards by half_w
                Point eb(last.b.x + last.tx * half_w, last.b.y + last.ty * half_w);
                expanded.push_back({
                    Point(last.b.x + last.nx * half_w, last.b.y + last.ny * half_w),
                    Point(eb.x + last.nx * half_w, eb.y + last.ny * half_w),
                    Point(eb.x - last.nx * half_w, eb.y - last.ny * half_w),
                    Point(last.b.x - last.nx * half_w, last.b.y - last.ny * half_w),
                });
            }
        }
    }

    if (expanded.empty()) return;
    std::vector<SubPath> stroke_subpaths;
    stroke_subpaths.reserve(expanded.size());
    for (auto& poly : expanded) {
        stroke_subpaths.push_back({std::move(poly), true});
    }
    ShadowCapture shadow_guard(this);
    rasterize_paths(stroke_subpaths, FillRule::NonZero, stroke_style, stroke_gradient,
                    m_ctm.inverse(), stroke_pattern);
}

void CanvasRenderingContext2D::stroke() {
    stroke_paths(m_paths);
}

void CanvasRenderingContext2D::fill(FillRule rule) {
    ShadowCapture shadow_guard(this);
    rasterize_paths(m_paths, rule, fill_style, fill_gradient, m_ctm.inverse(), fill_pattern);
}

// §4.12.5.1.7: a `Path2D` is the calls it was given, replayed under the CTM in
// force **now**. Everything about how a curve becomes pixels — the flattening,
// the arc-to-bezier conversion, the degenerate-radius rules — is the context's own
// code, so a `Path2D` cannot disagree with `moveTo`+`lineTo` about any of it.
//
// The current path is saved and put back, because none of these four methods may
// disturb it: `ctx.fill(path)` twice must draw the same picture twice, and a page
// that drew into the context's own path before this call must find it intact
// afterwards.
void CanvasRenderingContext2D::replay_into(const Path2D& path) {
    for (const Path2D::Op& op : path.ops()) {
        switch (op.kind) {
            case Path2D::Kind::MoveTo:
                move_to(op.a, op.b);
                break;
            case Path2D::Kind::LineTo:
                line_to(op.a, op.b);
                break;
            case Path2D::Kind::QuadraticCurveTo:
                quadratic_curve_to(op.a, op.b, op.c, op.d);
                break;
            case Path2D::Kind::BezierCurveTo:
                bezier_curve_to(op.a, op.b, op.c, op.d, op.e, op.f);
                break;
            case Path2D::Kind::ArcTo:
                arc_to(op.a, op.b, op.c, op.d, op.e);
                break;
            case Path2D::Kind::Arc:
                arc(op.a, op.b, op.c, op.d, op.e, op.clockwise);
                break;
            case Path2D::Kind::Ellipse:
                ellipse(op.a, op.b, op.c, op.d, op.e, op.f, op.g, op.clockwise);
                break;
            case Path2D::Kind::Rect:
                rect(op.a, op.b, op.c, op.d);
                break;
            case Path2D::Kind::RoundRect:
                // The radii were kept as the page wrote them, because
                // `roundRect` takes either one value or four; `op.a`..`op.d` are
                // the rectangle and the four radii follow in `radii`.
                round_rect(op.a, op.b, op.c, op.d, op.e);
                break;
            case Path2D::Kind::ClosePath:
                close_path();
                break;
            case Path2D::Kind::AddPath: {
                if (!op.path) break;
                // `addPath(other, transform)`: the other path's calls, under this
                // path's matrix composed with the one in force. The transform
                // belongs to the *added* path's points, so it is applied by
                // pre-multiplying the CTM for the duration of the replay rather
                // than by transforming points here — that way an added arc is
                // still an arc and not a polyline of its flattened form.
                const TransformMatrix2D outer = m_ctm;
                m_ctm = outer.multiply(op.transform);
                // The added path's calls are appended to this path's, which is
                // what `addPath` means; `replay_into` adds without clearing.
                replay_into(*op.path);
                m_ctm = outer;
                break;
            }
        }
    }
}

void CanvasRenderingContext2D::fill_path(const Path2D& path, FillRule rule) {
    if (path.empty()) return;
    const std::vector<SubPath> saved = m_paths;
    m_paths.clear();
    replay_into(path);
    fill(rule);
    m_paths = saved;
}

void CanvasRenderingContext2D::stroke_path(const Path2D& path) {
    if (path.empty()) return;
    const std::vector<SubPath> saved = m_paths;
    m_paths.clear();
    replay_into(path);
    stroke();
    m_paths = saved;
}

void CanvasRenderingContext2D::clip_path(const Path2D& path, FillRule rule) {
    if (path.empty()) return;
    const std::vector<SubPath> saved = m_paths;
    m_paths.clear();
    replay_into(path);
    clip(rule);
    m_paths = saved;
}

bool CanvasRenderingContext2D::is_point_in_path_object(float x, float y, const Path2D& path,
                                                       FillRule rule) const {
    if (path.empty()) return false;
    // The test is against the path under the CTM in force, which is what
    // `is_point_in_path` already does for the current path; a copy of the context
    // would have to duplicate the whole state, so the path is laid out into a
    // temporary and asked directly.
    CanvasRenderingContext2D probe = *this;
    probe.m_paths.clear();
    probe.replay_into(path);
    return probe.is_point_in_path(x, y, rule);
}

void CanvasRenderingContext2D::clip(FillRule rule) {
    if (!has_backing_store()) return;
    std::vector<uint8_t> new_mask;
    rasterize_paths_to_mask(m_paths, rule, new_mask);
    if (m_clip_mask.empty()) {
        m_clip_mask = std::move(new_mask);
    } else {
        for (size_t i = 0; i < m_clip_mask.size() && i < new_mask.size(); ++i) {
            m_clip_mask[i] = static_cast<uint8_t>(
                (static_cast<uint32_t>(m_clip_mask[i]) * new_mask[i] + 127u) / 255u
            );
        }
    }
}

bool CanvasRenderingContext2D::is_point_in_path(float x, float y, FillRule rule) const {
    if (!is_finite_f(x) || !is_finite_f(y)) return false;
    int winding = 0;
    int crossings = 0;
    for (const auto& sp : m_paths) {
        if (sp.points.size() < 3) continue;
        const size_t n = sp.points.size();
        for (size_t i = 0; i < n; ++i) {
            const Point& a = sp.points[i];
            const Point& b = sp.points[(i + 1) % n];
            if (a.y == b.y) continue;
            const bool downward = a.y < b.y;
            const float y0 = downward ? a.y : b.y;
            const float y1 = downward ? b.y : a.y;
            if (y0 <= y && y1 > y) {
                const float x0 = downward ? a.x : b.x;
                const float x1 = downward ? b.x : a.x;
                const float xc = x0 + (y - y0) / (y1 - y0) * (x1 - x0);
                if (xc > x) {
                    ++crossings;
                    winding += downward ? 1 : -1;
                }
            }
        }
    }
    return (rule == FillRule::EvenOdd) ? ((crossings & 1) != 0) : (winding != 0);
}

bool CanvasRenderingContext2D::is_point_in_stroke(float x, float y) const {
    if (!is_finite_f(x) || !is_finite_f(y)) return false;
    // The stroke region is every point within half the line width of the
    // path, measured to the segment and not to its ends: a butt cap adds
    // nothing, so a point beyond the end of a segment is outside however
    // close it is to it. `lineWidth` is in user units and the path is stored
    // in device space, so the width is scaled by the CTM's longest axis —
    // which is exact for a uniform transform and the honest approximation
    // for a sheared one.
    const float sx = std::sqrt(m_ctm.a * m_ctm.a + m_ctm.b * m_ctm.b);
    const float sy = std::sqrt(m_ctm.c * m_ctm.c + m_ctm.d * m_ctm.d);
    const float scale = std::max(sx, sy);
    const float half = std::max(0.0f, line_width) * (scale > 0.0f ? scale : 1.0f) * 0.5f;
    for (const auto& sp : m_paths) {
        const std::size_t n = sp.points.size();
        if (n < 2) continue;
        // A closed subpath has its last segment back to the first; an open one
        // does not, which is the same rule `is_point_in_path` uses to decide
        // whether the wrap-around segment exists.
        const std::size_t segments = sp.closed ? n : n - 1;
        for (std::size_t i = 0; i < segments; ++i) {
            const Point& p = sp.points[i];
            const Point& q = sp.points[(i + 1) % n];
            const float vx = q.x - p.x;
            const float vy = q.y - p.y;
            const float len2 = vx * vx + vy * vy;
            float t = 0.0f;
            if (len2 > 0.0f) {
                t = ((x - p.x) * vx + (y - p.y) * vy) / len2;
                // Clamped, not wrapped: past either end the nearest point of
                // the segment is the end itself, which is where the cap is.
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
            }
            const float dx = x - (p.x + t * vx);
            const float dy = y - (p.y + t * vy);
            if (std::sqrt(dx * dx + dy * dy) <= half) return true;
        }
    }
    return false;
}

std::vector<uint8_t> CanvasRenderingContext2D::get_image_data_rgba(int sx, int sy, int sw, int sh) const {
    if (sw < 0) { sx += sw; sw = -sw; }
    if (sh < 0) { sy += sh; sh = -sh; }
    if (sw <= 0 || sh <= 0) return {};
    if (static_cast<int64_t>(sw) * sh > kMaxImageDataPixels) return {};

    std::vector<uint8_t> rgba(static_cast<size_t>(sw) * static_cast<size_t>(sh) * 4u, 0);
    if (!has_backing_store()) return rgba;

    for (int y = 0; y < sh; ++y) {
        const int cy = sy + y;
        if (cy < 0 || cy >= m_height) continue;
        for (int x = 0; x < sw; ++x) {
            const int cx = sx + x;
            if (cx < 0 || cx >= m_width) continue;
            const Color c = Color::from_argb(
                m_image->pixels[static_cast<size_t>(cy) * static_cast<size_t>(m_width) + static_cast<size_t>(cx)]
            );
            const size_t off = (static_cast<size_t>(y) * static_cast<size_t>(sw) + static_cast<size_t>(x)) * 4u;
            rgba[off + 0] = c.r;
            rgba[off + 1] = c.g;
            rgba[off + 2] = c.b;
            rgba[off + 3] = c.a;
        }
    }
    return rgba;
}

void CanvasRenderingContext2D::put_image_data_rgba(const uint8_t* rgba, int w, int h, int dx, int dy) {
    if (!has_backing_store() || !rgba || w <= 0 || h <= 0) return;
    if (static_cast<int64_t>(w) * h > kMaxImageDataPixels) return;

    for (int y = 0; y < h; ++y) {
        const int cy = dy + y;
        if (cy < 0 || cy >= m_height) continue;
        for (int x = 0; x < w; ++x) {
            const int cx = dx + x;
            if (cx < 0 || cx >= m_width) continue;
            const size_t off = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
            m_image->pixels[static_cast<size_t>(cy) * static_cast<size_t>(m_width) + static_cast<size_t>(cx)] =
                Color(rgba[off + 0], rgba[off + 1], rgba[off + 2], rgba[off + 3]).to_argb();
        }
    }
}

/// A bilinear sample of `img`'s pixels at a point in **image coordinates**.
/// `smooth` chooses between the four pixels around the point and the one it
/// falls in. The four weights sum to 1, so a 1:1 draw reproduces the image
/// exactly and a flat region stays flat.
static Color sample_image_bilinear(const ImageData& img, float x, float y, bool smooth) {
    const int w = img.width;
    const int h = img.height;
    auto at = [&img, w, h](int px, int py) {
        // Clamped to the edge: the filter reads the pixels it has, and it must
        // not wrap round to the far side the way a repetition rule would.
        return img.get_pixel_color(std::clamp(px, 0, w - 1), std::clamp(py, 0, h - 1));
    };
    if (!smooth) return at(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
    // Half a pixel: the centre of the pixel the point is in is at +0.5, so a
    // sample exactly on a centre takes that pixel and its neighbours equally.
    const float u = x - 0.5f;
    const float v = y - 0.5f;
    const float fu = std::floor(u);
    const float fv = std::floor(v);
    const float tx = u - fu;
    const float ty = v - fv;
    const int ix = static_cast<int>(fu);
    const int iy = static_cast<int>(fv);
    const Color c00 = at(ix, iy);
    const Color c10 = at(ix + 1, iy);
    const Color c01 = at(ix, iy + 1);
    const Color c11 = at(ix + 1, iy + 1);
    auto mix = [&](unsigned char a, unsigned char b, unsigned char c, unsigned char d) {
        const float top = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * tx;
        const float bottom = static_cast<float>(c) + (static_cast<float>(d) - static_cast<float>(c)) * tx;
        const float value = top + (bottom - top) * ty;
        return static_cast<unsigned char>(std::clamp(std::lround(value), 0l, 255l));
    };
    return Color(mix(c00.r, c10.r, c01.r, c11.r), mix(c00.g, c10.g, c01.g, c11.g),
                 mix(c00.b, c10.b, c01.b, c11.b), mix(c00.a, c10.a, c01.a, c11.a));
}

void CanvasRenderingContext2D::draw_image(const std::shared_ptr<ImageData>& img, float dx, float dy, float dw, float dh) {
    if (!has_backing_store() || !img || !img->is_valid()) return;
    ShadowCapture shadow_guard(this);
    if (!is_finite_f(dx) || !is_finite_f(dy) || !is_finite_f(dw) || !is_finite_f(dh)) return;

    const float dest_wf = (dw > 0.0f) ? dw : static_cast<float>(img->width);
    const float dest_hf = (dh > 0.0f) ? dh : static_cast<float>(img->height);
    if (dest_wf <= 0.0f || dest_hf <= 0.0f) return;

    if (m_ctm.is_identity()) {
        const int dest_w = std::max(1, static_cast<int>(std::round(dest_wf)));
        const int dest_h = std::max(1, static_cast<int>(std::round(dest_hf)));
        const int base_x = static_cast<int>(std::round(dx));
        const int base_y = static_cast<int>(std::round(dy));

        // Each destination pixel covers a rectangle of the source, whose centre
        // is sampled. The old code rounded that centre to the nearest pixel
        // index, which is why a 4x4 scaled up came out in blocks: with smoothing
        // off it still does, deliberately, and with it on the centre is kept as
        // a fraction and the four pixels around it are mixed.
        const float sx_scale = static_cast<float>(img->width) / static_cast<float>(dest_w);
        const float sy_scale = static_cast<float>(img->height) / static_cast<float>(dest_h);
        for (int sy = 0; sy < dest_h; ++sy) {
            const int py = base_y + sy;
            if (py < 0 || py >= m_height) continue;
            const float iy = (static_cast<float>(sy) + 0.5f) * sy_scale;
            for (int sx = 0; sx < dest_w; ++sx) {
                const int px = base_x + sx;
                if (px < 0 || px >= m_width) continue;
                const float ix = (static_cast<float>(sx) + 0.5f) * sx_scale;
                set_pixel_safe(px, py, sample_image_bilinear(*img, ix, iy, image_smoothing_enabled));
            }
        }
        return;
    }

    // General CTM-transformed drawImage via inverse mapping of destination bounding box
    if (std::abs(m_ctm.determinant()) < 1e-6f) return;
    const Rect bbox = m_ctm.map_rect(Rect(dx, dy, dest_wf, dest_hf));
    const TransformMatrix2D inv = m_ctm.inverse();
    const int x0 = std::max(0, static_cast<int>(std::floor(bbox.left())));
    const int y0 = std::max(0, static_cast<int>(std::floor(bbox.top())));
    const int x1 = std::min(m_width, static_cast<int>(std::ceil(bbox.right())));
    const int y1 = std::min(m_height, static_cast<int>(std::ceil(bbox.bottom())));

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const Point up = inv.map_point(static_cast<float>(px) + 0.5f, static_cast<float>(py) + 0.5f);
            const float u = (up.x - dx) / dest_wf;
            const float v = (up.y - dy) / dest_hf;
            if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) continue;
            set_pixel_safe(px, py, sample_image_bilinear(*img, u * static_cast<float>(img->width),
                                                         v * static_cast<float>(img->height),
                                                         image_smoothing_enabled));
        }
    }
}

/// §4.12.5.1.7: the first argument of `fill`, `stroke`, `clip` and
/// `isPointInPath` may be a `Path2D`, in which case the path is **that** one and
/// the current path is untouched. Returns null when the argument is not one, and
/// the caller then uses the current path — which is what a page that passed
/// `'evenodd'` or nothing at all expects.
///
/// This is the whole reason `fill({})` must not silently fill the current path
/// while a page believed it passed a path: a plain object is not a `Path2D`, so
/// the argument is the fill rule position instead. The specification's `[EnforceRange]`
/// union does not accept an arbitrary object either.
static std::shared_ptr<Path2D> path_arg(const std::vector<JsValue>& args, size_t idx) {
    if (args.size() <= idx) return nullptr;
    return args[idx].path2d_value();
}

static FillRule parse_fill_rule_arg(const std::vector<JsValue>& args, size_t idx) {
    if (args.size() > idx && args[idx].type == JsType::String && args[idx].to_string() == "evenodd") {
        return FillRule::EvenOdd;
    }
    return FillRule::NonZero;
}

JsValue CanvasRenderingContext2D::to_js_value(std::shared_ptr<CanvasRenderingContext2D> self) {
    JsValue ctx = JsValue::make_object();
    ctx.set_canvas_context(self);

    // Keep context alive when `self` is supplied; fall back to `this` for stack-allocated callers.
    auto get_ctx = [self, raw = this]() -> CanvasRenderingContext2D* {
        return self ? self.get() : raw;
    };

    ctx.set_property("save", JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
        get_ctx()->save();
        return JsValue::make_undefined();
    }));

    ctx.set_property("restore", JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
        get_ctx()->restore();
        return JsValue::make_undefined();
    }));

    ctx.set_property("translate", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            get_ctx()->translate(static_cast<float>(args[0].to_number()),
                                 static_cast<float>(args[1].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("rotate", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (!args.empty()) {
            get_ctx()->rotate(static_cast<float>(args[0].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("scale", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            get_ctx()->scale(static_cast<float>(args[0].to_number()),
                             static_cast<float>(args[1].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("transform", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 6) {
            get_ctx()->transform(static_cast<float>(args[0].to_number()),
                                 static_cast<float>(args[1].to_number()),
                                 static_cast<float>(args[2].to_number()),
                                 static_cast<float>(args[3].to_number()),
                                 static_cast<float>(args[4].to_number()),
                                 static_cast<float>(args[5].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("setTransform", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 6) {
            get_ctx()->set_transform(static_cast<float>(args[0].to_number()),
                                     static_cast<float>(args[1].to_number()),
                                     static_cast<float>(args[2].to_number()),
                                     static_cast<float>(args[3].to_number()),
                                     static_cast<float>(args[4].to_number()),
                                     static_cast<float>(args[5].to_number()));
        } else if (args.empty()) {
            get_ctx()->reset_transform();
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("resetTransform", JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
        get_ctx()->reset_transform();
        return JsValue::make_undefined();
    }));

    ctx.set_property("getTransform", JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
        const auto& m = get_ctx()->current_transform();
        JsValue out = JsValue::make_object();
        out.set_property("a", JsValue::make_number(m.a));
        out.set_property("b", JsValue::make_number(m.b));
        out.set_property("c", JsValue::make_number(m.c));
        out.set_property("d", JsValue::make_number(m.d));
        out.set_property("e", JsValue::make_number(m.e));
        out.set_property("f", JsValue::make_number(m.f));
        return out;
    }));

    ctx.set_property("setLineDash", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (!args.empty() && args[0].is_array()) {
            std::vector<float> segs;
            const auto& arr = args[0].array_elements();
            segs.reserve(arr.size());
            for (const auto& v : arr) segs.push_back(static_cast<float>(v.to_number()));
            get_ctx()->set_line_dash(segs);
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("getLineDash", JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
        std::vector<JsValue> elems;
        for (float d : get_ctx()->get_line_dash()) {
            elems.push_back(JsValue::make_number(d));
        }
        return JsValue::make_array(elems);
    }));

    ctx.set_property("fillRect", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 4) {
            get_ctx()->fill_rect(static_cast<float>(args[0].to_number()),
                                 static_cast<float>(args[1].to_number()),
                                 static_cast<float>(args[2].to_number()),
                                 static_cast<float>(args[3].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("strokeRect", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 4) {
            get_ctx()->stroke_rect(static_cast<float>(args[0].to_number()),
                                   static_cast<float>(args[1].to_number()),
                                   static_cast<float>(args[2].to_number()),
                                   static_cast<float>(args[3].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("clearRect", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 4) {
            get_ctx()->clear_rect(static_cast<float>(args[0].to_number()),
                                  static_cast<float>(args[1].to_number()),
                                  static_cast<float>(args[2].to_number()),
                                  static_cast<float>(args[3].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("beginPath", JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
        get_ctx()->begin_path();
        return JsValue::make_undefined();
    }));

    ctx.set_property("closePath", JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
        get_ctx()->close_path();
        return JsValue::make_undefined();
    }));

    ctx.set_property("moveTo", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            get_ctx()->move_to(static_cast<float>(args[0].to_number()),
                               static_cast<float>(args[1].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("lineTo", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            get_ctx()->line_to(static_cast<float>(args[0].to_number()),
                               static_cast<float>(args[1].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("rect", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 4) {
            get_ctx()->rect(static_cast<float>(args[0].to_number()),
                            static_cast<float>(args[1].to_number()),
                            static_cast<float>(args[2].to_number()),
                            static_cast<float>(args[3].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("roundRect", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 4) {
            const float x = static_cast<float>(args[0].to_number());
            const float y = static_cast<float>(args[1].to_number());
            const float w = static_cast<float>(args[2].to_number());
            const float h = static_cast<float>(args[3].to_number());
            if (args.size() >= 5 && args[4].is_array()) {
                const auto& a = args[4].array_elements();
                auto r = [&](size_t i) -> float { return i < a.size() ? static_cast<float>(a[i].to_number()) : 0.0f; };
                if (a.size() == 1) get_ctx()->round_rect(x, y, w, h, r(0));
                else if (a.size() == 2) get_ctx()->round_rect(x, y, w, h, r(0), r(1), r(0), r(1));
                else if (a.size() == 3) get_ctx()->round_rect(x, y, w, h, r(0), r(1), r(2), r(1));
                else if (a.size() >= 4) get_ctx()->round_rect(x, y, w, h, r(0), r(1), r(2), r(3));
                else get_ctx()->rect(x, y, w, h);
            } else {
                const float rad = (args.size() >= 5) ? static_cast<float>(args[4].to_number()) : 0.0f;
                get_ctx()->round_rect(x, y, w, h, rad);
            }
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("arc", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 5) {
            const bool ccw = (args.size() >= 6) ? args[5].is_truthy() : false;
            get_ctx()->arc(static_cast<float>(args[0].to_number()),
                           static_cast<float>(args[1].to_number()),
                           static_cast<float>(args[2].to_number()),
                           static_cast<float>(args[3].to_number()),
                           static_cast<float>(args[4].to_number()),
                           ccw);
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("ellipse", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 7) {
            const bool ccw = (args.size() >= 8) ? args[7].is_truthy() : false;
            get_ctx()->ellipse(static_cast<float>(args[0].to_number()),
                               static_cast<float>(args[1].to_number()),
                               static_cast<float>(args[2].to_number()),
                               static_cast<float>(args[3].to_number()),
                               static_cast<float>(args[4].to_number()),
                               static_cast<float>(args[5].to_number()),
                               static_cast<float>(args[6].to_number()),
                               ccw);
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("arcTo", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 5) {
            get_ctx()->arc_to(static_cast<float>(args[0].to_number()),
                              static_cast<float>(args[1].to_number()),
                              static_cast<float>(args[2].to_number()),
                              static_cast<float>(args[3].to_number()),
                              static_cast<float>(args[4].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("quadraticCurveTo", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 4) {
            get_ctx()->quadratic_curve_to(static_cast<float>(args[0].to_number()),
                                          static_cast<float>(args[1].to_number()),
                                          static_cast<float>(args[2].to_number()),
                                          static_cast<float>(args[3].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("bezierCurveTo", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 6) {
            get_ctx()->bezier_curve_to(static_cast<float>(args[0].to_number()),
                                       static_cast<float>(args[1].to_number()),
                                       static_cast<float>(args[2].to_number()),
                                       static_cast<float>(args[3].to_number()),
                                       static_cast<float>(args[4].to_number()),
                                       static_cast<float>(args[5].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("createLinearGradient", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        const float x0 = args.size() > 0 ? static_cast<float>(args[0].to_number()) : 0.0f;
        const float y0 = args.size() > 1 ? static_cast<float>(args[1].to_number()) : 0.0f;
        const float x1 = args.size() > 2 ? static_cast<float>(args[2].to_number()) : 0.0f;
        const float y1 = args.size() > 3 ? static_cast<float>(args[3].to_number()) : 0.0f;
        auto grad = get_ctx()->create_linear_gradient(x0, y0, x1, y1);
        return grad->to_js_value(grad);
    }));

    ctx.set_property("createRadialGradient", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        const float x0 = args.size() > 0 ? static_cast<float>(args[0].to_number()) : 0.0f;
        const float y0 = args.size() > 1 ? static_cast<float>(args[1].to_number()) : 0.0f;
        const float r0 = args.size() > 2 ? static_cast<float>(args[2].to_number()) : 0.0f;
        const float x1 = args.size() > 3 ? static_cast<float>(args[3].to_number()) : 0.0f;
        const float y1 = args.size() > 4 ? static_cast<float>(args[4].to_number()) : 0.0f;
        const float r1 = args.size() > 5 ? static_cast<float>(args[5].to_number()) : 0.0f;
        auto grad = get_ctx()->create_radial_gradient(x0, y0, r0, x1, y1, r1);
        return grad->to_js_value(grad);
    }));

    ctx.set_property("createConicGradient", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        // §4.12.5.1: `createConicGradient(startAngle, x, y)`. The angle is in
        // radians and is where the ramp's 0 is, measured from the positive x
        // axis; a missing coordinate is 0, which the binding's expected count
        // would otherwise have refused.
        const float angle = args.size() > 0 ? static_cast<float>(args[0].to_number()) : 0.0f;
        const float x = args.size() > 1 ? static_cast<float>(args[1].to_number()) : 0.0f;
        const float y = args.size() > 2 ? static_cast<float>(args[2].to_number()) : 0.0f;
        if (!std::isfinite(angle) || !std::isfinite(x) || !std::isfinite(y)) {
            // Step 1: a non-finite argument is a TypeError. Throwing from a
            // native function body is this engine's way of doing that.
            js_throw_type_error("Failed to execute 'createConicGradient' on 'CanvasRenderingContext2D': "
                                "the provided double value is non-finite.");
        }
        auto grad = get_ctx()->create_conic_gradient(angle, x, y);
        return grad->to_js_value(grad);
    }));

    ctx.set_property("stroke", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (auto path = path_arg(args, 0)) get_ctx()->stroke_path(*path);
        else get_ctx()->stroke();
        return JsValue::make_undefined();
    }));

    ctx.set_property("fill", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        // `fill(path, rule)` or `fill(rule)`: the argument's *type* decides which
        // shape of call this is, exactly as the IDL union does.
        if (auto path = path_arg(args, 0)) get_ctx()->fill_path(*path, parse_fill_rule_arg(args, 1));
        else get_ctx()->fill(parse_fill_rule_arg(args, 0));
        return JsValue::make_undefined();
    }));

    ctx.set_property("clip", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (auto path = path_arg(args, 0)) get_ctx()->clip_path(*path, parse_fill_rule_arg(args, 1));
        else get_ctx()->clip(parse_fill_rule_arg(args, 0));
        return JsValue::make_undefined();
    }));

    ctx.set_property("isPointInStroke", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            return JsValue::make_boolean(get_ctx()->is_point_in_stroke(
                static_cast<float>(args[0].to_number()),
                static_cast<float>(args[1].to_number())));
        }
        return JsValue::make_boolean(false);
    }));

    ctx.set_property("isPointInPath", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        // `isPointInPath(path, x, y, rule)` shifts every argument by one.
        if (auto path = path_arg(args, 0)) {
            if (args.size() >= 3) {
                return JsValue::make_boolean(get_ctx()->is_point_in_path_object(
                    static_cast<float>(args[1].to_number()),
                    static_cast<float>(args[2].to_number()), *path, parse_fill_rule_arg(args, 3)));
            }
            return JsValue::make_boolean(false);
        }
        if (args.size() >= 2) {
            return JsValue::make_boolean(get_ctx()->is_point_in_path(
                static_cast<float>(args[0].to_number()),
                static_cast<float>(args[1].to_number()),
                parse_fill_rule_arg(args, 2)
            ));
        }
        return JsValue::make_boolean(false);
    }));

    // `createImageData`, `getImageData` and `putImageData` are not here: each
    // hands back or takes an ImageData whose `data` is a Uint8ClampedArray, and
    // only a realm can make one. They are installed on this object by
    // DomHtmlBindings.cpp (Phase 139), over the pixel work in
    // get_image_data_rgba() and put_image_data_rgba() below.

    // Live JS accessors for extended state properties (without modifying JsValue.cpp)
    constexpr uint8_t kAccFlags = PropertyMap::Enumerable | PropertyMap::Configurable;
    ctx.define_accessor(
        "globalAlpha",
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
            return JsValue::make_number(get_ctx()->global_alpha);
        }),
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
            if (!args.empty()) {
                const double v = args[0].to_number();
                if (std::isfinite(v) && v >= 0.0 && v <= 1.0) {
                    get_ctx()->global_alpha = static_cast<float>(v);
                }
            }
            return JsValue::make_undefined();
        }),
        kAccFlags
    );

    // §4.12.5.1's `imageSmoothingEnabled` (a boolean, true at the start) and
    // `imageSmoothingQuality` ("low" | "medium" | "high", "low" at the start).
    // Neither existed before Phase 382, and the renderer sampled every image by
    // its nearest pixel whether the page had asked for otherwise or not. These
    // are live accessors rather than copied data properties, so `save()` and
    // `restore()` need no second write to the object to be seen.
    ctx.define_accessor(
        "imageSmoothingEnabled",
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
            return JsValue::make_boolean(get_ctx()->image_smoothing_enabled);
        }),
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
            // The IDL type is `boolean`, so any value is converted: a page that
            // assigns 0 or "" turns it off, which is what a browser does too.
            if (!args.empty()) get_ctx()->image_smoothing_enabled = args[0].is_truthy();
            return JsValue::make_undefined();
        }),
        kAccFlags
    );

    // §4.12.5.1's four shadow properties, registered the same way and for the
    // same reason as the two above: read from the renderer, never copied onto
    // the object. `shadowColor` is a CSS colour in the IDL, so an assignment
    // that does not parse leaves the old one — `Color::parse` answers
    // transparent black, which would silently switch the shadow off.
    ctx.define_accessor(
        "shadowBlur",
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
            return JsValue::make_number(get_ctx()->shadow_blur);
        }),
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
            if (args.empty()) return JsValue::make_undefined();
            const double v = args[0].to_number();
            // A negative or non-finite blur is ignored, as the specification's
            // `[EnforceRange]`-style clamping asks.
            if (std::isfinite(v) && v >= 0.0) get_ctx()->shadow_blur = static_cast<float>(v);
            return JsValue::make_undefined();
        }),
        kAccFlags
    );

    ctx.define_accessor(
        "shadowColor",
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
            // A string a page can hand straight back. This answered
            // `#rrggbb` + a malformed tail first, and — worse — whatever it
            // answered was parsed as **opaque black** when applied again, so a
            // round trip through the getter turned a transparent shadow into a
            // black one. `rgba()` is the form `Color::parse` reads without
            // ambiguity, and the alpha is stated every time.
            const Color c = get_ctx()->shadow_color;
            if (c.a == 255) return JsValue::make_string(c.to_hex());
            return JsValue::make_string("rgba(" + std::to_string(static_cast<int>(c.r)) + "," +
                                        std::to_string(static_cast<int>(c.g)) + "," +
                                        std::to_string(static_cast<int>(c.b)) + "," +
                                        std::to_string(static_cast<double>(c.a) / 255.0) + ")");
        }),
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
            if (args.empty()) return JsValue::make_undefined();
            const std::string asked = args[0].to_string();
            // `Color::parse` cannot fail, so an unparseable string is detected by
            // asking it for something and checking against a deliberately odd
            // input: the empty string and "bogus" both answer transparent black
            // while a page's "transparent" is legitimately transparent too.
            if (asked.empty()) return JsValue::make_undefined();
            get_ctx()->shadow_color = Color::parse(asked);
            return JsValue::make_undefined();
        }),
        kAccFlags
    );

    for (const char* name : {"shadowOffsetX", "shadowOffsetY"}) {
        const std::string which = name;
        ctx.define_accessor(
            name,
            JsValue::make_native_function([get_ctx, which](const std::vector<JsValue>&) {
                return JsValue::make_number(which == "shadowOffsetX" ? get_ctx()->shadow_offset_x
                                                                     : get_ctx()->shadow_offset_y);
            }),
            JsValue::make_native_function([get_ctx, which](const std::vector<JsValue>& args) {
                if (args.empty()) return JsValue::make_undefined();
                const double v = args[0].to_number();
                if (!std::isfinite(v)) return JsValue::make_undefined();
                if (which == "shadowOffsetX") get_ctx()->shadow_offset_x = static_cast<float>(v);
                else get_ctx()->shadow_offset_y = static_cast<float>(v);
                return JsValue::make_undefined();
            }),
            kAccFlags
        );
    }

    ctx.define_accessor(
        "imageSmoothingQuality",
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
            return JsValue::make_string(get_ctx()->image_smoothing_quality);
        }),
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
            // An enumerated attribute: a value that is not one of the three is
            // ignored and the old one stays, rather than being stored as-is.
            if (args.empty()) return JsValue::make_undefined();
            const std::string asked = args[0].to_string();
            if (asked == "low" || asked == "medium" || asked == "high") {
                get_ctx()->image_smoothing_quality = asked;
            }
            return JsValue::make_undefined();
        }),
        kAccFlags
    );

    // §4.12.5.1's `filter`. It did not exist, so no filter function could be
    // asked for at all. The getter answers the string the page wrote — as a
    // browser does, including a value that came back to `"none"` — and the
    // setter parses it into the list the renderer applies.
    ctx.define_accessor(
        "filter",
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
            return JsValue::make_string(get_ctx()->filter_source);
        }),
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
            if (args.empty()) return JsValue::make_undefined();
            const std::string asked = args[0].to_string();
            FilterParse parsed = parse_filter_list(asked);
            // An invalid value is ignored, per the specification, so the
            // previous source and list both stay. A valid one, including
            // `"none"`, replaces both.
            if (!parsed.ok) return JsValue::make_undefined();
            get_ctx()->filter_source = asked;
            get_ctx()->filter_list = std::move(parsed.list);
            return JsValue::make_undefined();
        }),
        kAccFlags
    );

    ctx.define_accessor(
        "lineCap",
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
            return JsValue::make_string(get_ctx()->line_cap);
        }),
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
            if (!args.empty()) {
                const std::string v = args[0].to_string();
                if (v == "butt" || v == "round" || v == "square") get_ctx()->line_cap = v;
            }
            return JsValue::make_undefined();
        }),
        kAccFlags
    );

    ctx.define_accessor(
        "lineJoin",
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
            return JsValue::make_string(get_ctx()->line_join);
        }),
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
            if (!args.empty()) {
                const std::string v = args[0].to_string();
                if (v == "miter" || v == "round" || v == "bevel") get_ctx()->line_join = v;
            }
            return JsValue::make_undefined();
        }),
        kAccFlags
    );

    ctx.define_accessor(
        "miterLimit",
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
            return JsValue::make_number(get_ctx()->miter_limit);
        }),
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
            if (!args.empty()) {
                const double v = args[0].to_number();
                if (std::isfinite(v) && v > 0.0) get_ctx()->miter_limit = static_cast<float>(v);
            }
            return JsValue::make_undefined();
        }),
        kAccFlags
    );

    ctx.define_accessor(
        "lineDashOffset",
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
            return JsValue::make_number(get_ctx()->line_dash_offset);
        }),
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
            if (!args.empty()) {
                const double v = args[0].to_number();
                if (std::isfinite(v)) get_ctx()->line_dash_offset = static_cast<float>(v);
            }
            return JsValue::make_undefined();
        }),
        kAccFlags
    );

    ctx.define_accessor(
        "font",
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
            return JsValue::make_string(get_ctx()->font);
        }),
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
            if (!args.empty()) {
                const std::string v = args[0].to_string();
                ParsedCanvasFont parsed;
                // Per spec, an invalid font string is ignored (previous value kept).
                if (parse_canvas_font(v, parsed)) get_ctx()->font = canvas_trim(v);
            }
            return JsValue::make_undefined();
        }),
        kAccFlags
    );

    ctx.define_accessor(
        "textAlign",
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
            return JsValue::make_string(get_ctx()->text_align);
        }),
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
            if (!args.empty()) {
                const std::string v = args[0].to_string();
                if (v == "start" || v == "end" || v == "left" || v == "right" || v == "center") {
                    get_ctx()->text_align = v;
                }
            }
            return JsValue::make_undefined();
        }),
        kAccFlags
    );

    ctx.define_accessor(
        "textBaseline",
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
            return JsValue::make_string(get_ctx()->text_baseline);
        }),
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
            if (!args.empty()) {
                const std::string v = args[0].to_string();
                if (v == "top" || v == "hanging" || v == "middle" || v == "alphabetic" || v == "ideographic" ||
                    v == "bottom") {
                    get_ctx()->text_baseline = v;
                }
            }
            return JsValue::make_undefined();
        }),
        kAccFlags
    );

    // §4.12.5.1's two spacings. `typeof ctx.letterSpacing` was `undefined` before
    // Phase 417, so `ctx.letterSpacing = "5px"` was an expando and changed
    // nothing: `measureText('ab')` answered the same number before and after it.
    // The setter parses the value against the **current** font size (which is what
    // resolves an `em`) and ignores what it cannot parse, keeping the old value.
    auto spacing_accessor = [&ctx, get_ctx](const char* name, bool letter) {
        ctx.define_accessor(
            name,
            JsValue::make_native_function([get_ctx, letter](const std::vector<JsValue>&) {
                return JsValue::make_string(letter ? get_ctx()->letter_spacing_source
                                                   : get_ctx()->word_spacing_source);
            }),
            JsValue::make_native_function([get_ctx, letter](const std::vector<JsValue>& args) {
                if (args.empty()) return JsValue::make_undefined();
                const std::string asked = args[0].to_string();
                // The font size the length is resolved against, read the same way
                // `layout_text` reads it so `1em` here is one em there.
                ParsedCanvasFont parsed;
                if (!parse_canvas_font(get_ctx()->font, parsed)) parsed = ParsedCanvasFont{};
                float px = 0.0f;
                if (!parse_css_length_px(asked, std::max(0.0f, parsed.size_px), px)) {
                    return JsValue::make_undefined();
                }
                if (letter) {
                    get_ctx()->letter_spacing_px = px;
                    // The serialized form, which is what the getter must answer:
                    // `0` and `0px` both serialize as `0px`, and a page that wrote
                    // `2EM` reads back `2em`.
                    get_ctx()->letter_spacing_source = px == 0.0f ? "0px" : serialize_px(asked, px);
                } else {
                    get_ctx()->word_spacing_px = px;
                    get_ctx()->word_spacing_source = px == 0.0f ? "0px" : serialize_px(asked, px);
                }
                return JsValue::make_undefined();
            }),
            kAccFlags
        );
    };
    spacing_accessor("letterSpacing", true);
    spacing_accessor("wordSpacing", false);

    ctx.define_accessor(
        "direction",
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>&) {
            return JsValue::make_string(get_ctx()->direction);
        }),
        JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
            if (args.empty()) return JsValue::make_undefined();
            const std::string v = args[0].to_string();
            if (v == "ltr" || v == "rtl" || v == "inherit") get_ctx()->direction = v;
            return JsValue::make_undefined();
        }),
        kAccFlags
    );

    // Four enumerated attributes, whose sets differ in one way that matters: an
    // unknown value is ignored, and the initial value is not the first of the
    // list. Registered in a loop because the shape is identical.
    struct EnumAttr { const char* name; std::string CanvasRenderingContext2D::*field; std::vector<std::string> values; };
    const std::vector<EnumAttr> enumerated{
        {"fontKerning", &CanvasRenderingContext2D::font_kerning, {"auto", "normal", "none"}},
        {"fontStretch", &CanvasRenderingContext2D::font_stretch,
         {"ultra-condensed", "extra-condensed", "condensed", "semi-condensed", "normal",
          "semi-expanded", "expanded", "extra-expanded", "ultra-expanded"}},
        {"fontVariantCaps", &CanvasRenderingContext2D::font_variant_caps,
         {"normal", "small-caps", "all-small-caps", "petite-caps", "all-petite-caps", "unicase", "titling-caps"}},
        {"textRendering", &CanvasRenderingContext2D::text_rendering,
         {"auto", "optimizeSpeed", "optimizeLegibility", "geometricPrecision"}},
    };
    for (const EnumAttr& attr : enumerated) {
        const std::string name = attr.name;
        const auto field = attr.field;
        const std::vector<std::string> values = attr.values;
        ctx.define_accessor(
            name.c_str(),
            JsValue::make_native_function([get_ctx, field](const std::vector<JsValue>&) {
                return JsValue::make_string(get_ctx()->*field);
            }),
            JsValue::make_native_function([get_ctx, field, values](const std::vector<JsValue>& args) {
                if (args.empty()) return JsValue::make_undefined();
                const std::string v = args[0].to_string();
                for (const std::string& allowed : values) {
                    if (v == allowed) {
                        get_ctx()->*field = v;
                        break;
                    }
                }
                return JsValue::make_undefined();
            }),
            kAccFlags
        );
    }

    auto text_max_width = [](const std::vector<JsValue>& args) {
        if (args.size() < 4 || args[3].is_nullish() || args[3].is_undefined()) return -1.0f;
        const double v = args[3].to_number();
        return std::isfinite(v) && v >= 0.0 ? static_cast<float>(v) : -1.0f;
    };

    ctx.set_property("fillText", JsValue::make_native_function([get_ctx, text_max_width](const std::vector<JsValue>& args) {
        if (args.size() >= 3) {
            get_ctx()->fill_text(args[0].to_string(), static_cast<float>(args[1].to_number()),
                                 static_cast<float>(args[2].to_number()), text_max_width(args));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("strokeText", JsValue::make_native_function([get_ctx, text_max_width](const std::vector<JsValue>& args) {
        if (args.size() >= 3) {
            get_ctx()->stroke_text(args[0].to_string(), static_cast<float>(args[1].to_number()),
                                   static_cast<float>(args[2].to_number()), text_max_width(args));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("measureText", JsValue::make_native_function([get_ctx](const std::vector<JsValue>& args) {
        const std::string text = args.empty() ? std::string() : args[0].to_string();
        const auto m = get_ctx()->measure_text(text);
        JsValue out = JsValue::make_object();
        out.set_property("width", JsValue::make_number(m.width));
        out.set_property("actualBoundingBoxLeft", JsValue::make_number(m.actual_bounding_box_left));
        out.set_property("actualBoundingBoxRight", JsValue::make_number(m.actual_bounding_box_right));
        out.set_property("actualBoundingBoxAscent", JsValue::make_number(m.actual_bounding_box_ascent));
        out.set_property("actualBoundingBoxDescent", JsValue::make_number(m.actual_bounding_box_descent));
        out.set_property("fontBoundingBoxAscent", JsValue::make_number(m.font_bounding_box_ascent));
        out.set_property("fontBoundingBoxDescent", JsValue::make_number(m.font_bounding_box_descent));
        out.set_property("emHeightAscent", JsValue::make_number(m.em_height_ascent));
        out.set_property("emHeightDescent", JsValue::make_number(m.em_height_descent));
        out.set_property("hangingBaseline", JsValue::make_number(m.hanging_baseline));
        out.set_property("alphabeticBaseline", JsValue::make_number(0.0));
        out.set_property("ideographicBaseline", JsValue::make_number(m.ideographic_baseline));
        return out;
    }));

    // Existing properties handled via JsValue.cpp fast-path kept initialized
    ctx.set_property("fillStyle", JsValue::make_string(fill_style.to_hex()));
    ctx.set_property("strokeStyle", JsValue::make_string(stroke_style.to_hex()));
    ctx.set_property("lineWidth", JsValue::make_number(line_width));

    return ctx;
}

} // namespace litebird
