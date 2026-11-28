// Phase 468: direction and unicode-bidi reach the cascade and the line.
//
// Phase 109 built the algorithm (UAX #9, rules P2-L2) and ran it with a base
// level the first strong character chose, and that was all a page could ask
// for: `direction` and `unicode-bidi` were not properties the cascade knew, a
// `dir` attribute styled nothing, `bdo` carried no rule, and `text-align`'s
// default had no direction to resolve against. This suite is the feature as
// the page sees it: the declarations parse and compute, the `dir` attribute
// hints the cascade (auto through the first strong character), the elements
// whose content is an isolated sequence carry `unicode-bidi: isolate`, an
// explicit direction reorders a mixed line the way the paragraph direction
// implies, an override paints even ASCII backwards, and start/end alignment
// follows the line while left/right stay physical.
TEST(direction_and_unicode_bidi_reach_the_cascade_and_the_line) {
    using namespace litebird;

    const std::string shalom = "\u05e9\u05dc\u05d5\u05dd";           // שלום
    const std::string shalom_reversed = "\u05dd\u05d5\u05dc\u05e9";  // םולש

    // ------------------------------------------------------------------
    // The cascade: parsing, defaults, inheritance, hints, the UA sheet.
    // ------------------------------------------------------------------
    {
        TestPage page;
        page.load("<!doctype html><html><body>"
                  "<div id=plain>hello</div>"
                  "<div id=hinted dir=rtl>hello"
                  "<div id=inherited>deep</div>"
                  "</div>"
                  "<div id=declared style=\"direction: rtl\">hello</div>"
                  "<div id=auto_hebrew dir=auto>" + shalom + "</div>"
                  "<div id=auto_latin dir=auto>hello</div>"
                  "<div id=auto_digits dir=auto>123 456</div>"
                  "<div id=bogus style=\"direction: sideways; unicode-bidi: bogus\">hello</div>"
                  "<span id=span_plain>x</span>"
                  "<span id=embed style=\"unicode-bidi:embed\">x</span>"
                  "<span id=isolate style=\"unicode-bidi:isolate\">x</span>"
                  "<span id=isolate_override style=\"direction:rtl; unicode-bidi:isolate-override\">x</span>"
                  "<span id=plaintext style=\"unicode-bidi:plaintext\">x</span>"
                  "<bdo id=plain_bdo>abc</bdo>"
                  "<bdo id=rtl_bdo dir=rtl>abc</bdo>"
                  "<ul><li id=list_item>x</li></ul>"
                  "<bdi id=bdi_item>x</bdi>"
                  "</body></html>");
        JsInterpreter& js = page.js;
        auto cs = [&](const std::string& id, const std::string& prop) {
            return js.eval("getComputedStyle(document.getElementById('" + id + "'))." + prop)
                .to_string();
        };

        assert(cs("plain", "direction") == "ltr");
        // A block container isolates its content: HTML §Rendering gives
        // `div`, `p` and friends `unicode-bidi: isolate` (what a browser's
        // getComputedStyle answers for a div, too). The initial value shows
        // on an element outside that list.
        assert(cs("plain", "unicodeBidi") == "isolate");
        assert(cs("span_plain", "unicodeBidi") == "normal");
        // The `dir` attribute is a presentational hint on `direction`, set
        // before the matched rules so an author declaration can win.
        assert(cs("hinted", "direction") == "rtl");
        // `direction` inherits, so the hint's value reaches the descendants.
        assert(cs("inherited", "direction") == "rtl");
        assert(cs("declared", "direction") == "rtl");
        // `dir=auto` asks the first strong character (P2/P3).
        assert(cs("auto_hebrew", "direction") == "rtl");
        assert(cs("auto_latin", "direction") == "ltr");
        // Digits are not strong: no strong character means ltr here.
        assert(cs("auto_digits", "direction") == "ltr");
        // An unparseable value is ignored; the UA sheet's isolate for the
        // div still stands, and a span outside the group stays normal.
        assert(cs("bogus", "direction") == "ltr");
        assert(cs("bogus", "unicodeBidi") == "isolate");
        // The six values of `unicode-bidi` parse and answer by name.
        assert(cs("embed", "unicodeBidi") == "embed");
        assert(cs("isolate", "unicodeBidi") == "isolate");
        assert(cs("isolate_override", "unicodeBidi") == "isolate-override");
        assert(cs("plaintext", "unicodeBidi") == "plaintext");
        // The UA sheet: `bdo` turns the algorithm off for its content, and
        // the elements whose content is an isolated sequence isolate it.
        assert(cs("plain_bdo", "unicodeBidi") == "bidi-override");
        assert(cs("plain_bdo", "direction") == "ltr");
        assert(cs("rtl_bdo", "unicodeBidi") == "bidi-override");
        assert(cs("rtl_bdo", "direction") == "rtl");
        assert(cs("list_item", "unicodeBidi") == "isolate");
        assert(cs("bdi_item", "unicodeBidi") == "isolate");
    }

    // ------------------------------------------------------------------
    // The line: an explicit direction reorders what first-strong implied.
    // ------------------------------------------------------------------
    auto layout_line = [](const std::string& attrs, const std::string& css,
                          const std::string& inner) {
        const std::string html =
            "<html><body style=\"margin:0\"><p id=\"p\" " + attrs +
            " style=\"margin:0;width:600px;font-size:20px;" + css + "\">" + inner +
            "</p></body></html>";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver;
        auto root = LayoutTree::build(doc, resolver, 600.0f, 600.0f);
        std::shared_ptr<LayoutBox> paragraph;
        std::function<void(const std::shared_ptr<LayoutBox>&)> walk =
            [&](const std::shared_ptr<LayoutBox>& box) {
                if (!box) return;
                if (box->node && box->node->type() == NodeType::Element) {
                    auto element = std::static_pointer_cast<Element>(box->node);
                    if (element->get_attribute("id") == "p") paragraph = box;
                }
                for (const auto& child : box->children) walk(child);
            };
        walk(root);
        assert(paragraph);
        assert(!paragraph->line_boxes.empty());
        return paragraph->line_boxes.front();
    };

    {
        // The first strong character is Latin, so the first-strong base was
        // left-to-right whatever the page asked for. Two runs: the Latin
        // word and the Hebrew word, painted in that order.
        const LineBox line = layout_line("", "", "alpha " + shalom);
        assert(line.fragments.size() >= 2);
        assert(line.fragments.front().text == "alpha");
        assert(line.fragments.back().text == shalom_reversed);
    }
    {
        // The same text under dir=rtl: the base flips, so the line's two
        // runs trade places — the Hebrew run is painted leftmost now.
        const LineBox line = layout_line("dir=\"rtl\"", "", "alpha " + shalom);
        assert(line.fragments.size() >= 2);
        assert(line.fragments.front().text == shalom_reversed);
        assert(line.fragments.back().text == "alpha");
        assert(line.fragments.front().rect.x < line.fragments.back().rect.x);
    }
    {
        // An explicit ltr stays untouched: the fast path must not reorder
        // plain left-to-right ASCII just because the pass now runs.
        const LineBox line = layout_line("dir=\"ltr\"", "", "abc def");
        assert(line.fragments.size() >= 2);
        assert(line.fragments.front().text == "abc");
        assert(line.fragments.back().text == "def");
    }

    // ------------------------------------------------------------------
    // The override: even ASCII paints backwards, and only where asked.
    // ------------------------------------------------------------------
    {
        const LineBox line =
            layout_line("", "direction:rtl; unicode-bidi:bidi-override;", "abc");
        assert(line.fragments.size() == 1);
        assert(line.fragments.front().text == "cba");
    }
    {
        // The UA sheet's `bdo` rule, inside an ordinary paragraph: only the
        // bdo's own content is overridden; the paragraph keeps its order.
        const LineBox line = layout_line("", "", "<bdo dir=\"rtl\">abc</bdo> def");
        assert(line.fragments.front().text == "cba");
        std::string painted;
        for (const InlineFragment& fragment : line.fragments) painted += fragment.text;
        assert(painted.find("abc") == std::string::npos);  // logical order, nowhere
        assert(painted.find("def") != std::string::npos);  // the rest reads on
    }

    // ------------------------------------------------------------------
    // text-align: start and end follow the line; left and right are
    // physical, whichever way the line runs.
    // ------------------------------------------------------------------
    {
        // The default (start) aligns an rtl line to the right edge.
        const LineBox line = layout_line("dir=\"rtl\"", "", "alpha");
        assert(line.fragments.size() == 1);
        const InlineFragment& piece = line.fragments.front();
        assert(piece.rect.x + piece.rect.width > 500.0f);
    }
    {
        // end in an rtl line is the physical left edge.
        const LineBox line = layout_line("dir=\"rtl\"", "text-align:end;", "alpha");
        assert(line.fragments.front().rect.x < 50.0f);
    }
    {
        // left stays physical: the rtl line still starts at the left edge.
        const LineBox line = layout_line("dir=\"rtl\"", "text-align:left;", "alpha");
        assert(line.fragments.front().rect.x < 50.0f);
    }
    {
        // right stays physical, and an ltr line's default (start) is still
        // the left edge.
        const LineBox line = layout_line("", "text-align:right;", "alpha");
        assert(line.fragments.front().rect.x + line.fragments.front().rect.width > 500.0f);
        const LineBox ltr = layout_line("dir=\"ltr\"", "", "alpha");
        assert(ltr.fragments.front().rect.x < 50.0f);
    }

    // ------------------------------------------------------------------
    // The API underneath: the caller's fallback is the paragraph
    // direction when P2/P3 find no strong character, and an explicit
    // paragraph level ignores first-strong entirely.
    // ------------------------------------------------------------------
    assert(bidi_paragraph_utf8(shalom + " hello", -1).base_level == 1);
    assert(bidi_paragraph_utf8("hello " + shalom, -1).base_level == 0);
    assert(bidi_paragraph_utf8("123 456", -1, 0).base_level == 0);
    assert(bidi_paragraph_utf8("123 456", -1, 1).base_level == 1);
    assert(bidi_paragraph_utf8(shalom, 0).base_level == 0);
    assert(bidi_paragraph_utf8("hello", 1).base_level == 1);
}
