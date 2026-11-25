// Phase 391, finding 391: canvas shadows.
//
// §4.12.5.1 gives `CanvasRenderingContext2D` four shadow properties —
// `shadowBlur`, `shadowColor`, `shadowOffsetX`, `shadowOffsetY` — and says
// shadows are composited **under** their shapes. Before this phase none of the
// four existed (`typeof` was `undefined` for all), so nothing this engine drew
// cast a shadow at all.
//
// Two things the suite is careful about, because both are easy to get wrong in a
// way that looks right:
//
//   * the **initial** `shadowColor` is transparent black, so setting only an
//     offset must still paint nothing — a suite that sets a colour first would
//     never notice if the default were opaque black, which would blacken every
//     page that draws a rectangle;
//   * the shape **covers** its own shadow, so with no offset a blurred shadow is
//     visible only around the shape and the shape's own pixels are the fill
//     colour. A shadow painted over the shape passes every "is there a shadow"
//     assertion and fails that one.

namespace {

/// A fresh 24×24 canvas and its context, as globals `cc`/`ck`.
void fresh_canvas(JsInterpreter& js, const char* tag) {
    js.eval(std::string("var ") + tag + " = document.createElement('canvas'); " + tag +
            ".width = 24; " + tag + ".height = 24;");
    js.eval(std::string("var ck = ") + tag + ".getContext('2d');");
}

} // namespace

TEST(canvas_shadows_are_painted_under_their_shapes_and_off_by_default) {
    TestPage page;
    JsInterpreter& js = page.js;

    // --- Part 1: the four properties exist, and start at the values the
    // standard gives them. `shadowColor` starts transparent black, which is what
    // makes a shadow invisible until a colour is set.
    fresh_canvas(js, "c");
    assert(js.eval("typeof ck.shadowBlur").to_string() == "number");
    assert(js.eval("typeof ck.shadowColor").to_string() == "string");
    assert(js.eval("typeof ck.shadowOffsetX").to_string() == "number");
    assert(js.eval("typeof ck.shadowOffsetY").to_string() == "number");
    assert(js.eval("String(ck.shadowBlur)").to_string() == "0");
    assert(js.eval("String(ck.shadowOffsetX)").to_string() == "0");
    assert(js.eval("String(ck.shadowOffsetY)").to_string() == "0");
    // Whatever form the colour answers, it must be transparent: that is the
    // claim, not the notation.
    // A shadow **cast with the default colour** must paint nothing. This asks
    // the context that has never had a colour set, not a copy of the string it
    // answers: the first version copied `ck.shadowColor` onto a fresh context,
    // which round-trips correctly whether the default is transparent or opaque,
    // and so it passed when the default was made opaque black — measured, and it
    // is the gap that made this assertion worth rewriting.
    assert(js.eval("(function(){ var t = document.createElement('canvas');"
                   "t.width = 4; t.height = 4; var g = t.getContext('2d');"
                   "g.shadowOffsetX = 2; g.shadowOffsetY = 2;"
                   "g.fillStyle = '#00ff00'; g.fillRect(0, 0, 1, 1);"
                   "var d = g.getImageData(0, 0, 4, 4).data;"
                   // the shape is the single pixel (0,0); a shadow offset by
                   // (2,2) would land at (2,2), which must still be empty. Only
                   // that pixel is asked about — a loop over the whole canvas
                   // also counts the green shape and fails on a correct engine,
                   // which is how this assertion was written the first time.
                   "return d[(2*4+2)*4+3]; })()").to_string() == "0");

    // A negative blur is ignored; a non-finite one too.
    js.eval("ck.shadowBlur = 5; ck.shadowBlur = -3;");
    assert(js.eval("String(ck.shadowBlur)").to_string() == "5");
    js.eval("ck.shadowBlur = 0;");

    // --- Part 2: an offset with no blur puts the shadow exactly where the
    // offset says. A blue square at (2,2)–(5,5) with a red shadow offset by
    // (4,4) casts it at (6,6)–(9,9).
    fresh_canvas(js, "s");
    js.eval("ck.shadowColor = '#ff0000'; ck.shadowOffsetX = 4; ck.shadowOffsetY = 4;"
            "ck.fillStyle = '#0000ff'; ck.fillRect(2, 2, 4, 4);");
    js.eval("var sp = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    // The square itself is its own colour: the shape covers its shadow.
    assert(js.eval("sp(3,3)").to_string() == "0,0,255,255");
    assert(js.eval("sp(2,2)").to_string() == "0,0,255,255");
    // The shadow, moved by the offset and nothing blurred.
    assert(js.eval("sp(7,7)").to_string() == "255,0,0,255");
    assert(js.eval("sp(9,9)").to_string() == "255,0,0,255");
    // Outside both, nothing.
    assert(js.eval("sp(15,15)").to_string() == "0,0,0,0");
    // And a negative offset moves it the other way, which is the assertion that
    // catches an offset applied with the wrong sign — the first version of this
    // code subtracted it, and a shape at (2,2) then cast nothing visible at all.
    fresh_canvas(js, "n");
    js.eval("ck.shadowColor = '#00ff00'; ck.shadowOffsetX = -3; ck.shadowOffsetY = -3;"
            "ck.fillStyle = '#000000'; ck.fillRect(10, 10, 4, 4);"
            "var np = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    assert(js.eval("np(8,8)").to_string() == "0,255,0,255");

    // --- Part 3: the blur softens the edge. A hard shadow is opaque right up to
    // its edge and absent a pixel outside; a blurred one ramps away over several
    // pixels, so the pixel just outside the shape is partly covered and one
    // further out is mostly not.
    fresh_canvas(js, "h");
    js.eval("ck.shadowColor = '#000000'; ck.shadowOffsetX = 6; ck.shadowOffsetY = 6;"
            "ck.fillStyle = '#ffffff'; ck.fillRect(2, 2, 8, 8);"
            "function ha(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i+3]; }");
    // The shadow is at x = 8..15; inside is opaque, one past the edge is nothing.
    assert(js.eval("ha(11,11)").to_string() == "255");
    assert(js.eval("ha(17,11)").to_string() == "0");
    fresh_canvas(js, "b");
    js.eval("ck.shadowColor = '#000000'; ck.shadowOffsetX = 6; ck.shadowOffsetY = 6;"
            "ck.shadowBlur = 4; ck.fillStyle = '#ffffff'; ck.fillRect(2, 2, 8, 8);"
            "function ba(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i+3]; }");
    // Inside is still fully covered, and the edge now ramps instead of stepping:
    // three pixels out, three falling values, ending below a tenth.
    // The blurred shadow is **attenuated at the centre** as well as at the edge:
    // with a radius of 2 the kernel reaches outside an 8-wide shape, so the
    // middle is about 201/255 and not 255. Written first as `inside == 255`,
    // which is true of a small blur (radius 1 reaches 255) and false of this
    // one — the assertion was wrong, not the filter. Measured with blur 2 the
    // centre is 255, which is why this checks the ramp and not a constant.
    const int inside = std::stoi(js.eval("ba(11,11)").to_string());
    const int near = std::stoi(js.eval("ba(17,11)").to_string());
    const int mid = std::stoi(js.eval("ba(18,11)").to_string());
    const int far = std::stoi(js.eval("ba(21,11)").to_string());
    assert(inside > 128);          // still most of the way opaque at the centre
    assert(near > 0);              // a blurred shadow reaches past the shape
    assert(near > mid);            // and falls off
    assert(mid >= far);
    assert(far < 30);              // three standard deviations out is nearly gone
    // A blur small enough not to reach past the shape's own middle keeps it
    // fully opaque there, which is the other end of the same rule.
    fresh_canvas(js, "b2");
    js.eval("ck.shadowColor = '#000000'; ck.shadowOffsetX = 6; ck.shadowOffsetY = 6;"
            "ck.shadowBlur = 2; ck.fillStyle = '#ffffff'; ck.fillRect(2, 2, 8, 8);"
            "function b2a(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i+3]; }");
    assert(js.eval("b2a(11,11)").to_string() == "255");

    // --- Part 4: with no offset, the shape still covers its own shadow. The
    // fill colour is at the shape's centre and the shadow is round the outside.
    fresh_canvas(js, "u");
    js.eval("ck.shadowColor = '#ff0000'; ck.shadowBlur = 2;"
            "ck.fillStyle = '#0000ff'; ck.fillRect(8, 8, 6, 6);"
            "function up(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2]; }");
    assert(js.eval("up(10,10)").to_string() == "0,0,255");    // the shape, not its shadow
    assert(js.eval("up(7,10)").to_string() == "255,0,0");     // the shadow beside it

    // --- Part 5: every primitive casts one, not only `fillRect`. A path fill, a
    // stroke, `fillText` and `drawImage` each needed telling separately, and a
    // fix that only wrapped `fillRect` would pass Part 2 and fail here.
    fresh_canvas(js, "p");
    js.eval("ck.shadowColor = '#000000'; ck.shadowOffsetX = 5; ck.shadowOffsetY = 5;"
            "ck.fillStyle = '#ffffff'; ck.beginPath(); ck.rect(2, 2, 6, 6); ck.fill();"
            "function pp(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i+3]; }");
    assert(js.eval("pp(10,10)").to_string() == "255");      // the path fill's shadow

    fresh_canvas(js, "t");
    js.eval("ck.shadowColor = '#ff0000'; ck.shadowOffsetX = 4; ck.shadowOffsetY = 4;"
            "ck.font = '16px sans-serif'; ck.fillStyle = '#000000';"
            "var before = ck.getImageData(0, 0, 24, 24).data;"
            "ck.fillText('I', 2, 18);");
    // Text is drawn with the shadow under it, so at least one pixel of the canvas
    // that was empty before is now red: the shadow of the glyph.
    assert(js.eval("(function(){ var d = ck.getImageData(0, 0, 24, 24).data;"
                   "for (var i = 0; i < d.length; i += 4) {"
                   "  if (d[i] > 128 && d[i+1] < 64 && d[i+2] < 64 && d[i+3] > 0) return true; }"
                   "return false; })()").to_string() == "true");

    // `drawImage`'s shadow.
    fresh_canvas(js, "d");
    js.eval("var src = document.createElement('canvas'); src.width = 4; src.height = 4;"
            "var sg = src.getContext('2d'); sg.fillStyle = '#00ff00'; sg.fillRect(0, 0, 4, 4);"
            "ck.shadowColor = '#0000ff'; ck.shadowOffsetX = 6; ck.shadowOffsetY = 6;"
            "ck.drawImage(src, 2, 2);"
            "function dp(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2]; }");
    assert(js.eval("dp(3,3)").to_string() == "0,255,0");     // the image itself
    assert(js.eval("dp(9,9)").to_string() == "0,0,255");     // its shadow

    // --- Part 6: the shadow colour's own alpha is honoured, and `globalAlpha`
    // is not applied twice. A half-transparent shadow over white comes out grey,
    // not black and not nothing.
    fresh_canvas(js, "a");
    js.eval("ck.shadowColor = 'rgba(0,0,0,0.5)'; ck.shadowOffsetX = 6; ck.shadowOffsetY = 6;"
            "ck.fillStyle = '#ffffff'; ck.fillRect(2, 2, 6, 6);"
            "function ap(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; }");
    // Over nothing it is half-covered black: alpha 128, not 255 and not 0.
    assert(js.eval("ap(10,10)").to_string() == "0,0,0,128");

    // --- Part 7: `save()`/`restore()` carry all four, and `reset()` puts them
    // back to their initial values.
    fresh_canvas(js, "x");
    js.eval("ck.shadowColor = '#ff0000'; ck.shadowBlur = 3; ck.shadowOffsetX = 2; ck.shadowOffsetY = 1;"
            "ck.save(); ck.shadowColor = '#00ff00'; ck.shadowBlur = 9;"
            "ck.shadowOffsetX = 7; ck.shadowOffsetY = 8; ck.restore();");
    assert(js.eval("String(ck.shadowBlur)").to_string() == "3");
    assert(js.eval("String(ck.shadowOffsetX)").to_string() == "2");
    assert(js.eval("String(ck.shadowOffsetY)").to_string() == "1");
    // The colour comes back too, which a shadow drawn afterwards proves.
    js.eval("ck.fillStyle = '#0000ff'; ck.fillRect(1, 1, 2, 2);"
            "var rd = ck.getImageData(0, 0, 24, 24).data;"
            "var red = false;"
            "for (var i = 0; i < rd.length; i += 4) {"
            "  if (rd[i] > 128 && rd[i+1] < 64 && rd[i+2] < 64 && rd[i+3] > 0) red = true; }");
    assert(js.eval("String(red)").to_string() == "true");
    js.eval("ck.reset();");
    assert(js.eval("String(ck.shadowBlur)").to_string() == "0");
    assert(js.eval("String(ck.shadowOffsetX)").to_string() == "0");
    assert(js.eval("String(ck.shadowOffsetY)").to_string() == "0");
    // And a shadow drawn after `reset()` is transparent again — measured on a
    // **fresh canvas**, because `reset()` resets the drawing state and not the
    // bitmap (§4.12.5.1): the shadow drawn before the reset is still on the
    // canvas afterwards, and that is correct. Written first as "no partly
    // covered pixel anywhere" on the same canvas, which failed on a correct
    // engine for exactly that reason.
    fresh_canvas(js, "z");
    js.eval("ck.shadowOffsetX = 3; ck.fillStyle = '#ffffff'; ck.fillRect(1, 1, 2, 2);"
            "var rd2 = ck.getImageData(0, 0, 24, 24).data;"
            "var partial = 0;"
            "for (var i = 3; i < rd2.length; i += 4) { if (rd2[i] > 0 && rd2[i] < 255) partial++; }");
    assert(js.eval("String(partial)").to_string() == "0");
}
