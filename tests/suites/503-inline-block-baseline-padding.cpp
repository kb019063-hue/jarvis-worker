// Phase 503, finding 503: an inline-block's own top padding moves its
// content baseline, and that baseline must move the parent line with it.
TEST(inline_block_baseline_tracks_its_own_padding) {
    using namespace litebird;

    auto doc = HtmlParser::parse(R"html(
        <!doctype html><html><head><style>
          body { margin: 0; width: 300px; font: 16px/20px sans-serif; }
          p { margin: 0; }
          .padded { display: inline-block; padding-top: 10px; }
        </style></head><body>
          <p id="line">before <span class="padded" id="box">inside</span> after</p>
        </body></html>
    )html");
    StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 300.0f, 200.0f);
    auto root = LayoutTree::build(doc, resolver, 300.0f, 200.0f);

    std::shared_ptr<LayoutBox> line_box;
    std::shared_ptr<LayoutBox> inline_block;
    std::function<void(const std::shared_ptr<LayoutBox>&)> find =
        [&](const std::shared_ptr<LayoutBox>& box) {
            if (!box) return;
            if (box->node && box->node->is_element()) {
                const auto id = box->node->as_element()->get_attribute("id");
                if (id == "line") line_box = box;
                if (id == "box") inline_block = box;
            }
            for (const auto& child : box->children) find(child);
        };
    find(root);
    assert(line_box && inline_block);
    assert(line_box->line_boxes.size() == 1u);
    assert(!inline_block->line_boxes.empty());

    const InlineFragment* before = nullptr;
    for (const auto& fragment : line_box->line_boxes.front().fragments) {
        if (fragment.text == "before") {
            before = &fragment;
            break;
        }
    }
    assert(before);

    const auto& parent_line = line_box->line_boxes.front();
    const auto& child_line = inline_block->line_boxes.back();
    const InlineFragment* inside = nullptr;
    for (const auto& fragment : child_line.fragments) {
        if (fragment.text == "inside") {
            inside = &fragment;
            break;
        }
    }
    assert(inside);

    // Paint uses the same face baseline in both fragments. Aligning their top
    // positions therefore aligns the baselines; the old centering heuristic
    // left the inner baseline five pixels below the surrounding text here.
    assert(std::abs(before->rect.y - inside->rect.y) < 0.5f);
    assert(std::abs((parent_line.rect.y + parent_line.baseline) -
                    (child_line.rect.y + child_line.baseline)) < 0.5f);
    assert(parent_line.baseline > 20.0f);
    assert(std::abs(parent_line.rect.height - 30.0f) < 0.5f);
}
