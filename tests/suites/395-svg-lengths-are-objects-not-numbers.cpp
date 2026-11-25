// Phase 395, finding 395: the length-valued SVG attributes were undefined.
//
// Phase 374 gave the SVG elements their interfaces and Phase 381 gave
// SVGFitToViewBox its second attribute. What remained was the largest set:
// every geometry attribute that takes a `<length>` was simply absent.
//
//   <rect>.x, .y, .width, .height, .rx, .ry
//   <circle>.cx, .cy, .r          <ellipse>.cx, .cy, .rx, .ry
//   <line>.x1, .y1, .x2, .y2      <svg>.x, .y, .width, .height
//   <text>/<tspan>.x, .y          <marker>.refX, .refY, .markerWidth, .markerHeight
//
// all answered `undefined`. SVG 2 gives each an SVGAnimatedLength whose baseVal
// and animVal are SVGLengths -- objects, not numbers -- so a page could read
// `rect.getAttribute('x')` as a string and nothing else.
//
// SVGLength is the one type here with real semantics to get wrong, and three
// are asserted below:
//
//   1. `unitType` and `valueInSpecifiedUnits` are what was **written**; `value`
//      is the conversion to user units. `height="5mm"` reads (7, 5) by unit and
//      number, and 5 * 96/25.4 by value. A type that converted on the way in
//      could not answer the first two, which is why this does not reuse the
//      renderer's parser (that one resolves to px immediately).
//   2. A unit the constants do not name (`ch`, or a `calc()`) reads unitType
//      UNKNOWN and valueInSpecifiedUnits 0 (§4.5.2), but the attribute keeps
//      the text: overwriting it with "" would delete a value the DOM can read
//      but not express. That bug was made and caught here.
//   3. `em`/`ex` are units, and `e` is also an exponent marker. A parser that
//      takes a trailing `e` greedily as an exponent rejects every `em` and `ex`
//      length; that bug was made and caught here too.
//
// Reference: SVG 2 §4.5.2 (SVGLength, its constants, its members and their
// exceptions), §4.6.5 (SVGAnimatedLength), §A.1.1 (the initial value: 0 user
// units, SVG_LENGTHTYPE_NUMBER). jsdom has no SVGAnimatedLength at all -- it
// answers `undefined` for every one of these attributes -- so the
// specification is the only reference for this file.
TEST(svg_lengths_are_objects_not_numbers) {
    TestPage page;
    auto& js = page.js;
    auto S = [&](const char* code) { return js.eval(code).to_string(); };
    auto make = [&](const std::string& var, const std::string& tag) {
        S(("var " + var + "=document.createElementNS('http://www.w3.org/2000/svg','" + tag + "');").c_str());
    };

    make("svg", "svg");
    make("rect", "rect");
    make("circle", "circle");
    make("ellipse", "ellipse");
    make("line", "line");
    make("text", "text");
    make("tspan", "tspan");
    make("marker", "marker");
    make("g", "g");
    make("path", "path");

    // --- The two interfaces. ---
    assert(S("rect.x instanceof SVGAnimatedLength") == "true");
    assert(S("rect.x.baseVal instanceof SVGLength") == "true");
    assert(S("rect.x.animVal instanceof SVGLength") == "true");
    // [SameObject] on the animated length and on both halves.
    assert(S("rect.x===rect.x") == "true");
    assert(S("var ax=rect.x;ax.baseVal===ax.baseVal") == "true");
    assert(S("var ay=rect.x;ay.animVal===ay.animVal") == "true");
    assert(S("var az=rect.x;az.baseVal===az.animVal") == "false");

    // --- Which element has which attribute. ---
    // The positive half, one element each.
    assert(S("typeof rect.rx") == "object");
    assert(S("typeof circle.cx") == "object");
    assert(S("typeof circle.r") == "object");
    assert(S("typeof ellipse.ry") == "object");
    assert(S("typeof line.x1") == "object");
    assert(S("typeof line.y2") == "object");
    assert(S("typeof text.x") == "object");
    assert(S("typeof tspan.y") == "object");
    assert(S("typeof marker.refX") == "object");
    assert(S("typeof marker.markerHeight") == "object");
    assert(S("typeof svg.width") == "object");
    // And the negative half: an element without the attribute in SVG 2 answers
    // undefined rather than an object that would mean nothing.
    assert(S("typeof rect.cx") == "undefined");
    assert(S("typeof circle.rx") == "undefined");
    assert(S("typeof circle.x1") == "undefined");
    assert(S("typeof ellipse.r") == "undefined");
    assert(S("typeof line.cx") == "undefined");
    assert(S("typeof text.width") == "undefined");
    assert(S("typeof g.x") == "undefined");
    assert(S("typeof path.x") == "undefined");
    assert(S("typeof marker.x") == "undefined");
    assert(S("typeof rect.refX") == "undefined");

    // --- The constants, at the values the IDL gives them. ---
    assert(S("SVGLength.SVG_LENGTHTYPE_UNKNOWN") == "0");
    assert(S("SVGLength.SVG_LENGTHTYPE_NUMBER") == "1");
    assert(S("SVGLength.SVG_LENGTHTYPE_PERCENTAGE") == "2");
    assert(S("SVGLength.SVG_LENGTHTYPE_EMS") == "3");
    assert(S("SVGLength.SVG_LENGTHTYPE_EXS") == "4");
    assert(S("SVGLength.SVG_LENGTHTYPE_PX") == "5");
    assert(S("SVGLength.SVG_LENGTHTYPE_CM") == "6");
    assert(S("SVGLength.SVG_LENGTHTYPE_MM") == "7");
    assert(S("SVGLength.SVG_LENGTHTYPE_IN") == "8");
    assert(S("SVGLength.SVG_LENGTHTYPE_PT") == "9");
    assert(S("SVGLength.SVG_LENGTHTYPE_PC") == "10");

    // --- The initial value: 0 user units, type NUMBER (§A.1.1). ---
    assert(S("var v0=rect.x.baseVal;[v0.unitType,v0.valueInSpecifiedUnits,v0.value,v0.valueAsString].join('|')") == "1|0|0|0");

    // --- Reading: the unit is preserved, the value converts. ---
    assert(S("rect.setAttribute('x','10');"
             "var v1=rect.x.baseVal;[v1.unitType,v1.valueInSpecifiedUnits,v1.value,v1.valueAsString].join('|')") == "1|10|10|10");
    assert(S("rect.setAttribute('y','5mm');"
             "var v2=rect.y.baseVal;[v2.unitType,v2.valueInSpecifiedUnits,v2.valueAsString].join('|')") == "7|5|5mm");
    // 5mm in user units is 5 * 96 / 25.4 = 18.897...
    assert(S("Math.abs(rect.y.baseVal.value-5*96/25.4)<0.001") == "true");
    assert(S("rect.setAttribute('width','30e1');rect.width.baseVal.valueInSpecifiedUnits") == "300");
    assert(S("rect.setAttribute('height','3.5cm');"
             "var v3=rect.height.baseVal;[v3.unitType,v3.valueInSpecifiedUnits].join('|')") == "6|3.5");
    assert(S("rect.setAttribute('rx','50%');"
             "var v4=rect.rx.baseVal;[v4.unitType,v4.valueInSpecifiedUnits,v4.value].join('|')") == "2|50|50");

    // Every named unit reaches its constant, including the two that share a
    // letter with the exponent marker.
    assert(S("var seq=[];['7em','7ex','3ch','1cm','2mm','3in','4pt','5pc','6px','50%','12'].forEach(function(t){"
             "rect.setAttribute('x',t);seq.push(rect.x.baseVal.unitType);});seq.join(',')") == "3,4,0,6,7,8,9,10,5,2,1");
    // A non-scalar unit is readable but not expressible: UNKNOWN and 0, and the
    // attribute keeps the text it was given.
    assert(S("rect.setAttribute('x','3ch');"
             "var v5=rect.x.baseVal;[v5.unitType,v5.valueInSpecifiedUnits,v5.valueAsString,rect.getAttribute('x')].join('|')") == "0|0|3ch|3ch");
    // An unparseable attribute reads as the initial value.
    assert(S("rect.setAttribute('x','not a length');rect.x.baseVal.unitType") == "1");
    assert(S("rect.removeAttribute('x');rect.x.baseVal.valueInSpecifiedUnits") == "0");

    // --- Writing: each member writes the attribute in its own way. ---
    // valueInSpecifiedUnits replaces the number and keeps the unit.
    assert(S("rect.setAttribute('x','5mm');rect.x.baseVal.valueInSpecifiedUnits=25;rect.getAttribute('x')") == "25mm");
    // value is in user units, so writing it keeps the unit and converts the
    // number back into it. A plain number has no unit to convert.
    assert(S("rect.setAttribute('x','10');rect.x.baseVal.value=7;rect.getAttribute('x')") == "7");
    // valueAsString parses the whole value, unit included.
    assert(S("rect.x.baseVal.valueAsString='3.5cm';rect.getAttribute('x')+'|'+rect.x.baseVal.unitType") == "3.5cm|6");
    assert(S("rect.x.baseVal.valueAsString='42';rect.getAttribute('x')") == "42");
    // And it refuses what is not a length, with SyntaxError (§4.5.2), leaving
    // the attribute alone.
    assert(S("rect.setAttribute('x','10');"
             "try { rect.x.baseVal.valueAsString='nonsense'; 'no'; } catch(e) { e.name; }") == "SyntaxError");
    assert(S("rect.getAttribute('x')") == "10");

    // --- newValueSpecifiedUnits / convertToSpecifiedUnits. ---
    assert(S("rect.x.baseVal.newValueSpecifiedUnits(SVGLength.SVG_LENGTHTYPE_PX,42);"
             "rect.getAttribute('x')+'|'+rect.x.baseVal.unitType") == "42px|5");
    assert(S("try { rect.x.baseVal.newValueSpecifiedUnits(0,1); 'no'; } catch(e) { e.name; }") == "NotSupportedError");
    assert(S("try { rect.x.baseVal.newValueSpecifiedUnits(11,1); 'no'; } catch(e) { e.name; }") == "NotSupportedError");
    // 5mm is 0.5cm: the same absolute length in another unit.
    assert(S("rect.setAttribute('x','5mm');rect.x.baseVal.convertToSpecifiedUnits(SVGLength.SVG_LENGTHTYPE_CM);"
             "var c=rect.x.baseVal;c.unitType+'|'+Math.round(c.valueInSpecifiedUnits*1000)/1000") == "6|0.5");
    // 1in is 96 user units is 72pt.
    assert(S("rect.setAttribute('x','1in');rect.x.baseVal.convertToSpecifiedUnits(SVGLength.SVG_LENGTHTYPE_PT);"
             "var d=rect.x.baseVal;d.unitType+'|'+Math.round(d.valueInSpecifiedUnits)") == "9|72");
    assert(S("try { rect.x.baseVal.convertToSpecifiedUnits(0); 'no'; } catch(e) { e.name; }") == "NotSupportedError");

    // --- The animated half is read-only (§4.5.2). ---
    assert(S("try { rect.x.animVal.value=1; 'no'; } catch(e) { e.name; }") == "NoModificationAllowedError");
    assert(S("try { rect.x.animVal.valueInSpecifiedUnits=1; 'no'; } catch(e) { e.name; }") == "NoModificationAllowedError");
    assert(S("try { rect.x.animVal.valueAsString='1px'; 'no'; } catch(e) { e.name; }") == "NoModificationAllowedError");
    assert(S("try { rect.x.animVal.newValueSpecifiedUnits(1,1); 'no'; } catch(e) { e.name; }") == "NoModificationAllowedError");
    assert(S("try { rect.x.animVal.convertToSpecifiedUnits(1); 'no'; } catch(e) { e.name; }") == "NoModificationAllowedError");
    // baseVal and animVal agree in value because nothing animates them.
    assert(S("rect.setAttribute('x','5mm');"
             "rect.x.baseVal.valueInSpecifiedUnits+'|'+rect.x.animVal.valueInSpecifiedUnits+'|'+"
             "rect.x.animVal.unitType") == "5|5|7");

    // --- Writing through one element's length does not disturb another's. ---
    assert(S("var r2=document.createElementNS('http://www.w3.org/2000/svg','rect');"
             "r2.setAttribute('x','3');rect.setAttribute('x','9');"
             "r2.x.baseVal.valueInSpecifiedUnits+'|'+rect.x.baseVal.valueInSpecifiedUnits") == "3|9");

    // --- The interfaces are not constructible here. ---
    assert(S("try { new SVGLength(); 'no'; } catch(e) { e.name; }") == "TypeError");
    assert(S("try { new SVGAnimatedLength(); 'no'; } catch(e) { e.name; }") == "TypeError");
}
