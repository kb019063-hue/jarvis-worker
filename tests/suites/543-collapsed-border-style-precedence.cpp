// Phase 543, finding 543: CSS 2.1 collapsed-border conflict resolution uses
// style precedence after width, and the winning style reaches both cell edges
// so the shared border is painted in the winning style.
TEST(collapsed_border_equal_width_uses_css_style_precedence) {
    using namespace litebird;

    const std::vector<std::pair<std::string, std::string>> conflicts = {
        {"dashed", "solid"}, {"solid", "double"}, {"dotted", "dashed"},
        {"ridge", "dotted"}, {"outset", "ridge"}, {"groove", "outset"},
        {"inset", "groove"}, {"none", "inset"}, {"double", "hidden"}
    };
    const std::vector<std::string> winners = {
        "solid", "double", "dashed", "dotted", "ridge", "outset", "groove", "inset", "hidden"
    };
    const std::map<std::string, BorderStyle> parsed_styles = {
        {"none", BorderStyle::None}, {"inset", BorderStyle::Inset},
        {"groove", BorderStyle::Groove}, {"outset", BorderStyle::Outset},
        {"ridge", BorderStyle::Ridge}, {"dotted", BorderStyle::Dotted},
        {"dashed", BorderStyle::Dashed}, {"solid", BorderStyle::Solid},
        {"double", BorderStyle::Double}, {"hidden", BorderStyle::Hidden}
    };

    for (std::size_t i = 0; i < conflicts.size(); ++i) {
        const auto& [left_style, right_style] = conflicts[i];
        const std::string html = "<body style='margin:0'><table style='border-collapse:collapse'>"
            "<tr><td id='left' style='width:40px;height:30px;padding:0;"
            "border-top-width:0;border-bottom-width:0;border-left-width:0;"
            "border-right-width:4px;border-right-color:red;border-right-style:" + left_style + "'></td>"
            "<td id='right' style='width:40px;height:30px;padding:0;"
            "border-top-width:0;border-bottom-width:0;border-right-width:0;"
            "border-left-width:4px;border-left-color:blue;border-left-style:" + right_style + "'></td>"
            "</tr></table></body>";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver;
        auto root = LayoutTree::build(doc, resolver, 160.0f, 100.0f);
        assert(root);
        auto find_cell = [&](const std::string& id) {
            std::shared_ptr<LayoutBox> found;
            std::function<void(const std::shared_ptr<LayoutBox>&)> walk =
                [&](const std::shared_ptr<LayoutBox>& box) {
                    if (!box || found) return;
                    if (box->node && box->node->is_element() &&
                        box->node->as_element()->get_attribute("id") == id) {
                        found = box;
                        return;
                    }
                    for (const auto& child : box->children) walk(child);
                };
            walk(root);
            return found;
        };
        const auto left = find_cell("left");
        const auto right = find_cell("right");
        assert(left && right);
        assert(left->border_override && right->border_override);
        const BorderStyle expected = parsed_styles.at(winners[i]);
        assert(left->border_paint_override.styles.right == expected);
        assert(right->border_paint_override.styles.left == expected);
        assert(left->border_override->right == 4.0f);
        assert(right->border_override->left == 4.0f);

        if (i == 0) {
            // The winning style and colour are not just stored in layout: the
            // renderer receives the solid blue edge instead of dashed red.
            BitmapRenderer bitmap(160, 100);
            bitmap.clear(Color::white());
            bitmap.render(DisplayList::build(root));
            const auto shared = left->dimensions.border_box().x + left->dimensions.border_box().width;
            const auto y = left->dimensions.border_box().y + 8.0f;
            const Color pixel = bitmap.get_pixel(static_cast<int>(shared), static_cast<int>(y));
            assert(pixel.b > 200 && pixel.r < 60 && pixel.g < 60);
        }
    }
}
