// Phase 442: Arabic modified combining classes, UTR #53 MCM ordering, and pre-mark decomposition.
TEST(arabic_mark_ordering_and_pre_mark_decomposition) {
    using namespace litebird;

    auto ids = [](const TrueTypeFont& f, const std::vector<uint32_t>& cps) {
        std::vector<uint16_t> out;
        for (const auto& g : f.shape_glyphs(cps, true, false)) out.push_back(g.glyph);
        return out;
    };

    // 1. Arabic modified CCC (shadda 33 -> 27 before fathatan..kasra 27..32 -> 28..33,
    //    sukun 34, superscript alef 35) + UTR #53 MCM promotion (CCC 220 -> 25, CCC 230 -> 26)
    //    executed BEFORE GSUB `ccmp` ligature substitution.
    {
        const std::vector<TestGlyphSpec> glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {0x0628, 600, true, 0, 0, 500, 420}, // 1: beh
            {0x064B, 0, true, 0, 500, 40, 560},  // 2: fathatan (CCC 27 -> 28)
            {0x064E, 0, true, 0, 500, 40, 560},  // 3: fatha    (CCC 30 -> 31)
            {0x0650, 0, true, 0, -180, 40, -80}, // 4: kasra    (CCC 32 -> 33)
            {0x0651, 0, true, 0, 500, 40, 560},  // 5: shadda   (CCC 33 -> 27)
            {0x0652, 0, true, 0, 500, 40, 560},  // 6: sukun    (CCC 34 -> 34)
            {0x0670, 0, true, 0, 500, 40, 620},  // 7: sup alef (CCC 35 -> 35)
            {0x0653, 0, true, 0, 500, 40, 600},  // 8: madda    (CCC 230, non-MCM)
            {0x0654, 0, true, 0, 500, 40, 600},  // 9: hamza above (CCC 230, MCM -> 26)
            {0x0655, 0, true, 0, -200, 40, -90}, // 10: hamza below (CCC 220, MCM -> 25)
            {0x0656, 0, true, 0, -200, 40, -90}, // 11: subscript alef (CCC 220, non-MCM)
            {0x0658, 0, true, 0, 500, 40, 600},  // 12: noon ghunna (CCC 230, MCM -> 26)
            {0x08D3, 0, true, 0, -200, 40, -90}, // 13: small low waw (CCC 220, MCM -> 25)
            {0x08CA, 0, true, 0, 500, 40, 600},  // 14: small high farsi yeh (CCC 230, MCM -> 26)
            {0xFC60, 0, true, 0, 520, 60, 650},  // 15: shadda+fatha ccmp ligature
        };

        // GSUB Lookup 0 (`ccmp` under `arab`): Type 4 LigatureSubst `5 (shadda) + 3 (fatha) -> 15`.
        SfntByteBuf lig_sub;
        auto cov = build_cov_fmt1({5});
        lig_sub.u16(1); // substFormat = 1
        lig_sub.u16(8); // coverageOffset = 8
        lig_sub.u16(1); // ligatureSetCount = 1
        lig_sub.u16(static_cast<uint16_t>(8u + cov.size()));
        lig_sub.data.insert(lig_sub.data.end(), cov.begin(), cov.end());
        lig_sub.u16(1);  // ligatureCount = 1
        lig_sub.u16(4);  // ligatureOffsets[0] = 4
        lig_sub.u16(15); // ligatureGlyph = 15
        lig_sub.u16(2);  // componentCount = 2
        lig_sub.u16(3);  // componentGlyphIDs[0] = 3 (fatha)

        auto gsub = build_otl_table(
            {{"arab", {{"ccmp", {0}}}}},
            {{"ccmp", 4, 0, -1, {lig_sub.data}}});
        TrueTypeFont f;
        assert(f.load_memory(build_synthetic_sfnt(1000, 800, -200, glyphs, {{"GSUB", gsub}})));

        // Shadda moves ahead of fatha so `ccmp` ligates `5 + 3 -> 15`.
        assert((ids(f, {0x0628, 0x064E, 0x0651}) == std::vector<uint16_t>{1, 15}));
        assert((ids(f, {0x0628, 0x0651, 0x064E}) == std::vector<uint16_t>{1, 15}));
        // Fathatan (28) sorts between shadda (27) and fatha (31), separating `5` from `3`.
        assert((ids(f, {0x0628, 0x064E, 0x064B, 0x0651}) == std::vector<uint16_t>{1, 5, 2, 3}));
        // Full modified-CCC ladder: 220-MCM (25), 230-MCM (26), shadda (27), fathatan (28),
        // kasra (33), sukun (34), superscript alef (35), non-MCM 220, non-MCM 230.
        assert((ids(f, {0x0628, 0x0653, 0x0656, 0x0670, 0x0652, 0x0650, 0x064B, 0x0651}) ==
                std::vector<uint16_t>{1, 5, 2, 4, 6, 7, 11, 8}));
        // MCM promotion across vowels & shadda, preserving relative order among equal-CCC MCMs.
        assert((ids(f, {0x0628, 0x064E, 0x0654, 0x0658, 0x0655, 0x08D3, 0x0651}) ==
                std::vector<uint16_t>{1, 10, 13, 9, 12, 15}));
        assert((ids(f, {0x0628, 0x0650, 0x08CA}) == std::vector<uint16_t>{1, 14, 4}));
        // Non-MCM at CCC 220 (0x0656) or CCC 230 (0x0653) blocks a following equal-CCC MCM from promoting.
        assert((ids(f, {0x0628, 0x0650, 0x0656, 0x0655}) == std::vector<uint16_t>{1, 4, 11, 10}));
        assert((ids(f, {0x0628, 0x0650, 0x0653, 0x0654}) == std::vector<uint16_t>{1, 4, 8, 9}));
    }

    // 2. Canonical composition after Arabic mark ordering and pre-mark decomposition,
    //    plus coverage rollback when a decomposition part or target composite is absent.
    auto make_alef_font = [](bool madda_mark_covered, bool alef_hamza_below_covered) {
        std::vector<TestGlyphSpec> glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {0x0627, 600, true, 0, 0, 500, 500},  // 1: alef
            {0x064A, 600, true, 0, 0, 500, 500},  // 2: yeh
            {0x064E, 0, true, 0, 500, 40, 560},   // 3: fatha
            {0x0651, 0, true, 0, 500, 40, 560},   // 4: shadda
            {0x0654, 0, true, 0, 500, 40, 600},   // 5: hamza above (MCM 230 -> 26)
            {0x0655, 0, true, 0, -200, 40, -90},  // 6: hamza below (MCM 220 -> 25)
            {0x0622, 620, true, 0, 0, 500, 700},  // 7: alef with madda above
            {0x0623, 620, true, 0, 0, 500, 700},  // 8: alef with hamza above
            {0x0626, 620, true, 0, 0, 500, 700},  // 9: yeh with hamza above
            {' ', 200, false, 0, 0, 0, 0},        // 10: space
            {0x200C, 0, false, 0, 0, 0, 0},       // 11: ZWNJ
            {0x00E9, 600, true, 0, 0, 500, 700},  // 12: Latin 'é'
            {'e', 550, true, 0, 0, 500, 500},     // 13: Latin 'e'
            {0x0301, 0, true, 0, 500, 40, 600},   // 14: combining acute
        };
        if (madda_mark_covered) glyphs.push_back({0x0653, 0, true, 0, 500, 40, 600});       // 15
        if (alef_hamza_below_covered) glyphs.push_back({0x0625, 620, true, 0, -200, 500, 500}); // 16 (or 15)
        TrueTypeFont f;
        assert(f.load_memory(build_synthetic_sfnt(1000, 800, -200, glyphs, {})));
        return f;
    };

    const auto full = make_alef_font(true, true);
    // Decomposed `alef + madda/hamza_above + hamza_below` promotes U+0655 first and composes U+0625 (gid 16).
    assert((ids(full, {0x0627, 0x0653, 0x0655}) == std::vector<uint16_t>{16, 15}));
    assert((ids(full, {0x0627, 0x0654, 0x0655}) == std::vector<uint16_t>{16, 5}));
    // Precomposed `U+0622 + U+0655` and `U+0623 + U+0655` decompose before the mark and match decomposed input.
    assert((ids(full, {0x0622, 0x0655}) == std::vector<uint16_t>{16, 15}));
    assert((ids(full, {0x0623, 0x0655}) == std::vector<uint16_t>{16, 5}));
    assert((ids(full, {0x0622, 0x064E, 0x0655}) == std::vector<uint16_t>{16, 3, 15}));
    // Non-MCM U+0653 stays ahead of U+0654, composing U+0622 (gid 7) rather than U+0623.
    assert((ids(full, {0x0627, 0x0653, 0x0654}) == std::vector<uint16_t>{7, 5}));
    // Unconsumed 220-MCM (effective class 25) does not block 230-MCM (effective class 26) on U+064A -> U+0626 (gid 9).
    assert((ids(full, {0x064A, 0x0655, 0x064E, 0x0654}) == std::vector<uint16_t>{9, 6, 3}));
    assert((ids(full, {0x0626, 0x0655, 0x064E}) == std::vector<uint16_t>{9, 6, 3}));
    // Standalone supported composites stay intact; class-zero and script boundaries block decomposition and ordering.
    assert((ids(full, {0x0622}) == std::vector<uint16_t>{7}));
    assert((ids(full, {0x0622, ' ', 0x0655}) == std::vector<uint16_t>{7, 10, 6}));
    assert((ids(full, {0x0622, 0x200C, 0x0655}) == std::vector<uint16_t>{7, 10, 6}));
    assert((ids(full, {0x00E9, 0x064E}) == std::vector<uint16_t>{12, 3}));
    // Leading Arabic mark spans and marks following spaces order without a starter; input vector stays immutable.
    assert((ids(full, {0x064E, 0x0651}) == std::vector<uint16_t>{4, 3}));
    assert((ids(full, {' ', 0x064E, 0x0651}) == std::vector<uint16_t>{10, 4, 3}));
    const std::vector<uint32_t> original{0x0622, 0x064E, 0x0655};
    (void)ids(full, original);
    assert((original == std::vector<uint32_t>{0x0622, 0x064E, 0x0655}));

    // Rollback when U+0653 is missing from the face: U+0622 must not decompose into `.notdef`.
    const auto no_madda_mark = make_alef_font(false, true);
    assert((ids(no_madda_mark, {0x0622, 0x0655}) == std::vector<uint16_t>{7, 6}));

    // Missing standalone Arabic composite U+0625 decomposes into covered `[0x0627, 0x0655]` (`{1, 6}`).
    const auto no_alef_hamza_below = make_alef_font(true, false);
    assert((ids(no_alef_hamza_below, {0x0625}) == std::vector<uint16_t>{1, 6}));
}
