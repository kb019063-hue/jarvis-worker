// Phase 262, finding 266: the canvas makes its Blob.
//
// HTML §4.12.5.4 gives `HTMLCanvasElement` a second way to hand a script
// every pixel the canvas holds: `toBlob(callback, type, quality)` fires
// `callback` with a `Blob` in a task of its own. `toDataURL()` landed in
// Phase 144; this method did not — the docs of Phase 153 describe an
// implementation the tree does not contain (there is no `toBlob` binding,
// and there never was a suite for one), so §4.12.5.1's restriction on a
// tainted bitmap had half of its surface with nothing to apply to.
//
// The checks that matter, in the order they can fail:
//
//  * the callback fires asynchronously, never synchronously;
//  * the blob is `image/png` and really holds PNG bytes;
//  * an unsupported type, a non-MIME type and a zero-dimension canvas all
//    hand the callback `null` — the encoding-failure answer;
//  * a callback that is not callable is a `TypeError`, thrown synchronously;
//  * a tainted canvas throws `SecurityError` synchronously, the same rule
//    `toDataURL()` enforces (Phase 237), instead of handing the pixels over.

// The two pages below differ in one character of the image's URL, like
// Phase 237's: one image, served from the script's own origin and from
// another, and the same canvas either way.
namespace {

std::string two_by_two_red_png() {
    const auto crc32 = [](const std::string& data) {
        static uint32_t table[256];
        static bool built = false;
        if (!built) {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                table[i] = c;
            }
            built = true;
        }
        uint32_t c = 0xFFFFFFFFu;
        for (unsigned char b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    };
    const auto be32 = [](uint32_t v) {
        std::string out;
        for (int shift = 24; shift >= 0; shift -= 8) out += static_cast<char>((v >> shift) & 0xFF);
        return out;
    };
    const auto chunk = [&](const std::string& kind, const std::string& body) {
        return be32(static_cast<uint32_t>(body.size())) + kind + body + be32(crc32(kind + body));
    };
    // One final stored (uncompressed) deflate block, so no zlib call is
    // needed here.
    static const unsigned char kIdat[] = {
        0x78, 0x01, 0x01, 0x12, 0x00, 0xED, 0xFF,
        0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0xFF,
        0xFF, 0x00, 0x00, 0xFF,
        0x47, 0xCA, 0x07, 0xF9
    };
    const std::string header = be32(2) + be32(2) + std::string("\x08\x06\x00\x00\x00", 5);
    return std::string("\x89PNG\r\n\x1a\n", 8) + chunk("IHDR", header) +
           chunk("IDAT", std::string(reinterpret_cast<const char*>(kIdat), sizeof(kIdat))) +
           chunk("IEND", "");
}

} // namespace

TEST(a_canvas_serializes_itself_into_a_blob) {
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(
        "var c = document.createElement('canvas'); c.width = 4; c.height = 4;"
        "var ctx = c.getContext('2d'); ctx.fillStyle = '#ff0000'; ctx.fillRect(0, 0, 4, 4);"
        "window.__callbackAsync = false; window.__blob = null;"
        "c.toBlob(function(b) { window.__callbackAsync = true; window.__blob = b; });");
    // The callback must not have fired synchronously.
    assert(!js.global_object().get_property("__callbackAsync").is_truthy());
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
    assert(js.global_object().get_property("__callbackAsync").is_truthy());
    assert(js.eval("window.__blob instanceof Blob").is_truthy());
    assert(js.eval("window.__blob.type").to_string() == "image/png");
    assert(js.eval("window.__blob.size").to_number() > 0.0);
    // The bytes really are a PNG: the signature is the first four bytes.
    js.eval(
        "window.__magic = 'pending';"
        "window.__blob.arrayBuffer().then(function(buf) {"
        "  window.__magic = Array.prototype.join.call(new Uint8Array(buf).subarray(0, 8), ',');"
        "});");
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
    assert(js.global_object().get_property("__magic").to_string() == "137,80,78,71,13,10,26,10");

    // --- the encoding-failure answers: the callback gets null --------------
    js.eval(
        "window.__nullJpeg = 'unset'; window.__nullGarbage = 'unset'; window.__nullZero = 'unset';"
        "var cj = document.createElement('canvas');"
        "cj.toBlob(function(b) { window.__nullJpeg = b === null ? 'null' : b.type; }, 'image/jpeg');"
        "var cg = document.createElement('canvas');"
        "cg.toBlob(function(b) { window.__nullGarbage = b === null ? 'null' : b.type; }, 'not a mime type');"
        "var cz = document.createElement('canvas'); cz.width = 0; cz.height = 10;"
        "cz.toBlob(function(b) { window.__nullZero = b === null ? 'null' : b.type; });");
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
    assert(js.global_object().get_property("__nullJpeg").to_string() == "null");
    assert(js.global_object().get_property("__nullGarbage").to_string() == "null");
    assert(js.global_object().get_property("__nullZero").to_string() == "null");

    // An omitted type, the empty string and `undefined` all mean image/png;
    // the quality argument is accepted and ignored for a lossless encoder.
    js.eval(
        "window.__defaultType = 'unset'; window.__qualityType = 'unset';"
        "var cd = document.createElement('canvas');"
        "cd.toBlob(function(b) { window.__defaultType = b ? b.type : 'null'; });"
        "var cq = document.createElement('canvas');"
        "cq.toBlob(function(b) { window.__qualityType = b ? b.type : 'null'; }, 'image/png', 0.01);");
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
    assert(js.global_object().get_property("__defaultType").to_string() == "image/png");
    assert(js.global_object().get_property("__qualityType").to_string() == "image/png");

    // --- a callback that is not callable is a synchronous TypeError --------
    assert(js.eval(
        "(function(){ try { document.createElement('canvas').toBlob('nope'); return 'no'; }"
        " catch (e) { return e.name; } })()").to_string() == "TypeError");
    assert(js.eval(
        "(function(){ try { document.createElement('canvas').toBlob(); return 'no'; }"
        " catch (e) { return e.name; } })()").to_string() == "TypeError");

    // --- the origin-clean contract, with the machinery of Phase 237 --------
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "litebird_canvas_toblob_origin";
    fs::create_directories(dir);
    {
        std::ofstream out(dir / "red.png", std::ios::binary);
        const std::string png = two_by_two_red_png();
        out.write(png.data(), static_cast<std::streamsize>(png.size()));
    }
    ResourceLoader::set_origin_root_for_testing("https://app.example.test", dir.string());
    ResourceLoader::set_origin_root_for_testing("https://other.example.test", dir.string());

    const auto with_page = [&](const std::string& image_url, const auto& checks) {
        auto doc = std::make_shared<Document>();
        doc->set_url("https://app.example.test/index.html");
        JsInterpreter page_js(doc);
        page_js.load_page("<!doctype html><img id=x src=\"" + image_url + "\">"
                          "<canvas id=c width=4 height=4></canvas>"
                          "<script>window.ctx = document.getElementById('c').getContext('2d');"
                          "window.img = document.getElementById('x');</script>", true);
        page_js.notify_document_loaded();
        EventLoop::default_loop().run_until_idle(std::chrono::seconds(3));
        // The layout pass is what fetches and decodes an <img>.
        StyleResolver resolver;
        LayoutTree::build(doc, resolver, 800, 600);
        checks(page_js);
    };

    // A cross-origin image taints: toBlob, like toDataURL, throws instead of
    // handing the pixels over — and it does so synchronously, without waiting
    // for the task the callback would have run in.
    with_page("https://other.example.test/red.png", [](JsInterpreter& js) {
        assert(js.eval(
            "(function(){ try { ctx.drawImage(img,0,0); return 'drew'; } catch (e) { return 'err:' + e.name; } })()")
                   .to_string() == "drew");
        assert(js.eval(
            "(function(){ try { document.getElementById('c').toBlob(function(){}); return 'no'; }"
            " catch (e) { return e.name; } })()").to_string() == "SecurityError");
    });

    // The same image from the page's own origin does not taint: the callback
    // fires with a real Blob.
    with_page("https://app.example.test/red.png", [](JsInterpreter& js) {
        assert(js.eval(
            "(function(){ try { ctx.drawImage(img,0,0); return 'drew'; } catch (e) { return 'err:' + e.name; } })()")
                   .to_string() == "drew");
        js.eval(
            "window.__clean = 'pending'; var c = document.getElementById('c');"
            "c.toBlob(function(b) { window.__clean = b ? b.type : 'null'; });");
        EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
        assert(js.global_object().get_property("__clean").to_string() == "image/png");
    });

    std::printf("      [canvas toBlob] async callback, PNG bytes, null answers, SecurityError on taint\n");
    std::fflush(stdout);
}
