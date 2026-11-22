// Where the focus is after a dialog opens and after it closes. Every click is
// a real one through the shell's hit path, so the mousedown focus is the
// browser's and not a script's.
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
    auto active_tag = [&]() { return eval("document.activeElement.tagName"); };
    auto is_open = [&](const std::string& id) { return eval("document.getElementById('" + id + "').open") == "true"; };
    auto click = [&](const std::string& id) {
        // A page that changed since the last click moved its boxes; hit testing
        // reads the layout, so lay it out again the way a frame would.
        shell.relayout();
        auto element = doc->get_element_by_id(id);
        if (!element) return false;
        const auto r = element->bounding_box();
        return shell.click_at(r.x + r.width/2, r.y + r.height/2);
    };

    check(active_tag() == "BODY", "nothing has the focus at load: " + active_tag() + " " + active());

    // showModal focuses the first focusable descendant, and close gives the
    // focus back to the element that had it when the dialog opened.
    check(click("first-open"), "activate the opener");
    check(active() == "first-a", "showModal focuses the first control: " + active());
    check(is_open("first"), "the dialog is open");
    check(eval("document.getElementById('first').hasAttribute('tabindex')") == "false",
          "a dialog with a control is not made focusable itself");

    check(click("first-b"), "activate the second control");
    check(active() == "first-b", "a click inside the dialog moves the focus: " + active());
    check(eval("(function () { try { document.getElementById('first').show(); return 'no throw'; }"
               " catch (e) { return e.name; } })()") == "no throw",
          "show() on an open modal dialog does not throw");
    check(active() == "first-b", "and leaves the focus where it was: " + active());

    check(click("first-cancel"), "activate Cancel");
    check(!is_open("first"), "Cancel closes the dialog");
    check(active() == "first-open", "close gives the focus back to the opener: " + active());
    eval("document.getElementById('first').close()");
    check(active() == "first-open", "a second close changes nothing: " + active());

    // A descendant with autofocus is preferred to the first control.
    check(click("auto-open"), "activate the autofocus opener");
    check(active() == "auto-focused", "autofocus wins over tree order: " + active());
    check(click("auto-cancel"), "activate autofocus Cancel");
    check(active() == "auto-open", "and the focus goes back: " + active());

    // No control at all: the dialog itself, at tabindex -1.
    check(click("empty-open"), "activate the empty opener");
    check(active() == "empty", "a dialog with no control takes the focus: " + active());
    check(eval("document.getElementById('empty').tabIndex") == "-1", "the dialog is at tabindex -1");
    eval("document.getElementById('empty').close()");
    check(active() == "empty-open", "and gives it back: " + active());

    // show() focuses the same way, but close() has nothing to give back.
    check(click("nonmodal-open"), "activate the nonmodal toggle");
    check(active() == "nonmodal-a", "show() focuses the first control: " + active());
    check(eval("(function () { try { document.getElementById('nonmodal').show(); return 'no throw'; }"
               " catch (e) { return e.name; } })()") == "InvalidStateError",
          "show() on an open ordinary dialog throws");
    check(active() == "nonmodal-a", "and leaves the focus inside: " + active());
    check(click("nonmodal-open"), "activate the nonmodal toggle again");
    check(!is_open("nonmodal"), "the nonmodal dialog closed");
    check(active_tag() == "BODY", "close() of an ordinary dialog leaves nothing focused: " + active_tag() + " " + active());

    // A previous focus that has left the document cannot be restored.
    check(click("gone-open"), "activate the doomed opener");
    check(active() == "doomed-cancel", "the doomed dialog took the focus: " + active());
    eval("document.getElementById('gone-wrap').remove()");
    check(click("doomed-cancel"), "activate Cancel in the doomed dialog");
    check(active_tag() == "BODY", "a previous focus that is gone is not restored: " + active_tag() + " " + active());

    std::cout << passed << '/' << total << " dialog focus checks passed\n";
    return passed != total;
}
