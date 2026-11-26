// Phase 443, finding 443: four radii through a `Path2D`.
//
// Phase 426 gave the engine `Path2D` and recorded `roundRect` with **one** radius.
// It converted the sequence itself with `ToNumber`; `[15,0,0,0]` became NaN and
// the geometry clamped that to zero. Measured before this phase:
//
//   ctx.roundRect(5, 5, 40, 40, [15, 0, 0, 0])  -> top-left rounded, the rest square
//   p.roundRect(5, 5, 40, 40, [15, 0, 0, 0])    -> all four corners square
//
// The same shape drawn two ways gave two pictures, and nothing threw. That is the
// failure this suite exists for, and the reason every part compares the two forms
// **pixel for pixel** rather than asserting one of them against a hand-written
// number: the claim is not "the corners are round", it is "a `Path2D` and the
// context agree".
//
// The rest of §4.12.5.1's `roundRect` grammar matters because a page does not only
// pass four numbers:
//
//   * a **sequence** of 1, 2, 3 or 4 values spreads over the corners by the CSS
//     rule — 2 values are (top-left, bottom-right) and (top-right, bottom-left);
//   * an **empty** sequence is a plain rectangle, which is where the context's own
//     binding falls through, and is *not* an empty path;
//   * **overlapping** radii are scaled down proportionally rather than clipped, so
//     a 20×20 box with 15-pixel corners must not swallow itself;
//   * a corner given as a **pair** `[x, y]` is read with `to_number()` by the
//     context's binding, which is `NaN` and so a square corner. That is a
//     limitation of `round_rect` being circular-only, and this suite pins the
//     `Path2D` to the same answer rather than fixing one side alone.

TEST(a_path2d_round_rect_draws_the_same_four_corners_as_the_context_own) {
    TestPage page;
    JsInterpreter& js = page.js;

    // Two canvases, the same rectangle drawn two ways, and a sum of the alpha over
    // the whole canvas. Corner probes and the final byte-for-byte comparison below
    // distinguish pictures that happen to contain the same amount of ink.
    js.eval("var ca = document.createElement('canvas'); ca.width = 60; ca.height = 60;"
            "var ga = ca.getContext('2d'); ga.fillStyle = '#000000';"
            "var cb = document.createElement('canvas'); cb.width = 60; cb.height = 60;"
            "var gb = cb.getContext('2d'); gb.fillStyle = '#000000';"
            "function ink(g) { var d = g.getImageData(0, 0, 60, 60).data, t = 0;"
            "for (var i = 3; i < d.length; i += 4) t += d[i]; return t; }"
            "function same(radii) {"
            "  ga.clearRect(0, 0, 60, 60); gb.clearRect(0, 0, 60, 60);"
            "  ga.beginPath(); var a = [5, 5, 40, 40]; for (var i = 0; i < radii.length; i++) a.push(radii[i]);"
            "  ga.roundRect.apply(ga, a); ga.fill();"
            "  var p = new Path2D(); var b = [5, 5, 40, 40]; for (var j = 0; j < radii.length; j++) b.push(radii[j]);"
            "  p.roundRect.apply(p, b); gb.fill(p);"
            "  return ink(ga) + ':' + ink(gb) + ':' + (ink(ga) === ink(gb)); }");

    // --- Part 1: the four distinct radii, which is what Phase 426 got wrong. The
    // top-left is the only rounded corner; the old `ToNumber(sequence)` path made
    // all four corners square and gives a different ink sum.
    assert(js.eval("same([[15, 0, 0, 0]]).split(':')[2]").to_string() == "true");
    // And the corner really is rounded rather than the assertion being satisfiable
    // by two identically wrong pictures: the top-left pixel is empty and the other
    // three corners are filled.
    js.eval("ga.clearRect(0, 0, 60, 60); ga.beginPath(); ga.roundRect(5, 5, 40, 40, [15, 0, 0, 0]); ga.fill();");
    assert(js.eval("ga.getImageData(7, 7, 1, 1).data[3]").to_string() == "0");
    assert(js.eval("ga.getImageData(43, 7, 1, 1).data[3]").to_string() == "255");
    assert(js.eval("ga.getImageData(43, 43, 1, 1).data[3]").to_string() == "255");
    // The `Path2D` form must show exactly the same; the old sequence-to-NaN path
    // fails here because its top-left corner stays square.
    js.eval("gb.clearRect(0, 0, 60, 60); var p1 = new Path2D();"
            "p1.roundRect(5, 5, 40, 40, [15, 0, 0, 0]); gb.fill(p1);");
    assert(js.eval("gb.getImageData(7, 7, 1, 1).data[3]").to_string() == "0");
    assert(js.eval("gb.getImageData(43, 7, 1, 1).data[3]").to_string() == "255");

    // --- Part 2: all four different, so a parser that keeps only the first or sorts
    // them cannot pass. Each corner is asked on its own.
    js.eval("gb.clearRect(0, 0, 60, 60); var p2 = new Path2D();"
            "p2.roundRect(5, 5, 40, 40, [15, 0, 0, 10]); gb.fill(p2);");
    // top-left rounded (empty), top-right square, bottom-right square,
    // bottom-left rounded at 10 (empty at 7,43).
    assert(js.eval("gb.getImageData(7, 7, 1, 1).data[3]").to_string() == "0");
    assert(js.eval("gb.getImageData(43, 7, 1, 1).data[3]").to_string() == "255");
    assert(js.eval("gb.getImageData(43, 43, 1, 1).data[3]").to_string() == "255");
    assert(js.eval("gb.getImageData(7, 43, 1, 1).data[3]").to_string() == "0");
    // The context's own version of the same, to prove the picture above is the
    // shape and not four lucky probes.
    js.eval("ga.clearRect(0, 0, 60, 60); ga.beginPath(); ga.roundRect(5, 5, 40, 40, [15, 0, 0, 10]); ga.fill();");
    assert(js.eval("ink(ga) === ink(gb)").to_string() == "true");

    // --- Part 3: the spreading rule for a sequence of 1, 2 and 3 values. Two values
    // are (top-left, bottom-right) and (top-right, bottom-left): `[15, 0]` rounds the
    // top-left and the bottom-right and leaves the other two square.
    js.eval("gb.clearRect(0, 0, 60, 60); var p3 = new Path2D();"
            "p3.roundRect(5, 5, 40, 40, [15, 0]); gb.fill(p3);");
    assert(js.eval("gb.getImageData(7, 7, 1, 1).data[3]").to_string() == "0");
    assert(js.eval("gb.getImageData(43, 43, 1, 1).data[3]").to_string() == "0");
    assert(js.eval("gb.getImageData(43, 7, 1, 1).data[3]").to_string() == "255");
    assert(js.eval("gb.getImageData(7, 43, 1, 1).data[3]").to_string() == "255");
    assert(js.eval("same([[15, 0]]).split(':')[2]").to_string() == "true");
    // Three values are (tl, tr, bl) with the bottom-right taking the top-right's.
    assert(js.eval("same([[12, 0, 12]]).split(':')[2]").to_string() == "true");
    assert(js.eval("same([[1, 2, 3]]).split(':')[2]").to_string() == "true");
    // One value, and the bare-number form, are the same shape as each other and as
    // the context's: this is the regression guard for the one-radius kind, which
    // must keep replaying through the overload it always used.
    assert(js.eval("same([[10]]).split(':')[2]").to_string() == "true");
    assert(js.eval("same([10]).split(':')[2]").to_string() == "true");
    assert(js.eval("same([[10]]).split(':')[1]").to_string() ==
               js.eval("same([10]).split(':')[1]").to_string());
    // No radii at all is a plain rectangle with square corners.
    assert(js.eval("same([]).split(':')[2]").to_string() == "true");
    js.eval("gb.clearRect(0, 0, 60, 60); var p4 = new Path2D(); p4.roundRect(5, 5, 40, 40); gb.fill(p4);");
    assert(js.eval("gb.getImageData(6, 6, 1, 1).data[3]").to_string() == "255");

    // --- Part 4: Phase 466 closes this suite's former empty-sequence fallback.
    // Omitting radii above is a square rectangle; explicitly supplying `[]` is a
    // list with an invalid cardinality and throws before either path is changed.
    assert(js.eval("(function(){try{ga.roundRect(5,5,40,40,[]);return 'none';}"
                   "catch(e){return e.name;}})()").to_string() == "RangeError");
    assert(js.eval("(function(){try{var p5=new Path2D();p5.roundRect(5,5,40,40,[]);return 'none';}"
                   "catch(e){return e.name;}})()").to_string() == "RangeError");

    // --- Part 5: **overlapping radii are scaled down**, not clipped. A 20×20 box
    // with 15-pixel corners has 30 pixels of radius across 20 of width; the rule is
    // a proportional shrink of all four. The two forms must agree, and the box must
    // still be filled through the middle — a version that clamped each radius
    // separately or dropped the shape leaves a hole or nothing.
    assert(js.eval("(function(){"
                   "  ga.clearRect(0, 0, 60, 60); gb.clearRect(0, 0, 60, 60);"
                   "  ga.beginPath(); ga.roundRect(5, 5, 20, 20, [15, 15, 15, 15]); ga.fill();"
                   "  var q = new Path2D(); q.roundRect(5, 5, 20, 20, [15, 15, 15, 15]); gb.fill(q);"
                   "  return ink(ga) === ink(gb) && ink(gb) > 0 && gb.getImageData(15, 15, 1, 1).data[3] === 255;"
                   "})()").to_string() == "true");
    // A mixed, non-square case with radii that must shrink unevenly.
    assert(js.eval("same([[12, 3, 7, 1]]).split(':')[2]").to_string() == "true");

    // --- Part 6: a corner given as a **pair** `[x, y]`. Phase 454 made the
    // shared geometry elliptical; this suite's relational claim remains that the
    // context and `Path2D` answer the same pixels for that accepted shape.
    assert(js.eval("same([[[8, 4], [0, 0], [0, 0], [0, 0]]]).split(':')[2]").to_string() == "true");
    assert(js.eval("same([[[6, 3]]]).split(':')[2]").to_string() == "true");

    // --- Part 7: and the reason any of this is worth a suite — the phase before it
    // shipped one numeric slot. Reading all four radii back through the picture in
    // the exact form that was wrong, as one assertion, so a regression names itself.
    assert(js.eval("(function(){"
                   "  var c1 = document.createElement('canvas'); c1.width = 60; c1.height = 60;"
                   "  var k1 = c1.getContext('2d'); k1.fillStyle = '#000000';"
                   "  var c2 = document.createElement('canvas'); c2.width = 60; c2.height = 60;"
                   "  var k2 = c2.getContext('2d'); k2.fillStyle = '#000000';"
                   "  k1.beginPath(); k1.roundRect(5, 5, 40, 40, [15, 0, 0, 0]); k1.fill();"
                   "  var z = new Path2D(); z.roundRect(5, 5, 40, 40, [15, 0, 0, 0]); k2.fill(z);"
                   "  var d1 = k1.getImageData(0, 0, 60, 60).data, d2 = k2.getImageData(0, 0, 60, 60).data;"
                   "  for (var i = 0; i < d1.length; i++) if (d1[i] !== d2[i]) return 'differ at ' + i;"
                   "  return 'identical';"
                   "})()").to_string() == "identical");
}
