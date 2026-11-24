// Phase 280, finding 285: four methods the 2D context did not have.
//
// HTML §4.12.5.1 gives `CanvasRenderingContext2D` `reset()`,
// `isPointInStroke()`, `createPattern()` and `createConicGradient()`. All four
// were `undefined` on the context object this engine builds, and each one fails
// in a way a page cannot see:
//
//   * `reset()` doing nothing leaves the previous drawing's state in place, so
//     the next fill is the old colour;
//   * `isPointInStroke()` being `undefined` throws a `TypeError` at the call
//     site, in a hit test that was not expecting to fail;
//   * `createConicGradient()` likewise throws, so a page's gradient code dies
//     at its first line;
//   * `createPattern()` throwing means a fill pattern cannot be asked for.
//
// The suite asserts each answer in **both** directions — the point that is on
// the stroke and the one that is not, the state before and after `reset()` —
// because a method that always answers the same thing passes a test that only
// asks once.
//
// `reset()` has a trap that a test of "does `reset` exist" would miss, and it is
// the reason the binding had to move: `CanvasRenderingContext2D::to_js_value`
// copies `fillStyle`, `strokeStyle`, `lineWidth` and `globalCompositeOperation`
// onto the context object as **own data properties**, and an own property
// shadows the live read in `JsValue::get_property`. A `reset()` that only put
// the renderer's fields back left the object answering `#ff0000` afterwards.
// The state assertions below are what caught that.

namespace {

/// The colour at (x, y) of the canvas, as "r,g,b,a".
std::string canvas_pixel(JsInterpreter& js, const char* context, int x, int y) {
    return js.eval(std::string("(function(){ var d = ") + context +
                   ".getImageData(" + std::to_string(x) + ", " + std::to_string(y) +
                   ", 1, 1).data; return d[0] + ',' + d[1] + ',' + d[2] + ',' + d[3]; })()")
        .to_string();
}

} // namespace

TEST(the_2d_context_resets_tests_points_on_a_stroke_and_makes_patterns_and_conic_gradients) {
    TestPage page;
    JsInterpreter& js = page.js;

    // --- All four are functions. This is the assertion that was failing before
    // the phase; it is not the only one, but it is the one that names them.
    js.eval("var c = document.createElement('canvas'); c.width = 20; c.height = 20;"
            "var g = c.getContext('2d');");
    for (const char* name : {"reset", "isPointInStroke", "createPattern", "createConicGradient"}) {
        assert(js.eval(std::string("typeof g.") + name).to_string() == "function");
    }

    // --- `isPointInStroke`. A square path from (4,4) to (12,12), stroked 2px
    // wide: the stroke region is the ring within 1px of the square's outline.
    js.eval("g.beginPath(); g.moveTo(4,4); g.lineTo(12,4); g.lineTo(12,12); g.lineTo(4,12);"
            "g.closePath(); g.lineWidth = 2;");
    assert(js.eval("g.isPointInStroke(8, 4)").to_string() == "true");    // on the top edge
    assert(js.eval("g.isPointInStroke(8, 12)").to_string() == "true");   // on the bottom edge
    assert(js.eval("g.isPointInStroke(4, 8)").to_string() == "true");    // on the left edge
    assert(js.eval("g.isPointInStroke(8, 5)").to_string() == "true");    // 1px in, inside the band
    assert(js.eval("g.isPointInStroke(8, 8)").to_string() == "false");   // in the middle, inside the path
    assert(js.eval("g.isPointInStroke(8, 1)").to_string() == "false");   // 3px above, outside
    assert(js.eval("g.isPointInStroke(1, 8)").to_string() == "false");   // 3px left, outside
    // A wider line covers more, which is the half-width rule and not a fixed
    // tolerance; and the same point answers `true` for the path and `false` for
    // the stroke only when the path is right — the two are asked together.
    js.eval("g.lineWidth = 8;");
    assert(js.eval("g.isPointInStroke(8, 2)").to_string() == "true");    // within 4px of the edge
    assert(js.eval("g.isPointInPath(8, 8)").to_string() == "true");      // the inside, unchanged
    assert(js.eval("g.isPointInPath(8, 2)").to_string() == "false");     // outside the path
    js.eval("g.lineWidth = 2;");
    // An open subpath has no closing segment, so the point on the imaginary
    // line from its last point back to its first is not on the stroke.
    js.eval("g.beginPath(); g.moveTo(4,16); g.lineTo(16,16); g.lineTo(16,4); g.lineWidth = 2;");
    assert(js.eval("g.isPointInStroke(16, 16)").to_string() == "true");
    assert(js.eval("g.isPointInStroke(10, 10)").to_string() == "false");  // the missing closing edge
    // Too few arguments is `false`, not a throw.
    assert(js.eval("g.isPointInStroke(4)").to_string() == "false");

    // --- `reset()`. Every state property back to its initial value, the path
    // emptied, the transform identity, and the stack discarded.
    js.eval("g.fillStyle = '#ff0000'; g.strokeStyle = '#00ff00'; g.lineWidth = 7;"
            "g.lineCap = 'round'; g.lineJoin = 'bevel'; g.miterLimit = 3;"
            "g.globalAlpha = 0.5; g.globalCompositeOperation = 'xor';"
            "g.translate(5, 5); g.scale(2, 2); g.beginPath(); g.moveTo(1,1); g.lineTo(9,9);"
            "g.save(); g.lineWidth = 11;");
    js.eval("g.reset();");
    assert(js.eval("g.fillStyle").to_string() == "#000000");
    assert(js.eval("g.strokeStyle").to_string() == "#000000");
    assert(js.eval("g.lineWidth").to_string() == "1");
    assert(js.eval("g.lineCap").to_string() == "butt");
    assert(js.eval("g.lineJoin").to_string() == "miter");
    assert(js.eval("g.miterLimit").to_string() == "10");
    assert(js.eval("g.globalAlpha").to_string() == "1");
    assert(js.eval("g.globalCompositeOperation").to_string() == "source-over");
    assert(js.eval("g.font").to_string() == "10px sans-serif");
    assert(js.eval("g.textAlign").to_string() == "start");
    assert(js.eval("g.getTransform().a + ',' + g.getTransform().e").to_string() == "1,0");
    // The path is gone: a point that was on it is on nothing now.
    assert(js.eval("g.isPointInStroke(5, 5)").to_string() == "false");
    assert(js.eval("g.isPointInPath(5, 5)").to_string() == "false");
    // The stack is discarded, not popped: a `restore()` after `reset()` must
    // not resurrect what was saved before it. This is the difference between
    // `reset()` and `restore()` and the reason both exist.
    js.eval("g.restore();");
    assert(js.eval("g.lineWidth").to_string() == "1");     // still reset, not 11

    // --- `createConicGradient`. A ramp whose 0 is at the positive x axis, so a
    // point to the right of the centre is the first stop and a point below the
    // centre (y grows down, so this is +90°) is a quarter of the way round.
    js.eval("var cg = g.createConicGradient(0, 10, 10);"
            "cg.addColorStop(0, '#ff0000'); cg.addColorStop(0.5, '#00ff00');"
            "cg.addColorStop(1, '#ff0000');"
            "g.fillStyle = cg; g.fillRect(0, 0, 20, 20);");
    assert(canvas_pixel(js, "g", 19, 10) == "255,0,0,255");     // t = 0
    const std::string south = canvas_pixel(js, "g", 10, 19);    // t = 0.25
    const std::string west = canvas_pixel(js, "g", 0, 10);      // t = 0.5
    // The ramp is red -> green -> red, so a quarter and a half of the way round
    // are the green-ish middles and not the red the first stop is.
    assert(south != "255,0,0,255");
    assert(west == "0,255,0,255");     // exactly the second stop
    // **Which way round the ramp runs** is the thing this catches and a
    // single-point test cannot: with stops at 0, 0.25 and 0.5 that each have a
    // distinct colour, a gradient computed counter-clockwise gives south and
    // north each other's colour while still answering red at east, so it must
    // be asserted on both sides of the centre.
    js.eval("g.fillStyle = '#000000'; g.fillRect(0,0,20,20);"
            "var cgq = g.createConicGradient(0, 10, 10);"
            "cgq.addColorStop(0, '#ff0000'); cgq.addColorStop(0.25, '#00ff00');"
            "cgq.addColorStop(0.5, '#0000ff'); cgq.addColorStop(0.75, '#ffffff');"
            "g.fillStyle = cgq; g.fillRect(0, 0, 20, 20);");
    assert(canvas_pixel(js, "g", 19, 10) == "255,0,0,255");     // east, t = 0
    assert(canvas_pixel(js, "g", 10, 19) == "0,255,0,255");     // south, t = 0.25 (y grows down)
    assert(canvas_pixel(js, "g", 0, 10) == "0,0,255,255");      // west, t = 0.5
    assert(canvas_pixel(js, "g", 10, 0) == "255,255,255,255");  // north, t = 0.75

    // The start angle moves the ramp: 90° puts the first stop at the bottom.
    js.eval("g.fillStyle = '#000000'; g.fillRect(0,0,20,20);"
            "var cg2 = g.createConicGradient(Math.PI / 2, 10, 10);"
            "cg2.addColorStop(0, '#ff0000'); cg2.addColorStop(1, '#0000ff');"
            "g.fillStyle = cg2; g.fillRect(0, 0, 20, 20);");
    assert(canvas_pixel(js, "g", 10, 19) == "255,0,0,255");     // now t = 0 here
    assert(canvas_pixel(js, "g", 19, 10) != "255,0,0,255");     // and not here
    // A gradient with no stops is not an error; it paints nothing.
    js.eval("var cg3 = g.createConicGradient(0, 5, 5); assert_ok = (typeof cg3.addColorStop === 'function');");
    assert(js.eval("assert_ok").to_string() == "true");
    // A non-finite angle is a TypeError, which is the one case the method
    // throws rather than returning a gradient.
    assert(js.eval("(function(){ try { g.createConicGradient(NaN, 0, 0); return 'no throw'; }"
                   " catch (e) { return e.name; } })()").to_string() == "TypeError");

    // --- `createPattern`. The tile is a 2x2 canvas of solid red, so every
    // painted pixel is the same colour and the test is about the nulls.
    js.eval("var tile = document.createElement('canvas'); tile.width = 2; tile.height = 2;"
            "var tg = tile.getContext('2d'); tg.fillStyle = '#ff0000'; tg.fillRect(0,0,2,2);");
    js.eval("var pat = g.createPattern(tile, 'repeat');");
    assert(js.eval("pat !== null && typeof pat === 'object'").to_string() == "true");
    assert(js.eval("typeof pat.setTransform").to_string() == "function");
    // Each of the three other repetitions is accepted...
    for (const char* rep : {"repeat-x", "repeat-y", "no-repeat"}) {
        assert(js.eval(std::string("g.createPattern(tile, '") + rep + "') !== null").to_string() ==
               "true");
    }
    // ...and anything else is `null`, which is step 2's answer and not a throw.
    for (const char* rep : {"bogus", "REPEAT", "", "repeat "}) {
        assert(js.eval(std::string("g.createPattern(tile, '") + rep + "') === null").to_string() ==
               "true");
    }
    // A source that is not an image is `null` too.
    assert(js.eval("g.createPattern({}, 'repeat') === null").to_string() == "true");
    assert(js.eval("g.createPattern(null, 'repeat') === null").to_string() == "true");
    // `setTransform` accepts nothing (clearing it), or six numbers.
    js.eval("pat.setTransform(1, 0, 0, 1, 5, 5); pat.setTransform();"
            "pat.setTransform(null); pat.setTransform();");
    assert(js.eval("typeof pat.setTransform").to_string() == "function");
}
