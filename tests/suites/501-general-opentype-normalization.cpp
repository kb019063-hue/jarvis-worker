// Phase 501 (finding 501): General OpenType normalization (`_hb_ot_shape_normalize`)
// across scripts (`Grek`, `Cyrl`, `Kana`, `Thai`, `Lao`, `Common`, etc.),
// decomposable combining marks in multi-char clusters, and 1-step alternate
// partial canonical decomposition (`shortest = true` and `shortest = false`).

TEST(general_opentype_normalization_and_partial_decomposition) {
    using namespace litebird;

    auto glyph_ids = [](const TrueTypeFont& f, const std::vector<uint32_t>& cps, bool rtl = false) {
        std::vector<uint16_t> out;
        for (const auto& g : f.shape_glyphs(cps, true, rtl)) out.push_back(g.glyph);
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
    // 1. Alternate partial canonical decomposition (`shortest = true` & `false`)
    //    and 1-step singleton decompositions (`U+212B`, `U+1F71`, `U+1FD3`).
    // -------------------------------------------------------------------------
    {
        // Font covers 'a'(2), U+00E5 'å'(3), U+0301 ACUTE(4), U+0323 DOT BELOW(5),
        // U+00C5 'Å'(6), U+03AC 'ά'(7), U+0390 'ΐ'(8), U+1F04 'ἄ'(9), U+0345 YPOG(10),
        // while MISSING leaf marks U+030A RING ABOVE, U+0313 PSILI, U+0308 DIALYTIKA,
        // and MISSING top composites U+01FB 'ǻ', U+1F84 'ᾄ', U+212B, U+1F71, U+1FD3!
        const std::vector<TestGlyphSpec> glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {' ', 250, false, 0, 0, 0, 0},             // 1
            {'a', 600, true, 40, 0, 560, 500},         // 2
            {0x00E5, 620, true, 40, 0, 560, 720},      // 3: å (a + 030A)
            {0x0301, 0, true, 100, 540, 220, 680},     // 4: ACUTE (CCC 230)
            {0x0323, 0, true, 100, -180, 220, -60},    // 5: DOT BELOW (CCC 220)
            {0x00C5, 700, true, 40, 0, 660, 780},      // 6: Å (target of U+212B)
            {0x03AC, 640, true, 40, 0, 600, 700},      // 7: ά (target of U+1F71)
            {0x0390, 420, true, 40, 0, 380, 740},      // 8: ΐ (target of U+1FD3)
            {0x1F04, 660, true, 40, 0, 620, 760},      // 9: ἄ (α + 0313 + 0301)
            {0x0345, 0, true, 100, -200, 220, -80},    // 10: YPOGEGRAMMENI (CCC 240)
        };
        TrueTypeFont f_partial;
        assert(f_partial.load_memory(build_synthetic_sfnt(1000, 800, -200, glyphs, {})));

        // Standalone missing U+01FB ('ǻ') uses 1-step [U+00E5 ('å'), U+0301] even though U+030A is absent!
        assert((glyph_ids(f_partial, {0x01FB}) == std::vector<uint16_t>{3, 4}));
        // Non-simple cluster {U+01FB, U+0323}: full recursion on U+00E5 fails (U+030A absent),
        // falls back to 1-step [U+00E5, U+0301], then reorders U+0323 (220) before U+0301 (230)!
        assert((glyph_ids(f_partial, {0x01FB, 0x0323}) == std::vector<uint16_t>{3, 5, 4}));

        // 4-leaf Greek U+1F84 ('ᾄ' = α + 0313 + 0301 + 0345) with U+0313 absent:
        // standalone decomposes 1-step to [U+1F04 ('ἄ'), U+0345], and before U+0323 (220)
        // reorders U+0323 before U+0345 (240)!
        assert((glyph_ids(f_partial, {0x1F84}) == std::vector<uint16_t>{9, 10}));
        assert((glyph_ids(f_partial, {0x1F84, 0x0323}) == std::vector<uint16_t>{9, 5, 10}));

        // Singleton 1-step canonical decompositions (`b == 0`) resolve to 1 composite glyph
        // even when leaf marks (U+030A, U+0308) are absent!
        assert((glyph_ids(f_partial, {0x212B}) == std::vector<uint16_t>{6})); // ANGSTROM -> Å
        assert((glyph_ids(f_partial, {0x1F71}) == std::vector<uint16_t>{7})); // ALPHA WITH OXIA -> ά
        assert((glyph_ids(f_partial, {0x1FD3}) == std::vector<uint16_t>{8})); // IOTA DIALYTIKA OXIA -> ΐ

        // Even when leaf bases + marks ARE covered (`'A'` + `U+030A`, `'α'` + `U+0301`),
        // `shortest = true` keeps standalone U+212B / U+1F71 as the 1-step composite `Å` / `ά`!
        auto glyphs_with_leaves = glyphs;
        glyphs_with_leaves.push_back({'A', 680, true, 40, 0, 640, 700});       // 11
        glyphs_with_leaves.push_back({0x030A, 0, true, 100, 540, 220, 680});   // 12
        glyphs_with_leaves.push_back({0x03B1, 620, true, 40, 0, 580, 500});    // 13
        TrueTypeFont f_leaves;
        assert(f_leaves.load_memory(build_synthetic_sfnt(1000, 800, -200, glyphs_with_leaves, {})));
        assert((glyph_ids(f_leaves, {0x212B}) == std::vector<uint16_t>{6}));
        assert((glyph_ids(f_leaves, {0x1F71}) == std::vector<uint16_t>{7}));
    }

    // -------------------------------------------------------------------------
    // 2. General normalization across Greek, Cyrillic, Kana, Thai, Lao, Common,
    //    plus decomposable marks (`U+0341`, `U+0343`, `U+0344`) and boundaries.
    // -------------------------------------------------------------------------
    {
        const std::vector<TestGlyphSpec> glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {' ', 250, false, 0, 0, 0, 0},             // 1: space (also CGJ hidden slot)
            {'a', 600, true, 40, 0, 560, 500},         // 2
            {0x00E1, 610, true, 40, 0, 560, 700},      // 3: á
            {0x03B1, 620, true, 40, 0, 580, 500},      // 4: α
            {0x03AC, 630, true, 40, 0, 590, 700},      // 5: ά (α + 0301)
            {0x1F00, 640, true, 40, 0, 600, 700},      // 6: ἀ (α + 0313)
            {0x1F04, 650, true, 40, 0, 610, 740},      // 7: ἄ (ἀ + 0301)
            {0x1F80, 660, true, 40, 0, 620, 760},      // 8: ᾀ (ἀ + 0345)
            {0x1F84, 670, true, 40, 0, 630, 780},      // 9: ᾄ (ἄ + 0345)
            {0x03B9, 360, true, 40, 0, 320, 500},      // 10: ι
            {0x03CA, 380, true, 40, 0, 340, 680},      // 11: ϊ (ι + 0308)
            {0x0390, 400, true, 40, 0, 360, 740},      // 12: ΐ (ϊ + 0301)
            {0x0435, 600, true, 40, 0, 560, 500},      // 13: е (Cyrillic ie)
            {0x0451, 615, true, 40, 0, 575, 700},      // 14: ё (е + 0308)
            {0x0456, 350, true, 40, 0, 310, 500},      // 15: і (Cyrillic byelorussian-ukrainian i)
            {0x0457, 370, true, 40, 0, 330, 700},      // 16: ї (і + 0308)
            {0x304B, 900, true, 40, 0, 860, 800},      // 17: か (Hiragana ka)
            {0x304C, 900, true, 40, 0, 860, 820},      // 18: が (か + 3099)
            {0x30CF, 900, true, 40, 0, 860, 800},      // 19: ハ (Katakana ha; パ unmapped!)
            {'=', 600, true, 40, 150, 560, 450},       // 20: '='
            {0x2260, 600, true, 40, 50, 560, 650},     // 21: '≠' ('=' + 0338)
            {0x0E01, 650, true, 40, 0, 610, 700},      // 22: Thai KO KAI
            {0x0E38, 0, true, 100, -160, 220, -40},    // 23: Thai SARA U (CCC 103 -> mod 3)
            {0x0E3A, 0, true, 100, -220, 220, -100},   // 24: Thai PHINTHU (CCC 9)
            {0x0E81, 650, true, 40, 0, 610, 700},      // 25: Lao KO
            {0x0EB8, 0, true, 100, -160, 220, -40},    // 26: Lao VOWEL SIGN U (CCC 118)
            {0x0EC8, 0, true, 100, 560, 220, 680},     // 27: Lao MAI EK (CCC 122)
            {0x0301, 0, true, 100, 540, 220, 680},     // 28: COMBINING ACUTE (CCC 230)
            {0x0308, 0, true, 100, 540, 220, 680},     // 29: COMBINING DIAERESIS (CCC 230)
            {0x0313, 0, true, 100, 540, 220, 680},     // 30: COMBINING COMMA ABOVE / PSILI (CCC 230)
            {0x0323, 0, true, 100, -180, 220, -60},    // 31: COMBINING DOT BELOW (CCC 220)
            {0x0338, 0, true, 80, 40, 520, 660},       // 32: COMBINING LONG SOLIDUS OVERLAY (CCC 1)
            {0x0341, 0, true, 100, 540, 220, 680},     // 33: COMBINING ACUTE TONE MARK (-> 0301)
            {0x0343, 0, true, 100, 540, 220, 680},     // 34: COMBINING GREEK KORONIS (-> 0313)
            {0x0344, 0, true, 100, 540, 220, 740},     // 35: COMBINING GREEK DIALYTIKA TONOS (-> 0308 0301)
            {0x0345, 0, true, 100, -200, 220, -80},    // 36: COMBINING GREEK YPOGEGRAMMENI (CCC 240)
            {0x3099, 0, true, 700, 620, 860, 780},     // 37: COMBINING VOICED SOUND MARK (CCC 8)
            {0x309A, 0, true, 700, 620, 860, 780},     // 38: COMBINING SEMI-VOICED SOUND MARK (CCC 8)
            {0x05B8, 0, true, 100, -180, 220, -60},    // 39: HEBREW POINT QAMATS (cross-script control)
        };
        TrueTypeFont f;
        assert(f.load_memory(build_synthetic_sfnt(1000, 800, -200, glyphs, {})));

        // Greek polytonic multi-step composition, reordering, and equal-CCC blocking:
        assert((glyph_ids(f, {0x03B1, 0x0301}) == std::vector<uint16_t>{5}));
        assert((glyph_ids(f, {0x03B1, 0x0313, 0x0301}) == std::vector<uint16_t>{7}));
        assert((glyph_ids(f, {0x03B1, 0x0301, 0x0313}) == std::vector<uint16_t>{5, 30})); // equal CCC 230 blocks!
        assert((glyph_ids(f, {0x03B1, 0x0345, 0x0313, 0x0301}) == std::vector<uint16_t>{9}));
        assert((glyph_ids(f, {0x1F04, 0x0345}) == std::vector<uint16_t>{9}));
        assert((glyph_ids(f, {0x03AC, 0x0323}) == std::vector<uint16_t>{5, 31}));
        assert(same_run(f.shape_glyphs({0x03AC, 0x0323}), f.shape_glyphs({0x03B1, 0x0301, 0x0323})));
        assert(same_run(f.shape_glyphs({0x03AC, 0x0323}), f.shape_glyphs({0x03B1, 0x0323, 0x0301})));
        assert((glyph_ids(f, {0x03B1, 0x0313, 0x034F, 0x0301}) == std::vector<uint16_t>{6, 1, 28}));

        // Decomposable marks in multi-char clusters vs standalone 1-char simple clusters:
        assert((glyph_ids(f, {0x03B9, 0x0344}) == std::vector<uint16_t>{12}));           // ι + 0344 -> ΐ
        assert((glyph_ids(f, {0x03B1, 0x0343, 0x0341}) == std::vector<uint16_t>{7}));    // α + 0343 + 0341 -> ἄ
        assert((glyph_ids(f, {'a', 0x0341}) == std::vector<uint16_t>{3}));               // a + 0341 -> á
        assert((glyph_ids(f, {' ', 0x0341}) == std::vector<uint16_t>{1, 28}));           // space + 0341 -> space + 0301
        assert((glyph_ids(f, {0x0341, 0x0323}) == std::vector<uint16_t>{31, 28}));       // 0341 + 0323 -> [0323, 0301]
        assert((glyph_ids(f, {0x0341}) == std::vector<uint16_t>{33}));                   // 1-char standalone stays 0341!
        assert((glyph_ids(f, {0x0344}) == std::vector<uint16_t>{35}));                   // 1-char standalone stays 0344!

        // Cyrillic composition, pre-mark decomposition, equal-class blocking, CGJ, and missing composite:
        assert((glyph_ids(f, {0x0435, 0x0308}) == std::vector<uint16_t>{14}));           // е + 0308 -> ё
        assert((glyph_ids(f, {0x0456, 0x0308}) == std::vector<uint16_t>{16}));           // і + 0308 -> ї
        assert((glyph_ids(f, {0x0451, 0x0323}) == std::vector<uint16_t>{14, 31}));
        assert(same_run(f.shape_glyphs({0x0451, 0x0323}), f.shape_glyphs({0x0435, 0x0308, 0x0323})));
        assert(same_run(f.shape_glyphs({0x0451, 0x0323}), f.shape_glyphs({0x0435, 0x0323, 0x0308})));
        assert((glyph_ids(f, {0x0435, 0x0301, 0x0308}) == std::vector<uint16_t>{13, 28, 29}));
        assert((glyph_ids(f, {0x0435, 0x034F, 0x0308}) == std::vector<uint16_t>{13, 1, 29}));
        // Missing U+0450 ('ѐ' = е + 0300) stays .notdef when U+0300 is absent, while missing
        // Katakana U+30D1 ('パ' = ハ + 309A) decomposes to covered [ハ(19), 309A(38)]!
        assert((glyph_ids(f, {0x0450}) == std::vector<uint16_t>{0}));
        assert((glyph_ids(f, {0x304B, 0x3099}) == std::vector<uint16_t>{18}));           // か + 3099 -> が
        assert((glyph_ids(f, {0x30D1}) == std::vector<uint16_t>{19, 38}));               // missing パ -> [ハ, 309A]

        // Common math overlay composition ('=' + U+0338 -> '≠'):
        assert((glyph_ids(f, {'=', 0x0338}) == std::vector<uint16_t>{21}));

        // Thai modified CCC (`U+0E38` CCC 103 -> 3 precedes `U+0E3A` CCC 9) & Lao CCC 118 < 122:
        assert((glyph_ids(f, {0x0E01, 0x0E3A, 0x0E38}) == std::vector<uint16_t>{22, 23, 24}));
        assert((glyph_ids(f, {0x0E81, 0x0EC8, 0x0EB8}) == std::vector<uint16_t>{25, 26, 27}));

        // Cross-script boundary control: 'á'(Latn) + Qamats(Hebr) does NOT decompose 'á'!
        assert((glyph_ids(f, {0x00E1, 0x05B8}) == std::vector<uint16_t>{3, 39}));
    }

    // -------------------------------------------------------------------------
    // 3. Host DejaVuSans / DejaVuSerif HarfBuzz 12.1.0 agreement + HTML pixel paint.
    // -------------------------------------------------------------------------
    FontRenderer& fr = FontRenderer::instance();
    const std::string sans_path = fr.face_path("DejaVu Sans");
    if (!sans_path.empty()) {
        TrueTypeFont sans;
        if (sans.load_file(sans_path) && sans.glyph_index(0x1F84) != 0 &&
            sans.glyph_index(0x0451) != 0 && sans.glyph_index(0x2260) != 0) {
            assert(same_run(sans.shape_glyphs({0x03B1, 0x0313, 0x0301, 0x0345}),
                            sans.shape_glyphs({0x1F84})));
            assert(same_run(sans.shape_glyphs({0x03B9, 0x0344}),
                            sans.shape_glyphs({0x0390})));
            assert(same_run(sans.shape_glyphs({0x0435, 0x0308, 0x0323}),
                            sans.shape_glyphs({0x0451, 0x0323})));
            assert(same_run(sans.shape_glyphs({'=', 0x0338}),
                            sans.shape_glyphs({0x2260})));
        }
    }
    const std::string serif_path = fr.face_path("DejaVu Serif");
    if (!serif_path.empty()) {
        TrueTypeFont serif;
        if (serif.load_file(serif_path) && serif.glyph_index(0x1F80) != 0 &&
            serif.glyph_index(0x0345) == 0) {
            // DejaVuSerif lacks U+0345 COMBINING GREEK YPOGEGRAMMENI, so before Phase 501
            // {α, 0345, 0313} and {ἄ, 0345} emitted `.notdef` (gid 0)! Now they reorder
            // and compose to covered U+1F80 ('ᾀ') and U+1F84 ('ᾄ')!
            const auto r1 = serif.shape_glyphs({0x03B1, 0x0345, 0x0313});
            assert(r1.size() == 1 && r1[0].glyph == serif.glyph_index(0x1F80));
            const auto r2 = serif.shape_glyphs({0x1F04, 0x0345});
            assert(r2.size() == 1 && r2[0].glyph == serif.glyph_index(0x1F84));
        }
    }

    // End-to-end HTML layout & paint check using a registered webfont so the
    // pixel oracle is deterministic even without host fonts.
    {
        const std::vector<TestGlyphSpec> web_glyphs = {
            {0, 500, false, 0, 0, 0, 0},
            {' ', 250, false, 0, 0, 0, 0},
            {0x03B1, 600, true, 50, 0, 550, 480},
            {0x0313, 0, true, 180, 540, 300, 680},
            {0x0301, 0, true, 320, 540, 440, 680},
            {0x1F00, 610, true, 50, 0, 560, 700},
            {0x1F04, 620, true, 50, 0, 570, 760},
            {0x0435, 600, true, 50, 0, 550, 480},
            {0x0308, 0, true, 160, 540, 440, 680},
            {0x0451, 610, true, 50, 0, 560, 740},
        };
        const auto sfnt_bytes = build_synthetic_sfnt(1000, 800, -200, web_glyphs, {});
        if (fr.register_font_file("tests/wpt/fonts/Ahem.ttf") != FontRenderer::kNoFamily) {
            assert(fr.register_webfont("Ahem", {[sfnt_bytes]() { return sfnt_bytes; }},
                                       400, 400, false) >= 0);

            auto render_doc = [&](const char* body_text) {
                const std::string html = std::string(
                    "<!doctype html><html><body style=\"margin:0;background:#ffffff\">"
                    "<div style=\"font:48px 'Ahem';color:#000000;padding:8px\">") +
                    body_text + "</div></body></html>";
                auto doc = HtmlParser::parse(html);
                StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 260.0f, 96.0f);
                auto root = LayoutTree::build(doc, resolver, 260.0f, 96.0f);
                assert(root);
                const DisplayList dl = DisplayList::build(root);
                BitmapRenderer bmp(260, 96);
                bmp.clear(Color::white());
                bmp.render(dl);
                return bmp;
            };

            const BitmapRenderer bmp_decomp = render_doc("&#x03b1;&#x0313;&#x0301; &#x0435;&#x0308;");
            const BitmapRenderer bmp_precomp = render_doc("&#x1f04; &#x0451;");
            int ink_pixels = 0;
            for (int y = 0; y < 96; ++y) {
                for (int x = 0; x < 260; ++x) {
                    const Color p1 = bmp_decomp.get_pixel(x, y);
                    const Color p2 = bmp_precomp.get_pixel(x, y);
                    assert(p1 == p2);
                    if (!(p1 == Color::white())) ++ink_pixels;
                }
            }
            assert(ink_pixels > 50);
            fr.clear_webfonts();
        }
    }
}
