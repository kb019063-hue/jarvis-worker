// Phase 389, finding 389: a modal dialog uses the HTML UA rule's
// overflow:auto, clipping its contents at the padding box while leaving its
// own border and background visible.
TEST(modal_dialog_overflow_auto_clips_contents_but_not_its_chrome) {
    using namespace litebird;

    const std::string html = R"html(
        <!doctype html>
        <html><body style="margin:0">
          <dialog id="modal" style="width:40px;height:40px;padding:4px;
              border:4px solid rgb(255,0,0);background-color:rgb(0,0,255)">
            <div style="width:200px;height:200px;background-color:rgb(0,255,0)"></div>
          </dialog>
          <dialog id="ordinary"></dialog>
        </body></html>
    )html";

    auto doc = HtmlParser::parse(html);
    auto modal = doc->get_element_by_id("modal");
    auto ordinary = doc->get_element_by_id("ordinary");
    assert(modal && ordinary);
    modal->set_attribute("open", "");
    doc->set_modal_dialog(modal);
    modal->set_modal_dialog(true);

    StyleResolver resolver = DocumentStyles::build_resolver(*doc, nullptr, 100.0f, 100.0f);
    assert(resolver.resolve_style(modal).overflow == Overflow::Auto);
    // The overflow declaration belongs to :modal, not to every open dialog.
    assert(resolver.resolve_style(ordinary).overflow == Overflow::Visible);

    auto root = LayoutTree::build(doc, resolver, 100.0f, 100.0f);
    assert(root);
    std::function<std::shared_ptr<LayoutBox>(const std::shared_ptr<LayoutBox>&)> find_modal =
        [&](const std::shared_ptr<LayoutBox>& box) -> std::shared_ptr<LayoutBox> {
            if (!box) return nullptr;
            if (box->node.get() == modal.get()) return box;
            for (const auto& child : box->children) {
                if (auto found = find_modal(child)) return found;
            }
            return nullptr;
        };
    const auto box = find_modal(root);
    assert(box);
    assert(box->style->overflow == Overflow::Auto);

    const Rect border = box->dimensions.border_box();
    const Rect padding = box->dimensions.padding_box();
    DisplayList list = DisplayList::build(root, modal.get());
    int clip_count = 0;
    int pop_count = 0;
    bool modal_padding_clip = false;
    for (const auto& command : list.commands()) {
        if (std::holds_alternative<PushClipCommand>(command)) {
            ++clip_count;
            const auto& clip = std::get<PushClipCommand>(command);
            if (clip.rect.x == padding.x && clip.rect.y == padding.y &&
                clip.rect.width == padding.width && clip.rect.height == padding.height) {
                modal_padding_clip = true;
            }
        }
        if (std::holds_alternative<PopClipCommand>(command)) ++pop_count;
    }
    assert(clip_count == 1 && pop_count == 1);
    assert(modal_padding_clip);

    // The modal is centered at this viewport size. A green 200px child paints
    // inside the dialog; outside its padding-box clip the backdrop, not that
    // child, must remain visible. The red border is chrome and must not be
    // eaten by the contents clip.
    BitmapRenderer renderer(100, 100);
    renderer.clear(Color::white());
    renderer.render(list);

    const int border_x = static_cast<int>(border.x + border.width / 2.0f);
    const int border_y = static_cast<int>(border.y + 1.0f);
    const Color border_pixel = renderer.get_pixel(border_x, border_y);
    assert(border_pixel.r > 240 && border_pixel.g < 20 && border_pixel.b < 20);

    const int padding_x = static_cast<int>(border.x + box->dimensions.border.left + 1.0f);
    const int padding_y = static_cast<int>(border.y + box->dimensions.border.top + 1.0f);
    const Color background_pixel = renderer.get_pixel(padding_x, padding_y);
    assert(background_pixel.b > 240 && background_pixel.r < 20 && background_pixel.g < 20);

    const int child_y = static_cast<int>(padding.y + box->dimensions.padding.top + 10.0f);
    const int child_x = static_cast<int>(padding.x + box->dimensions.padding.left + 10.0f);
    const Color inside_child = renderer.get_pixel(child_x, child_y);
    assert(inside_child.g > 240 && inside_child.r < 20 && inside_child.b < 20);

    const int outside_right_x = static_cast<int>(border.right()) + 2;
    const Color outside_right = renderer.get_pixel(outside_right_x, child_y);
    assert(outside_right.r > 210 && outside_right.g > 210 && outside_right.b > 210);
    assert(outside_right.g < 245); // the uncovered page is dimmed by the backdrop

    const int outside_bottom_y = static_cast<int>(border.bottom()) + 2;
    const Color outside_bottom = renderer.get_pixel(child_x, outside_bottom_y);
    assert(outside_bottom.r > 210 && outside_bottom.g > 210 && outside_bottom.b > 210);
    assert(outside_bottom.g < 245);
}
