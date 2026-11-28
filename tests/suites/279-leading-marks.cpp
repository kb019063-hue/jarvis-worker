// Phase 279: canonical order is a property of a mark span, not of its base.
TEST(leading_latin_resolved_marks_sort_without_inventing_a_base) {
    using namespace litebird;
    const std::vector<TestGlyphSpec> glyphs = {
        {0, 500, false, 0, 0, 0, 0},
        {0x0301, 0, true, 0, 500, 40, 550}, // 230
        {0x0323, 0, true, 0, -50, 40, 0},  // 220
        {0x0327, 0, true, 0, -50, 40, 0},  // 202
        {0x030D, 0, true, 0, 500, 40, 550}, // 230
        {' ', 200, false, 0, 0, 0, 0},
        {0x200C, 0, false, 0, 0, 0, 0},
        {0x25CC, 600, true, 0, 0, 500, 500},
        {'a', 600, true, 0, 0, 500, 500},
        {0x05E9, 600, true, 0, 0, 500, 500},
        {0x034F, 0, false, 0, 0, 0, 0}, // CGJ has class zero
        {0x03B1, 600, true, 0, 0, 500, 500},
        {0x0915, 600, true, 0, 0, 500, 500},
    };
    TrueTypeFont f;
    assert(f.load_memory(build_synthetic_sfnt(1000, 800, -200, glyphs, {})));
    auto ids = [&](const std::vector<uint32_t>& cps) {
        std::vector<uint16_t> out;
        for (auto g : f.shape_glyphs(cps)) out.push_back(g.glyph);
        return out;
    };
    assert((ids({0x0301, 0x0323, 0x0327}) == std::vector<uint16_t>{3, 2, 1}));
    assert((ids({0x0327, 0x0323, 0x0301}) == std::vector<uint16_t>{3, 2, 1}));
    assert((ids({0x030D, 0x0301, 0x030D}) == std::vector<uint16_t>{4, 1, 4}));
    assert((ids({0x0301, 0x0323, 'a'}) == std::vector<uint16_t>{2, 1, 8}));
    assert((ids({' ', 0x0301, 0x0323}) == std::vector<uint16_t>{5, 2, 1}));
    assert((ids({0x200C, 0x0301, 0x0323}) == std::vector<uint16_t>{5, 2, 1}));
    assert((ids({0x25CC, 0x0301, 0x0323}) == std::vector<uint16_t>{7, 2, 1}));
    // No crossing a class-zero character (CGJ hides to zero-advance space);
    // Hebrew and Greek runs order (Phase 476/501), while deferred `dev2` stays unchanged.
    assert((ids({0x0301, 0x034F, 0x0323}) == std::vector<uint16_t>{1, 5, 2}));
    assert((f.shape_glyphs({0x0301, 0x034F, 0x0323})[1].x_advance == 0));
    assert((ids({0x0301, ' ', 0x0323}) == std::vector<uint16_t>{1, 5, 2}));
    assert((ids({0x05E9, 0x0301, 0x0323}) == std::vector<uint16_t>{9, 2, 1}));
    assert((ids({0x0301, 0x0323, 0x05E9}) == std::vector<uint16_t>{2, 1, 9}));
    assert((ids({0x03B1, 0x0301, 0x0323}) == std::vector<uint16_t>{11, 2, 1}));
    assert((ids({0x0301, 0x0323, 0x03B1}) == std::vector<uint16_t>{2, 1, 11}));
    assert((ids({0x0915, 0x0301, 0x0323}) == std::vector<uint16_t>{12, 1, 2}));
    assert((ids({0x0301, 0x0323, 0x0915}) == std::vector<uint16_t>{1, 2, 12}));
    // Covered dotted circle is not inserted for an isolated Latin-resolved mark.
    auto single = f.shape_glyphs({0x0301});
    assert(single.size() == 1 && single[0].glyph == 1 && single[0].x_advance == 0);
    const std::vector<uint32_t> original{0x0301, 0x0323};
    ids(original);
    assert(original[0] == 0x0301 && original[1] == 0x0323);
    assert(ids({}).empty());
}
