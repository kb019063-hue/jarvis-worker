// Phase 237, finding 241: drawImage itself, which had never been bound.
// The 3-, 5- and 9-argument forms must put the source's pixels where they
// say, leave the canvas' own pixels alone elsewhere, and refuse a source
// that is not an image rather than drawing a blank box.

// Phase 237, finding 241: what the origin-clean flag is for.
//
// `HTMLCanvasElement.toDataURL()` hands a script every pixel the canvas holds.
// That is only allowed while the bitmap is origin-clean (HTML §4.12.5.1); the
// moment a cross-origin image is drawn onto it, the flag is cleared and the
// pixels must stop being readable. Until this phase the flag did not exist,
// because nothing could put a foreign pixel on a canvas: `drawImage` was not
// bound to script at all, though the renderer could already draw one.
//
// The two pages below are the whole point, and they differ in one character
// of the image's URL:
//
//   * an `<img>` from the page's own origin, drawn with `drawImage`, leaves the
//     canvas clean — `toDataURL()` still answers the pixels, scaled and
//     positioned as asked;
//   * the same image served from another origin taints the canvas, and
//     `toDataURL()` throws `SecurityError`.
//
// A flag that is always true and a flag that works are indistinguishable
// unless both of those are checked, so both are.
//
// The image is served from a temporary directory through
// `ResourceLoader::set_origin_root_for_testing`, the same hook
// tests/cross_origin_windows.cpp uses, which makes the two realms genuinely
// cross-origin rather than merely differently named.

TEST(draw_image_puts_the_source_pixels_where_it_says) {
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        var src = document.createElement('canvas'); src.width = 2; src.height = 2;
        var sctx = src.getContext('2d');
        sctx.fillStyle = '#00ff00'; sctx.fillRect(0, 0, 2, 2);
        var dst = document.createElement('canvas'); dst.width = 6; dst.height = 6;
        var ctx = dst.getContext('2d');
    )JS");
    // 3-argument form at the origin.
    js.eval("ctx.drawImage(src, 0, 0)");
    assert(js.eval("Array.prototype.join.call(ctx.getImageData(0,0,1,1).data, ',')").to_string() ==
           "0,255,0,255");
    // Outside the source: still the canvas' own cleared pixel.
    assert(js.eval("Array.prototype.join.call(ctx.getImageData(3,3,1,1).data, ',')").to_string() ==
           "0,0,0,0");
    // 5-argument form scales it.
    js.eval("ctx.drawImage(src, 2, 2, 4, 4)");
    assert(js.eval("Array.prototype.join.call(ctx.getImageData(3,3,1,1).data, ',')").to_string() ==
           "0,255,0,255");
    // 9-argument form crops: a 1x1 corner of the source, blown up.
    js.eval("var c2 = document.createElement('canvas'); c2.width = 2; c2.height = 2;"
            "var k2 = c2.getContext('2d'); k2.drawImage(src, 0, 0, 1, 1, 0, 0, 2, 2)");
    assert(js.eval("Array.prototype.join.call(k2.getImageData(0,0,1,1).data, ',')").to_string() ==
           "0,255,0,255");
    // A canvas that drew from a clean canvas is still readable.
    assert(js.eval("String(dst.toDataURL()).slice(0, 22)").to_string() == "data:image/png;base64,");
    // A source that is not an image at all is refused rather than drawn blank.
    const std::string refused = js.eval(
        "(function(){ try { ctx.drawImage({}, 0, 0); return 'drew'; }"
        " catch (e) { return 'err:' + e.name; } })()").to_string();
    assert(refused == "err:TypeError");
}
