// Phase 370, finding 370: a `CanvasPattern` is a paint.
//
// Phase 280 (#346) added `createPattern()` and named this as the half it did not
// do: the method returned the right object and the right `null`s, and assigning
// it to `fillStyle` painted nothing, because a style was only ever a colour or a
// `CanvasGradient`. `fillRect` on a 16×16 canvas with a red/green tile came out
// `0,0,0` — measured, not guessed.
//
// The paint is now the third kind, and this suite asserts **pixels**, not that
// the property was stored. Storing it was already true before the phase: the
// object went in and `ctx.fillStyle === pattern` held, while nothing was drawn.
// That is the shape of test this phase had to avoid.
//
// The tile used throughout is 4×4, red on its left two columns and green on its
// right two, so a pixel's colour says which column of the tile it came from:
// x=1 is red, x=3 is green, and a wrapped x=5 is red again while a clamped one
// stays green. That is what tells `repeat` from `repeat-x` from `no-repeat`.

namespace {

/// The colour at (x, y) of the canvas `context`, as "r,g,b".
std::string px(JsInterpreter& js, int x, int y, int size = 16) {
    return js.eval("(function(){ var d = g.getImageData(0,0," + std::to_string(size) + "," +
                   std::to_string(size) + ").data; var i = (" + std::to_string(y) + "*" +
                   std::to_string(size) + "+" + std::to_string(x) + ")*4;"
                   "return d[i] + ',' + d[i+1] + ',' + d[i+2]; })()")
        .to_string();
}

/// Fills the whole canvas black, then the whole canvas with the current
/// `fillStyle`, so only what the style paints is left.
std::string fill_with_style(JsInterpreter& js, const char* style_setup) {
    js.eval("g.fillStyle = '#000000'; g.fillRect(0, 0, 16, 16);");
    js.eval(style_setup);
    js.eval("g.fillRect(0, 0, 16, 16);");
    return js.eval("1").to_string();
}

} // namespace

TEST(a_canvas_pattern_paints_and_its_repetition_rule_places_the_tile) {
    TestPage page;
    JsInterpreter& js = page.js;

    js.eval("var c = document.createElement('canvas'); c.width = 16; c.height = 16;"
            "var g = c.getContext('2d');"
            // The tile: left half red, right half green, full height.
            "var t = document.createElement('canvas'); t.width = 4; t.height = 4;"
            "var tg = t.getContext('2d');"
            "tg.fillStyle = '#ff0000'; tg.fillRect(0, 0, 2, 4);"
            "tg.fillStyle = '#00ff00'; tg.fillRect(2, 0, 2, 4);");

    // --- Part 1: the paint happens at all. This is the assertion that failed
    // before the phase, and it is a pixel, not a property.
    js.eval("var p = g.createPattern(t, 'repeat');");
    fill_with_style(js, "");
    js.eval("g.fillStyle = p; g.fillRect(0, 0, 16, 16);");
    assert(px(js, 1, 0) == "255,0,0");     // the tile's left column
    assert(px(js, 3, 0) == "0,255,0");     // the tile's right column
    assert(px(js, 5, 0) == "255,0,0");     // the tile again, one width across
    assert(px(js, 7, 0) == "0,255,0");
    // Vertically too: the tile is 4 tall, so row 4 is row 0 again.
    assert(px(js, 1, 4) == "255,0,0");
    assert(px(js, 3, 4) == "0,255,0");

    // --- Part 2: each repetition rule, on the same tile. `x=9` is two widths
    // and a bit across, so it is past the tile's end on the x axis, which is
    // where the four rules first disagree.
    js.eval("function colAt(rep, x, y) {"
            "  g.fillStyle = '#000000'; g.fillRect(0, 0, 16, 16);"
            "  g.fillStyle = g.createPattern(t, rep); g.fillRect(0, 0, 16, 16);"
            "  var d = g.getImageData(0, 0, 16, 16).data; var i = (y*16+x)*4;"
            "  return d[i] + ',' + d[i+1] + ',' + d[i+2];"
            "}");
    // `repeat`: wraps on both axes. x=9 is the tile's column 1 (red).
    assert(js.eval("colAt('repeat', 9, 0)").to_string() == "255,0,0");
    assert(js.eval("colAt('repeat', 1, 4)").to_string() == "255,0,0");
    // `repeat-x`: wraps across, and clamps down. y=4 is the tile's last row,
    // which is red on the left and green on the right — the same either way,
    // so the assertion that tells them apart is the one below on a row that
    // would have changed: y=5 would be row 1 of a second tile, and here it is
    // row 3 of the only one, which is the same colour; so the x axis is the
    // one that must be checked, and it wraps.
    assert(js.eval("colAt('repeat-x', 9, 0)").to_string() == "255,0,0");
    assert(js.eval("colAt('repeat-x', 3, 0)").to_string() == "0,255,0");
    // `repeat-y`: wraps down and clamps across, so x=9 — past the end — takes
    // the edge column, green, and does **not** wrap back to red.
    assert(js.eval("colAt('repeat-y', 9, 0)").to_string() == "0,255,0");
    assert(js.eval("colAt('repeat-y', 1, 4)").to_string() == "255,0,0");
    // `no-repeat` repeats on neither axis: the edge column, both ways. This one
    // was wrong when the phase's pixels first worked — the condition was
    // written "not repeat-y" and made `no-repeat` wrap on both axes, which
    // nothing could see while the pattern painted nothing.
    assert(js.eval("colAt('no-repeat', 9, 0)").to_string() == "0,255,0");
    assert(js.eval("colAt('no-repeat', 3, 0)").to_string() == "0,255,0");
    assert(js.eval("colAt('no-repeat', 1, 0)").to_string() == "255,0,0");

    // --- Part 3: `setTransform` moves the tile. A translation of 2 puts the
    // tile's red half at x=2..3 instead of x=0..1, which a pattern that ignored
    // the matrix could not do.
    js.eval("var p2 = g.createPattern(t, 'repeat'); p2.setTransform(1, 0, 0, 1, 2, 0);");
    fill_with_style(js, "g.fillStyle = p2;");
    assert(px(js, 0, 0) == "0,255,0");     // the tile's green, pushed left of the origin
    assert(px(js, 1, 0) == "0,255,0");
    assert(px(js, 2, 0) == "255,0,0");     // the red half, now here
    assert(px(js, 3, 0) == "255,0,0");
    js.eval("p2.setTransform();");         // no argument clears it
    fill_with_style(js, "g.fillStyle = p2;");
    assert(px(js, 0, 0) == "255,0,0");     // back where it started

    // --- Part 4: the paint is a path fill and a stroke too, not only
    // `fillRect` — those go through `rasterize_paths` rather than the fast
    // rectangular path, and each had to be told about the pattern separately.
    js.eval("g.fillStyle = '#000000'; g.fillRect(0, 0, 16, 16);"
            "g.fillStyle = p; g.beginPath();"
            "g.moveTo(0,0); g.lineTo(16,0); g.lineTo(16,16); g.lineTo(0,16); g.closePath(); g.fill();");
    assert(px(js, 1, 8) == "255,0,0");
    assert(px(js, 3, 8) == "0,255,0");
    js.eval("g.fillStyle = '#000000'; g.fillRect(0, 0, 16, 16);"
            "g.strokeStyle = p; g.lineWidth = 4; g.beginPath();"
            "g.moveTo(0, 8); g.lineTo(16, 8); g.stroke();");
    assert(px(js, 1, 8) == "255,0,0");
    assert(px(js, 3, 8) == "0,255,0");

    // --- Part 5: a style is exactly one of the three. Setting a colour clears
    // the pattern, and the canvas then paints the colour.
    fill_with_style(js, "g.fillStyle = p; g.fillStyle = '#0000ff';");
    assert(px(js, 1, 0) == "0,0,255");

    // --- Part 6: reading a style back answers the object that was set, which
    // is what §4.12.5.1's getter says. A marker string or a colour here would
    // be a different answer, and a page comparing with `===` would see it.
    js.eval("g.fillStyle = p;");
    assert(js.eval("g.fillStyle === p").to_string() == "true");
    assert(js.eval("g.strokeStyle = p; g.strokeStyle === p").to_string() == "true");
    js.eval("g.fillStyle = '#123456';");
    assert(js.eval("g.fillStyle").to_string() == "#123456");

    // --- Part 7: `save()` and `restore()` carry the pattern. This found a
    // second defect, one that predates the phase: `to_js_value` copies
    // `fillStyle` onto the context object as an own data property, and an own
    // property shadows the live read, so `restore()` put the paint back in the
    // renderer while the object kept answering the colour set inside the
    // `save()`. A gradient set before a `save()` was wrong the same way.
    js.eval("g.fillStyle = p; g.save(); g.fillStyle = '#00ff00'; g.restore();");
    assert(js.eval("g.fillStyle === p").to_string() == "true");
    fill_with_style(js, "g.fillStyle = p;");
    assert(px(js, 1, 0) == "255,0,0");     // and it still paints
    js.eval("var gr = g.createLinearGradient(0, 0, 16, 0);"
            "g.fillStyle = gr; g.save(); g.fillStyle = '#00ff00'; g.restore();");
    assert(js.eval("g.fillStyle === gr").to_string() == "true");

    // --- Part 8: `reset()` leaves a colour, not a pattern, and the canvas
    // paints black afterwards.
    js.eval("g.fillStyle = p; g.reset();");
    assert(js.eval("g.fillStyle").to_string() == "#000000");
    assert(js.eval("g.fillStyle === p").to_string() == "false");
}
