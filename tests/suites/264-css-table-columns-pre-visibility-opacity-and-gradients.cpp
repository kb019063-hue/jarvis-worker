// Phase 264, finding 268: CSS 2.1 table column/colgroup backgrounds,
// white-space: pre/pre-line newline preservation, visibility: hidden/visible,
// multi-layer inline ancestor backgrounds, isolated opacity compositing groups,
// and linear-gradient background images (including root canvas propagation).
TEST(css_table_columns_pre_visibility_opacity_and_gradients) {
    using namespace litebird;

    auto render_html = [](const std::string& html, int w, int h) -> BitmapRenderer {
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver =
            DocumentStyles::build_resolver(*doc, nullptr, static_cast<float>(w), static_cast<float>(h));
        auto root = LayoutTree::build(doc, resolver, static_cast<float>(w), static_cast<float>(h));
        assert(root);
        const DisplayList list = DisplayList::build(root);
        BitmapRenderer bmp(w, h);
        bmp.clear(Color::white());
        bmp.render(list);
        return bmp;
    };

    // 1. Table <colgroup> / <col> CSS 2.1 §17.5.1 layer order, span attribute,
    //    explicit <col> width hint, and DOM-order independence.
    {
        const std::string html = R"html(
            <!doctype html><html><head><style>
                html { background: #ffffff; }
                body { margin: 0; }
                table { border-collapse: collapse; border-spacing: 0; background: #ff0000; }
                colgroup.cg { background: #ffff00; }
                col.c12 { background: #00ff00; width: 40px; }
                tr.r2 { background: #00ffff; }
                td { width: 40px; height: 30px; padding: 0; border: 0; }
                td.top { background: #0000ff; }
            </style></head><body>
                <table>
                    <tbody>
                        <tr><td class="top"></td><td></td><td></td><td></td></tr>
                        <tr class="r2"><td></td><td></td><td></td><td></td></tr>
                        <tr><td></td><td></td><td></td><td></td></tr>
                    </tbody>
                    <colgroup class="cg"><col class="c12" span="2"><col></colgroup>
                </table>
            </body></html>
        )html";
        auto bmp = render_html(html, 200, 120);
        // Row 0, col 0: opaque cell blue beats col green, colgroup yellow, table red
        assert(bmp.get_pixel(10, 10) == Color(0, 0, 255, 255));
        // Row 0, col 1: col span=2 green beats colgroup yellow and table red
        assert(bmp.get_pixel(50, 10) == Color(0, 255, 0, 255));
        // Row 0, col 2: unstyled col inside yellow colgroup beats table red
        assert(bmp.get_pixel(90, 10) == Color(255, 255, 0, 255));
        // Row 0, col 3: outside colgroup -> table red
        assert(bmp.get_pixel(130, 10) == Color(255, 0, 0, 255));
        // Row 1, col 1: row cyan beats col green even when <colgroup> is after <tbody> in DOM
        assert(bmp.get_pixel(50, 40) == Color(0, 255, 255, 255));
        // Row 2, col 0: transparent cell exposes col green
        assert(bmp.get_pixel(10, 70) == Color(0, 255, 0, 255));
    }

    // 2. white-space: pre and pre-line preserve source newlines between inline
    //    replaced elements, while white-space: normal collapses them.
    {
        const std::string red_px =
            "data:image/png;base64,"
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg==";
        const std::string pre_html =
            "<!doctype html><html><head><style>"
            "html{background:#fff}body{margin:0}"
            ".line{width:100px;font-size:0;line-height:20px;white-space:pre}"
            "img{width:20px;height:20px;vertical-align:top}"
            "</style></head><body><div class='line'><img src='" + red_px + "'>\n<img src='" + red_px + "'></div></body></html>";
        auto bmp_pre = render_html(pre_html, 120, 60);
        assert(bmp_pre.get_pixel(10, 10) == Color(255, 0, 0, 255));
        assert(bmp_pre.get_pixel(30, 10) == Color(255, 255, 255, 255));
        assert(bmp_pre.get_pixel(10, 30) == Color(255, 0, 0, 255));

        const std::string preline_html =
            "<!doctype html><html><head><style>"
            "html{background:#fff}body{margin:0}"
            ".line{width:100px;font-size:0;line-height:20px;white-space:pre-line}"
            "img{width:20px;height:20px;vertical-align:top}"
            "</style></head><body><div class='line'><img src='" + red_px + "'>   \n   <img src='" + red_px + "'></div></body></html>";
        auto bmp_pl = render_html(preline_html, 120, 60);
        assert(bmp_pl.get_pixel(10, 10) == Color(255, 0, 0, 255));
        assert(bmp_pl.get_pixel(30, 10) == Color(255, 255, 255, 255));
        assert(bmp_pl.get_pixel(10, 30) == Color(255, 0, 0, 255));

        const std::string normal_html =
            "<!doctype html><html><head><style>"
            "html{background:#fff}body{margin:0}"
            ".line{width:100px;font-size:0;line-height:20px;white-space:normal}"
            "img{width:20px;height:20px;vertical-align:top}"
            "</style></head><body><div class='line'><img src='" + red_px + "'>\n<img src='" + red_px + "'></div></body></html>";
        auto bmp_norm = render_html(normal_html, 120, 60);
        assert(bmp_norm.get_pixel(10, 10) == Color(255, 0, 0, 255));
        assert(bmp_norm.get_pixel(30, 10) == Color(255, 0, 0, 255));
        assert(bmp_norm.get_pixel(10, 30) == Color(255, 255, 255, 255));
    }

    // 3. visibility: hidden hides chrome/descendants while preserving layout
    //    geometry, allowing visibility: visible descendants to paint, and
    //    still propagating body background to the canvas.
    {
        const std::string html = R"html(
            <!doctype html><body style="margin:0;visibility:hidden;background:#ff0000">
                <div style="width:40px;height:40px;background:#0000ff">
                    <div style="width:20px;height:20px;background:#00ff00;visibility:visible"></div>
                </div>
                <div style="width:40px;height:20px;background:#ffff00;visibility:visible"></div>
            </body>
        )html";
        auto bmp = render_html(html, 100, 100);
        // Canvas stays red despite visibility:hidden on body
        assert(bmp.get_pixel(80, 80) == Color(255, 0, 0, 255));
        // Hidden blue outer div does not paint at (30, 30)
        assert(bmp.get_pixel(30, 30) == Color(255, 0, 0, 255));
        // Visible green grandchild paints at (10, 10)
        assert(bmp.get_pixel(10, 10) == Color(0, 255, 0, 255));
        // Second sibling sits at y=40 (hidden first block kept its 40px height)
        assert(bmp.get_pixel(10, 50) == Color(255, 255, 0, 255));
    }

    // 4. Multi-layer inline ancestor backgrounds: translucent child composites
    //    over inline parent/body, and opaque inner inline is not overwritten.
    {
        const std::string html_translucent = R"html(
            <!doctype html><body style="margin:0;display:inline;background:rgba(255,0,0,.5);font:20px/20px monospace;color:transparent">
                <span style="background:rgba(0,0,255,.5)">MMMMMMMM</span>
            </body>
        )html";
        auto bmp = render_html(html_translucent, 240, 40);
        assert(bmp.get_pixel(10, 10) == Color(0x7f, 0x1f, 0x9f, 255));
        assert(bmp.get_pixel(200, 10) == Color(0xff, 0x7f, 0x7f, 255));

        const std::string html_opaque_nested = R"html(
            <!doctype html><html><body style="margin:0;background:#ffffff;font:20px/20px monospace;color:transparent">
                <span style="background:#ff0000"><span style="background:#0000ff">MMMM</span></span>
            </body></html>
        )html";
        auto bmp_nested = render_html(html_opaque_nested, 120, 40);
        assert(bmp_nested.get_pixel(10, 10) == Color(0, 0, 255, 255));
    }

    // 5. Opacity compositing groups (CSS Color 4 §3.2):
    //    - root opacity: 0 hides canvas + body
    //    - element opacity: 0.5 isolates overlapping opaque children so lower
    //      opaque red does not bleed through upper opaque blue
    //    - nested opacity multiplies cleanly
    {
        const std::string root_zero = R"html(
            <!doctype html><html style="opacity:0;background:#0000ff">
                <body style="margin:0;background:#ff0000;height:50px"></body>
            </html>
        )html";
        auto bmp0 = render_html(root_zero, 80, 80);
        assert(bmp0.get_pixel(0, 0) == Color(255, 255, 255, 255));
        assert(bmp0.get_pixel(20, 20) == Color(255, 255, 255, 255));

        const std::string isolated_group = R"html(
            <!doctype html><html><body style="margin:0;background:#ffffff">
                <div style="opacity:0.5;width:60px;height:60px;background:#ff0000">
                    <div style="width:40px;height:40px;background:#0000ff"></div>
                </div>
            </body></html>
        )html";
        auto bmp_iso = render_html(isolated_group, 80, 80);
        // Inside inner opaque blue child: group pixel is (0,0,255) at 50% over white -> (127,127,255)
        // (No red bleed-through from the parent background underneath!)
        const Color inner_px = bmp_iso.get_pixel(20, 20);
        assert(std::abs(static_cast<int>(inner_px.r) - 127) <= 1);
        assert(std::abs(static_cast<int>(inner_px.g) - 127) <= 1);
        assert(inner_px.b == 255);
        // Outside inner blue child, inside outer red div: (255,0,0) at 50% over white -> (255,127,127)
        const Color outer_px = bmp_iso.get_pixel(50, 50);
        assert(outer_px.r == 255);
        assert(std::abs(static_cast<int>(outer_px.g) - 127) <= 1);
        assert(std::abs(static_cast<int>(outer_px.b) - 127) <= 1);

        // TerminalRenderer also safely skips opacity: 0 subtrees.
        auto doc = HtmlParser::parse(root_zero);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 80.0f, 80.0f);
        auto root = LayoutTree::build(doc, resolver, 80.0f, 80.0f);
        TerminalRenderer term(20, 5);
        const std::string ansi = term.to_ansi_string(DisplayList::build(root));
        assert(!ansi.empty());
    }

    // 6. CSS linear-gradient(...) backgrounds and root background-image
    //    blocking body propagation (CSS Backgrounds 3 §2.11.2, Images 3 §3.4).
    {
        const std::string root_grad = R"html(
            <!doctype html><html style="background-image:linear-gradient(#0000ff,#0000ff)">
                <body style="margin:20px;background:#ff0000;height:50px"></body>
            </html>
        )html";
        auto bmp_rg = render_html(root_grad, 120, 120);
        assert(bmp_rg.get_pixel(5, 5) == Color(0, 0, 255, 255));
        assert(bmp_rg.get_pixel(30, 30) == Color(255, 0, 0, 255));
        assert(bmp_rg.get_pixel(115, 115) == Color(0, 0, 255, 255));

        const std::string hard_split = R"html(
            <!doctype html><html><body style="margin:0;background:#ffffff">
                <div style="width:100px;height:40px;background-image:linear-gradient(to right, #ff0000 0%, #ff0000 50%, #0000ff 50%, #0000ff 100%)"></div>
            </body></html>
        )html";
        auto bmp_split = render_html(hard_split, 120, 60);
        assert(bmp_split.get_pixel(10, 20) == Color(255, 0, 0, 255));
        assert(bmp_split.get_pixel(90, 20) == Color(0, 0, 255, 255));

        const std::string smooth_vert = R"html(
            <!doctype html><html><body style="margin:0;background:#ffffff">
                <div style="width:40px;height:100px;background:linear-gradient(#ff0000, #0000ff)"></div>
            </body></html>
        )html";
        auto bmp_sv = render_html(smooth_vert, 60, 120);
        assert(bmp_sv.get_pixel(20, 0).r >= 250 && bmp_sv.get_pixel(20, 0).b <= 5);
        assert(bmp_sv.get_pixel(20, 99).b >= 250 && bmp_sv.get_pixel(20, 99).r <= 5);
        const Color mid = bmp_sv.get_pixel(20, 50);
        assert(mid.r >= 115 && mid.r <= 140 && mid.b >= 115 && mid.b <= 140 && mid.g == 0);
    }
}
