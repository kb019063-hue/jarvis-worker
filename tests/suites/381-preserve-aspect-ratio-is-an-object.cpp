// Phase 381, finding 381: `preserveAspectRatio` had no DOM at all.
//
// Phase 374 put `viewBox` on the five elements that include SVGFitToViewBox.
// That mixin has two attributes (SVG 2 §4.6.9), and the other one --
// `preserveAspectRatio` -- was still absent: `typeof svg.preserveAspectRatio`
// was `undefined`, and so were the two interfaces it returns. The renderer
// had been reading the attribute as a string all along
// (`SvgRenderer::compute_viewbox_matrix`), so a page could set it but could
// not read or edit it as the object the standard defines.
//
// The shape of it, from §8.14.4 and §8.14.5:
//
//   SVGAnimatedPreserveAspectRatio  baseVal, animVal -> SVGPreserveAspectRatio
//   SVGPreserveAspectRatio          align, meetOrSlice, and 14 constants
//
// Three rules are easy to get wrong, and each has its assertions below:
//
//   1. The initial value is `xMidYMid meet` when the attribute is absent **or
//      invalid** (§4.3.6) -- not UNKNOWN. An attribute the grammar rejects
//      reads back as the initial pair.
//   2. A write serializes **both** parts, always: "align, one space,
//      meetOrSlice" (§8.14.4). There is no exception for `none`, even though
//      the attribute grammar ignores `<meetOrSlice>` after it, so setting
//      align to NONE on `xMidYMid slice` writes `none slice`. Getting this
//      wrong is the tempting mistake, because `none meet` looks redundant.
//   3. `animVal` is read-only: writing through it throws
//      NoModificationAllowedError, while `baseVal` writes the attribute.
//
// Reference: SVG 2 §8.7 (the grammar and its initial value), §8.14.4
// (SVGPreserveAspectRatio, its constants and its two setters), §8.14.5
// (SVGAnimatedPreserveAspectRatio), §4.3.6 (reading an invalid value).
TEST(preserve_aspect_ratio_is_an_object) {
    TestPage page;
    auto& js = page.js;
    auto S = [&](const char* code) { return js.eval(code).to_string(); };
    auto make = [&](const std::string& var, const std::string& tag) {
        S(("var " + var + "=document.createElementNS('http://www.w3.org/2000/svg','" + tag + "');").c_str());
    };

    make("svg", "svg");
    make("sym", "symbol");
    make("mk", "marker");
    make("pat", "pattern");
    make("vw", "view");
    make("g", "g");

    // --- The two interfaces, and where they live. ---
    assert(S("typeof svg.preserveAspectRatio") == "object");
    assert(S("svg.preserveAspectRatio instanceof SVGAnimatedPreserveAspectRatio") == "true");
    assert(S("svg.preserveAspectRatio.baseVal instanceof SVGPreserveAspectRatio") == "true");
    assert(S("svg.preserveAspectRatio.animVal instanceof SVGPreserveAspectRatio") == "true");
    // [SameObject] on all four members.
    assert(S("svg.preserveAspectRatio===svg.preserveAspectRatio") == "true");
    assert(S("var p=svg.preserveAspectRatio;p.baseVal===p.baseVal") == "true");
    assert(S("var p2=svg.preserveAspectRatio;p2.animVal===p2.animVal") == "true");
    // baseVal and animVal are distinct objects (no animation runs, so they
    // agree in value but are not the same object).
    assert(S("var p3=svg.preserveAspectRatio;p3.baseVal===p3.animVal") == "false");

    // It is SVGFitToViewBox's, so the same five elements have it as have
    // `viewBox` -- and a `<g>` has neither (Phase 374's negative half).
    assert(S("typeof sym.preserveAspectRatio") == "object");
    assert(S("typeof mk.preserveAspectRatio") == "object");
    assert(S("typeof pat.preserveAspectRatio") == "object");
    assert(S("typeof vw.preserveAspectRatio") == "object");
    assert(S("typeof g.preserveAspectRatio") == "undefined");

    // --- The constants, at the values the IDL gives them. ---
    assert(S("SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_UNKNOWN") == "0");
    assert(S("SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_NONE") == "1");
    assert(S("SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_XMINYMIN") == "2");
    assert(S("SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_XMIDYMIN") == "3");
    assert(S("SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_XMAXYMIN") == "4");
    assert(S("SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_XMINYMID") == "5");
    assert(S("SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_XMIDYMID") == "6");
    assert(S("SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_XMAXYMID") == "7");
    assert(S("SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_XMINYMAX") == "8");
    assert(S("SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_XMIDYMAX") == "9");
    assert(S("SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_XMAXYMAX") == "10");
    assert(S("SVGPreserveAspectRatio.SVG_MEETORSLICE_UNKNOWN") == "0");
    assert(S("SVGPreserveAspectRatio.SVG_MEETORSLICE_MEET") == "1");
    assert(S("SVGPreserveAspectRatio.SVG_MEETORSLICE_SLICE") == "2");
    // The constants are defined on the interface object and reach instances
    // through the prototype, which is where Web IDL puts them: readable from
    // an instance, but not an own property of one.
    assert(S("var p4=svg.preserveAspectRatio;p4.baseVal.SVG_PRESERVEASPECTRATIO_NONE") == "1");
    assert(S("var pc=svg.preserveAspectRatio;"
             "Object.prototype.hasOwnProperty.call(pc.baseVal,'SVG_PRESERVEASPECTRATIO_NONE')") == "false");
    assert(S("var pd=svg.preserveAspectRatio;"
             "'SVG_PRESERVEASPECTRATIO_NONE' in pd.baseVal") == "true");

    // --- The initial value, and reading the attribute. ---
    assert(S("var p5=svg.preserveAspectRatio;p5.baseVal.align+'|'+p5.baseVal.meetOrSlice") == "6|1");
    assert(S("svg.setAttribute('preserveAspectRatio','xMinYMax slice');"
             "var p6=svg.preserveAspectRatio;p6.baseVal.align+'|'+p6.baseVal.meetOrSlice") == "8|2");
    // The optional `<meetOrSlice>?` defaults to meet when it is not given.
    assert(S("svg.setAttribute('preserveAspectRatio','xMaxYMin');"
             "var p7=svg.preserveAspectRatio;p7.baseVal.align+'|'+p7.baseVal.meetOrSlice") == "4|1");
    // `none` ignores its meetOrSlice in the grammar but still reads what was
    // written, so `none slice` is (NONE, SLICE).
    assert(S("svg.setAttribute('preserveAspectRatio','none slice');"
             "var p8=svg.preserveAspectRatio;p8.baseVal.align+'|'+p8.baseVal.meetOrSlice") == "1|2");

    // An invalid value falls back to the initial pair, not to UNKNOWN (§4.3.6).
    for (const char* bad : {"'nonsense'", "'xMidYMid sideways'", "'XMIDYMID meet'", "'xMidYMid meet extra'",
                            "'defer xMidYMid slice'", "''", "'slice'"}) {
        assert(S(("svg.setAttribute('preserveAspectRatio'," + std::string(bad) + ");"
                  "var pb=svg.preserveAspectRatio;pb.baseVal.align+'|'+pb.baseVal.meetOrSlice").c_str()) == "6|1");
    }
    // Whitespace is not significant around or between the keywords.
    assert(S("svg.setAttribute('preserveAspectRatio','  xMinYMin   slice  ');"
             "var pw=svg.preserveAspectRatio;pw.baseVal.align+'|'+pw.baseVal.meetOrSlice") == "2|2");
    // Removing it returns to the initial value.
    assert(S("svg.removeAttribute('preserveAspectRatio');"
             "var pr=svg.preserveAspectRatio;pr.baseVal.align+'|'+pr.baseVal.meetOrSlice") == "6|1");

    // --- Writing, which always serializes both parts. ---
    assert(S("svg.removeAttribute('preserveAspectRatio');"
             "svg.preserveAspectRatio.baseVal.meetOrSlice=SVGPreserveAspectRatio.SVG_MEETORSLICE_SLICE;"
             "svg.getAttribute('preserveAspectRatio')") == "xMidYMid slice");
    assert(S("svg.preserveAspectRatio.baseVal.align=SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_XMINYMAX;"
             "svg.getAttribute('preserveAspectRatio')") == "xMinYMax slice");
    // The rule that looks wrong and is not: `none` still writes its
    // meetOrSlice, because §8.14.4's serialization has no exception for it.
    assert(S("svg.preserveAspectRatio.baseVal.align=SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_NONE;"
             "svg.getAttribute('preserveAspectRatio')") == "none slice");
    assert(S("svg.preserveAspectRatio.baseVal.meetOrSlice=SVGPreserveAspectRatio.SVG_MEETORSLICE_MEET;"
             "svg.getAttribute('preserveAspectRatio')") == "none meet");
    // Writing reads back as itself, on a symbol too.
    assert(S("var sA=sym.preserveAspectRatio;sA.baseVal.align=SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_XMAXYMAX;"
             "sA.baseVal.meetOrSlice=SVGPreserveAspectRatio.SVG_MEETORSLICE_SLICE;"
             "[sym.getAttribute('preserveAspectRatio'),sA.baseVal.align,sA.baseVal.meetOrSlice].join('|')") ==
           "xMaxYMax slice|10|2");

    // --- What the setters refuse. ---
    // A value with no keyword throws a TypeError (§8.14.4).
    assert(S("try { svg.preserveAspectRatio.baseVal.align=11; 'no'; } catch(e) { e.name; }") == "TypeError");
    assert(S("try { svg.preserveAspectRatio.baseVal.align=0; 'no'; } catch(e) { e.name; }") == "TypeError");
    assert(S("try { svg.preserveAspectRatio.baseVal.meetOrSlice=0; 'no'; } catch(e) { e.name; }") == "TypeError");
    assert(S("try { svg.preserveAspectRatio.baseVal.meetOrSlice=3; 'no'; } catch(e) { e.name; }") == "TypeError");
    // The animated half is read-only (§8.14.4, "If the SVGPreserveAspectRatio
    // is read only, then throw a NoModificationAllowedError").
    assert(S("try { svg.preserveAspectRatio.animVal.align=2; 'no'; } catch(e) { e.name; }") == "NoModificationAllowedError");
    assert(S("try { svg.preserveAspectRatio.animVal.meetOrSlice=2; 'no'; } catch(e) { e.name; }") == "NoModificationAllowedError");
    // A refused write leaves the attribute alone.
    assert(S("svg.setAttribute('preserveAspectRatio','xMidYMid slice');"
             "try { svg.preserveAspectRatio.baseVal.align=99; } catch(e) {}"
             "svg.getAttribute('preserveAspectRatio')") == "xMidYMid slice");
    // WebIDL converts before the range check runs, so a fractional value
    // truncates rather than throwing and then lands in range.
    assert(S("svg.preserveAspectRatio.baseVal.align=6.9;"
             "svg.preserveAspectRatio.baseVal.align") == "6");

    // --- The interfaces are not constructible. ---
    assert(S("try { new SVGPreserveAspectRatio(); 'no'; } catch(e) { e.name; }") == "TypeError");
    assert(S("try { new SVGAnimatedPreserveAspectRatio(); 'no'; } catch(e) { e.name; }") == "TypeError");

    // --- The renderer and the DOM agree about the attribute. ---
    // Setting through the DOM writes the attribute the renderer reads, so a
    // value written by script is what the viewBox transform uses.
    assert(S("var r=document.createElementNS('http://www.w3.org/2000/svg','svg');"
             "r.setAttribute('viewBox','0 0 10 10');"
             "r.preserveAspectRatio.baseVal.align=SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_NONE;"
             "r.getAttribute('preserveAspectRatio')") == "none meet");
}
