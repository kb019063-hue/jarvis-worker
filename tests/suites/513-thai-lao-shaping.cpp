// Phase 513 (finding 513): Thai and Lao OpenType shaping (`_hb_ot_shaper_thai`):
// `SARA AM` (`U+0E33`, `U+0EB3`) decomposition and `NIKHAHIT` (`U+0E4D`, `U+0ECD`)
// reordering across preceding above-base marks, legacy Thai PUA fallback shaping
// (`U+F700..U+F71A` Windows and `U+F884..U+F89E` Macintosh) when no explicit
// `'thai'` GSUB script exists, and GDEF-less non-spacing mark width zeroing with
// GPOS-less forward offset compensation (`ZERO_WIDTH_MARKS_BY_GDEF_LATE`).

TEST(thai_lao_sara_am_and_pua_shaping) {
    using namespace litebird;

    auto glyph_ids = [](const TrueTypeFont& f, const std::vector<uint32_t>& cps) {
        std::vector<uint16_t> out;
        for (const auto& g : f.shape_glyphs(cps, true, false)) out.push_back(g.glyph);
        return out;
    };
    auto same_run = [](const std::vector<TrueTypeFont::PositionedGlyph>& a,
                       const std::vector<TrueTypeFont::PositionedGlyph>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i].glyph != b[i].glyph || a[i].x_advance != b[i].x_advance ||
                a[i].y_advance != b[i].y_advance || a[i].x_offset != b[i].x_offset ||
                a[i].y_offset != b[i].y_offset) {
                return false;
            }
        }
        return true;
    };

    // -------------------------------------------------------------------------
    // 1. Lao `preprocess_text_thai` (`U+0EB3` -> `U+0ECD` + `U+0EB2`), above-mark
    //    reordering, interaction with below-mark CCC sorting, and GDEF-less
    //    `Mn` mark zeroing + GPOS-less offset compensation (including U+0ECE).
    // -------------------------------------------------------------------------
    {
        const std::vector<TestGlyphSpec> lao_glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {' ', 250, false, 0, 0, 0, 0},              // 1
            {0x0E81, 600, true, 40, 0, 560, 520},       // 2: KO
            {0x0E99, 600, true, 40, 0, 560, 520},       // 3: NO
            {0x0EAB, 600, true, 40, 0, 560, 520},       // 4: HO SUNG
            {0x0EB1, 400, true, 80, 560, 320, 680},     // 5: MAI KAN (Mn, above)
            {0x0EB2, 500, true, 40, 0, 460, 520},       // 6: VOWEL SIGN AA (Lo)
            {0x0EB3, 900, true, 40, 0, 860, 680},       // 7: VOWEL SIGN AM (decomposed!)
            {0x0EB4, 400, true, 80, 560, 320, 680},     // 8: VOWEL SIGN I (Mn, above)
            {0x0EB8, 400, true, 80, -180, 320, -60},    // 9: VOWEL SIGN U (Mn, CCC 118)
            {0x0EBB, 400, true, 80, 560, 320, 680},     // 10: MAI KON (Mn, above = 0E3B|0x80)
            {0x0EBC, 400, true, 80, -180, 320, -60},    // 11: SEMIVOWEL SIGN LO (Mn, NOT above)
            {0x0EC8, 400, true, 80, 700, 320, 820},     // 12: MAI EK (Mn, CCC 122, above)
            {0x0EC9, 400, true, 80, 700, 320, 820},     // 13: MAI THO (Mn, CCC 122, above)
            {0x0ECA, 400, true, 80, 700, 320, 820},     // 14: MAI TI (Mn, CCC 122, above)
            {0x0ECB, 400, true, 80, 700, 320, 820},     // 15: MAI CATAWA (Mn, CCC 122, above)
            {0x0ECC, 400, true, 80, 560, 320, 680},     // 16: CANCELLATION MARK (Mn, above)
            {0x0ECD, 400, true, 80, 560, 320, 680},     // 17: NIGGAHITA (Mn, above)
            {0x0ECE, 400, true, 80, 560, 320, 680},     // 18: LAO YAMAKKAN (Unicode 15 Mn, above)
        };
        TrueTypeFont f_lao;
        assert(f_lao.load_memory(build_synthetic_sfnt(1000, 800, -200, lao_glyphs, {})));

        // Basic <KO, SARA AM> -> [KO(2), NIGGAHITA(17), AA(6)], never precomposed gid 7.
        const auto ko_am = f_lao.shape_glyphs({0x0E81, 0x0EB3});
        assert((glyph_ids(f_lao, {0x0E81, 0x0EB3}) == std::vector<uint16_t>{2, 17, 6}));
        assert(ko_am[0].x_advance == 600 && ko_am[0].x_offset == 0);
        assert(ko_am[1].x_advance == 0 && ko_am[1].x_offset == -400 && ko_am[1].is_mark);
        assert(ko_am[2].x_advance == 500 && ko_am[2].x_offset == 0 && !ko_am[2].is_mark);
        assert(same_run(ko_am, f_lao.shape_glyphs({0x0E81, 0x0ECD, 0x0EB2})));

        // Everyday word ນ້ຳ (<NO, MAI THO, SARA AM>) -> [NO(3), NIGGAHITA(17), MAI THO(13), AA(6)].
        assert((glyph_ids(f_lao, {0x0E99, 0x0EC9, 0x0EB3}) == std::vector<uint16_t>{3, 17, 13, 6}));
        assert(same_run(f_lao.shape_glyphs({0x0E99, 0x0EC9, 0x0EB3}),
                        f_lao.shape_glyphs({0x0E99, 0x0ECD, 0x0EC9, 0x0EB2})));

        // All four tones and multiple contiguous above-base marks crossed by NIGGAHITA:
        assert((glyph_ids(f_lao, {0x0E81, 0x0EC8, 0x0EB3}) == std::vector<uint16_t>{2, 17, 12, 6}));
        assert((glyph_ids(f_lao, {0x0E81, 0x0ECA, 0x0EB3}) == std::vector<uint16_t>{2, 17, 14, 6}));
        assert((glyph_ids(f_lao, {0x0E81, 0x0ECB, 0x0EB3}) == std::vector<uint16_t>{2, 17, 15, 6}));
        assert((glyph_ids(f_lao, {0x0E81, 0x0EB4, 0x0EC9, 0x0EB3}) == std::vector<uint16_t>{2, 17, 8, 13, 6}));
        assert((glyph_ids(f_lao, {0x0E81, 0x0EB1, 0x0EC8, 0x0EB3}) == std::vector<uint16_t>{2, 17, 5, 12, 6}));
        assert((glyph_ids(f_lao, {0x0E81, 0x0EBB, 0x0EC9, 0x0EB3}) == std::vector<uint16_t>{2, 17, 10, 13, 6}));
        assert((glyph_ids(f_lao, {0x0E81, 0x0ECC, 0x0EB3}) == std::vector<uint16_t>{2, 17, 16, 6}));
        // U+0ECE LAO YAMAKKAN (`(0x0ECE & ~0x80) == 0x0E4E` is in `IS_ABOVE_BASE_MARK` and `Mn`!):
        const auto yam = f_lao.shape_glyphs({0x0E81, 0x0ECE, 0x0EB3});
        assert((glyph_ids(f_lao, {0x0E81, 0x0ECE, 0x0EB3}) == std::vector<uint16_t>{2, 17, 18, 6}));
        assert(yam[2].x_advance == 0 && yam[2].x_offset == -400 && yam[2].is_mark);

        // Non-above-base marks stop NIGGAHITA during `preprocess_text_thai`:
        // - Semivowel LO (`0x0EBC`, CCC 0): NIGGAHITA crosses `0x0EC9` and stops after `0x0EBC`.
        assert((glyph_ids(f_lao, {0x0EAB, 0x0EBC, 0x0EC9, 0x0EB3}) == std::vector<uint16_t>{4, 11, 17, 13, 6}));
        // - `<KO, 0x0EB8, 0x0EC9, 0x0EB3>`: NIGGAHITA crosses `0x0EC9` and stops after `0x0EB8`.
        assert((glyph_ids(f_lao, {0x0E81, 0x0EB8, 0x0EC9, 0x0EB3}) == std::vector<uint16_t>{2, 9, 17, 13, 6}));
        // - `<KO, 0x0EC9, 0x0EB8, 0x0EB3>`: `preprocess_text_thai` runs BEFORE mark sorting,
        //   so `0x0EB8` stops NIGGAHITA, then normalization sorts `0x0EB8` (118) before `0x0EC9` (122)!
        assert((glyph_ids(f_lao, {0x0E81, 0x0EC9, 0x0EB8, 0x0EB3}) == std::vector<uint16_t>{2, 9, 13, 17, 6}));

        // Controls: trailing tone after SARA AM stays after AA; literal NIGGAHITA does not reorder;
        // standalone SARA AM decomposes to [NIGGAHITA, AA].
        assert((glyph_ids(f_lao, {0x0E81, 0x0EB3, 0x0EC9}) == std::vector<uint16_t>{2, 17, 6, 13}));
        assert((glyph_ids(f_lao, {0x0E81, 0x0EC9, 0x0ECD, 0x0EB2}) == std::vector<uint16_t>{2, 13, 17, 6}));
        assert((glyph_ids(f_lao, {0x0EB3}) == std::vector<uint16_t>{17, 6}));
    }

    // -------------------------------------------------------------------------
    // 2. Legacy Thai PUA state machine (`do_thai_pua_shaping`): Windows (`F700..F71A`)
    //    and Macintosh (`F884..F89E`) tables, out-of-order mark normalization
    //    after PUA remapping, and `'thai'` GSUB script gating.
    // -------------------------------------------------------------------------
    {
        // Build a synthetic Windows-PUA Thai face with non-zero mark `hmtx` widths (400)
        // and NO GSUB / GDEF / GPOS tables.
        std::vector<TestGlyphSpec> win_glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {' ', 250, false, 0, 0, 0, 0},
            {0x0E01, 600, true, 40, 0, 560, 520},     // NC: KO KAI
            {0x0E0D, 600, true, 40, -180, 560, 520},  // RC: YO YING
            {0x0E0E, 600, true, 40, -180, 560, 520},  // DC: DO CHADA
            {0x0E0F, 600, true, 40, -180, 560, 520},  // DC: TO PATAK
            {0x0E10, 600, true, 40, -180, 560, 520},  // RC: THO THAN
            {0x0E1B, 600, true, 40, 0, 560, 740},     // AC: PO PLA
            {0x0E1D, 600, true, 40, 0, 560, 740},     // AC: FO FA
            {0x0E1F, 600, true, 40, 0, 560, 740},     // AC: FO FAN
            {0x0E31, 400, true, 120, 560, 360, 680},  // AV: MAI HAN-AKAT
            {0x0E32, 500, true, 40, 0, 460, 520},     // Lo: SARA AA
            {0x0E33, 900, true, 40, 0, 860, 680},     // Lo: SARA AM
            {0x0E34, 400, true, 120, 560, 360, 680},  // AV: SARA I
            {0x0E38, 400, true, 120, -180, 360, -60}, // BV: SARA U (CCC 103 -> 3)
            {0x0E39, 400, true, 120, -180, 360, -60}, // BV: SARA UU (CCC 103 -> 3)
            {0x0E3A, 400, true, 120, -180, 360, -60}, // BV: PHINTHU (CCC 9)
            {0x0E47, 400, true, 120, 560, 360, 680},  // AV: MAITAIKHU
            {0x0E48, 400, true, 120, 700, 360, 820},  // T: MAI EK (CCC 107)
            {0x0E49, 400, true, 120, 700, 360, 820},  // T: MAI THO (CCC 107)
            {0x0E4A, 400, true, 120, 700, 360, 820},  // T: MAI TRI (CCC 107)
            {0x0E4B, 400, true, 120, 700, 360, 820},  // T: MAI CHATTAWA (CCC 107)
            {0x0E4C, 400, true, 120, 700, 360, 820},  // T: THANTHAKHAT (CCC 0)
            {0x0E4D, 400, true, 120, 560, 360, 680},  // AV: NIKHAHIT
            {0x0E4E, 400, true, 120, 560, 360, 680},  // AV: YAMAKKAN
        };
        for (uint32_t pua = 0xF700u; pua <= 0xF71Au; ++pua) {
            const uint16_t adv = (pua == 0xF700u || pua == 0xF70Fu) ? 600u : 400u;
            win_glyphs.push_back({pua, adv, true, 40, 0, 360, 760});
        }
        TrueTypeFont f_win;
        assert(f_win.load_memory(build_synthetic_sfnt(1000, 800, -200, win_glyphs, {})));
        auto gid_of = [&](uint32_t cp) { return f_win.glyph_index(cp); };

        // NC + T -> SD (`0x0E48 -> 0xF70A`), zeroed mark width, `x_offset == -400`:
        const auto nc_t = f_win.shape_glyphs({0x0E01, 0x0E48});
        assert(nc_t.size() == 2 && nc_t[0].glyph == gid_of(0x0E01) && nc_t[1].glyph == gid_of(0xF70A));
        assert(nc_t[0].x_advance == 600 && nc_t[0].x_offset == 0);
        assert(nc_t[1].x_advance == 0 && nc_t[1].x_offset == -400);

        // NC + AV + T -> NOP + NOP (`[0x0E01, 0x0E34, 0x0E48]`):
        assert((glyph_ids(f_win, {0x0E01, 0x0E34, 0x0E48}) ==
                std::vector<uint16_t>{gid_of(0x0E01), gid_of(0x0E34), gid_of(0x0E48)}));

        // AC + T -> SDL (`0x0E1B+0x0E48 -> 0xF705`, `0x0E1D+0x0E49 -> 0xF706`, `0x0E1F+0x0E4A -> 0xF707`):
        assert((glyph_ids(f_win, {0x0E1B, 0x0E48}) == std::vector<uint16_t>{gid_of(0x0E1B), gid_of(0xF705)}));
        assert((glyph_ids(f_win, {0x0E1D, 0x0E49}) == std::vector<uint16_t>{gid_of(0x0E1D), gid_of(0xF706)}));
        assert((glyph_ids(f_win, {0x0E1F, 0x0E4A}) == std::vector<uint16_t>{gid_of(0x0E1F), gid_of(0xF707)}));

        // AC + AV + T -> SL + SL (`0x0E34 -> 0xF701`, `0x0E48 -> 0xF713`):
        assert((glyph_ids(f_win, {0x0E1B, 0x0E34, 0x0E48}) ==
                std::vector<uint16_t>{gid_of(0x0E1B), gid_of(0xF701), gid_of(0xF713)}));

        // AC + T + SARA AM -> `<0x0E1B, 0x0E4D, 0x0E49, 0x0E32>` -> `[0x0E1B, 0xF711, 0xF714, 0x0E32]`:
        const auto ac_am = f_win.shape_glyphs({0x0E1B, 0x0E49, 0x0E33});
        assert((glyph_ids(f_win, {0x0E1B, 0x0E49, 0x0E33}) ==
                std::vector<uint16_t>{gid_of(0x0E1B), gid_of(0xF711), gid_of(0xF714), gid_of(0x0E32)}));
        assert(ac_am[0].x_advance == 600 && ac_am[1].x_advance == 0 && ac_am[1].x_offset == -400);
        assert(ac_am[2].x_advance == 0 && ac_am[2].x_offset == -400 && ac_am[3].x_advance == 500);

        // RC + BV -> RD on base consonant (`0x0E0D -> 0xF70F`, `0x0E10 -> 0xF700`) with base width 600!
        const auto rc_bv = f_win.shape_glyphs({0x0E0D, 0x0E38});
        assert(rc_bv.size() == 2 && rc_bv[0].glyph == gid_of(0xF70F) && rc_bv[1].glyph == gid_of(0x0E38));
        assert(rc_bv[0].x_advance == 600 && rc_bv[0].x_offset == 0 && !rc_bv[0].is_mark);
        assert(rc_bv[1].x_advance == 0 && rc_bv[1].x_offset == -400 && rc_bv[1].is_mark);
        assert((glyph_ids(f_win, {0x0E10, 0x0E39}) == std::vector<uint16_t>{gid_of(0xF700), gid_of(0x0E39)}));

        // DC + BV -> SD on below vowel (`0x0E38 -> 0xF718`, `0x0E3A -> 0xF71A`):
        assert((glyph_ids(f_win, {0x0E0E, 0x0E38}) == std::vector<uint16_t>{gid_of(0x0E0E), gid_of(0xF718)}));
        assert((glyph_ids(f_win, {0x0E0F, 0x0E3A}) == std::vector<uint16_t>{gid_of(0x0E0F), gid_of(0xF71A)}));

        // Out-of-order marks remapped to PUA BEFORE normalization STILL reorder by original modified CCC!
        assert((glyph_ids(f_win, {0x0E01, 0x0E48, 0x0E38}) ==
                std::vector<uint16_t>{gid_of(0x0E01), gid_of(0x0E38), gid_of(0xF70A)}));
        assert((glyph_ids(f_win, {0x0E0E, 0x0E3A, 0x0E38}) ==
                std::vector<uint16_t>{gid_of(0x0E0E), gid_of(0xF718), gid_of(0xF71A)}));

        // NOT_CONSONANT (`0x0E32`) starts in `(T3, B2)`: `T` stays `NOP`, `BV` shifts down (`SD`)!
        assert((glyph_ids(f_win, {0x0E32, 0x0E48}) == std::vector<uint16_t>{gid_of(0x0E32), gid_of(0x0E48)}));
        assert((glyph_ids(f_win, {0x0E32, 0x0E38}) == std::vector<uint16_t>{gid_of(0x0E32), gid_of(0xF718)}));

        // Macintosh PUA fallback (`0xF884..0xF89E`) when Windows PUA glyphs are absent:
        std::vector<TestGlyphSpec> mac_glyphs(win_glyphs.begin(), win_glyphs.begin() + 25);
        for (uint32_t pua = 0xF884u; pua <= 0xF89Eu; ++pua) {
            const uint16_t adv = (pua == 0xF89Au || pua == 0xF89Eu) ? 600u : 400u;
            mac_glyphs.push_back({pua, adv, true, 40, 0, 360, 760});
        }
        TrueTypeFont f_mac;
        assert(f_mac.load_memory(build_synthetic_sfnt(1000, 800, -200, mac_glyphs, {})));
        assert((glyph_ids(f_mac, {0x0E1B, 0x0E48}) ==
                std::vector<uint16_t>{f_mac.glyph_index(0x0E1B), f_mac.glyph_index(0xF88C)}));
        assert((glyph_ids(f_mac, {0x0E1B, 0x0E34, 0x0E48}) ==
                std::vector<uint16_t>{f_mac.glyph_index(0x0E1B), f_mac.glyph_index(0xF885),
                                      f_mac.glyph_index(0xF88A)}));
        assert((glyph_ids(f_mac, {0x0E0D, 0x0E38}) ==
                std::vector<uint16_t>{f_mac.glyph_index(0xF89A), f_mac.glyph_index(0x0E38)}));
        assert((glyph_ids(f_mac, {0x0E0E, 0x0E38}) ==
                std::vector<uint16_t>{f_mac.glyph_index(0x0E0E), f_mac.glyph_index(0xF89B)}));

        // GSUB `'thai'` script gating:
        // - `GSUB(DFLT)` without `'thai'` STILL runs `do_thai_pua_shaping` (`0x0E1B+0x0E48 -> 0xF705`).
        // - Even an empty `GSUB(thai)` script record suppresses `do_thai_pua_shaping`
        //   (`0x0E1B+0x0E48 -> 0x0E48`) while preserving `preprocess_text_thai` (`SARA AM`)!
        const auto gsub_dflt = build_otl_table({{"DFLT", {}}}, {});
        const auto gsub_thai = build_otl_table({{"thai", {}}}, {});
        TrueTypeFont f_gsub_dflt, f_gsub_thai;
        assert(f_gsub_dflt.load_memory(build_synthetic_sfnt(
            1000, 800, -200, win_glyphs, {{"GSUB", gsub_dflt}})));
        assert(f_gsub_thai.load_memory(build_synthetic_sfnt(
            1000, 800, -200, win_glyphs, {{"GSUB", gsub_thai}})));
        assert((glyph_ids(f_gsub_dflt, {0x0E1B, 0x0E48}) ==
                std::vector<uint16_t>{gid_of(0x0E1B), gid_of(0xF705)}));
        assert((glyph_ids(f_gsub_thai, {0x0E1B, 0x0E48}) ==
                std::vector<uint16_t>{gid_of(0x0E1B), gid_of(0x0E48)}));
        assert((glyph_ids(f_gsub_thai, {0x0E1B, 0x0E49, 0x0E33}) ==
                std::vector<uint16_t>{gid_of(0x0E1B), gid_of(0x0E4D), gid_of(0x0E49), gid_of(0x0E32)}));

        // GPOS presence disables `adjust_offsets_when_zeroing` (`x_offset` stays 0, `x_advance` still 0):
        const auto gpos_empty = build_otl_table({{"thai", {}}}, {});
        TrueTypeFont f_with_gpos;
        assert(f_with_gpos.load_memory(build_synthetic_sfnt(
            1000, 800, -200, win_glyphs, {{"GSUB", gsub_thai}, {"GPOS", gpos_empty}})));
        const auto gpos_res = f_with_gpos.shape_glyphs({0x0E01, 0x0E48});
        assert(gpos_res.size() == 2 && gpos_res[1].x_advance == 0 && gpos_res[1].x_offset == 0);
    }

    // -------------------------------------------------------------------------
    // 3. Host DejaVuSans / DejaVuSansMono parity + end-to-end HTML pixel paint.
    // -------------------------------------------------------------------------
    FontRenderer& fr = FontRenderer::instance();
    for (const char* family : {"DejaVu Sans", "DejaVu Sans Mono"}) {
        const std::string path = fr.face_path(family);
        if (path.empty()) continue;
        TrueTypeFont face;
        if (face.load_file(path) && face.glyph_index(0x0E99) != 0 && face.glyph_index(0x0EB3) != 0) {
            assert(same_run(face.shape_glyphs({0x0E81, 0x0EB3}),
                            face.shape_glyphs({0x0E81, 0x0ECD, 0x0EB2})));
            assert(same_run(face.shape_glyphs({0x0E99, 0x0EC9, 0x0EB3}),
                            face.shape_glyphs({0x0E99, 0x0ECD, 0x0EC9, 0x0EB2})));
        }
    }

    if (fr.register_font_file("tests/wpt/fonts/Ahem.ttf") != FontRenderer::kNoFamily) {
        const std::vector<TestGlyphSpec> web_glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {' ', 250, false, 0, 0, 0, 0},
            {0x0E99, 600, true, 60, 0, 520, 460},      // Lao NO
            {0x0EB2, 500, true, 60, 0, 440, 460},      // Lao AA
            {0x0EB3, 950, true, 60, 0, 900, 460},      // Unshaped AM (must not be used!)
            {0x0EC9, 400, true, 80, 680, 320, 800},    // Lao MAI THO
            {0x0ECD, 400, true, 120, 520, 280, 640},   // Lao NIGGAHITA
        };
        const auto sfnt_bytes = build_synthetic_sfnt(1000, 800, -200, web_glyphs, {});
        assert(fr.register_webfont("Ahem", {[sfnt_bytes]() { return sfnt_bytes; }},
                                   400, 400, false) >= 0);

        auto render_doc = [&](const char* body_text) {
            const std::string html = std::string(
                "<!doctype html><html><body style=\"margin:0;background:#ffffff\">"
                "<div style=\"font:48px 'Ahem';color:#000000;padding:8px\">") +
                body_text + "</div></body></html>";
            auto doc = HtmlParser::parse(html);
            StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 240.0f, 96.0f);
            auto root = LayoutTree::build(doc, resolver, 240.0f, 96.0f);
            assert(root);
            const DisplayList dl = DisplayList::build(root);
            BitmapRenderer bmp(240, 96);
            bmp.clear(Color::white());
            bmp.render(dl);
            return bmp;
        };

        const BitmapRenderer bmp_sara_am = render_doc("&#x0e99;&#x0ec9;&#x0eb3;");
        const BitmapRenderer bmp_explicit = render_doc("&#x0e99;&#x0ecd;&#x0ec9;&#x0eb2;");
        const BitmapRenderer bmp_without_nik = render_doc("&#x0e99;&#x0ec9;&#x0eb2;");
        int ink_pixels = 0;
        int diff_without_nik = 0;
        for (int y = 0; y < 96; ++y) {
            for (int x = 0; x < 240; ++x) {
                const Color p_am = bmp_sara_am.get_pixel(x, y);
                const Color p_ex = bmp_explicit.get_pixel(x, y);
                const Color p_no = bmp_without_nik.get_pixel(x, y);
                assert(p_am == p_ex);
                if (!(p_am == Color::white())) ++ink_pixels;
                if (!(p_am == p_no)) ++diff_without_nik;
            }
        }
        assert(ink_pixels > 40);
        assert(diff_without_nik > 10);
        fr.clear_webfonts();
    }
}
