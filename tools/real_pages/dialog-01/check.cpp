// Nonmodal show/close goes through the outside button and the shell's hit path.
#include "litebird/shell/BrowserShell.hpp"
#include "litebird/render/ImageDecoder.hpp"
#include <algorithm>
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
    auto display = [&](const std::string& id, const std::string& expected) {
        auto value = shell.js_engine()->eval("getComputedStyle(document.getElementById('" + id + "')).display");
        check(value.to_string() == expected, id + " computed display: " + value.to_string());
    };
    auto outside = doc->get_element_by_id("outside"), marker = doc->get_element_by_id("marker");
    auto probe = doc->get_element_by_id("probe");
    if (!outside || !marker || !probe) return 2;
    Point last_center{};
    for (int step = 0; step < 4; ++step) {
        bool open = step % 2;
        display("probe", open ? "block" : "none");
        check(probe->has_layout_box() == open, "dialog layout");
        check(marker->has_layout_box() == open, "descendant layout");
        auto image = ImageDecoder::decode(shell.render_png_frame());
        if (!image || !image->is_valid()) return 2;
        const auto count = std::count(image->pixels.begin(), image->pixels.end(), 0xff13c975u);
        check(count == (open ? 32*24 : 0), "marker pixels: " + std::to_string(count));
        if (open) {
            auto r = marker->bounding_box();
            last_center = {r.x + r.width/2, r.y + r.height/2};
            check(shell.element_at(last_center.x, last_center.y) == marker, "visible marker receives hit");
        } else if (step > 0) {
            auto hit = shell.element_at(last_center.x, last_center.y);
            bool inside = false;
            for (std::shared_ptr<Node> n = hit; n; n = n->parent()) inside |= n == probe;
            check(!inside, "closed subtree no longer receives hit");
        }
        if (step != 3) {
            auto r = outside->bounding_box();
            check(shell.click_at(r.x + r.width/2, r.y + r.height/2), "activate outside button");
        }
    }
    for (auto id : {"boolean", "author-visible", "author-hidden"}) {
        bool visible = std::string(id) != "author-hidden";
        display(id, visible ? "block" : "none");
        auto el = doc->get_element_by_id(id);
        check(el && el->has_layout_box() == visible, std::string(id) + " layout");
    }
    std::cout << passed << '/' << total << " dialog display checks passed\n";
    return passed != total;
}
