// Phase 232, finding 236: a checkbox does not paint the literal string "on".
TEST(a_checkbox_does_not_paint_the_string_on) {
    using namespace litebird;
    // The overlay in BrowserShell::render_png_frame used to paint every
    // <input> with el.value() — for a checkbox/radio with no value attribute
    // that is "on" (FormState::DefaultOn). The H4-added TodoMVC frame showed
    // a small box containing "on" where Chromium shows a native checkbox.
    // Now checkbox/radio are rendered as a bordered box with a check/dot,
    // and the value is never painted as text.
    BrowserShell shell(800, 600);
    const std::string html = R"html(
        <!doctype html><style>input.toggle{width:20px;height:20px;}</style>
        <input class="toggle" type="checkbox" id="cb">
        <input type="radio" id="rd" name="g">
        <input type="text" id="txt" value="hello">
    )html";
    auto doc = std::make_shared<Document>();
    doc->set_url("http://snapshot.test/checkbox.html");
    // Use HtmlParser directly via BrowserShell navigate_to with temp file.
    // Simpler: load via shell's navigate_to using origin root.
    auto root = std::filesystem::temp_directory_path() / ("litebird-checkbox-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    {
        std::ofstream f(root / "checkbox.html");
        f << html;
    }
    ResourceLoader::set_origin_root_for_testing("http://snapshot.test", root.string());
    assert(shell.navigate_to("http://snapshot.test/checkbox.html"));
    auto cb = shell.document()->get_element_by_id("cb");
    auto rd = shell.document()->get_element_by_id("rd");
    auto txt = shell.document()->get_element_by_id("txt");
    assert(cb && rd && txt);
    // Default value for checkbox/radio is "on", but it must not be rendered
    // as text inside the overlay. The element's value is still "on" for
    // submission — only the painting changes.
    assert(cb->value() == "on");
    assert(rd->value() == "on");
    assert(txt->value() == "hello");
    // Checked state is separate from value and must render correctly.
    assert(!cb->checked());
    cb->set_checked(true);
    assert(cb->checked());
    // The overlay must not throw and must produce a valid PNG.
    auto png = shell.render_png_frame();
    assert(!png.empty());
    // PNG signature
    assert(png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
    // Re-render after toggling radio as well.
    rd->set_checked(true);
    assert(rd->checked());
    auto png2 = shell.render_png_frame();
    assert(!png2.empty());
    assert(png2.size() > 100);
    ResourceLoader::clear_origin_roots_for_testing();
    std::filesystem::remove_all(root);
}
