// Phase 535, finding 535: `CanvasRenderingContext2D.filter` blur uses a Gaussian kernel.
//
// `ImageFilter::blur` used a separable box average: every tap inside the window
// had the same weight. CSS `blur(<length>)` instead uses a Gaussian whose
// standard deviation is the supplied length. The impulse below makes the
// distinction numerical, not visual, and also holds the implementation to
// premultiplied-alpha filtering: a red pixel stays red as its coverage fades.
TEST(canvas_filter_blur_and_drop_shadow_use_gaussian_weights_and_premultiplied_alpha) {
    constexpr int width = 11;
    constexpr int height = 11;
    std::vector<Color> pixels(static_cast<std::size_t>(width * height), Color::transparent());
    pixels[static_cast<std::size_t>(5 * width + 5)] = Color(240, 40, 20, 255);

    ImageFilter::blur(pixels, width, height, 1.0f);

    const auto& center = pixels[static_cast<std::size_t>(5 * width + 5)];
    const auto& axial = pixels[static_cast<std::size_t>(5 * width + 6)];
    const auto& diagonal = pixels[static_cast<std::size_t>(6 * width + 6)];
    const auto& two_away = pixels[static_cast<std::size_t>(5 * width + 7)];

    // For sigma=1, the normalized 7-tap kernel is approximately
    // [.0044, .0540, .2420, .3989, .2420, .0540, .0044]. The center,
    // axial, and diagonal alpha values are its 2D products rounded to bytes.
    assert(center.a == 41);
    assert(axial.a == 25);
    assert(diagonal.a == 15);
    assert(two_away.a == 5);
    assert(center.r == 240 && center.g == 40 && center.b == 20);
    assert(axial.r == 240 && axial.g == 40 && axial.b == 20);
    assert(diagonal.r == 240 && diagonal.g == 40 && diagonal.b == 20);
    assert(center.a > axial.a && axial.a > two_away.a);

    // The canvas edge is transparent, not a duplicated edge pixel: the same
    // impulse placed at the corner has the same central weight, and its spread
    // still follows the Gaussian into the visible quadrant.
    std::vector<Color> edge_pixels(static_cast<std::size_t>(width * height), Color::transparent());
    edge_pixels[0] = Color(240, 40, 20, 255);
    ImageFilter::blur(edge_pixels, width, height, 1.0f);
    assert(edge_pixels[0].a == 41);
    assert(edge_pixels[1].a == 25);
    assert(edge_pixels[static_cast<std::size_t>(width + 1)].a == 15);
    assert(edge_pixels[0].r == 240);

    // A zero-radius blur is the identity, including transparent pixels.
    std::vector<Color> unchanged{Color(1, 2, 3, 4), Color::transparent()};
    const auto before = unchanged;
    ImageFilter::blur(unchanged, 2, 1, 0.0f);
    assert(unchanged == before);

    // drop_shadow() shares the blur helper, but must still preserve the
    // unfiltered source over its Gaussian shadow.
    std::vector<Color> shadowed(static_cast<std::size_t>(width * height), Color::transparent());
    shadowed[static_cast<std::size_t>(5 * width + 5)] = Color(240, 40, 20, 255);
    ImageFilter::drop_shadow(shadowed, width, height, 0.0f, 0.0f, 1.0f, Color::black());
    const auto& source = shadowed[static_cast<std::size_t>(5 * width + 5)];
    const auto& axial_shadow = shadowed[static_cast<std::size_t>(5 * width + 6)];
    const auto& diagonal_shadow = shadowed[static_cast<std::size_t>(6 * width + 6)];
    assert(source.r == 240 && source.g == 40 && source.b == 20 && source.a == 255);
    assert(axial_shadow.r == 0 && axial_shadow.g == 0 && axial_shadow.b == 0 && axial_shadow.a == 25);
    assert(diagonal_shadow.r == 0 && diagonal_shadow.g == 0 && diagonal_shadow.b == 0 && diagonal_shadow.a == 15);
}
