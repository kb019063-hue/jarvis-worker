#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace litebird {

enum class ColorSpace : uint8_t {
    Srgb,
    SrgbLinear,
    DisplayP3,
    DisplayP3Linear,
    A98Rgb,
    ProphotoRgb,
    Rec2020,
    XyzD50,
    XyzD65,
    Lab,
    Lch,
    Oklab,
    Oklch,
    Hsl,
    Hwb,
};

enum class HueInterpolation : uint8_t {
    Shorter,
    Longer,
    Increasing,
    Decreasing,
};

struct CssColor;

struct ColorResolveContext {
    /// If non-null, resolves `currentcolor` (including inside `color-mix()` and
    /// relative color syntax). If null, `currentcolor` produces a `CssColor`
    /// with `is_currentcolor == true` at top level and fails inside expressions
    /// that require channel values.
    const CssColor* current_color{nullptr};
    /// Selects the second branch of CSS Color 5 `light-dark(light, dark)`.
    bool dark_scheme{false};
};

struct Color {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};

    constexpr Color() = default;
    constexpr Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
        : r(red), g(green), b(blue), a(alpha) {}

    constexpr bool operator==(const Color& other) const = default;
    constexpr bool operator!=(const Color& other) const = default;

    constexpr bool is_transparent() const { return a == 0; }

    constexpr uint32_t to_argb() const {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8) |
               (static_cast<uint32_t>(b));
    }

    static constexpr Color from_argb(uint32_t val) {
        return Color(
            static_cast<uint8_t>((val >> 16) & 0xFF),
            static_cast<uint8_t>((val >> 8) & 0xFF),
            static_cast<uint8_t>(val & 0xFF),
            static_cast<uint8_t>((val >> 24) & 0xFF)
        );
    }

    /// The colour a CSS named colour stands for, as 0xRRGGBB. Covers all 148
    /// CSS Color 4 named keywords; `transparent` and `currentcolor` are handled
    /// separately because neither is a fixed RGB triplet in CSSOM.
    static bool named_color_value(const std::string& lower, unsigned& out);

    /// True if `lower_name` is a CSS Color 4/5 functional notation name
    /// (`rgb`, `rgba`, `hsl`, `hsla`, `hwb`, `lab`, `lch`, `oklab`, `oklch`,
    /// `color`, `color-mix`, `light-dark`).
    static bool is_color_function_name(std::string_view lower_name);

    /// Fast syntactic check used by CSS shorthand splitters to recognize color
    /// tokens (`#hex`, named colors, `transparent`, `currentcolor`, or any
    /// CSS Color 4/5 function).
    static bool looks_like_color(std::string_view token);

    static constexpr Color transparent() { return Color(0, 0, 0, 0); }
    static constexpr Color black()       { return Color(0, 0, 0, 255); }
    static constexpr Color white()       { return Color(255, 255, 255, 255); }
    static constexpr Color red()         { return Color(255, 0, 0, 255); }
    static constexpr Color green()       { return Color(0, 128, 0, 255); }
    static constexpr Color blue()        { return Color(0, 0, 255, 255); }
    static constexpr Color gray()        { return Color(128, 128, 128, 255); }
    static constexpr Color dark_gray()   { return Color(40, 44, 52, 255); }
    static constexpr Color light_gray()  { return Color(220, 220, 220, 255); }

    /// Strict CSS Color 4/5 parser returning false on invalid syntax.
    static bool try_parse(std::string_view str, Color& out);
    static bool try_parse(std::string_view str, Color& out, const Color& current_color,
                          bool dark_scheme = false);

    /// Parses any CSS Color 4/5 color into 8-bit sRGB (with CSS Color 4 §13.2
    /// OKLCH gamut mapping for wide-gamut spaces), falling back to black on
    /// invalid input for legacy callers.
    static Color parse(std::string_view str);
    static Color parse(std::string_view str, const Color& current_color, bool dark_scheme = false);

    std::string to_hex() const;
    std::string to_ansi_fg() const;
    std::string to_ansi_bg() const;
};

/// Full-precision CSS Color 4 / CSS Color 5 color representation.
///
/// Preserves the color space, floating-point channel values (including extended
/// out-of-gamut ranges), missing (`none`) component flags, and whether the
/// color serializes using legacy `rgb()`/`rgba()` syntax or modern Color 4
/// syntax (`lab()`, `lch()`, `oklab()`, `oklch()`, `color(<space> ...)`).
struct CssColor {
    ColorSpace space{ColorSpace::Srgb};
    bool legacy_srgb{true};
    bool from_8bit_alpha{false};
    bool is_currentcolor{false};
    double c[3]{0.0, 0.0, 0.0};
    double alpha{1.0};
    bool missing[3]{false, false, false};
    bool alpha_missing{false};

    static CssColor from_srgb8(Color c, bool legacy = true);

    /// Parses any CSS Color 4/5 expression into a `CssColor`.
    static bool parse(std::string_view text, CssColor& out, const ColorResolveContext& ctx = {});

    /// Validates CSS Color 4/5 syntax even when `currentcolor` or `light-dark()`
    /// requires deferred resolution at computed-value time.
    static bool is_valid_syntax(std::string_view text);

    /// Converts this color to `target` per CSS Color 4 §10 & §12.2 (including
    /// Bradford D65<->D50 chromatic adaptation, powerless hue detection, and
    /// analogous missing component carry-forward).
    CssColor convert_to(ColorSpace target) const;

    /// Mixes two colors per CSS Color 5 §2 (`color-mix()`).
    static bool mix(const CssColor& c1, double p1, bool has_p1,
                    const CssColor& c2, double p2, bool has_p2,
                    ColorSpace interp_space, HueInterpolation hue_method,
                    CssColor& out);

    /// Converts to 8-bit sRGB `Color` using CSS Color 4 §13.2 OKLCH chroma
    /// reduction gamut mapping when a wide-gamut color falls outside sRGB.
    Color to_srgb8() const;

    /// Serializes per CSS Color 4 §15 (computed value serialization).
    std::string serialize_computed() const;
};

} // namespace litebird
