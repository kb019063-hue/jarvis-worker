// Phase 364, finding 364: generated content can be a list that includes attr().
TEST(generated_content_lists_and_attr) {
    // 1. A single string still generates a box (Phase 215 regression).
    {
        const std::string html =
            "<!doctype html><html><head><style>"
            "body{margin:0;font-size:16px}"
            "#a::before{content:\"hello\"}"
            "</style></head><body><div id=a></div></body></html>";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 800, 600);
        auto b = doc->get_element_by_id("a");
        assert(b);
        ComputedStyle base = resolver.resolve_style(b, nullptr);
        ComputedStyle before = resolver.resolve_pseudo_element(b, 1, base);
        assert(before.content == "hello");
        auto root = LayoutTree::build(doc, resolver, 800, 600);
        assert(root);
        // Find the generated text box
        bool saw_hello = false;
        std::vector<const LayoutBox*> stack{root.get()};
        while (!stack.empty()) {
            const LayoutBox* bx = stack.back(); stack.pop_back();
            if (bx->box_type == BoxType::TextNode && bx->text == "hello") saw_hello = true;
            for (auto &c : bx->children) stack.push_back(c.get());
        }
        assert(saw_hello);
    }

    // 2. A list of strings concatenates
    {
        const std::string html =
            "<!doctype html><html><head><style>"
            "#b::before{content:\"a\" \"b\" \"c\"}"
            "#c::after{content:'x' 'y'}"
            "</style></head><body><div id=b></div><div id=c></div></body></html>";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 800, 600);
        auto b = doc->get_element_by_id("b");
        auto c = doc->get_element_by_id("c");
        assert(b && c);
        ComputedStyle base_b = resolver.resolve_style(b, nullptr);
        ComputedStyle base_c = resolver.resolve_style(c, nullptr);
        assert(resolver.resolve_pseudo_element(b, 1, base_b).content == "abc");
        assert(resolver.resolve_pseudo_element(c, 2, base_c).content == "xy");
    }

    // 3. attr() reads the element's attribute
    {
        const std::string html =
            "<!doctype html><html><head><style>"
            "#d::before{content:attr(data-value)}"
            "#e::before{content:\"prefix-\" attr(data-value) \"-suffix\"}"
            "</style></head><body>"
            "<div id=d data-value=\"42\"></div>"
            "<div id=e data-value=\"hello\"></div>"
            "</body></html>";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 800, 600);
        auto d = doc->get_element_by_id("d");
        auto e = doc->get_element_by_id("e");
        assert(d && e);
        ComputedStyle base_d = resolver.resolve_style(d, nullptr);
        ComputedStyle base_e = resolver.resolve_style(e, nullptr);
        assert(resolver.resolve_pseudo_element(d, 1, base_d).content == "42");
        assert(resolver.resolve_pseudo_element(e, 1, base_e).content == "prefix-hello-suffix");
    }

    // 4. attr() with fallback when attribute is missing
    {
        const std::string html =
            "<!doctype html><html><head><style>"
            "#f::before{content:attr(data-missing, \"fallback\")}"
            "#g::before{content:attr(data-missing, 'fb2')}"
            "#h::before{content:attr(data-present, \"fallback\")}"
            "</style></head><body>"
            "<div id=f></div><div id=g></div><div id=h data-present=\"yes\"></div>"
            "</body></html>";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 800, 600);
        auto f = doc->get_element_by_id("f");
        auto g = doc->get_element_by_id("g");
        auto h = doc->get_element_by_id("h");
        assert(f && g && h);
        assert(resolver.resolve_pseudo_element(f, 1, resolver.resolve_style(f, nullptr)).content == "fallback");
        assert(resolver.resolve_pseudo_element(g, 1, resolver.resolve_style(g, nullptr)).content == "fb2");
        assert(resolver.resolve_pseudo_element(h, 1, resolver.resolve_style(h, nullptr)).content == "yes");
    }

    // 5. open-quote / close-quote produce quote characters
    {
        const std::string html =
            "<!doctype html><html><head><style>"
            "#i::before{content:open-quote \"hi\" close-quote}"
            "</style></head><body><div id=i></div></body></html>";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 800, 600);
        auto el = doc->get_element_by_id("i");
        assert(el);
        std::string c = resolver.resolve_pseudo_element(el, 1, resolver.resolve_style(el, nullptr)).content;
        assert(c == "\"hi\"");
    }

    // 6. url() and counter() items are skipped but do not invalidate the list
    {
        const std::string html =
            "<!doctype html><html><head><style>"
            "#j::before{content:\"a\" url(\"x.png\") \"b\"}"
            "#k::before{content:\"a\" counter(my) \"b\"}"
            "</style></head><body><div id=j></div><div id=k></div></body></html>";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 800, 600);
        auto j = doc->get_element_by_id("j");
        auto k = doc->get_element_by_id("k");
        assert(j && k);
        assert(resolver.resolve_pseudo_element(j, 1, resolver.resolve_style(j, nullptr)).content == "ab");
        assert(resolver.resolve_pseudo_element(k, 1, resolver.resolve_style(k, nullptr)).content == "ab");
    }

    // 7. normal / none still generate nothing, and mixed list with them ignored
    {
        const std::string html =
            "<!doctype html><html><head><style>"
            "#l::before{content:normal}"
            "#m::before{content:none}"
            "#n::before{content:\"a\" none \"b\"}"
            "</style></head><body><div id=l></div><div id=m></div><div id=n></div></body></html>";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 800, 600);
        auto l = doc->get_element_by_id("l");
        auto m = doc->get_element_by_id("m");
        auto n = doc->get_element_by_id("n");
        assert(l && m && n);
        assert(resolver.resolve_pseudo_element(l, 1, resolver.resolve_style(l, nullptr)).content.empty());
        assert(resolver.resolve_pseudo_element(m, 1, resolver.resolve_style(m, nullptr)).content.empty());
        // "a" none "b" should be "ab" (none ignored)
        assert(resolver.resolve_pseudo_element(n, 1, resolver.resolve_style(n, nullptr)).content == "ab");
    }

    // 8. CSS escapes inside strings still work in a list
    {
        const std::string html =
            "<!doctype html><html><head><style>"
            "#o::before{content:\"\\f105\" \"-\" attr(data-x)}"
            "</style></head><body><div id=o data-x=\"ok\"></div></body></html>";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 800, 600);
        auto o = doc->get_element_by_id("o");
        assert(o);
        std::string c = resolver.resolve_pseudo_element(o, 1, resolver.resolve_style(o, nullptr)).content;
        // U+F105 in UTF-8 is 3 bytes EF 84 85, then "-ok"
        assert(c.size() >= 4);
        assert(static_cast<unsigned char>(c[0]) == 0xEF);
        assert(c.substr(3) == "-ok");
    }

    // 9. var() in content is resolved before attr()
    {
        const std::string html =
            "<!doctype html><html><head><style>"
            ":root{--x:\"hi\"}"
            "#p::before{content:var(--x) \"!\"}"
            "</style></head><body><div id=p></div></body></html>";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 800, 600);
        auto p = doc->get_element_by_id("p");
        assert(p);
        assert(resolver.resolve_pseudo_element(p, 1, resolver.resolve_style(p, nullptr)).content == "hi!");
    }

    // 10. Layout generates a box for attr() content and DOM textContent stays empty
    {
        const std::string html =
            "<!doctype html><html><head><style>"
            "body{margin:0}"
            "#q::before{content:\"[\" attr(data-v) \"]\"}"
            "</style></head><body><div id=q data-v=\"42\">mid</div></body></html>";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 800, 600);
        auto root = LayoutTree::build(doc, resolver, 800, 600);
        assert(root);
        bool saw = false;
        std::vector<const LayoutBox*> stack{root.get()};
        while (!stack.empty()) {
            const LayoutBox* bx = stack.back(); stack.pop_back();
            if (bx->box_type == BoxType::TextNode && bx->text == "[42]") saw = true;
            for (auto &c : bx->children) stack.push_back(c.get());
        }
        assert(saw);
        auto q = doc->get_element_by_id("q");
        assert(q);
        assert(q->text_content() == "mid");
        assert(resolver.resolve_style(q, nullptr).content.empty());
    }
}
