// Phase 217, finding 221: every text-decoration line is painted, at the
// face's own ink. `line-through` and `overline` were parsed, carried on
// every InlineFragment and reported by getComputedStyle, and drawn by nothing
// (finding 77, open since Phase 71).
TEST(every_text_decoration_line_is_painted_at_the_faces_own_ink) {
    using namespace litebird;

    // `text-decoration` has three lines and the painter drew one of them: the
    // underline. `line-through` — after the link underline the most common
    // decoration there is — and `overline` were parsed by the resolver and
    // reported correctly by `getComputedStyle`, and then dropped at the last
    // step, in the inline branch that asked only `fs.has_underline()`
    // (finding 77 has listed this as open since Phase 71). The information
    // reached the painter; the ink is the only place the loss showed.
    auto doc = HtmlParser::parse(
        "<body style=\"margin:0;background:#ffffff;font-size:40px;font-family:serif\">"
        "<div id=u style=\"text-decoration:underline\">MM</div>"
        "<div id=l style=\"text-decoration:line-through\">MM</div>"
        "<div id=o style=\"text-decoration:overline\">MM</div>"
        "<div id=n style=\"text-decoration:none\">MM</div>"
        "</body>");
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 400.0f, 400.0f);
    assert(root);

    // The lines the painter is told to draw, one box at a time. The list is
    // held in a named variable: iterating `DisplayList::build(root).commands()`
    // directly reads the temporary's vector after its owner has gone
    // (AddressSanitizer's stack-use-after-scope in the sanitizer job).
    const DisplayList list = DisplayList::build(root);
    std::vector<Rect> lines;
    for (const auto& cmd : list.commands()) {
        std::visit([&](auto&& a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, DrawRectCommand>) lines.push_back(a.rect);
        }, cmd);
    }
    // One line for `underline`, one for `line-through`, one for `overline`,
    // and none for `none`.
    assert(lines.size() == 3);

    auto box_of = [&](const char* id) {
        auto el = doc->get_element_by_id(id);
        assert(el && el->has_layout_box());
        return el->bounding_box();
    };
    auto line_in = [&](const Rect& box) -> std::optional<Rect> {
        for (const auto& r : lines) {
            if (r.y >= box.y - 1.0f && r.y <= box.y + box.height + 1.0f && r.x >= box.x - 1.0f)
                return r;
        }
        return std::nullopt;
    };
    const Rect u_box = box_of("u"), l_box = box_of("l"), o_box = box_of("o");
    const Rect none_box = box_of("n");

    // The face's baseline inside the line box, the same arithmetic
    // `FontRenderer::draw_text` does, so these are the y the text uses.
    FontRenderer& fonts = FontRenderer::instance();
    const float fs = 40.0f;
    const auto [ascent, descent] = fonts.em_ascent_descent(fs, 400, "serif", false);
    const float leading = std::max(0.0f, fs * 1.25f - (ascent + descent));
    const float baseline = ascent + leading * 0.5f;

    const auto u = line_in(u_box);
    assert(u.has_value());
    // Below the baseline, above the bottom of the glyphs.
    assert(u->y > u_box.y + baseline);
    assert(u->y < u_box.y + baseline + descent);
    // The face's ink: `line-through` sits on the x-height, `overline` on the
    // capitals. Both must be *inside* the text's own ink vertically, which is
    // what a fraction of the line box could not promise.
    const auto l = line_in(l_box);
    const auto o = line_in(o_box);
    assert(l.has_value() && o.has_value());
    // Measured within its own box: each of these boxes starts a new line, so
    // the absolute y only orders them by box, not by line.
    const float l_y = l->y - l_box.y;
    const float o_y = o->y - o_box.y;
    assert(l_y > 0.0f && l_y < baseline);      // above the baseline, inside the ink
    assert(o_y > 0.0f && o_y < l_y);           // overline is above line-through
    // Both are on the face's ink, not on a fraction of the line box: the
    // x-height line sits inside the text's own ink, and the capital line is
    // above everything the glyphs draw.
    const auto [x_height, cap_height] = fonts.decoration_lines(fs, 400, "serif", false);
    assert(std::fabs(l_y - (baseline - x_height)) < 0.01f);
    assert(std::fabs(o_y - (baseline - cap_height)) < 0.01f);
    assert(o_y < l_y);
    // `text-decoration: none` paints no line at all.
    assert(!line_in(none_box).has_value());

    // A stand-alone text node (no inline formatting context) used to get no
    // decoration either; it is one line and its decoration is that line's.
    auto bare = HtmlParser::parse(
        "<body style=\"margin:0;font-size:20px;text-decoration:line-through\">X</body>");
    StyleResolver bare_resolver;
    auto bare_root = LayoutTree::build(bare, bare_resolver, 200.0f, 100.0f);
    assert(bare_root);
    int bare_lines = 0;
    const DisplayList bare_list = DisplayList::build(bare_root);
    for (const auto& cmd : bare_list.commands()) {
        std::visit([&](auto&& a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, DrawRectCommand>) ++bare_lines;
        }, cmd);
    }
    assert(bare_lines == 1);

    std::printf("      [text-decoration] underline, line-through and overline each painted\n");
    std::fflush(stdout);
}
