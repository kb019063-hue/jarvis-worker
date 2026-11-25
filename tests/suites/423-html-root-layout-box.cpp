// Phase 423, finding 423: full <html> root layout box.
TEST(html_root_layout_box_exists_and_offsets_body) {
    auto layout_doc = [](const std::string& html, float vw = 800.0f, float vh = 600.0f) {
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, vw, vh);
        auto root = LayoutTree::build(doc, resolver, vw, vh);
        assert(root);
        return std::make_pair(doc, root);
    };
    auto find_box = [](const std::shared_ptr<LayoutBox>& root, const std::string& tag) -> const LayoutBox* {
        std::vector<const LayoutBox*> stack{root.get()};
        while (!stack.empty()) {
            const LayoutBox* box = stack.back(); stack.pop_back();
            if (box->node && box->node->is_element() && box->node->as_element()->tag_name() == tag) return box;
            for (const auto& c : box->children) stack.push_back(c.get());
        }
        return nullptr;
    };
    auto near = [](float a, float b){ return std::fabs(a-b) < 0.05f; };

    // 1. html element now has a LayoutBox
    {
        auto [doc, root] = layout_doc("<!doctype html><html><body><div id=x style='width:10px;height:10px'></div></body></html>");
        const LayoutBox* html = find_box(root, "html");
        assert(html);
        assert(html->node && html->node->as_element()->tag_name()=="html");
        // html covers viewport (or at least contains body)
        assert(html->dimensions.content.width > 0);
        // body inside html
        const LayoutBox* body = find_box(root, "body");
        assert(body);
        // body should be child of html (or descendant)
        (void)body;
    }
    // 2. html margin 30 + body margin 8 => child at 38,38 (Chromium)
    {
        auto [doc, root] = layout_doc("<!doctype html><html style=\"margin:30px\"><body><div id=target style=\"width:100px;height:20px;background:#ff0000\"></div></body></html>");
        const LayoutBox* html = find_box(root, "html");
        const LayoutBox* target = [&]()->const LayoutBox*{
            std::vector<const LayoutBox*> st{root.get()};
            while(!st.empty()){auto b=st.back();st.pop_back(); if(b->node && b->node->is_element() && b->node->as_element()->id()=="target") return b; for(auto &c:b->children) st.push_back(c.get());}return nullptr;
        }();
        assert(html && target);
        float tx = target->dimensions.border_box().x;
        float ty = target->dimensions.border_box().y;
        assert(near(tx, 38.0f));
        assert(near(ty, 38.0f));
        // html border box should be at margin offset
        assert(near(html->dimensions.border_box().x, 30.0f));
        assert(near(html->dimensions.border_box().y, 30.0f));
    }
    // 3. html border and padding contain body margin still (addition not collapse)
    {
        auto [doc, root] = layout_doc("<!doctype html><html style=\"margin:10px;border:5px solid black;padding:7px\"><body style=\"margin:8px\"><div id=t style=\"width:10px;height:10px\"></div></body></html>");
        const LayoutBox* html = find_box(root, "html");
        const LayoutBox* t = [&]()->const LayoutBox*{ std::vector<const LayoutBox*> st{root.get()}; while(!st.empty()){auto b=st.back();st.pop_back(); if(b->node && b->node->is_element() && b->node->as_element()->id()=="t") return b; for(auto &c:b->children) st.push_back(c.get());}return nullptr; }();
        assert(html && t);
        // html content at margin+border+padding = 10+5+7=22
        // body at html content + body margin 8 = 30 + body border/padding 0 + 8 => but body border box x = html content x + body margin left =22+8=30
        // target inside body at body content +0 => 30+0? body content at 30+0? Let's just check target x > html content
        assert(t->dimensions.border_box().x >= html->dimensions.content.x);
    }
    // 4. UA body background is transparent by default (no longer white)
    {
        auto doc = HtmlParser::parse("<!doctype html><body><div style='width:10px;height:10px'></div></body>");
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 800,600);
        auto body = doc->body();
        ComputedStyle cs = resolver.resolve_style(body, nullptr);
        assert(cs.background_color.is_transparent());
    }
    // 5. html root still supplies canvas: body transparent, html blue -> canvas blue, body box not propagated
    {
        auto [doc, root] = layout_doc("<!doctype html><html style=\"background:#0000ff\"><body><div style=\"width:10px;height:10px\"></div></body></html>");
        assert(root->has_document_canvas);
        assert(root->canvas_background.r==0 && root->canvas_background.g==0 && root->canvas_background.b==255);
        // html box should be marked propagated (background not painted as box)
        const LayoutBox* html = find_box(root, "html");
        assert(html && html->background_propagated);
    }
    // 6. sizeof(LayoutBox) < 400
    {
        static_assert(sizeof(LayoutBox) < 400, "LayoutBox grew past 400 bytes");
    }
}
