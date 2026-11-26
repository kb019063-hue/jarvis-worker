// Phase 467, finding 467: RTL visual buffer reversal (`[mark_k, ..., mark_1, base]`),
// HarfBuzz-compatible RTL mark attachment offset propagation across `(j, i]`, and
// unanchored monospaced composite mark origins.

TEST(rtl_mark_order_attachment_offsets_and_unanchored_origins) {
    using namespace litebird;

    struct Origin {
        uint16_t glyph;
        int32_t x;
        int32_t y;
        bool operator==(const Origin&) const = default;
        bool operator<(const Origin& o) const {
            if (glyph != o.glyph) return glyph < o.glyph;
            if (x != o.x) return x < o.x;
            return y < o.y;
        }
    };
    auto origins_and_pen = [](const std::vector<TrueTypeFont::PositionedGlyph>& run) {
        std::vector<Origin> origins;
        int32_t px = 0;
        int32_t py = 0;
        for (const auto& g : run) {
            origins.push_back({g.glyph, px + g.x_offset, py + g.y_offset});
            px += g.x_advance;
            py += g.y_advance;
        }
        std::sort(origins.begin(), origins.end());
        return std::make_pair(origins, std::make_pair(px, py));
    };

    // 1. Monospaced Arabic SFNT with:
    //    - anchored bases (gid 1 = U+0628 BEH, gid 2 = U+0645 MEEM, gid 6 = U+062A TEH),
    //    - an unanchored composite base (gid 7 = U+0622 ALEF WITH MADDA ABOVE, omitted from BaseCoverage),
    //    - GDEF class-3 marks (gid 3 = U+064E FATHA, gid 4 = U+0650 KASRA, gid 5 = U+0651 SHADDA),
    //    - RTL CursivePos (`Beh -> Teh`), MarkToBase (`1, 2`), and MarkToMark (`Fatha -> Shadda`),
    //    - Latin `'a'`, `'b'`, and `U+0301` (`MarkToBase` on `'b'`) for `<bdo dir="rtl">` cluster checks.
    const std::vector<TestGlyphSpec> glyphs = {
        {0, 500, false, 0, 0, 0, 0},
        {0x0628, 1200, true, 80, 0, 1120, 420},    // 1: BEH
        {0x0645, 1200, true, 80, 0, 1120, 450},    // 2: MEEM
        {0x064E, 1200, true, 120, 540, 280, 640},  // 3: FATHA
        {0x0650, 1200, true, 120, -180, 280, -80}, // 4: KASRA
        {0x0651, 1200, true, 120, 540, 280, 640},  // 5: SHADDA
        {0x062A, 1200, true, 80, 0, 1120, 440},    // 6: TEH
        {0x0622, 1200, true, 80, 0, 1120, 700},    // 7: ALEF WITH MADDA ABOVE (unanchored)
        {'a', 600, true, 40, 0, 560, 500},         // 8: 'a'
        {'b', 600, true, 40, 0, 560, 650},         // 9: 'b'
        {0x0301, 0, true, 100, 520, 220, 660},     // 10: U+0301 COMBINING ACUTE
    };

    SfntByteBuf gdef;
    gdef.u32(0x00010000u);
    gdef.u16(12); gdef.u16(0); gdef.u16(0); gdef.u16(0);
    auto gcd = build_classdef_fmt2({{1, 2, 1}, {3, 5, 3}, {6, 9, 1}, {10, 10, 3}});
    gdef.data.insert(gdef.data.end(), gcd.begin(), gcd.end());

    auto put_anchor = [](SfntByteBuf& b, std::size_t off_pos, int16_t x, int16_t y) {
        b.patch_u16(off_pos, static_cast<uint16_t>(b.data.size()));
        b.u16(1); b.i16(x); b.i16(y);
    };

    // Lookup 0 (`curs`, RTL | IgnoreMarks = 0x0009): BEH(1) exit=(40, 140) -> TEH(6) entry=(560, 20).
    SfntByteBuf cur_rtl;
    cur_rtl.u16(1); cur_rtl.u16(0); cur_rtl.u16(2);
    for (int i = 0; i < 4; ++i) cur_rtl.u16(0);
    cur_rtl.patch_u16(2, static_cast<uint16_t>(cur_rtl.data.size()));
    auto cur_cov = build_cov_fmt1({1, 6});
    cur_rtl.data.insert(cur_rtl.data.end(), cur_cov.begin(), cur_cov.end());
    put_anchor(cur_rtl, 6 + 0 * 4 + 2, 40, 140);  // BEH exit
    put_anchor(cur_rtl, 6 + 1 * 4 + 0, 560, 20);  // TEH entry

    // Lookup 1 (`mark` under `arab`): marks {3, 4, 5} onto bases {1, 2} (gid 7 deliberately unanchored).
    // Above dx = 600 - 200 = +400, dy = 700 - 600 = +100; below dx = +400, dy = -250 - (-100) = -150.
    SfntByteBuf mkbase_arab;
    mkbase_arab.u16(1); mkbase_arab.u16(0); mkbase_arab.u16(0); mkbase_arab.u16(2); mkbase_arab.u16(0); mkbase_arab.u16(0);
    mkbase_arab.patch_u16(2, static_cast<uint16_t>(mkbase_arab.data.size()));
    auto mc_arab = build_cov_fmt1({3, 4, 5});
    mkbase_arab.data.insert(mkbase_arab.data.end(), mc_arab.begin(), mc_arab.end());
    mkbase_arab.patch_u16(4, static_cast<uint16_t>(mkbase_arab.data.size()));
    auto bc_arab = build_cov_fmt1({1, 2});
    mkbase_arab.data.insert(mkbase_arab.data.end(), bc_arab.begin(), bc_arab.end());
    mkbase_arab.patch_u16(8, static_cast<uint16_t>(mkbase_arab.data.size()));
    mkbase_arab.u16(3);
    mkbase_arab.u16(0); mkbase_arab.u16(14);
    mkbase_arab.u16(1); mkbase_arab.u16(20);
    mkbase_arab.u16(0); mkbase_arab.u16(26);
    mkbase_arab.u16(1); mkbase_arab.i16(200); mkbase_arab.i16(600);
    mkbase_arab.u16(1); mkbase_arab.i16(200); mkbase_arab.i16(-100);
    mkbase_arab.u16(1); mkbase_arab.i16(200); mkbase_arab.i16(600);
    mkbase_arab.patch_u16(10, static_cast<uint16_t>(mkbase_arab.data.size()));
    mkbase_arab.u16(2);
    mkbase_arab.u16(10); mkbase_arab.u16(16);
    mkbase_arab.u16(10); mkbase_arab.u16(16);
    mkbase_arab.u16(1); mkbase_arab.i16(600); mkbase_arab.i16(700);
    mkbase_arab.u16(1); mkbase_arab.i16(600); mkbase_arab.i16(-250);

    // Lookup 2 (`mkmk` under `arab`): FATHA(3) onto SHADDA(5), dx = 0, dy = 820 - 600 = +220.
    SfntByteBuf mkmk_arab;
    mkmk_arab.u16(1); mkmk_arab.u16(0); mkmk_arab.u16(0); mkmk_arab.u16(1); mkmk_arab.u16(0); mkmk_arab.u16(0);
    mkmk_arab.patch_u16(2, static_cast<uint16_t>(mkmk_arab.data.size()));
    auto m1c = build_cov_fmt1({3});
    mkmk_arab.data.insert(mkmk_arab.data.end(), m1c.begin(), m1c.end());
    mkmk_arab.patch_u16(4, static_cast<uint16_t>(mkmk_arab.data.size()));
    auto m2c = build_cov_fmt1({5});
    mkmk_arab.data.insert(mkmk_arab.data.end(), m2c.begin(), m2c.end());
    mkmk_arab.patch_u16(8, static_cast<uint16_t>(mkmk_arab.data.size()));
    mkmk_arab.u16(1); mkmk_arab.u16(0); mkmk_arab.u16(6);
    mkmk_arab.u16(1); mkmk_arab.i16(200); mkmk_arab.i16(600);
    mkmk_arab.patch_u16(10, static_cast<uint16_t>(mkmk_arab.data.size()));
    mkmk_arab.u16(1); mkmk_arab.u16(4);
    mkmk_arab.u16(1); mkmk_arab.i16(200); mkmk_arab.i16(820);

    // Lookup 3 (`mark` under `latn`): ACUTE(10) mark=(150, 500) onto 'b'(9) base=(300, 680) -> dx=+150, dy=+180.
    SfntByteBuf mkbase_latn;
    mkbase_latn.u16(1); mkbase_latn.u16(0); mkbase_latn.u16(0); mkbase_latn.u16(1); mkbase_latn.u16(0); mkbase_latn.u16(0);
    mkbase_latn.patch_u16(2, static_cast<uint16_t>(mkbase_latn.data.size()));
    auto mc_latn = build_cov_fmt1({10});
    mkbase_latn.data.insert(mkbase_latn.data.end(), mc_latn.begin(), mc_latn.end());
    mkbase_latn.patch_u16(4, static_cast<uint16_t>(mkbase_latn.data.size()));
    auto bc_latn = build_cov_fmt1({9});
    mkbase_latn.data.insert(mkbase_latn.data.end(), bc_latn.begin(), bc_latn.end());
    mkbase_latn.patch_u16(8, static_cast<uint16_t>(mkbase_latn.data.size()));
    mkbase_latn.u16(1); mkbase_latn.u16(0); mkbase_latn.u16(6);
    mkbase_latn.u16(1); mkbase_latn.i16(150); mkbase_latn.i16(500);
    mkbase_latn.patch_u16(10, static_cast<uint16_t>(mkbase_latn.data.size()));
    mkbase_latn.u16(1); mkbase_latn.u16(4);
    mkbase_latn.u16(1); mkbase_latn.i16(300); mkbase_latn.i16(680);

    auto gpos_bytes = build_otl_table(
        {
            {"arab", {{"curs", {0}}, {"mark", {1}}, {"mkmk", {2}}}},
            {"latn", {{"mark", {3}}}},
        },
        {
            {"curs", 3, 0x0009, -1, {cur_rtl.data}},
            {"mark", 4, 0x0001, -1, {mkbase_arab.data}},
            {"mkmk", 6, 0x0001, -1, {mkmk_arab.data}},
            {"mark", 4, 0x0000, -1, {mkbase_latn.data}},
        });

    const auto sfnt_bytes = build_synthetic_sfnt(
        1000, 800, -200, glyphs, {{"GDEF", gdef.data}, {"GPOS", gpos_bytes}});
    TrueTypeFont f;
    assert(f.load_memory(sfnt_bytes));

    // 1a. Stacked MarkToMark (`BEH + SHADDA + FATHA`):
    //     Logical (`visual_rtl = false`): `[BEH(1), SHADDA(5), FATHA(3)]` with `-800` offsets.
    //     Visual RTL (`visual_rtl = true`): `[FATHA(3), SHADDA(5), BEH(1)]` with `+400` offsets.
    const auto stacked_log = f.shape_glyphs({0x0628, 0x0651, 0x064E}, true, false);
    const auto stacked_rtl = f.shape_glyphs({0x0628, 0x0651, 0x064E}, true, true);
    assert(stacked_log.size() == 3 && stacked_rtl.size() == 3);
    assert(stacked_rtl[0].glyph == 3 && stacked_rtl[0].x_advance == 0 &&
           stacked_rtl[0].x_offset == 400 && stacked_rtl[0].y_offset == 320 && stacked_rtl[0].is_mark);
    assert(stacked_rtl[1].glyph == 5 && stacked_rtl[1].x_advance == 0 &&
           stacked_rtl[1].x_offset == 400 && stacked_rtl[1].y_offset == 100 && stacked_rtl[1].is_mark);
    assert(stacked_rtl[2].glyph == 1 && stacked_rtl[2].x_advance == 1200 &&
           stacked_rtl[2].x_offset == 0 && stacked_rtl[2].y_offset == 0 && !stacked_rtl[2].is_mark);
    assert(origins_and_pen(stacked_log) == origins_and_pen(stacked_rtl));

    // 1b. Multi-cluster visual RTL (`BEH + KASRA`, `MEEM + SHADDA + FATHA`) -> `[FATHA, SHADDA, MEEM, KASRA, BEH]`.
    const auto multi_log = f.shape_glyphs({0x0628, 0x0650, 0x0645, 0x0651, 0x064E}, true, false);
    const auto multi_rtl = f.shape_glyphs({0x0628, 0x0650, 0x0645, 0x0651, 0x064E}, true, true);
    assert(multi_log.size() == 5 && multi_rtl.size() == 5);
    assert(multi_rtl[0].glyph == 3 && multi_rtl[0].x_offset == 400 && multi_rtl[0].y_offset == 320);
    assert(multi_rtl[1].glyph == 5 && multi_rtl[1].x_offset == 400 && multi_rtl[1].y_offset == 100);
    assert(multi_rtl[2].glyph == 2 && multi_rtl[2].x_advance == 1200 && multi_rtl[2].x_offset == 0);
    assert(multi_rtl[3].glyph == 4 && multi_rtl[3].x_offset == 400 && multi_rtl[3].y_offset == -150);
    assert(multi_rtl[4].glyph == 1 && multi_rtl[4].x_advance == 1200 && multi_rtl[4].x_offset == 0);
    const auto multi_rtl_sig = origins_and_pen(multi_rtl);
    assert((multi_rtl_sig.first == std::vector<Origin>{
        {1, 1200, 0}, {2, 0, 0}, {3, 400, 320}, {4, 1600, -150}, {5, 400, 100}}));
    assert(multi_rtl_sig.second == origins_and_pen(multi_log).second);

    // 1c. RTL CursiveAttachment (`BEH -> TEH`) combined with MarkToBase (`FATHA` on `BEH`):
    //     `BEH` gets `x_offset = -40, y_offset = -120, x_advance = 1160`; `TEH` gets `x_advance = 560, y_offset = 0`.
    //     `FATHA` (`cursive_dx = +400, cursive_dy = +100`) propagates `BEH`'s offset -> `(360, -20)`,
    //     preserving `Origin(FATHA) - Origin(BEH) == (+400, +100)` after visual reversal `[TEH, FATHA, BEH]`.
    const auto curs_log = f.shape_glyphs({0x0628, 0x064E, 0x062A}, true, false);
    const auto curs_rtl = f.shape_glyphs({0x0628, 0x064E, 0x062A}, true, true);
    assert(curs_log.size() == 3 && curs_rtl.size() == 3);
    assert(curs_rtl[0].glyph == 6 && curs_rtl[0].x_advance == 560 && curs_rtl[0].y_offset == 0);
    assert(curs_rtl[1].glyph == 3 && curs_rtl[1].x_advance == 0 &&
           curs_rtl[1].x_offset == 360 && curs_rtl[1].y_offset == -20);
    assert(curs_rtl[2].glyph == 1 && curs_rtl[2].x_advance == 1160 &&
           curs_rtl[2].x_offset == -40 && curs_rtl[2].y_offset == -120);
    const auto curs_rtl_sig = origins_and_pen(curs_rtl);
    assert((curs_rtl_sig.first == std::vector<Origin>{{1, 520, -120}, {3, 920, -20}, {6, 0, 0}}));
    assert(curs_rtl_sig.second == origins_and_pen(curs_log).second);

    // 1d. Unanchored monospaced composite (`U+0622 + U+064E`, gid 7 omitted from BaseCoverage):
    //     In visual RTL, zero-advance `FATHA` precedes `U+0622` so both glyphs share cell origin `x = 0`
    //     rather than stranding `FATHA` at `x = +1200` after `U+0622`'s advance.
    const auto unanchored_rtl = f.shape_glyphs({0x0622, 0x064E}, true, true);
    assert(unanchored_rtl.size() == 2);
    assert(unanchored_rtl[0].glyph == 3 && unanchored_rtl[0].x_advance == 0 && unanchored_rtl[0].x_offset == 0);
    assert(unanchored_rtl[1].glyph == 7 && unanchored_rtl[1].x_advance == 1200 && unanchored_rtl[1].x_offset == 0);
    const auto unanchored_sig = origins_and_pen(unanchored_rtl);
    assert(unanchored_sig.first.size() == 2);
    assert(unanchored_sig.first[0].glyph == 3 && unanchored_sig.first[0].x == 0);
    assert(unanchored_sig.first[1].glyph == 7 && unanchored_sig.first[1].x == 0);
    assert(unanchored_sig.second.first == 1200);

    // 1e. `FontRenderer::shape_text` parity between direct (`pre_reversed = false`) and Bidi-L2
    //     (`pre_reversed = true`) paths, plus `<bdo dir="rtl">` non-RTL cluster preservation.
    FontRenderer& fr = FontRenderer::instance();
    if (fr.register_font_file("tests/wpt/fonts/Ahem.ttf") != FontRenderer::kNoFamily) {
        assert(fr.register_webfont("Ahem", {[&]() { return sfnt_bytes; }}) >= 0);
        const auto direct_shaped = fr.shape_text("\xD8\xA2\xD9\x8E", 100.0f, 400, "Ahem", false, true, true, false);
        const auto bidi_shaped = fr.shape_text("\xD9\x8E\xD8\xA2", 100.0f, 400, "Ahem", false, true, true, true);
        assert(direct_shaped.size() == 2 && bidi_shaped.size() == 2);
        for (std::size_t i = 0; i < 2; ++i) {
            assert(direct_shaped[i].glyph == bidi_shaped[i].glyph);
            assert(std::abs(direct_shaped[i].advance - bidi_shaped[i].advance) < 1e-4f);
            assert(std::abs(direct_shaped[i].x_offset - bidi_shaped[i].x_offset) < 1e-4f);
            assert(std::abs(direct_shaped[i].y_offset - bidi_shaped[i].y_offset) < 1e-4f);
        }
        assert(direct_shaped[0].glyph == 3 && std::abs(direct_shaped[0].advance) < 1e-4f &&
               std::abs(direct_shaped[0].x_offset) < 1e-4f);
        assert(direct_shaped[1].glyph == 7 && std::abs(direct_shaped[1].advance - 120.0f) < 1e-3f);

        // Odd-level Bidi override on Latin `"ab\u0301"` (pre-reversed input `"\u0301ba"`):
        // logical shaping gives `['a'(8), 'b'(9), acute(10, x_off = 150 - 600 = -450)]`, and
        // cluster-preserving reversal for `!fr.is_rtl` emits `['b'(9), acute(10), 'a'(8)]` so
        // `acute` stays over `'b'` (`origin_x = 60 - 45 = 15px`).
        const auto bdo_latn = fr.shape_text("\xCC\x81" "ba", 100.0f, 400, "Ahem", false, true, true, true);
        assert(bdo_latn.size() == 3);
        assert(bdo_latn[0].glyph == 9 && std::abs(bdo_latn[0].advance - 60.0f) < 1e-3f);
        assert(bdo_latn[1].glyph == 10 && std::abs(bdo_latn[1].advance) < 1e-4f &&
               std::abs((bdo_latn[0].advance + bdo_latn[1].x_offset) - 15.0f) < 1e-3f);
        assert(bdo_latn[2].glyph == 8 && std::abs(bdo_latn[2].advance - 60.0f) < 1e-3f);
        fr.clear_webfonts();
    }

    // 2. Nonzero mark advance in RTL (`hebr`): verifies Stage 4b sums `(j, i]` (including `k = i`)
    //    rather than `[j, i)`. Mark gid 2 has `hmtx` `x_advance = 90` and `SinglePos` `y_advance = 25`,
    //    attached to `SHIN` gid 1 (`x_advance = 1000`) with `cursive_dx = 250, cursive_dy = 100`.
    {
        const std::vector<TestGlyphSpec> heb_glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {0x05E9, 1000, true, 40, 0, 960, 650},  // 1: SHIN
            {0x05B8, 90, true, 10, -180, 80, -80},  // 2: QAMATS (nonzero advance)
        };
        SfntByteBuf heb_gdef;
        heb_gdef.u32(0x00010000u);
        heb_gdef.u16(12); heb_gdef.u16(0); heb_gdef.u16(0); heb_gdef.u16(0);
        auto heb_gcd = build_classdef_fmt2({{1, 1, 1}, {2, 2, 3}});
        heb_gdef.data.insert(heb_gdef.data.end(), heb_gcd.begin(), heb_gcd.end());

        SfntByteBuf sp_y;
        sp_y.u16(1); sp_y.u16(8); sp_y.u16(0x0008u); // YAdvance = +25
        sp_y.i16(25);
        auto sp_cov = build_cov_fmt1({2});
        sp_y.data.insert(sp_y.data.end(), sp_cov.begin(), sp_cov.end());

        SfntByteBuf heb_mk;
        heb_mk.u16(1); heb_mk.u16(0); heb_mk.u16(0); heb_mk.u16(1); heb_mk.u16(0); heb_mk.u16(0);
        heb_mk.patch_u16(2, static_cast<uint16_t>(heb_mk.data.size()));
        heb_mk.data.insert(heb_mk.data.end(), sp_cov.begin(), sp_cov.end());
        heb_mk.patch_u16(4, static_cast<uint16_t>(heb_mk.data.size()));
        auto heb_bc = build_cov_fmt1({1});
        heb_mk.data.insert(heb_mk.data.end(), heb_bc.begin(), heb_bc.end());
        heb_mk.patch_u16(8, static_cast<uint16_t>(heb_mk.data.size()));
        heb_mk.u16(1); heb_mk.u16(0); heb_mk.u16(6);
        heb_mk.u16(1); heb_mk.i16(50); heb_mk.i16(-100);
        heb_mk.patch_u16(10, static_cast<uint16_t>(heb_mk.data.size()));
        heb_mk.u16(1); heb_mk.u16(4);
        heb_mk.u16(1); heb_mk.i16(300); heb_mk.i16(0);

        auto heb_gpos = build_otl_table(
            {{"hebr", {{"mark", {0, 1}}}}},
            {
                {"mark", 1, 0x0000, -1, {sp_y.data}},
                {"mark", 4, 0x0000, -1, {heb_mk.data}},
            });
        TrueTypeFont heb_font;
        assert(heb_font.load_memory(build_synthetic_sfnt(
            1000, 800, -200, heb_glyphs, {{"GDEF", heb_gdef.data}, {"GPOS", heb_gpos}})));

        const auto heb_log = heb_font.shape_glyphs({0x05E9, 0x05B8}, true, false);
        const auto heb_rtl = heb_font.shape_glyphs({0x05E9, 0x05B8}, true, true);
        assert(heb_log.size() == 2 && heb_rtl.size() == 2);
        assert(heb_rtl[0].glyph == 2 && heb_rtl[0].x_advance == 90 && heb_rtl[0].y_advance == 25);
        assert(heb_rtl[0].x_offset == 340 && heb_rtl[0].y_offset == 125);
        assert(heb_rtl[1].glyph == 1 && heb_rtl[1].x_advance == 1000 && heb_rtl[1].x_offset == 0);
        const auto heb_log_sig = origins_and_pen(heb_log);
        const auto heb_rtl_sig = origins_and_pen(heb_rtl);
        assert(heb_log_sig.second == heb_rtl_sig.second);
        // `Origin(MARK) - Origin(SHIN)` stays `(250, 100)` across both directions even with nonzero mark advance.
        assert(heb_rtl_sig.first[1].x - heb_rtl_sig.first[0].x ==
               heb_log_sig.first[1].x - heb_log_sig.first[0].x);
        assert(heb_rtl_sig.first[1].y - heb_rtl_sig.first[0].y ==
               heb_log_sig.first[1].y - heb_log_sig.first[0].y);
    }

    // 3. Optional host DejaVu Sans & DejaVu Sans Mono checks when installed.
    const std::string sans_path = fr.face_path("DejaVu Sans");
    if (!sans_path.empty()) {
        TrueTypeFont sans;
        if (sans.load_file(sans_path) && sans.has_gpos()) {
            const uint16_t shin = sans.glyph_index(0x05E9);
            const uint16_t qamats = sans.glyph_index(0x05B8);
            const uint16_t shin_dot = sans.glyph_index(0x05C1);
            if (shin != 0 && qamats != 0 && shin_dot != 0) {
                const auto sq_log = sans.shape_glyphs({0x05E9, 0x05B8}, true, false);
                const auto sq_rtl = sans.shape_glyphs({0x05E9, 0x05B8}, true, true);
                assert(sq_log.size() == 2 && sq_rtl.size() == 2);
                assert(sq_rtl[0].glyph == qamats && sq_rtl[1].glyph == shin);
                assert(origins_and_pen(sq_log) == origins_and_pen(sq_rtl));

                const auto heb_log = sans.shape_glyphs({0x05E9, 0x05B8, 0x05C1}, true, false);
                const auto heb_rtl = sans.shape_glyphs({0x05E9, 0x05B8, 0x05C1}, true, true);
                assert(heb_log.size() == 3 && heb_rtl.size() == 3);
                // Logical: `[shin, shin_dot, qamats]` -> Visual RTL: `[qamats, shin_dot, shin]`,
                // with unanchored zero-advance marks sharing `shin`'s origin `x = 0`.
                assert(heb_rtl[0].glyph == qamats && heb_rtl[1].glyph == shin_dot && heb_rtl[2].glyph == shin);
                for (const auto& orig : origins_and_pen(heb_rtl).first) assert(orig.x == 0);
            }
            const auto beh_log = sans.shape_glyphs({0x0628, 0x0651, 0x064E}, true, false);
            const auto beh_rtl = sans.shape_glyphs({0x0628, 0x0651, 0x064E}, true, true);
            if (!beh_rtl.empty() && beh_rtl[0].glyph != 0) {
                assert(beh_rtl.size() == 2 && beh_rtl[0].is_mark && beh_rtl[0].x_offset > 0);
                assert(origins_and_pen(beh_log) == origins_and_pen(beh_rtl));
            }
            const auto arab_log = sans.shape_glyphs(
                {0x0627, 0x0644, 0x0633, 0x0651, 0x064E, 0x0644, 0x064E, 0x0627, 0x0645, 0x064F},
                true, false);
            const auto arab_rtl = sans.shape_glyphs(
                {0x0627, 0x0644, 0x0633, 0x0651, 0x064E, 0x0644, 0x064E, 0x0627, 0x0645, 0x064F},
                true, true);
            if (!arab_rtl.empty() && arab_rtl[0].glyph != 0) {
                assert(arab_rtl.size() == arab_log.size());
                assert(arab_rtl[0].is_mark && arab_rtl[0].x_offset > 0);
                assert(origins_and_pen(arab_log).second == origins_and_pen(arab_rtl).second);
            }
        }
    }

    const std::string mono_path = fr.face_path("DejaVu Sans Mono");
    if (!mono_path.empty()) {
        TrueTypeFont mono;
        if (mono.load_file(mono_path) && mono.has_gpos()) {
            const uint16_t alef_madda = mono.glyph_index(0x0622);
            const uint16_t fatha = mono.glyph_index(0x064E);
            if (alef_madda != 0 && fatha != 0) {
                const auto pair_rtl = mono.shape_glyphs({0x0627, 0x064E, 0x0653}, true, true);
                assert(pair_rtl.size() == 2);
                assert(pair_rtl[0].glyph == fatha && pair_rtl[0].x_advance == 0 && pair_rtl[0].x_offset == 0);
                assert(pair_rtl[1].glyph == alef_madda && pair_rtl[1].x_advance > 0 && pair_rtl[1].x_offset == 0);
            }
        }
    }
}
