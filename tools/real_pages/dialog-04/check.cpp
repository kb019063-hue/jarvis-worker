// What the frame shows while a modal dialog is open: the UA backdrop over the
// page, and the dialog's own box in front of a positioned box that would other-
// wise paint above it. Points are fixed and named in the table below.
//
//   A  (400,60)   inside the page block, outside the dialog
//   B  (80,135)   inside the dialog, and inside the z-index 5 box behind it
//   C  (500,400)  white page background, outside the page block
#include "litebird/shell/BrowserShell.hpp"
#include "litebird/render/ImageDecoder.hpp"
#include <iostream>
using namespace litebird;

int main(int argc, char** argv) {
    BrowserShell shell(800, 600);
    if (argc != 2 || !shell.navigate_to(argv[1])) return 2;
    auto doc = shell.document();
    int passed = 0, total = 0;
    auto check = [&](bool ok, const std::string& what) {
        ++total;
        if (ok) ++passed; else std::cerr << "FAIL: " << what << '\n';
    };
    auto eval = [&](const std::string& js) { return shell.js_engine()->eval(js).to_string(); };
    auto click = [&](const std::string& id) {
        shell.relayout();
        auto element = doc->get_element_by_id(id);
        if (!element) return false;
        const auto r = element->bounding_box();
        return shell.click_at(r.x + r.width/2, r.y + r.height/2);
    };
    auto pixel = [&](int x, int y) -> Color {
        auto image = ImageDecoder::decode(shell.render_png_frame());
        if (!image || !image->is_valid()) return Color(1, 2, 3);
        return Color::from_argb(image->pixels[static_cast<size_t>(y) * image->width + x]);
    };
    auto expect = [&](const char* point, int x, int y, Color wanted) {
        const Color got = pixel(x, y);
        check(got.r == wanted.r && got.g == wanted.g && got.b == wanted.b,
              std::string(point) + " is " + std::to_string(got.r) + "," + std::to_string(got.g) + "," +
                  std::to_string(got.b) + " wanted " + std::to_string(wanted.r) + "," +
                  std::to_string(wanted.g) + "," + std::to_string(wanted.b));
    };
    // 10% black over the colour already there: the renderer's integer
    // source-over, (0*26 + dst*229 + 127) / 255.
    const Color red(255, 0, 0), blue(0, 0, 255), white(255, 255, 255), green(0, 255, 0);
    const Color dim_red(229, 0, 0), dim_white(229, 229, 229);

    // Nothing open: the page block, the z-index 5 box, and the white page.
    expect("A", 400, 60, red);
    expect("B", 80, 135, blue);
    expect("C", 500, 400, white);

    check(click("open"), "activate the modal opener");
    check(eval("document.getElementById('sheet').open") == "true", "the modal dialog is open");
    expect("A", 400, 60, dim_red);      // the backdrop is over the page block
    expect("C", 500, 400, dim_white);   // and over the white page
    expect("B", 80, 135, green);        // the dialog is above the z-index 5 box, undimmed

    eval("document.getElementById('sheet').close()");
    shell.relayout();
    expect("A", 400, 60, red);
    expect("B", 80, 135, blue);
    expect("C", 500, 400, white);

    // An ordinary dialog takes no top layer and paints no backdrop.
    check(click("open-ordinary"), "activate the ordinary opener");
    check(eval("document.getElementById('sheet').open") == "true", "the ordinary dialog is open");
    expect("A", 400, 60, red);          // no backdrop
    expect("B", 80, 135, blue);         // the z-index 5 box still wins
    expect("C", 500, 400, white);

    std::cout << passed << '/' << total << " top-layer paint checks passed\n";
    return passed != total;
}
