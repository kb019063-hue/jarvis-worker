// Phase 281, finding 286: CSS Sizing's fit-content size is
// min(max-content, max(min-content, available space)), not min-content alone.
// Dialogs exposed the bug: a multiword label was squeezed to its widest word
// even when the containing block had room for the whole line.
TEST(fit_content_uses_max_content_before_clamping) {
    using namespace litebird;

    const std::string html = R"html(
        <!doctype html><html><head><style>
          body { margin: 0; font-size: 16px; }
          p { margin: 0; }
          #narrow-container { width: 150px; }
          #narrow-fit { width: fit-content; }
          #too-narrow-container { width: 20px; }
          #too-narrow-fit { width: fit-content; }
          #inline-fit { display: inline-block; }
        </style></head><body>
          <dialog id="dialog" open><p id="dialog-text">Favorite animal: choose a favorite.</p></dialog>
          <div id="narrow-container"><div id="narrow-fit">wombat kangaroo capybara</div></div>
          <div id="too-narrow-container"><div id="too-narrow-fit">wombat</div></div>
          <div><span id="inline-fit">wombat kangaroo capybara</span></div>
          <div id="float-fit" style="float:left">wombat kangaroo capybara</div>
        </body></html>
    )html";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 800.0f, 600.0f);
    auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
    assert(root);

    auto find_by_id = [](const std::shared_ptr<LayoutBox>& tree,
                         const std::string& id) -> std::shared_ptr<LayoutBox> {
        std::shared_ptr<LayoutBox> found;
        std::function<void(const std::shared_ptr<LayoutBox>&)> visit =
            [&](const std::shared_ptr<LayoutBox>& box) {
                if (!box || found) return;
                if (box->node && box->node->is_element() &&
                    box->node->as_element()->get_attribute("id") == id) {
                    found = box;
                    return;
                }
                for (const auto& child : box->children) visit(child);
            };
        visit(tree);
        return found;
    };

    const auto dialog = find_by_id(root, "dialog");
    const auto dialog_text = find_by_id(root, "dialog-text");
    const auto narrow_container = find_by_id(root, "narrow-container");
    const auto narrow_fit = find_by_id(root, "narrow-fit");
    const auto too_narrow_fit = find_by_id(root, "too-narrow-fit");
    const auto inline_fit = find_by_id(root, "inline-fit");
    const auto float_fit = find_by_id(root, "float-fit");
    assert(dialog && dialog_text && narrow_container && narrow_fit && too_narrow_fit &&
           inline_fit && float_fit);

    // With ample room, a dialog's fit-content width is its full unwrapped
    // content width. The old min-content-only calculation made the label wrap
    // at its longest word and made the dialog that narrow as well.
    assert(dialog->style->width_fit_content);
    assert(dialog_text->line_boxes.size() == 1u);
    assert(dialog->dimensions.content.width > 150.0f);

    // When available space falls between the min- and max-content sizes,
    // fit-content uses that available width rather than collapsing to the
    // longest word. This also checks that the lower bound still permits the
    // unbreakable words in the line.
    assert(narrow_fit->style->width_fit_content);
    assert(narrow_fit->dimensions.content.width > 100.0f);
    assert(narrow_fit->dimensions.content.width < narrow_container->dimensions.content.width + 0.5f);

    // If even the available width is below the min-content floor, the
    // unbreakable word sets the width instead of being squeezed below itself.
    assert(too_narrow_fit->dimensions.content.width > 20.0f);

    // CSS 2.1 shrink-to-fit for an auto-width inline-block uses the same
    // max-content / min-content bounds when the containing block is wide.
    assert(inline_fit->style->is_inline_block());
    assert(inline_fit->line_boxes.size() == 1u);
    assert(inline_fit->dimensions.content.width > 150.0f);

    // Floats use CSS 2.1's same shrink-to-fit formula, with the available
    // width supplied by the remaining float band.
    assert(float_fit->style->is_floated());
    assert(float_fit->line_boxes.size() == 1u);
    assert(float_fit->dimensions.content.width > 150.0f);

    std::printf("      [fit-content] dialog %.1fpx; constrained %.1f/150px; "
                "inline-block %.1fpx; float %.1fpx; min-content floor %.1f/20px\n",
                dialog->dimensions.content.width, narrow_fit->dimensions.content.width,
                inline_fit->dimensions.content.width, float_fit->dimensions.content.width,
                too_narrow_fit->dimensions.content.width);
}
