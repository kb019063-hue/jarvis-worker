// Phase 238, finding 242: CSS 2.1 table row-group/cell-height layout and
// inline formatting (`white-space`, `vertical-align`, `display: inline-block`,
// and inline horizontal `margin`/`padding`/`border` cursor advance).
TEST(css_table_layers_cell_height_and_inline_formatting) {
    using namespace litebird;

    auto render_html = [](const std::string& html, int w, int h) -> BitmapRenderer {
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, static_cast<float>(w), static_cast<float>(h));
        auto root = LayoutTree::build(doc, resolver, static_cast<float>(w), static_cast<float>(h));
        assert(root);
        const DisplayList list = DisplayList::build(root);
        BitmapRenderer bmp(w, h);
        bmp.clear(Color::white());
        bmp.render(list);
        return bmp;
    };

    auto find_by_id = [](const std::shared_ptr<LayoutBox>& root,
                         const std::string& id) -> std::shared_ptr<LayoutBox> {
        std::shared_ptr<LayoutBox> found;
        std::function<void(const std::shared_ptr<LayoutBox>&)> walk =
            [&](const std::shared_ptr<LayoutBox>& b) {
                if (!b || found) return;
                if (b->node && b->node->is_element() &&
                    b->node->as_element()->get_attribute("id") == id) {
                    found = b;
                    return;
                }
                for (const auto& ch : b->children) walk(ch);
            };
        walk(root);
        return found;
    };

    // 1. Table row-group (<thead>/<tbody>/<tfoot>) bounding boxes & CSS 2.1 §17.5.1 paint layers.
    {
        const std::string html = R"html(
            <!doctype html><html><head><style>
                html { background: #ffffff; }
                body { margin: 0; }
                table { border-collapse: collapse; border-spacing: 0; background: #ff0000; }
                thead { background: #00ffff; }
                tbody { background: #00ff00; }
                tr.r1 { background: #0000ff; }
                td { width: 50px; height: 30px; padding: 0; border: 0; }
                td.c1 { background: #ffff00; }
            </style></head><body>
                <table>
                    <thead id="th"><tr><td></td><td></td></tr></thead>
                    <tbody id="tb"><tr class="r1"><td class="c1"></td><td></td></tr><tr><td></td><td></td></tr></tbody>
                </table>
            </body></html>
        )html";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 200.0f, 200.0f);
        auto root = LayoutTree::build(doc, resolver, 200.0f, 200.0f);
        auto th = find_by_id(root, "th");
        auto tb = find_by_id(root, "tb");
        assert(th && tb);
        assert(std::fabs(th->dimensions.content.height - 30.0f) < 0.5f);
        assert(std::fabs(tb->dimensions.content.y - 30.0f) < 0.5f);
        assert(std::fabs(tb->dimensions.content.height - 60.0f) < 0.5f);

        auto bmp = render_html(html, 120, 100);
        // thead cyan at (10, 10)
        assert(bmp.get_pixel(10, 10) == Color(0, 255, 255, 255));
        // cell yellow over row blue over tbody green at (10, 40)
        assert(bmp.get_pixel(10, 40) == Color(255, 255, 0, 255));
        // row blue over tbody green at (60, 40)
        assert(bmp.get_pixel(60, 40) == Color(0, 0, 255, 255));
        // tbody green over table red at (10, 70)
        assert(bmp.get_pixel(10, 70) == Color(0, 255, 0, 255));
    }

    // 2. Shorter table cell stretches its border/background box to match a taller sibling's row height.
    {
        const std::string html = R"html(
            <!doctype html><html><head><style>
                html { background: #ffffff; }
                body { margin: 0; }
                table { border-collapse: collapse; border-spacing: 0; }
                td { width: 40px; padding: 0; border: 0; }
                #a { background: #ff0000; }
                #b { background: #00ff00; }
            </style></head><body>
                <table><tr>
                    <td id="a"><div style="width:10px;height:15px"></div></td>
                    <td id="b"><div style="width:10px;height:55px"></div></td>
                </tr></table>
            </body></html>
        )html";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 200.0f, 200.0f);
        auto root = LayoutTree::build(doc, resolver, 200.0f, 200.0f);
        auto a = find_by_id(root, "a");
        auto b = find_by_id(root, "b");
        assert(a && b);
        assert(std::fabs(a->dimensions.content.height - 55.0f) < 0.5f);
        assert(std::fabs(b->dimensions.content.height - 55.0f) < 0.5f);

        auto bmp = render_html(html, 100, 80);
        assert(bmp.get_pixel(15, 45) == Color(255, 0, 0, 255));
        assert(bmp.get_pixel(55, 45) == Color(0, 255, 0, 255));
        assert(bmp.get_pixel(15, 60) == Color(255, 255, 255, 255));
    }

    // 3. white-space parsing & inheritance; vertical-align parsing (non-inherited).
    {
        auto doc = HtmlParser::parse(R"html(
            <div id="p" style="white-space: nowrap; vertical-align: top;">
                <span id="c1">a</span>
                <span id="c2" style="white-space: pre-wrap; vertical-align: bottom;">b</span>
                <span id="c3" style="white-space: initial; vertical-align: middle;">c</span>
                <span id="c4" style="white-space: pre-line; vertical-align: text-top;">d</span>
            </div>
        )html");
        StyleResolver resolver;
        auto p = doc->get_element_by_id("p");
        auto c1 = doc->get_element_by_id("c1");
        auto c2 = doc->get_element_by_id("c2");
        auto c3 = doc->get_element_by_id("c3");
        auto c4 = doc->get_element_by_id("c4");
        assert(p && c1 && c2 && c3 && c4);

        ComputedStyle ps = resolver.resolve_style(p);
        ComputedStyle s1 = resolver.resolve_style(c1, &ps);
        ComputedStyle s2 = resolver.resolve_style(c2, &ps);
        ComputedStyle s3 = resolver.resolve_style(c3, &ps);
        ComputedStyle s4 = resolver.resolve_style(c4, &ps);

        assert(ps.white_space == WhiteSpace::Nowrap);
        assert(ps.vertical_align == VerticalAlign::Top);
        // white-space inherits; vertical-align does not.
        assert(s1.white_space == WhiteSpace::Nowrap);
        assert(s1.vertical_align == VerticalAlign::Baseline);
        assert(s2.white_space == WhiteSpace::PreWrap);
        assert(s2.vertical_align == VerticalAlign::Bottom);
        assert(s3.white_space == WhiteSpace::Normal);
        assert(s3.vertical_align == VerticalAlign::Middle);
        assert(s4.white_space == WhiteSpace::PreLine);
        assert(s4.vertical_align == VerticalAlign::TextTop);
    }

    // 4. display: inline-block flows horizontally, wraps, shifts nested children, and honors vertical-align.
    {
        const std::string html = R"html(
            <!doctype html><html><head><style>
                html { background: #ffffff; }
                body { margin: 0; }
                .wrap { width: 50px; font-size: 0; line-height: 30px; background: #eeeeee; }
                .ib { display: inline-block; width: 20px; height: 10px; }
                #b1 { background: #ff0000; vertical-align: top; }
                #b2 { background: #00ff00; vertical-align: bottom; }
                #b3 { background: #0000ff; vertical-align: top; }
                .inner { width: 10px; height: 10px; background: #ffff00; }
            </style></head><body>
                <div class="wrap"><span id="b1" class="ib"></span><span id="b2" class="ib"></span><span id="b3" class="ib"><div class="inner"></div></span></div>
            </body></html>
        )html";
        auto bmp = render_html(html, 80, 80);
        // b1 top-aligned at [0..20, 0..10]
        assert(bmp.get_pixel(5, 5) == Color(255, 0, 0, 255));
        assert(bmp.get_pixel(5, 25) == Color(238, 238, 238, 255));
        // b2 bottom-aligned at [20..40, 20..30]
        assert(bmp.get_pixel(25, 5) == Color(238, 238, 238, 255));
        assert(bmp.get_pixel(25, 25) == Color(0, 255, 0, 255));
        // b3 wrapped to line 2 [0..20, 30..40] with nested yellow child shifted to [0..10, 30..40]
        assert(bmp.get_pixel(5, 35) == Color(255, 255, 0, 255));
        assert(bmp.get_pixel(15, 35) == Color(0, 0, 255, 255));
    }

    // 5. Inline horizontal margin/padding advances cursor without stretching or painting margin.
    {
        const std::string html = R"html(
            <!doctype html><html><head><style>
                html { background: #ffffff; }
                body { margin: 0; }
                .line { width: 100px; font-size: 0; line-height: 20px; background: #eeeeee; }
                .sp { margin-left: 10px; padding-left: 10px; }
                img { width: 20px; height: 20px; vertical-align: top; }
            </style></head><body>
                <div class="line"><span class="sp"><img src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg=="></span></div>
            </body></html>
        )html";
        auto bmp = render_html(html, 100, 40);
        assert(bmp.get_pixel(5, 10) == Color(238, 238, 238, 255));
        assert(bmp.get_pixel(15, 10) == Color(238, 238, 238, 255));
        assert(bmp.get_pixel(25, 10) == Color(255, 0, 0, 255));
        assert(bmp.get_pixel(39, 10) == Color(255, 0, 0, 255));
        assert(bmp.get_pixel(45, 10) == Color(238, 238, 238, 255));
    }
}
