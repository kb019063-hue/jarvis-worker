// Phase 374, finding 374: every SVG element was an SVGElement.
//
// The engine had three SVG element interfaces: `SVGElement`, `SVGGraphicsElement`
// and `SVGSVGElement`, plus `SVGTitleElement` -- and it built only two of them.
// `el.constructor.name` was `SVGSVGElement` for `<svg>` and `SVGElement` for
// everything else, so a `<g>`, a `<rect>`, a `<symbol>`, a `<marker>` and a
// `<pattern>` were the same interface to a script and none of them was a
// graphics element. Three things followed, and each is asserted below:
//
//   1. `getBBox()` was `undefined` on every SVG element, because SVG 2 puts it
//      on SVGGraphicsElement (§4.4.2) and the graphics elements did not
//      inherit from it.
//   2. `createSVGRect()` was `undefined` on `<svg>`, though SVG 2 §5.13.2 gives
//      it to SVGSVGElement.
//   3. `viewBox` existed on `<svg>` alone. SVG 2 §7.7: "All elements that
//      establish a new viewport ... plus the marker, pattern and view elements
//      have attribute viewBox", and the interfaces that carry it -- svg,
//      symbol, marker, pattern, view -- each include SVGFitToViewBox (§4.6.9),
//      whose one attribute is `viewBox`. A `<symbol viewBox="1 2 3 4">` read
//      `undefined`, and the same for marker, pattern and view.
//
// The negative half matters as much as the positive one: `g`, `rect` and the
// other shapes have no `viewBox` in SVG 2, so `undefined` is the correct answer
// there and a fix that gave every SVG element a viewBox would be wrong.
//
// Reference: SVG 2 §4.4.2 (SVGGraphicsElement), §5.13.2 (SVGSVGElement),
// §5.13.8 (SVGSymbolElement : SVGGraphicsElement, includes SVGFitToViewBox),
// §12.13.1 (SVGMarkerElement), §13.6.9 (SVGPatternElement), §7.7 and §4.6.9.
TEST(the_svg_interfaces_that_were_all_one) {
    TestPage page;
    auto& js = page.js;
    auto S = [&](const char* code) { return js.eval(code).to_string(); };
    const std::string SVG_NS = "'http://www.w3.org/2000/svg'";

    // `make()` declares an SVG element as a JS variable: the namespace URL is
    // what createElementNS needs, and passing it once keeps the asserts short.
    auto make = [&](const std::string& var, const std::string& tag) {
        S(("var " + var + "=document.createElementNS('http://www.w3.org/2000/svg','" + tag + "');").c_str());
    };

    // --- The interface each element is an instance of. ---
    // `constructor.name` is what a page reads, and `instanceof` walks the same
    // prototype chain, so both are asserted: a name without the chain would be
    // a constructor that is not the parent's child.
    make("svg", "svg");
    make("g", "g");
    make("rect", "rect");
    make("sym", "symbol");
    make("mk", "marker");
    make("pat", "pattern");
    make("defs", "defs");
    make("vw", "view");
    assert(S("svg.constructor.name") == "SVGSVGElement");
    assert(S("g.constructor.name") == "SVGGElement");
    assert(S("rect.constructor.name") == "SVGRectElement");
    assert(S("sym.constructor.name") == "SVGSymbolElement");
    assert(S("mk.constructor.name") == "SVGMarkerElement");
    assert(S("pat.constructor.name") == "SVGPatternElement");
    assert(S("vw.constructor.name") == "SVGViewElement");
    // An element with an interface of its own that is not a graphics element:
    // SVG 2 §5.13 gives `defs` its own, and it inherits SVGElement.
    make("desc", "desc");
    make("grad", "linearGradient");
    assert(S("defs.constructor.name") == "SVGDefsElement");
    assert(S("desc.constructor.name") == "SVGDescElement");
    assert(S("grad.constructor.name") == "SVGLinearGradientElement");
    assert(S("defs instanceof SVGGraphicsElement") == "false");
    // A tag named by no interface is still a plain SVGElement.
    make("odd", "somethingElse");
    assert(S("odd.constructor.name") == "SVGElement");

    // The chain, not just the leaf name: a graphics element is a
    // SVGGraphicsElement and an SVGElement, and an SVGElement is an Element.
    assert(S("var chain=[];for(var c=g.constructor;c&&c.name;c=Object.getPrototypeOf(c))chain.push(c.name);"
             "chain.join('<')") == "SVGGElement<SVGGraphicsElement<SVGElement<Element<Node<EventTarget");
    assert(S("g instanceof SVGGraphicsElement") == "true");
    assert(S("g instanceof SVGElement") == "true");
    assert(S("rect instanceof SVGGraphicsElement") == "true");
    assert(S("svg instanceof SVGGraphicsElement") == "true");
    // A symbol is a graphics element in SVG 2; a marker and a pattern are not
    // (they are SVGElement, with SVGFitToViewBox included).
    assert(S("sym instanceof SVGGraphicsElement") == "true");
    assert(S("mk instanceof SVGGraphicsElement") == "false");
    assert(S("pat instanceof SVGGraphicsElement") == "false");

    // --- viewBox on the elements that have it, and nowhere else. ---
    assert(S("typeof svg.viewBox") == "object");
    assert(S("svg.viewBox instanceof SVGAnimatedRect") == "true");
    // The value comes from the attribute, and it is live, on each element.
    assert(S("sym.setAttribute('viewBox','1 2 3 4');"
             "var b=sym.viewBox.baseVal;[b.x,b.y,b.width,b.height].join('|')") == "1|2|3|4");
    assert(S("mk.setAttribute('viewBox','5 6 7 8');mk.viewBox.baseVal.y") == "6");
    assert(S("pat.setAttribute('viewBox','9 10 11 12');pat.viewBox.baseVal.width") == "11");
    assert(S("vw.setAttribute('viewBox','0 0 24 24');vw.viewBox.baseVal.height") == "24");
    // Writing through the rectangle writes the attribute, on a symbol too.
    assert(S("sym.viewBox.baseVal.width=30;sym.getAttribute('viewBox')") == "1 2 30 4");
    // The same object each time, and the animated half stays distinct.
    assert(S("sym.viewBox===sym.viewBox") == "true");
    assert(S("sym.viewBox.baseVal===sym.viewBox.animVal") == "false");
    // An element with no viewBox in SVG 2 keeps none: the fix must not have
    // given every SVG element one.
    assert(S("typeof g.viewBox") == "undefined");
    assert(S("typeof rect.viewBox") == "undefined");
    assert(S("typeof defs.viewBox") == "undefined");

    // --- getBBox(), on graphics elements and only on them. ---
    assert(S("typeof g.getBBox") == "function");
    assert(S("typeof rect.getBBox") == "function");
    assert(S("typeof svg.getBBox") == "function");
    // SVG 2 §4.4.2 calls it with an optional SVGBoundingBoxOptions dictionary.
    assert(S("g.getBBox.length") == "0");
    assert(S("g.getBBox() instanceof SVGRect") == "true");
    assert(S("var bb=g.getBBox();[bb.x,bb.y,bb.width,bb.height].join('|')") == "0|0|0|0");
    // A detached rectangle is writable: it is not an animated half, so a script
    // may set its members (SVGRect's are attributes, not read-only ones).
    assert(S("var bb2=g.getBBox();bb2.x=5;bb2.width=7;bb2.x+'|'+bb2.width") == "5|7");
    // Each call makes a new rectangle; they are not the element's viewBox.
    assert(S("g.getBBox()!==g.getBBox()") == "true");
    // An element that is not a graphics element has no getBBox: SVG 2 puts it
    // on SVGGraphicsElement, and a `<defs>` is not one.
    assert(S("typeof defs.getBBox") == "undefined");
    assert(S("typeof mk.getBBox") == "undefined");

    // --- createSVGRect(), on SVGSVGElement. ---
    assert(S("typeof svg.createSVGRect") == "function");
    assert(S("svg.createSVGRect().constructor.name") == "SVGRect");
    assert(S("var cr=svg.createSVGRect();[cr.x,cr.y,cr.width,cr.height].join('|')") == "0|0|0|0");
    assert(S("svg.createSVGRect() instanceof SVGRect") == "true");
    assert(S("svg.createSVGRect()!==svg.createSVGRect()") == "true");
    // It is SVGSVGElement's, so a `<g>` does not have it.
    assert(S("typeof g.createSVGRect") == "undefined");

    // --- The interfaces are not constructible. ---
    for (const char* name : {"SVGGraphicsElement", "SVGSVGElement", "SVGSymbolElement",
                             "SVGMarkerElement", "SVGPatternElement", "SVGGElement", "SVGRectElement"}) {
        assert(S(("try { new " + std::string(name) + "(); 'no'; } catch(e) { e.name; }").c_str()) == "TypeError");
    }

    // --- The parent interface objects are where Web IDL puts them. ---
    assert(S("Object.getPrototypeOf(SVGGElement)===SVGGraphicsElement") == "true");
    assert(S("Object.getPrototypeOf(SVGGraphicsElement)===SVGElement") == "true");
    assert(S("Object.getPrototypeOf(SVGSymbolElement)===SVGGraphicsElement") == "true");
    assert(S("Object.getPrototypeOf(SVGMarkerElement)===SVGElement") == "true");
}
