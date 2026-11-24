// Phase 270: expose a supported composite only when following marks need normalization.
TEST(supported_latin_composites_expose_covered_parts_before_marks) {
    using namespace litebird;
    auto make = [](bool acute_covered) {
        std::vector<TestGlyphSpec> glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {'a', 600, true, 0, 0, 500, 500},
            {0x0323, 0, true, 0, -50, 40, 0},
            {0x00E1, 710, true, 0, 0, 500, 700},
            {0x1EA1, 720, true, 0, -50, 500, 500},
            {0x030D, 0, true, 0, 500, 40, 600},
            {0x030A, 0, true, 0, 500, 40, 600},
            {0x00E5, 730, true, 0, 0, 500, 700},
            {0x01FB, 740, true, 0, 0, 500, 800},
            {' ', 200, false, 0, 0, 0, 0},
            {0x200C, 0, false, 0, 0, 0, 0},
        };
        if (acute_covered) glyphs.push_back({0x0301, 0, true, 0, 500, 40, 600});
        TrueTypeFont f;
        assert(f.load_memory(build_synthetic_sfnt(1000, 800, -200, glyphs, {})));
        return f;
    };
    auto f = make(true);
    auto ids = [](const TrueTypeFont& font, const std::vector<uint32_t>& cps) {
        std::vector<uint16_t> out;
        for (auto g : font.shape_glyphs(cps)) out.push_back(g.glyph);
        return out;
    };
    auto normalized = ids(f, {0x00E1, 0x0323});
    assert(normalized == ids(f, {'a', 0x0301, 0x0323}));
    assert((normalized == std::vector<uint16_t>{4, 11})); // dot composite, then acute
    assert(ids(f, {0x01FB, 0x0323}) == ids(f, {'a', 0x030A, 0x0301, 0x0323}));
    assert((ids(f, {0x00E1, 0x030D}) == std::vector<uint16_t>{3, 5}));
    auto alone = f.shape_glyphs({0x00E1});
    assert(alone.size() == 1 && alone[0].glyph == 3 && alone[0].x_advance == 710);
    assert((ids(f, {0x00E1, ' ', 0x0323}) == std::vector<uint16_t>{3, 9, 2}));
    assert((ids(f, {0x00E1, 0x200C, 0x0323}) == std::vector<uint16_t>{3, 9, 2}));
    auto missing = make(false);
    // The acute part is absent: rollback must retain the available original glyph.
    assert((ids(missing, {0x00E1, 0x0323}) == std::vector<uint16_t>{3, 2}));
    assert((ids(missing, {0x01FB, 0x0323}) == std::vector<uint16_t>{8, 2}));
    const std::vector<uint32_t> original{0x00E1, 0x0323};
    ids(f, original);
    assert(original[0] == 0x00E1 && original.size() == 2);
}
