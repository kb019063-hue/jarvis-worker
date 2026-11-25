// Phase 406, finding 406: `ctx.filter`.
//
// §4.12.5.1 gives `CanvasRenderingContext2D` a `filter` attribute and ten filter
// functions — `blur()`, `brightness()`, `contrast()`, `drop-shadow()`,
// `grayscale()`, `hue-rotate()`, `invert()`, `opacity()`, `saturate()`,
// `sepia()`. Before this phase `typeof ctx.filter` was `"undefined"`, so none of
// them could be asked for, and this engine's only painterly effect was the box
// shadow from Phase 391.
//
// The suite is built around the four things that are easy to get wrong in a way
// that still looks right on screen:
//
//   * a filter applies to the drawing **as a whole**, so it cannot be a per-pixel
//     tint applied where the pixel is written. `drop-shadow` must paint where the
//     shape did *not* draw — the coverage gate that is correct for a shadow from
//     Phase 391 would silently drop every one of those pixels;
//   * `blur()` takes a **length**, so `blur(2px)` parses and `blur(2)` does not —
//     CSS requires a unit for a non-zero length, and a parser that accepts the
//     bare number accepts a value a browser would reject;
//   * an invalid value is **ignored**, leaving the previous filter in place
//     rather than turning filtering off. A page that types `invert(1)` and then
//     makes a mistake must not have its whole canvas change;
//   * the result still composites **over** what is already on the canvas. A blur
//     that erased the backdrop inside its softened edge would look plausible in a
//     test that only asked about the shape, and be badly wrong.

namespace {

/// A fresh canvas of the given size, and its context as `ck`.
void fresh_filter_canvas(JsInterpreter& js, const char* tag, int w, int h) {
    js.eval(std::string("var ") + tag + " = document.createElement('canvas'); " + tag +
            ".width = " + std::to_string(w) + "; " + tag + ".height = " + std::to_string(h) + ";");
    js.eval(std::string("var ck = ") + tag + ".getContext('2d');");
}

} // namespace

TEST(canvas_filter_applies_to_the_whole_drawing_and_ignores_what_it_cannot_parse) {
    TestPage page;
    JsInterpreter& js = page.js;

    // --- Part 1: the attribute exists and starts at `"none"`, which is what
    // makes every canvas drawn before this phase render exactly as it did.
    fresh_filter_canvas(js, "c", 24, 24);
    assert(js.eval("typeof ck.filter").to_string() == "string");
    assert(js.eval("ck.filter").to_string() == "none");
    // The getter answers the string as written, as a browser does.
    js.eval("ck.filter = 'grayscale(1)'");
    assert(js.eval("ck.filter").to_string() == "grayscale(1)");

    // --- Part 2: `grayscale` maps each channel to the luma of the input. Red is
    // 0.2126 * 255 = 54.2, and asserting the number and not "it changed" is what
    // catches a matrix with the wrong coefficients: green would be 182 and blue
    // 18, so a transposed one passes a looser test.
    js.eval("var gp = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    js.eval("ck.filter = 'grayscale(1)'; ck.fillStyle = '#ff0000'; ck.fillRect(0, 0, 4, 4);");
    assert(js.eval("gp(1,1)").to_string() == "54,54,54,255");
    // A half-strength grayscale is halfway between the colour and its luma:
    // red becomes 255 - (255-54)/2 = 155 and green/blue become (54)/2 = 27. The
    // channels are **not** equal here — only `amount = 1` collapses them — and an
    // assertion that they were equal was this suite's first wrong test, written as
    // `154,154,154` and wrong on a correct engine.
    fresh_filter_canvas(js, "half", 24, 24);
    js.eval("ck.filter = 'grayscale(0.5)'; ck.fillStyle = '#ff0000'; ck.fillRect(0, 0, 4, 4);"
            "var hp = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    assert(js.eval("hp(1,1)").to_string() == "155,27,27,255");
    // With no filter the fill is its own colour, which is the control: without
    // it, an engine that grayscaled everything would pass the assertions above.
    fresh_filter_canvas(js, "plain", 24, 24);
    js.eval("ck.fillStyle = '#ff0000'; ck.fillRect(0, 0, 4, 4);"
            "var pp = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    assert(js.eval("pp(1,1)").to_string() == "255,0,0,255");

    // --- Part 3: `invert(1)` complements each channel, and `opacity` scales the
    // alpha *without* touching the colour — a drop-in-the-wrong-place alpha
    // application would show up as a darkened green rather than a faint one.
    fresh_filter_canvas(js, "inv", 24, 24);
    js.eval("ck.filter = 'invert(1)'; ck.fillStyle = '#00ff00'; ck.fillRect(0, 0, 4, 4);"
            "var ip = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    assert(js.eval("ip(1,1)").to_string() == "255,0,255,255");
    fresh_filter_canvas(js, "op", 24, 24);
    js.eval("ck.filter = 'opacity(0.5)'; ck.fillStyle = '#000000'; ck.fillRect(0, 0, 4, 4);"
            "var op = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    assert(js.eval("op(1,1)").to_string() == "0,0,0,128");

    // --- Part 4: `saturate` and `hue-rotate` are matrix filters, not switches.
    // `saturate(0)` is the luma, the same 54 as `grayscale(1)`, which is the
    // cross-check that the two matrices agree at their endpoints.
    fresh_filter_canvas(js, "sat", 24, 24);
    js.eval("ck.filter = 'saturate(0)'; ck.fillStyle = '#ff0000'; ck.fillRect(0, 0, 4, 4);"
            "var sap = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    assert(js.eval("sap(1,1)").to_string() == "54,54,54,255");
    // `saturate(1)` is the identity to the byte.
    fresh_filter_canvas(js, "sat1", 24, 24);
    js.eval("ck.filter = 'saturate(1)'; ck.fillStyle = '#ff0000'; ck.fillRect(0, 0, 4, 4);"
            "var s1p = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    assert(js.eval("s1p(1,1)").to_string() == "255,0,0,255");
    // `hue-rotate(120deg)` turns pure red into pure green — the angle is the
    // whole point, so a filter registered but never rotating passes nothing.
    fresh_filter_canvas(js, "hue", 24, 24);
    js.eval("ck.filter = 'hue-rotate(120deg)'; ck.fillStyle = '#ff0000'; ck.fillRect(0, 0, 4, 4);"
            "var hup = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    assert(js.eval("hup(1,1)").to_string() == "0,113,0,255");
    // And `hue-rotate(0deg)` is the identity, so the rotation is not baked in.
    fresh_filter_canvas(js, "hue0", 24, 24);
    js.eval("ck.filter = 'hue-rotate(0deg)'; ck.fillStyle = '#ff0000'; ck.fillRect(0, 0, 4, 4);"
            "var h0 = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    assert(js.eval("h0(1,1)").to_string() == "255,0,0,255");

    // --- Part 5: a blur softens the edge outward. The box ends at x = 11, so a
    // hard edge leaves x = 12 empty while `blur(2px)` leaves it partly covered and
    // the coverage falls away from there. This is the assertion that fails when
    // the replay keeps Phase 391's coverage gate: the softened pixels lie outside
    // the original coverage and would be dropped one by one.
    fresh_filter_canvas(js, "bl", 24, 24);
    js.eval("ck.filter = 'blur(2px)'; ck.fillStyle = '#000000'; ck.fillRect(2, 4, 10, 8);"
            "var ba = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i+3]; };");
    // The middle of a box this wide is still fully covered, so the blur softens
    // the edge without eating the shape. A 4-wide box does **not** reach 255 in
    // its middle — measured at 204 — for the same reason Phase 391's shadow does
    // not: the kernel reaches outside the shape. That is the rasteriser's
    // behaviour, not a filter bug, so the assertion uses a box where it is 255.
    assert(js.eval("ba(6,8)").to_string() == "255");
    assert(js.eval("Number(ba(12,8)) > 0").to_string() == "true");
    assert(js.eval("Number(ba(12,8)) > Number(ba(13,8))").to_string() == "true");
    assert(js.eval("Number(ba(13,8)) > Number(ba(14,8))").to_string() == "true");
    // Far enough out, the blur has run out rather than smearing the whole canvas.
    assert(js.eval("ba(20,8)").to_string() == "0");
    // And a hard edge, for the control: no filter, no ramp at all.
    fresh_filter_canvas(js, "hard", 24, 24);
    js.eval("ck.fillStyle = '#000000'; ck.fillRect(2, 4, 10, 8);"
            "var hp2 = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i+3]; };");
    assert(js.eval("hp2(11,8)").to_string() == "255");
    assert(js.eval("hp2(12,8)").to_string() == "0");

    // --- Part 6: `drop-shadow` paints the silhouette where the shape did not
    // draw. A red box at (2,2)–(5,5) with a shadow offset 4 right puts black at
    // (8,3) and leaves the box red.
    fresh_filter_canvas(js, "ds", 24, 24);
    js.eval("ck.filter = 'drop-shadow(4px 0px 0px #000000)';"
            "ck.fillStyle = '#ff0000'; ck.fillRect(2, 2, 4, 4);"
            "var dp = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    assert(js.eval("dp(3,3)").to_string() == "255,0,0,255");
    assert(js.eval("dp(8,3)").to_string() == "0,0,0,255");
    assert(js.eval("dp(13,3)").to_string() == "0,0,0,0");

    // --- Part 7: and it composites **over the backdrop**. A green canvas with the
    // same shadow must still be green under the shadow's alpha, not black and not
    // transparent: writing the filtered buffer with full replacement instead of
    // compositing is the failure this part exists for.
    fresh_filter_canvas(js, "bk", 24, 24);
    js.eval("ck.fillStyle = '#00ff00'; ck.fillRect(0, 0, 24, 24);"
            "ck.filter = 'drop-shadow(4px 0px 0px #000000)';"
            "ck.fillStyle = '#ff0000'; ck.fillRect(2, 2, 4, 4);"
            "var bp = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    assert(js.eval("bp(3,3)").to_string() == "255,0,0,255");
    assert(js.eval("bp(8,3)").to_string() == "0,0,0,255");
    // A blurred edge is partly covered, so it must be *between* the two colours
    // rather than a hard switch to either one.
    assert(js.eval("(function(){ var d = ck.getImageData(12, 0, 1, 1).data;"
                   "return d[1] === 255 && d[3] === 255; })()").to_string() == "true");

    // --- Part 8: the list is a list. Two functions apply in order, and an
    // invalid value anywhere in it rejects the whole value.
    fresh_filter_canvas(js, "ml", 24, 24);
    js.eval("ck.filter = 'grayscale(1) opacity(0.5)'; ck.fillStyle = '#ff0000'; ck.fillRect(0, 0, 4, 4);"
            "var mp = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    // Grayscale first, then half the alpha: 54 in every channel, alpha 128.
    assert(js.eval("mp(1,1)").to_string() == "54,54,54,128");
    // An unknown function, a bad number, a negative blur, an unbalanced
    // parenthesis and an unterminated list are all ignored — the previous value
    // stays. Each is checked on its own so a parser that happens to reject one of
    // them cannot carry the others.
    js.eval("ck.filter = 'invert(1)'");
    for (const char* bad : {"'nonsense'", "'brightness(abc)'", "'blur(-2px)'", "'invert(1'",
                            "'url(#f)'", "'sepia(1) bogus'"}) {
        js.eval(std::string("ck.filter = ") + bad);
        assert(js.eval("ck.filter").to_string() == "invert(1)");
    }
    // A length needs a unit: `blur(2px)` is the CSS length and `blur(2)` is not,
    // even though a bare number is the natural thing to type. `blur()` with no
    // argument is `blur(0)`, which is valid and is the identity.
    js.eval("ck.filter = 'blur(2)'");
    assert(js.eval("ck.filter").to_string() == "invert(1)");
    js.eval("ck.filter = 'blur()'");
    assert(js.eval("ck.filter").to_string() == "blur()");
    // `"none"` is valid and clears the filter, which an early version did not:
    // it treated an empty list as an invalid value and kept the old one.
    js.eval("ck.filter = 'none'");
    assert(js.eval("ck.filter").to_string() == "none");

    // --- Part 9: `save`/`restore` carry the filter, and a filter near a shadow
    // does not disturb it. The two both capture, so a filter that suppressed the
    // shadow or a shadow that filtered the shape would show here.
    fresh_filter_canvas(js, "sv", 24, 24);
    js.eval("ck.filter = 'invert(1)'; ck.save(); ck.filter = 'sepia(1)';"
            "ck.filter = 'grayscale(1)'; ck.restore();");
    assert(js.eval("ck.filter").to_string() == "invert(1)");
    fresh_filter_canvas(js, "both", 24, 24);
    js.eval("ck.filter = 'grayscale(1)'; ck.shadowColor = '#0000ff';"
            "ck.shadowOffsetX = 4; ck.fillStyle = '#ff0000'; ck.fillRect(2, 2, 4, 4);"
            "var bpx = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    // The shape is grayscaled and the shadow keeps its own colour: a filter
    // applies to the drawing, not to the shadow it casts.
    assert(js.eval("bpx(3,3)").to_string() == "54,54,54,255");
    assert(js.eval("bpx(8,3)").to_string() == "0,0,255,255");

    // --- Part 10: the fill rasteriser's opaque fast path writes the bitmap
    // directly and never calls `set_pixel_safe`, which has hidden a feature three
    // times in this engine (Phases 249, 370, 391). A 200×200 opaque fill is
    // exactly what takes that path, so it is the one that proves a filter forces
    // it off.
    fresh_filter_canvas(js, "fast", 200, 200);
    js.eval("ck.filter = 'invert(1)'; ck.fillStyle = '#ff0000'; ck.fillRect(0, 0, 200, 200);"
            "var fp = function(x, y) { var d = ck.getImageData(0, 0, 200, 200).data;"
            "var i = (y*200+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    assert(js.eval("fp(100,100)").to_string() == "0,255,255,255");

    // --- Part 11: a filter applies to strokes and text as well as fills, because
    // it is a property of the context and not of `fillRect`.
    fresh_filter_canvas(js, "st", 24, 24);
    js.eval("ck.filter = 'invert(1)'; ck.strokeStyle = '#00ff00'; ck.lineWidth = 2;"
            "ck.strokeRect(2, 2, 10, 10);"
            "var sp = function(x, y) { var d = ck.getImageData(0, 0, 24, 24).data;"
            "var i = (y*24+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; };");
    assert(js.eval("sp(2,7)").to_string() == "255,0,255,255");
}
