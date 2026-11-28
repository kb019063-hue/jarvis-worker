// Phase 497, finding 497: CSS Color 5 §8 `contrast-color()`.
//
// `contrast-color(<color>)` returns the better-contrasting of white or
// black against its argument, measured with the WCAG 2.1 relative-luminance
// ratio ((Y_light + 0.05) / (Y_dark + 0.05)); a tie returns white, matching
// the CSS Color 5 MVP and MDN. The function resolves at parse time, so
// every consumer of the color parser sees a plain black or white:
// declarations, computed values, `color-mix()` arguments and nested
// `contrast-color()` calls. Alpha is ignored (the comparison is defined on
// opaque colors); an unresolvable `currentcolor` argument fails the parse,
// exactly like `color-mix()`, and a context that resolves currentcolor gets
// the resolved answer.
TEST(css_color5_contrast_color_resolves_at_parse_time) {
    using namespace litebird;
    CssColor c;

    // The two endpoints and the four well-known anchors.
    assert(CssColor::parse("contrast-color(white)", c));
    assert(c.legacy_srgb && c.serialize_computed() == "rgb(0, 0, 0)");
    assert(CssColor::parse("contrast-color(black)", c));
    assert(c.serialize_computed() == "rgb(255, 255, 255)");
    assert(CssColor::parse("contrast-color(red)", c));        // Y = 0.2126, black 5.25 > white 4.00
    assert(c.serialize_computed() == "rgb(0, 0, 0)");
    assert(CssColor::parse("contrast-color(yellow)", c));     // Y = 0.9278
    assert(c.serialize_computed() == "rgb(0, 0, 0)");
    assert(CssColor::parse("contrast-color(navy)", c));       // Y = 0.0156
    assert(c.serialize_computed() == "rgb(255, 255, 255)");
    assert(CssColor::parse("contrast-color(rebeccapurple)", c));
    assert(c.serialize_computed() == "rgb(255, 255, 255)");
    assert(CssColor::parse("contrast-color(#ffd700)", c));
    assert(c.serialize_computed() == "rgb(0, 0, 0)");

    // The tie boundary on 8-bit grays is between 117 (white wins) and 118
    // (black wins): no integer gray sits exactly at the equal-contrast
    // luminance, so the white-wins tie branch cannot be reached from a
    // quantized channel; both neighbours are pinned here.
    assert(CssColor::parse("contrast-color(rgb(117, 117, 117))", c));
    assert(c.serialize_computed() == "rgb(255, 255, 255)");
    assert(CssColor::parse("contrast-color(rgb(118, 118, 118))", c));
    assert(c.serialize_computed() == "rgb(0, 0, 0)");

    // Any color space works as the argument; it is gamut-mapped to in-gamut
    // sRGB and read as opaque for the comparison.
    assert(CssColor::parse("contrast-color(hsl(0 100% 50%))", c));
    assert(c.serialize_computed() == "rgb(0, 0, 0)");
    assert(CssColor::parse("contrast-color(oklch(0.9 0.05 240))", c));
    assert(c.serialize_computed() == "rgb(0, 0, 0)");
    assert(CssColor::parse("contrast-color(oklch(0.3 0.1 240))", c));
    assert(c.serialize_computed() == "rgb(255, 255, 255)");
    assert(CssColor::parse("contrast-color(lab(50 20 30))", c) == true);

    // Alpha is ignored for the comparison: the same channels, with and
    // without alpha, answer the same.
    assert(CssColor::parse("contrast-color(rgb(255 0 0 / 0.5))", c));
    assert(c.serialize_computed() == "rgb(0, 0, 0)");
    assert(CssColor::parse("contrast-color(transparent)", c));   // channels 0, 0, 0
    assert(c.serialize_computed() == "rgb(255, 255, 255)");

    // The function is a <color> anywhere the parser runs, including as a
    // color-mix() argument and nested inside another contrast-color().
    assert(CssColor::parse("color-mix(in srgb, contrast-color(red) 50%, white)", c));
    assert(c.serialize_computed() == "color(srgb 0.5 0.5 0.5)");
    assert(CssColor::parse("contrast-color(contrast-color(yellow))", c));
    assert(c.serialize_computed() == "rgb(255, 255, 255)");

    // An unresolvable currentcolor argument fails like color-mix(); a context
    // that resolves currentcolor computes against it.
    assert(!CssColor::parse("contrast-color(currentcolor)", c));
    assert(CssColor::is_valid_syntax("contrast-color(currentcolor)"));   // preserved for computed-value time
    const CssColor red_cc = CssColor::from_srgb8(Color::red());
    const CssColor navy_cc = CssColor::from_srgb8(Color(0, 0, 128));
    ColorResolveContext ctx_red{&red_cc, false};
    ColorResolveContext ctx_navy{&navy_cc, false};
    assert(CssColor::parse("contrast-color(currentcolor)", c, ctx_red));
    assert(c.serialize_computed() == "rgb(0, 0, 0)");
    assert(CssColor::parse("contrast-color(currentcolor)", c, ctx_navy));
    assert(c.serialize_computed() == "rgb(255, 255, 255)");

    // Argument-count and validity rules: exactly one top-level <color>.
    assert(!CssColor::parse("contrast-color()", c));
    assert(!CssColor::parse("contrast-color(red, blue)", c));
    assert(!CssColor::parse("contrast-color(notacolor)", c));
    assert(!CssColor::parse("contrast-color(contrast-color())", c));
    assert(CssColor::parse("contrast-color(light-dark(white, navy))", c, ColorResolveContext{}));
    assert(c.serialize_computed() == "rgb(0, 0, 0)");
    ColorResolveContext dark_ctx{nullptr, true};
    assert(CssColor::parse("contrast-color(light-dark(white, navy))", c, dark_ctx));
    assert(c.serialize_computed() == "rgb(255, 255, 255)");

    // End to end through the style engine: the specified value is preserved,
    // getComputedStyle resolves against currentcolor.
    TestPage page;
    JsInterpreter& js = page.js;
    const JsValue out = js.eval(R"js(
        const a = document.createElement('div');
        a.style.color = 'contrast-color(#ffd700)';
        document.body.appendChild(a);
        const b = document.createElement('div');
        b.style.color = 'navy';
        b.style.backgroundColor = 'contrast-color(currentcolor)';
        document.body.appendChild(b);
        [getComputedStyle(a).color, getComputedStyle(b).backgroundColor].join('|');
    )js");
    assert(out.to_string() == "rgb(0, 0, 0)|rgb(255, 255, 255)");
}
