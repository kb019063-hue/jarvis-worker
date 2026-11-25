// Phase 409, finding 409: the focus ring is the tab order, not a list of three
// tag names.
//
// Phase 228 (finding 232) made Tab move the document's focus instead of only a
// number. It left the ring built by `BrowserShell::scan_interactive_elements`,
// which took `a[href]`, `button` (or a `btn`-ish class or id, or any element
// with a click listener) and `input`. Everything else the bindings' own
// `is_focusable` accepts -- `select`, `textarea`, a `summary` in a `details`,
// a `[contenteditable]` region, any `[tabindex]` element -- was reachable by
// `element.focus()` and unreachable by Tab.
//
// The suite asks the two paths the same question and requires one answer: for
// every element on the page, `focus()` and Tab must agree about whether it is
// a stop.
TEST(shell_tab_reaches_every_focusable_element) {
    using namespace litebird;

    const std::string page =
        "<!DOCTYPE html><body style='margin:0'>"
        "<input id='a'>"
        "<select id='b'><option>one</option></select>"
        "<textarea id='c'></textarea>"
        "<div id='d' tabindex='0' style='display:block;width:100px;height:20px'>d</div>"
        "<span id='e' contenteditable='true'>e</span>"
        "<details><summary id='f'>f</summary>body</details>"
        "<button id='g'>g</button>"
        "<a id='h' href='#x'>h</a>"
        "<input id='hidden' type='hidden'>"
        "<input id='disabled' disabled>"
        "<div id='plain' style='display:block;width:100px;height:20px'>p</div>"
        "<script>window.log = [];"
        "document.querySelectorAll('*').forEach(function (el) {"
        "  if (!el.id) return;"
        "  el.addEventListener('focus', function () { log.push('focus:' + el.id); });"
        "  el.addEventListener('blur', function () { log.push('blur:' + el.id); });"
        "});"
        "</script></body>";

    auto write_page = [&](const std::string& html) {
        const std::string path = "/tmp/litebird-tab-409-" + std::to_string(getpid()) + ".html";
        { std::ofstream file(path); file << html; }
        return path;
    };
    const std::string path = write_page(page);

    BrowserShell shell(400.0f, 300.0f, 40, 12);
    assert(shell.navigate_to(path));
    JsInterpreter& js = *shell.js_engine();
    auto text = [&](const char* source) { return js.eval(source).to_string(); };

    // ---- the ring holds every focusable element, in tree order -------------
    // Before the fix this answered `a,b,c,g,h`: `select`, `textarea`, the
    // `tabindex` div, the `contenteditable` span and the `summary` were absent.
    std::string ring;
    for (const auto& item : shell.interactive_elements()) {
        if (!ring.empty()) ring += ',';
        ring += item.element->id();
    }
    assert(ring == "a,b,c,d,e,f,g,h");

    // A hidden input is not focusable, nor is a disabled one, nor a plain div.
    assert(text("document.getElementById('hidden').matches(':focus')") == "false");
    for (const char* absent : {"hidden", "disabled", "plain"}) {
        js.eval(std::string("log = []; document.getElementById('") + absent + "').focus()");
        assert(text("log.join(',')") == "");
    }

    // ---- Tab reaches each of them, and the page hears it -------------------
    // Every id on the page answers `focus()`; Tab must reach exactly those and
    // nothing else, in the same order.
    for (const char* id : {"a", "b", "c", "d", "e", "f", "g", "h"}) {
        js.eval(std::string("log = []; document.getElementById('") + id + "').focus()");
        assert(text("document.activeElement.id") == id);
        // The blur of whatever held it before comes first, when something did.
        const std::string heard = text("log.join(',')");
        assert(heard == std::string("focus:") + id ||
               (heard.size() > 5 && heard.compare(heard.size() - 7, 7, std::string("focus:") + id) == 0 &&
                heard.compare(0, 5, "blur:") == 0));
    }

    // Nothing holds the focus before the first press. (The loop above left it
    // on the last one; `blur()` takes it back off.)
    js.eval("document.activeElement.blur()");
    assert(text("document.activeElement.tagName") == "BODY");
    for (const char* id : {"a", "b", "c", "d", "e", "f", "g", "h"}) {
        assert(shell.handle_key("Tab"));
        assert(text("document.activeElement.id") == id);
        assert(text("document.activeElement.matches(':focus')") == "true");
    }
    // And it wraps, so the eight stops are the whole ring.
    assert(shell.handle_key("Tab"));
    assert(text("document.activeElement.id") == "a");

    // ---- the blur/focus pair still reaches the page ------------------------
    // The stop that had the focus hears `blur`, the one that takes it hears
    // `focus`: Phase 228's guarantee, now across the wider ring.
    js.eval("log = []");
    assert(shell.handle_key("Tab"));    // a -> b
    assert(text("log.join(',')") == "blur:a,focus:b");

    std::remove(path.c_str());
}
