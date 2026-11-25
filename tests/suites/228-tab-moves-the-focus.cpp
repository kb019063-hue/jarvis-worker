// Phase 228, finding 232: Tab moves the document's focus, not only the ring's
// index, and the page's listeners see it.
TEST(shell_tab_moves_the_focus_and_dispatches_its_events) {
    using namespace litebird;

    // Phase 228, finding 232. `focus_next()`/`focus_prev()` walked an index
    // over `m_interactive_elements` and set `m_dirty`, and that was all: no
    // `focus()`, so no `focus`/`blur`, no `focusin`/`focusout`, and
    // `document.activeElement` still whatever it was. Measured on holdout-02
    // H2: after clicking `#number1`, typing, and pressing Tab twice,
    // `document.activeElement` was still that input.
    //
    // A Tab press now blurs what holds the focus and focuses the stop it
    // reaches, through the bindings' own `focus_element`, so the events reach
    // the page and the elements carry `:focus`.
    const std::string page =
        "<!DOCTYPE html><body style='margin:0'>"
        "<input id='number1'><input id='number2'><button id='go'>Go</button>"
        "<script>window.log = [];"
        "['number1', 'number2', 'go'].forEach(function (id) {"
        "  var el = document.getElementById(id);"
        "  el.addEventListener('focus', function () { log.push('focus:' + id); });"
        "  el.addEventListener('blur', function () { log.push('blur:' + id); });"
        "  el.addEventListener('focusin', function () { log.push('focusin:' + id); });"
        "  el.addEventListener('focusout', function () { log.push('focusout:' + id); });"
        "});"
        "</script></body>";
    auto write_page = [&](const std::string& html) {
        const std::string path = "/tmp/litebird-tab-" + std::to_string(getpid()) + ".html";
        { std::ofstream file(path); file << html; }
        return path;
    };
    const std::string path = write_page(page);

    BrowserShell shell(400.0f, 300.0f, 40, 12);
    assert(shell.navigate_to(path));
    JsInterpreter& js = *shell.js_engine();
    auto text = [&](const char* source) { return js.eval(source).to_string(); };

    // The page has three stops, and nothing holds the focus at the start: the
    // document answers with the body, as the standard says.
    assert(shell.interactive_elements().size() == 3);
    assert(text("document.activeElement.tagName") == "BODY");

    // ---- the first Tab takes the first stop -------------------------------
    assert(shell.handle_key("Tab"));
    assert(text("document.activeElement.id") == "number1");
    assert(shell.focused_element()->id() == "number1");
    assert(text("log.join(',')") == "focus:number1,focusin:number1");
    // `:focus` is the same fact, seen from the style engine.
    assert(text("number1.matches(':focus')") == "true");
    assert(text("number2.matches(':focus')") == "false");

    // ---- the second blur's the one and focuses the next -------------------
    assert(shell.handle_key("Tab"));
    assert(text("document.activeElement.id") == "number2");
    assert(text("log.join(',')") ==
           "focus:number1,focusin:number1,blur:number1,focusout:number1,focus:number2,focusin:number2");
    assert(text("number1.matches(':focus')") == "false");

    // ---- and it wraps, after every stop has had it -----------------------
    assert(shell.handle_key("Tab"));
    assert(text("document.activeElement.id") == "go");
    assert(shell.handle_key("Tab"));
    assert(text("document.activeElement.id") == "number1");
    assert(text("log.join(',')").find("blur:go,focusout:go,focus:number1,focusin:number1") !=
           std::string::npos);

    // ---- Shift+Tab goes the other way -------------------------------------
    // `handle_key` is the key dispatch a page's listeners see, and Tab is the
    // only one of the two it carries; Shift+Tab is the terminal's own escape
    // sequence, which calls `focus_prev()` directly. So the back step is
    // asked of the method, and the events it sends are checked the same way.
    js.eval("log = []");
    shell.focus_prev();
    assert(text("document.activeElement.id") == "go");
    assert(text("log.join(',')") == "blur:number1,focusout:number1,focus:go,focusin:go");

    // ---- a script's own focus() is where the next Tab starts from ---------
    // The ring used to be the only truth, so `el.focus()` left the walk where
    // it was and Tab jumped two stops away.
    js.eval("document.getElementById('number1').focus()");
    assert(text("document.activeElement.id") == "number1");
    shell.handle_key("Tab");
    assert(text("document.activeElement.id") == "number2");
    shell.handle_key("Tab");
    assert(text("document.activeElement.id") == "go");

    // ---- a stop that leaves the document takes nothing with it -------------
    js.eval("var g = document.getElementById('go'); g.parentNode.removeChild(g);"
            "document.getElementById('number2').focus();");
    shell.relayout();
    assert(shell.interactive_elements().size() == 2);
    assert(text("document.activeElement.id") == "number2");
    // The walk stood on the stop that went; the next press starts over from
    // the document's own focus rather than from a stale index.
    shell.handle_key("Tab");
    assert(text("document.activeElement.id") == "number1");

    js.eval("var n = document.getElementById('number2'); n.parentNode.removeChild(n);"
            "document.getElementById('number1').blur();");
    shell.relayout();
    assert(shell.interactive_elements().size() == 1);
    shell.handle_key("Tab");
    assert(text("document.activeElement.id") == "number1");

    // ---- entering from the far end -----------------------------------------
    {
        BrowserShell empty(400.0f, 300.0f, 40, 12);
        assert(empty.navigate_to(write_page(
            "<!DOCTYPE html><body><div id='sink' tabindex='0' style='display:block;width:200px;height:40px'>s</div>"
            "<button id='go'>Go</button>"
            "<script>window.log = []; sink.focus();"
            "sink.addEventListener('blur', function () { log.push('blur:sink'); });"
            "go.addEventListener('focus', function () { log.push('focus:go'); });"
            "</script></body>")));
        JsInterpreter& other = *empty.js_engine();
        auto read = [&](const char* source) { return other.eval(source).to_string(); };
        // The page focused a `tabindex` div before the ring was built, and the
        // div is a stop: Phase 409 (finding 409) made the ring what
        // `is_focusable` accepts rather than `a`, `button` and `input`, so a
        // `tabindex` element is reachable by Tab as it is by `focus()`. This
        // block used to assert the opposite -- "that div is not a ring stop
        // (the scan takes `a`, `button` and `input`)" -- which was the defect
        // stated as the contract. The walk now reconciles with the document's
        // focus, so the first Tab leaves `sink` and takes `go`.
        assert(read("document.activeElement.id") == "sink");
        assert(empty.handle_key("Tab"));
        assert(read("document.activeElement.id") == "go");
        assert(read("log.join(',')") == "blur:sink,focus:go");
        // Two stops, so the back step returns to `sink`. The page listens for
        // `blur` on `sink` only and `focus` on `go` only, so the pair this
        // step dispatches -- `blur` on `go`, `focus` on `sink` -- has no
        // listener to reach, and the log is the same afterwards.
        empty.focus_prev();
        assert(read("document.activeElement.id") == "sink");
        assert(read("log.join(',')") == "blur:sink,focus:go");
    }

    std::remove(path.c_str());
}
