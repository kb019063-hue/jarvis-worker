// Phase 491, finding 491: the layout cascade must not treat form-validation
// pseudo-classes as universal selectors.
TEST(css_valid_and_invalid_pseudo_classes_do_not_paint_non_controls) {
    using namespace litebird;
    const auto root = std::filesystem::temp_directory_path() /
                      ("litebird-valid-invalid-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    {
        std::ofstream page(root / "validity.html");
        page << R"html(<!doctype html><html><head><style>
            html { background-color: #c4c4c4; }
            body { margin: 0; background-color: white; }
            div { width: 80px; height: 60px; background-color: white; }
            :valid { display: none; background-color: lime; }
            :invalid { display: none; background-color: #ffb6c1; }
        </style></head><body>
            <div id="probe"></div>
            <textarea id="valid-area">plain text</textarea>
            <input id="valid-email" type="email" value="reader@example.test">
            <input id="invalid-email" type="email" value="not-an-address">
        </body></html>)html";
    }
    ResourceLoader::set_origin_root_for_testing("http://validity.test", root.string());
    BrowserShell shell(200, 160);
    assert(shell.navigate_to("http://validity.test/validity.html"));

    // The CSS used by the Mothereff pages includes `:invalid { background:
    // #ffb6c1 }`. A CSS matcher that ignores the pseudo-class paints the
    // document and this ordinary div pink, even though no non-control can be
    // valid or invalid. Check the actual painted pixels rather than the
    // script-facing computed-style path (which has its own selector matcher).
    auto png = shell.render_png_frame();
    auto frame = ImageDecoder::decode_png(png.data(), png.size());
    assert(frame && frame->is_valid());
    assert(frame->get_pixel(10, 10) == 0xFFFFFFFFu);

    auto probe = shell.document()->get_element_by_id("probe");
    auto valid_area = shell.document()->get_element_by_id("valid-area");
    auto valid_email = shell.document()->get_element_by_id("valid-email");
    auto invalid_email = shell.document()->get_element_by_id("invalid-email");
    assert(probe && valid_area && valid_email && invalid_email);
    // The CSS cascade itself must apply each state to its matching control;
    // neither state may accidentally suppress an ordinary element.
    assert(probe->has_layout_box());
    assert(!valid_area->has_layout_box());
    assert(!valid_email->has_layout_box());
    assert(!invalid_email->has_layout_box());
    assert(shell.js_engine()->eval("document.body.matches(':valid')").to_string() == "false");
    assert(shell.js_engine()->eval("document.body.matches(':invalid')").to_string() == "false");
    assert(shell.js_engine()->eval("document.getElementById('valid-area').matches(':valid')").to_string() ==
           "true");
    assert(shell.js_engine()->eval("document.getElementById('invalid-email').matches(':invalid')").to_string() ==
           "true");

    ResourceLoader::clear_origin_roots_for_testing();
    std::filesystem::remove_all(root);
}
