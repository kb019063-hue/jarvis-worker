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

namespace {

/// A 2x2 opaque red PNG, built here rather than kept as a fixture so the
/// suite has no file to go missing. The pixels are two rows of
/// filter-byte 0 then `FF 00 00 FF` twice each; the IDAT is a *stored*
/// (uncompressed) deflate block, which is why there is no zlib call below —
/// 18 bytes of pixels are cheaper to write out than to compress, and a stored
/// block is what every inflater must accept.
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
    // zlib header, one final stored block, and the Adler-32 of the pixels.
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

TEST(a_canvas_drawing_a_cross_origin_image_cannot_be_read_back) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "litebird_canvas_origin_clean";
    fs::create_directories(dir);
    {
        std::ofstream out(dir / "red.png", std::ios::binary);
        const std::string png = two_by_two_red_png();
        out.write(png.data(), static_cast<std::streamsize>(png.size()));
    }
    // Two roots: one the page's own origin, one foreign.
    ResourceLoader::set_origin_root_for_testing("https://app.example.test", dir.string());
    ResourceLoader::set_origin_root_for_testing("https://other.example.test", dir.string());

    // JsInterpreter is not movable, so each page is built in place and the
    // checks run inside the same scope.
    const auto with_page = [&](const std::string& image_url, const auto& checks) {
        auto doc = std::make_shared<Document>();
        doc->set_url("https://app.example.test/index.html");
        JsInterpreter js(doc);
        js.load_page("<!doctype html><img id=x src=\"" + image_url + "\">"
                     "<canvas id=a width=4 height=4></canvas>"
                     "<canvas id=b width=4 height=4></canvas>"
                     "<script>"
                     "  window.img = document.getElementById('x');"
                     "  window.a = document.getElementById('a');"
                     "  window.b = document.getElementById('b');"
                     "  window.actx = a.getContext('2d');"
                     "  window.bctx = b.getContext('2d');"
                     "</script>", true);
        js.notify_document_loaded();
        EventLoop::default_loop().run_until_idle(std::chrono::seconds(3));
        // The layout pass is what fetches and decodes an <img>.
        StyleResolver resolver;
        LayoutTree::build(doc, resolver, 800, 600);
        checks(js);
    };

    // --- a cross-origin image taints ---------------------------------------
    with_page("https://other.example.test/red.png", [](JsInterpreter& js) {
        const std::string drew = js.eval(
            "(function(){ try { actx.drawImage(img,0,0); return 'drew'; }"
            " catch (e) { return 'err:' + e.name; } })()").to_string();
        assert(drew == "drew");
        const std::string read_back = js.eval(
            "(function(){ try { return 'data:' + String(a.toDataURL()).slice(0, 14); }"
            " catch (e) { return 'err:' + e.name; } })()").to_string();
        // The whole point: the pixels are no longer the script's to read.
        assert(read_back == "err:SecurityError");
        // A canvas that drew nothing of the sort is unaffected.
        assert(js.eval("String(b.toDataURL()).slice(0, 22)").to_string() == "data:image/png;base64,");
    });

    // --- the same image from the page's own origin does not ----------------
    with_page("https://app.example.test/red.png", [](JsInterpreter& js) {
        const std::string drew = js.eval(
            "(function(){ try { actx.drawImage(img,0,0); return 'drew'; }"
            " catch (e) { return 'err:' + e.name; } })()").to_string();
        assert(drew == "drew");
        const std::string read_back = js.eval(
            "String(a.toDataURL()).slice(0, 22)").to_string();
        assert(read_back == "data:image/png;base64,");
        // And the pixels really are the image's, not a blank canvas'.
        const std::string pixel = js.eval(
            "Array.prototype.join.call(actx.getImageData(0,0,1,1).data, ',')").to_string();
        assert(pixel == "255,0,0,255");
    });

    ResourceLoader::clear_origin_roots_for_testing();
}
