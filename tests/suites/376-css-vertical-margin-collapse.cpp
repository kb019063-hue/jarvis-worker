// Phase 376, finding 376: general vertical margin collapse (CSS 2.1 §8.3.1).
TEST(css_vertical_margin_collapse_general) {
    auto layout_doc = [](const std::string& html, float vw = 800.0f, float vh = 600.0f) {
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, vw, vh);
        auto root = LayoutTree::build(doc, resolver, vw, vh);
        assert(root);
        return std::make_pair(doc, root);
    };

    auto find_box = [](const std::shared_ptr<LayoutBox>& root, const std::string& id) -> const LayoutBox* {
        std::vector<const LayoutBox*> stack{root.get()};
        while (!stack.empty()) {
            const LayoutBox* box = stack.back();
            stack.pop_back();
            if (box->node && box->node->is_element() && box->node->as_element()->id() == id) {
                return box;
            }
            for (const auto& child : box->children) {
                stack.push_back(child.get());
            }
        }
        return nullptr;
    };

    auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };

    // 1. Adjacent sibling margin collapse: positive, negative, and mixed signs.
    {
        auto [doc, root] = layout_doc(
            "<!doctype html><style>"
            "body{margin:0}"
            "#a{height:20px;margin-bottom:20px}"
            "#b{height:20px;margin-top:35px;margin-bottom:-10px}"
            "#c{height:20px;margin-top:-25px;margin-bottom:30px}"
            "#d{height:20px;margin-top:-12px}"
            "</style><body>"
            "<div id=a></div><div id=b></div><div id=c></div><div id=d></div>"
            "</body>");
        const auto* a = find_box(root, "a");
        const auto* b = find_box(root, "b");
        const auto* c = find_box(root, "c");
        const auto* d = find_box(root, "d");
        assert(a && b && c && d);
        // a: 0..20
        assert(near(a->dimensions.border_box().y, 0.0f));
        // a.mb(20) + b.mt(35) -> max(20,35) = 35 -> b at 55..75
        assert(near(b->dimensions.border_box().y, 55.0f));
        // b.mb(-10) + c.mt(-25) -> min(-10,-25) = -25 -> c at 75-25 = 50..70
        assert(near(c->dimensions.border_box().y, 50.0f));
        // c.mb(30) + d.mt(-12) -> 30 + (-12) = 18 -> d at 70+18 = 88..108
        assert(near(d->dimensions.border_box().y, 88.0f));
    }

    // 2. Self-collapsing empty blocks, multi-block chains, mixed signs, and
    //    CSS 2.1 §8.3.1 border-top placement of empty blocks.
    {
        auto [doc, root] = layout_doc(
            "<!doctype html><style>"
            "body{margin:0}"
            "#a{height:10px;margin-bottom:20px}"
            "#e1{margin-top:30px;margin-bottom:50px}"
            "#e2{margin-top:-15px;margin-bottom:40px}"
            "#b{height:10px;margin-top:10px}"
            "</style><body>"
            "<div id=a></div><div id=e1></div><div id=e2></div><div id=b></div>"
            "</body>");
        const auto* a = find_box(root, "a");
        const auto* e1 = find_box(root, "e1");
        const auto* e2 = find_box(root, "e2");
        const auto* b = find_box(root, "b");
        assert(a && e1 && e2 && b);
        assert(near(a->dimensions.border_box().y, 0.0f));
        // e1 sits as if it had a non-zero bottom border: 10 + max(20, 30) = 40.
        assert(near(e1->dimensions.border_box().y, 40.0f));
        assert(near(e1->dimensions.border_box().height, 0.0f));
        // e2 sits at 10 + (max(20,30,50) + (-15)) = 10 + 35 = 45.
        assert(near(e2->dimensions.border_box().y, 45.0f));
        // b collapses all 6 margins: max(20,30,50,40,10) + (-15) = 50 - 15 = 35 -> 10 + 35 = 45.
        assert(near(b->dimensions.border_box().y, 45.0f));
    }

    // 3. Leading empty block chain collapsing through <body> and nested empty blocks,
    //    plus empty inline block `<div><span></span></div>`.
    {
        auto [doc, root] = layout_doc(
            "<!doctype html><style>"
            "body{margin:8px}"
            "#e1{margin-top:20px;margin-bottom:25px}"
            "#e2{margin-top:-5px;margin-bottom:30px}"
            "#e3{margin-top:12px;margin-bottom:15px}"
            "#target{margin-top:10px;height:20px}"
            "</style><body>"
            "<div id=e1><div id=e2></div></div>"
            "<div id=e3><span></span></div>"
            "<div id=target></div>"
            "</body>");
        const auto* e1 = find_box(root, "e1");
        const auto* target = find_box(root, "target");
        assert(e1 && target);
        // Collapsed top = max(8, 20, 25, 30, 12, 15, 10) + (-5) = 25.
        assert(near(root->dimensions.border_box().y, 25.0f));
        // Empty block collapsing with parent's top has same top border edge as parent.
        assert(near(e1->dimensions.border_box().y, 25.0f));
        assert(near(target->dimensions.border_box().y, 25.0f));
        assert(near(root->dimensions.border_box().height, 20.0f));
    }

    // 4. Parent-bottom / last-child-bottom collapse and all barrier conditions:
    //    auto height (collapses), definite height (stops), min-height > 0 (stops
    //    collapse without growing auto height), max-height clamp (stops),
    //    padding-bottom / border-bottom / overflow:hidden (stops & includes margin).
    {
        auto [doc, root] = layout_doc(
            "<!doctype html><style>"
            "body{margin:0}"
            "#p_auto{margin-bottom:10px}"
            "#p_auto_c{height:20px;margin-bottom:40px}"
            "#after_auto{height:10px;margin-top:15px}"
            "</style><body>"
            "<div id=p_auto><div id=p_auto_c></div></div>"
            "<div id=after_auto></div>"
            "</body>");
        const auto* p = find_box(root, "p_auto");
        const auto* after = find_box(root, "after_auto");
        assert(p && after);
        assert(near(p->dimensions.border_box().height, 20.0f));
        // max(10, 40, 15) = 40 -> 20 + 40 = 60.
        assert(near(after->dimensions.border_box().y, 60.0f));
    }
    {
        auto [doc, root] = layout_doc(
            "<!doctype html><style>"
            "body{margin:0}"
            "#p_min{min-height:50px;margin-bottom:10px}"
            "#p_min_c{height:20px;margin-bottom:40px}"
            "#after_min{height:10px;margin-top:5px}"
            "</style><body>"
            "<div id=p_min><div id=p_min_c></div></div>"
            "<div id=after_min></div>"
            "</body>");
        const auto* p = find_box(root, "p_min");
        const auto* after = find_box(root, "after_min");
        assert(p && after);
        // Auto height is 20 (child's border bottom), clamped to min-height 50 (not 60).
        assert(near(p->dimensions.border_box().height, 50.0f));
        // Bottom margin does not collapse with child: 50 + max(10, 5) = 60.
        assert(near(after->dimensions.border_box().y, 60.0f));
    }
    {
        auto [doc, root] = layout_doc(
            "<!doctype html><style>"
            "body{margin:0}"
            "#p_def{height:30px;margin-bottom:10px}"
            "#p_def_c{height:20px;margin-bottom:40px}"
            "#after_def{height:10px}"
            "#p_max{max-height:15px;margin-bottom:10px}"
            "#p_max_c{height:20px;margin-bottom:40px}"
            "#after_max{height:10px}"
            "#p_pad{padding-bottom:1px;margin-bottom:10px}"
            "#p_pad_c{height:20px;margin-bottom:40px}"
            "#after_pad{height:10px}"
            "#p_bfc{overflow:hidden;margin-bottom:10px}"
            "#p_bfc_c{height:20px;margin-bottom:40px}"
            "#after_bfc{height:10px}"
            "</style><body>"
            "<div id=p_def><div id=p_def_c></div></div><div id=after_def></div>"
            "<div id=p_max><div id=p_max_c></div></div><div id=after_max></div>"
            "<div id=p_pad><div id=p_pad_c></div></div><div id=after_pad></div>"
            "<div id=p_bfc><div id=p_bfc_c></div></div><div id=after_bfc></div>"
            "</body>");
        const auto* p_def = find_box(root, "p_def");
        const auto* after_def = find_box(root, "after_def");
        const auto* p_max = find_box(root, "p_max");
        const auto* after_max = find_box(root, "after_max");
        const auto* p_pad = find_box(root, "p_pad");
        const auto* after_pad = find_box(root, "after_pad");
        const auto* p_bfc = find_box(root, "p_bfc");
        const auto* after_bfc = find_box(root, "after_bfc");
        assert(p_def && after_def && p_max && after_max && p_pad && after_pad && p_bfc && after_bfc);

        // Definite height stops bottom collapse: 30 + 10 = 40.
        assert(near(p_def->dimensions.border_box().height, 30.0f));
        assert(near(after_def->dimensions.border_box().y, 40.0f));
        // max-height clamping stops bottom collapse: p_max at 50..65, after_max at 65+10 = 75.
        assert(near(p_max->dimensions.border_box().height, 15.0f));
        assert(near(after_max->dimensions.border_box().y, 75.0f));
        // padding-bottom stops bottom collapse and contains child's bottom margin: 20 + 40 + 1 = 61.
        assert(near(p_pad->dimensions.border_box().height, 61.0f));
        assert(near(after_pad->dimensions.border_box().y,
                    p_pad->dimensions.border_box().y + 61.0f + 10.0f));
        // overflow:hidden (BFC) stops bottom collapse and contains child's bottom margin: 20 + 40 = 60.
        assert(near(p_bfc->dimensions.border_box().height, 60.0f));
        assert(near(after_bfc->dimensions.border_box().y,
                    p_bfc->dimensions.border_box().y + 60.0f + 10.0f));
    }

    // 5. Floats and clearance (CSS 2.1 §9.5.2): both non-empty and self-collapsing
    //    clearing boxes.
    {
        auto [doc, root] = layout_doc(
            "<!doctype html><style>"
            "body{margin:0}"
            "#wrap{margin-bottom:5px}"
            "#flt{float:left;width:50px;height:100px}"
            "#clr{clear:left;margin-top:20px;margin-bottom:20px}"
            "#after_clr{height:20px;margin-top:10px}"
            "#outside{height:20px}"
            "</style><body>"
            "<div id=wrap>"
            "  <div id=flt></div>"
            "  <div id=clr></div>"
            "  <div id=after_clr></div>"
            "</div>"
            "<div id=outside></div>"
            "</body>");
        const auto* clr = find_box(root, "clr");
        const auto* after_clr = find_box(root, "after_clr");
        assert(clr && after_clr);
        // clr clears the 100px float: top border edge is at y=100.
        assert(near(clr->dimensions.border_box().y, 100.0f));
        // Clearance sits above clr's 20px top margin, so after_clr (mt=10) sits at 100, not 120.
        assert(near(after_clr->dimensions.border_box().y, 100.0f));
    }
    {
        // Self-collapsing clearing last child: its bottom margin must NOT collapse
        // with its parent's bottom margin (CSS 2.1 §8.3.1), and parent auto height
        // grows to contain it.
        auto [doc, root] = layout_doc(
            "<!doctype html><style>"
            "body{margin:0}"
            "#wrap{margin-bottom:10px}"
            "#flt{float:left;width:50px;height:100px}"
            "#clr{clear:left;margin-top:20px;margin-bottom:50px}"
            "#outside{height:20px}"
            "</style><body>"
            "<div id=wrap><div id=flt></div><div id=clr></div></div>"
            "<div id=outside></div>"
            "</body>");
        const auto* wrap = find_box(root, "wrap");
        const auto* clr = find_box(root, "clr");
        const auto* outside = find_box(root, "outside");
        assert(wrap && clr && outside);
        assert(near(clr->dimensions.border_box().y, 100.0f));
        // Clearance ends at 80, self-collapsed margin is 50 -> wrap height = 80 + 50 = 130.
        assert(near(wrap->dimensions.border_box().height, 130.0f));
        // wrap.mb(10) does not collapse with clr.mb(50): outside sits at 130 + 10 = 140.
        assert(near(outside->dimensions.border_box().y, 140.0f));
    }

    // 6. Flex and Grid containers: outer margins collapse with block siblings,
    //    inner items do not collapse out of flex/grid items or containers.
    {
        auto [doc, root] = layout_doc(
            "<!doctype html><style>"
            "body{margin:0}"
            "#before{height:20px;margin-bottom:30px}"
            "#flex{display:flex;margin-top:20px;margin-bottom:40px}"
            "#item{margin-top:0}"
            "#inner{margin-top:100px;height:10px}"
            "#after{height:20px;margin-top:15px}"
            "</style><body>"
            "<div id=before></div>"
            "<div id=flex><div id=item><div id=inner></div></div></div>"
            "<div id=after></div>"
            "</body>");
        const auto* flex = find_box(root, "flex");
        const auto* item = find_box(root, "item");
        const auto* inner = find_box(root, "inner");
        const auto* after = find_box(root, "after");
        assert(flex && item && inner && after);
        // before.mb(30) collapses with flex.mt(20) -> flex at 20 + 30 = 50.
        assert(near(flex->dimensions.border_box().y, 50.0f));
        // inner's margin-top:100 stays inside the flex item (does not escape).
        assert(near(item->dimensions.border_box().y, 50.0f));
        assert(near(inner->dimensions.border_box().y, 150.0f));
        assert(near(flex->dimensions.border_box().height, 110.0f));
        // flex.mb(40) collapses with after.mt(15) -> after at 50 + 110 + 40 = 200.
        assert(near(after->dimensions.border_box().y, 200.0f));
    }

    // 7. Rendered pixel check: sibling and bottom collapse reach the bitmap.
    {
        const std::string html =
            "<!doctype html><style>"
            "body{margin:0;background:#ffffff}"
            "#a{width:40px;height:10px;background:#ff0000;margin-bottom:20px}"
            "#wrap{margin-bottom:10px}"
            "#child{width:40px;height:10px;background:#00ff00;margin-top:30px;margin-bottom:25px}"
            "#c{width:40px;height:10px;background:#0000ff;margin-top:15px}"
            "</style><body>"
            "<div id=a></div>"
            "<div id=wrap><div id=child></div></div>"
            "<div id=c></div>"
            "</body>";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 100, 100);
        auto root = LayoutTree::build(doc, resolver, 100, 100);
        assert(root);
        BitmapRenderer renderer(100, 100);
        renderer.clear(Color::parse("#ffffff"));
        renderer.render(DisplayList::build(root));
        // a: y=0..10 (red); gap = max(20, 30) = 30 -> child: y=40..50 (green);
        // gap = max(25, 10, 15) = 25 -> c: y=75..85 (blue).
        assert(renderer.get_pixel(5, 5) == Color::parse("#ff0000"));
        assert(renderer.get_pixel(5, 39) == Color::parse("#ffffff"));
        assert(renderer.get_pixel(5, 40) == Color::parse("#00ff00"));
        assert(renderer.get_pixel(5, 74) == Color::parse("#ffffff"));
        assert(renderer.get_pixel(5, 75) == Color::parse("#0000ff"));
    }
}
