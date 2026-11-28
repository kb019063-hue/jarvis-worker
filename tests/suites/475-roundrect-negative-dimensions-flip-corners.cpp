// Phase 475, finding 475: negative dimensions remap corner radii after normalization.
TEST(round_rect_flips_corner_radii_with_negative_dimensions) {
    TestPage page;
    JsInterpreter& js = page.js;

    js.eval(
        "function canvas475() {"
        "  var c = document.createElement('canvas'); c.width = 96; c.height = 80;"
        "  var g = c.getContext('2d'); g.fillStyle = '#000000'; return g;"
        "}"
        "function same475(a, b) {"
        "  var ad = a.getImageData(0, 0, 96, 80).data;"
        "  var bd = b.getImageData(0, 0, 96, 80).data;"
        "  for (var i = 0; i < ad.length; ++i) if (ad[i] !== bd[i]) return false;"
        "  return true;"
        "}"
        "function direct475(x, y, w, h, radii) {"
        "  var g = canvas475(); g.beginPath(); g.roundRect(x, y, w, h, radii); g.fill(); return g;"
        "}"
        "function recorded475(x, y, w, h, radii) {"
        "  var g = canvas475(), p = new Path2D(); p.roundRect(x, y, w, h, radii);"
        "  g.fill(p); return g;"
        "}");

    // Distinct elliptical corners make every permutation observable. Keep the
    // radii non-overlapping so the result depends only on corner identity.
    js.eval(
        "var radii475 = [{x:13,y:4}, {x:3,y:11}, {x:8,y:2}, {x:5,y:9}];"
        "var flipX475 = [radii475[1], radii475[0], radii475[3], radii475[2]];"
        "var flipY475 = [radii475[3], radii475[2], radii475[1], radii475[0]];"
        "var flipXY475 = [radii475[2], radii475[3], radii475[0], radii475[1]];"
        "var control475 = direct475(14, 8, 44, 30, radii475);"
        "var expectX475 = direct475(14, 8, 44, 30, flipX475);"
        "var expectY475 = direct475(14, 10, 44, 30, flipY475);"
        "var expectXY475 = direct475(14, 10, 44, 30, flipXY475);");

    // Prove the chosen radii make each flip visible; otherwise a missing swap
    // could satisfy the relational check because the corners happened to match.
    assert(js.eval("!same475(control475, expectX475)").to_string() == "true");
    assert(js.eval("!same475(control475, expectY475)").to_string() == "true");
    assert(js.eval("!same475(control475, expectXY475)").to_string() == "true");

    // A negative width moves the origin left and exchanges each row's left and
    // right corner identities: TL↔TR and BL↔BR.
    js.eval(
        "var directX475 = direct475(58, 8, -44, 30, radii475);"
        "var pathX475 = recorded475(58, 8, -44, 30, radii475);"
        "var pathExpectX475 = recorded475(14, 8, 44, 30, flipX475);");
    assert(js.eval("same475(directX475, expectX475)").to_string() == "true");
    assert(js.eval("same475(pathX475, pathExpectX475)").to_string() == "true");
    assert(js.eval("same475(directX475, pathX475)").to_string() == "true");

    // A negative height moves the origin up and exchanges top/bottom identities:
    // TL↔BL and TR↔BR.
    js.eval(
        "var directY475 = direct475(14, 40, 44, -30, radii475);"
        "var pathY475 = recorded475(14, 40, 44, -30, radii475);"
        "var pathExpectY475 = recorded475(14, 10, 44, 30, flipY475);");
    assert(js.eval("same475(directY475, expectY475)").to_string() == "true");
    assert(js.eval("same475(pathY475, pathExpectY475)").to_string() == "true");
    assert(js.eval("same475(directY475, pathY475)").to_string() == "true");

    // Applying both flips composes the two swaps, rotating the corner identities
    // by 180 degrees while preserving the positive normalized rectangle.
    js.eval(
        "var directXY475 = direct475(58, 40, -44, -30, radii475);"
        "var pathXY475 = recorded475(58, 40, -44, -30, radii475);"
        "var pathExpectXY475 = recorded475(14, 10, 44, 30, flipXY475);");
    assert(js.eval("same475(directXY475, expectXY475)").to_string() == "true");
    assert(js.eval("same475(pathXY475, pathExpectXY475)").to_string() == "true");
    assert(js.eval("same475(directXY475, pathXY475)").to_string() == "true");
}
