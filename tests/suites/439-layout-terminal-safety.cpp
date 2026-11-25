// Phase 439, finding 439: layout dump fields are terminal-facing, not data.
TEST(layout_dump_filters_page_controls_without_changing_layout_data) {
    using namespace litebird;
    const std::string attack = "A\xc4\x9b\x1b]0;OWN\x07\r\xc2\x9bZ";
    const std::string clean = "A\xc4\x9b\xef\xbf\xbd]0;OWN\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbdZ";
    auto element = std::make_shared<Element>("div");
    element->set_attribute("id", attack);
    auto root = LayoutBox::create_block(element, nullptr);
    root->text = attack;
    auto one_line = LayoutBox::create_text(nullptr, "unused", nullptr);
    one_line->lines = {attack};
    auto multiple_lines = LayoutBox::create_text(nullptr, "unused", nullptr);
    multiple_lines->lines = {attack, "second"};
    auto ifc = LayoutBox::create_block(nullptr, nullptr);
    ifc->line_boxes.emplace_back();
    ifc->text = attack;
    ifc->lines = {attack};
    root->append_child(one_line);
    root->append_child(multiple_lines);
    root->append_child(ifc);
    std::ostringstream output;
    root->dump(output);
    const std::string actual = output.str();
    assert(actual.find("BlockBox #" + clean + " [") != std::string::npos);
    assert(actual.find("text=\"" + clean + "\"") != std::string::npos);
    assert(actual.find("lines=1 text=\"" + clean + "\"") != std::string::npos);
    assert(actual.find("lines=2 text=\"" + clean + "...\"") != std::string::npos);
    assert(actual.find("IFC lines=1 first_line=(") != std::string::npos);
    assert(actual.find('\x1b') == std::string::npos);
    assert(element->id() == attack);
    assert(root->text == attack && one_line->lines[0] == attack && multiple_lines->lines[0] == attack);
    assert(ifc->text == attack && ifc->lines[0] == attack);

    // A layout object can also carry malformed bytes when made by API code.
    // Tabs and line feeds remain the dump format's existing whitespace.
    auto bytes = LayoutBox::create_text(nullptr, std::string("ok\t\n\x9b\xc0\x9b", 7), nullptr);
    std::ostringstream invalid;
    bytes->dump(invalid);
    assert(invalid.str().find("text=\"ok\t\n\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\"") != std::string::npos);
    assert(bytes->text == std::string("ok\t\n\x9b\xc0\x9b", 7));
}
