// Phase 512, finding 512: the styles CSS accepts for borders and outlines
// paint as those styles, not as solid. `dashed`, `dotted`, `double`,
// `groove`, `ridge`, `inset` and `outset` were accepted by the parser,
// reported by `getComputedStyle` and by the CSSOM's declaration block, and
// the painter never saw them: `DrawBorderCommand` carried no style, so every
// edge came out solid; the same command draws the outline ring, which was
// solid too. The `border` shorthand worse: it expanded to a width and a
// colour and dropped the style outright, so `border: 2px dashed red` itself
// was solid until this phase.
TEST(border_styles_paint_as_their_style_not_as_solid) {
    using namespace litebird;

    auto render_box = [&](const char* css, const std::function<void(BitmapRenderer&, const Rect&)>& check) {
        const std::string html = std::string(
            "<body style=\"margin:0;background:#ffffff\">"
            "<div id=box style=\"") + css +
            "width:100px;height:40px\"></div></body>";
        auto doc = HtmlParser::parse(html);
        auto el = doc->get_element_by_id("box");
        assert(el);
        StyleResolver resolver;
        auto root = LayoutTree::build(doc, resolver, 200.0f, 100.0f);
        assert(root);
        assert(el->has_layout_box());
        const Rect box = el->bounding_box();
        BitmapRenderer bmp(200, 100);
        bmp.clear(Color::white());
        bmp.render(DisplayList::build(root));
        check(bmp, box);
    };
    const auto px = [](BitmapRenderer& bmp, float x, float y) {
        return bmp.get_pixel(static_cast<int>(x), static_cast<int>(y));
    };
    const auto is_red = [](const Color& c) { return c.r > 200 && c.g < 60 && c.b < 60; };
    const auto is_white = [](const Color& c) { return c.r > 240 && c.g > 240 && c.b > 240; };
    // darken(c) and lighten(c), the two tones groove/ridge/inset/outset are
    // built from: halfway towards black, halfway towards white.
    const auto dark_red = [](const Color& c) { return c.r == 127 && c.g == 0 && c.b == 0; };
    const auto light_red = [](const Color& c) { return c.r == 255 && c.g == 127 && c.b == 127; };

    // Resolver bookkeeping: the shorthand splits, the longhands round-trip,
    // one to four values map top/right/bottom/left, and a bad keyword is
    // dropped like any other.
    {
        auto doc = HtmlParser::parse(
            "<div id=a style=\"border: 2px dashed #112233\"></div>"
            "<div id=b style=\"border-style: dotted double double dotted\"></div>"
            "<div id=c style=\"border-style: solid dashed\"></div>"
            "<div id=d style=\"border-left-style: outset; border-top-width: 3px\"></div>"
            "<div id=e style=\"border-style: banana\"></div>"
            "<div id=f style=\"border: 4px inset rgb(0, 128, 64)\"></div>"
            "<div id=g style=\"border-style: none; border-width: 4px; border-color: #ff0000\"></div>");
        StyleResolver resolver;
        const auto a = resolver.resolve_style(doc->get_element_by_id("a"), nullptr);
        assert(a.border_style.top == BorderStyle::Dashed && a.border_style.right == BorderStyle::Dashed &&
               a.border_style.bottom == BorderStyle::Dashed && a.border_style.left == BorderStyle::Dashed);
        assert(a.border_width.top == 2.0f && a.border_color.r == 0x11 && a.border_color.g == 0x22 &&
               a.border_color.b == 0x33);
        const auto b = resolver.resolve_style(doc->get_element_by_id("b"), nullptr);
        assert(b.border_style.top == BorderStyle::Dotted && b.border_style.right == BorderStyle::Double &&
               b.border_style.bottom == BorderStyle::Double && b.border_style.left == BorderStyle::Dotted);
        const auto c = resolver.resolve_style(doc->get_element_by_id("c"), nullptr);
        assert(c.border_style.top == BorderStyle::Solid && c.border_style.right == BorderStyle::Dashed &&
               c.border_style.bottom == BorderStyle::Solid && c.border_style.left == BorderStyle::Dashed);
        const auto d = resolver.resolve_style(doc->get_element_by_id("d"), nullptr);
        assert(d.border_style.left == BorderStyle::Outset && d.border_style.top == BorderStyle::None);
        const auto e = resolver.resolve_style(doc->get_element_by_id("e"), nullptr);
        assert(e.border_style.top == BorderStyle::None && e.border_style.right == BorderStyle::None);
        const auto f = resolver.resolve_style(doc->get_element_by_id("f"), nullptr);
        assert(f.border_style.top == BorderStyle::Inset && f.border_style.bottom == BorderStyle::Inset);
        assert(f.border_color.g == 0x80);
        const auto g = resolver.resolve_style(doc->get_element_by_id("g"), nullptr);
        assert(g.border_style.top == BorderStyle::None);
    }

    // Per-edge styles from `border-top` et al., and `outline`.
    {
        auto doc = HtmlParser::parse(
            "<div id=a style=\"border: 1px solid; border-top: 4px ridge #00ff00\"></div>"
            "<div id=b style=\"outline: 6px double #00ff00; outline-offset: 0px\"></div>"
            "<div id=c style=\"outline: 5px solid #0000ff; outline-style: groove\"></div>"
            "<div id=d style=\"outline-style: banana\"></div>"
            "<div id=e style=\"outline: 2px dashed\"></div>");
        StyleResolver resolver;
        const auto a = resolver.resolve_style(doc->get_element_by_id("a"), nullptr);
        assert(a.border_style.top == BorderStyle::Ridge);
        assert(a.border_style.right == BorderStyle::Solid && a.border_style.left == BorderStyle::Solid);
        const auto b = resolver.resolve_style(doc->get_element_by_id("b"), nullptr);
        assert(b.outline_style == BorderStyle::Double && b.outline_width == 6.0f && b.outline_offset == 0.0f);
        const auto c = resolver.resolve_style(doc->get_element_by_id("c"), nullptr);
        assert(c.outline_style == BorderStyle::Groove);
        const auto d = resolver.resolve_style(doc->get_element_by_id("d"), nullptr);
        assert(d.outline_style == BorderStyle::None);
        const auto e = resolver.resolve_style(doc->get_element_by_id("e"), nullptr);
        assert(e.outline_style == BorderStyle::Dashed);
    }

    // Ink. `dashed`: 4px border, dashes 12 long, gaps 8 (3x and 2x the
    // width); `dotted`: 4px dots spaced 8; `double`: two 2px bands with a
    // 2px gap; the grooves and ridges split the band in half.
    render_box("border: 4px dashed #ff0000;", [&](BitmapRenderer& bmp, const Rect& box) {
        const float t = 4.0f;
        const float y = box.y + t / 2.0f;
        const float left = box.x;
        assert(is_red(px(bmp, left + 5.0f, y)));       // first dash: 0..11
        assert(is_white(px(bmp, left + 15.0f, y)));    // first gap: 12..19
        assert(is_red(px(bmp, left + 25.0f, y)));      // second dash: 20..31
    });
    render_box("border: 4px dotted #ff0000;", [&](BitmapRenderer& bmp, const Rect& box) {
        const float y = box.y + 2.0f;
        const float left = box.x;
        assert(is_red(px(bmp, left + 2.0f, y)));       // dot centred on x = 2
        assert(is_white(px(bmp, left + 5.0f, y)));     // between dots
        assert(is_red(px(bmp, left + 10.0f, y)));      // next dot
        assert(is_white(px(bmp, left + 13.0f, y)));
    });
    render_box("border: 6px double #ff0000;", [&](BitmapRenderer& bmp, const Rect& box) {
        const float left = box.x + box.width / 2.0f;
        const float top = box.y;
        assert(is_red(px(bmp, left, top + 0.5f)));
        assert(is_red(px(bmp, left, top + 1.5f)));
        assert(is_white(px(bmp, left, top + 2.5f)));   // the gap
        assert(is_white(px(bmp, left, top + 3.5f)));
        assert(is_red(px(bmp, left, top + 4.5f)));
        assert(is_red(px(bmp, left, top + 5.5f)));
        assert(is_white(px(bmp, left, top + 6.5f)));   // below the band
    });
    render_box("border: 4px groove #ff0000;", [&](BitmapRenderer& bmp, const Rect& box) {
        const float left = box.x + box.width / 2.0f;
        const float top = box.y;
        assert(dark_red(px(bmp, left, top + 0.5f)));   // outer half: dark
        assert(dark_red(px(bmp, left, top + 1.5f)));
        assert(light_red(px(bmp, left, top + 2.5f)));  // inner half: light
        assert(light_red(px(bmp, left, top + 3.5f)));
    });
    render_box("border: 4px ridge #ff0000;", [&](BitmapRenderer& bmp, const Rect& box) {
        const float left = box.x + box.width / 2.0f;
        const float top = box.y;
        assert(light_red(px(bmp, left, top + 0.5f)));
        assert(light_red(px(bmp, left, top + 1.5f)));
        assert(dark_red(px(bmp, left, top + 2.5f)));
        assert(dark_red(px(bmp, left, top + 3.5f)));
    });
    render_box("border: 4px inset #ff0000;", [&](BitmapRenderer& bmp, const Rect& box) {
        const float left = box.x + box.width / 2.0f;
        const float right = box.x + box.width - 1.0f;
        const float top = box.y;
        const float bottom = box.y + box.height - 1.0f;
        const float mid = box.y + box.height / 2.0f;
        assert(dark_red(px(bmp, left, top + 1.0f)));    // top: dark
        assert(light_red(px(bmp, left, bottom)));       // bottom: light
        assert(dark_red(px(bmp, box.x + 1.0f, mid)));   // left: dark
        assert(light_red(px(bmp, right, mid)));         // right: light
    });
    render_box("border: 4px outset #ff0000;", [&](BitmapRenderer& bmp, const Rect& box) {
        const float left = box.x + box.width / 2.0f;
        const float right = box.x + box.width - 1.0f;
        const float top = box.y;
        const float bottom = box.y + box.height - 1.0f;
        const float mid = box.y + box.height / 2.0f;
        assert(light_red(px(bmp, left, top + 1.0f)));   // top: light
        assert(dark_red(px(bmp, left, bottom)));        // bottom: dark
        assert(light_red(px(bmp, box.x + 1.0f, mid)));  // left: light
        assert(dark_red(px(bmp, right, mid)));          // right: dark
    });
    // `hidden` paints nothing, whatever the width says.
    render_box("border: 4px hidden #ff0000;", [&](BitmapRenderer& bmp, const Rect& box) {
        const float mid = box.y + box.height / 2.0f;
        assert(is_white(px(bmp, box.x + box.width / 2.0f, box.y + 0.5f)));
        assert(is_white(px(bmp, box.x + 0.5f, mid)));
    });
    // `border-style: none` with a width reserved: nothing is painted. (The
    // layout still reserves the width — see finding 512.)
    render_box("border-width: 4px; border-style: none; border-color: #ff0000;", [&](BitmapRenderer& bmp, const Rect& box) {
        assert(is_white(px(bmp, box.x + box.width / 2.0f, box.y + 0.5f)));
    });
    // The outline ring carries its style too: `double` two bands with a
    // gap, `dashed` with gaps, and `hidden` not at all.
    {
        const std::string html =
            "<body style=\"margin:0;background:#ffffff\">"
            "<div id=a style=\"margin:20px;width:40px;height:20px;outline:6px double #00ff00\"></div>"
            "</body>";
        auto doc = HtmlParser::parse(html);
        auto el = doc->get_element_by_id("a");
        StyleResolver resolver;
        auto root = LayoutTree::build(doc, resolver, 200.0f, 100.0f);
        const Rect box = el->bounding_box();
        BitmapRenderer bmp(200, 100);
        bmp.clear(Color::white());
        bmp.render(DisplayList::build(root));
        const float mid = box.y + box.height / 2.0f;
        const auto is_green = [](const Color& c) { return c.g > 200 && c.r < 60 && c.b < 60; };
        assert(is_green(px(bmp, box.x - 6.0f, mid)));   // outer band
        assert(is_green(px(bmp, box.x - 5.0f, mid)));
        assert(is_white(px(bmp, box.x - 4.0f, mid)));   // the gap
        assert(is_white(px(bmp, box.x - 3.0f, mid)));
        assert(is_green(px(bmp, box.x - 2.0f, mid)));   // inner band
        assert(is_green(px(bmp, box.x - 1.0f, mid)));
    }
    {
        const std::string html =
            "<body style=\"margin:0;background:#ffffff\">"
            "<div id=a style=\"margin:20px;width:40px;height:20px;outline:4px dashed #ff0000\"></div>"
            "</body>";
        auto doc = HtmlParser::parse(html);
        auto el = doc->get_element_by_id("a");
        StyleResolver resolver;
        auto root = LayoutTree::build(doc, resolver, 200.0f, 100.0f);
        const Rect box = el->bounding_box();
        BitmapRenderer bmp(200, 100);
        bmp.clear(Color::white());
        bmp.render(DisplayList::build(root));
        // The ring's top band is a dashed edge starting at the box's left.
        const float y = box.y - 2.0f;
        assert(is_red(px(bmp, box.x + 5.0f, y)));        // first dash: 0..11
        assert(is_white(px(bmp, box.x + 15.0f, y)));     // first gap: 12..19
        assert(is_red(px(bmp, box.x + 25.0f, y)));       // second dash: 20..31
    }
    {
        const std::string html =
            "<body style=\"margin:0;background:#ffffff\">"
            "<div id=a style=\"margin:20px;width:40px;height:20px;outline:4px hidden #ff0000\"></div>"
            "</body>";
        auto doc = HtmlParser::parse(html);
        auto el = doc->get_element_by_id("a");
        StyleResolver resolver;
        auto root = LayoutTree::build(doc, resolver, 200.0f, 100.0f);
        const Rect box = el->bounding_box();
        BitmapRenderer bmp(200, 100);
        bmp.clear(Color::white());
        bmp.render(DisplayList::build(root));
        const float mid = box.y + box.height / 2.0f;
        assert(is_white(px(bmp, box.x - 2.0f, mid)));    // no ring at all
    }

    // And the same declarations, read back through getComputedStyle: the
    // report was already right, the ink was what was missing.
    auto page = std::make_unique<TestPage>();
    JsInterpreter& js = page->js;
    js.eval(R"JS(
        var d = document.createElement('div');
        d.setAttribute('style', 'border: 3px dashed #112233; outline: 4px double #ff0000');
        document.body.appendChild(d);
        window.__d = d;
    )JS");
    assert(js.eval("getComputedStyle(window.__d).borderTopStyle").to_string() == "dashed");
    assert(js.eval("getComputedStyle(window.__d).borderStyle").to_string() == "dashed");
    assert(js.eval("getComputedStyle(window.__d).borderLeftColor").to_string() == "rgb(17, 34, 51)");
    assert(js.eval("getComputedStyle(window.__d).outlineStyle").to_string() == "double");

    std::printf("      [border-style] dashes, dots, doubles, grooves, ridges, insets, outsets, hidden, and the outline ring\n");
    std::fflush(stdout);
}
