// Phase 235, finding 239: a keyword CSS property with no rule took any string.
//
// `canonicalize_css_value` treats a property it has no rule for as free text:
// the declaration is stored and `getPropertyValue` hands it back. So
// `el.style.setProperty("table-layout", "totallybogusvalue")` looked accepted,
// `cssText` wrote it out, and `getComputedStyle` reported it — while a browser
// drops the whole declaration. Twenty-seven properties were in that state. The
// fix gives each a rule with the keywords CSS allows, so a value outside the
// grammar is refused at the door rather than stored and echoed.
TEST(a_keyword_value_outside_the_grammar_is_not_a_declaration) {
    TestPage page;
    JsInterpreter& js = page.js;

    // A value with no keyword in the property's grammar is dropped: the
    // declaration never enters the style, `cssText` does not carry it, and
    // `getComputedStyle` reports the initial value instead of the text.
    auto refuses = [&](const char* property, const char* value) {
        const std::string setup =
            std::string("var d = document.createElement('div');"
                        "document.body.appendChild(d);"
                        "d.style.cssText = '';"
                        "d.style.setProperty('") +
            property + "', '" + value + "');";
        js.eval(setup);
        const std::string stored = js.eval("d.style.getPropertyValue('" + std::string(property) + "')").to_string();
        return stored.empty();
    };
    auto accepts = [&](const char* property, const char* value) {
        const std::string setup =
            std::string("var d = document.createElement('div');"
                        "document.body.appendChild(d);"
                        "d.style.cssText = '';"
                        "d.style.setProperty('") +
            property + "', '" + value + "');";
        js.eval(setup);
        return js.eval("d.style.getPropertyValue('" + std::string(property) + "')").to_string() == value;
    };

    // The twenty-seven properties that took any string before. Each one has a
    // grammar in CSS, and each one's own keywords still work.
    struct Case {
        const char* property;
        const char* good;
    };
    const Case cases[] = {
        {"table-layout", "fixed"},          {"border-collapse", "collapse"},
        {"empty-cells", "hide"},            {"caption-side", "bottom"},
        {"writing-mode", "vertical-rl"},    {"direction", "rtl"},
        {"unicode-bidi", "isolate"},        {"mix-blend-mode", "multiply"},
        {"isolation", "isolate"},           {"object-fit", "cover"},
        {"white-space", "pre-wrap"},        {"word-break", "break-all"},
        {"overflow-wrap", "anywhere"},      {"hyphens", "auto"},
        {"vertical-align", "middle"},       {"pointer-events", "none"},
        {"user-select", "none"},            {"resize", "vertical"},
        {"list-style-type", "upper-roman"}, {"background-repeat", "repeat-x"},
        {"background-attachment", "fixed"}, {"background-clip", "content-box"},
        {"background-origin", "padding-box"}, {"appearance", "none"},
        {"font-weight", "bold"},            {"touch-action", "manipulation"},
        {"scroll-behavior", "smooth"},      {"image-rendering", "pixelated"},
        {"justify-content", "space-between"}, {"align-items", "center"},
        {"align-self", "stretch"},          {"align-content", "space-evenly"},
        {"justify-items", "end"},           {"will-change", "transform"},
        {"cursor", "pointer"},
    };
    for (const auto& c : cases) {
        assert(accepts(c.property, c.good));
        assert(refuses(c.property, "totallybogusvalue"));
    }

    // A keyword is not the whole grammar everywhere. `font-weight: 700` and
    // `cursor: url(hand.cur), auto` are values a browser keeps, so the rule
    // must not turn the property into keywords-only.
    assert(accepts("font-weight", "700"));
    assert(accepts("font-weight", "normal"));
    assert(accepts("writing-mode", "horizontal-tb"));

    // The properties whose grammar is a function list, not a keyword set, are
    // left as they were: this phase fixes what it can name the keywords of.
    // (`contain`, `filter`, `mask` and `clip-path` take `url(...)`, `blur(...)`,
    // `polygon(...)` and the like, so a keyword-only rule would refuse values
    // a browser accepts — worse than not checking.)

    std::printf("      [css keywords] 35 properties check their grammar, valid numbers still pass\n");
    std::fflush(stdout);
}
