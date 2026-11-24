// Phase 272, finding 276: `createImageBitmap` read one argument of six.
//
// The binding was `I.method("createImageBitmap", 1, …)` and never looked at
// `args[1]`. HTML §8.11.2's method is
//
//     createImageBitmap(image, options)
//     createImageBitmap(image, sx, sy, sw, sh, options)
//
// so every crop, every resize and both of the zero-size checks in its steps 1
// and 2 went unperformed — and because the promise went on resolving with a
// correctly-sized bitmap of the *whole* image, a page could not tell that
// anything was missing. That is why this suite asserts the dimensions of the
// result rather than only that the call resolved: `createImageBitmap(cv, 0, 0,
// 4, 4)` on an 8×8 canvas answered 8×8, and looked like success.
//
// Two failures of a different kind came out with it. A `Blob` was refused with
// a `TypeError` although §8.11.2 lists it as a source (its data is decoded, and
// only data that is not an image is an `InvalidStateError`), and an `ImageData`
// resolved with a bitmap of the right size and **zeroed** pixels, so a caller
// who drew it got a transparent rectangle and no error.
//
// The pixels are what is asserted where the pixels carry the answer: a crop that
// is right at the corner and wrong one pixel in resolves to the same dimensions,
// so a dimension-only test would pass on it.

namespace {

/// The four components at (x, y) of the canvas after `bitmap` is drawn on it,
/// as "r,g,b,a" — which is how a suite can talk about where a pixel came from
/// without depending on `ImageBitmap` exposing pixels it does not expose.
std::string pixel_of_bitmap(JsInterpreter& js, const std::string& bitmap, int x, int y) {
    return js.eval("(function(){ var c = document.createElement('canvas');"
                   " c.width = " + std::to_string(x + 1) + "; c.height = " + std::to_string(y + 1) + ";"
                   " var g = c.getContext('2d'); g.drawImage(" + bitmap + ", 0, 0);"
                   " var d = g.getImageData(" + std::to_string(x) + ", " + std::to_string(y) + ", 1, 1).data;"
                   " return d[0] + ',' + d[1] + ',' + d[2] + ',' + d[3]; })()")
        .to_string();
}

/// A 2×2 canvas: red, green / blue, white. Each quadrant a different colour, so
/// which part of the source a pixel came from is decidable from the pixel.
const char* kQuadrants =
    "var cv = document.createElement('canvas'); cv.width = 2; cv.height = 2;"
    "var g = cv.getContext('2d');"
    "g.fillStyle = '#ff0000'; g.fillRect(0, 0, 1, 1);"
    "g.fillStyle = '#00ff00'; g.fillRect(1, 0, 1, 1);"
    "g.fillStyle = '#0000ff'; g.fillRect(0, 1, 1, 1);"
    "g.fillStyle = '#ffffff'; g.fillRect(1, 1, 1, 1);";

const char* kRed = "255,0,0,255";
const char* kGreen = "0,255,0,255";
const char* kBlue = "0,0,255,255";
const char* kWhite = "255,255,255,255";
const char* kTransparent = "0,0,0,0";

} // namespace

TEST(create_image_bitmap_crops_resizes_and_takes_the_sources_the_standard_lists) {
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(kQuadrants);

    // --- The whole image, which always worked. Asserted first so that a later
    // failure is known to be the crop and not the source.
    js.eval("window.bm = null; createImageBitmap(cv).then(function(b){ window.bm = b; });");
    js.eval("void 0");
    assert(js.eval("window.bm && window.bm.width + 'x' + window.bm.height").to_string() == "2x2");
    assert(pixel_of_bitmap(js, "window.bm", 0, 0) == kRed);
    assert(pixel_of_bitmap(js, "window.bm", 1, 0) == kGreen);
    assert(pixel_of_bitmap(js, "window.bm", 0, 1) == kBlue);
    assert(pixel_of_bitmap(js, "window.bm", 1, 1) == kWhite);

    // --- The crop. Each single pixel is picked out by its own colour, so a
    // crop that read the right size from the wrong origin fails here.
    struct Crop {
        const char* rect;
        const char* colour;
    };
    for (const Crop& crop : {Crop{"0,0,1,1", kRed}, Crop{"1,0,1,1", kGreen}, Crop{"0,1,1,1", kBlue},
                             Crop{"1,1,1,1", kWhite}}) {
        js.eval(std::string("window.c = null; createImageBitmap(cv, ") + crop.rect +
                ").then(function(b){ window.c = b; });");
        assert(js.eval("window.c && window.c.width + 'x' + window.c.height").to_string() == "1x1");
        assert(pixel_of_bitmap(js, "window.c", 0, 0) == crop.colour);
    }

    // A rectangle wider and taller than the source is not an error: §8.11.2
    // crops "with formatting", so the part outside is transparent black and
    // the part inside is where it belongs.
    js.eval("window.p = null; createImageBitmap(cv, 0, 0, 4, 4).then(function(b){ window.p = b; });");
    assert(js.eval("window.p.width + 'x' + window.p.height").to_string() == "4x4");
    assert(pixel_of_bitmap(js, "window.p", 0, 0) == kRed);
    assert(pixel_of_bitmap(js, "window.p", 1, 1) == kWhite);
    assert(pixel_of_bitmap(js, "window.p", 2, 0) == kTransparent);
    assert(pixel_of_bitmap(js, "window.p", 3, 3) == kTransparent);

    // An origin outside the image crops entirely to transparent, which is the
    // same rule and the case a bounds check gets wrong in the other direction.
    js.eval("window.o = null; createImageBitmap(cv, 5, 5, 2, 2).then(function(b){ window.o = b; });");
    assert(js.eval("window.o.width + 'x' + window.o.height").to_string() == "2x2");
    assert(pixel_of_bitmap(js, "window.o", 0, 0) == kTransparent);
    assert(pixel_of_bitmap(js, "window.o", 1, 1) == kTransparent);

    // --- Step 1: a zero side of the source rectangle is a RangeError, and it
    // is checked before the image is, which is why it is not an
    // InvalidStateError. It also must not resolve.
    for (const char* rect : {"0,0,0,2", "0,0,2,0"}) {
        const std::string name = js.eval(std::string("window.z = 'none';"
            "createImageBitmap(cv, ") + rect + ").then(function(){ window.z = 'resolved'; },"
            " function(e){ window.z = e.name; }); window.z").to_string();
        assert(name == "none");           // not settled synchronously
        assert(js.eval("window.z").to_string() == "RangeError");
    }

    // --- Step 2: `resizeWidth`/`resizeHeight` of zero is an InvalidStateError.
    for (const char* opts : {"{resizeWidth:0, resizeHeight:2}", "{resizeWidth:2, resizeHeight:0}"}) {
        js.eval(std::string("window.r0 = 'none'; createImageBitmap(cv, ") + opts +
                ").then(function(){ window.r0 = 'resolved'; },"
                " function(e){ window.r0 = e.name; });");
        assert(js.eval("window.r0").to_string() == "InvalidStateError");
    }

    // --- The resize itself, and the aspect ratio when only one side is given.
    js.eval("window.r = null; createImageBitmap(cv, {resizeWidth: 4, resizeHeight: 6})"
            ".then(function(b){ window.r = b; });");
    assert(js.eval("window.r.width + 'x' + window.r.height").to_string() == "4x6");
    // One side alone keeps the aspect ratio, which needs a source that is not
    // square to say anything: a 2x2 halved is 1x1 either way, so the ratio is
    // tested on a 4x2 canvas. Half the width is half the height; twice the
    // height is twice the width.
    js.eval("var wide = document.createElement('canvas'); wide.width = 4; wide.height = 2;"
            "var wg = wide.getContext('2d');"
            "wg.fillStyle = '#ff0000'; wg.fillRect(0, 0, 2, 2);"
            "wg.fillStyle = '#0000ff'; wg.fillRect(2, 0, 2, 2);");
    js.eval("window.rh = null; createImageBitmap(wide, {resizeWidth: 2}).then(function(b){ window.rh = b; });");
    assert(js.eval("window.rh.width + 'x' + window.rh.height").to_string() == "2x1");
    js.eval("window.rw = null; createImageBitmap(wide, {resizeHeight: 4}).then(function(b){ window.rw = b; });");
    assert(js.eval("window.rw.width + 'x' + window.rw.height").to_string() == "8x4");
    // Doubling keeps the pixels where they belong: red stays left of blue.
    assert(pixel_of_bitmap(js, "window.rw", 0, 0) == kRed);
    assert(pixel_of_bitmap(js, "window.rw", 7, 3) == kBlue);
    // And a square source scaled by one side stays square, which is the case a
    // ratio computed backwards also passes — asserted so that the two are told
    // apart by the 4x2 above and not by this.
    js.eval("window.rs = null; createImageBitmap(cv, {resizeWidth: 1}).then(function(b){ window.rs = b; });");
    assert(js.eval("window.rs.width + 'x' + window.rs.height").to_string() == "1x1");

    // --- `flipY`, off unless asked: the rows are reversed and the columns
    // are not, so red stays at the left and blue comes to the top.
    js.eval("window.f = null; createImageBitmap(cv, {flipY: true}).then(function(b){ window.f = b; });");
    assert(pixel_of_bitmap(js, "window.f", 0, 0) == kBlue);
    assert(pixel_of_bitmap(js, "window.f", 1, 0) == kWhite);
    assert(pixel_of_bitmap(js, "window.f", 0, 1) == kRed);
    // ...and `flipY: false` is the identity, which is the direction a default
    // that is applied to both values gets wrong.
    js.eval("window.nf = null; createImageBitmap(cv, {flipY: false}).then(function(b){ window.nf = b; });");
    assert(pixel_of_bitmap(js, "window.nf", 0, 0) == kRed);

    // --- The crop and the resize compose, crop first: this is the order the
    // steps have, and cropping after resizing would sample the wrong pixels.
    js.eval("window.cr = null; createImageBitmap(cv, 1, 0, 1, 2, {resizeWidth: 2, resizeHeight: 2})"
            ".then(function(b){ window.cr = b; });");
    assert(js.eval("window.cr.width + 'x' + window.cr.height").to_string() == "2x2");
    // The green-over-white column, doubled: top green, bottom white.
    assert(pixel_of_bitmap(js, "window.cr", 0, 0) == kGreen);
    assert(pixel_of_bitmap(js, "window.cr", 1, 0) == kGreen);
    assert(pixel_of_bitmap(js, "window.cr", 0, 1) == kWhite);

    // --- `ImageData` as a source, with its pixels. A bitmap of the right size
    // and zeroed pixels was the second half of this finding: the promise
    // resolved either way.
    js.eval("var id = new ImageData(new Uint8ClampedArray([255,0,0,255, 0,255,0,255,"
            " 0,0,255,255, 255,255,255,255]), 2, 2);"
            "window.im = null; createImageBitmap(id).then(function(b){ window.im = b; });");
    assert(js.eval("window.im.width + 'x' + window.im.height").to_string() == "2x2");
    assert(pixel_of_bitmap(js, "window.im", 0, 0) == kRed);
    assert(pixel_of_bitmap(js, "window.im", 1, 0) == kGreen);
    assert(pixel_of_bitmap(js, "window.im", 0, 1) == kBlue);
    assert(pixel_of_bitmap(js, "window.im", 1, 1) == kWhite);
    // It is a copy, not a view: writing the ImageData afterwards must not
    // change the bitmap that was already made from it.
    js.eval("id.data[0] = 0; id.data[1] = 0; id.data[2] = 0;");
    assert(pixel_of_bitmap(js, "window.im", 0, 0) == kRed);

    // --- A `Blob` is a source, and its bytes are decoded. Refusing it with a
    // TypeError was this phase's third defect: `Blob` is in §8.11.2's list.
    // A 4x2 PNG, left half red and right half blue, built here rather than
    // kept as a fixture so that the suite has no file to go missing.
    js.eval("function fromB64(s){ var t = atob(s); var a = new Uint8Array(t.length);"
            " for (var i = 0; i < t.length; ++i) a[i] = t.charCodeAt(i); return a; }");
    const std::string kPng =
        "iVBORw0KGgoAAAANSUhEUgAAAAQAAAACCAYAAAB/qH1jAAAAFElEQVR4nGP4z8DwH4Sh1H8GdAEABykP8fcOk50AAAAASUVORK5CYII=";
    js.eval("window.b = null; createImageBitmap(new Blob([fromB64('" + kPng + "')],"
            " {type: 'image/png'})).then(function(x){ window.b = x; });");
    assert(js.eval("window.b.width + 'x' + window.b.height").to_string() == "4x2");
    assert(pixel_of_bitmap(js, "window.b", 0, 0) == kRed);
    assert(pixel_of_bitmap(js, "window.b", 3, 0) == kBlue);
    // A Blob's bytes take the same rectangle and resize as a canvas does.
    js.eval("window.bc = null; createImageBitmap(new Blob([fromB64('" + kPng + "')],"
            " {type: 'image/png'}), 2, 0, 2, 2).then(function(x){ window.bc = x; });");
    assert(js.eval("window.bc.width + 'x' + window.bc.height").to_string() == "2x2");
    assert(pixel_of_bitmap(js, "window.bc", 0, 0) == kBlue);

    // A Blob whose data is not an image is an InvalidStateError, not a
    // TypeError: the source was a Blob, which is the right kind of thing.
    js.eval("window.j = 'none'; createImageBitmap(new Blob([new Uint8Array([1,2,3])]))"
            ".then(function(){ window.j = 'resolved'; }, function(e){ window.j = e.name; });");
    assert(js.eval("window.j").to_string() == "InvalidStateError");

    // --- A source that is nothing at all is still a TypeError, and the two
    // rejection kinds above are what tells the three cases apart.
    js.eval("window.t = 'none'; createImageBitmap({}).then(function(){ window.t = 'resolved'; },"
            " function(e){ window.t = e.name; });");
    assert(js.eval("window.t").to_string() == "TypeError");

    // --- An `<img>` that has no decoded image is step 3's unusable source:
    // InvalidStateError. Before this phase it was a TypeError, which said the
    // wrong thing about a source the standard lists.
    js.eval("var bad = document.createElement('img');"
            "window.i = 'none'; createImageBitmap(bad).then(function(){ window.i = 'resolved'; },"
            " function(e){ window.i = e.name; });");
    assert(js.eval("window.i").to_string() == "InvalidStateError");
}
