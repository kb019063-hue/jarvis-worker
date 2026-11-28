// Phase 476, finding 476: Hebrew canonical decomposition, unified mark ordering
// across cantillation/varika/non-Hebrew marks, special meteg reordering,
// GPOS-gated presentation-form fallback (`compose_hebrew`), `hebr` GPOS tag and
// late GDEF mark-advance zeroing, and U+034F CGJ / default-ignorable handling.

TEST(hebrew_normalization_cantillation_meteg_fallback_and_cgj) {
    using namespace litebird;

    auto glyph_ids = [](const TrueTypeFont& f, const std::vector<uint32_t>& cps, bool rtl = false) {
        std::vector<uint16_t> out;
        for (const auto& g : f.shape_glyphs(cps, true, rtl)) out.push_back(g.glyph);
        return out;
    };
    auto same_pg = [](const TrueTypeFont::PositionedGlyph& a,
                      const TrueTypeFont::PositionedGlyph& b) {
        return a.glyph == b.glyph &&
               a.x_advance == b.x_advance &&
               a.y_advance == b.y_advance &&
               a.x_offset == b.x_offset &&
               a.y_offset == b.y_offset &&
               a.is_mark == b.is_mark;
    };
    auto same_run = [&](const std::vector<TrueTypeFont::PositionedGlyph>& a,
                        const std::vector<TrueTypeFont::PositionedGlyph>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!same_pg(a[i], b[i])) return false;
        }
        return true;
    };

    // -------------------------------------------------------------------------
    // 1 & 2. Unified Hebrew mark ordering (cantillation, varika, non-Hebrew
    //        combining marks) and `reorder_marks_hebrew` meteg/below swap.
    // -------------------------------------------------------------------------
    {
        const std::vector<TestGlyphSpec> glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {' ', 250, false, 0, 0, 0, 0},                // 1: space
            {0x05E9, 1000, true, 60, 0, 940, 700},        // 2: SHIN
            {0x05DC, 900, true, 60, 0, 840, 750},         // 3: LAMED
            {0x05D1, 900, true, 60, 0, 840, 700},         // 4: BET
            {0x05B0, 0, true, 100, -180, 220, -60},       // 5: SHEVA (mod 22)
            {0x05B4, 0, true, 100, -180, 220, -60},       // 6: HIRIQ (mod 23)
            {0x05B5, 0, true, 100, -180, 220, -60},       // 7: TSERE (mod 18)
            {0x05B7, 0, true, 100, -180, 220, -60},       // 8: PATAH (mod 20)
            {0x05B8, 0, true, 100, -180, 220, -60},       // 9: QAMATS (mod 21)
            {0x05BB, 0, true, 100, -180, 220, -60},       // 10: QUBUTS (mod 24)
            {0x05BC, 0, true, 240, 260, 360, 380},        // 11: DAGESH (mod 12)
            {0x05BD, 0, true, 80, -220, 140, -60},        // 12: METEG (mod 25)
            {0x05C1, 0, true, 680, 720, 800, 840},        // 13: SHIN DOT (mod 10)
            {0x05C7, 0, true, 100, -180, 220, -60},       // 14: QAMATS QATAN (mod 21)
            {0x0596, 0, true, 180, -240, 320, -80},       // 15: TIPEHA (CCC 220)
            {0x059C, 0, true, 180, 760, 320, 900},        // 16: GERESH (CCC 230)
            {0xFB1E, 0, true, 180, 760, 320, 880},        // 17: VARIKA (CCC 26)
            {0x0323, 0, true, 180, -200, 280, -100},      // 18: COMBINING DOT BELOW (CCC 220)
            {0x0301, 0, true, 180, 760, 300, 900},        // 19: COMBINING ACUTE ACCENT (CCC 230)
        };
        TrueTypeFont f;
        assert(f.load_memory(build_synthetic_sfnt(1000, 800, -200, glyphs, {})));

        // Cantillation (220/230) and non-Hebrew marks (220/230) sort in one span with points (10..26).
        assert((glyph_ids(f, {0x05E9, 0x0596, 0x05B8, 0x05C1, 0x05BC}, false) ==
                std::vector<uint16_t>{2, 13, 11, 9, 15}));
        assert((glyph_ids(f, {0x05E9, 0x0596, 0x05B8, 0x05C1, 0x05BC}, true) ==
                std::vector<uint16_t>{15, 9, 11, 13, 2}));
        assert((glyph_ids(f, {0x05E9, 0x0301, 0x05B8, 0x0323, 0x05C1, 0x05BC}, false) ==
                std::vector<uint16_t>{2, 13, 11, 9, 18, 19}));
        assert((glyph_ids(f, {0x05D1, 0xFB1E, 0x059C, 0x05B8, 0x05BD}, false) ==
                std::vector<uint16_t>{4, 9, 12, 17, 16}));

        // Special meteg / CCC_BELOW swap: [patah/qamats, sheva/hiriq, meteg/below]
        // swaps the last two marks across all 6 input permutations.
        const std::vector<uint32_t> perms[6] = {
            {0x05DC, 0x05B7, 0x05B4, 0x05BD},
            {0x05DC, 0x05B7, 0x05BD, 0x05B4},
            {0x05DC, 0x05B4, 0x05B7, 0x05BD},
            {0x05DC, 0x05B4, 0x05BD, 0x05B7},
            {0x05DC, 0x05BD, 0x05B7, 0x05B4},
            {0x05DC, 0x05BD, 0x05B4, 0x05B7},
        };
        for (const auto& p : perms) {
            assert((glyph_ids(f, p, false) == std::vector<uint16_t>{3, 8, 12, 6}));
            assert((glyph_ids(f, p, true) == std::vector<uint16_t>{6, 12, 8, 3}));
        }
        // Qamats / qamats-qatan + sheva + meteg / CCC_BELOW (0596, 0323) also swap.
        assert((glyph_ids(f, {0x05DC, 0x05B8, 0x05B0, 0x05BD}, false) ==
                std::vector<uint16_t>{3, 9, 12, 5}));
        assert((glyph_ids(f, {0x05DC, 0x05C7, 0x05B0, 0x05BD}, false) ==
                std::vector<uint16_t>{3, 14, 12, 5}));
        assert((glyph_ids(f, {0x05DC, 0x05B7, 0x05B0, 0x0596}, false) ==
                std::vector<uint16_t>{3, 8, 15, 5}));
        assert((glyph_ids(f, {0x05DC, 0x05B8, 0x05B4, 0x0323}, false) ==
                std::vector<uint16_t>{3, 9, 18, 6}));
        // Non-matching vowel triples keep ordinary modified-CCC order.
        assert((glyph_ids(f, {0x05DC, 0x05B5, 0x05B4, 0x05BD}, false) ==
                std::vector<uint16_t>{3, 7, 6, 12}));
        assert((glyph_ids(f, {0x05DC, 0x05B7, 0x05BB, 0x05BD}, false) ==
                std::vector<uint16_t>{3, 8, 10, 12}));
    }

    // -------------------------------------------------------------------------
    // 3 & 4. Presentation-form decomposition vs `hebr` GPOS `mark` gating,
    //        `compose_hebrew` fallback, `gpos_tag = 'hebr'` (ignoring DFLT-only
    //        GPOS), and `ZERO_WIDTH_MARKS_BY_GDEF_LATE` on monospaced `hebr`.
    // -------------------------------------------------------------------------
    {
        const std::vector<TestGlyphSpec> glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {' ', 250, false, 0, 0, 0, 0},                // 1: space
            {0x05D0, 1200, true, 80, 0, 1120, 700},       // 2: ALEF
            {0x05D1, 1200, true, 80, 0, 1120, 700},       // 3: BET
            {0x05D5, 1200, true, 80, 0, 1120, 700},       // 4: VAV
            {0x05D9, 1200, true, 80, 0, 1120, 700},       // 5: YOD
            {0x05DB, 1200, true, 80, 0, 1120, 700},       // 6: KAF
            {0x05E4, 1200, true, 80, 0, 1120, 700},       // 7: PE
            {0x05E9, 1200, true, 80, 0, 1120, 700},       // 8: SHIN
            {0x05F2, 1200, true, 80, 0, 1120, 700},       // 9: YIDDISH DOUBLE YOD
            // Monospaced non-zero `hmtx` advances on marks (1200 units) to test GDEF late zeroing!
            {0x05B4, 1200, true, 120, -180, 240, -60},    // 10: HIRIQ
            {0x05B7, 1200, true, 120, -180, 240, -60},    // 11: PATAH
            {0x05B8, 1200, true, 120, -180, 240, -60},    // 12: QAMATS
            {0x05B9, 1200, true, 120, 720, 240, 840},     // 13: HOLAM
            {0x05BC, 1200, true, 300, 280, 420, 400},     // 14: DAGESH
            {0x05BF, 1200, true, 240, 760, 480, 840},     // 15: RAFE
            {0x05C1, 1200, true, 760, 720, 880, 840},     // 16: SHIN DOT
            {0x05C2, 1200, true, 160, 720, 280, 840},     // 17: SIN DOT
            // Presentation forms:
            {0xFB1D, 1200, true, 80, -180, 1120, 700},    // 18: YOD WITH HIRIQ
            {0xFB1F, 1200, true, 80, -180, 1120, 700},    // 19: YIDDISH YOD YOD PATAH
            {0xFB2A, 1200, true, 80, 0, 1120, 840},       // 20: SHIN WITH SHIN DOT
            {0xFB2B, 1200, true, 80, 0, 1120, 840},       // 21: SHIN WITH SIN DOT
            {0xFB2C, 1200, true, 80, 0, 1120, 840},       // 22: SHIN WITH DAGESH AND SHIN DOT
            {0xFB2D, 1200, true, 80, 0, 1120, 840},       // 23: SHIN WITH DAGESH AND SIN DOT
            {0xFB2E, 1200, true, 80, -180, 1120, 700},    // 24: ALEF WITH PATAH
            {0xFB2F, 1200, true, 80, -180, 1120, 700},    // 25: ALEF WITH QAMATS
            {0xFB30, 1200, true, 80, 0, 1120, 700},       // 26: ALEF WITH DAGESH
            {0xFB49, 1200, true, 80, 0, 1120, 700},       // 27: SHIN WITH DAGESH
            {0xFB4B, 1200, true, 80, 0, 1120, 840},       // 28: VAV WITH HOLAM
            {0xFB4C, 1200, true, 80, 0, 1120, 840},       // 29: BET WITH RAFE
            {0xFB4D, 1200, true, 80, 0, 1120, 840},       // 30: KAF WITH RAFE
            {0xFB4E, 1200, true, 80, 0, 1120, 840},       // 31: PE WITH RAFE
            {0xFB4F, 1200, true, 80, 0, 1120, 750},       // 32: ALEF LAMED (<compat>)
            {0x05DC, 1200, true, 80, 0, 1120, 750},       // 33: LAMED
        };

        SfntByteBuf gdef;
        gdef.u32(0x00010000u);
        gdef.u16(12); gdef.u16(0); gdef.u16(0); gdef.u16(0);
        auto gcd = build_classdef_fmt2({{2, 9, 1}, {10, 17, 3}, {18, 33, 1}});
        gdef.data.insert(gdef.data.end(), gcd.begin(), gcd.end());

        auto make_mark_gpos = [](uint32_t script_tag) {
            auto put_anchor = [](SfntByteBuf& b, std::size_t base_pos, std::size_t off_pos, int16_t x, int16_t y) {
                b.patch_u16(off_pos, static_cast<uint16_t>(b.data.size() - base_pos));
                b.u16(1); b.i16(x); b.i16(y);
            };
            // MarkToBase on ALEF(2) and SHIN(8) for QAMATS(12), DAGESH(14), SHIN DOT(16).
            SfntByteBuf mb;
            mb.u16(1); mb.u16(0); mb.u16(0); mb.u16(1); mb.u16(0); mb.u16(0);
            mb.patch_u16(2, static_cast<uint16_t>(mb.data.size()));
            auto mcov = build_cov_fmt1({12, 14, 16});
            mb.data.insert(mb.data.end(), mcov.begin(), mcov.end());
            mb.patch_u16(4, static_cast<uint16_t>(mb.data.size()));
            auto bcov = build_cov_fmt1({2, 8});
            mb.data.insert(mb.data.end(), bcov.begin(), bcov.end());

            const std::size_t ma_pos = mb.data.size();
            mb.patch_u16(8, static_cast<uint16_t>(ma_pos));
            mb.u16(3);
            mb.u16(0); mb.u16(0);
            mb.u16(0); mb.u16(0);
            mb.u16(0); mb.u16(0);
            put_anchor(mb, ma_pos, ma_pos + 2 + 0 * 4 + 2, 180, -120);
            put_anchor(mb, ma_pos, ma_pos + 2 + 1 * 4 + 2, 360, 340);
            put_anchor(mb, ma_pos, ma_pos + 2 + 2 * 4 + 2, 820, 780);

            const std::size_t ba_pos = mb.data.size();
            mb.patch_u16(10, static_cast<uint16_t>(ba_pos));
            mb.u16(2);
            mb.u16(0);
            mb.u16(0);
            put_anchor(mb, ba_pos, ba_pos + 2 + 0 * 2, 600, 300);
            put_anchor(mb, ba_pos, ba_pos + 2 + 1 * 2, 620, 320);

            SfntByteBuf gpos;
            gpos.u32(0x00010000u);
            gpos.u16(10); gpos.u16(0); gpos.u16(0);
            gpos.u16(1);
            gpos.u32(script_tag); gpos.u16(8);
            gpos.u16(4); gpos.u16(0);
            gpos.u16(0); gpos.u16(0xFFFFu); gpos.u16(1); gpos.u16(0);

            const std::size_t flist = gpos.data.size();
            gpos.patch_u16(6, static_cast<uint16_t>(flist));
            gpos.u16(1);
            gpos.u32(0x6D61726Bu); // 'mark'
            gpos.u16(8);
            gpos.u16(0); gpos.u16(1); gpos.u16(0);

            const std::size_t llist = gpos.data.size();
            gpos.patch_u16(8, static_cast<uint16_t>(llist));
            gpos.u16(1);
            gpos.u16(4);
            gpos.u16(4); gpos.u16(0); gpos.u16(1); gpos.u16(8);
            gpos.data.insert(gpos.data.end(), mb.data.begin(), mb.data.end());
            return gpos.data;
        };

        // 3a. Font WITH `hebr` GPOS `mark`: standalone presentation forms stay
        //     precomposed, while mark-followed canonical presentation forms
        //     decompose and stay decomposed so MarkToBase anchors every mark,
        //     and GDEF class-3 mark advances are zeroed!
        TrueTypeFont f_mark;
        assert(f_mark.load_memory(build_synthetic_sfnt(
            1000, 800, -200, glyphs,
            {{"GDEF", gdef.data}, {"GPOS", make_mark_gpos(0x68656272u)}}))); // 'hebr'

        assert((glyph_ids(f_mark, {0xFB2A}, true) == std::vector<uint16_t>{20}));
        assert((glyph_ids(f_mark, {0xFB2C}, true) == std::vector<uint16_t>{22}));
        assert((glyph_ids(f_mark, {0xFB30}, true) == std::vector<uint16_t>{26}));
        assert((glyph_ids(f_mark, {0xFB4B}, true) == std::vector<uint16_t>{28}));
        assert((glyph_ids(f_mark, {0xFB4F}, true) == std::vector<uint16_t>{32}));
        // Compatibility ligature U+FB4F ALEF LAMED never decomposes before a mark.
        assert((glyph_ids(f_mark, {0xFB4F, 0x05B8}, true) == std::vector<uint16_t>{12, 32}));

        const auto fb30_qam = f_mark.shape_glyphs({0xFB30, 0x05B8}, true, true);
        const auto alef_dag_qam = f_mark.shape_glyphs({0x05D0, 0x05BC, 0x05B8}, true, true);
        assert(fb30_qam.size() == 3 && same_run(fb30_qam, alef_dag_qam));
        assert(fb30_qam[0].glyph == 12 && fb30_qam[1].glyph == 14 && fb30_qam[2].glyph == 2);
        // Monospaced 1200-unit mark advances zeroed by ZERO_WIDTH_MARKS_BY_GDEF_LATE;
        // both marks anchored onto ALEF(2) in RTL!
        assert(fb30_qam[0].x_advance == 0 && fb30_qam[1].x_advance == 0 && fb30_qam[2].x_advance == 1200);
        assert(fb30_qam[0].x_offset == (600 - 180) && fb30_qam[0].y_offset == (300 - (-120)));
        assert(fb30_qam[1].x_offset == (600 - 360) && fb30_qam[1].y_offset == (300 - 340));

        const auto fb2c_qam = f_mark.shape_glyphs({0xFB2C, 0x05B8}, true, true);
        const auto shin_all = f_mark.shape_glyphs({0x05E9, 0x05B8, 0x05BC, 0x05C1}, true, true);
        assert(fb2c_qam.size() == 4 && same_run(fb2c_qam, shin_all));
        assert((glyph_ids(f_mark, {0xFB2C, 0x05B8}, true) == std::vector<uint16_t>{12, 14, 16, 8}));

        // 4a. Font WITHOUT `hebr` GPOS (even when `DFLT` GPOS `mark` exists!):
        //     `gpos_tag = 'hebr'` ignores `DFLT` GPOS and `compose_hebrew`
        //     synthesizes presentation forms!
        TrueTypeFont f_dflt_only;
        assert(f_dflt_only.load_memory(build_synthetic_sfnt(
            1000, 800, -200, glyphs,
            {{"GDEF", gdef.data}, {"GPOS", make_mark_gpos(0x44464C54u)}}))); // 'DFLT'

        assert((glyph_ids(f_dflt_only, {0x05D9, 0x05B4}, true) == std::vector<uint16_t>{18})); // FB1D
        assert((glyph_ids(f_dflt_only, {0x05F2, 0x05B7}, true) == std::vector<uint16_t>{19})); // FB1F
        assert((glyph_ids(f_dflt_only, {0x05D0, 0x05B7}, true) == std::vector<uint16_t>{24})); // FB2E
        assert((glyph_ids(f_dflt_only, {0x05D0, 0x05B8}, true) == std::vector<uint16_t>{25})); // FB2F
        assert((glyph_ids(f_dflt_only, {0x05D5, 0x05B9}, true) == std::vector<uint16_t>{28})); // FB4B
        assert((glyph_ids(f_dflt_only, {0x05D0, 0x05BC}, true) == std::vector<uint16_t>{26})); // FB30
        assert((glyph_ids(f_dflt_only, {0x05D1, 0x05BF}, true) == std::vector<uint16_t>{29})); // FB4C
        assert((glyph_ids(f_dflt_only, {0x05DB, 0x05BF}, true) == std::vector<uint16_t>{30})); // FB4D
        assert((glyph_ids(f_dflt_only, {0x05E4, 0x05BF}, true) == std::vector<uint16_t>{31})); // FB4E
        assert((glyph_ids(f_dflt_only, {0x05E9, 0x05B8, 0x05BC, 0x05C1}, true) ==
                std::vector<uint16_t>{12, 22})); // RTL: [QAMATS, FB2C]
        assert((glyph_ids(f_dflt_only, {0x05E9, 0x05BC, 0x05C2}, true) ==
                std::vector<uint16_t>{23})); // FB2D
        // DFLT GPOS did NOT run on Hebrew (`x_offset == 0`), while GDEF late zeroing zeroed QAMATS!
        const auto fallback_shin = f_dflt_only.shape_glyphs({0x05E9, 0x05B8, 0x05BC, 0x05C1}, true, true);
        assert(fallback_shin.size() == 2 && fallback_shin[0].glyph == 12 && fallback_shin[1].glyph == 22);
        assert(fallback_shin[0].x_advance == 0 && fallback_shin[0].x_offset == 0 && fallback_shin[0].y_offset == 0);

        // 4b. Font missing `FB2A` (`0xFB2A` unmapped) while `FB49` (`SHIN WITH DAGESH`)
        //     is covered, plus missing `FB2C` decomposing standalone to `[SHIN, SHIN_DOT, DAGESH]`:
        auto glyphs_partial = glyphs;
        glyphs_partial[20].codepoint = 0; // unmap FB2A
        glyphs_partial[22].codepoint = 0; // unmap FB2C
        TrueTypeFont f_partial;
        assert(f_partial.load_memory(build_synthetic_sfnt(
            1000, 800, -200, glyphs_partial, {{"GDEF", gdef.data}})));
        // Standalone missing FB2C decomposes canonically to [05E9, 05C1, 05BC],
        // then compose_hebrew skips missing FB2A (class 10) and composes 05E9+05BC -> FB49 (27)!
        assert((glyph_ids(f_partial, {0xFB2C}, false) == std::vector<uint16_t>{27, 16}));
    }

    // -------------------------------------------------------------------------
    // 5. U+034F CGJ and default-ignorable barriers & GPOS skipping across
    //    Hebrew, Latin, and Arabic.
    // -------------------------------------------------------------------------
    {
        const std::vector<TestGlyphSpec> glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {' ', 250, false, 0, 0, 0, 0},                // 1: space (invisible slot)
            {0x034F, 400, true, 10, 10, 390, 390},        // 2: CGJ (must hide to space + 0 adv!)
            {'a', 600, true, 40, 0, 560, 500},            // 3: 'a'
            {0x00E1, 600, true, 40, 0, 560, 680},         // 4: 'á'
            {'e', 600, true, 40, 0, 560, 500},            // 5: 'e'
            {0x00E9, 600, true, 40, 0, 560, 680},         // 6: 'é'
            {'f', 500, true, 40, 0, 460, 700},            // 7: 'f'
            {'i', 350, true, 40, 0, 310, 700},            // 8: 'i'
            {0xFB01, 800, true, 40, 0, 760, 700},         // 9: 'fi' ligature
            {0x0301, 0, true, 100, 540, 220, 680},        // 10: COMBINING ACUTE (CCC 230)
            {0x0323, 0, true, 100, -180, 220, -60},       // 11: COMBINING DOT BELOW (CCC 220)
            {0x05DC, 900, true, 60, 0, 840, 750},         // 12: LAMED
            {0x05B7, 0, true, 120, -180, 240, -60},       // 13: PATAH (mod 20)
            {0x05B4, 0, true, 120, -180, 240, -60},       // 14: HIRIQ (mod 23)
            {0x200C, 300, false, 0, 0, 0, 0},             // 15: ZWNJ
        };

        SfntByteBuf gdef;
        gdef.u32(0x00010000u);
        gdef.u16(12); gdef.u16(0); gdef.u16(0); gdef.u16(0);
        auto gcd = build_classdef_fmt2({{3, 9, 1}, {10, 11, 3}, {12, 12, 1}, {13, 14, 3}});
        gdef.data.insert(gdef.data.end(), gcd.begin(), gcd.end());

        // GSUB `latn`/`hebr`: 'f'(7) + 'i'(8) -> 'fi'(9), and HIRIQ(14) + PATAH(13) -> 'fi'(9 sentinel)
        // to verify hidden CGJ blocks GSUB when CGJ prevented mark reordering, while unhidden CGJ
        // between 'f' and 'i' allows the 'fi' ligature!
        SfntByteBuf lig_sub;
        lig_sub.u16(1); lig_sub.u16(0); lig_sub.u16(3); lig_sub.u16(0); lig_sub.u16(0); lig_sub.u16(0);
        lig_sub.patch_u16(2, static_cast<uint16_t>(lig_sub.data.size()));
        auto lcov = build_cov_fmt1({7, 13, 14});
        lig_sub.data.insert(lig_sub.data.end(), lcov.begin(), lcov.end());
        lig_sub.patch_u16(6, static_cast<uint16_t>(lig_sub.data.size()));
        lig_sub.u16(1); lig_sub.u16(4);
        lig_sub.u16(9); lig_sub.u16(2); lig_sub.u16(8); // 7 + 8 -> 9
        lig_sub.patch_u16(8, static_cast<uint16_t>(lig_sub.data.size()));
        lig_sub.u16(1); lig_sub.u16(4);
        lig_sub.u16(9); lig_sub.u16(2); lig_sub.u16(14); // 13(patah) + 14(hiriq) -> 9
        lig_sub.patch_u16(10, static_cast<uint16_t>(lig_sub.data.size()));
        lig_sub.u16(1); lig_sub.u16(4);
        lig_sub.u16(9); lig_sub.u16(2); lig_sub.u16(13); // 14(hiriq) + 13(patah) -> 9

        SfntByteBuf gsub;
        gsub.u32(0x00010000u);
        gsub.u16(10); gsub.u16(0); gsub.u16(0);
        gsub.u16(2);
        gsub.u32(0x44464C54u); gsub.u16(14); // 'DFLT'
        gsub.u32(0x68656272u); gsub.u16(14); // 'hebr'
        gsub.u16(4); gsub.u16(0);
        gsub.u16(0); gsub.u16(0xFFFFu); gsub.u16(1); gsub.u16(0);
        const std::size_t g_flist = gsub.data.size();
        gsub.patch_u16(6, static_cast<uint16_t>(g_flist));
        gsub.u16(1);
        gsub.u32(0x6C696761u); gsub.u16(8); // 'liga'
        gsub.u16(0); gsub.u16(1); gsub.u16(0);
        const std::size_t g_llist = gsub.data.size();
        gsub.patch_u16(8, static_cast<uint16_t>(g_llist));
        gsub.u16(1);
        gsub.u16(4);
        gsub.u16(4); gsub.u16(0); gsub.u16(1); gsub.u16(8);
        gsub.data.insert(gsub.data.end(), lig_sub.data.begin(), lig_sub.data.end());

        // GPOS (`DFLT` + `hebr`) MarkToBase: ACUTE(10) on 'a'(3), PATAH(13) & HIRIQ(14) on LAMED(12).
        auto put_anchor = [](SfntByteBuf& b, std::size_t base_pos, std::size_t off_pos, int16_t x, int16_t y) {
            b.patch_u16(off_pos, static_cast<uint16_t>(b.data.size() - base_pos));
            b.u16(1); b.i16(x); b.i16(y);
        };
        SfntByteBuf mb;
        mb.u16(1); mb.u16(0); mb.u16(0); mb.u16(1); mb.u16(0); mb.u16(0);
        mb.patch_u16(2, static_cast<uint16_t>(mb.data.size()));
        auto mcov = build_cov_fmt1({10, 13, 14});
        mb.data.insert(mb.data.end(), mcov.begin(), mcov.end());
        mb.patch_u16(4, static_cast<uint16_t>(mb.data.size()));
        auto bcov = build_cov_fmt1({3, 12});
        mb.data.insert(mb.data.end(), bcov.begin(), bcov.end());
        const std::size_t ma_pos = mb.data.size();
        mb.patch_u16(8, static_cast<uint16_t>(ma_pos));
        mb.u16(3);
        mb.u16(0); mb.u16(0);
        mb.u16(0); mb.u16(0);
        mb.u16(0); mb.u16(0);
        put_anchor(mb, ma_pos, ma_pos + 2 + 0 * 4 + 2, 160, 560);
        put_anchor(mb, ma_pos, ma_pos + 2 + 1 * 4 + 2, 180, -120);
        put_anchor(mb, ma_pos, ma_pos + 2 + 2 * 4 + 2, 180, -120);
        const std::size_t ba_pos = mb.data.size();
        mb.patch_u16(10, static_cast<uint16_t>(ba_pos));
        mb.u16(2);
        mb.u16(0);
        mb.u16(0);
        put_anchor(mb, ba_pos, ba_pos + 2 + 0 * 2, 300, 520);
        put_anchor(mb, ba_pos, ba_pos + 2 + 1 * 2, 450, -120);

        SfntByteBuf gpos;
        gpos.u32(0x00010000u);
        gpos.u16(10); gpos.u16(0); gpos.u16(0);
        gpos.u16(2);
        gpos.u32(0x44464C54u); gpos.u16(14); // 'DFLT'
        gpos.u32(0x68656272u); gpos.u16(14); // 'hebr'
        gpos.u16(4); gpos.u16(0);
        gpos.u16(0); gpos.u16(0xFFFFu); gpos.u16(1); gpos.u16(0);
        const std::size_t p_flist = gpos.data.size();
        gpos.patch_u16(6, static_cast<uint16_t>(p_flist));
        gpos.u16(1);
        gpos.u32(0x6D61726Bu); gpos.u16(8); // 'mark'
        gpos.u16(0); gpos.u16(1); gpos.u16(0);
        const std::size_t p_llist = gpos.data.size();
        gpos.patch_u16(8, static_cast<uint16_t>(p_llist));
        gpos.u16(1);
        gpos.u16(4);
        gpos.u16(4); gpos.u16(0); gpos.u16(1); gpos.u16(8);
        gpos.data.insert(gpos.data.end(), mb.data.begin(), mb.data.end());

        TrueTypeFont f_cgj;
        assert(f_cgj.load_memory(build_synthetic_sfnt(
            1000, 800, -200, glyphs,
            {{"GDEF", gdef.data}, {"GSUB", gsub.data}, {"GPOS", gpos.data}})));

        // 'a' + CGJ + ACUTE: blocks 'á' composition, hides CGJ to [space(1), adv=0],
        // and skips CGJ in GPOS so ACUTE(10) still anchors onto 'a'(3)!
        const auto a_cgj_ac = f_cgj.shape_glyphs({'a', 0x034F, 0x0301}, true, false);
        assert(a_cgj_ac.size() == 3);
        assert(a_cgj_ac[0].glyph == 3 && a_cgj_ac[0].x_advance == 600);
        assert(a_cgj_ac[1].glyph == 1 && a_cgj_ac[1].x_advance == 0 && a_cgj_ac[1].x_offset == 0);
        assert(a_cgj_ac[2].glyph == 10 && a_cgj_ac[2].x_advance == 0);
        assert(a_cgj_ac[2].x_offset == (300 - 160 - 600) && a_cgj_ac[2].y_offset == (520 - 560));

        // '\u00e9' + CGJ + DOT_BELOW: decomposes '\u00e9' -> 'e' + ACUTE, CGJ prevents
        // DOT_BELOW(220) from crossing ACUTE(230), then 'e' + ACUTE recomposes to '\u00e9'(6)!
        assert((glyph_ids(f_cgj, {0x00E9, 0x034F, 0x0323}, false) ==
                std::vector<uint16_t>{6, 1, 11}));

        // 'f' + CGJ + 'i': CGJ did not prevent mark reordering -> unhidden in GSUB -> ligates to 'fi'(9) + space(1),
        // whereas 'f' + ZWNJ + 'i' blocks the ligature!
        const auto f_cgj_i = f_cgj.shape_glyphs({'f', 0x034F, 'i'}, true, false);
        assert(f_cgj_i.size() == 2 && f_cgj_i[0].glyph == 9 && f_cgj_i[1].glyph == 1 && f_cgj_i[1].x_advance == 0);
        const auto f_zwnj_i = f_cgj.shape_glyphs({'f', 0x200C, 'i'}, true, false);
        assert(f_zwnj_i.size() == 3 && f_zwnj_i[0].glyph == 7 && f_zwnj_i[1].glyph == 1 && f_zwnj_i[2].glyph == 8);

        // Hebrew Yerushalayim (`LAMED + HIRIQ(23) + CGJ + PATAH(20)` vs `LAMED + PATAH(20) + CGJ + HIRIQ(23)`):
        // In `HIRIQ + CGJ + PATAH`, CGJ prevents `PATAH(20)` from reordering before `HIRIQ(23)` AND stays
        // hidden in GSUB (blocking `14 + 13 -> 9`), while MarkToBase still anchors both marks onto `LAMED(12)`.
        // In `PATAH + CGJ + HIRIQ`, CGJ did not prevent reordering (`20 <= 23`), so it unhides in GSUB (`13 + 14 -> 9`)!
        const auto hiriq_cgj_patah = f_cgj.shape_glyphs({0x05DC, 0x05B4, 0x034F, 0x05B7}, true, true);
        assert(hiriq_cgj_patah.size() == 4);
        assert(hiriq_cgj_patah[0].glyph == 13 && hiriq_cgj_patah[1].glyph == 1 &&
               hiriq_cgj_patah[2].glyph == 14 && hiriq_cgj_patah[3].glyph == 12);
        assert(hiriq_cgj_patah[0].x_offset == (450 - 180));
        assert(hiriq_cgj_patah[1].x_advance == 0 && hiriq_cgj_patah[1].x_offset == 0);
        assert(hiriq_cgj_patah[2].x_offset == (450 - 180));
        const auto patah_cgj_hiriq = f_cgj.shape_glyphs({0x05DC, 0x05B7, 0x034F, 0x05B4}, true, true);
        assert(patah_cgj_hiriq.size() == 3 &&
               patah_cgj_hiriq[0].glyph == 1 && patah_cgj_hiriq[1].glyph == 9 && patah_cgj_hiriq[2].glyph == 12);
    }

    // -------------------------------------------------------------------------
    // 6. Host DejaVuSans.ttf exact HarfBuzz 12.1.0 differential check + HTML/Canvas2D paint.
    // -------------------------------------------------------------------------
    FontRenderer& fr = FontRenderer::instance();
    const std::string dejavu_path = fr.face_path("DejaVu Sans");
    if (!dejavu_path.empty()) {
        TrueTypeFont dejavu;
        if (dejavu.load_file(dejavu_path) && dejavu.glyph_index(0x05E9) != 0 &&
            dejavu.glyph_index(0xFB2C) != 0) {
            // Standalone vs mark-followed presentation forms agree identically with decomposed input.
            assert(dejavu.shape_glyphs({0xFB2A}, true, true).size() == 1);
            assert(dejavu.shape_glyphs({0xFB2C}, true, true).size() == 1);
            assert(dejavu.shape_glyphs({0xFB30}, true, true).size() == 1);
            assert(same_run(dejavu.shape_glyphs({0xFB2A, 0x05B8}, true, true),
                            dejavu.shape_glyphs({0x05E9, 0x05C1, 0x05B8}, true, true)));
            assert(same_run(dejavu.shape_glyphs({0xFB2C, 0x05B8}, true, true),
                            dejavu.shape_glyphs({0x05E9, 0x05B8, 0x05BC, 0x05C1}, true, true)));
            assert(same_run(dejavu.shape_glyphs({0xFB30, 0x05B8}, true, true),
                            dejavu.shape_glyphs({0x05D0, 0x05BC, 0x05B8}, true, true)));
            // Meteg swap permutations agree identically on DejaVuSans.
            assert(same_run(dejavu.shape_glyphs({0x05DC, 0x05BD, 0x05B4, 0x05B7}, true, true),
                            dejavu.shape_glyphs({0x05DC, 0x05B7, 0x05B4, 0x05BD}, true, true)));
            const auto meteg_rtl = dejavu.shape_glyphs({0x05DC, 0x05B7, 0x05B4, 0x05BD}, true, true);
            assert(meteg_rtl.size() == 4 &&
                   meteg_rtl[0].glyph == dejavu.glyph_index(0x05B4) &&
                   meteg_rtl[1].glyph == dejavu.glyph_index(0x05BD) &&
                   meteg_rtl[2].glyph == dejavu.glyph_index(0x05B7) &&
                   meteg_rtl[3].glyph == dejavu.glyph_index(0x05DC));
            // Yerushalayim CGJ keeps mark attachment on LAMED while hiding CGJ to space(0 adv).
            const auto yeru_cgj = dejavu.shape_glyphs({0x05DC, 0x05B7, 0x034F, 0x05B4}, true, true);
            const auto yeru_plain = dejavu.shape_glyphs({0x05DC, 0x05B7, 0x05B4}, true, true);
            assert(yeru_cgj.size() == 4 && yeru_plain.size() == 3);
            assert(same_pg(yeru_cgj[0], yeru_plain[0]) &&
                   yeru_cgj[1].glyph == dejavu.glyph_index(' ') && yeru_cgj[1].x_advance == 0 &&
                   same_pg(yeru_cgj[2], yeru_plain[1]) &&
                   same_pg(yeru_cgj[3], yeru_plain[2]));

            // End-to-end HTML/CSS/Bidi/BitmapRenderer pixel verification:
            // (a) `&#xFB2C;&#x05B8;` paints identical pixels to `&#x05E9;&#x05B8;&#x05BC;&#x05C1;`.
            // (b) `&#x05DC;&#x05B7;&#x034F;&#x05B4;` paints identical pixels to `&#x05DC;&#x05B7;&#x05B4;`
            //     (no `.notdef` tofu or horizontal drift from `U+034F` CGJ).
            auto render_rtl = [](const char* inner) -> BitmapRenderer {
                const std::string html = std::string(
                    "<!doctype html><html><body style=\"margin:0;background:#ffffff\">"
                    "<div dir=\"rtl\" style=\"font:36px 'DejaVu Sans';color:#000000;padding:8px\">") +
                    inner + "</div></body></html>";
                auto doc = HtmlParser::parse(html);
                StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 160.0f, 80.0f);
                auto root = LayoutTree::build(doc, resolver, 160.0f, 80.0f);
                assert(root);
                const DisplayList list = DisplayList::build(root);
                BitmapRenderer bmp(160, 80);
                bmp.clear(Color::white());
                bmp.render(list);
                return bmp;
            };
            auto same_pixels_with_ink = [](const BitmapRenderer& a, const BitmapRenderer& b) {
                int ink = 0;
                for (int y = 0; y < 80; ++y) {
                    for (int x = 0; x < 160; ++x) {
                        const Color pa = a.get_pixel(x, y);
                        if (!(pa == b.get_pixel(x, y))) return false;
                        if (!(pa == Color::white())) ++ink;
                    }
                }
                return ink > 20;
            };
            assert(same_pixels_with_ink(render_rtl("&#xFB2C;&#x05B8;"),
                                        render_rtl("&#x05E9;&#x05B8;&#x05BC;&#x05C1;")));
            assert(same_pixels_with_ink(render_rtl("&#x05DC;&#x05B7;&#x034F;&#x05B4;"),
                                        render_rtl("&#x05DC;&#x05B7;&#x05B4;")));
        }
    }
}
