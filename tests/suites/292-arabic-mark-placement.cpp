// Phase 292: Arabic composition and MarkToBase coverage are independent.
TEST(arabic_composition_and_mark_to_base_coverage) {
    using namespace litebird;
    auto arabic_font = [](bool composite) {
        std::vector<TestGlyphSpec> glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {0x0627, 600, true, 0, 0, 500, 500}, // alef
            {0x064E, 0, true, 0, 500, 40, 550},  // fatha, CCC 30
            {0x0654, 0, true, 0, 500, 40, 550},  // hamza, CCC 230
            {0x0653, 0, true, 0, 500, 40, 550},  // madda, CCC 230
            {' ', 200, false, 0, 0, 0, 0},
            {0x200C, 0, false, 0, 0, 0, 0},
        };
        if (composite) glyphs.push_back({0x0623, 610, true, 0, 0, 500, 700});
        TrueTypeFont f;
        assert(f.load_memory(build_synthetic_sfnt(1000, 800, -200, glyphs, {})));
        return f;
    };
    auto ids = [](const TrueTypeFont& f, const std::vector<uint32_t>& cps) {
        std::vector<uint16_t> out;
        for (const auto& g : f.shape_glyphs(cps, true, false)) out.push_back(g.glyph);
        return out;
    };
    auto f = arabic_font(true);
    assert((ids(f, {0x0627, 0x0654}) == std::vector<uint16_t>{7}));
    assert((ids(f, {0x0627, 0x064E, 0x0654}) == std::vector<uint16_t>{7, 2}));
    assert((ids(f, {0x0627, 0x0654, 0x064E}) == std::vector<uint16_t>{7, 2}));
    // An unconsumed equal-class mark blocks; do not cross a class-zero barrier.
    assert((ids(f, {0x0627, 0x0653, 0x0654}) == std::vector<uint16_t>{1, 4, 3}));
    assert((ids(f, {0x0627, ' ', 0x0654}) == std::vector<uint16_t>{1, 5, 3}));
    assert((ids(f, {0x0627, 0x200C, 0x0654}) == std::vector<uint16_t>{1, 5, 3}));
    auto missing = arabic_font(false);
    assert((ids(missing, {0x0627, 0x064E, 0x0654}) == std::vector<uint16_t>{1, 2, 3}));
    const std::vector<uint32_t> original{0x0627, 0x064E, 0x0654};
    ids(f, original);
    assert(original[0] == 0x0627 && original[1] == 0x064E && original[2] == 0x0654);

    // Separate synthetic GPOS test: a GDEF ligature-class glyph can be listed
    // in MarkToBase coverage. Coverage admits it, lookup flags can still reject it.
    const std::vector<TestGlyphSpec> glyphs = {
        {0, 500, false, 0, 0, 0, 0},
        {'a', 600, true, 0, 0, 500, 500},
        {'b', 700, true, 0, 0, 500, 500},
        {'c', 800, true, 0, 0, 500, 500},
        {0x0301, 0, true, 0, 500, 40, 550},
    };
    SfntByteBuf gdef;
    gdef.u32(0x00010000u); gdef.u16(12); gdef.u16(0); gdef.u16(0); gdef.u16(0);
    auto classes = build_classdef_fmt2({{1, 1, 1}, {2, 3, 2}, {4, 4, 3}});
    gdef.data.insert(gdef.data.end(), classes.begin(), classes.end());
    SfntByteBuf base;
    base.u16(1); base.u16(0); base.u16(0); base.u16(1); base.u16(0); base.u16(0);
    base.patch_u16(2, static_cast<uint16_t>(base.data.size()));
    auto mc = build_cov_fmt1({4}); base.data.insert(base.data.end(), mc.begin(), mc.end());
    base.patch_u16(4, static_cast<uint16_t>(base.data.size()));
    auto bc = build_cov_fmt1({1, 2}); base.data.insert(base.data.end(), bc.begin(), bc.end());
    const std::size_t ma = base.data.size(); base.patch_u16(8, static_cast<uint16_t>(ma));
    base.u16(1); base.u16(0); base.u16(6);
    base.u16(1); base.i16(100); base.i16(500);
    const std::size_t ba = base.data.size(); base.patch_u16(10, static_cast<uint16_t>(ba));
    base.u16(2); base.u16(6); base.u16(12);
    base.u16(1); base.i16(300); base.i16(800);
    base.u16(1); base.i16(400); base.i16(900);
    auto gpos_font = [&](uint16_t flag) {
        auto gpos = build_gpos_table({{"mark", 4, flag, -1, {base.data}}});
        TrueTypeFont face;
        assert(face.load_memory(build_synthetic_sfnt(1000, 800, -200, glyphs,
                                  {{"GDEF", gdef.data}, {"GPOS", gpos}})));
        return face;
    };
    auto normal = gpos_font(0);
    auto a = normal.shape_glyphs({'a', 0x0301});
    auto b = normal.shape_glyphs({'b', 0x0301});
    auto c = normal.shape_glyphs({'c', 0x0301});
    assert(a.size() == 2 && a[1].x_offset == -400 && a[1].y_offset == 300);
    assert(b.size() == 2 && b[1].x_offset == -400 && b[1].y_offset == 400);
    assert(c.size() == 2 && c[1].x_offset == 0 && c[1].y_offset == 0); // not covered
    auto ignored = gpos_font(4); // IgnoreLigatures
    auto ignored_b = ignored.shape_glyphs({'b', 0x0301});
    assert(ignored_b.size() == 2 && ignored_b[1].x_offset == 0 && ignored_b[1].y_offset == 0);
}
