// Phase 511, finding 511: a candidate the DOM refuses cannot stop Tab.
TEST(shell_tab_skips_unfocusable_and_inert_candidates) {
    using namespace litebird;
    const std::string path = "/tmp/litebird-tab-511-" + std::to_string(getpid()) + ".html";
    auto write_page = [&](const std::string& html) { std::ofstream file(path); file << html; };
    write_page(
        "<!DOCTYPE html><body><button id='a'>a</button><button id='b'>b</button>"
        "<button id='disabled' disabled>d</button><div id='legacy' class='btn'>c</div>"
        "<script>window.log=[]; window.clicked=0;"
        "document.getElementById('legacy').addEventListener('click',function(){clicked++});"
        "['a','b','disabled'].forEach(function(id){var e=document.getElementById(id);"
        "e.addEventListener('focus',function(){log.push('focus:'+id)});"
        "e.addEventListener('blur',function(){log.push('blur:'+id)});});</script></body>");
    BrowserShell shell(400.0f, 300.0f, 40, 12);
    assert(shell.navigate_to(path));
    JsInterpreter& js = *shell.js_engine();
    auto text = [&](const char* source) { return js.eval(source).to_string(); };
    std::shared_ptr<Element> legacy;
    for (const auto& item : shell.interactive_elements())
        if (item.element->id() == "legacy") legacy = item.element;
    assert(legacy); // The legacy list and pointer activation remain available.
    assert(shell.dispatch_click(legacy));
    assert(text("clicked") == "1");
    assert(shell.handle_key("Tab"));
    assert(text("document.activeElement.id") == "a");
    assert(shell.handle_key("Tab"));
    assert(text("document.activeElement.id") == "b");
    js.eval("log=[]");
    assert(shell.handle_key("Tab")); // skip disabled and legacy; wrap
    assert(text("document.activeElement.id") == "a");
    assert(text("log.join(',')") == "blur:b,focus:a");
    js.eval("log=[]");
    shell.focus_prev(); // reverse skip and wrap
    assert(text("document.activeElement.id") == "b");
    assert(text("log.join(',')") == "blur:a,focus:b");

    // A candidate becoming focusable or unfocusable is checked at key time,
    // even if the candidate list itself has not been rebuilt.
    js.eval("document.getElementById('disabled').removeAttribute('disabled')");
    assert(shell.handle_key("Tab"));
    assert(text("document.activeElement.id") == "disabled");
    js.eval("document.getElementById('disabled').setAttribute('disabled','');"
            "document.getElementById('b').focus();"
            "var gone=document.getElementById('a'); gone.parentNode.removeChild(gone); log=[]");
    assert(shell.handle_key("Tab")); // only b remains a valid candidate
    assert(text("document.activeElement.id") == "b");
    assert(text("log.join(',')") == "");

    // With no usable candidate, both directions terminate without taking focus.
    write_page("<!DOCTYPE html><body><button disabled>no</button><div class='btn'>no</div></body>");
    BrowserShell none(400.0f, 300.0f, 40, 12);
    assert(none.navigate_to(path));
    assert(none.handle_key("Tab"));
    assert(none.js_engine()->eval("document.activeElement.tagName").to_string() == "BODY");
    assert(none.focused_element_index() == -1);
    none.focus_prev();
    assert(none.js_engine()->eval("document.activeElement.tagName").to_string() == "BODY");
    assert(none.focused_element_index() == -1);

    // showModal makes outside candidates inert without rebuilding the shell's
    // list; Tab wraps inside the dialog instead of stalling on an outside item.
    write_page("<!DOCTYPE html><body><button id='outside'>out</button><dialog id='d'>"
               "<button id='x'>x</button><button id='y'>y</button></dialog></body>");
    BrowserShell modal(400.0f, 300.0f, 40, 12);
    assert(modal.navigate_to(path));
    JsInterpreter& jm = *modal.js_engine();
    jm.eval("document.getElementById('d').showModal(); document.getElementById('y').focus()");
    assert(jm.eval("document.activeElement.id").to_string() == "y");
    assert(modal.handle_key("Tab"));
    assert(jm.eval("document.activeElement.id").to_string() == "x");
    modal.focus_prev();
    assert(jm.eval("document.activeElement.id").to_string() == "y");
    std::remove(path.c_str());
}
