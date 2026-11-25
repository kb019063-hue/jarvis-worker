// Phase 410, finding 410: a text position is a list, not a length.
//
// Phase 395 gave every `<length>` geometry attribute an SVGAnimatedLength.
// That is right for `rect.x`, `circle.r` and `svg.width`, and wrong for four
// attributes on the SVG text elements: SVG 2 §4.5.3 and §4.6.3 give `x`, `y`,
// `dx` and `dy` on `text`/`tspan` an SVGAnimatedLengthList, because one
// attribute holds a list of positions -- one per glyph run.
//
// A single-length model cannot represent one at all, and the tree Phase 395
// left measured it three ways for `x="10 20 30"`:
//
//   text.x.constructor.name      SVGAnimatedLength       (should be the list)
//   text.x.baseVal.numberOfItems undefined
//   text.x.baseVal.value         0                       (should not exist)
//
// So three positions read back as zero, and a write replaced all three with
// one. The same class of bug the 395 write-up records for `3ch`, in the place
// 395 introduced it.
//
// Two more things are asserted here because they were wrong too:
//
//   * the list is **live**: `getItem(1).value = 99` rewrites the whole
//     attribute, `10 20 30` -> `10 99 30`, because the item is a view of entry
//     1 and not a copy of it;
//   * `SVGLength.value` is in **user units** (§4.5.2), so writing it converts
//     back into the attribute's own unit -- `value = 100` on `x="1cm"` is
//     `2.64583cm`, not `100cm`. Phase 395 wrote the number straight into
//     `valueInSpecifiedUnits`, which made it 37.795 times too long.
//
// Reference: Chromium (pinned headless, `tools/conformance/verify_svg_length_lists.py`
// is 28/28 with it). jsdom has neither SVGAnimatedLengthList nor SVGLength, so
// it is no reference here at all.
TEST(text_positions_are_length_lists) {
    TestPage page;
    auto& js = page.js;
    auto S = [&](const char* code) { return js.eval(code).to_string(); };
    auto make = [&](const std::string& var, const std::string& tag) {
        S(("var " + var + "=document.createElementNS('http://www.w3.org/2000/svg','" + tag + "');").c_str());
    };

    make("text", "text");
    make("tspan", "tspan");
    make("path", "textPath");
    make("rect", "rect");
    make("g", "g");

    // --- The interfaces exist and are the right ones (§4.5.3, §4.6.3). ---
    assert(S("typeof SVGLengthList") == "function");
    assert(S("typeof SVGAnimatedLengthList") == "function");
    assert(S("text.setAttribute('x','10 20 30');text.x instanceof SVGAnimatedLengthList") == "true");
    assert(S("text.x.baseVal instanceof SVGLengthList") == "true");
    assert(S("text.x.constructor.name") == "SVGAnimatedLengthList");
    assert(S("text.x.baseVal.constructor.name") == "SVGLengthList");
    // §4.6.3's [SameObject]; the two halves are different objects.
    assert(S("var a=text.x;a===text.x") == "true");
    assert(S("var b=text.x;b.baseVal===b.baseVal") == "true");
    assert(S("var c=text.x;c.animVal===c.animVal") == "true");
    assert(S("var d=text.x;d.baseVal===d.animVal") == "false");

    // --- One attribute, three entries. ---
    assert(S("text.x.baseVal.numberOfItems") == "3");
    // `length` and `numberOfItems` are the same number.
    assert(S("text.x.baseVal.length") == "3");
    assert(S("text.x.baseVal[0].value+'|'+text.x.baseVal[1].value+'|'+text.x.baseVal[2].value") == "10|20|30");
    assert(S("text.x.baseVal[3]") == "undefined");
    assert(S("text.x.animVal.numberOfItems") == "3");
    // The entries are SVGLengths like any other.
    assert(S("text.x.baseVal[0] instanceof SVGLength") == "true");
    assert(S("text.x.baseVal.getItem(0).constructor.name") == "SVGLength");
    // A fresh object each ask, as a browser gives: `getItem(0) !== getItem(0)`.
    assert(S("var e=text.x.baseVal;e.getItem(0)===e.getItem(0)") == "false");

    // --- All four lists, and only on the elements that have them. ---
    assert(S("text.setAttribute('y','5');text.y.baseVal.numberOfItems") == "1");
    assert(S("text.setAttribute('dx','1 2');text.dx instanceof SVGAnimatedLengthList") == "true");
    assert(S("text.dx.baseVal.numberOfItems") == "2");
    assert(S("text.setAttribute('dy','3');text.dy.baseVal[0].value") == "3");
    assert(S("tspan.setAttribute('x','7');tspan.x.baseVal[0].value") == "7");
    // `textPath` is an SVGTextContentElement, not a SVGTextPositioningElement:
    // its positions come from the path, so it has none of the four.
    assert(S("path.setAttribute('x','4');typeof path.x") == "undefined");
    assert(S("typeof path.dx") == "undefined");
    // The single-length attributes are untouched by any of this.
    assert(S("rect.setAttribute('x','3');rect.x instanceof SVGAnimatedLength") == "true");
    assert(S("rect.x.baseVal.value") == "3");
    assert(S("typeof g.x") == "undefined");

    // --- An absent attribute is an empty list, not a length of 0. ---
    assert(S("var t2=document.createElementNS('http://www.w3.org/2000/svg','text');t2.x.baseVal.numberOfItems") == "0");
    assert(S("t2.x.baseVal[0]") == "undefined");

    // --- The list is live: an item reads and writes the attribute. ---
    assert(S("text.setAttribute('x','10 20 30');text.x.baseVal.getItem(1).value=99;"
             "text.getAttribute('x')") == "10 99 30");
    assert(S("text.x.baseVal.numberOfItems") == "3");
    assert(S("text.x.baseVal[1].value") == "99");
    // The item went through user units into the unit the entry was written in.
    assert(S("var cm=document.createElementNS('http://www.w3.org/2000/svg','text');"
             "cm.setAttribute('x','1cm 2mm');cm.x.baseVal.getItem(0).value=100;"
             "cm.getAttribute('x')") == "2.64583cm 2mm");

    // --- The units of each entry survive (§4.5.2, per entry). ---
    assert(S("var u=document.createElementNS('http://www.w3.org/2000/svg','text');"
             "u.setAttribute('x','1cm 2mm');"
             "var L=u.x.baseVal;L[0].unitType+'|'+L[0].valueInSpecifiedUnits+'|'+L[0].valueAsString+"
             "'|'+L[1].unitType+'|'+L[1].valueInSpecifiedUnits") == "6|1|1cm|7|2");

    // --- The list grammar: comma-wsp, and a bad entry voids the whole list. ---
    assert(S("var w=document.createElementNS('http://www.w3.org/2000/svg','text');"
             "w.setAttribute('x','10,20 , 30');w.x.baseVal.numberOfItems") == "3");
    assert(S("w.x.baseVal[2].value") == "30");
    // §4.5.3: a value the grammar rejects is an empty list, never a partial one.
    assert(S("var bad=document.createElementNS('http://www.w3.org/2000/svg','text');"
             "bad.setAttribute('x','10 bogus 30');bad.x.baseVal.numberOfItems") == "0");

    // --- The members §4.5.3 gives the list. ---
    // getItem and removeItem check the index; insertItemBefore clamps it.
    assert(S("try { text.x.baseVal.getItem(9); 'no'; } catch(e) { e.name; }") == "IndexSizeError");
    assert(S("try { text.x.baseVal.removeItem(-1); 'no'; } catch(e) { e.name; }") == "IndexSizeError");
    assert(S("text.setAttribute('x','10 20 30');text.x.baseVal.removeItem(1);text.getAttribute('x')") == "10 30");
    assert(S("text.x.baseVal.numberOfItems") == "2");
    assert(S("text.setAttribute('x','10 20 30');"
             "var L2=text.x.baseVal;L2.insertItemBefore(L2.getItem(0),0);text.getAttribute('x')") == "10 10 20 30");
    assert(S("text.x.baseVal.numberOfItems") == "4");
    assert(S("text.setAttribute('x','10 20 30');text.x.baseVal.clear();text.x.baseVal.numberOfItems") == "0");
    // An empty list has no text, so the attribute is removed rather than set to "".
    assert(S("text.hasAttribute('x')") == "false");
    assert(S("text.setAttribute('x','1 2');var N=text.x.baseVal;N.initialize(N.getItem(1));"
             "text.getAttribute('x')+'|'+N.numberOfItems") == "2|1");

    // --- An indexed write takes an SVGLength, and does not reorder. ---
    // A browser's list has an indexed setter (§4.5.3) that type-checks its
    // argument before the range, so `list[0] = 5` throws in sloppy code too.
    assert(S("var n=document.createElementNS('http://www.w3.org/2000/svg','text');"
             "n.setAttribute('x','1 2');"
             "try { n.x.baseVal[0]=5; 'no'; } catch(e) { e.name; }") == "TypeError");
    assert(S("var n2=document.createElementNS('http://www.w3.org/2000/svg','text');"
             "n2.setAttribute('x','1 2 3');n2.x.baseVal[0]=n2.x.baseVal.getItem(2);"
             "n2.getAttribute('x')") == "3 2 3");
    assert(S("n2.x.baseVal.numberOfItems") == "3");

    // --- The list enumerates its indices, and refuses nothing else. ---
    assert(S("var k=document.createElementNS('http://www.w3.org/2000/svg','text');"
             "k.setAttribute('x','1 2 3');Object.keys(k.x.baseVal).join(',')") == "0,1,2");
    assert(S("0 in k.x.baseVal") == "true");
    assert(S("3 in k.x.baseVal") == "false");

    // --- animVal's entries are read-only (§4.5.2 reached through §4.6.3). ---
    assert(S("var va=document.createElementNS('http://www.w3.org/2000/svg','text');"
             "va.setAttribute('x','1 2');"
             "try { va.x.animVal.getItem(0).value=5; 'no'; } catch(e) { e.name; }") == "NoModificationAllowedError");
    assert(S("va.getAttribute('x')") == "1 2");
    // baseVal still writes, and both halves read the same entries.
    assert(S("va.x.baseVal.getItem(0).value=5;va.getAttribute('x')+'|'+va.x.animVal.getItem(0).value") == "5 2|5");

    // --- SVGLength.value is in user units, whichever attribute carries it. ---
    // §4.5.2: `value` converts, `valueInSpecifiedUnits` does not. Writing the
    // first converts back into the entry's own unit.
    assert(S("var rv=document.createElementNS('http://www.w3.org/2000/svg','rect');"
             "rv.setAttribute('x','1cm');rv.x.baseVal.value=100;rv.getAttribute('x')") == "2.64583cm");
    assert(S("rv.x.baseVal.unitType") == "6");
    // The member that is in specified units takes the number as written.
    assert(S("rv.x.baseVal.valueInSpecifiedUnits=2;rv.getAttribute('x')") == "2cm");
    assert(S("rv.setAttribute('x','1in');rv.x.baseVal.value=36;rv.getAttribute('x')") == "0.375in");
    assert(S("rv.setAttribute('x','50%');rv.x.baseVal.value=25;rv.getAttribute('x')") == "25%");

    // --- The interfaces are not constructible here. ---
    assert(S("try { new SVGLengthList(); 'no'; } catch(e) { e.name; }") == "TypeError");
    assert(S("try { new SVGAnimatedLengthList(); 'no'; } catch(e) { e.name; }") == "TypeError");

    // --- A detached element's list goes with it. ---
    assert(S("var gone=(function(){var x=document.createElementNS('http://www.w3.org/2000/svg','text');"
             "x.setAttribute('x','1 2');return x.x.baseVal.getItem(1).value;})();gone") == "2");
}
