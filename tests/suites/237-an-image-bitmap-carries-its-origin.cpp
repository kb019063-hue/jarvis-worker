// Phase 237, finding 241: an ImageBitmap carries the origin of what made it.
// A tainted canvas' bitmap is tainted too, so the taint cannot be laundered
// by copying the pixels through one. This also pins the copy itself: a
// bitmap of a canvas used to be built of zeroed pixels, not of the canvas'.

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

TEST(an_image_bitmap_carries_the_origin_of_what_made_it) {
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        var src = document.createElement('canvas'); src.width = 2; src.height = 2;
        var sctx = src.getContext('2d');
        sctx.fillStyle = '#0000ff'; sctx.fillRect(0, 0, 2, 2);
        var dst = document.createElement('canvas'); dst.width = 2; dst.height = 2;
        var ctx = dst.getContext('2d');
    )JS");
    // createImageBitmap is asynchronous; run the loop until its promise lands.
    js.eval("var bitmap = null; createImageBitmap(src).then(function (b) { bitmap = b; })");
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(2));
    assert(js.eval("bitmap ? bitmap.width : -1").to_number() == 2.0);
    // The bitmap copied the canvas' pixels rather than a blank store.
    js.eval("ctx.drawImage(bitmap, 0, 0)");
    assert(js.eval("Array.prototype.join.call(ctx.getImageData(0,0,1,1).data, ',')").to_string() ==
           "0,0,255,255");
    // Clean all the way through.
    assert(js.eval("String(dst.toDataURL()).slice(0, 22)").to_string() == "data:image/png;base64,");
}
