// Phase 240: use covered canonical parts without inventing missing marks.
TEST(missing_latin_composites_use_only_fully_covered_canonical_parts) {
    using namespace litebird;
    auto font = [](bool acute, bool composite, bool ring_composite) {
        std::vector<TestGlyphSpec> glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {'a', 600, true, 40, 0, 560, 500},
            {0x030A, 0, true, 10, 0, 20, 10},
            {'f', 300, true, 10, 0, 200, 500},
            {'i', 200, true, 10, 0, 100, 500},
        };
        if (acute) glyphs.push_back({0x0301, 0, true, 10, 0, 20, 10});
        if (composite) glyphs.push_back({0x00E1, 710, true, 40, 0, 560, 700});
        if (ring_composite) glyphs.push_back({0x00E5, 720, true, 40, 0, 560, 700});
        TrueTypeFont f;
        assert(f.load_memory(build_synthetic_sfnt(1000, 800, -200, glyphs, {})));
        return f;
    };
    auto f = font(true, false, false);
    auto a = f.shape_glyphs({0x00E1});
    auto b = f.shape_glyphs({'a', 0x0301});
    assert(a.size() == 2 && b.size() == 2);
    for (size_t i = 0; i < a.size(); ++i) {
        assert(a[i].glyph == b[i].glyph && a[i].glyph != 0);
        assert(a[i].x_advance == b[i].x_advance && a[i].x_offset == b[i].x_offset);
    }
    auto nested = f.shape_glyphs({0x01FB}); // a-ring-acute: recurse through a-ring.
    assert(nested.size() == 3);
    assert(nested[0].glyph == f.glyph_index('a'));
    assert(nested[1].glyph == f.glyph_index(0x030A));
    assert(nested[2].glyph == f.glyph_index(0x0301));
    auto partial = font(false, false, false);
    auto refused = partial.shape_glyphs({0x01FB, 'a'});
    assert(refused.size() == 2 && refused[0].glyph == 0);
    assert(refused[1].glyph == partial.glyph_index('a')); // No partial expansion leaked.
    auto direct = font(true, true, false).shape_glyphs({0x00E1});
    assert(direct.size() == 1 && direct[0].x_advance == 710);
    auto intermediate_font = font(true, false, true);
    auto intermediate = intermediate_font.shape_glyphs({0x01FB});
    assert(intermediate.size() == 2);
    assert(intermediate[0].glyph == intermediate_font.glyph_index(0x00E5));
    // Compatibility ligatures are not canonical decompositions.
    auto fi = f.shape_glyphs({0xFB01});
    assert(fi.size() == 1 && fi[0].glyph == 0);
    // Greek is deliberately not part of this Latin-only repair.
    auto greek = f.shape_glyphs({0x03AC});
    assert(greek.size() == 1 && greek[0].glyph == 0);
    const std::vector<uint32_t> original{0x00E1};
    f.shape_glyphs(original);
    assert(original.size() == 1 && original[0] == 0x00E1);
    assert(f.shape_glyphs({}).empty());
}
