// Phase 454, finding 454: `roundRect` keeps both axes of an elliptical corner.
//
// Before this phase, both public routes converted a DOMPoint-like corner to one
// number. `[20, 5]` became NaN and `{x:20,y:5}` did too, then the geometry's
// non-finite clamp made either a **zero** radius. A requested ellipse was a square
// corner, on both the context and `Path2D`, so merely comparing those two routes
// could not find it.
//
// The independent oracle below is the context's existing `ellipse()` method. The
// first part builds one top-left corner by hand with line segments and an ellipse,
// then requires direct `roundRect` and recorded `Path2D.roundRect` to match every
// RGBA byte. Later parts pin x/y orientation, DOMPointInit objects, the 1/2/3/4
// spreading rule, and HTML's one common overlap factor.

TEST(round_rect_preserves_the_horizontal_and_vertical_radius_of_each_corner) {
    TestPage page;
    JsInterpreter& js = page.js;

    js.eval(
        "function canvas454(w, h) {"
        "  var c = document.createElement('canvas'); c.width = w; c.height = h;"
        "  var g = c.getContext('2d'); g.fillStyle = '#000000'; return g;"
        "}"
        "function alpha454(g, x, y) { return g.getImageData(x, y, 1, 1).data[3]; }"
        "function equal454(a, b) {"
        "  var ad = a.getImageData(0, 0, 80, 70).data;"
        "  var bd = b.getImageData(0, 0, 80, 70).data;"
        "  if (ad.length !== bd.length) return false;"
        "  for (var i = 0; i < ad.length; ++i) if (ad[i] !== bd[i]) return false;"
        "  return true;"
        "}"
        "function direct454(radii, x, y, w, h) {"
        "  var g = canvas454(80, 70); g.beginPath(); g.roundRect(x, y, w, h, radii); g.fill(); return g;"
        "}"
        "function path454(radii, x, y, w, h) {"
        "  var g = canvas454(80, 70), p = new Path2D(); p.roundRect(x, y, w, h, radii); g.fill(p); return g;"
        "}");

    // --- Part 1: an independent geometry oracle. The top-left corner has a 20px
    // horizontal radius and a 5px vertical one; the other three are square. The
    // hand-written path uses `ellipse()` directly, so two identically broken
    // `roundRect` bindings cannot satisfy this comparison.
    js.eval(
        "var manual = canvas454(80, 70); manual.beginPath();"
        "manual.moveTo(25, 5); manual.lineTo(55, 5); manual.lineTo(55, 45);"
        "manual.lineTo(5, 45); manual.lineTo(5, 10);"
        "manual.ellipse(25, 10, 20, 5, 0, Math.PI, Math.PI * 1.5);"
        "manual.closePath(); manual.fill();"
        "var directPair = direct454([[20, 5], 0, 0, 0], 5, 5, 50, 40);"
        "var pathPair = path454([[20, 5], 0, 0, 0], 5, 5, 50, 40);");
    assert(js.eval("equal454(manual, directPair)").to_string() == "true");
    assert(js.eval("equal454(manual, pathPair)").to_string() == "true");
    assert(js.eval("equal454(directPair, pathPair)").to_string() == "true");
    // Under the old zero-radius path this pixel was fully opaque. It lies on the
    // antialiased boundary now, while pixels just beyond each end of the arc stay
    // solid. Thresholds avoid pinning one rasteriser rounding bit.
    assert(js.eval("alpha454(directPair, 7, 7) < 200").to_string() == "true");
    assert(js.eval("alpha454(directPair, 10, 7) === 255").to_string() == "true");
    assert(js.eval("alpha454(directPair, 7, 15) === 255").to_string() == "true");
    assert(js.eval("alpha454(directPair, 54, 6) === 255").to_string() == "true");

    // --- Part 2: x and y are not interchangeable and not collapsed to an average.
    // At (10,6), the tall 5×20 corner is inside while the flat 20×5 one is on the
    // boundary. At (6,10), the relation is reversed.
    js.eval(
        "var flat454 = direct454([{x:20, y:5}, 0, 0, 0], 5, 5, 50, 40);"
        "var tall454 = direct454([{x:5, y:20}, 0, 0, 0], 5, 5, 50, 40);");
    assert(js.eval("alpha454(flat454, 10, 6) < 200").to_string() == "true");
    assert(js.eval("alpha454(tall454, 10, 6) === 255").to_string() == "true");
    assert(js.eval("alpha454(flat454, 6, 10) === 255").to_string() == "true");
    assert(js.eval("alpha454(tall454, 6, 10) < 200").to_string() == "true");

    // --- Part 3: the standard spelling is DOMPointInit, both as the entire radii
    // argument (one corner value spread to all four) and as members of a sequence.
    // The `[x,y]` inner-array spelling retained by this engine must produce the
    // same pixels, and `Path2D` must preserve either representation.
    js.eval(
        "var objAllD = direct454({x:20, y:5}, 5, 5, 50, 40);"
        "var objAllP = path454({x:20, y:5}, 5, 5, 50, 40);"
        "var arrAllD = direct454([[20, 5]], 5, 5, 50, 40);"
        "var arrAllP = path454([[20, 5]], 5, 5, 50, 40);"
        "var objOneD = direct454([{x:20, y:5}, 0, 0, 0], 5, 5, 50, 40);"
        "var objOneP = path454([{x:20, y:5}, 0, 0, 0], 5, 5, 50, 40);");
    assert(js.eval("equal454(objAllD, objAllP)").to_string() == "true");
    assert(js.eval("equal454(objAllD, arrAllD)").to_string() == "true");
    assert(js.eval("equal454(arrAllD, arrAllP)").to_string() == "true");
    assert(js.eval("equal454(objOneD, manual)").to_string() == "true");
    assert(js.eval("equal454(objOneD, objOneP)").to_string() == "true");

    // Missing dictionary members default to zero. With only x, one zero axis makes
    // a square corner rather than a diagonal segment; the direct and path forms
    // must agree on that degenerate ellipse.
    js.eval(
        "var xOnlyD = direct454([{x:20}, 0, 0, 0], 5, 5, 50, 40);"
        "var xOnlyP = path454([{x:20}, 0, 0, 0], 5, 5, 50, 40);"
        "var squareD = direct454([0, 0, 0, 0], 5, 5, 50, 40);");
    assert(js.eval("equal454(xOnlyD, xOnlyP)").to_string() == "true");
    assert(js.eval("equal454(xOnlyD, squareD)").to_string() == "true");

    // --- Part 4: spreading a sequence spreads the complete pair, not x and y as
    // separate corners. Two values alternate over opposite corners. Three values
    // repeat the second at bottom-left. Four keep clockwise order.
    js.eval(
        "var twoD = direct454([{x:20,y:5}, {x:5,y:20}], 5, 5, 50, 40);"
        "var twoP = path454([{x:20,y:5}, {x:5,y:20}], 5, 5, 50, 40);"
        "var threeD = direct454([{x:20,y:5}, {x:5,y:20}, {x:12,y:3}], 5, 5, 50, 40);"
        "var threeP = path454([{x:20,y:5}, {x:5,y:20}, {x:12,y:3}], 5, 5, 50, 40);"
        "var fourD = direct454([{x:20,y:5}, {x:5,y:20}, {x:12,y:3}, {x:3,y:12}], 5, 5, 50, 40);"
        "var fourP = path454([{x:20,y:5}, {x:5,y:20}, {x:12,y:3}, {x:3,y:12}], 5, 5, 50, 40);");
    assert(js.eval("equal454(twoD, twoP)").to_string() == "true");
    assert(js.eval("equal454(threeD, threeP)").to_string() == "true");
    assert(js.eval("equal454(fourD, fourP)").to_string() == "true");
    // Changing only the fourth pair changes only the bottom-left family of pixels;
    // this guards the storage/replay order rather than just total ink.
    assert(js.eval("alpha454(threeD, 6, 43) !== alpha454(fourD, 6, 43)").to_string() == "true");
    assert(js.eval("alpha454(threeD, 53, 7) === alpha454(fourD, 53, 7)").to_string() == "true");

    // --- Part 5: overlap uses one common factor, horizontal sums against width and
    // vertical sums against height. Four 15×5 corners on a 20×20 box scale to
    // 10×(10/3); four 5×15 corners scale to (10/3)×10. Comparing to those explicit
    // values pins both axes and preserves each ellipse's aspect ratio.
    js.eval(
        "var wideOver = direct454([{x:15,y:5}], 5, 5, 20, 20);"
        "var wideExpected = direct454([{x:10,y:10/3}], 5, 5, 20, 20);"
        "var tallOver = direct454([{x:5,y:15}], 5, 5, 20, 20);"
        "var tallExpected = direct454([{x:10/3,y:10}], 5, 5, 20, 20);"
        "var widePath = path454([{x:15,y:5}], 5, 5, 20, 20);"
        "var tallPath = path454([{x:5,y:15}], 5, 5, 20, 20);");
    assert(js.eval("equal454(wideOver, wideExpected)").to_string() == "true");
    assert(js.eval("equal454(tallOver, tallExpected)").to_string() == "true");
    assert(js.eval("equal454(wideOver, widePath)").to_string() == "true");
    assert(js.eval("equal454(tallOver, tallPath)").to_string() == "true");
    assert(js.eval("!equal454(wideOver, tallOver)").to_string() == "true");

    // --- Part 6: circular numbers still go through the old compact operation and
    // remain pixel-identical to an equal-axis point. Phase 443's empty-sequence
    // square fallback also remains intact.
    js.eval(
        "var circleD = direct454(8, 5, 5, 50, 40);"
        "var circleP = path454(8, 5, 5, 50, 40);"
        "var circlePoint = direct454({x:8,y:8}, 5, 5, 50, 40);"
        "var emptyD = direct454([], 5, 5, 50, 40);"
        "var emptyP = path454([], 5, 5, 50, 40);"
        "var zeroD = direct454(0, 5, 5, 50, 40);");
    assert(js.eval("equal454(circleD, circleP)").to_string() == "true");
    assert(js.eval("equal454(circleD, circlePoint)").to_string() == "true");
    assert(js.eval("equal454(emptyD, emptyP)").to_string() == "true");
    assert(js.eval("equal454(emptyD, zeroD)").to_string() == "true");

    // --- Part 7: a recorded ellipse remains in user space. The CTM at use doubles
    // both axes; sampling beyond the unscaled 50px right edge proves replay still
    // applies it rather than baking the pair into device coordinates.
    js.eval(
        "var scaled = canvas454(130, 100), scaledPath = new Path2D();"
        "scaledPath.roundRect(5, 5, 50, 30, [{x:20,y:5}]);"
        "scaled.scale(2, 2); scaled.fill(scaledPath);");
    assert(js.eval("alpha454(scaled, 100, 30) === 255").to_string() == "true");
    assert(js.eval("alpha454(scaled, 115, 30) === 0").to_string() == "true");
}
