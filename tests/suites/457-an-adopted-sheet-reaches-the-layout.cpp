// Phase 457, finding 457: a stylesheet a script builds was not in the cascade.
//
// `document.adoptedStyleSheets` did not exist: on a page with no script,
// `typeof document.adoptedStyleSheets` was `undefined`, and a page that
// assigned to it made a plain JS expando (an `Array` own property, gone after
// `delete`). `new CSSStyleSheet()` worked and `replaceSync` filled the sheet,
// but nothing ever read it, so a constructed sheet styled nothing. Phase 111
// asserted a constructed sheet was "not in the cascade" and passed vacuously:
// there was no cascade entry to be absent from.
//
// CSSOM's adopted style sheets are the list a script gives a document with
// `document.adoptedStyleSheets = [sheet, ...]`, read after the document's own
// sheets. This suite checks the whole of it: the list exists and is live, an
// adopted sheet styles the page, its order against the document's sheets is
// the spec's, edits after adoption reach the cascade, and the refusals are
// the spec's.
TEST(an_adopted_sheet_reaches_the_layout) {
    using namespace litebird;
    const auto root = std::filesystem::temp_directory_path() /
                      ("litebird-adopted-sheet-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    {
        std::ofstream page(root / "adopted.html");
        page << "<!doctype html><html><head></head><body><div id='probe'>probe</div>"
                "<script>var s = new CSSStyleSheet();"
                "s.replaceSync('#probe { display: block; height: 77px; }');"
                "document.adoptedStyleSheets = [s];</script></body></html>";
    }
    ResourceLoader::set_origin_root_for_testing("http://adopted-sheet.test", root.string());

    BrowserShell shell(800, 600);
    assert(shell.navigate_to("http://adopted-sheet.test/adopted.html"));
    JsInterpreter& js = *shell.js_engine();
    // What script is told...
    assert(js.eval("getComputedStyle(document.getElementById('probe')).height").to_string() == "77px");
    // ...and what was painted agree: the box is 77px tall.
    assert(js.eval("String(document.getElementById('probe').getBoundingClientRect().height)").to_string() == "77");
}
