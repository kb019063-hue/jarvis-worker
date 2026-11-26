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
TEST(adopted_constructable_stylesheets_reach_the_cascade) {
    using namespace litebird;
    TestPage page;
    page.load("<!doctype html><html><head><style>#probe { color: rgb(0, 0, 255); }</style></head>"
              "<body><div id='probe'>probe</div></body></html>");

    // The list exists before anything is adopted, and is empty.
    assert(page.js.eval("typeof document.adoptedStyleSheets").to_string() == "object");
    assert(page.js.eval("document.adoptedStyleSheets.length").to_string() == "0");

    // The document's own sheet applies first, as it always did.
    assert(page.js.eval("getComputedStyle(document.getElementById('probe')).color").to_string() ==
           "rgb(0, 0, 255)");

    // A constructed sheet, adopted, beats the document's sheet at equal
    // specificity: adopted sheets come after the document's in the cascade.
    page.js.eval("var s = new CSSStyleSheet();"
                 "s.replaceSync('#probe { color: rgb(255, 0, 0); }');"
                 "document.adoptedStyleSheets = [s];");
    assert(page.js.eval("getComputedStyle(document.getElementById('probe')).color").to_string() ==
           "rgb(255, 0, 0)");

    // The list is live and holds the same sheet object: `length`, `item(i)`
    // and index access all answer it, and the list object is the same one
    // every time.
    assert(page.js.eval("document.adoptedStyleSheets.length").to_string() == "1");
    assert(page.js.eval("document.adoptedStyleSheets.item(0) === s").to_string() == "true");
    assert(page.js.eval("document.adoptedStyleSheets[0] === s").to_string() == "true");
    assert(page.js.eval("document.adoptedStyleSheets === document.adoptedStyleSheets").to_string() == "true");
    assert(page.js.eval("document.adoptedStyleSheets.item(1)").to_string() == "null");

    // An edit after adoption reaches the cascade: `insertRule` adds a rule
    // the layout and `getComputedStyle` both see, and `deleteRule` takes the
    // first rule back out.
    page.js.eval("s.insertRule('#probe { background-color: rgb(0, 0, 255); }', 1);");
    assert(page.js.eval("getComputedStyle(document.getElementById('probe')).backgroundColor").to_string() ==
           "rgb(0, 0, 255)");
    page.js.eval("s.deleteRule(0);");
    assert(page.js.eval("getComputedStyle(document.getElementById('probe')).color").to_string() ==
           "rgb(0, 0, 255)");

    // A second adopted sheet comes after the first, so it wins at equal
    // specificity; and the document's own sheet is still behind both.
    page.js.eval("var t = new CSSStyleSheet();"
                 "t.replaceSync('#probe { color: rgb(0, 128, 0); }');"
                 "document.adoptedStyleSheets = [s, t];");
    assert(page.js.eval("document.adoptedStyleSheets.length").to_string() == "2");
    assert(page.js.eval("getComputedStyle(document.getElementById('probe')).color").to_string() ==
           "rgb(0, 128, 0)");

    // Clearing the list takes the adopted sheets back out of the cascade.
    page.js.eval("document.adoptedStyleSheets = [];");
    assert(page.js.eval("getComputedStyle(document.getElementById('probe')).color").to_string() ==
           "rgb(0, 0, 255)");

    // The document's record holds the sheets' text, which is what the layout
    // cascade parses: adopting wrote it, and the edit rewrote it.
    page.js.eval("document.adoptedStyleSheets = [t];");
    const auto& adopted = page.document().adopted_sheets();
    assert(adopted.size() == 1);
    assert(adopted[0] && adopted[0]->text.find("rgb(0, 128, 0)") != std::string::npos);
}

// The refusals are the spec's: a sheet that belongs to an element cannot be
// adopted (`NotAllowedError`), and anything that is not a constructed sheet
// is a `TypeError`. A failed adoption leaves the list as it was.
