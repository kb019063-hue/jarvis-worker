// Phase 267, finding 271: a default-ignorable is an invisible glyph, not an
// absent one.
//
// `shape_glyphs` used to erase `U+200C`/`U+200D` from the shaped run once GSUB
// had used them. Two things were wrong with that. The count came out short for
// any text holding a joiner, and — worse — `mark_base` is an *index into the
// run*, so removing an element mid-run slid every later mark onto the base
// before its own, silently misplacing it.
//
// OpenType makes a default-ignorable an **invisible glyph**: it keeps its slot,
// paints nothing, advances nothing. That is what HarfBuzz emits, and the
// arabic-zwnj / arabic-zwj cases of `make shaping-oracle` are the differential
// that holds it (both were `mismatch` before this phase and `match` after).
//
// A joiner must still *do its job*, which is the half that would break if the
// glyph were simply kept with its own advance: ZWNJ prevents a Lam-Alef
// ligature, and ZWJ still joins.
TEST(a_joiner_keeps_its_slot) {
    using namespace litebird;
    FontRenderer& fr = FontRenderer::instance();
    const std::string path = fr.face_path("DejaVu Sans");
    if (path.empty()) {
        std::printf("      [joiner] no DejaVu Sans available; skipped\n");
        return;
    }
    TrueTypeFont font;
    if (!font.load_file(path)) {
        std::printf("      [joiner] DejaVu Sans would not load; skipped\n");
        return;
    }
    const uint16_t lam = font.glyph_index(0x0644);
    const uint16_t alef = font.glyph_index(0x0627);
    const uint16_t meem = font.glyph_index(0x0645);
    const uint16_t space = font.glyph_index(0x0020);
    if (lam == 0 || alef == 0 || meem == 0 || space == 0) {
        std::printf("      [joiner] DejaVu Sans lacks an Arabic glyph; skipped\n");
        return;
    }

    // ZWNJ between Lam and Alef: three slots, the middle one inert, and no
    // Lam-Alef ligature (the two letters keep their own isolated forms).
    const auto zwnj = font.shape_glyphs({0x0644, 0x200C, 0x0627}, false, false);
    assert(zwnj.size() == 3);
    assert(zwnj[0].glyph == lam);
    assert(zwnj[2].glyph == alef);
    // The invisible glyph: the space glyph, nothing painted, nothing advanced.
    assert(zwnj[1].glyph == space);
    assert(zwnj[1].x_advance == 0 && zwnj[1].y_advance == 0);
    assert(zwnj[1].x_offset == 0 && zwnj[1].y_offset == 0);

    // Order is unchanged: the pen reaches Alef after the same advance a lone
    // Lam would have used, so the joiner took no room.
    const auto lam_only = font.shape_glyphs({0x0644, 0x0627}, false, false);
    assert(lam_only.size() == 1);  // a real Lam-Alef ligature
    int32_t pen_zwnj = 0;
    for (const auto& g : zwnj) pen_zwnj += g.x_advance;
    int32_t pen_ligature = 0;
    for (const auto& g : lam_only) pen_ligature += g.x_advance;
    assert(pen_zwnj >= pen_ligature);

    // ZWJ at the end and at the start: still two slots, still inert.
    const auto zwj_after = font.shape_glyphs({0x0645, 0x200D}, false, false);
    assert(zwj_after.size() == 2);
    assert(zwj_after[0].glyph != meem);  // joined, so not the isolated form
    assert(zwj_after[1].glyph == space && zwj_after[1].x_advance == 0);

    const auto zwj_before = font.shape_glyphs({0x200D, 0x0645}, false, false);
    assert(zwj_before.size() == 2);
    assert(zwj_before[1].glyph != meem);
    assert(zwj_before[0].glyph == space && zwj_before[0].x_advance == 0);

    // A mark after a joiner still lands on the letter, not two slots back.
    // Beh + Fatha + ZWNJ: the Fatha's base is the Beh.
    const auto beh = font.glyph_index(0x0628);
    const auto fatha = font.glyph_index(0x064E);
    const auto marked = font.shape_glyphs({0x0628, 0x200C, 0x064E}, false, false);
    assert(marked.size() == 3);
    assert(marked[0].glyph == beh);
    assert(marked[1].glyph == space && marked[1].x_advance == 0);
    assert(marked[2].glyph == fatha);
    assert(marked[2].x_advance == 0);
}
