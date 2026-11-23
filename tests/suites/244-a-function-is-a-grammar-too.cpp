// Phase 244, finding 248: a function is a grammar too.
//
// Phase 235 gave thirty-five keyword properties a grammar and deliberately
// left four properties as free text because their grammars are function
// lists (`filter: blur(2px)`, `clip-path: polygon(...)`), and left `will-change`
// checking only its first keyword, so `will-change: transform, opacity` was
// refused while a browser keeps it. Each of the five now checks its own
// grammar; a value that fails it is not a declaration, exactly as a keyword
// outside the list is not.
TEST(a_function_or_list_value_outside_the_grammar_is_not_a_declaration) {
    TestPage page;
    JsInterpreter& js = page.js;

    auto refuses = [&](const char* property, const char* value) {
        const std::string setup =
            std::string("var d = document.createElement('div');"
                        "document.body.appendChild(d);"
                        "d.style.cssText = '';"
                        "d.style.setProperty('") +
            property + "', '" + value + "');";
        js.eval(setup);
        return js.eval(std::string("d.style.getPropertyValue('") + property + "')").to_string().empty();
    };
    auto accepts = [&](const char* property, const char* value, const char* stored) {
        const std::string setup =
            std::string("var d = document.createElement('div');"
                        "document.body.appendChild(d);"
                        "d.style.cssText = '';"
                        "d.style.setProperty('") +
            property + "', '" + value + "');";
        js.eval(setup);
        return js.eval(std::string("d.style.getPropertyValue('") + property + "')").to_string() == stored;
    };
    // accepts(property, input) where the stored form is the canonical one.
    auto accepts_self = [&](const char* property, const char* value) { return accepts(property, value, value); };

    // --- contain ---------------------------------------------------------
    assert(accepts_self("contain", "none"));
    assert(accepts_self("contain", "strict"));
    assert(accepts_self("contain", "content"));
    assert(accepts_self("contain", "size"));
    assert(accepts_self("contain", "layout style"));
    assert(accepts_self("contain", "paint layout inline-size"));
    assert(refuses("contain", "everything"));
    assert(refuses("contain", "none layout"));   // `none` stands alone
    assert(refuses("contain", "layout layout")); // no component twice
    assert(refuses("contain", "contained"));

    // --- filter ----------------------------------------------------------
    assert(accepts_self("filter", "none"));
    assert(accepts_self("filter", "blur(2px)"));
    assert(accepts_self("filter", "blur(0)"));
    assert(accepts_self("filter", "brightness(150%)"));
    assert(accepts_self("filter", "contrast(2)"));
    assert(accepts_self("filter", "grayscale(100%)"));
    assert(accepts_self("filter", "hue-rotate(90deg)"));
    assert(accepts_self("filter", "drop-shadow(2px 3px)"));
    assert(accepts_self("filter", "drop-shadow(2px 3px 4px red)"));
    assert(accepts_self("filter", "drop-shadow(red 2px 3px)"));
    assert(accepts_self("filter", "blur(2px) brightness(80%)"));
    assert(accepts_self("filter", "url(\"#f\")"));
    assert(refuses("filter", "blur(-2px)"));
    assert(refuses("filter", "blur(bogus)"));
    assert(refuses("filter", "bogus(2px)"));
    assert(refuses("filter", "blur"));       // a function call, not a keyword
    assert(refuses("filter", "hue-rotate(90)"));
    assert(refuses("filter", "drop-shadow(2px)"));           // two offsets required
    assert(refuses("filter", "drop-shadow(2px 3px 4px 5px)")); // a drop-shadow has no spread
    assert(refuses("filter", "totallybogusvalue"));

    // --- mask ------------------------------------------------------------
    assert(accepts_self("mask", "none"));
    assert(accepts("mask", "url(a.png)", "url(\"a.png\")"));
    assert(accepts("mask", "url(a.png) center / contain no-repeat", "url(\"a.png\") center / contain no-repeat"));
    assert(accepts("mask", "luminance url(a.png)", "luminance url(\"a.png\")"));
    assert(accepts("mask", "url(a.png), url(b.png)", "url(\"a.png\"), url(\"b.png\")"));
    assert(accepts("mask", "linear-gradient(to top, red, blue)", "linear-gradient(to top, red, blue)"));
    assert(refuses("mask", "cover"));              // a layer needs a <mask-reference>
    assert(refuses("mask", "blur(2px)"));          // not an image
    assert(refuses("mask", "url()"));              // an empty reference
    assert(refuses("mask", "luminance"));          // a component is not a reference
    assert(refuses("mask", "totallybogusvalue"));

    // --- clip-path -------------------------------------------------------
    assert(accepts_self("clip-path", "none"));
    assert(accepts_self("clip-path", "circle(50%)"));
    assert(accepts_self("clip-path", "circle()"));
    assert(accepts_self("clip-path", "circle(50% at center)"));
    assert(accepts_self("clip-path", "circle(closest-side at 20% 30%)"));
    assert(accepts_self("clip-path", "ellipse(20px 30px at center)"));
    assert(accepts_self("clip-path", "ellipse(closest-side farthest-side)"));
    assert(accepts_self("clip-path", "inset(10px)"));
    assert(accepts_self("clip-path", "inset(10px 20px 30px 40px round 4px)"));
    assert(accepts_self("clip-path", "polygon(0 0, 100% 0, 50% 100%)"));
    assert(accepts_self("clip-path", "polygon(evenodd, 0 0, 100% 0, 50% 100%)"));
    assert(accepts_self("clip-path", "path(\"M0 0 L10 0 L10 10 Z\")"));
    assert(accepts_self("clip-path", "xywh(0px 0px 10px 10px)"));
    assert(accepts_self("clip-path", "border-box"));
    assert(accepts_self("clip-path", "circle(50%) border-box"));
    assert(accepts("clip-path", "url(#c)", "url(\"#c\")"));
    assert(refuses("clip-path", "bogus(1px)"));
    assert(refuses("clip-path", "circle(50% 60%)"));   // a circle has one radius
    assert(refuses("clip-path", "ellipse(20px)"));     // an ellipse has two or none
    assert(refuses("clip-path", "inset(10px round)")); // `round` needs radii
    assert(refuses("clip-path", "polygon(0 0, 1)"));   // a point is a pair
    assert(refuses("clip-path", "xywh(0px 0px 10px)"));// x, y, width, height
    assert(refuses("clip-path", "url(#c) border-box")); // a url stands alone
    assert(refuses("clip-path", "border-box margin-box"));

    // --- will-change -----------------------------------------------------
    assert(accepts_self("will-change", "auto"));
    assert(accepts_self("will-change", "transform, opacity"));
    assert(accepts_self("will-change", "contents"));
    assert(accepts_self("will-change", "scroll-position, contents"));
    assert(accepts_self("will-change", "z-index"));
    assert(accepts_self("will-change", "--glow"));
    assert(accepts_self("will-change", "transform, filter, opacity"));
    assert(refuses("will-change", "10px"));
    assert(refuses("will-change", "transform, 10px"));
    assert(refuses("will-change", "auto, transform"));
    assert(refuses("will-change", "transform,"));      // a trailing comma leaves an empty feature
    assert(refuses("will-change", "bogus value"));

    // A global keyword and a var() are not rewritten by any of this.
    assert(accepts_self("will-change", "inherit"));
    assert(accepts_self("filter", "initial"));

    std::printf("      [css functions] contain, filter, mask, clip-path and will-change check their grammars\n");
    std::fflush(stdout);
}
