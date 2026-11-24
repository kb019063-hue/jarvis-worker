// Phase 266: stable canonical classes, blocking and font coverage are independent.
TEST(latin_mark_order_and_composition_respect_blocking) {
    using namespace litebird;
    auto make = [](bool composite) {
        std::vector<TestGlyphSpec> glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {'a', 600, true, 0, 0, 500, 500},
            {'x', 600, true, 0, 0, 500, 500},
            {0x0301, 0, true, 0, 500, 50, 550}, // 230
            {0x030D, 0, true, 0, 500, 50, 550}, // 230, no a-composite
            {0x0327, 0, true, 0, -50, 50, 0},  // 202
            {0x0323, 0, true, 0, -50, 50, 0},  // 220
            {0x200C, 0, false, 0, 0, 0, 0},
            {' ', 200, false, 0, 0, 0, 0},
            {0x05E9, 600, true, 0, 0, 500, 500},
        };
        if (composite) glyphs.push_back({0x00E1, 610, true, 0, 0, 500, 700});
        TrueTypeFont f;
        assert(f.load_memory(build_synthetic_sfnt(1000, 800, -200, glyphs, {})));
        return f;
    };
    auto f = make(true);
    auto ids = [](const TrueTypeFont& face, const std::vector<uint32_t>& cps) {
        std::vector<uint16_t> result;
        for (auto g : face.shape_glyphs(cps)) result.push_back(g.glyph);
        return result;
    };
    auto acute = f.glyph_index(0x00E1);
    assert((ids(f, {'a', 0x0327, 0x0301}) == std::vector<uint16_t>{acute, 5}));
    assert(ids(f, {'a', 0x0327, 0x0301}) == ids(f, {'a', 0x0301, 0x0327}));
    assert((ids(f, {'x', 0x0301, 0x0323, 0x0327}) == std::vector<uint16_t>{2, 5, 6, 3}));
    assert((ids(f, {'a', 0x030D, 0x0301}) == std::vector<uint16_t>{1, 4, 3}));
    assert((ids(f, {'a', 0x0301, 0x030D}) == std::vector<uint16_t>{acute, 4}));
    // Equal-class repetitions retain order and count. No merging across space/control.
    assert((ids(f, {'x', 0x030D, 0x0301, 0x030D}) == std::vector<uint16_t>{2, 4, 3, 4}));
    assert((ids(f, {'a', ' ', 0x0301}) == std::vector<uint16_t>{1, 8, 3}));
    // A joiner is a composition barrier *and* a glyph slot. `{1, 3}` here used
    // to encode both facts at once, but the second was the erase bug Phase 267
    // (finding 271) fixed: the slot now stays, with zero advance. The barrier
    // is what this line is about, so it is asserted separately from the slots.
    assert((ids(f, {'a', 0x200C, 0x0301}) == std::vector<uint16_t>{1, 8, 3}));
    assert((f.shape_glyphs({'a', 0x200C, 0x0301})[1].x_advance == 0));
    // Leading marks and non-Latin bases are deliberately outside this repair.
    assert((ids(f, {0x0301, 0x0323}) == std::vector<uint16_t>{3, 6}));
    assert((ids(f, {0x05E9, 0x0301, 0x0323}) == std::vector<uint16_t>{9, 3, 6}));
    auto missing = make(false);
    assert((ids(missing, {'a', 0x0301, 0x0327}) == std::vector<uint16_t>{1, 5, 3}));
    const std::vector<uint32_t> input{'x', 0x0301, 0x0323};
    ids(f, input);
    assert(input[1] == 0x0301 && input[2] == 0x0323);
}
