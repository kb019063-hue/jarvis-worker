// Phase 382, finding 382: image smoothing.
//
// §4.12.5.1 gives `CanvasRenderingContext2D` two properties that decide how an
// image is sampled when it is not drawn at its own size:
//
//   imageSmoothingEnabled — boolean, true at the start
//   imageSmoothingQuality — "low" | "medium" | "high", "low" at the start
//
// Neither existed on this engine's context (`typeof` was `undefined` for both),
// and every sample was nearest neighbour: `drawImage` scaling a 2×2 image to
// 8×8 came out in four solid blocks, and a pattern magnified by `ctx.scale(4,4)`
// repeated each tile pixel four times. With both properties in place, the same
// draw is interpolated, and `imageSmoothingEnabled = false` gives the blocks
// back — so the suite asserts **both settings**, because a filter that ignores
// the switch passes half of it and a switch that does nothing passes the other.
//
// The 1:1 case is the one that keeps a filter honest: at its own size an image
// must come out exactly, either way. A filter that always blurs would pass every
// scaled assertion here and fail that one.

namespace {

/// Draws a 2×2 four-colour image (red, green / blue, white) into a fresh 8×8
/// canvas with the given smoothing setting, and returns row 0 as "r,g,b" triples.
std::string scaled_row(JsInterpreter& js, bool smooth) {
    return js.eval(std::string("(function(){"
                   "var cc = document.createElement('canvas'); cc.width = 8; cc.height = 8;"
                   "var gg = cc.getContext('2d');"
                   "gg.imageSmoothingEnabled = ") + (smooth ? "true" : "false") + ";"
                   "gg.drawImage(src, 0, 0, 8, 8);"
                   "var d = gg.getImageData(0, 0, 8, 8).data;"
                   "var out = [];"
                   "for (var x = 0; x < 4; x++) { var i = x * 4; out.push(d[i] + ',' + d[i+1] + ',' + d[i+2]); }"
                   "return out.join(' | '); })()")
        .to_string();
}

} // namespace

TEST(image_smoothing_is_a_pair_of_live_properties_that_change_how_an_image_is_sampled) {
    TestPage page;
    JsInterpreter& js = page.js;

    // --- Part 1: the properties exist and start at their specified values.
    js.eval("var c = document.createElement('canvas'); c.width = 32; c.height = 32;"
            "var g = c.getContext('2d');");
    assert(js.eval("typeof g.imageSmoothingEnabled").to_string() == "boolean");
    assert(js.eval("typeof g.imageSmoothingQuality").to_string() == "string");
    assert(js.eval("String(g.imageSmoothingEnabled)").to_string() == "true");
    assert(js.eval("g.imageSmoothingQuality").to_string() == "low");

    // The IDL type is boolean: any value is converted, not stored as given.
    js.eval("g.imageSmoothingEnabled = 0;");
    assert(js.eval("g.imageSmoothingEnabled").to_string() == "false");
    js.eval("g.imageSmoothingEnabled = '';");
    assert(js.eval("g.imageSmoothingEnabled").to_string() == "false");
    js.eval("g.imageSmoothingEnabled = 'x';");
    assert(js.eval("g.imageSmoothingEnabled").to_string() == "true");

    // The quality is an enumeration: a value that is not one of the three is
    // ignored and the old one stays, which is what "set to the given value" says
    // for an enumerated attribute.
    js.eval("g.imageSmoothingQuality = 'high';");
    assert(js.eval("g.imageSmoothingQuality").to_string() == "high");
    js.eval("g.imageSmoothingQuality = 'medium';");
    assert(js.eval("g.imageSmoothingQuality").to_string() == "medium");
    js.eval("g.imageSmoothingQuality = 'bogus';");
    assert(js.eval("g.imageSmoothingQuality").to_string() == "medium");
    js.eval("g.imageSmoothingQuality = '';");
    assert(js.eval("g.imageSmoothingQuality").to_string() == "medium");

    // --- Part 2: `save()`/`restore()` carry both, so a setting made inside a
    // save does not outlive it. `reset()` puts them at their initial values.
    js.eval("g.imageSmoothingEnabled = false; g.imageSmoothingQuality = 'high';"
            "g.save(); g.imageSmoothingEnabled = true; g.imageSmoothingQuality = 'low'; g.restore();");
    assert(js.eval("g.imageSmoothingEnabled").to_string() == "false");
    assert(js.eval("g.imageSmoothingQuality").to_string() == "high");
    js.eval("g.reset();");
    assert(js.eval("String(g.imageSmoothingEnabled)").to_string() == "true");
    assert(js.eval("g.imageSmoothingQuality").to_string() == "low");

    // --- Part 3: what a scaled draw looks like. A 2×2 image of four distinct
    // colours, drawn to 8×8.
    js.eval("var src = document.createElement('canvas'); src.width = 2; src.height = 2;"
            "var sg = src.getContext('2d');"
            "sg.fillStyle = '#ff0000'; sg.fillRect(0, 0, 1, 1);"
            "sg.fillStyle = '#00ff00'; sg.fillRect(1, 0, 1, 1);"
            "sg.fillStyle = '#0000ff'; sg.fillRect(0, 1, 1, 1);"
            "sg.fillStyle = '#ffffff'; sg.fillRect(1, 1, 1, 1);");

    // With smoothing **off** the row is two blocks of four: the source's two top
    // pixels are red and green, each covering half of an 8-wide destination, and
    // nothing between them is mixed. (Written first as two pixels each, which
    // was the assertion being wrong and not the engine.)
    const std::string nearest = scaled_row(js, false);
    assert(nearest == "255,0,0 | 255,0,0 | 255,0,0 | 255,0,0");
    // With smoothing **on** there are intermediate colours: the boundary between
    // the red and the green runs through the middle columns, so at least one of
    // the four sampled pixels is neither of the two. That is the whole claim —
    // an interpolated draw is not a blocked one.
    const std::string smooth = scaled_row(js, true);
    assert(smooth != nearest);
    assert(smooth.find("255,0,0") == 0);            // the left edge is still red
    assert(js.eval("(function(){"
                   "var cc = document.createElement('canvas'); cc.width = 8; cc.height = 8;"
                   "var gg = cc.getContext('2d'); gg.imageSmoothingEnabled = true;"
                   "gg.drawImage(src, 0, 0, 8, 8);"
                   "var d = gg.getImageData(0, 0, 8, 8).data;"
                   // count the pixels on row 0 that are a mix of red and green
                   "var mixed = 0;"
                   "for (var x = 0; x < 8; x++) { var i = x * 4;"
                   "  if (d[i] > 0 && d[i] < 255 && d[i+1] > 0 && d[i+1] < 255) mixed++; }"
                   "return mixed; })()").to_string() != "0");

    // --- Part 4: at 1:1 the image must be its own pixels **either way**. This is
    // what a filter that always blurs fails.
    for (const bool smooth_1to1 : {true, false}) {
        const std::string row = js.eval(
            std::string("(function(){"
            "var cc = document.createElement('canvas'); cc.width = 2; cc.height = 2;"
            "var gg = cc.getContext('2d');"
            "gg.imageSmoothingEnabled = ") + (smooth_1to1 ? "true" : "false") + ";"
            "gg.drawImage(src, 0, 0, 2, 2);"
            "var d = gg.getImageData(0, 0, 2, 2).data;"
            "return [d[0],d[1],d[2],d[4],d[5],d[6],d[8],d[9],d[10],d[12],d[13],d[14]].join(','); })()")
            .to_string();
        assert(row == "255,0,0,0,255,0,0,0,255,255,255,255");
    }

    // --- Part 5: a pattern follows the context's setting too, and its own 1:1
    // draw stays exact — the repetition rules Phase 370 asserts depend on that,
    // and they now run with smoothing on by default.
    js.eval("var t = document.createElement('canvas'); t.width = 4; t.height = 4;"
            "var tg = t.getContext('2d');"
            "tg.fillStyle = '#ff0000'; tg.fillRect(0, 0, 2, 4);"
            "tg.fillStyle = '#00ff00'; tg.fillRect(2, 0, 2, 4);"
            "var p = g.createPattern(t, 'repeat');"
            "function patRow(en, magnify) {"
            "  g.fillStyle = '#000000'; g.fillRect(0, 0, 32, 32);"
            "  g.imageSmoothingEnabled = en;"
            "  g.save(); if (magnify) g.scale(4, 4);"
            "  g.fillStyle = p; g.fillRect(0, 0, magnify ? 8 : 32, magnify ? 8 : 32);"
            "  g.restore();"
            "  var d = g.getImageData(0, 0, 32, 32).data;"
            "  var out = [];"
            "  for (var x = 0; x < 6; x++) { var i = x * 4; out.push(d[i] + ',' + d[i+1] + ',' + d[i+2]); }"
            "  return out.join(' | ');"
            "}");
    // At 1:1 there is no interpolation to do, so the tile is exact and turning
    // smoothing off changes nothing.
    const std::string pat_1to1_on = js.eval("patRow(true, false)").to_string();
    const std::string pat_1to1_off = js.eval("patRow(false, false)").to_string();
    assert(pat_1to1_on == pat_1to1_off);
    assert(pat_1to1_on.find("255,0,0 | 255,0,0 | 0,255,0") == 0);
    // Magnified, the two settings differ: smoothing interpolates the tile edge.
    assert(js.eval("patRow(true, true)").to_string() != js.eval("patRow(false, true)").to_string());
    js.eval("g.imageSmoothingEnabled = true;");
}
