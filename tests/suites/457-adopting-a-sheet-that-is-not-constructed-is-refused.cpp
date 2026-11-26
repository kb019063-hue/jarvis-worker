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
TEST(adopting_a_sheet_that_is_not_constructed_is_refused) {
    using namespace litebird;
    TestPage page;
    page.load("<!doctype html><html><head><style>#probe { color: rgb(0, 0, 255); }</style></head>"
              "<body><div id='probe'>probe</div></body></html>");

    auto refused = [&page](const char* assignment, const char* name) {
        bool threw = false;
        std::string error_name;
        page.js.global_env()->define("record", JsValue::make_native_function(
            [&error_name](const std::vector<JsValue>& args) {
                if (!args.empty()) error_name = args[0].to_string();
                return JsValue::make_undefined();
            }));
        try {
            page.js.eval(assignment);
            threw = page.js.last_eval_failed();
        } catch (const JsThrow&) {
            threw = true;
        }
        assert(threw);
        assert(error_name == name);
    };

    // A `<style>` element's sheet is owned by the element: adopting it is
    // `NotAllowedError`, not a silent success.
    refused("try { document.adoptedStyleSheets = [document.querySelector('style').sheet]; }"
            "catch (e) { record(e.name); throw e; }", "NotAllowedError");
    // A plain object is not a sheet at all.
    refused("try { document.adoptedStyleSheets = [{}]; } catch (e) { record(e.name); throw e; }",
            "TypeError");
    // The value itself must be a list of sheets.
    refused("try { document.adoptedStyleSheets = 'x'; } catch (e) { record(e.name); throw e; }",
            "TypeError");

    // None of the refusals adopted anything.
    assert(page.js.eval("document.adoptedStyleSheets.length").to_string() == "0");
    assert(page.document().adopted_sheets().empty());
}

// The layout cascade reads the same adopted sheets `getComputedStyle` does:
// a sheet adopted by a page's own script changes the painted box, not just
// what script is told.
