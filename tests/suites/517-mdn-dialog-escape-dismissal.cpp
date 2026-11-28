// Phase 517: Escape dismisses a modal dialog through the cancelable close-request path.
TEST(modal_dialog_escape_honors_cancellation_and_restores_focus) {
    using namespace litebird;
    const auto root = std::filesystem::temp_directory_path() /
                      ("litebird-dialog-escape-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    {
        std::ofstream page(root / "dialog.html");
        page << R"html(<!doctype html><html><body>
          <button id="opener">Open dialog</button>
          <button id="outside">Outside action</button>
          <dialog id="modal"><button id="inside">Inside dialog</button>
            <select id="animal"><option></option></select>
          </dialog>
          <script>
            var keydownPrevent = true;
            var cancelPrevent = true;
            var cancelCount = 0;
            var closeCalls = 0;
            var cancelBubbles = "";
            var cancelCancelable = "";
            var outsideClicks = 0;
            document.getElementById("outside").addEventListener("click", () => outsideClicks++);
            var modalElement = document.getElementById("modal");
            modalElement.returnValue = "preserved";
            modalElement.addEventListener("keydown", event => {
              if (event.key === "Escape" && keydownPrevent) event.preventDefault();
            });
            modalElement.addEventListener("cancel", event => {
              cancelCount++;
              cancelBubbles = String(event.bubbles);
              cancelCancelable = String(event.cancelable);
              if (cancelPrevent) event.preventDefault();
            });
          </script>
        </body></html>)html";
    }
    ResourceLoader::set_origin_root_for_testing("http://dialog-escape.test", root.string());
    BrowserShell shell(400, 300);
    assert(shell.navigate_to("http://dialog-escape.test/dialog.html"));

    auto document = shell.document();
    auto opener = document->get_element_by_id("opener");
    auto outside = document->get_element_by_id("outside");
    auto modal = document->get_element_by_id("modal");
    auto inside = document->get_element_by_id("inside");
    auto animal = document->get_element_by_id("animal");
    assert(opener && outside && modal && inside && animal);

    // A closed dialog contributes neither a box nor an interaction surface.
    assert(!modal->has_attribute("open"));
    assert(!modal->has_layout_box());

    shell.js_engine()->eval("document.getElementById('opener').focus(); document.getElementById('modal').showModal();");
    shell.relayout();
    assert(modal->has_attribute("open"));
    assert(document->modal_dialog() == modal);
    assert(document->is_inert(*outside));
    assert(document->focused_element() == inside);
    // The native select must contribute its browser-default box even when its
    // selected option is empty; otherwise the dialog's intrinsic width and the
    // visible dropdown affordance collapse.
    assert(animal->bounding_box().width > 0.0f);
    assert(animal->bounding_box().height > 0.0f);
    const Rect outside_box = outside->bounding_box();
    const float outside_x = outside_box.x + outside_box.width * 0.5f;
    const float outside_y = outside_box.y + outside_box.height * 0.5f;
    assert(shell.element_at(outside_x, outside_y) != outside);
    assert(shell.click_at(outside_x, outside_y));
    assert(shell.js_engine()->eval("outsideClicks").to_string() == "0");

    // A canceled keydown suppresses the close request entirely.
    assert(shell.handle_key("Escape"));
    assert(modal->has_attribute("open"));
    assert(shell.js_engine()->eval("cancelCount").to_string() == "0");

    // Otherwise Escape dispatches a cancelable, non-bubbling `cancel` event.
    // preventDefault() keeps the modal open and does not change returnValue.
    shell.js_engine()->eval("keydownPrevent = false;");
    assert(shell.handle_key("Escape"));
    assert(modal->has_attribute("open"));
    assert(shell.js_engine()->eval("cancelCount").to_string() == "1");
    assert(shell.js_engine()->eval("cancelBubbles").to_string() == "false");
    assert(shell.js_engine()->eval("cancelCancelable").to_string() == "true");
    assert(shell.js_engine()->eval("document.getElementById('modal').returnValue").to_string() == "preserved");

    // The platform close request must run the dialog close steps directly,
    // even if author script shadows the public `close()` method.
    shell.js_engine()->eval("cancelPrevent = false; document.getElementById('modal').close = function(){ closeCalls++; };");
    assert(shell.handle_key("Escape"));
    assert(!modal->has_attribute("open"));
    assert(!document->modal_dialog());
    assert(!document->is_inert(*outside));
    assert(shell.js_engine()->eval("cancelCount").to_string() == "2");
    assert(shell.js_engine()->eval("closeCalls").to_string() == "0");
    assert(shell.js_engine()->eval("document.getElementById('modal').returnValue").to_string() == "preserved");
    assert(document->focused_element() == opener);

    ResourceLoader::clear_origin_roots_for_testing();
    std::filesystem::remove_all(root);
}
