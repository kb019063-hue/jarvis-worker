// Phase 547 (finding 547): OpenType shaping public source-cluster indices:
// - `TrueTypeFont::PositionedGlyph::cluster` (0-based input codepoint index in
//   `shape_glyphs`, matching HarfBuzz `HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES`)
// - `FontRenderer::ShapedGlyph::cluster` (0-based UTF-8 byte offset in
//   `shape_text`, across multi-byte UTF-8, ligatures, multi-face fallback runs,
//   visual RTL, and `pre_reversed` Bidi runs)

TEST(opentype_source_cluster_indices) {
    using namespace litebird;

    auto clusters_of = [](const std::vector<TrueTypeFont::PositionedGlyph>& run) {
        std::vector<uint32_t> out;
        out.reserve(run.size());
        for (const auto& g : run) out.push_back(g.cluster);
        return out;
    };
    auto shaped_clusters_of = [](const std::vector<FontRenderer::ShapedGlyph>& run) {
        std::vector<uint32_t> out;
        out.reserve(run.size());
        for (const auto& g : run) out.push_back(g.cluster);
        return out;
    };

    // -------------------------------------------------------------------------
    // 1. Synthetic OpenType face exercising GSUB Multiple (expand + delete),
    //    Alternate, Ligature across intervening + trailing marks, Emoji ZWJ +
    //    Extended_Pictographic, Regional Indicators, Hangul jamo/tones/Round 2,
    //    and Lao SARA AM.
    // -------------------------------------------------------------------------
    std::vector<TestGlyphSpec> glyphs = {
        {0, 500, false, 0, 0, 0, 0},
        {' ', 300, false, 0, 0, 0, 0},                // 1
        {'A', 600, true, 40, 0, 560, 700},            // 2
        {'D', 600, true, 40, 0, 560, 700},            // 3 (deleted by Multiple 1->0)
        {'X', 600, true, 40, 0, 560, 700},            // 4 (expanded by Multiple 1->3)
        {'a', 600, true, 40, 0, 560, 500},            // 5
        {'b', 600, true, 40, 0, 560, 700},            // 6
        {'f', 500, true, 40, 0, 460, 700},            // 7
        {'i', 300, true, 40, 0, 260, 700},            // 8
        {'x', 550, true, 40, 0, 510, 500},            // 9
        {0x0300, 0, true, 40, 560, 160, 680},         // 10: GRAVE (CCC 230)
        {0x0301, 0, true, 40, 560, 160, 680},         // 11: ACUTE (CCC 230)
        {0x0323, 0, true, 40, -140, 160, -20},        // 12: DOT BELOW (CCC 220)
        {0x0E99, 600, true, 40, 0, 560, 600},         // 13: LAO NO
        {0x0EB2, 500, true, 40, 0, 460, 600},         // 14: LAO SARA AA
        {0x0EB3, 600, true, 40, 0, 560, 600},         // 15: LAO SARA AM
        {0x0EC9, 0, true, 40, 620, 180, 740},         // 16: LAO MAI THO
        {0x0ECD, 0, true, 40, 560, 160, 680},         // 17: LAO NIGGAHITA
        {0x1100, 800, true, 40, 40, 760, 720},        // 18: HANGUL L KIYEOK
        {0x1103, 800, true, 40, 40, 760, 720},        // 19: HANGUL L TIKEUT
        {0x1161, 800, true, 40, 40, 760, 720},        // 20: HANGUL V A
        {0x11A8, 800, true, 40, 40, 760, 720},        // 21: HANGUL T KIYEOK
        {0x200C, 0, false, 0, 0, 0, 0},               // 22: ZWNJ
        {0x200D, 0, false, 0, 0, 0, 0},               // 23: ZWJ
        {0x20DD, 0, true, 10, -40, 590, 740},         // 24: ENCLOSING CIRCLE (Me)
        {0x25CC, 600, true, 40, 0, 560, 600},         // 25: DOTTED CIRCLE
        {0x302E, 260, true, 20, 420, 180, 560},       // 26: HANGUL SINGLE DOT TONE (Mc, CCC 224)
        {0xAC00, 800, true, 40, 40, 760, 720},        // 27: HANGUL SYLLABLE GA
        {0xAC01, 800, true, 40, 40, 760, 720},        // 28: HANGUL SYLLABLE GAG
        {0xFF76, 500, true, 40, 0, 460, 700},         // 29: HALFWIDTH KATAKANA KA
        {0xFF77, 500, true, 40, 0, 460, 700},         // 30: HALFWIDTH KATAKANA KI
        {0xFF9E, 250, true, 20, 500, 160, 680},       // 31: HALFWIDTH VOICED SOUND MARK
        {0xFF9F, 250, true, 20, 500, 160, 680},       // 32: HALFWIDTH SEMI-VOICED SOUND MARK
        {0x1F1E7, 800, true, 40, 0, 760, 700},        // 33: REGIONAL INDICATOR B
        {0x1F1EC, 800, true, 40, 0, 760, 700},        // 34: REGIONAL INDICATOR G
        {0x1F1F8, 800, true, 40, 0, 760, 700},        // 35: REGIONAL INDICATOR S
        {0x1F1FA, 800, true, 40, 0, 760, 700},        // 36: REGIONAL INDICATOR U
        {0x1F3F4, 800, true, 40, 0, 760, 700},        // 37: WAVING BLACK FLAG
        {0x1F3FD, 400, true, 40, 0, 360, 700},        // 38: EMOJI MODIFIER FITZPATRICK TYPE-4
        {0x1F44B, 800, true, 40, 0, 760, 700},        // 39: WAVING HAND SIGN
        {0x1F467, 800, true, 40, 0, 760, 700},        // 40: GIRL
        {0x1F468, 800, true, 40, 0, 760, 700},        // 41: MAN
        {0x1F469, 800, true, 40, 0, 760, 700},        // 42: WOMAN
        {0xE0067, 0, false, 0, 0, 0, 0},              // 43: TAG LATIN SMALL LETTER G
        {0xE007F, 0, false, 0, 0, 0, 0},              // 44: CANCEL TAG
        // Unmapped GSUB targets (45..49):
        {0xE200, 400, true, 40, 0, 360, 500},         // 45: m1
        {0xE201, 400, true, 40, 0, 360, 500},         // 46: m2
        {0xE202, 400, true, 40, 0, 360, 500},         // 47: m3
        {0xE203, 620, true, 40, 0, 580, 700},         // 48: alt(A)
        {0xE204, 720, true, 40, 0, 680, 700},         // 49: lig(f, i)
    };

    // MultipleSubstFormat1: gid 3 ('D') -> [], gid 4 ('X') -> [45, 46, 47]
    SfntByteBuf mult_st;
    const auto cov_mult = build_cov_fmt1({3, 4});
    const uint16_t mult_cov_off = 10u;
    const uint16_t seq0_off = static_cast<uint16_t>(mult_cov_off + cov_mult.size());
    const uint16_t seq1_off = static_cast<uint16_t>(seq0_off + 2u);
    mult_st.u16(1);
    mult_st.u16(mult_cov_off);
    mult_st.u16(2);
    mult_st.u16(seq0_off);
    mult_st.u16(seq1_off);
    mult_st.data.insert(mult_st.data.end(), cov_mult.begin(), cov_mult.end());
    mult_st.u16(0); // 'D' -> deleted
    mult_st.u16(3);
    mult_st.u16(45);
    mult_st.u16(46);
    mult_st.u16(47);

    // AlternateSubstFormat1: gid 2 ('A') -> [48]
    SfntByteBuf alt_st;
    const auto cov_alt = build_cov_fmt1({2});
    alt_st.u16(1);
    alt_st.u16(8);
    alt_st.u16(1);
    alt_st.u16(static_cast<uint16_t>(8u + cov_alt.size()));
    alt_st.data.insert(alt_st.data.end(), cov_alt.begin(), cov_alt.end());
    alt_st.u16(1);
    alt_st.u16(48);

    // LigatureSubstFormat1 (IgnoreMarks = 0x0008): gid 7 ('f') + gid 8 ('i') -> 49
    SfntByteBuf lig_st;
    const auto cov_lig = build_cov_fmt1({7});
    lig_st.u16(1);
    lig_st.u16(8);
    lig_st.u16(1);
    lig_st.u16(static_cast<uint16_t>(8u + cov_lig.size()));
    lig_st.data.insert(lig_st.data.end(), cov_lig.begin(), cov_lig.end());
    lig_st.u16(1);
    lig_st.u16(4);
    lig_st.u16(49);
    lig_st.u16(2);
    lig_st.u16(8);

    const auto gsub = build_otl_table(
        {
            {"DFLT", {{"calt", {1}}, {"ccmp", {0}}, {"liga", {2}}}},
            {"hang", {{"calt", {1}}, {"ccmp", {0}}, {"liga", {2}}}},
            {"kana", {{"calt", {1}}, {"ccmp", {0}}, {"liga", {2}}}},
            {"lao ", {{"calt", {1}}, {"ccmp", {0}}, {"liga", {2}}}},
            {"latn", {{"calt", {1}}, {"ccmp", {0}}, {"liga", {2}}}},
        },
        {
            {"ccmp", 2, 0x0000, -1, {mult_st.data}},
            {"calt", 3, 0x0000, -1, {alt_st.data}},
            {"liga", 4, 0x0008, -1, {lig_st.data}},
        });

    SfntByteBuf gdef;
    gdef.u32(0x00010000u);
    gdef.u16(12);
    gdef.u16(0);
    gdef.u16(0);
    gdef.u16(0);
    const auto gcd = build_classdef_fmt2({{10, 12, 3}, {16, 17, 3}, {24, 24, 3}, {49, 49, 2}});
    gdef.data.insert(gdef.data.end(), gcd.begin(), gcd.end());

    const auto sfnt_bytes = build_synthetic_sfnt(
        1000, 800, -200, glyphs, {{"GDEF", gdef.data}, {"GSUB", gsub}});
    TrueTypeFont synth;
    assert(synth.load_memory(sfnt_bytes));

    // GSUB Multiple 1 -> 3 expansion preserves source cluster: "aXb" -> [0, 1, 1, 1, 2]
    assert((clusters_of(synth.shape_glyphs({'a', 'X', 'b'})) ==
            std::vector<uint32_t>{0, 1, 1, 1, 2}));
    // GSUB Multiple 1 -> 0 deletion at middle, start (delete_glyph forward merge), and shared mark:
    assert((clusters_of(synth.shape_glyphs({'a', 'D', 'b'})) ==
            std::vector<uint32_t>{0, 2}));
    assert((clusters_of(synth.shape_glyphs({'D', 'a', 'X'})) ==
            std::vector<uint32_t>{0, 2, 2, 2}));
    assert((clusters_of(synth.shape_glyphs({'a', 'D', 0x0301, 'b'})) ==
            std::vector<uint32_t>{0, 1, 3}));
    // GSUB Alternate 1 -> 1: "aAb" -> [0, 1, 2]
    assert((clusters_of(synth.shape_glyphs({'a', 'A', 'b'})) ==
            std::vector<uint32_t>{0, 1, 2}));
    // GSUB Ligature across intervening mark + trailing mark on last component:
    // "f\u0300i\u0323x" (0:f, 1:0300, 2:i, 3:0323, 4:x) -> [0, 0, 0, 4]
    assert((clusters_of(synth.shape_glyphs({'f', 0x0300, 'i', 0x0323, 'x'})) ==
            std::vector<uint32_t>{0, 0, 0, 4}));

    // Grapheme continuations: Me enclosing circle, Emoji modifier, Regional Indicator pairs,
    // ZWJ + Extended_Pictographic vs ZWJ + ASCII, Katakana FF9E/FF9F, and Emoji tags:
    assert((clusters_of(synth.shape_glyphs({'a', 0x20DD, 'b'})) ==
            std::vector<uint32_t>{0, 0, 2}));
    assert((clusters_of(synth.shape_glyphs({0x1F44B, 0x1F3FD, 0x1F44B})) ==
            std::vector<uint32_t>{0, 0, 2}));
    assert((clusters_of(synth.shape_glyphs({0x1F1FA, 0x1F1F8, 0x1F1EC, 0x1F1E7, 0x1F1FA})) ==
            std::vector<uint32_t>{0, 0, 2, 2, 4}));
    assert((clusters_of(synth.shape_glyphs({0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467, 0x1F44B})) ==
            std::vector<uint32_t>{0, 0, 0, 0, 0, 5}));
    assert((clusters_of(synth.shape_glyphs({0x1F468, 0x200D, 'a'})) ==
            std::vector<uint32_t>{0, 0, 2}));
    assert((clusters_of(synth.shape_glyphs({0xFF76, 0xFF9E, 0xFF77, 0xFF9F})) ==
            std::vector<uint32_t>{0, 0, 2, 2}));
    assert((clusters_of(synth.shape_glyphs({0x1F3F4, 0xE0067, 0xE007F, 0x1F44B})) ==
            std::vector<uint32_t>{0, 0, 0, 3}));

    // Lao SARA AM backward base+tone cluster merge:
    assert((clusters_of(synth.shape_glyphs({0x0E99, 0x0EB3, 0x0E99, 0x0EC9, 0x0EB3})) ==
            std::vector<uint32_t>{0, 0, 0, 2, 2, 2, 2}));
    assert((clusters_of(synth.shape_glyphs({0x0EC9, 0x0EB3})) ==
            std::vector<uint32_t>{0, 0, 0}));

    // Hangul composition + trailing mark, uncomposable decomposition, standalone L+T,
    // and Round 2 mark swap (`\u0301\uac00\u302e` merges to 0; `\u0323\uac00\u302e` keeps [0, 1, 1]):
    assert((clusters_of(synth.shape_glyphs({0x1100, 0x1161, 0x0301})) ==
            std::vector<uint32_t>{0, 0}));
    assert((clusters_of(synth.shape_glyphs({0xAC00, 0x11A8, 0x0301})) ==
            std::vector<uint32_t>{0, 0}));
    assert((clusters_of(synth.shape_glyphs({0x1103, 0x1161, 0x11A8, 0x0301})) ==
            std::vector<uint32_t>{0, 0, 0, 0}));
    assert((clusters_of(synth.shape_glyphs({0x1100, 0x11A8})) ==
            std::vector<uint32_t>{0, 1}));
    assert((clusters_of(synth.shape_glyphs({0x0301, 0xAC00, 0x302E})) ==
            std::vector<uint32_t>{0, 0, 0}));
    assert((clusters_of(synth.shape_glyphs({0x0323, 0xAC00, 0x302E})) ==
            std::vector<uint32_t>{0, 1, 1}));

    // -------------------------------------------------------------------------
    // 2. Real system font (`DejaVuSans.ttf`) RTL Arabic/Hebrew & `FontRenderer::shape_text`
    //    UTF-8 byte-offset clusters across multi-byte UTF-8, fallback faces, and Bidi runs.
    // -------------------------------------------------------------------------
    TrueTypeFont dejavu;
    if (dejavu.load_file("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")) {
        // "office" -> o(0), ffi(1), c(4), e(5)
        assert((clusters_of(dejavu.shape_glyphs({'o', 'f', 'f', 'i', 'c', 'e'})) ==
                std::vector<uint32_t>{0, 1, 4, 5}));
        // ZWNJ keeps its own cluster (`[2, 1, 0]`) while ZWJ merges into preceding starter (`[2, 0, 0]`):
        assert((clusters_of(dejavu.shape_glyphs({0x0628, 0x200C, 0x0628}, true, true)) ==
                std::vector<uint32_t>{2, 1, 0}));
        assert((clusters_of(dejavu.shape_glyphs({0x0628, 0x200D, 0x0628}, true, true)) ==
                std::vector<uint32_t>{2, 0, 0}));
        // Arabic "السَّلَامُ" (10 codepoints, Lam+Fatha+Alef ligature at [5..7]) -> [8, 8, 5, 5, 2, 2, 1, 0]:
        assert((clusters_of(dejavu.shape_glyphs(
                    {0x0627, 0x0644, 0x0633, 0x064E, 0x0651, 0x0644, 0x064E, 0x0627, 0x0645, 0x064F},
                    true, true)) ==
                std::vector<uint32_t>{8, 8, 5, 5, 2, 2, 1, 0}));
        // Hebrew "שָׁלוֹם" (7 codepoints) -> [6, 4, 4, 3, 0, 0, 0]:
        assert((clusters_of(dejavu.shape_glyphs(
                    {0x05E9, 0x05B8, 0x05C1, 0x05DC, 0x05D5, 0x05B9, 0x05DD},
                    true, true)) ==
                std::vector<uint32_t>{6, 4, 4, 3, 0, 0, 0}));
    }

    FontRenderer& fr = FontRenderer::instance();
    // Multi-byte UTF-8: "Ελληνικά" (8 Greek 2-byte UTF-8 codepoints -> byte offsets 0,2,4,6,8,10,12,14)
    const auto greek_utf8 = fr.shape_text("Ελληνικά", 16.0f, 400, std::string_view("sans-serif"),
                                          false, true, false, false);
    assert((shaped_clusters_of(greek_utf8) ==
            std::vector<uint32_t>{0, 2, 4, 6, 8, 10, 12, 14}));

    // Multi-byte UTF-8 + ligature: "éffié" -> 'é'(2 bytes at 0), "ffi"(3 bytes at 2), 'é'(2 bytes at 5)
    const auto lig_utf8 = fr.shape_text("\xC3\xA9" "ffi" "\xC3\xA9", 16.0f, 400,
                                        std::string_view("sans-serif"), false, true, false, false);
    if (lig_utf8.size() == 3u) {
        assert((shaped_clusters_of(lig_utf8) == std::vector<uint32_t>{0, 2, 5}));
    }

    // Visual RTL UTF-8: "سلام" (س=0, ل=2, ا=4, م=6; Lam+Alef ligate at byte 2 -> visual RTL [6, 2, 0])
    const auto arab_utf8 = fr.shape_text("سلام", 16.0f, 400, "sans-serif",
                                         false, true, /*visual_rtl=*/true, /*pre_reversed=*/false);
    if (arab_utf8.size() == 3u) {
        assert((shaped_clusters_of(arab_utf8) == std::vector<uint32_t>{6, 2, 0}));
    }

    // Pre-reversed Bidi L2 visual run: visual string "مالس" (م=0, ا=2, ل=4, س=6)
    // Logical order is س(6), ل(4), ا(2), م(0); Lam(4)+Alef(2) ligate with logical cluster starter byte 4!
    const auto arab_prerev = fr.shape_text("مالس", 16.0f, 400, "sans-serif",
                                           false, true, /*visual_rtl=*/true, /*pre_reversed=*/true);
    if (arab_prerev.size() == 3u) {
        assert((shaped_clusters_of(arab_prerev) == std::vector<uint32_t>{0, 4, 6}));
    }

    // Multi-face fallback run: primary webfont covers only 'A' (1 byte), fallback system face covers
    // "é" (2 bytes at offset 1) + "א" (2 bytes at offset 3) or Greek "Ω" (2 bytes at offset 3),
    // then primary covers 'A' again at byte offset 5!
    std::vector<TestGlyphSpec> only_a_glyphs = {
        {0, 500, false, 0, 0, 0, 0},
        {'A', 600, true, 40, 0, 560, 700},
    };
    if (fr.register_font_file("tests/wpt/fonts/Ahem.ttf") != FontRenderer::kNoFamily) {
        const auto only_a_sfnt = build_synthetic_sfnt(1000, 800, -200, only_a_glyphs, {});
        assert(fr.register_webfont("Ahem", {[only_a_sfnt]() { return only_a_sfnt; }}) >= 0);
        // "A" (0) + "λ" (1..2) + "Ω" (3..4) + "A" (5)
        const auto fallback_run = fr.shape_text("A\xCE\xBB\xCE\xA9" "A", 16.0f, 400,
                                                std::string_view("Ahem"),
                                                false, true, false, false);
        assert(fallback_run.size() == 4u);
        assert(fallback_run[0].face != fallback_run[1].face);
        assert((shaped_clusters_of(fallback_run) == std::vector<uint32_t>{0, 1, 3, 5}));
        fr.clear_webfonts();
    }
}
