// Phase 432: Arabic GDEF combining-mark advances (`ZERO_WIDTH_MARKS_BY_GDEF_LATE`).

TEST(arabic_gdef_marks_zero_advances_after_gpos) {
    using namespace litebird;

    // 1. Monospaced Arabic + Latin synthetic SFNT with full-cell 1200-unit `hmtx`
    //    advances on both bases and marks, `MarkToBase` + `MarkToMark` under `arab`,
    //    and NO `SinglePos` advance-zeroing lookup.
    //    gid 0=.notdef, 1=U+0628 BEH, 2=U+0645 MEEM, 3=U+064E FATHA,
    //    4=U+0650 KASRA, 5=U+0651 SHADDA, 6='x', 7=U+0301 COMBINING ACUTE.
    const std::vector<TestGlyphSpec> glyphs = {
        {0, 500, false, 0, 0, 0, 0},
        {0x0628, 1200, true, 80, 0, 1120, 420},
        {0x0645, 1200, true, 80, 0, 1120, 450},
        {0x064E, 1200, true, 120, 540, 280, 640},
        {0x0650, 1200, true, 120, -180, 280, -80},
        {0x0651, 1200, true, 120, 540, 280, 640},
        {'x', 1200, true, 80, 0, 1120, 500},
        {0x0301, 1200, true, 120, 540, 280, 640},
    };

    SfntByteBuf gdef_marks;
    gdef_marks.u32(0x00010000u);
    gdef_marks.u16(12); gdef_marks.u16(0); gdef_marks.u16(0); gdef_marks.u16(0);
    auto gcd_marks = build_classdef_fmt2({{1, 2, 1}, {3, 5, 3}, {6, 6, 1}, {7, 7, 3}});
    gdef_marks.data.insert(gdef_marks.data.end(), gcd_marks.begin(), gcd_marks.end());

    // MarkToBase (2 mark classes: class 0 above for 3,5; class 1 below for 4)
    // Marks: 3=(200,600) cls 0, 4=(200,-100) cls 1, 5=(200,600) cls 0.
    // Bases: 1=(600,700)/(600,-250), 2=(600,700)/(600,-250).
    // -> above dx=+400, dy=+100; below dx=+400, dy=-150.
    SfntByteBuf mkbase;
    mkbase.u16(1); mkbase.u16(0); mkbase.u16(0); mkbase.u16(2); mkbase.u16(0); mkbase.u16(0);
    mkbase.patch_u16(2, static_cast<uint16_t>(mkbase.data.size()));
    auto mc = build_cov_fmt1({3, 4, 5});
    mkbase.data.insert(mkbase.data.end(), mc.begin(), mc.end());
    mkbase.patch_u16(4, static_cast<uint16_t>(mkbase.data.size()));
    auto bc = build_cov_fmt1({1, 2});
    mkbase.data.insert(mkbase.data.end(), bc.begin(), bc.end());
    const std::size_t ma_pos = mkbase.data.size();
    mkbase.patch_u16(8, static_cast<uint16_t>(ma_pos));
    mkbase.u16(3);
    mkbase.u16(0); mkbase.u16(14);
    mkbase.u16(1); mkbase.u16(20);
    mkbase.u16(0); mkbase.u16(26);
    mkbase.u16(1); mkbase.i16(200); mkbase.i16(600);
    mkbase.u16(1); mkbase.i16(200); mkbase.i16(-100);
    mkbase.u16(1); mkbase.i16(200); mkbase.i16(600);
    const std::size_t ba_pos = mkbase.data.size();
    mkbase.patch_u16(10, static_cast<uint16_t>(ba_pos));
    mkbase.u16(2);
    mkbase.u16(10); mkbase.u16(16);
    mkbase.u16(10); mkbase.u16(16);
    mkbase.u16(1); mkbase.i16(600); mkbase.i16(700);
    mkbase.u16(1); mkbase.i16(600); mkbase.i16(-250);

    // MarkToMark: FATHA(3) mark1=(200,600) onto SHADDA(5) mark2=(200,820) -> dx=0, dy=+220.
    SfntByteBuf mkmk;
    mkmk.u16(1); mkmk.u16(0); mkmk.u16(0); mkmk.u16(1); mkmk.u16(0); mkmk.u16(0);
    mkmk.patch_u16(2, static_cast<uint16_t>(mkmk.data.size()));
    auto m1c = build_cov_fmt1({3});
    mkmk.data.insert(mkmk.data.end(), m1c.begin(), m1c.end());
    mkmk.patch_u16(4, static_cast<uint16_t>(mkmk.data.size()));
    auto m2c = build_cov_fmt1({5});
    mkmk.data.insert(mkmk.data.end(), m2c.begin(), m2c.end());
    const std::size_t m1a = mkmk.data.size();
    mkmk.patch_u16(8, static_cast<uint16_t>(m1a));
    mkmk.u16(1); mkmk.u16(0); mkmk.u16(6);
    mkmk.u16(1); mkmk.i16(200); mkmk.i16(600);
    const std::size_t m2a = mkmk.data.size();
    mkmk.patch_u16(10, static_cast<uint16_t>(m2a));
    mkmk.u16(1); mkmk.u16(4);
    mkmk.u16(1); mkmk.i16(200); mkmk.i16(820);

    auto gpos_mono = build_otl_table(
        {{"arab", {{"mark", {0}}, {"mkmk", {1}}}}},
        {
            {"mark", 4, 0x0001, -1, {mkbase.data}},
            {"mkmk", 6, 0x0001, -1, {mkmk.data}},
        });

    TrueTypeFont mono_arab;
    assert(mono_arab.load_memory(build_synthetic_sfnt(
        1000, 800, -200, glyphs, {{"GDEF", gdef_marks.data}, {"GPOS", gpos_mono}})));

    // Single mark: advance zeroed, offset compensated against the 1200-unit base only.
    const auto beh_fatha = mono_arab.shape_glyphs({0x0628, 0x064E}, true, false);
    assert(beh_fatha.size() == 2);
    assert(beh_fatha[0].glyph == 1 && beh_fatha[0].x_advance == 1200 && beh_fatha[0].y_advance == 0);
    assert(beh_fatha[1].glyph == 3 && beh_fatha[1].x_advance == 0 && beh_fatha[1].y_advance == 0);
    assert(beh_fatha[1].x_offset == -800 && beh_fatha[1].y_offset == 100);

    // `font-kerning: none` still zeroes Arabic GDEF marks.
    const auto beh_fatha_nokern = mono_arab.shape_glyphs({0x0628, 0x064E}, false, false);
    assert(beh_fatha_nokern.size() == 2 && beh_fatha_nokern[1].x_advance == 0 &&
           beh_fatha_nokern[1].x_offset == -800);

    // Two MarkToBase marks on the same base (above + below): Stage 4b sums advances
    // across [base, mark2), so mark1 must already be zeroed before Stage 4b runs.
    const auto two_marks = mono_arab.shape_glyphs({0x0628, 0x064E, 0x0650}, true, false);
    assert(two_marks.size() == 3);
    assert(two_marks[0].x_advance == 1200);
    assert(two_marks[1].x_advance == 0 && two_marks[1].x_offset == -800 && two_marks[1].y_offset == 100);
    assert(two_marks[2].x_advance == 0 && two_marks[2].x_offset == -800 && two_marks[2].y_offset == -150);

    // Stacked MarkToMark (BEH + SHADDA + FATHA): both marks zero-advance, FATHA lifted by +220.
    const auto stacked = mono_arab.shape_glyphs({0x0628, 0x0651, 0x064E}, true, false);
    assert(stacked.size() == 3);
    assert(stacked[0].x_advance == 1200);
    assert(stacked[1].x_advance == 0 && stacked[1].x_offset == -800 && stacked[1].y_offset == 100);
    assert(stacked[2].x_advance == 0 && stacked[2].x_offset == -800 && stacked[2].y_offset == 320);

    // Visual RTL multi-cluster: `[FATHA, MEEM, BEH]` keeps total pen at 2400 (not 3600).
    const auto rtl_two = mono_arab.shape_glyphs({0x0628, 0x0645, 0x064E}, true, true);
    assert(rtl_two.size() == 3);
    assert(rtl_two[0].glyph == 3 && rtl_two[0].x_advance == 0 && rtl_two[0].x_offset == 400);
    assert(rtl_two[1].glyph == 2 && rtl_two[1].x_advance == 1200);
    assert(rtl_two[2].glyph == 1 && rtl_two[2].x_advance == 1200);

    // 2. Timing control (`ZERO_WIDTH_MARKS_BY_GDEF_LATE`):
    //    - A font that ALSO carries `SinglePos` `x_advance = -1200` under `arab`
    //      ends at `0` (not `-1200` as pre-GPOS zeroing would produce), while
    //      preserving `SinglePos` placement offsets `(35, -15)`.
    //    - A `SinglePos` with nonzero `(x_advance = +90, y_advance = +40)` on a
    //      GDEF class-3 mark is zeroed after GPOS while keeping `(35, -15)`.
    auto make_single_pos_font = [&](int16_t dx_adv, int16_t dy_adv) {
        SfntByteBuf sp;
        sp.u16(1); sp.u16(14); sp.u16(0x000Fu); // XPlacement|YPlacement|XAdvance|YAdvance
        sp.i16(35); sp.i16(-15); sp.i16(dx_adv); sp.i16(dy_adv);
        auto spc = build_cov_fmt1({3});
        sp.data.insert(sp.data.end(), spc.begin(), spc.end());
        auto gpos = build_otl_table({{"arab", {{"mark", {0}}}}},
                                    {{"mark", 1, 0x0000, -1, {sp.data}}});
        TrueTypeFont f;
        assert(f.load_memory(build_synthetic_sfnt(
            1000, 800, -200, glyphs, {{"GDEF", gdef_marks.data}, {"GPOS", gpos}})));
        return f;
    };
    const auto explicit_zero = make_single_pos_font(-1200, 0).shape_glyphs({0x0628, 0x064E}, true, false);
    assert(explicit_zero.size() == 2 && explicit_zero[1].x_advance == 0 && explicit_zero[1].y_advance == 0);
    assert(explicit_zero[1].x_offset == 35 && explicit_zero[1].y_offset == -15);

    const auto nonzero_gpos = make_single_pos_font(90, 40).shape_glyphs({0x0628, 0x064E}, true, false);
    assert(nonzero_gpos.size() == 2 && nonzero_gpos[1].x_advance == 0 && nonzero_gpos[1].y_advance == 0);
    assert(nonzero_gpos[1].x_offset == 35 && nonzero_gpos[1].y_offset == -15);

    // 3. Negative controls:
    //    - Non-mark GDEF class (`GlyphClassDef == 1`) is NOT zeroed in `arab`.
    //    - Missing `GDEF` `GlyphClassDef` leaves raw `hmtx` advance untouched.
    //    - Non-Arabic script (`latn`) without `SinglePos` keeps its `hmtx` advance.
    SfntByteBuf gdef_all_base;
    gdef_all_base.u32(0x00010000u);
    gdef_all_base.u16(12); gdef_all_base.u16(0); gdef_all_base.u16(0); gdef_all_base.u16(0);
    auto gcd_base = build_classdef_fmt2({{1, 7, 1}});
    gdef_all_base.data.insert(gdef_all_base.data.end(), gcd_base.begin(), gcd_base.end());

    TrueTypeFont arab_base_class;
    assert(arab_base_class.load_memory(build_synthetic_sfnt(
        1000, 800, -200, glyphs, {{"GDEF", gdef_all_base.data}, {"GPOS", gpos_mono}})));
    const auto non_mark_cls = arab_base_class.shape_glyphs({0x0628, 0x064E}, true, false);
    assert(non_mark_cls.size() == 2 && non_mark_cls[1].x_advance == 1200);

    TrueTypeFont arab_no_gdef;
    assert(arab_no_gdef.load_memory(build_synthetic_sfnt(
        1000, 800, -200, glyphs, {{"GPOS", gpos_mono}})));
    const auto no_gdef = arab_no_gdef.shape_glyphs({0x0628, 0x064E}, true, false);
    assert(no_gdef.size() == 2 && no_gdef[1].x_advance == 1200);

    const auto latn_unzeroed = mono_arab.shape_glyphs({'x', 0x0301}, true, false);
    assert(latn_unzeroed.size() == 2 && latn_unzeroed[1].x_advance == 1200);

    // 4. Host `DejaVu Sans Mono` end-to-end check when installed.
    FontRenderer& fr = FontRenderer::instance();
    const std::string mono_path = fr.face_path("DejaVu Sans Mono");
    if (!mono_path.empty()) {
        TrueTypeFont host_mono;
        if (host_mono.load_file(mono_path) && host_mono.has_gpos()) {
            const auto plain_beh = host_mono.shape_glyphs({0x0628}, true, true);
            const auto marked_beh = host_mono.shape_glyphs({0x0628, 0x064E}, true, true);
            assert(plain_beh.size() == 1 && marked_beh.size() == 2);
            assert(marked_beh[0].x_advance + marked_beh[1].x_advance == plain_beh[0].x_advance);
            assert(marked_beh[0].is_mark && marked_beh[0].x_advance == 0 && marked_beh[0].y_advance == 0);

            const auto greeting = host_mono.shape_glyphs(
                {0x0627, 0x0644, 0x0633, 0x0651, 0x064E, 0x0644, 0x064E, 0x0627, 0x0645, 0x064F},
                true, true);
            int32_t total_adv = 0;
            for (const auto& pg : greeting) {
                if (pg.is_mark) assert(pg.x_advance == 0 && pg.y_advance == 0);
                total_adv += pg.x_advance;
            }
            assert(total_adv == 5 * plain_beh[0].x_advance);

            CanvasRenderingContext2D ctx(160, 60);
            ctx.font = "24px 'DejaVu Sans Mono'";
            const double w_plain = ctx.measure_text("\xD8\xA8").width;
            const double w_marked = ctx.measure_text("\xD8\xA8\xD9\x8E").width;
            assert(w_plain > 0.0 && std::abs(w_plain - w_marked) < 1e-4);
        }
    }
}
