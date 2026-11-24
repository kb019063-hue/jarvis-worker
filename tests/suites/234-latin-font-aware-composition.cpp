// Phase 234: composition uses canonical pairs and the current face's coverage.
TEST(latin_composition_asks_the_font_before_replacing_a_pair) {
    using namespace litebird;
    auto make_font = [](bool coverage) {
        std::vector<TestGlyphSpec> glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {'a', 600, true, 40, 0, 560, 500},
            {0x0301, 0, true, 10, 0, 20, 10},
            {0x030A, 0, true, 10, 0, 20, 10},
            {0x0323, 0, true, 10, 0, 20, 10},
            {'f', 300, true, 10, 0, 200, 500},
            {'i', 200, true, 10, 0, 100, 500},
        };
        if (coverage) {
            glyphs.push_back({0x00E1, 610, true, 40, 0, 560, 700});
            glyphs.push_back({0x00E5, 620, true, 40, 0, 560, 700});
            glyphs.push_back({0x01FB, 630, true, 40, 0, 560, 800});
            glyphs.push_back({0xFB01, 400, true, 40, 0, 360, 500});
        }
        TrueTypeFont f;
        assert(f.load_memory(build_synthetic_sfnt(1000, 800, -200, glyphs, {})));
        return f;
    };
    auto f = make_font(true);
    auto s = f.shape_glyphs({'a', 0x0301});
    assert(s.size() == 1 && s[0].glyph == f.glyph_index(0x00E1));
    assert(s[0].x_advance == 610); // The composite's metrics, not the base's.
    auto pre = f.shape_glyphs({0x00E1});
    assert(pre.size() == 1 && pre[0].glyph == s[0].glyph);
    auto chain = f.shape_glyphs({'a', 0x030A, 0x0301});
    assert(chain.size() == 1 && chain[0].glyph == f.glyph_index(0x01FB));
    auto bare = make_font(false);
    auto fallback = bare.shape_glyphs({'a', 0x0301});
    assert(fallback.size() == 2 && fallback[0].glyph == bare.glyph_index('a'));
    assert(fallback[1].glyph == bare.glyph_index(0x0301));
    // No compatibility composition or mutation of input. Phase 266 allows
    // composition across the lower-class dot while preserving that dot.
    assert(f.shape_glyphs({'f', 'i'}).size() == 2);
    auto crossed = f.shape_glyphs({'a', 0x0323, 0x0301});
    assert(crossed.size() == 2 && crossed[0].glyph == f.glyph_index(0x00E1));
    assert(crossed[1].glyph == f.glyph_index(0x0323));
    assert(f.shape_glyphs({0x0301, 'a'}).size() == 2);
    const std::vector<uint32_t> input{'a', 0x0301};
    f.shape_glyphs(input);
    assert(input.size() == 2 && input[0] == 'a');
    assert(f.shape_glyphs({}).empty());
}
