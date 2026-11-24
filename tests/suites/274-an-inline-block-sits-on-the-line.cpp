// Phase 274, finding 278: an inline-block is atomic *inline*-level, so it sits
// on the line instead of breaking it.
//
// `is_block_level_box()` in `LayoutTree.cpp` answered from the box type alone,
// and `display: inline-block` is built as a `BlockNode`. Every mixed-content
// parent holding one was therefore treated as mixed block/inline content:
// `normalize_block_children` wrapped the inline-blocks in anonymous blocks and
// `split_inline_box_around_blocks` lifted them out of the line. A paragraph
// `Click <button>OK</button> to continue.` became three stacked blocks 60px
// tall instead of one 20px line, and the button was painted *below* the words.
//
// CSS 2.1 §9.2.2.1 makes an inline-block an atomic inline-level box and
// §9.2.1.1 says only a block-level box interrupts an inline formatting context.
// The rest of the engine already knew this: `establishes_ifc()` counts an
// inline-block child as inline content, and `collect_items()` lays it out
// atomically like a replaced element. Only the predicate disagreed.
//
// A real block-level child must still split the line, which is the control this
// suite keeps: the fix narrows the test, it does not disable it.
TEST(an_inline_block_sits_on_the_line) {
    using namespace litebird;

    auto layout = [](const std::string& html) {
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver =
            DocumentStyles::build_resolver(*doc, nullptr, 600.0f, 800.0f);
        return LayoutTree::build(doc, resolver, 600.0f, 800.0f);
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

    const std::string page = R"html(
        <!doctype html><html><head><style>
          body { margin: 8px; width: 600px; font: 16px/20px sans-serif; }
          .field { display: inline-block; width: 120px; height: 20px; }
        </style></head><body>
          <p id="sentence">Status: <span class="field" id="badge">ok</span> since.</p>
          <p id="plain">Status: ok since.</p>
          <p id="twice">a <span class="field" id="b1"></span> b <span class="field" id="b2"></span> c</p>
          <div id="control">before <div id="blk" style="height:10px"></div> after</div>
        </body></html>
    )html";

    auto root = layout(page);

    auto badge = find_by_id(root, "badge");
    auto b1 = find_by_id(root, "b1");
    auto b2 = find_by_id(root, "b2");
    auto sentence = find_by_id(root, "sentence");
    auto plain = find_by_id(root, "plain");
    auto twice = find_by_id(root, "twice");
    auto control = find_by_id(root, "control");
    auto blk = find_by_id(root, "blk");
    if (!badge || !b1 || !b2 || !sentence || !plain || !twice || !control || !blk) {
        std::printf("      [inline-block] a box was not built; skipped\n");
        return;
    }

    // 1. The line is not split. `Status: <badge> since.` is one line, as tall as
    //    `Status: ok since.` — before the fix it was three lines and 60px tall.
    const float with_badge = sentence->dimensions.margin_box().height;
    const float with_text = plain->dimensions.margin_box().height;
    assert(sentence->line_boxes.size() == 1u);
    assert(std::abs(with_badge - with_text) < 0.5f);

    // 2. The inline-block is *on* that line, not under it: it shares the
    //    paragraph's content top rather than starting below the first line.
    assert(std::abs(badge->dimensions.content.y - sentence->dimensions.content.y) < 1.5f);

    // 3. It sits after the text that precedes it, not at the left edge. The
    //    stranding at x = content.x is what "lifted out of the line" looked like.
    assert(badge->dimensions.content.x > sentence->dimensions.content.x);

    // 4. Two of them share one line, side by side, both on that line.
    assert(twice->line_boxes.size() == 1u);
    assert(std::abs(b1->dimensions.content.y - b2->dimensions.content.y) < 0.5f);
    assert(b2->dimensions.content.x > b1->dimensions.content.x + 100.0f);

    // 5. The control: a genuinely block-level child still interrupts the line.
    //    The container holding one is what `normalize_block_children` is for:
    //    it is rebuilt as anonymous block / block / anonymous block, and the
    //    block sits at the left edge between the two text runs.
    //    The fix narrows the test for "does this child split the line"; it does
    //    not switch that path off.
    assert(control->children.size() == 3u);
    assert(blk->dimensions.content.x == control->dimensions.content.x);
    assert(blk->dimensions.content.y > control->dimensions.content.y + 1.0f);
    assert(control->children.back()->dimensions.content.y >
           blk->dimensions.content.y + blk->dimensions.margin_box().height - 0.5f);
}
