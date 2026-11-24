// Phase 250: attachment precedence comes from LookupList order, not lookup type.
TEST(gpos_mark_attachment_respects_lookup_list_order) {
    using namespace litebird;
    const std::vector<TestGlyphSpec> glyphs = {
        {0, 500, false, 0, 0, 0, 0},
        {'a', 600, true, 50, 0, 550, 500},
        {'A', 800, true, 50, 0, 750, 800},
        {0x0301, 0, true, 100, 520, 200, 620},
        {0x0302, 0, true, 100, 520, 200, 620},
    };

    SfntByteBuf gdef;
    gdef.u32(0x00010000u);
    gdef.u16(12);
    gdef.u16(0);
    gdef.u16(0);
    gdef.u16(0);
    auto gcd = build_classdef_fmt2({{1, 2, 1}, {3, 4, 3}});
    gdef.data.insert(gdef.data.end(), gcd.begin(), gcd.end());

    // Type 6 MarkToMark: circ(4) mark1=(150,500) onto acute(3) mark2=(150,700) -> +200 Y lift.
    SfntByteBuf mkmk;
    mkmk.u16(1); // fmt 1
    mkmk.u16(0); // mark1Cov
    mkmk.u16(0); // mark2Cov
    mkmk.u16(1); // classCount = 1
    mkmk.u16(0); // mark1Array
    mkmk.u16(0); // mark2Array
    mkmk.patch_u16(2, static_cast<uint16_t>(mkmk.data.size()));
    auto m1c = build_cov_fmt1({4});
    mkmk.data.insert(mkmk.data.end(), m1c.begin(), m1c.end());
    mkmk.patch_u16(4, static_cast<uint16_t>(mkmk.data.size()));
    auto m2c = build_cov_fmt1({3});
    mkmk.data.insert(mkmk.data.end(), m2c.begin(), m2c.end());
    const std::size_t m1arr = mkmk.data.size();
    mkmk.patch_u16(8, static_cast<uint16_t>(m1arr));
    mkmk.u16(1); // markCount = 1
    mkmk.u16(0); // class 0
    mkmk.u16(6); // anchor offset from m1arr
    mkmk.u16(1); mkmk.i16(150); mkmk.i16(500);
    const std::size_t m2arr = mkmk.data.size();
    mkmk.patch_u16(10, static_cast<uint16_t>(m2arr));
    mkmk.u16(1); // mark2Count = 1
    mkmk.u16(4); // anchor offset from m2arr
    mkmk.u16(1); mkmk.i16(150); mkmk.i16(700);

    // Type 4 MarkToBase: marks {3,4} anchor=(150,500); bases 'a'(1)=(300,500), 'A'(2)=(400,800).
    SfntByteBuf mkbase;
    mkbase.u16(1); // fmt 1
    mkbase.u16(0); // markCov
    mkbase.u16(0); // baseCov
    mkbase.u16(1); // classCount
    mkbase.u16(0); // markArray
    mkbase.u16(0); // baseArray
    mkbase.patch_u16(2, static_cast<uint16_t>(mkbase.data.size()));
    auto mcov = build_cov_fmt1({3, 4});
    mkbase.data.insert(mkbase.data.end(), mcov.begin(), mcov.end());
    mkbase.patch_u16(4, static_cast<uint16_t>(mkbase.data.size()));
    auto bcov = build_cov_fmt1({1, 2});
    mkbase.data.insert(mkbase.data.end(), bcov.begin(), bcov.end());
    const std::size_t marr = mkbase.data.size();
    mkbase.patch_u16(8, static_cast<uint16_t>(marr));
    mkbase.u16(2);
    mkbase.u16(0); mkbase.u16(10); // record 0 -> anchor at marr+10
    mkbase.u16(0); mkbase.u16(10); // record 1 -> shared anchor at marr+10
    mkbase.u16(1); mkbase.i16(150); mkbase.i16(500);
    const std::size_t barr = mkbase.data.size();
    mkbase.patch_u16(10, static_cast<uint16_t>(barr));
    mkbase.u16(2);
    mkbase.u16(6);  // base 'a' anchor at barr+6
    mkbase.u16(12); // base 'A' anchor at barr+12
    mkbase.u16(1); mkbase.i16(300); mkbase.i16(500);
    mkbase.u16(1); mkbase.i16(400); mkbase.i16(800);


    auto make_font = [&](bool base_first) {
        auto gpos = base_first ? build_gpos_table({
            {"mark", 4, 0, -1, {mkbase.data}},
            {"mkmk", 6, 0, -1, {mkmk.data}},
        }) : build_gpos_table({
            {"mkmk", 6, 0, -1, {mkmk.data}},
            {"mark", 4, 0, -1, {mkbase.data}},
        });
        TrueTypeFont f;
        assert(f.load_memory(build_synthetic_sfnt(1000, 900, -200, glyphs,
                              {{"GDEF", gdef.data}, {"GPOS", gpos}})));
        return f;
    };
    auto base_then_mark = make_font(true);
    auto mark_then_base = make_font(false);
    for (bool kern : {false, true}) {
        auto stacked = base_then_mark.shape_glyphs({'A', 0x0301, 0x0302}, kern);
        auto replaced = mark_then_base.shape_glyphs({'A', 0x0301, 0x0302}, kern);
        assert(stacked.size() == 3 && replaced.size() == 3);
        assert(stacked[1].y_offset == 300 && replaced[1].y_offset == 300);
        assert(stacked[2].y_offset == 500); // +200 from the acute's anchor.
        assert(replaced[2].y_offset == 300); // Later base lookup replaces that link.
        assert(stacked[2].x_offset == -550 && replaced[2].x_offset == -550);
        assert(stacked[0].x_advance == 800 && replaced[0].x_advance == 800);
        assert(stacked[1].x_advance == 0 && replaced[2].x_advance == 0);
    }
    // No preceding mark means the mkmk lookup is inapplicable in either order.
    auto a = base_then_mark.shape_glyphs({'A', 0x0302});
    auto b = mark_then_base.shape_glyphs({'A', 0x0302});
    assert(a.size() == 2 && b.size() == 2);
    assert(a[1].y_offset == 300 && b[1].y_offset == 300);
}
