#include "litebird/common/Color.hpp"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace litebird {

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kE = 2.718281828459045235360287471352662498;
constexpr int kMaxColorDepth = 32;
constexpr int kMaxMathDepth = 64;

using Vec3 = std::array<double, 3>;
using Mat3 = std::array<std::array<double, 3>, 3>;

inline Vec3 mat_mul(const Mat3& m, const Vec3& v) {
    return {
        m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
        m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
        m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2],
    };
}

// ============================================================================
// Normative W3C CSS Color 4 §10 & §17 Matrices and Transfer Functions
// ============================================================================

// D50 standard illuminant white point (CSS Color 4 §17)
constexpr Vec3 kD50 = {0.3457 / 0.3585, 1.0, (1.0 - 0.3457 - 0.3585) / 0.3585};
constexpr double kLabKappa = 24389.0 / 27.0;
constexpr double kLabEpsilon = 216.0 / 24389.0;

// Linear sRGB <-> XYZ-D65
constexpr Mat3 kLinSrgbToXyzD65 = {{
    {0.41239079926595951, 0.35758433938387796, 0.18048078840183429},
    {0.21263900587151036, 0.71516867876775592, 0.072192315360733714},
    {0.019330818715591849, 0.11919477979462599, 0.95053215224966059},
}};
constexpr Mat3 kXyzD65ToLinSrgb = {{
    {3.2409699419045213, -1.5373831775700935, -0.49861076029300327},
    {-0.96924363628087984, 1.8759675015077206, 0.041555057407175612},
    {0.055630079696993608, -0.20397695888897657, 1.0569715142428786},
}};

// Linear Display P3 <-> XYZ-D65
constexpr Mat3 kLinP3ToXyzD65 = {{
    {0.48657094864821626, 0.26566769316909294, 0.19821728523436249},
    {0.22897456406974884, 0.69173852183650619, 0.079286914093744998},
    {0.0, 0.045113381858902575, 1.0439443689009757},
}};
constexpr Mat3 kXyzD65ToLinP3 = {{
    {2.4934969119414245, -0.93138361791912361, -0.40271078445071684},
    {-0.82948896956157503, 1.7626640603183468, 0.023624685841943591},
    {0.035845830243784335, -0.076172389268041707, 0.95688452400768731},
}};

// Linear A98 RGB <-> XYZ-D65
constexpr Mat3 kLinA98ToXyzD65 = {{
    {0.57666904291013077, 0.18555823790654627, 0.18822864623499472},
    {0.29734497525053616, 0.62736356625546597, 0.07529145849399789},
    {0.027031361386412378, 0.070688852535827143, 0.99133753683763892},
}};
constexpr Mat3 kXyzD65ToLinA98 = {{
    {2.0415879038107461, -0.5650069742788596, -0.3447313507783295},
    {-0.96924363628087984, 1.8759675015077206, 0.041555057407175612},
    {0.013444280632031024, -0.11836239223101824, 1.0151749943912054},
}};

// Linear ProPhoto RGB <-> XYZ-D50
constexpr Mat3 kLinProphotoToXyzD50 = {{
    {0.79776048967230251, 0.13518583717574034, 0.031349349581524806},
    {0.28807112822929337, 0.7118432178101014, 8.5653960605259035e-5},
    {0.0, 0.0, 0.82510460251046025},
}};
constexpr Mat3 kXyzD50ToLinProphoto = {{
    {1.3457989731028284, -0.2555801000799755, -0.051106285067534021},
    {-0.54462249390283479, 1.5082327413132783, 0.020536032391479733},
    {0.0, 0.0, 1.2119675456389452},
}};

// Linear Rec. 2020 <-> XYZ-D65
constexpr Mat3 kLinRec2020ToXyzD65 = {{
    {0.63695804830129132, 0.14461690358620838, 0.16888097516417205},
    {0.26270021201126703, 0.67799807151887104, 0.059301716469861945},
    {0.0, 0.028072693049087508, 1.0609850577107909},
}};
constexpr Mat3 kXyzD65ToLinRec2020 = {{
    {1.7166511879712676, -0.35567078377639239, -0.2533662813736598},
    {-0.66668435183248898, 1.616481236634939, 0.015768545813911131},
    {0.017639857445310915, -0.042770613257808655, 0.94210312123547402},
}};

// Bradford Chromatic Adaptation D65 <-> D50
constexpr Mat3 kXyzD65ToD50 = {{
    {1.0479297925449966, 0.022946870601609527, -0.050192266289205194},
    {0.029627808770055674, 0.99043442675388005, -0.017073799063418792},
    {-0.0092430406462045214, 0.015055191490298164, 0.75187428142813695},
}};
constexpr Mat3 kXyzD50ToD65 = {{
    {0.95547342148807524, -0.023098454948764523, 0.063259243200570664},
    {-0.028369709333863583, 1.0099953980813041, 0.021041441191917306},
    {0.012314014864481996, -0.020507649298898981, 1.3303659262421239},
}};

// CSS Color 4 normative OKLab <-> LMS <-> XYZ-D65
constexpr Mat3 kXyzD65ToLms = {{
    {0.8190224379967030, 0.3619062600528904, -0.1288737815209879},
    {0.0329836539323885, 0.9292868615863434, 0.0361446663506424},
    {0.0481771893596242, 0.2642395317527308, 0.6335478284694309},
}};
constexpr Mat3 kLmsToOklab = {{
    {0.2104542683093140, 0.7936177747023054, -0.0040720430116193},
    {1.9779985324311684, -2.4285922420485799, 0.4505937096174110},
    {0.0259040424655478, 0.7827717124575296, -0.8086757549230774},
}};
constexpr Mat3 kOklabToLms = {{
    {1.0, 0.39633777737617504, 0.21580375730991364},
    {1.0, -0.10556134581565856, -0.063854172825813335},
    {1.0, -0.089484177529811817, -1.2914855480194094},
}};
constexpr Mat3 kLmsToXyzD65 = {{
    {1.2268798758459241, -0.55781499446021698, 0.28139104566596462},
    {-0.040575745214800875, 1.1122868032803173, -0.07171105806551642},
    {-0.076372936674660011, -0.42149333240224313, 1.5869240198367816},
}};

inline double srgb_to_linear_channel(double v) {
    const double s = v < 0.0 ? -1.0 : 1.0;
    const double a = std::fabs(v);
    if (a <= 0.04045) return v / 12.92;
    return s * std::pow((a + 0.055) / 1.055, 2.4);
}

inline double linear_to_srgb_channel(double v) {
    const double s = v < 0.0 ? -1.0 : 1.0;
    const double a = std::fabs(v);
    if (a > 0.0031308) return s * (1.055 * std::pow(a, 1.0 / 2.4) - 0.055);
    return 12.92 * v;
}

inline double a98_to_linear_channel(double v) {
    const double s = v < 0.0 ? -1.0 : 1.0;
    return s * std::pow(std::fabs(v), 563.0 / 256.0);
}

inline double linear_to_a98_channel(double v) {
    const double s = v < 0.0 ? -1.0 : 1.0;
    return s * std::pow(std::fabs(v), 256.0 / 563.0);
}

inline double prophoto_to_linear_channel(double v) {
    const double s = v < 0.0 ? -1.0 : 1.0;
    const double a = std::fabs(v);
    if (a <= 16.0 / 512.0) return v / 16.0;
    return s * std::pow(a, 1.8);
}

inline double linear_to_prophoto_channel(double v) {
    const double s = v < 0.0 ? -1.0 : 1.0;
    const double a = std::fabs(v);
    if (a >= 1.0 / 512.0) return s * std::pow(a, 1.0 / 1.8);
    return 16.0 * v;
}

inline double rec2020_to_linear_channel(double v) {
    constexpr double alpha = 1.09929682680944;
    constexpr double beta = 0.018053968510807;
    const double s = v < 0.0 ? -1.0 : 1.0;
    const double a = std::fabs(v);
    if (a < beta * 4.5) return v / 4.5;
    return s * std::pow((a + alpha - 1.0) / alpha, 1.0 / 0.45);
}

inline double linear_to_rec2020_channel(double v) {
    constexpr double alpha = 1.09929682680944;
    constexpr double beta = 0.018053968510807;
    const double s = v < 0.0 ? -1.0 : 1.0;
    const double a = std::fabs(v);
    if (a > beta) return s * (alpha * std::pow(a, 0.45) - (alpha - 1.0));
    return 4.5 * v;
}

inline double normalize_hue(double h) {
    if (!std::isfinite(h)) return 0.0;
    h = std::fmod(h, 360.0);
    if (h < 0.0) h += 360.0;
    if (h >= 360.0 || h == -0.0) h = 0.0;
    return h;
}

inline Vec3 hsl_to_srgb(double h_deg, double s_pct, double l_pct) {
    const double h = normalize_hue(h_deg);
    const double s = s_pct / 100.0;
    const double l = l_pct / 100.0;
    auto f = [&](double n) {
        const double k = std::fmod(n + h / 30.0, 12.0);
        const double a = s * std::min(l, 1.0 - l);
        return l - a * std::max(-1.0, std::min({k - 3.0, 9.0 - k, 1.0}));
    };
    return {f(0.0), f(8.0), f(4.0)};
}

inline Vec3 srgb_to_hsl(const Vec3& rgb, bool& hue_powerless) {
    const double r = rgb[0], g = rgb[1], b = rgb[2];
    const double mx = std::max({r, g, b});
    const double mn = std::min({r, g, b});
    const double d = mx - mn;
    const double l = 0.5 * (mx + mn);
    double h = 0.0;
    double s = 0.0;
    if (d > 1e-6) {
        const double denom = std::min(l, 1.0 - l);
        s = (std::fabs(denom) <= 1e-6) ? 0.0 : (mx - l) / denom;
        if (mx == r) h = (g - b) / d + (g < b ? 6.0 : 0.0);
        else if (mx == g) h = (b - r) / d + 2.0;
        else h = (r - g) / d + 4.0;
        h *= 60.0;
    }
    if (s < 0.0) {
        h = normalize_hue(h + 180.0);
        s = -s;
    }
    h = normalize_hue(h);
    hue_powerless = (std::fabs(s) <= 1e-6);
    if (hue_powerless) h = 0.0;
    return {h, s * 100.0, l * 100.0};
}

inline Vec3 hwb_to_srgb(double h_deg, double w_pct, double b_pct) {
    const double w = w_pct / 100.0;
    const double b = b_pct / 100.0;
    if (w + b >= 1.0) {
        const double gray = (w + b == 0.0) ? 0.0 : (w / (w + b));
        return {gray, gray, gray};
    }
    Vec3 rgb = hsl_to_srgb(h_deg, 100.0, 50.0);
    const double factor = 1.0 - w - b;
    return {rgb[0] * factor + w, rgb[1] * factor + w, rgb[2] * factor + w};
}

inline Vec3 srgb_to_hwb(const Vec3& rgb, bool& hue_powerless) {
    const double r = rgb[0], g = rgb[1], b = rgb[2];
    const double mx = std::max({r, g, b});
    const double mn = std::min({r, g, b});
    const double d = mx - mn;
    double h = 0.0;
    if (d != 0.0) {
        if (mx == r) h = (g - b) / d + (g < b ? 6.0 : 0.0);
        else if (mx == g) h = (b - r) / d + 2.0;
        else h = (r - g) / d + 4.0;
        h *= 60.0;
    }
    h = normalize_hue(h);
    const double w = mn * 100.0;
    const double bl = (1.0 - mx) * 100.0;
    hue_powerless = (w + bl >= 100.0 - 1e-6);
    if (hue_powerless) h = 0.0;
    return {h, w, bl};
}

inline Vec3 xyz_d50_to_lab(const Vec3& xyz) {
    auto f = [](double t) {
        return t > kLabEpsilon ? std::cbrt(t) : (kLabKappa * t + 16.0) / 116.0;
    };
    const double f0 = f(xyz[0] / kD50[0]);
    const double f1 = f(xyz[1] / kD50[1]);
    const double f2 = f(xyz[2] / kD50[2]);
    return {116.0 * f1 - 16.0, 500.0 * (f0 - f1), 200.0 * (f1 - f2)};
}

inline Vec3 lab_to_xyz_d50(const Vec3& lab) {
    const double f1 = (lab[0] + 16.0) / 116.0;
    const double f0 = lab[1] / 500.0 + f1;
    const double f2 = f1 - lab[2] / 200.0;
    const double f0_3 = f0 * f0 * f0;
    const double f2_3 = f2 * f2 * f2;
    const double x = f0_3 > kLabEpsilon ? f0_3 : (116.0 * f0 - 16.0) / kLabKappa;
    const double y = lab[0] > kLabKappa * kLabEpsilon ? f1 * f1 * f1 : lab[0] / kLabKappa;
    const double z = f2_3 > kLabEpsilon ? f2_3 : (116.0 * f2 - 16.0) / kLabKappa;
    return {x * kD50[0], y * kD50[1], z * kD50[2]};
}

inline Vec3 xyz_d65_to_oklab(const Vec3& xyz) {
    const Vec3 lms = mat_mul(kXyzD65ToLms, xyz);
    const Vec3 lms_c = {std::cbrt(lms[0]), std::cbrt(lms[1]), std::cbrt(lms[2])};
    return mat_mul(kLmsToOklab, lms_c);
}

inline Vec3 oklab_to_xyz_d65(const Vec3& oklab) {
    const Vec3 lms_c = mat_mul(kOklabToLms, oklab);
    const Vec3 lms = {
        lms_c[0] * lms_c[0] * lms_c[0],
        lms_c[1] * lms_c[1] * lms_c[1],
        lms_c[2] * lms_c[2] * lms_c[2],
    };
    return mat_mul(kLmsToXyzD65, lms);
}

inline Vec3 lab_to_lch(const Vec3& lab, double achromatic_eps, bool& hue_powerless) {
    const double c = std::hypot(lab[1], lab[2]);
    hue_powerless = (c <= achromatic_eps);
    double h = hue_powerless ? 0.0 : normalize_hue(std::atan2(lab[2], lab[1]) * 180.0 / kPi);
    return {lab[0], c, h};
}

inline Vec3 lch_to_lab(const Vec3& lch) {
    const double rad = lch[2] * kPi / 180.0;
    return {lch[0], lch[1] * std::cos(rad), lch[1] * std::sin(rad)};
}

// Convert resolved (no missing) channel triple in `space` to XYZ-D65.
Vec3 to_xyz_d65(ColorSpace space, const Vec3& v) {
    switch (space) {
        case ColorSpace::Srgb:
            return mat_mul(kLinSrgbToXyzD65, {srgb_to_linear_channel(v[0]),
                                              srgb_to_linear_channel(v[1]),
                                              srgb_to_linear_channel(v[2])});
        case ColorSpace::SrgbLinear:
            return mat_mul(kLinSrgbToXyzD65, v);
        case ColorSpace::DisplayP3:
            return mat_mul(kLinP3ToXyzD65, {srgb_to_linear_channel(v[0]),
                                            srgb_to_linear_channel(v[1]),
                                            srgb_to_linear_channel(v[2])});
        case ColorSpace::DisplayP3Linear:
            return mat_mul(kLinP3ToXyzD65, v);
        case ColorSpace::A98Rgb:
            return mat_mul(kLinA98ToXyzD65, {a98_to_linear_channel(v[0]),
                                             a98_to_linear_channel(v[1]),
                                             a98_to_linear_channel(v[2])});
        case ColorSpace::ProphotoRgb: {
            const Vec3 lin = {prophoto_to_linear_channel(v[0]),
                              prophoto_to_linear_channel(v[1]),
                              prophoto_to_linear_channel(v[2])};
            return mat_mul(kXyzD50ToD65, mat_mul(kLinProphotoToXyzD50, lin));
        }
        case ColorSpace::Rec2020:
            return mat_mul(kLinRec2020ToXyzD65, {rec2020_to_linear_channel(v[0]),
                                                 rec2020_to_linear_channel(v[1]),
                                                 rec2020_to_linear_channel(v[2])});
        case ColorSpace::XyzD50:
            return mat_mul(kXyzD50ToD65, v);
        case ColorSpace::XyzD65:
            return v;
        case ColorSpace::Lab:
            return mat_mul(kXyzD50ToD65, lab_to_xyz_d50(v));
        case ColorSpace::Lch:
            return mat_mul(kXyzD50ToD65, lab_to_xyz_d50(lch_to_lab(v)));
        case ColorSpace::Oklab:
            return oklab_to_xyz_d65(v);
        case ColorSpace::Oklch:
            return oklab_to_xyz_d65(lch_to_lab(v));
        case ColorSpace::Hsl: {
            const Vec3 srgb = hsl_to_srgb(v[0], v[1], v[2]);
            return to_xyz_d65(ColorSpace::Srgb, srgb);
        }
        case ColorSpace::Hwb: {
            const Vec3 srgb = hwb_to_srgb(v[0], v[1], v[2]);
            return to_xyz_d65(ColorSpace::Srgb, srgb);
        }
    }
    return {0.0, 0.0, 0.0};
}

Vec3 from_xyz_d65(ColorSpace target, const Vec3& xyz, bool& powerless_0, bool& powerless_2) {
    powerless_0 = false;
    powerless_2 = false;
    switch (target) {
        case ColorSpace::Srgb: {
            const Vec3 lin = mat_mul(kXyzD65ToLinSrgb, xyz);
            return {linear_to_srgb_channel(lin[0]),
                    linear_to_srgb_channel(lin[1]),
                    linear_to_srgb_channel(lin[2])};
        }
        case ColorSpace::SrgbLinear:
            return mat_mul(kXyzD65ToLinSrgb, xyz);
        case ColorSpace::DisplayP3: {
            const Vec3 lin = mat_mul(kXyzD65ToLinP3, xyz);
            return {linear_to_srgb_channel(lin[0]),
                    linear_to_srgb_channel(lin[1]),
                    linear_to_srgb_channel(lin[2])};
        }
        case ColorSpace::DisplayP3Linear:
            return mat_mul(kXyzD65ToLinP3, xyz);
        case ColorSpace::A98Rgb: {
            const Vec3 lin = mat_mul(kXyzD65ToLinA98, xyz);
            return {linear_to_a98_channel(lin[0]),
                    linear_to_a98_channel(lin[1]),
                    linear_to_a98_channel(lin[2])};
        }
        case ColorSpace::ProphotoRgb: {
            const Vec3 d50 = mat_mul(kXyzD65ToD50, xyz);
            const Vec3 lin = mat_mul(kXyzD50ToLinProphoto, d50);
            return {linear_to_prophoto_channel(lin[0]),
                    linear_to_prophoto_channel(lin[1]),
                    linear_to_prophoto_channel(lin[2])};
        }
        case ColorSpace::Rec2020: {
            const Vec3 lin = mat_mul(kXyzD65ToLinRec2020, xyz);
            return {linear_to_rec2020_channel(lin[0]),
                    linear_to_rec2020_channel(lin[1]),
                    linear_to_rec2020_channel(lin[2])};
        }
        case ColorSpace::XyzD50:
            return mat_mul(kXyzD65ToD50, xyz);
        case ColorSpace::XyzD65:
            return xyz;
        case ColorSpace::Lab:
            return xyz_d50_to_lab(mat_mul(kXyzD65ToD50, xyz));
        case ColorSpace::Lch: {
            const Vec3 lab = xyz_d50_to_lab(mat_mul(kXyzD65ToD50, xyz));
            return lab_to_lch(lab, 1e-4, powerless_2);
        }
        case ColorSpace::Oklab:
            return xyz_d65_to_oklab(xyz);
        case ColorSpace::Oklch: {
            const Vec3 oklab = xyz_d65_to_oklab(xyz);
            return lab_to_lch(oklab, 1e-6, powerless_2);
        }
        case ColorSpace::Hsl: {
            const Vec3 lin = mat_mul(kXyzD65ToLinSrgb, xyz);
            const Vec3 srgb = {linear_to_srgb_channel(lin[0]),
                               linear_to_srgb_channel(lin[1]),
                               linear_to_srgb_channel(lin[2])};
            return srgb_to_hsl(srgb, powerless_0);
        }
        case ColorSpace::Hwb: {
            const Vec3 lin = mat_mul(kXyzD65ToLinSrgb, xyz);
            const Vec3 srgb = {linear_to_srgb_channel(lin[0]),
                               linear_to_srgb_channel(lin[1]),
                               linear_to_srgb_channel(lin[2])};
            return srgb_to_hwb(srgb, powerless_0);
        }
    }
    return {0.0, 0.0, 0.0};
}

// Analogous missing-component categories (CSS Color 4 §12.2)
enum class AnalogousCategory : uint8_t {
    None,
    Red,
    Green,
    Blue,
    Lightness,
    Colorfulness,
    Hue,
    OpponentA,
    OpponentB,
};

AnalogousCategory channel_category(ColorSpace space, int index) {
    switch (space) {
        case ColorSpace::Srgb:
        case ColorSpace::SrgbLinear:
        case ColorSpace::DisplayP3:
        case ColorSpace::DisplayP3Linear:
        case ColorSpace::A98Rgb:
        case ColorSpace::ProphotoRgb:
        case ColorSpace::Rec2020:
            if (index == 0) return AnalogousCategory::Red;
            if (index == 1) return AnalogousCategory::Green;
            return AnalogousCategory::Blue;
        case ColorSpace::Lab:
        case ColorSpace::Oklab:
            if (index == 0) return AnalogousCategory::Lightness;
            if (index == 1) return AnalogousCategory::OpponentA;
            return AnalogousCategory::OpponentB;
        case ColorSpace::Lch:
        case ColorSpace::Oklch:
            if (index == 0) return AnalogousCategory::Lightness;
            if (index == 1) return AnalogousCategory::Colorfulness;
            return AnalogousCategory::Hue;
        case ColorSpace::Hsl:
            if (index == 0) return AnalogousCategory::Hue;
            if (index == 1) return AnalogousCategory::Colorfulness;
            return AnalogousCategory::Lightness;
        case ColorSpace::Hwb:
            if (index == 0) return AnalogousCategory::Hue;
            return AnalogousCategory::None;
        case ColorSpace::XyzD50:
        case ColorSpace::XyzD65:
            return AnalogousCategory::None;
    }
    return AnalogousCategory::None;
}

bool is_polar_space(ColorSpace space) {
    return space == ColorSpace::Hsl || space == ColorSpace::Hwb ||
           space == ColorSpace::Lch || space == ColorSpace::Oklch;
}

int hue_channel_index(ColorSpace space) {
    if (space == ColorSpace::Hsl || space == ColorSpace::Hwb) return 0;
    if (space == ColorSpace::Lch || space == ColorSpace::Oklch) return 2;
    return -1;
}

// ============================================================================
// String, Token, and Number Helpers
// ============================================================================

inline bool is_css_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

inline std::string_view trim_ws(std::string_view s) {
    while (!s.empty() && is_css_ws(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_css_ws(s.back())) s.remove_suffix(1);
    return s;
}

inline std::string ascii_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

inline int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parse_func_call(std::string_view text, std::string& lower_name, std::string_view& args) {
    text = trim_ws(text);
    if (text.size() < 3 || text.back() != ')') return false;
    const size_t open = text.find('(');
    if (open == std::string_view::npos || open == 0) return false;
    // Validate function name characters (ASCII letters, digits, '-')
    for (size_t i = 0; i < open; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (!std::isalnum(ch) && ch != '-') return false;
    }
    // Verify the opening '(' matches the final ')' at depth 0
    int depth = 0;
    for (size_t i = open; i < text.size(); ++i) {
        if (text[i] == '(') ++depth;
        else if (text[i] == ')') {
            --depth;
            if (depth < 0) return false;
            if (depth == 0 && i + 1 != text.size()) return false;
        }
    }
    if (depth != 0) return false;
    lower_name = ascii_lower(text.substr(0, open));
    args = text.substr(open + 1, text.size() - open - 2);
    return true;
}

bool split_top_level_commas(std::string_view text, std::vector<std::string_view>& out) {
    out.clear();
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '(') ++depth;
        else if (c == ')') {
            --depth;
            if (depth < 0) return false;
        } else if (c == ',' && depth == 0) {
            out.push_back(trim_ws(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    if (depth != 0) return false;
    out.push_back(trim_ws(text.substr(start)));
    return true;
}

bool has_top_level_comma(std::string_view text) {
    int depth = 0;
    for (char c : text) {
        if (c == '(') ++depth;
        else if (c == ')') --depth;
        else if (c == ',' && depth == 0) return true;
    }
    return false;
}

// Tokenizes space-separated CSS components at depth 0, splitting '/' into its
// own standalone token so `lab(20 0 10/0.5)` and `rgb(255 0 0 / 50%)` parse identically.
bool tokenize_components(std::string_view text, std::vector<std::string_view>& out) {
    out.clear();
    int depth = 0;
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        while (i < n && is_css_ws(text[i])) ++i;
        if (i >= n) break;
        if (text[i] == '/') {
            out.push_back(text.substr(i, 1));
            ++i;
            continue;
        }
        const size_t start = i;
        while (i < n) {
            const char c = text[i];
            if (c == '(') {
                ++depth;
            } else if (c == ')') {
                --depth;
                if (depth < 0) return false;
            } else if (depth == 0 && (is_css_ws(c) || c == '/')) {
                break;
            }
            ++i;
        }
        if (depth != 0) return false;
        out.push_back(text.substr(start, i - start));
    }
    return depth == 0;
}

// Formats a floating-point number per CSS Color 4 §15 (clean shortest decimal,
// no "-0", no scientific notation for near-zero values).
std::string format_css_double(double v, int max_decimals = 6) {
    if (std::isnan(v)) return "0";
    if (std::isinf(v)) return v > 0.0 ? "calc(infinity)" : "calc(-infinity)";
    if (std::fabs(v) < 1e-7) return "0";
    // Round to `max_decimals` decimal places to eliminate binary FP noise like 0.30000000000000004
    const double scale = std::pow(10.0, max_decimals);
    double r = std::round(v * scale) / scale;
    if (std::fabs(r) < 1e-7) return "0";
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(max_decimals) << r;
    std::string s = oss.str();
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0" || s.empty()) return "0";
    return s;
}

std::string format_legacy_rgb(double r01, double g01, double b01, double alpha, bool hex_alpha_hint = false) {
    auto ch8 = [](double v01) -> std::string {
        if (!std::isfinite(v01)) v01 = 0.0;
        const long clamped = std::clamp(std::lround(std::clamp(v01, 0.0, 1.0) * 255.0 + 1e-12), 0L, 255L);
        return std::to_string(clamped);
    };
    if (!std::isfinite(alpha)) alpha = 0.0;
    alpha = std::clamp(alpha, 0.0, 1.0);
    if (alpha >= 1.0) {
        return "rgb(" + ch8(r01) + ", " + ch8(g01) + ", " + ch8(b01) + ")";
    }
    double a_out = alpha;
    if (hex_alpha_hint) {
        const double a8 = std::round(alpha * 255.0);
        const double hundredths = std::round(a8 * 100.0 / 255.0);
        a_out = (std::round(hundredths * 255.0 / 100.0) == a8)
                    ? (hundredths / 100.0)
                    : (std::round(a8 * 1000.0 / 255.0) / 1000.0);
    } else {
        a_out = std::round(alpha * 1000.0) / 1000.0;
    }
    if (a_out >= 1.0) {
        return "rgb(" + ch8(r01) + ", " + ch8(g01) + ", " + ch8(b01) + ")";
    }
    return "rgba(" + ch8(r01) + ", " + ch8(g01) + ", " + ch8(b01) + ", " + format_css_double(a_out, 3) + ")";
}

// ============================================================================
// CSS Values 4 Math Expression Evaluator (for color channels & relative colors)
// ============================================================================

enum class MathKind : uint8_t {
    Number,
    Percent,
    Angle, // degrees
};

struct MathVal {
    double value{0.0};
    MathKind kind{MathKind::Number};
};

struct ChannelEnv {
    bool active{false};
    const char* names[4]{nullptr, nullptr, nullptr, nullptr};
    double values[4]{0.0, 0.0, 0.0, 0.0};
    bool missing[4]{false, false, false, false};

    bool lookup(std::string_view lower_ident, double& out_val, bool& out_missing) const {
        if (!active) return false;
        for (int i = 0; i < 4; ++i) {
            if (names[i] && lower_ident == names[i]) {
                out_val = missing[i] ? 0.0 : values[i];
                out_missing = missing[i];
                return true;
            }
        }
        return false;
    }
};

class MathParser {
public:
    MathParser(std::string_view src, const ChannelEnv& env, int depth)
        : m_src(src), m_env(env), m_depth(depth) {}

    bool parse_full(MathVal& out) {
        if (m_depth > kMaxMathDepth) return false;
        skip_ws();
        if (!parse_sum(out)) return false;
        skip_ws();
        return m_pos == m_src.size();
    }

private:
    std::string_view m_src;
    const ChannelEnv& m_env;
    int m_depth{0};
    size_t m_pos{0};

    void skip_ws() {
        while (m_pos < m_src.size() && is_css_ws(m_src[m_pos])) ++m_pos;
    }

    bool match_char(char c) {
        skip_ws();
        if (m_pos < m_src.size() && m_src[m_pos] == c) {
            ++m_pos;
            return true;
        }
        return false;
    }

    bool parse_sum(MathVal& out) {
        if (!parse_product(out)) return false;
        while (true) {
            // In CSS calc(), '+' and '-' binary operators must be surrounded by whitespace
            const size_t saved = m_pos;
            const bool ws_before = (m_pos < m_src.size() && is_css_ws(m_src[m_pos]));
            skip_ws();
            if (m_pos >= m_src.size()) {
                m_pos = saved;
                break;
            }
            const char op = m_src[m_pos];
            if ((op != '+' && op != '-') || !ws_before) {
                m_pos = saved;
                break;
            }
            if (m_pos + 1 >= m_src.size() || !is_css_ws(m_src[m_pos + 1])) {
                m_pos = saved;
                break;
            }
            ++m_pos;
            MathVal rhs;
            if (!parse_product(rhs)) return false;
            if (out.kind != rhs.kind) return false;
            out.value = (op == '+') ? (out.value + rhs.value) : (out.value - rhs.value);
        }
        return true;
    }

    bool parse_product(MathVal& out) {
        if (!parse_unary(out)) return false;
        while (true) {
            const size_t saved = m_pos;
            skip_ws();
            if (m_pos >= m_src.size()) {
                m_pos = saved;
                break;
            }
            const char op = m_src[m_pos];
            if (op != '*' && op != '/') {
                m_pos = saved;
                break;
            }
            ++m_pos;
            MathVal rhs;
            if (!parse_unary(rhs)) return false;
            if (op == '*') {
                if (out.kind != MathKind::Number && rhs.kind != MathKind::Number) return false;
                out.kind = (out.kind != MathKind::Number) ? out.kind : rhs.kind;
                out.value = out.value * rhs.value;
            } else {
                if (rhs.kind == MathKind::Number) {
                    out.value = out.value / rhs.value;
                } else if (out.kind == rhs.kind) {
                    out.value = out.value / rhs.value;
                    out.kind = MathKind::Number;
                } else {
                    return false;
                }
            }
        }
        return true;
    }

    bool parse_unary(MathVal& out) {
        skip_ws();
        if (m_pos >= m_src.size()) return false;
        if (m_src[m_pos] == '(') {
            if (m_depth + 1 > kMaxMathDepth) return false;
            ++m_pos;
            const std::string_view inner = extract_parens();
            if (!m_parens_ok) return false;
            MathParser sub(inner, m_env, m_depth + 1);
            if (!sub.parse_full(out)) return false;
            return true;
        }
        // Check for `-infinity` / `+infinity` or signed number
        if ((m_src[m_pos] == '+' || m_src[m_pos] == '-') && m_pos + 1 < m_src.size()) {
            const char next = m_src[m_pos + 1];
            if (std::isalpha(static_cast<unsigned char>(next))) {
                const bool neg = (m_src[m_pos] == '-');
                const size_t saved = m_pos;
                ++m_pos;
                std::string ident = read_ident();
                const std::string lower = ascii_lower(ident);
                if (lower == "infinity") {
                    out = {neg ? -std::numeric_limits<double>::infinity()
                               : std::numeric_limits<double>::infinity(),
                           MathKind::Number};
                    return true;
                }
                m_pos = saved;
                return false;
            }
        }
        // Number / percentage / dimension
        if (std::isdigit(static_cast<unsigned char>(m_src[m_pos])) ||
            m_src[m_pos] == '.' || m_src[m_pos] == '+' || m_src[m_pos] == '-') {
            return parse_number_or_dim(out);
        }
        // Identifier or function call
        if (std::isalpha(static_cast<unsigned char>(m_src[m_pos]))) {
            const std::string ident = ascii_lower(read_ident());
            if (m_pos < m_src.size() && m_src[m_pos] == '(') {
                ++m_pos;
                const std::string_view arg_text = extract_parens();
                if (!m_parens_ok) return false;
                return eval_math_func(ident, arg_text, out);
            }
            // Constants & relative-color channel variables
            if (ident == "pi") { out = {kPi, MathKind::Number}; return true; }
            if (ident == "e") { out = {kE, MathKind::Number}; return true; }
            if (ident == "infinity") { out = {std::numeric_limits<double>::infinity(), MathKind::Number}; return true; }
            if (ident == "nan") { out = {std::numeric_limits<double>::quiet_NaN(), MathKind::Number}; return true; }
            double ch_val = 0.0;
            bool ch_missing = false;
            if (m_env.lookup(ident, ch_val, ch_missing)) {
                out = {ch_val, MathKind::Number};
                return true;
            }
            return false;
        }
        return false;
    }

    bool m_parens_ok{true};

    // Assumes m_pos is just after '('. Advances m_pos past the matching ')'.
    std::string_view extract_parens() {
        m_parens_ok = false;
        const size_t start = m_pos;
        int depth = 1;
        while (m_pos < m_src.size() && depth > 0) {
            if (m_src[m_pos] == '(') ++depth;
            else if (m_src[m_pos] == ')') --depth;
            ++m_pos;
        }
        if (depth != 0) return {};
        m_parens_ok = true;
        return m_src.substr(start, m_pos - start - 1);
    }

    std::string read_ident() {
        const size_t start = m_pos;
        while (m_pos < m_src.size()) {
            const unsigned char c = static_cast<unsigned char>(m_src[m_pos]);
            if (std::isalnum(c) || c == '-' || c == '_') ++m_pos;
            else break;
        }
        return std::string(m_src.substr(start, m_pos - start));
    }

    bool parse_number_or_dim(MathVal& out) {
        const size_t start = m_pos;
        if (m_src[m_pos] == '+' || m_src[m_pos] == '-') ++m_pos;
        bool has_digits = false;
        while (m_pos < m_src.size() && std::isdigit(static_cast<unsigned char>(m_src[m_pos]))) {
            has_digits = true;
            ++m_pos;
        }
        if (m_pos < m_src.size() && m_src[m_pos] == '.') {
            ++m_pos;
            while (m_pos < m_src.size() && std::isdigit(static_cast<unsigned char>(m_src[m_pos]))) {
                has_digits = true;
                ++m_pos;
            }
        }
        if (!has_digits) return false;
        if (m_pos < m_src.size() && (m_src[m_pos] == 'e' || m_src[m_pos] == 'E')) {
            const size_t exp_pos = m_pos;
            ++m_pos;
            if (m_pos < m_src.size() && (m_src[m_pos] == '+' || m_src[m_pos] == '-')) ++m_pos;
            bool exp_digits = false;
            while (m_pos < m_src.size() && std::isdigit(static_cast<unsigned char>(m_src[m_pos]))) {
                exp_digits = true;
                ++m_pos;
            }
            if (!exp_digits) m_pos = exp_pos; // backtrack if 'e' starts a unit
        }
        std::string num_str(m_src.substr(start, m_pos - start));
        char* endp = nullptr;
        const double val = std::strtod(num_str.c_str(), &endp);
        if (endp == num_str.c_str()) return false;

        if (m_pos < m_src.size() && m_src[m_pos] == '%') {
            ++m_pos;
            out = {val, MathKind::Percent};
            return true;
        }
        if (m_pos < m_src.size() && std::isalpha(static_cast<unsigned char>(m_src[m_pos]))) {
            const std::string unit = ascii_lower(read_ident());
            if (unit == "deg") { out = {val, MathKind::Angle}; return true; }
            if (unit == "grad") { out = {val * 0.9, MathKind::Angle}; return true; }
            if (unit == "rad") { out = {val * 180.0 / kPi, MathKind::Angle}; return true; }
            if (unit == "turn") { out = {val * 360.0, MathKind::Angle}; return true; }
            return false; // unknown/invalid unit in color expression (e.g. px)
        }
        out = {val, MathKind::Number};
        return true;
    }

    bool eval_sub(std::string_view expr, MathVal& out) const {
        if (m_depth + 1 > kMaxMathDepth) return false;
        MathParser sub(expr, m_env, m_depth + 1);
        return sub.parse_full(out);
    }

    bool eval_math_func(const std::string& fn, std::string_view arg_text, MathVal& out) const {
        if (m_depth + 1 > kMaxMathDepth) return false;
        if (fn == "calc") {
            return eval_sub(arg_text, out);
        }
        std::vector<std::string_view> parts;
        if (!split_top_level_commas(arg_text, parts) || parts.empty()) return false;
        for (auto p : parts) {
            if (p.empty()) return false;
        }

        if (fn == "min" || fn == "max") {
            MathVal acc;
            if (!eval_sub(parts[0], acc)) return false;
            for (size_t i = 1; i < parts.size(); ++i) {
                MathVal v;
                if (!eval_sub(parts[i], v) || v.kind != acc.kind) return false;
                if (std::isnan(acc.value) || std::isnan(v.value)) {
                    acc.value = std::numeric_limits<double>::quiet_NaN();
                } else {
                    acc.value = (fn == "min") ? std::min(acc.value, v.value) : std::max(acc.value, v.value);
                }
            }
            out = acc;
            return true;
        }
        if (fn == "clamp") {
            if (parts.size() != 3) return false;
            MathVal lo, v, hi;
            if (!eval_sub(parts[0], lo) || !eval_sub(parts[1], v) || !eval_sub(parts[2], hi)) return false;
            if (lo.kind != v.kind || hi.kind != v.kind) return false;
            if (std::isnan(lo.value) || std::isnan(v.value) || std::isnan(hi.value)) {
                out = {std::numeric_limits<double>::quiet_NaN(), v.kind};
            } else {
                out = {std::max(lo.value, std::min(v.value, hi.value)), v.kind};
            }
            return true;
        }
        if (fn == "abs") {
            if (parts.size() != 1) return false;
            MathVal v;
            if (!eval_sub(parts[0], v)) return false;
            out = {std::fabs(v.value), v.kind};
            return true;
        }
        if (fn == "sign") {
            if (parts.size() != 1) return false;
            MathVal v;
            if (!eval_sub(parts[0], v)) return false;
            double s = 0.0;
            if (std::isnan(v.value)) s = v.value;
            else if (v.value > 0.0) s = 1.0;
            else if (v.value < 0.0) s = -1.0;
            out = {s, MathKind::Number};
            return true;
        }
        if (fn == "sin" || fn == "cos" || fn == "tan") {
            if (parts.size() != 1) return false;
            MathVal v;
            if (!eval_sub(parts[0], v)) return false;
            if (v.kind == MathKind::Percent) return false;
            const double rad = (v.kind == MathKind::Angle) ? (v.value * kPi / 180.0) : v.value;
            double r = 0.0;
            if (fn == "sin") r = std::sin(rad);
            else if (fn == "cos") r = std::cos(rad);
            else r = std::tan(rad);
            out = {r, MathKind::Number};
            return true;
        }
        if (fn == "asin" || fn == "acos" || fn == "atan") {
            if (parts.size() != 1) return false;
            MathVal v;
            if (!eval_sub(parts[0], v) || v.kind != MathKind::Number) return false;
            double r = 0.0;
            if (fn == "asin") r = std::asin(v.value);
            else if (fn == "acos") r = std::acos(v.value);
            else r = std::atan(v.value);
            out = {r * 180.0 / kPi, MathKind::Angle};
            return true;
        }
        if (fn == "atan2") {
            if (parts.size() != 2) return false;
            MathVal y, x;
            if (!eval_sub(parts[0], y) || !eval_sub(parts[1], x) || y.kind != x.kind) return false;
            out = {std::atan2(y.value, x.value) * 180.0 / kPi, MathKind::Angle};
            return true;
        }
        if (fn == "sqrt") {
            if (parts.size() != 1) return false;
            MathVal v;
            if (!eval_sub(parts[0], v) || v.kind != MathKind::Number) return false;
            out = {std::sqrt(v.value), MathKind::Number};
            return true;
        }
        if (fn == "exp") {
            if (parts.size() != 1) return false;
            MathVal v;
            if (!eval_sub(parts[0], v) || v.kind != MathKind::Number) return false;
            out = {std::exp(v.value), MathKind::Number};
            return true;
        }
        if (fn == "pow") {
            if (parts.size() != 2) return false;
            MathVal a, b;
            if (!eval_sub(parts[0], a) || !eval_sub(parts[1], b)) return false;
            if (a.kind != MathKind::Number || b.kind != MathKind::Number) return false;
            out = {std::pow(a.value, b.value), MathKind::Number};
            return true;
        }
        if (fn == "log") {
            if (parts.size() != 1 && parts.size() != 2) return false;
            MathVal a;
            if (!eval_sub(parts[0], a) || a.kind != MathKind::Number) return false;
            if (parts.size() == 1) {
                out = {std::log(a.value), MathKind::Number};
                return true;
            }
            MathVal b;
            if (!eval_sub(parts[1], b) || b.kind != MathKind::Number) return false;
            out = {std::log(a.value) / std::log(b.value), MathKind::Number};
            return true;
        }
        if (fn == "hypot") {
            MathVal first;
            if (!eval_sub(parts[0], first)) return false;
            double sum = first.value * first.value;
            for (size_t i = 1; i < parts.size(); ++i) {
                MathVal v;
                if (!eval_sub(parts[i], v) || v.kind != first.kind) return false;
                sum += v.value * v.value;
            }
            out = {std::sqrt(sum), first.kind};
            return true;
        }
        if (fn == "mod" || fn == "rem") {
            if (parts.size() != 2) return false;
            MathVal a, b;
            if (!eval_sub(parts[0], a) || !eval_sub(parts[1], b) || a.kind != b.kind) return false;
            double r = std::fmod(a.value, b.value);
            if (fn == "mod" && std::isfinite(r) && b.value != 0.0) {
                if ((r < 0.0 && b.value > 0.0) || (r > 0.0 && b.value < 0.0)) r += b.value;
            }
            out = {r, a.kind};
            return true;
        }
        if (fn == "round") {
            if (parts.size() < 1 || parts.size() > 3) return false;
            std::string strategy = "nearest";
            size_t idx = 0;
            const std::string first_low = ascii_lower(parts[0]);
            if (first_low == "nearest" || first_low == "up" || first_low == "down" || first_low == "to-zero") {
                strategy = first_low;
                idx = 1;
            }
            if (idx >= parts.size()) return false;
            MathVal a;
            if (!eval_sub(parts[idx], a)) return false;
            MathVal step = {1.0, a.kind};
            if (idx + 1 < parts.size()) {
                if (!eval_sub(parts[idx + 1], step) || step.kind != a.kind) return false;
            } else if (a.kind != MathKind::Number) {
                return false;
            }
            const double b = std::fabs(step.value);
            if (b == 0.0) {
                out = {std::numeric_limits<double>::quiet_NaN(), a.kind};
                return true;
            }
            double q = a.value / b;
            double rq = 0.0;
            if (strategy == "up") rq = std::ceil(q);
            else if (strategy == "down") rq = std::floor(q);
            else if (strategy == "to-zero") rq = std::trunc(q);
            else rq = std::floor(q + 0.5);
            out = {rq * b, a.kind};
            return true;
        }
        return false;
    }
};

// Evaluates a single color component token (may be `none`, a channel keyword,
// a bare number/percentage/angle, or a CSS math function).
struct ComponentSpec {
    double pct_ref{1.0};
    bool allow_none{true};
    bool allow_number{true};
    bool allow_percent{true};
    bool allow_angle{false};
};

struct ParsedComponent {
    double value{0.0};
    bool is_none{false};
    MathKind kind{MathKind::Number};
};

bool parse_component(std::string_view token, const ComponentSpec& spec,
                     const ChannelEnv& env, ParsedComponent& out) {
    token = trim_ws(token);
    if (token.empty()) return false;
    const std::string lower = ascii_lower(token);
    if (lower == "none") {
        if (!spec.allow_none) return false;
        out = {0.0, true, MathKind::Number};
        return true;
    }
    // Check bare relative-color channel keyword FIRST so missing components stay `none`!
    double env_val = 0.0;
    bool env_missing = false;
    if (env.lookup(lower, env_val, env_missing)) {
        out = {env_val, env_missing, MathKind::Number};
        return true;
    }
    MathParser mp(token, env, 0);
    MathVal mv;
    if (!mp.parse_full(mv)) return false;

    if (mv.kind == MathKind::Number) {
        if (!spec.allow_number) return false;
        out = {mv.value, false, MathKind::Number};
        return true;
    }
    if (mv.kind == MathKind::Percent) {
        if (!spec.allow_percent) return false;
        out = {(mv.value / 100.0) * spec.pct_ref, false, MathKind::Percent};
        return true;
    }
    if (mv.kind == MathKind::Angle) {
        if (!spec.allow_angle) return false;
        out = {mv.value, false, MathKind::Angle};
        return true;
    }
    return false;
}

inline double sanitize_nan_to_zero(double v) {
    return std::isnan(v) ? 0.0 : v;
}

inline double clamp_channel(double v, double lo, double hi) {
    if (std::isnan(v)) return lo;
    return std::clamp(v, lo, hi);
}

inline double clamp_min(double v, double lo) {
    if (std::isnan(v)) return lo;
    return v < lo ? lo : v;
}

// ============================================================================
// Recursive CSS Color 4 / 5 Parser
// ============================================================================

bool parse_color_impl(std::string_view text, CssColor& out, const ColorResolveContext& ctx, int depth);

bool parse_predefined_space(const std::string& lower, ColorSpace& out) {
    if (lower == "srgb") { out = ColorSpace::Srgb; return true; }
    if (lower == "srgb-linear") { out = ColorSpace::SrgbLinear; return true; }
    if (lower == "display-p3") { out = ColorSpace::DisplayP3; return true; }
    if (lower == "display-p3-linear") { out = ColorSpace::DisplayP3Linear; return true; }
    if (lower == "a98-rgb") { out = ColorSpace::A98Rgb; return true; }
    if (lower == "prophoto-rgb") { out = ColorSpace::ProphotoRgb; return true; }
    if (lower == "rec2020") { out = ColorSpace::Rec2020; return true; }
    if (lower == "xyz" || lower == "xyz-d65") { out = ColorSpace::XyzD65; return true; }
    if (lower == "xyz-d50") { out = ColorSpace::XyzD50; return true; }
    return false;
}

bool parse_interpolation_space(const std::string& lower, ColorSpace& out) {
    if (parse_predefined_space(lower, out)) return true;
    if (lower == "lab") { out = ColorSpace::Lab; return true; }
    if (lower == "lch") { out = ColorSpace::Lch; return true; }
    if (lower == "oklab") { out = ColorSpace::Oklab; return true; }
    if (lower == "oklch") { out = ColorSpace::Oklch; return true; }
    if (lower == "hsl") { out = ColorSpace::Hsl; return true; }
    if (lower == "hwb") { out = ColorSpace::Hwb; return true; }
    return false;
}

// Splits modern space-separated args `c0 c1 c2 [ / alpha ]`, enforcing exact arity
// and rejecting misplaced slashes or extra tokens.
bool split_modern_3_and_alpha(const std::vector<std::string_view>& toks, size_t offset,
                              std::string_view ch_toks[3], bool& has_alpha,
                              std::string_view& alpha_tok) {
    const size_t rem = toks.size() - offset;
    if (rem != 3 && rem != 5) return false;
    for (size_t i = 0; i < 3; ++i) {
        if (toks[offset + i] == "/") return false;
        ch_toks[i] = toks[offset + i];
    }
    if (rem == 5) {
        if (toks[offset + 3] != "/" || toks[offset + 4] == "/") return false;
        has_alpha = true;
        alpha_tok = toks[offset + 4];
    } else {
        has_alpha = false;
        alpha_tok = {};
    }
    return true;
}

// Builds a ChannelEnv for CSS Color 5 relative color syntax from an origin color.
ChannelEnv make_relative_env(const CssColor& origin, ColorSpace target_space,
                             const char* n0, const char* n1, const char* n2) {
    CssColor conv = origin.convert_to(target_space);
    ChannelEnv env;
    env.active = true;
    env.names[0] = n0;
    env.names[1] = n1;
    env.names[2] = n2;
    env.names[3] = "alpha";
    for (int i = 0; i < 3; ++i) {
        env.values[i] = conv.c[i];
        env.missing[i] = conv.missing[i];
    }
    // For rgb(from ...), channel keywords r/g/b are in [0..255], whereas CssColor::Srgb stores [0..1].
    if (target_space == ColorSpace::Srgb && std::string_view(n0) == "r" &&
        std::string_view(n1) == "g" && std::string_view(n2) == "b") {
        // Caller adjusts scaling for rgb() vs color(srgb) explicitly!
    }
    env.values[3] = conv.alpha;
    env.missing[3] = conv.alpha_missing;
    return env;
}

bool parse_rgb_func(std::string_view args, CssColor& out, const ColorResolveContext& ctx, int depth) {
    if (has_top_level_comma(args)) {
        // Legacy comma-separated syntax: rgb(r, g, b [, a])
        std::vector<std::string_view> parts;
        if (!split_top_level_commas(args, parts)) return false;
        if (parts.size() != 3 && parts.size() != 4) return false;
        const ChannelEnv empty_env;
        const ComponentSpec ch_spec{255.0, false, true, true, false};
        ParsedComponent ch[3];
        for (int i = 0; i < 3; ++i) {
            if (!parse_component(parts[i], ch_spec, empty_env, ch[i])) return false;
        }
        // CSS Color 4 §5.1: legacy comma syntax forbids mixing numbers and percentages
        if (ch[0].kind != ch[1].kind || ch[1].kind != ch[2].kind) return false;
        double alpha = 1.0;
        if (parts.size() == 4) {
            const ComponentSpec a_spec{1.0, false, true, true, false};
            ParsedComponent pa;
            if (!parse_component(parts[3], a_spec, empty_env, pa)) return false;
            alpha = clamp_channel(pa.value, 0.0, 1.0);
        }
        out = CssColor{};
        out.space = ColorSpace::Srgb;
        out.legacy_srgb = true;
        for (int i = 0; i < 3; ++i) {
            out.c[i] = clamp_channel(ch[i].value, 0.0, 255.0) / 255.0;
        }
        out.alpha = alpha;
        return true;
    }

    // Modern space-separated syntax or relative color syntax `rgb(from <origin> r g b [/ a])`
    std::vector<std::string_view> toks;
    if (!tokenize_components(args, toks) || toks.size() < 3) return false;

    const bool is_relative = (ascii_lower(toks[0]) == "from");
    size_t offset = 0;
    ChannelEnv env;
    double default_alpha = 1.0;
    bool default_alpha_missing = false;

    if (is_relative) {
        if (toks.size() < 5) return false;
        CssColor origin;
        if (!parse_color_impl(toks[1], origin, ctx, depth + 1)) return false;
        if (origin.is_currentcolor) return false;
        env = make_relative_env(origin, ColorSpace::Srgb, "r", "g", "b");
        for (int i = 0; i < 3; ++i) env.values[i] *= 255.0;
        default_alpha = env.values[3];
        default_alpha_missing = env.missing[3];
        offset = 2;
    }

    std::string_view ch_toks[3];
    bool has_alpha = false;
    std::string_view alpha_tok;
    if (!split_modern_3_and_alpha(toks, offset, ch_toks, has_alpha, alpha_tok)) return false;

    const ComponentSpec ch_spec{255.0, true, true, true, false};
    ParsedComponent ch[3];
    for (int i = 0; i < 3; ++i) {
        if (!parse_component(ch_toks[i], ch_spec, env, ch[i])) return false;
    }

    double alpha = default_alpha;
    bool alpha_missing = default_alpha_missing;
    if (has_alpha) {
        const ComponentSpec a_spec{1.0, true, true, true, false};
        ParsedComponent pa;
        if (!parse_component(alpha_tok, a_spec, env, pa)) return false;
        alpha_missing = pa.is_none;
        alpha = pa.is_none ? 0.0 : clamp_channel(pa.value, 0.0, 1.0);
    }

    const bool has_any_none = ch[0].is_none || ch[1].is_none || ch[2].is_none || alpha_missing;
    out = CssColor{};
    out.space = ColorSpace::Srgb;
    out.legacy_srgb = !is_relative && !has_any_none;
    for (int i = 0; i < 3; ++i) {
        if (is_relative) {
            out.missing[i] = ch[i].is_none;
            out.c[i] = ch[i].is_none ? 0.0 : sanitize_nan_to_zero(ch[i].value) / 255.0;
        } else {
            out.missing[i] = ch[i].is_none;
            out.c[i] = ch[i].is_none ? 0.0 : clamp_channel(ch[i].value, 0.0, 255.0) / 255.0;
        }
    }
    out.alpha = alpha;
    out.alpha_missing = alpha_missing;
    return true;
}

bool parse_hsl_func(std::string_view args, CssColor& out, const ColorResolveContext& ctx, int depth) {
    if (has_top_level_comma(args)) {
        // Legacy comma-separated syntax: hsl(H, S%, L% [, A])
        std::vector<std::string_view> parts;
        if (!split_top_level_commas(args, parts)) return false;
        if (parts.size() != 3 && parts.size() != 4) return false;
        const ChannelEnv empty_env;
        ParsedComponent ph, ps, pl;
        if (!parse_component(parts[0], {1.0, false, true, false, true}, empty_env, ph)) return false;
        if (!parse_component(parts[1], {100.0, false, false, true, false}, empty_env, ps)) return false;
        if (!parse_component(parts[2], {100.0, false, false, true, false}, empty_env, pl)) return false;
        double alpha = 1.0;
        if (parts.size() == 4) {
            ParsedComponent pa;
            if (!parse_component(parts[3], {1.0, false, true, true, false}, empty_env, pa)) return false;
            alpha = clamp_channel(pa.value, 0.0, 1.0);
        }
        const double h = normalize_hue(sanitize_nan_to_zero(ph.value));
        const double s = clamp_channel(ps.value, 0.0, 100.0);
        const double l = clamp_channel(pl.value, 0.0, 100.0);
        out = CssColor{};
        out.space = ColorSpace::Hsl;
        out.legacy_srgb = true;
        out.c[0] = h;
        out.c[1] = s;
        out.c[2] = l;
        out.alpha = alpha;
        return true;
    }

    // Modern space-separated syntax or relative `hsl(from <origin> h s l [/ a])`
    std::vector<std::string_view> toks;
    if (!tokenize_components(args, toks) || toks.size() < 3) return false;
    const bool is_relative = (ascii_lower(toks[0]) == "from");
    size_t offset = 0;
    ChannelEnv env;
    double default_alpha = 1.0;
    bool default_alpha_missing = false;

    if (is_relative) {
        if (toks.size() < 5) return false;
        CssColor origin;
        if (!parse_color_impl(toks[1], origin, ctx, depth + 1)) return false;
        if (origin.is_currentcolor) return false;
        env = make_relative_env(origin, ColorSpace::Hsl, "h", "s", "l");
        default_alpha = env.values[3];
        default_alpha_missing = env.missing[3];
        offset = 2;
    }

    std::string_view ch_toks[3];
    bool has_alpha = false;
    std::string_view alpha_tok;
    if (!split_modern_3_and_alpha(toks, offset, ch_toks, has_alpha, alpha_tok)) return false;

    ParsedComponent ph, ps, pl;
    if (!parse_component(ch_toks[0], {1.0, true, true, false, true}, env, ph)) return false;
    if (!parse_component(ch_toks[1], {100.0, true, true, true, false}, env, ps)) return false;
    if (!parse_component(ch_toks[2], {100.0, true, true, true, false}, env, pl)) return false;

    double alpha = default_alpha;
    bool alpha_missing = default_alpha_missing;
    if (has_alpha) {
        ParsedComponent pa;
        if (!parse_component(alpha_tok, {1.0, true, true, true, false}, env, pa)) return false;
        alpha_missing = pa.is_none;
        alpha = pa.is_none ? 0.0 : clamp_channel(pa.value, 0.0, 1.0);
    }

    const double h = ph.is_none ? 0.0 : normalize_hue(sanitize_nan_to_zero(ph.value));
    const double s = ps.is_none ? 0.0 : (is_relative ? clamp_min(ps.value, 0.0) : clamp_channel(ps.value, 0.0, 100.0));
    const double l = pl.is_none ? 0.0 : (is_relative ? sanitize_nan_to_zero(pl.value) : clamp_channel(pl.value, 0.0, 100.0));
    const bool has_any_none = ph.is_none || ps.is_none || pl.is_none || alpha_missing;

    if (!is_relative || has_any_none) {
        out = CssColor{};
        out.space = ColorSpace::Hsl;
        out.legacy_srgb = !is_relative && !has_any_none;
        out.c[0] = h;
        out.c[1] = s;
        out.c[2] = l;
        out.missing[0] = ph.is_none;
        out.missing[1] = ps.is_none;
        out.missing[2] = pl.is_none;
        out.alpha = alpha;
        out.alpha_missing = alpha_missing;
        return true;
    }

    const Vec3 rgb = hsl_to_srgb(h, s, l);

    out = CssColor{};
    out.space = ColorSpace::Srgb;
    out.legacy_srgb = false;
    for (int i = 0; i < 3; ++i) {
        out.c[i] = rgb[i];
    }
    out.alpha = alpha;
    out.alpha_missing = alpha_missing;
    return true;
}

bool parse_hwb_func(std::string_view args, CssColor& out, const ColorResolveContext& ctx, int depth) {
    // CSS Color 4 §8: hwb() never accepts legacy comma syntax
    if (has_top_level_comma(args)) return false;
    std::vector<std::string_view> toks;
    if (!tokenize_components(args, toks) || toks.size() < 3) return false;
    const bool is_relative = (ascii_lower(toks[0]) == "from");
    size_t offset = 0;
    ChannelEnv env;
    double default_alpha = 1.0;
    bool default_alpha_missing = false;

    if (is_relative) {
        if (toks.size() < 5) return false;
        CssColor origin;
        if (!parse_color_impl(toks[1], origin, ctx, depth + 1)) return false;
        if (origin.is_currentcolor) return false;
        env = make_relative_env(origin, ColorSpace::Hwb, "h", "w", "b");
        default_alpha = env.values[3];
        default_alpha_missing = env.missing[3];
        offset = 2;
    }

    std::string_view ch_toks[3];
    bool has_alpha = false;
    std::string_view alpha_tok;
    if (!split_modern_3_and_alpha(toks, offset, ch_toks, has_alpha, alpha_tok)) return false;

    ParsedComponent ph, pw, pb;
    if (!parse_component(ch_toks[0], {1.0, true, true, false, true}, env, ph)) return false;
    if (!parse_component(ch_toks[1], {100.0, true, true, true, false}, env, pw)) return false;
    if (!parse_component(ch_toks[2], {100.0, true, true, true, false}, env, pb)) return false;

    double alpha = default_alpha;
    bool alpha_missing = default_alpha_missing;
    if (has_alpha) {
        ParsedComponent pa;
        if (!parse_component(alpha_tok, {1.0, true, true, true, false}, env, pa)) return false;
        alpha_missing = pa.is_none;
        alpha = pa.is_none ? 0.0 : clamp_channel(pa.value, 0.0, 1.0);
    }

    const double h = ph.is_none ? 0.0 : normalize_hue(sanitize_nan_to_zero(ph.value));
    const double w = pw.is_none ? 0.0 : sanitize_nan_to_zero(pw.value);
    const double b = pb.is_none ? 0.0 : sanitize_nan_to_zero(pb.value);
    const bool has_any_none = ph.is_none || pw.is_none || pb.is_none || alpha_missing;

    if (!is_relative || has_any_none) {
        out = CssColor{};
        out.space = ColorSpace::Hwb;
        out.legacy_srgb = !is_relative && !has_any_none;
        out.c[0] = h;
        out.c[1] = w;
        out.c[2] = b;
        out.missing[0] = ph.is_none;
        out.missing[1] = pw.is_none;
        out.missing[2] = pb.is_none;
        out.alpha = alpha;
        out.alpha_missing = alpha_missing;
        return true;
    }

    const Vec3 rgb = hwb_to_srgb(h, w, b);

    out = CssColor{};
    out.space = ColorSpace::Srgb;
    out.legacy_srgb = false;
    for (int i = 0; i < 3; ++i) {
        out.c[i] = rgb[i];
    }
    out.alpha = alpha;
    out.alpha_missing = alpha_missing;
    return true;
}

bool parse_lab_like_func(ColorSpace space, std::string_view args, CssColor& out,
                         const ColorResolveContext& ctx, int depth) {
    if (has_top_level_comma(args)) return false;
    std::vector<std::string_view> toks;
    if (!tokenize_components(args, toks) || toks.size() < 3) return false;
    const bool is_relative = (ascii_lower(toks[0]) == "from");
    size_t offset = 0;
    ChannelEnv env;
    double default_alpha = 1.0;
    bool default_alpha_missing = false;

    const bool is_ok = (space == ColorSpace::Oklab || space == ColorSpace::Oklch);
    const bool is_polar = (space == ColorSpace::Lch || space == ColorSpace::Oklch);

    if (is_relative) {
        if (toks.size() < 5) return false;
        CssColor origin;
        if (!parse_color_impl(toks[1], origin, ctx, depth + 1)) return false;
        if (origin.is_currentcolor) return false;
        if (is_polar) env = make_relative_env(origin, space, "l", "c", "h");
        else env = make_relative_env(origin, space, "l", "a", "b");
        default_alpha = env.values[3];
        default_alpha_missing = env.missing[3];
        offset = 2;
    }

    std::string_view ch_toks[3];
    bool has_alpha = false;
    std::string_view alpha_tok;
    if (!split_modern_3_and_alpha(toks, offset, ch_toks, has_alpha, alpha_tok)) return false;

    const double l_max = is_ok ? 1.0 : 100.0;
    const double ab_ref = is_ok ? 0.4 : 125.0;
    const double c_ref = is_ok ? 0.4 : 150.0;

    ParsedComponent p0, p1, p2;
    if (!parse_component(ch_toks[0], {l_max, true, true, true, false}, env, p0)) return false;
    if (is_polar) {
        if (!parse_component(ch_toks[1], {c_ref, true, true, true, false}, env, p1)) return false;
        if (!parse_component(ch_toks[2], {1.0, true, true, false, true}, env, p2)) return false;
    } else {
        if (!parse_component(ch_toks[1], {ab_ref, true, true, true, false}, env, p1)) return false;
        if (!parse_component(ch_toks[2], {ab_ref, true, true, true, false}, env, p2)) return false;
    }

    double alpha = default_alpha;
    bool alpha_missing = default_alpha_missing;
    if (has_alpha) {
        ParsedComponent pa;
        if (!parse_component(alpha_tok, {1.0, true, true, true, false}, env, pa)) return false;
        alpha_missing = pa.is_none;
        alpha = pa.is_none ? 0.0 : clamp_channel(pa.value, 0.0, 1.0);
    }

    out = CssColor{};
    out.space = space;
    out.legacy_srgb = false;
    out.missing[0] = p0.is_none;
    out.missing[1] = p1.is_none;
    out.missing[2] = p2.is_none;
    out.c[0] = p0.is_none ? 0.0 : clamp_channel(p0.value, 0.0, l_max);
    if (is_polar) {
        out.c[1] = p1.is_none ? 0.0 : clamp_min(p1.value, 0.0);
        out.c[2] = p2.is_none ? 0.0 : normalize_hue(sanitize_nan_to_zero(p2.value));
    } else {
        out.c[1] = p1.is_none ? 0.0 : sanitize_nan_to_zero(p1.value);
        out.c[2] = p2.is_none ? 0.0 : sanitize_nan_to_zero(p2.value);
    }
    out.alpha = alpha;
    out.alpha_missing = alpha_missing;
    return true;
}

bool parse_color_func(std::string_view args, CssColor& out, const ColorResolveContext& ctx, int depth) {
    if (has_top_level_comma(args)) return false;
    std::vector<std::string_view> toks;
    if (!tokenize_components(args, toks) || toks.size() < 4) return false;

    const bool is_relative = (ascii_lower(toks[0]) == "from");
    size_t space_idx = is_relative ? 2 : 0;
    if (toks.size() < space_idx + 4) return false;

    ColorSpace space = ColorSpace::Srgb;
    if (!parse_predefined_space(ascii_lower(toks[space_idx]), space)) return false;

    ChannelEnv env;
    double default_alpha = 1.0;
    bool default_alpha_missing = false;
    if (is_relative) {
        CssColor origin;
        if (!parse_color_impl(toks[1], origin, ctx, depth + 1)) return false;
        if (origin.is_currentcolor) return false;
        const bool is_xyz = (space == ColorSpace::XyzD50 || space == ColorSpace::XyzD65);
        env = make_relative_env(origin, space, is_xyz ? "x" : "r", is_xyz ? "y" : "g", is_xyz ? "z" : "b");
        default_alpha = env.values[3];
        default_alpha_missing = env.missing[3];
    }

    std::string_view ch_toks[3];
    bool has_alpha = false;
    std::string_view alpha_tok;
    if (!split_modern_3_and_alpha(toks, space_idx + 1, ch_toks, has_alpha, alpha_tok)) return false;

    const ComponentSpec ch_spec{1.0, true, true, true, false};
    ParsedComponent ch[3];
    for (int i = 0; i < 3; ++i) {
        if (!parse_component(ch_toks[i], ch_spec, env, ch[i])) return false;
    }

    double alpha = default_alpha;
    bool alpha_missing = default_alpha_missing;
    if (has_alpha) {
        ParsedComponent pa;
        if (!parse_component(alpha_tok, {1.0, true, true, true, false}, env, pa)) return false;
        alpha_missing = pa.is_none;
        alpha = pa.is_none ? 0.0 : clamp_channel(pa.value, 0.0, 1.0);
    }

    out = CssColor{};
    out.space = space;
    out.legacy_srgb = false;
    for (int i = 0; i < 3; ++i) {
        out.missing[i] = ch[i].is_none;
        out.c[i] = ch[i].is_none ? 0.0 : sanitize_nan_to_zero(ch[i].value);
    }
    out.alpha = alpha;
    out.alpha_missing = alpha_missing;
    return true;
}

bool parse_mix_item(std::string_view item, CssColor& out_color, double& out_pct, bool& has_pct,
                    const ColorResolveContext& ctx, int depth) {
    std::vector<std::string_view> toks;
    if (!tokenize_components(item, toks)) return false;
    if (toks.size() != 1 && toks.size() != 2) return false;
    const ChannelEnv empty_env;
    const ComponentSpec pct_spec{100.0, false, false, true, false};

    if (toks.size() == 1) {
        has_pct = false;
        out_pct = 0.0;
        if (!parse_color_impl(toks[0], out_color, ctx, depth + 1)) return false;
        return !out_color.is_currentcolor;
    }

    auto parse_mix_pct = [&](std::string_view tok, double& pct) -> bool {
        ParsedComponent pc;
        if (!parse_component(tok, pct_spec, empty_env, pc)) return false;
        const bool is_math_expr = (tok.find('(') != std::string_view::npos);
        if (is_math_expr) {
            pct = clamp_channel(pc.value, 0.0, 100.0);
            return true;
        }
        if (!std::isfinite(pc.value) || pc.value < 0.0 || pc.value > 100.0) return false;
        pct = pc.value;
        return true;
    };

    // Either `<color> <percentage>` or `<percentage> <color>`
    double pct = 0.0;
    if (parse_mix_pct(toks[0], pct)) {
        has_pct = true;
        out_pct = pct;
        if (!parse_color_impl(toks[1], out_color, ctx, depth + 1)) return false;
        return !out_color.is_currentcolor;
    }
    if (parse_mix_pct(toks[1], pct)) {
        has_pct = true;
        out_pct = pct;
        if (!parse_color_impl(toks[0], out_color, ctx, depth + 1)) return false;
        return !out_color.is_currentcolor;
    }
    return false;
}

bool parse_color_mix_func(std::string_view args, CssColor& out, const ColorResolveContext& ctx, int depth) {
    std::vector<std::string_view> parts;
    if (!split_top_level_commas(args, parts) || parts.size() < 2) return false;

    std::vector<std::string_view> method_toks;
    if (!tokenize_components(parts[0], method_toks) || method_toks.size() < 2) return false;
    if (ascii_lower(method_toks[0]) != "in") return false;

    ColorSpace interp_space = ColorSpace::Oklab;
    if (!parse_interpolation_space(ascii_lower(method_toks[1]), interp_space)) return false;

    HueInterpolation hue_method = HueInterpolation::Shorter;
    if (method_toks.size() == 2) {
        // default shorter hue
    } else if (method_toks.size() == 4) {
        if (!is_polar_space(interp_space)) return false;
        if (ascii_lower(method_toks[3]) != "hue") return false;
        const std::string hm = ascii_lower(method_toks[2]);
        if (hm == "shorter") hue_method = HueInterpolation::Shorter;
        else if (hm == "longer") hue_method = HueInterpolation::Longer;
        else if (hm == "increasing") hue_method = HueInterpolation::Increasing;
        else if (hm == "decreasing") hue_method = HueInterpolation::Decreasing;
        else return false;
    } else {
        return false;
    }

    const size_t count = parts.size() - 1;
    std::vector<CssColor> colors(count);
    std::vector<double> pcts(count, 0.0);
    std::vector<uint8_t> has_pct(count, 0);
    for (size_t i = 0; i < count; ++i) {
        bool hp = false;
        if (!parse_mix_item(parts[i + 1], colors[i], pcts[i], hp, ctx, depth)) return false;
        has_pct[i] = hp ? 1 : 0;
    }

    if (count == 2) {
        return CssColor::mix(colors[0], pcts[0], has_pct[0] != 0,
                             colors[1], pcts[1], has_pct[1] != 0,
                             interp_space, hue_method, out);
    }

    if (count == 1) {
        CssColor conv = colors[0].convert_to(interp_space);
        conv.legacy_srgb = false;
        const double mult = has_pct[0] ? (pcts[0] / 100.0) : 1.0;
        if (!conv.alpha_missing) conv.alpha = clamp_channel(conv.alpha * mult, 0.0, 1.0);
        const bool has_any_none = conv.missing[0] || conv.missing[1] || conv.missing[2] || conv.alpha_missing;
        if ((interp_space == ColorSpace::Hsl || interp_space == ColorSpace::Hwb) && !has_any_none) {
            out = conv.convert_to(ColorSpace::Srgb);
            out.legacy_srgb = false;
        } else {
            out = conv;
        }
        return true;
    }

    // CSS Color 5 multi-argument color-mix() (N >= 3): normalize percentages
    // and combine sequentially or by weighted average.
    double specified_sum = 0.0;
    size_t missing_cnt = 0;
    for (size_t i = 0; i < count; ++i) {
        if (has_pct[i]) specified_sum += pcts[i];
        else ++missing_cnt;
    }
    if (missing_cnt > 0) {
        const double rem = std::max(0.0, 100.0 - specified_sum);
        const double share = rem / static_cast<double>(missing_cnt);
        for (size_t i = 0; i < count; ++i) {
            if (!has_pct[i]) pcts[i] = share;
        }
    }
    double total = 0.0;
    for (double p : pcts) total += p;
    if (total == 0.0) {
        // Left-fold with equal weights and alpha_mult = 0 per CSS Color 5 WPT
        CssColor acc = colors[0];
        for (size_t i = 1; i < count; ++i) {
            CssColor next;
            if (!CssColor::mix(acc, 50.0, true, colors[i], 50.0, true, interp_space, hue_method, next)) return false;
            acc = next;
        }
        if (!acc.alpha_missing) acc.alpha = 0.0;
        out = acc;
        return true;
    }

    const double alpha_mult = (total < 100.0) ? (total / 100.0) : 1.0;
    std::vector<CssColor> conv(count);
    for (size_t i = 0; i < count; ++i) conv[i] = colors[i].convert_to(interp_space);

    std::vector<double> w(count);
    for (size_t i = 0; i < count; ++i) w[i] = pcts[i] / total;

    double a_interp = 0.0;
    bool all_alpha_missing = true;
    for (size_t i = 0; i < count; ++i) {
        if (!conv[i].alpha_missing) all_alpha_missing = false;
    }
    for (size_t i = 0; i < count; ++i) {
        const double ai = conv[i].alpha_missing ? 1.0 : conv[i].alpha;
        a_interp += ai * w[i];
    }

    CssColor mixed;
    mixed.space = interp_space;
    mixed.legacy_srgb = false;
    for (int ch = 0; ch < 3; ++ch) {
        bool all_m = true;
        double sum_num = 0.0;
        double sum_w = 0.0;
        for (size_t i = 0; i < count; ++i) {
            if (conv[i].missing[ch]) continue;
            all_m = false;
            const double ai = conv[i].alpha_missing ? 1.0 : conv[i].alpha;
            sum_num += conv[i].c[ch] * ai * w[i];
            sum_w += ai * w[i];
        }
        mixed.missing[ch] = all_m;
        if (!all_m) {
            mixed.c[ch] = (sum_w > 0.0) ? (sum_num / sum_w) : 0.0;
        }
    }
    mixed.alpha_missing = all_alpha_missing;
    mixed.alpha = all_alpha_missing ? 0.0 : clamp_channel(a_interp * alpha_mult, 0.0, 1.0);
    const bool has_any_none = mixed.missing[0] || mixed.missing[1] || mixed.missing[2] || mixed.alpha_missing;
    if ((interp_space == ColorSpace::Hsl || interp_space == ColorSpace::Hwb) && !has_any_none) {
        out = mixed.convert_to(ColorSpace::Srgb);
        out.legacy_srgb = false;
    } else {
        out = mixed;
    }
    return true;
}

bool parse_light_dark_func(std::string_view args, CssColor& out, const ColorResolveContext& ctx, int depth) {
    std::vector<std::string_view> parts;
    if (!split_top_level_commas(args, parts) || parts.size() != 2) return false;
    CssColor light_c, dark_c;
    // Validate both branches strictly per CSS Color 5 §9.1
    if (!parse_color_impl(parts[0], light_c, ctx, depth + 1)) return false;
    if (!parse_color_impl(parts[1], dark_c, ctx, depth + 1)) return false;
    out = ctx.dark_scheme ? dark_c : light_c;
    return true;
}

bool parse_color_impl(std::string_view text, CssColor& out, const ColorResolveContext& ctx, int depth) {
    if (depth > kMaxColorDepth) return false;
    text = trim_ws(text);
    if (text.empty()) return false;

    if (text.front() == '#') {
        const std::string_view h = text.substr(1);
        const size_t len = h.size();
        if (len != 3 && len != 4 && len != 6 && len != 8) return false;
        int d[8]{};
        for (size_t i = 0; i < len; ++i) {
            d[i] = hex_nibble(h[i]);
            if (d[i] < 0) return false;
        }
        int r = 0, g = 0, b = 0, a = 255;
        if (len == 3 || len == 4) {
            r = d[0] * 17;
            g = d[1] * 17;
            b = d[2] * 17;
            if (len == 4) a = d[3] * 17;
        } else {
            r = (d[0] << 4) | d[1];
            g = (d[2] << 4) | d[3];
            b = (d[4] << 4) | d[5];
            if (len == 8) a = (d[6] << 4) | d[7];
        }
        out = CssColor{};
        out.space = ColorSpace::Srgb;
        out.legacy_srgb = true;
        out.from_8bit_alpha = (len == 4 || len == 8);
        out.c[0] = r / 255.0;
        out.c[1] = g / 255.0;
        out.c[2] = b / 255.0;
        out.alpha = a / 255.0;
        return true;
    }

    // Functional notation?
    if (text.find('(') != std::string_view::npos) {
        std::string fn;
        std::string_view args;
        if (!parse_func_call(text, fn, args)) return false;
        if (fn == "rgb" || fn == "rgba") return parse_rgb_func(args, out, ctx, depth);
        if (fn == "hsl" || fn == "hsla") return parse_hsl_func(args, out, ctx, depth);
        if (fn == "hwb") return parse_hwb_func(args, out, ctx, depth);
        if (fn == "lab") return parse_lab_like_func(ColorSpace::Lab, args, out, ctx, depth);
        if (fn == "lch") return parse_lab_like_func(ColorSpace::Lch, args, out, ctx, depth);
        if (fn == "oklab") return parse_lab_like_func(ColorSpace::Oklab, args, out, ctx, depth);
        if (fn == "oklch") return parse_lab_like_func(ColorSpace::Oklch, args, out, ctx, depth);
        if (fn == "color") return parse_color_func(args, out, ctx, depth);
        if (fn == "color-mix") return parse_color_mix_func(args, out, ctx, depth);
        if (fn == "light-dark") return parse_light_dark_func(args, out, ctx, depth);
        return false;
    }

    const std::string lower = ascii_lower(text);
    if (lower == "transparent") {
        out = CssColor{};
        out.space = ColorSpace::Srgb;
        out.legacy_srgb = true;
        out.alpha = 0.0;
        return true;
    }
    if (lower == "currentcolor") {
        if (ctx.current_color) {
            out = *ctx.current_color;
            out.is_currentcolor = false;
            return true;
        }
        out = CssColor{};
        out.is_currentcolor = true;
        return true;
    }
    unsigned named = 0;
    if (Color::named_color_value(lower, named)) {
        out = CssColor{};
        out.space = ColorSpace::Srgb;
        out.legacy_srgb = true;
        out.c[0] = ((named >> 16) & 0xFF) / 255.0;
        out.c[1] = ((named >> 8) & 0xFF) / 255.0;
        out.c[2] = (named & 0xFF) / 255.0;
        out.alpha = 1.0;
        return true;
    }
    return false;
}

const char* color_space_css_name(ColorSpace space) {
    switch (space) {
        case ColorSpace::Srgb: return "srgb";
        case ColorSpace::SrgbLinear: return "srgb-linear";
        case ColorSpace::DisplayP3: return "display-p3";
        case ColorSpace::DisplayP3Linear: return "display-p3-linear";
        case ColorSpace::A98Rgb: return "a98-rgb";
        case ColorSpace::ProphotoRgb: return "prophoto-rgb";
        case ColorSpace::Rec2020: return "rec2020";
        case ColorSpace::XyzD50: return "xyz-d50";
        case ColorSpace::XyzD65: return "xyz-d65";
        case ColorSpace::Lab: return "lab";
        case ColorSpace::Lch: return "lch";
        case ColorSpace::Oklab: return "oklab";
        case ColorSpace::Oklch: return "oklch";
        case ColorSpace::Hsl: return "hsl";
        case ColorSpace::Hwb: return "hwb";
    }
    return "srgb";
}

} // namespace

// ============================================================================
// Color Methods
// ============================================================================

bool Color::named_color_value(const std::string& lower, unsigned& out) {
    static const std::unordered_map<std::string, unsigned> s_values = {
        {"aliceblue", 0xf0f8ff}, {"antiquewhite", 0xfaebd7}, {"aqua", 0x00ffff}, {"aquamarine", 0x7fffd4},
        {"azure", 0xf0ffff}, {"beige", 0xf5f5dc}, {"bisque", 0xffe4c4}, {"black", 0x000000},
        {"blanchedalmond", 0xffebcd}, {"blue", 0x0000ff}, {"blueviolet", 0x8a2be2}, {"brown", 0xa52a2a},
        {"burlywood", 0xdeb887}, {"cadetblue", 0x5f9ea0}, {"chartreuse", 0x7fff00}, {"chocolate", 0xd2691e},
        {"coral", 0xff7f50}, {"cornflowerblue", 0x6495ed}, {"cornsilk", 0xfff8dc}, {"crimson", 0xdc143c},
        {"cyan", 0x00ffff}, {"darkblue", 0x00008b}, {"darkcyan", 0x008b8b}, {"darkgoldenrod", 0xb8860b},
        {"darkgray", 0xa9a9a9}, {"darkgreen", 0x006400}, {"darkgrey", 0xa9a9a9}, {"darkkhaki", 0xbdb76b},
        {"darkmagenta", 0x8b008b}, {"darkolivegreen", 0x556b2f}, {"darkorange", 0xff8c00}, {"darkorchid", 0x9932cc},
        {"darkred", 0x8b0000}, {"darksalmon", 0xe9967a}, {"darkseagreen", 0x8fbc8f}, {"darkslateblue", 0x483d8b},
        {"darkslategray", 0x2f4f4f}, {"darkslategrey", 0x2f4f4f}, {"darkturquoise", 0x00ced1},
        {"darkviolet", 0x9400d3}, {"deeppink", 0xff1493}, {"deepskyblue", 0x00bfff}, {"dimgray", 0x696969},
        {"dimgrey", 0x696969}, {"dodgerblue", 0x1e90ff}, {"firebrick", 0xb22222}, {"floralwhite", 0xfffaf0},
        {"forestgreen", 0x228b22}, {"fuchsia", 0xff00ff}, {"gainsboro", 0xdcdcdc}, {"ghostwhite", 0xf8f8ff},
        {"gold", 0xffd700}, {"goldenrod", 0xdaa520}, {"gray", 0x808080}, {"green", 0x008000},
        {"greenyellow", 0xadff2f}, {"grey", 0x808080}, {"honeydew", 0xf0fff0}, {"hotpink", 0xff69b4},
        {"indianred", 0xcd5c5c}, {"indigo", 0x4b0082}, {"ivory", 0xfffff0}, {"khaki", 0xf0e68c},
        {"lavender", 0xe6e6fa}, {"lavenderblush", 0xfff0f5}, {"lawngreen", 0x7cfc00}, {"lemonchiffon", 0xfffacd},
        {"lightblue", 0xadd8e6}, {"lightcoral", 0xf08080}, {"lightcyan", 0xe0ffff},
        {"lightgoldenrodyellow", 0xfafad2}, {"lightgray", 0xd3d3d3}, {"lightgreen", 0x90ee90},
        {"lightgrey", 0xd3d3d3}, {"lightpink", 0xffb6c1}, {"lightsalmon", 0xffa07a}, {"lightseagreen", 0x20b2aa},
        {"lightskyblue", 0x87cefa}, {"lightslategray", 0x778899}, {"lightslategrey", 0x778899},
        {"lightsteelblue", 0xb0c4de}, {"lightyellow", 0xffffe0}, {"lime", 0x00ff00}, {"limegreen", 0x32cd32},
        {"linen", 0xfaf0e6}, {"magenta", 0xff00ff}, {"maroon", 0x800000}, {"mediumaquamarine", 0x66cdaa},
        {"mediumblue", 0x0000cd}, {"mediumorchid", 0xba55d3}, {"mediumpurple", 0x9370db},
        {"mediumseagreen", 0x3cb371}, {"mediumslateblue", 0x7b68ee}, {"mediumspringgreen", 0x00fa9a},
        {"mediumturquoise", 0x48d1cc}, {"mediumvioletred", 0xc71585}, {"midnightblue", 0x191970},
        {"mintcream", 0xf5fffa}, {"mistyrose", 0xffe4e1}, {"moccasin", 0xffe4b5}, {"navajowhite", 0xffdead},
        {"navy", 0x000080}, {"oldlace", 0xfdf5e6}, {"olive", 0x808000}, {"olivedrab", 0x6b8e23},
        {"orange", 0xffa500}, {"orangered", 0xff4500}, {"orchid", 0xda70d6}, {"palegoldenrod", 0xeee8aa},
        {"palegreen", 0x98fb98}, {"paleturquoise", 0xafeeee}, {"palevioletred", 0xdb7093}, {"papayawhip", 0xffefd5},
        {"peachpuff", 0xffdab9}, {"peru", 0xcd853f}, {"pink", 0xffc0cb}, {"plum", 0xdda0dd},
        {"powderblue", 0xb0e0e6}, {"purple", 0x800080}, {"rebeccapurple", 0x663399}, {"red", 0xff0000},
        {"rosybrown", 0xbc8f8f}, {"royalblue", 0x4169e1}, {"saddlebrown", 0x8b4513}, {"salmon", 0xfa8072},
        {"sandybrown", 0xf4a460}, {"seagreen", 0x2e8b57}, {"seashell", 0xfff5ee}, {"sienna", 0xa0522d},
        {"silver", 0xc0c0c0}, {"skyblue", 0x87ceeb}, {"slateblue", 0x6a5acd}, {"slategray", 0x708090},
        {"slategrey", 0x708090}, {"snow", 0xfffafa}, {"springgreen", 0x00ff7f}, {"steelblue", 0x4682b4},
        {"tan", 0xd2b48c}, {"teal", 0x008080}, {"thistle", 0xd8bfd8}, {"tomato", 0xff6347}, {"turquoise", 0x40e0d0},
        {"violet", 0xee82ee}, {"wheat", 0xf5deb3}, {"white", 0xffffff}, {"whitesmoke", 0xf5f5f5},
        {"yellow", 0xffff00}, {"yellowgreen", 0x9acd32},
    };
    auto found = s_values.find(lower);
    if (found == s_values.end()) return false;
    out = found->second;
    return true;
}

bool Color::is_color_function_name(std::string_view lower_name) {
    return lower_name == "rgb" || lower_name == "rgba" ||
           lower_name == "hsl" || lower_name == "hsla" ||
           lower_name == "hwb" || lower_name == "lab" ||
           lower_name == "lch" || lower_name == "oklab" ||
           lower_name == "oklch" || lower_name == "color" ||
           lower_name == "color-mix" || lower_name == "light-dark";
}

bool Color::looks_like_color(std::string_view token) {
    token = trim_ws(token);
    if (token.empty()) return false;
    if (token.front() == '#') return true;
    const size_t open = token.find('(');
    if (open != std::string_view::npos) {
        return is_color_function_name(ascii_lower(token.substr(0, open)));
    }
    const std::string lower = ascii_lower(token);
    if (lower == "transparent" || lower == "currentcolor") return true;
    unsigned named = 0;
    return named_color_value(lower, named);
}

bool Color::try_parse(std::string_view str, Color& out) {
    CssColor css;
    if (!CssColor::parse(str, css)) return false;
    if (css.is_currentcolor) return false;
    out = css.to_srgb8();
    return true;
}

bool Color::try_parse(std::string_view str, Color& out, const Color& current_color, bool dark_scheme) {
    const CssColor cc = CssColor::from_srgb8(current_color);
    ColorResolveContext ctx;
    ctx.current_color = &cc;
    ctx.dark_scheme = dark_scheme;
    CssColor css;
    if (!CssColor::parse(str, css, ctx)) return false;
    out = css.to_srgb8();
    return true;
}

Color Color::parse(std::string_view str) {
    Color out;
    if (try_parse(str, out)) return out;
    return Color::black();
}

Color Color::parse(std::string_view str, const Color& current_color, bool dark_scheme) {
    Color out;
    if (try_parse(str, out, current_color, dark_scheme)) return out;
    return Color::black();
}

std::string Color::to_hex() const {
    std::ostringstream ss;
    ss << "#" << std::hex << std::setfill('0')
       << std::setw(2) << static_cast<int>(r)
       << std::setw(2) << static_cast<int>(g)
       << std::setw(2) << static_cast<int>(b);
    return ss.str();
}

std::string Color::to_ansi_fg() const {
    return "\033[38;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
}

std::string Color::to_ansi_bg() const {
    return "\033[48;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
}

// ============================================================================
// CssColor Methods
// ============================================================================

CssColor CssColor::from_srgb8(Color c, bool legacy) {
    CssColor out;
    out.space = ColorSpace::Srgb;
    out.legacy_srgb = legacy;
    out.c[0] = c.r / 255.0;
    out.c[1] = c.g / 255.0;
    out.c[2] = c.b / 255.0;
    out.alpha = c.a / 255.0;
    return out;
}

bool CssColor::parse(std::string_view text, CssColor& out, const ColorResolveContext& ctx) {
    if (text.find("/*") != std::string_view::npos) {
        std::string cleaned;
        cleaned.reserve(text.size());
        size_t i = 0;
        while (i < text.size()) {
            if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '*') {
                const size_t end = text.find("*/", i + 2);
                if (end == std::string_view::npos) return false;
                cleaned.push_back(' ');
                i = end + 2;
            } else {
                cleaned.push_back(text[i++]);
            }
        }
        return parse_color_impl(cleaned, out, ctx, 0);
    }
    return parse_color_impl(text, out, ctx, 0);
}

bool CssColor::is_valid_syntax(std::string_view text) {
    const CssColor dummy = from_srgb8(Color::black());
    ColorResolveContext ctx;
    ctx.current_color = &dummy;
    CssColor parsed;
    return CssColor::parse(text, parsed, ctx);
}

CssColor CssColor::convert_to(ColorSpace target) const {
    if (space == target) {
        CssColor copy = *this;
        if (target != ColorSpace::Srgb) copy.legacy_srgb = false;
        return copy;
    }

    const Vec3 src_v = {
        missing[0] ? 0.0 : c[0],
        missing[1] ? 0.0 : c[1],
        missing[2] ? 0.0 : c[2],
    };

    Vec3 dst_v{0.0, 0.0, 0.0};
    bool powerless_0 = false;
    bool powerless_2 = false;

    // Direct shortcuts avoiding unnecessary XYZ/Bradford round-trips
    if (space == ColorSpace::Srgb && target == ColorSpace::Hsl) {
        dst_v = srgb_to_hsl(src_v, powerless_0);
    } else if (space == ColorSpace::Hsl && target == ColorSpace::Srgb) {
        dst_v = hsl_to_srgb(src_v[0], src_v[1], src_v[2]);
    } else if (space == ColorSpace::Srgb && target == ColorSpace::Hwb) {
        dst_v = srgb_to_hwb(src_v, powerless_0);
    } else if (space == ColorSpace::Hwb && target == ColorSpace::Srgb) {
        dst_v = hwb_to_srgb(src_v[0], src_v[1], src_v[2]);
    } else if (space == ColorSpace::Srgb && target == ColorSpace::SrgbLinear) {
        dst_v = {srgb_to_linear_channel(src_v[0]),
                 srgb_to_linear_channel(src_v[1]),
                 srgb_to_linear_channel(src_v[2])};
    } else if (space == ColorSpace::SrgbLinear && target == ColorSpace::Srgb) {
        dst_v = {linear_to_srgb_channel(src_v[0]),
                 linear_to_srgb_channel(src_v[1]),
                 linear_to_srgb_channel(src_v[2])};
    } else if (space == ColorSpace::Lab && target == ColorSpace::Lch) {
        dst_v = lab_to_lch(src_v, 1e-4, powerless_2);
    } else if (space == ColorSpace::Lch && target == ColorSpace::Lab) {
        dst_v = lch_to_lab(src_v);
    } else if (space == ColorSpace::Oklab && target == ColorSpace::Oklch) {
        dst_v = lab_to_lch(src_v, 1e-6, powerless_2);
    } else if (space == ColorSpace::Oklch && target == ColorSpace::Oklab) {
        dst_v = lch_to_lab(src_v);
    } else if (space == ColorSpace::XyzD50 && target == ColorSpace::Lab) {
        dst_v = xyz_d50_to_lab(src_v);
    } else if (space == ColorSpace::Lab && target == ColorSpace::XyzD50) {
        dst_v = lab_to_xyz_d50(src_v);
    } else if (space == ColorSpace::XyzD50 && target == ColorSpace::Lch) {
        dst_v = lab_to_lch(xyz_d50_to_lab(src_v), 1e-4, powerless_2);
    } else if (space == ColorSpace::Lch && target == ColorSpace::XyzD50) {
        dst_v = lab_to_xyz_d50(lch_to_lab(src_v));
    } else {
        const Vec3 xyz65 = to_xyz_d65(space, src_v);
        dst_v = from_xyz_d65(target, xyz65, powerless_0, powerless_2);
    }

    CssColor out;
    out.space = target;
    out.legacy_srgb = false;
    out.is_currentcolor = is_currentcolor;
    out.c[0] = dst_v[0];
    out.c[1] = dst_v[1];
    out.c[2] = dst_v[2];
    out.alpha = alpha;
    out.alpha_missing = alpha_missing;
    out.missing[0] = powerless_0;
    out.missing[1] = false;
    out.missing[2] = powerless_2;

    // Carry forward analogous missing components per CSS Color 4 §12.2
    for (int dst_i = 0; dst_i < 3; ++dst_i) {
        const AnalogousCategory dst_cat = channel_category(target, dst_i);
        if (dst_cat == AnalogousCategory::None) continue;
        for (int src_i = 0; src_i < 3; ++src_i) {
            if (missing[src_i] && channel_category(space, src_i) == dst_cat) {
                out.missing[dst_i] = true;
                out.c[dst_i] = 0.0;
                break;
            }
        }
    }
    // Coupled opponent/polar missing pairs per WPT CSS Color 4:
    // oklab(L none none) <-> oklch(L none none), lab(L none none) <-> lch(L none none),
    // hsl(H none none) <-> hwb(H none none).
    if ((space == ColorSpace::Oklab && target == ColorSpace::Oklch) ||
        (space == ColorSpace::Lab && target == ColorSpace::Lch)) {
        if (missing[1] && missing[2]) {
            out.missing[1] = out.missing[2] = true;
            out.c[1] = out.c[2] = 0.0;
        }
    } else if ((space == ColorSpace::Oklch && target == ColorSpace::Oklab) ||
               (space == ColorSpace::Lch && target == ColorSpace::Lab)) {
        if (missing[1] && missing[2]) {
            out.missing[1] = out.missing[2] = true;
            out.c[1] = out.c[2] = 0.0;
        }
    } else if ((space == ColorSpace::Hsl && target == ColorSpace::Hwb) ||
               (space == ColorSpace::Hwb && target == ColorSpace::Hsl)) {
        if (missing[1] && missing[2]) {
            out.missing[1] = out.missing[2] = true;
            out.c[1] = out.c[2] = 0.0;
        }
    }
    return out;
}

bool CssColor::mix(const CssColor& c1_in, double p1, bool has_p1,
                   const CssColor& c2_in, double p2, bool has_p2,
                   ColorSpace interp_space, HueInterpolation hue_method,
                   CssColor& out) {
    // Percentage normalization per CSS Color 5 §2.2
    if (!has_p1 && !has_p2) {
        p1 = 50.0;
        p2 = 50.0;
    } else if (has_p1 && !has_p2) {
        p2 = 100.0 - p1;
    } else if (!has_p1 && has_p2) {
        p1 = 100.0 - p2;
    }
    const double p_sum = p1 + p2;
    if (p_sum < 0.0 || !std::isfinite(p_sum)) return false;

    double alpha_mult = 1.0;
    double w1 = 0.0;
    double w2 = 0.0;
    if (p_sum == 0.0) {
        // CSS Color 5 (2025/2026 WPT): 0% + 0% mixes equally with alpha multiplier 0.
        alpha_mult = 0.0;
        w1 = 0.5;
        w2 = 0.5;
    } else if (p_sum > 100.0) {
        w1 = p1 / p_sum;
        w2 = p2 / p_sum;
    } else if (p_sum < 100.0) {
        alpha_mult = p_sum / 100.0;
        w1 = p1 / p_sum;
        w2 = p2 / p_sum;
    } else {
        w1 = p1 / 100.0;
        w2 = p2 / 100.0;
    }

    CssColor a = c1_in.convert_to(interp_space);
    CssColor b = c2_in.convert_to(interp_space);

    // Carry forward missing components between endpoints (CSS Color 4 §12.2)
    bool res_missing[3]{false, false, false};
    double av[3]{a.c[0], a.c[1], a.c[2]};
    double bv[3]{b.c[0], b.c[1], b.c[2]};
    for (int i = 0; i < 3; ++i) {
        if (a.missing[i] && b.missing[i]) {
            res_missing[i] = true;
            av[i] = 0.0;
            bv[i] = 0.0;
        } else if (a.missing[i]) {
            av[i] = bv[i];
        } else if (b.missing[i]) {
            bv[i] = av[i];
        }
    }

    bool res_alpha_missing = false;
    double aa = a.alpha;
    double ba = b.alpha;
    if (a.alpha_missing && b.alpha_missing) {
        res_alpha_missing = true;
        aa = 1.0;
        ba = 1.0;
    } else if (a.alpha_missing) {
        aa = ba;
    } else if (b.alpha_missing) {
        ba = aa;
    }

    const double a_interp = aa * w1 + ba * w2;
    const int hue_idx = hue_channel_index(interp_space);

    CssColor mixed;
    mixed.space = interp_space;
    mixed.legacy_srgb = false;
    for (int i = 0; i < 3; ++i) {
        mixed.missing[i] = res_missing[i];
        if (res_missing[i]) {
            mixed.c[i] = 0.0;
            continue;
        }
        if (i == hue_idx) {
            double h1 = normalize_hue(av[i]);
            double h2 = normalize_hue(bv[i]);
            const double diff = h2 - h1;
            switch (hue_method) {
                case HueInterpolation::Shorter:
                    if (diff > 180.0) h1 += 360.0;
                    else if (diff < -180.0) h2 += 360.0;
                    break;
                case HueInterpolation::Longer:
                    if (diff > 0.0 && diff < 180.0) h1 += 360.0;
                    else if (diff > -180.0 && diff <= 0.0) h2 += 360.0;
                    break;
                case HueInterpolation::Increasing:
                    if (h2 < h1) h2 += 360.0;
                    break;
                case HueInterpolation::Decreasing:
                    if (h1 < h2) h1 += 360.0;
                    break;
            }
            mixed.c[i] = normalize_hue(h1 * w1 + h2 * w2);
        } else {
            if (a_interp > 0.0) {
                mixed.c[i] = (av[i] * aa * w1 + bv[i] * ba * w2) / a_interp;
            } else {
                mixed.c[i] = av[i] * w1 + bv[i] * w2;
            }
        }
    }

    mixed.alpha_missing = res_alpha_missing;
    mixed.alpha = res_alpha_missing ? 0.0 : clamp_channel(a_interp * alpha_mult, 0.0, 1.0);

    // Per CSS Color 5 §2.3 & WPT: hsl and hwb color-mix results serialize as
    // color(srgb ...) unless any component (or alpha) is `none`.
    const bool has_any_none = mixed.missing[0] || mixed.missing[1] || mixed.missing[2] || mixed.alpha_missing;
    if ((interp_space == ColorSpace::Hsl || interp_space == ColorSpace::Hwb) && !has_any_none) {
        out = mixed.convert_to(ColorSpace::Srgb);
        out.legacy_srgb = false;
    } else {
        out = mixed;
    }
    return true;
}

Color CssColor::to_srgb8() const {
    const double eff_alpha = alpha_missing ? 0.0 : clamp_channel(alpha, 0.0, 1.0);
    const uint8_t a8 = static_cast<uint8_t>(std::clamp(std::lround(eff_alpha * 255.0), 0L, 255L));

    const CssColor srgb = convert_to(ColorSpace::Srgb);
    const double r = srgb.missing[0] ? 0.0 : srgb.c[0];
    const double g = srgb.missing[1] ? 0.0 : srgb.c[1];
    const double b = srgb.missing[2] ? 0.0 : srgb.c[2];

    auto pack = [&](double rr, double gg, double bb) -> Color {
        return Color(
            static_cast<uint8_t>(std::clamp(std::lround(clamp_channel(rr, 0.0, 1.0) * 255.0 + 1e-12), 0L, 255L)),
            static_cast<uint8_t>(std::clamp(std::lround(clamp_channel(gg, 0.0, 1.0) * 255.0 + 1e-12), 0L, 255L)),
            static_cast<uint8_t>(std::clamp(std::lround(clamp_channel(bb, 0.0, 1.0) * 255.0 + 1e-12), 0L, 255L)),
            a8);
    };

    auto in_gamut = [](double x, double y, double z) {
        return x >= -1e-6 && x <= 1.0 + 1e-6 &&
               y >= -1e-6 && y <= 1.0 + 1e-6 &&
               z >= -1e-6 && z <= 1.0 + 1e-6;
    };

    // Fast path: in-gamut or legacy sRGB
    if (legacy_srgb || in_gamut(r, g, b)) {
        return pack(r, g, b);
    }

    // CSS Color 4 §13.2 OKLCH Binary-Search Chroma Reduction Gamut Mapping
    constexpr double kJnd = 0.02;
    constexpr double kEps = 0.0001;

    const CssColor oklch = convert_to(ColorSpace::Oklch);
    const double L = oklch.missing[0] ? 0.0 : oklch.c[0];
    const double C0 = oklch.missing[1] ? 0.0 : oklch.c[1];
    const double H = oklch.missing[2] ? 0.0 : oklch.c[2];
    if (L >= 1.0) return Color(255, 255, 255, a8);
    if (L <= 0.0) return Color(0, 0, 0, a8);

    auto oklch_to_srgb_vec = [](double l, double c, double h) -> Vec3 {
        const Vec3 oklab = lch_to_lab({l, c, h});
        const Vec3 xyz = oklab_to_xyz_d65(oklab);
        const Vec3 lin = mat_mul(kXyzD65ToLinSrgb, xyz);
        return {linear_to_srgb_channel(lin[0]),
                linear_to_srgb_channel(lin[1]),
                linear_to_srgb_channel(lin[2])};
    };
    auto srgb_to_oklab_vec = [](const Vec3& rgb) -> Vec3 {
        const Vec3 lin = {srgb_to_linear_channel(rgb[0]),
                          srgb_to_linear_channel(rgb[1]),
                          srgb_to_linear_channel(rgb[2])};
        return xyz_d65_to_oklab(mat_mul(kLinSrgbToXyzD65, lin));
    };
    auto delta_e_ok = [](const Vec3& a, const Vec3& b) {
        const double dl = a[0] - b[0];
        const double da = a[1] - b[1];
        const double db = a[2] - b[2];
        return std::sqrt(dl * dl + da * da + db * db);
    };

    const Vec3 origin_oklab = lch_to_lab({L, C0, H});
    const Vec3 direct_clip = {clamp_channel(r, 0.0, 1.0),
                              clamp_channel(g, 0.0, 1.0),
                              clamp_channel(b, 0.0, 1.0)};
    if (delta_e_ok(origin_oklab, srgb_to_oklab_vec(direct_clip)) < kJnd) {
        return pack(direct_clip[0], direct_clip[1], direct_clip[2]);
    }

    double lo = 0.0;
    double hi = C0;
    bool lo_in_gamut = true;
    Vec3 best = direct_clip;

    for (int iter = 0; iter < 32 && (hi - lo) > kEps; ++iter) {
        const double mid = 0.5 * (lo + hi);
        const Vec3 cur = oklch_to_srgb_vec(L, mid, H);
        if (lo_in_gamut && in_gamut(cur[0], cur[1], cur[2])) {
            lo = mid;
            best = cur;
            continue;
        }
        const Vec3 clipped = {clamp_channel(cur[0], 0.0, 1.0),
                              clamp_channel(cur[1], 0.0, 1.0),
                              clamp_channel(cur[2], 0.0, 1.0)};
        const double dE = delta_e_ok(lch_to_lab({L, mid, H}), srgb_to_oklab_vec(clipped));
        if (dE < kJnd) {
            if (kJnd - dE < kEps) {
                best = clipped;
                break;
            }
            lo_in_gamut = false;
            lo = mid;
            best = clipped;
        } else {
            hi = mid;
        }
    }
    return pack(best[0], best[1], best[2]);
}

std::string CssColor::serialize_computed() const {
    if (is_currentcolor) return "currentcolor";
    if (legacy_srgb) {
        const CssColor srgb = (space == ColorSpace::Srgb) ? *this : convert_to(ColorSpace::Srgb);
        const double r = srgb.missing[0] ? 0.0 : srgb.c[0];
        const double g = srgb.missing[1] ? 0.0 : srgb.c[1];
        const double b = srgb.missing[2] ? 0.0 : srgb.c[2];
        const double a = alpha_missing ? 0.0 : alpha;
        return format_legacy_rgb(r, g, b, a, from_8bit_alpha);
    }

    auto fmt_ch = [&](int i) -> std::string {
        if (missing[i]) return "none";
        return format_css_double(c[i], 6);
    };
    std::string alpha_suffix;
    if (alpha_missing) {
        alpha_suffix = " / none";
    } else if (alpha < 1.0 - 1e-7) {
        alpha_suffix = " / " + format_css_double(clamp_channel(alpha, 0.0, 1.0), 6);
    }

    if (space == ColorSpace::Lab || space == ColorSpace::Lch ||
        space == ColorSpace::Oklab || space == ColorSpace::Oklch) {
        return std::string(color_space_css_name(space)) + "(" +
               fmt_ch(0) + " " + fmt_ch(1) + " " + fmt_ch(2) + alpha_suffix + ")";
    }

    if (space == ColorSpace::Hsl || space == ColorSpace::Hwb) {
        auto fmt_pct = [&](int i) -> std::string {
            if (missing[i]) return "none";
            return format_css_double(c[i], 6) + "%";
        };
        return std::string(color_space_css_name(space)) + "(" +
               fmt_ch(0) + " " + fmt_pct(1) + " " + fmt_pct(2) + alpha_suffix + ")";
    }

    // Predefined spaces serialize via `color(<space> c1 c2 c3 [/ alpha])`
    return std::string("color(") + color_space_css_name(space) + " " +
           fmt_ch(0) + " " + fmt_ch(1) + " " + fmt_ch(2) + alpha_suffix + ")";
}

} // namespace litebird
