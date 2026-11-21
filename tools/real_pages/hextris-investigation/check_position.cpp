// Compare CSS-pixel border boxes to Chromium 131 on position.html. No rounding
// before comparison: one known subpixel discrepancy remains visible on resize.
#include "litebird/shell/BrowserShell.hpp"
#include <iomanip>
#include <iostream>
using namespace litebird;
struct ExpectedBox { const char* id; Rect before, resized; };
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    BrowserShell shell(800, 600);
    if (!shell.navigate_to(argv[1])) return 2;
    const ExpectedBox boxes[]{
        {"start", {320,228,200,150}, {256,182.390625f,160,120}},
        {"fixed", {740,510,20,30}, {588,402,20,30}},
        {"mixed", {8,-60,40,20}, {8,-48,40,20}},
        {"outer", {40,20,210,110}, {40,20,210,110}},
        {"inner", {65,45,150,50}, {65,45,150,50}},
        {"child", {65,45,16,8}, {65,45,16,8}},
        {"auto", {215,25,10,10}, {215,25,10,10}},
    };
    int failed = 0;
    for (int w : {800,640}) {
        shell.set_viewport_size(w,w == 800 ? 600 : 480);
        for (const auto& box : boxes) {
            auto e = shell.document()->get_element_by_id(box.id);
            if (!e) return 2;
            const auto& r = e->bounding_box();
            auto expected = w == 800 ? box.before : box.resized;
            bool ok = r.x == expected.x && r.y == expected.y && r.width == expected.width && r.height == expected.height;
            std::cout << (ok ? "PASS " : "FAIL ") << w << ' ' << box.id << ' ' << std::setprecision(12)
                      << r.x << ',' << r.y << ',' << r.width << ',' << r.height << '\n';
            failed += !ok;
        }
    }
    return failed != 0;
}
