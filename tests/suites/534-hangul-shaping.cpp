// Phase 534 (finding 534): OpenType Hangul shaping (`_hb_ot_shaper_hangul`):
// Unicode Hangul script itemization (`kTagHang`), algorithmic `<L,V>` / `<L,V,T>`
// and `<LV,T>` syllable composition, font-aware `<LV>` / `<LVT>` / `<LV,T>`
// decomposition with per-jamo `ljmo`/`vjmo`/`tjmo` `GSUB`+`GPOS` feature masks,
// `calt` suppression on conjoining jamo (`isL`/`isV`/`isT`), visible Hangul
// fillers (`U+115F`, `U+1160`, `U+3164`, `U+FFA0`), Middle Korean `U+302E` /
// `U+302F` tone-mark reordering and `U+25CC` dotted-circle insertion, and
// `NORMALIZATION_MODE_NONE` / `ZERO_WIDTH_MARKS_NONE` mark handling.

TEST(hangul_syllable_jamo_and_tone_shaping) {
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
    auto make_single_subst = [](const std::vector<std::pair<uint16_t, uint16_t>>& pairs) {
        SfntByteBuf st;
        st.u16(2); // SingleSubstFormat2
        st.u16(static_cast<uint16_t>(6u + 2u * pairs.size()));
        st.u16(static_cast<uint16_t>(pairs.size()));
        std::vector<uint16_t> cov_gids;
        for (const auto& [src, dst] : pairs) {
            cov_gids.push_back(src);
            st.u16(dst);
        }
        const auto cov = build_cov_fmt1(cov_gids);
        st.data.insert(st.data.end(), cov.begin(), cov.end());
        return st.data;
    };
    auto make_single_pos = [](const std::vector<std::tuple<uint16_t, int16_t, int16_t, int16_t>>& entries) {
        SfntByteBuf st;
        st.u16(2); // SinglePosFormat2
        st.u16(static_cast<uint16_t>(8u + 6u * entries.size()));
        st.u16(0x0007u); // XPlacement | YPlacement | XAdvance
        st.u16(static_cast<uint16_t>(entries.size()));
        std::vector<uint16_t> cov_gids;
        for (const auto& [gid, dx, dy, dadv] : entries) {
            cov_gids.push_back(gid);
            st.i16(dx);
            st.i16(dy);
            st.i16(dadv);
        }
        const auto cov = build_cov_fmt1(cov_gids);
        st.data.insert(st.data.end(), cov.begin(), cov.end());
        return st.data;
    };

    // -------------------------------------------------------------------------
    // 1. Hybrid OpenType Hangul face (`'hang'` GSUB + GPOS + GDEF):
    //    - Covers U+AC00 (가), U+AC01 (각), U+AE00 (글), U+D55C (한), but omits
    //      U+AC02 (갂), U+B2E4 (다), and U+B2E5 (닥) to test fallback decomposition.
    // -------------------------------------------------------------------------
    auto build_hangul_face = [&](uint16_t tone_adv, bool include_25cc, const std::string& script_tag) {
        std::vector<TestGlyphSpec> glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {' ', 300, false, 0, 0, 0, 0},                // 1
            {0x0301, 220, true, 40, 620, 160, 740},       // 2: COMBINING ACUTE ACCENT (CCC 230)
            {0x0323, 220, true, 40, -140, 160, -20},      // 3: COMBINING DOT BELOW (CCC 220)
            {0x1100, 800, true, 40, 40, 760, 720},        // 4: L KIYEOK
            {0x1103, 800, true, 40, 40, 760, 720},        // 5: L TIKEUT
            {0x1112, 800, true, 40, 40, 760, 720},        // 6: L HIEUH
            {0x1140, 800, true, 40, 40, 760, 720},        // 7: Old L PANSIOS
            {0x115F, 800, true, 40, 40, 760, 720},        // 8: CHOSEONG FILLER
            {0x1160, 800, true, 40, 40, 760, 720},        // 9: JUNGSEONG FILLER
            {0x1161, 800, true, 40, 40, 760, 720},        // 10: V A
            {0x1173, 800, true, 40, 40, 760, 720},        // 11: V EU
            {0x119E, 800, true, 40, 40, 760, 720},        // 12: Old V ARAEA
            {0x11A8, 800, true, 40, 40, 760, 720},        // 13: T KIYEOK
            {0x11A9, 800, true, 40, 40, 760, 720},        // 14: T SSANGKIYEOK
            {0x11AB, 800, true, 40, 40, 760, 720},        // 15: T NIEUN
            {0x11AF, 800, true, 40, 40, 760, 720},        // 16: T RIEUL
            {0x11EB, 800, true, 40, 40, 760, 720},        // 17: Old T PANSIOS
            {include_25cc ? 0x25CCu : 0xE000u, 800, true, 40, 40, 760, 720}, // 18: DOTTED CIRCLE
            {0x302E, tone_adv, true, 20, 420, 180, 560},  // 19: SINGLE DOT TONE MARK (CCC 224)
            {0x302F, tone_adv, true, 20, 460, 180, 600},  // 20: DOUBLE DOT TONE MARK (CCC 224)
            {0x3131, 800, true, 40, 40, 760, 720},        // 21: COMPAT KIYEOK
            {0x3164, 800, true, 40, 40, 760, 720},        // 22: HANGUL FILLER
            {0xA960, 800, true, 40, 40, 760, 720},        // 23: Ext-A L
            {0xAC00, 800, true, 40, 40, 760, 720},        // 24: SYLLABLE GA (LV)
            {0xAC01, 800, true, 40, 40, 760, 720},        // 25: SYLLABLE GAG (LVT)
            {0xAE00, 800, true, 40, 40, 760, 720},        // 26: SYLLABLE GEUL (LVT)
            {0xD55C, 800, true, 40, 40, 760, 720},        // 27: SYLLABLE HAN (LVT)
            {0xD7B0, 800, true, 40, 40, 760, 720},        // 28: Ext-B V
            {0xD7CB, 800, true, 40, 40, 760, 720},        // 29: Ext-B T
            {0xFFA0, 800, true, 40, 40, 760, 720},        // 30: HALFWIDTH HANGUL FILLER
            // Unmapped GSUB targets (31..49):
            {0xE100, 800, true, 40, 380, 360, 720},       // 31: ljmo(1100)
            {0xE101, 800, true, 40, 380, 360, 720},       // 32: ljmo(1103)
            {0xE102, 800, true, 40, 380, 360, 720},       // 33: ljmo(1140)
            {0xE103, 800, true, 40, 380, 360, 720},       // 34: ljmo(115F)
            {0xE104, 800, true, 40, 380, 360, 720},       // 35: ljmo(A960)
            {0xE105, 800, true, 420, 260, 740, 720},      // 36: vjmo(1160)
            {0xE106, 800, true, 420, 260, 740, 720},      // 37: vjmo(1161)
            {0xE107, 800, true, 420, 260, 740, 720},      // 38: vjmo(119E)
            {0xE108, 800, true, 420, 260, 740, 720},      // 39: vjmo(D7B0)
            {0xE109, 800, true, 120, 20, 680, 220},       // 40: tjmo(11A8)
            {0xE10A, 800, true, 120, 20, 680, 220},       // 41: tjmo(11A9)
            {0xE10B, 800, true, 120, 20, 680, 220},       // 42: tjmo(11EB)
            {0xE10C, 800, true, 120, 20, 680, 220},       // 43: tjmo(D7CB)
            {0xE10D, 800, true, 40, 40, 760, 720},        // 44: calt decoy on 1100 (MUST NOT FIRE!)
            {0xE10E, 800, true, 40, 40, 760, 720},        // 45: calt on compat 3131 (FIRES!)
            {0xE10F, 800, true, 40, 40, 760, 720},        // 46: calt on AC00 (FIRES!)
        };

        auto gsub = build_otl_table(
            {{script_tag, {{"calt", {0}}, {"ljmo", {1}}, {"tjmo", {3}}, {"vjmo", {2}}}}},
            {
                {"calt", 1, 0, -1, {make_single_subst({{4, 44}, {21, 45}, {24, 46}})}},
                {"ljmo", 1, 0, -1, {make_single_subst({{4, 31}, {5, 32}, {7, 33}, {8, 34}, {23, 35}})}},
                {"vjmo", 1, 0, -1, {make_single_subst({{9, 36}, {10, 37}, {12, 38}, {28, 39}})}},
                {"tjmo", 1, 0, -1, {make_single_subst({{13, 40}, {14, 41}, {17, 42}, {29, 43}})}},
            });

        auto gpos = build_otl_table(
            {{script_tag, {{"calt", {0}}, {"tjmo", {2}}, {"vjmo", {1}}}}},
            {
                {"calt", 1, 0, -1, {make_single_pos({{4, 150, 0, 150}, {45, 30, 0, 40}})}},
                {"vjmo", 1, 0, -1, {make_single_pos({{36, -800, 0, -800}, {37, -800, 0, -800},
                                                     {38, -800, 0, -800}, {39, -800, 0, -800}})}},
                {"tjmo", 1, 0, -1, {make_single_pos({{40, -800, -40, -800}, {41, -800, -40, -800},
                                                     {42, -800, -40, -800}, {43, -800, -40, -800}})}},
            });

        SfntByteBuf gdef;
        gdef.u32(0x00010000u);
        gdef.u16(12);
        gdef.u16(0);
        gdef.u16(0);
        gdef.u16(0);
        const auto gcd = build_classdef_fmt2({{2, 3, 3}, {19, 20, 3}});
        gdef.data.insert(gdef.data.end(), gcd.begin(), gcd.end());

        return build_synthetic_sfnt(
            1000, 800, -200, glyphs,
            {{"GDEF", gdef.data}, {"GSUB", gsub}, {"GPOS", gpos}});
    };

    TrueTypeFont f;
    assert(f.load_memory(build_hangul_face(260, true, "hang")));

    // Algorithmic <L,V> -> LV and <L,V,T> -> LVT composition; calt applies to composed AC00 (46).
    assert((glyph_ids(f, {0x1100, 0x1161}) == std::vector<uint16_t>{46}));
    assert((glyph_ids(f, {0x1112, 0x1161, 0x11AB}) == std::vector<uint16_t>{27}));
    assert((glyph_ids(f, {0x1112, 0x1161, 0x11AB, 0x1100, 0x1173, 0x11AF}) ==
            std::vector<uint16_t>{27, 26}));

    // Partial <LV,T>: compose when covered (AC00 + 11A8 -> AC01=25), else decompose to L+V+T
    // with ljmo/vjmo/tjmo GSUB + GPOS (and never calt decoy 44!).
    assert((glyph_ids(f, {0xAC00, 0x11A8}) == std::vector<uint16_t>{25}));
    const auto partial_decomp = f.shape_glyphs({0xAC00, 0x11A9});
    assert((glyph_ids(f, {0xAC00, 0x11A9}) == std::vector<uint16_t>{31, 37, 41}));
    assert(partial_decomp[0].x_advance == 800 && partial_decomp[0].x_offset == 0);
    assert(partial_decomp[1].x_advance == 0 && partial_decomp[1].x_offset == -800);
    assert(partial_decomp[2].x_advance == 0 && partial_decomp[2].x_offset == -800 &&
           partial_decomp[2].y_offset == -40);
    assert((glyph_ids(f, {0xAC00, 0xD7CB}) == std::vector<uint16_t>{31, 37, 43}));
    // <LVT, T> does NOT recompose or give T tjmo: [AC01(25), raw 11A8(13)].
    assert((glyph_ids(f, {0xAC01, 0x11A8}) == std::vector<uint16_t>{25, 13}));

    // Missing precomposed <LV> (U+B2E4 다) and <LVT> (U+B2E5 닥) decompose to L+V(+T) with ljmo/vjmo/tjmo;
    // missing <LV> + trailing T (!has_glyph && !tindex) decomposes LV to [32, 37] and leaves trailing T raw (13).
    assert((glyph_ids(f, {0xB2E4}) == std::vector<uint16_t>{32, 37}));
    assert((glyph_ids(f, {0xB2E5}) == std::vector<uint16_t>{32, 37, 40}));
    assert((glyph_ids(f, {0xB2E4, 0x11A8}) == std::vector<uint16_t>{32, 37, 13}));
    assert((glyph_ids(f, {0x1103, 0x1161, 0x11A8}) == std::vector<uint16_t>{32, 37, 40}));

    // Old Hangul (U+11xx and Jamo Extended-A/B) + visible Hangul fillers + calt gating.
    assert((glyph_ids(f, {0x1140, 0x119E, 0x11EB}) == std::vector<uint16_t>{33, 38, 42}));
    assert((glyph_ids(f, {0xA960, 0xD7B0, 0xD7CB}) == std::vector<uint16_t>{35, 39, 43}));
    assert((glyph_ids(f, {0x115F, 0x1161}) == std::vector<uint16_t>{34, 37}));
    assert((glyph_ids(f, {0x1100, 0x1160}) == std::vector<uint16_t>{31, 36}));
    const auto standalone_jamo = f.shape_glyphs({0x1100, 0x11A8});
    assert((glyph_ids(f, {0x1100, 0x11A8}) == std::vector<uint16_t>{4, 13}));
    assert(standalone_jamo[0].x_advance == 800 && standalone_jamo[0].x_offset == 0); // GPOS calt suppressed!
    const auto compat_and_fillers = f.shape_glyphs({0x3131, 0x3164, 0xFFA0});
    assert((glyph_ids(f, {0x3131, 0x3164, 0xFFA0}) == std::vector<uint16_t>{45, 22, 30}));
    assert(compat_and_fillers[0].x_advance == 840 && compat_and_fillers[0].x_offset == 30); // GPOS calt active!
    assert(compat_and_fillers[1].x_advance == 800 && compat_and_fillers[2].x_advance == 800);

    // Spacing tone marks (U+302E=19, U+302F=20): reorder before syllable or append U+25CC (18).
    assert((glyph_ids(f, {0xD55C, 0x302E}) == std::vector<uint16_t>{19, 27}));
    assert((glyph_ids(f, {0x1140, 0x119E, 0x11EB, 0x302F}) == std::vector<uint16_t>{20, 33, 38, 42}));
    assert((glyph_ids(f, {0x302E}) == std::vector<uint16_t>{19, 18}));
    assert((glyph_ids(f, {0xD55C, 0x302E, 0x302F}) == std::vector<uint16_t>{19, 27, 20, 18}));
    assert((glyph_ids(f, {0x3131, 0x302E}) == std::vector<uint16_t>{45, 19, 18}));

    // NORMALIZATION_MODE_NONE + ZERO_WIDTH_MARKS_NONE: CCC mark sorting without zeroing GDEF class-3 marks.
    const auto mark_sort = f.shape_glyphs({0x0301, 0xAC00, 0x302E});
    assert((glyph_ids(f, {0x0301, 0xAC00, 0x302E}) == std::vector<uint16_t>{19, 2, 46}));
    assert(mark_sort[0].x_advance == 260 && mark_sort[1].x_advance == 220);
    const auto post_marks = f.shape_glyphs({0xAC00, 0x0301, 0x0323});
    assert((glyph_ids(f, {0xAC00, 0x0301, 0x0323}) == std::vector<uint16_t>{46, 3, 2}));
    assert(post_marks[1].x_advance == 220 && post_marks[2].x_advance == 220);

    // -------------------------------------------------------------------------
    // 2. Zero-width tone marks (`hmtx` advance 0) and `DFLT`-only face without U+25CC.
    // -------------------------------------------------------------------------
    TrueTypeFont f_zw;
    assert(f_zw.load_memory(build_hangul_face(0, true, "hang")));
    assert((glyph_ids(f_zw, {0xD55C, 0x302E}) == std::vector<uint16_t>{27, 19}));
    assert((glyph_ids(f_zw, {0x1140, 0x119E, 0x11EB, 0x302F}) == std::vector<uint16_t>{33, 38, 42, 20}));
    assert((glyph_ids(f_zw, {0x302E}) == std::vector<uint16_t>{18, 19}));
    assert((glyph_ids(f_zw, {0xD55C, 0x302E, 0x302F}) == std::vector<uint16_t>{27, 19, 18, 20}));

    TrueTypeFont f_dflt;
    assert(f_dflt.load_memory(build_hangul_face(260, false, "DFLT")));
    assert((glyph_ids(f_dflt, {0x1100}) == std::vector<uint16_t>{4}));     // calt still blocked on jamo under DFLT!
    assert((glyph_ids(f_dflt, {0x3131}) == std::vector<uint16_t>{45}));    // calt still active on compat jamo!
    assert((glyph_ids(f_dflt, {0x302E}) == std::vector<uint16_t>{19}));    // no 0x25CC -> left untouched

    // -------------------------------------------------------------------------
    // 3. End-to-end HTML webfont rendering: NFD jamo `<ᄒ,ᅡ,ᆫ,ᄀ,ᅳ,ᆯ>` paints
    //    identically to NFC precomposed `한글` (`&#xd55c;&#xae00;`).
    // -------------------------------------------------------------------------
    FontRenderer& fr = FontRenderer::instance();
    if (fr.register_font_file("tests/wpt/fonts/Ahem.ttf") != FontRenderer::kNoFamily) {
        const auto sfnt_bytes = build_hangul_face(260, true, "hang");
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

        const BitmapRenderer bmp_nfd = render_doc("&#x1112;&#x1161;&#x11ab;&#x1100;&#x1173;&#x11af;");
        const BitmapRenderer bmp_nfc = render_doc("&#xd55c;&#xae00;");
        const BitmapRenderer bmp_diff = render_doc("&#xd55c;");
        assert(same_run(f.shape_glyphs({0x1112, 0x1161, 0x11AB, 0x1100, 0x1173, 0x11AF}),
                        f.shape_glyphs({0xD55C, 0xAE00})));
        int ink_pixels = 0;
        int diff_pixels = 0;
        for (int y = 0; y < 96; ++y) {
            for (int x = 0; x < 240; ++x) {
                const Color p_nfd = bmp_nfd.get_pixel(x, y);
                const Color p_nfc = bmp_nfc.get_pixel(x, y);
                const Color p_one = bmp_diff.get_pixel(x, y);
                assert(p_nfd == p_nfc);
                if (!(p_nfd == Color::white())) ++ink_pixels;
                if (!(p_nfd == p_one)) ++diff_pixels;
            }
        }
        assert(ink_pixels > 80);
        assert(diff_pixels > 40);
        fr.clear_webfonts();
    }
}
