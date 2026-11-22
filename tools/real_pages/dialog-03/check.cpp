// What a real click and a Tab step reach while a modal dialog is open, and
// what they reach again once it has closed. Every click is the shell's own hit
// path, so nothing here calls a page handler directly.
#include "litebird/shell/BrowserShell.hpp"
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
    auto active = [&]() { return eval("document.activeElement.id"); };
    auto clicks = [&]() { return eval("document.getElementById('count').textContent"); };
    auto el = [&](const std::string& id) { return doc->get_element_by_id(id); };
    auto inside = [&](const std::shared_ptr<Element>& node, const std::string& id) {
        auto ancestor = el(id);
        for (auto n = node; n && ancestor; n = std::dynamic_pointer_cast<Element>(n->parent())) {
            if (n == ancestor) return true;
        }
        return false;
    };
    auto click = [&](const std::string& id) {
        shell.relayout();
        auto element = el(id);
        if (!element) return false;
        const auto r = element->bounding_box();
        return shell.click_at(r.x + r.width/2, r.y + r.height/2);
    };
    auto click_point = [&](float x, float y) { return shell.click_at(x, y); };
    // A point inside the page button and outside the dialog's own box.
    auto behind_point = [&]() {
        shell.relayout();
        const auto r = el("behind")->bounding_box();
        return std::pair<float, float>{r.x + r.width/2, r.y + r.height/2};
    };

    check(click("behind"), "activate the page button with nothing open");
    check(clicks() == "1", "the page button works before any dialog: " + clicks());

    check(click("open"), "activate the modal opener");
    check(eval("document.getElementById('sheet').open") == "true", "the modal dialog is open");
    check(active() == "inside", "and it took the focus: " + active());

    // The dialog is painted where it sits; the page behind it is out of reach
    // anyway, and the event target of that point is the dialog.
    const auto [bx, by] = behind_point();
    auto hit = shell.element_at(bx, by);
    check(hit && hit == el("sheet"), "a point behind the dialog hits the dialog");
    check(click_point(bx, by), "a click behind the dialog finds something to click");
    check(clicks() == "1", "and the page button behind it did not run: " + clicks());

    check(eval("(function () { document.getElementById('behind').focus();"
               " return document.activeElement.id; })()") == "inside",
          "a script cannot focus out of the dialog: " + active());

    shell.relayout();
    shell.focus_next();
    auto tabbed = shell.focused_element();
    check(tabbed && inside(tabbed, "sheet"), "Tab stays inside the dialog: " + (tabbed ? tabbed->id() : "nothing"));

    // A script's own click is not input: the element is still there to click.
    check(eval("(function () { document.getElementById('behind').click();"
               " return document.getElementById('count').textContent; })()") == "2",
          "a script click behind the dialog still runs: " + clicks());

    check(click("dismiss"), "activate Dismiss");
    check(eval("document.getElementById('sheet').open") == "false", "the modal dialog closed");
    check(active() == "open", "and the focus came back to the opener: " + active());
    check(el("behind") && shell.element_at(bx, by) == el("behind"), "the page is under the pointer again");
    check(click("behind"), "activate the page button after the close");
    check(clicks() == "3", "the page button works again: " + clicks());

    // An ordinary show() takes no input away; only a modal dialog does.
    check(click("open2"), "activate the ordinary opener");
    check(eval("document.getElementById('ordinary').open") == "true", "the ordinary dialog is open");
    check(active() == "ordinary-inside", "and it took the focus: " + active());
    check(click("behind"), "activate the page button behind an ordinary dialog");
    check(clicks() == "4", "an ordinary dialog isolates nothing: " + clicks());

    std::cout << passed << '/' << total << " modal isolation checks passed\n";
    return passed != total;
}
