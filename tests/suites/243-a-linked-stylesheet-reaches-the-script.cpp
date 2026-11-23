// Phase 243, finding 247: the style a script reads is the style that was painted.
//
// `getComputedStyle` reads a cascade of its own (DomStyleBindings.cpp), separate
// from the one `DocumentStyles::build_resolver` builds for the layout. That
// second cascade collected `<style>` elements and nothing else. A `<link
// rel=stylesheet>` has no text content, so its sheet came out empty: every rule
// in the page's only stylesheet was missing from what script was told, while the
// layout painted all of them. The rectangle a script read was then a true
// statement about a page the browser never showed.
//
// The page below is the smallest form of it, and the two halves are checked
// against each other: what `getComputedStyle` answers must be what the layout
// did. A `<style>` holding the same rules is the control, so that a fix which
// simply stopped reading sheets could not pass.
TEST(a_linked_stylesheet_reaches_the_script) {
    using namespace litebird;
    const auto root = std::filesystem::temp_directory_path() /
                      ("litebird-linked-sheet-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    {
        std::ofstream css(root / "page.css");
        css << "#box { display: none; height: 123px; }\n"
               "#after { height: 40px; }\n";
    }
    {
        std::ofstream page(root / "linked.html");
        page << "<!doctype html><html><head><link rel='stylesheet' href='page.css'>"
                "</head><body><div id='box'>box</div><div id='after'>after</div></body></html>";
    }
    {
        std::ofstream page(root / "inlined.html");
        page << "<!doctype html><html><head><style>#box { display: none; height: 123px; }"
                "#after { height: 40px; }</style>"
                "</head><body><div id='box'>box</div><div id='after'>after</div></body></html>";
    }
    ResourceLoader::set_origin_root_for_testing("http://linked-sheet.test", root.string());

    auto read = [](const char* name) {
        struct Seen { std::string display, height, after_top; bool box_laid_out; } seen;
        BrowserShell shell(800, 600);
        assert(shell.navigate_to(std::string("http://linked-sheet.test/") + name));
        JsInterpreter& js = *shell.js_engine();
        seen.display = js.eval("getComputedStyle(document.getElementById('box')).display").to_string();
        seen.height = js.eval("getComputedStyle(document.getElementById('box')).height").to_string();
        seen.after_top = js.eval("String(document.getElementById('after').getBoundingClientRect().top)").to_string();
        const auto box = shell.document()->get_element_by_id("box");
        assert(box);
        // The painted layout, which the linked sheet has always reached.
        seen.box_laid_out = box->has_layout_box();
        return seen;
    };

    const auto linked = read("linked.html");
    const auto inlined = read("inlined.html");

    // The rule is in the linked sheet, so the script must be told it is.
    assert(linked.display == "none");
    assert(linked.height == "123px");
    // And the painted layout agrees: a `display: none` box reserves no room, so
    // `#after` is the first box in the flow and sits at the body's padding edge.
    assert(!linked.box_laid_out);

    // The control: the same rules in a `<style>` answered the same way before
    // this phase, and must still. A fix that changed `<style>` handling would
    // show up as a disagreement between these two lines.
    assert(inlined.display == linked.display);
    assert(inlined.height == linked.height);
    assert(inlined.after_top == linked.after_top);
    assert(inlined.box_laid_out == linked.box_laid_out);
}
