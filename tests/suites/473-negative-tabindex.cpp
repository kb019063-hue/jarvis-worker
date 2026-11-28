// Phase 473, finding 473: negative tabindex is programmatic focus only.
TEST(shell_skips_negative_tabindex_in_both_directions) {
    using namespace litebird;
    const std::string path = "/tmp/litebird-tab-473-" + std::to_string(getpid()) + ".html";
    const std::string page =
        "<!DOCTYPE html><body>"
        "<button id='neg-first' tabindex='-1'>n</button>"
        "<button id='a'>a</button>"
        "<button id='neg-middle' tabindex='  -2tail'>n</button>"
        "<button id='b' tabindex='0'>b</button>"
        "<button id='positive' tabindex='1'>p</button>"
        "<button id='minus-zero' tabindex='-0'>z</button>"
        "<button id='invalid' tabindex='bad'>i</button>"
        "<button id='neg-last' tabindex='-1'>n</button>"
        "<script>window.log=[]; document.querySelectorAll('button').forEach(function(e){"
        "e.addEventListener('focus',function(){log.push('focus:'+e.id)});"
        "e.addEventListener('blur',function(){log.push('blur:'+e.id)});"
        "});</script></body>";
    { std::ofstream file(path); file << page; }
    BrowserShell shell(400.0f, 300.0f, 40, 12);
    assert(shell.navigate_to(path));
    JsInterpreter& js = *shell.js_engine();
    auto text = [&](const char* source) { return js.eval(source).to_string(); };
    for (const char* id : {"positive", "a", "b", "minus-zero", "invalid", "positive"}) {
        assert(shell.handle_key("Tab"));
        assert(text("document.activeElement.id") == id);
    }
    js.eval("log=[]");
    shell.focus_prev();
    assert(text("document.activeElement.id") == "invalid");
    assert(text("log.join(',')") == "blur:positive,focus:invalid");
    shell.focus_prev();
    assert(text("document.activeElement.id") == "minus-zero");
    shell.focus_prev();
    assert(text("document.activeElement.id") == "b");
    js.eval("log=[]");
    shell.focus_prev();
    assert(text("document.activeElement.id") == "a");
    assert(text("log.join(',')") == "blur:b,focus:a");

    // A negative stop can be focused by script; navigation leaves it from its
    // position rather than restarting from the first stop.
    js.eval("document.getElementById('neg-middle').focus(); log=[]");
    assert(text("document.activeElement.id") == "neg-middle");
    assert(shell.handle_key("Tab"));
    assert(text("document.activeElement.id") == "b");
    assert(text("log.join(',')") == "blur:neg-middle,focus:b");
    js.eval("document.getElementById('neg-middle').focus(); log=[]");
    shell.focus_prev();
    assert(text("document.activeElement.id") == "a");
    assert(text("log.join(',')") == "blur:neg-middle,focus:a");

    // Changing the sign is read on the next key, without a layout rebuild.
    js.eval("document.getElementById('b').setAttribute('tabindex','-1')");
    assert(shell.handle_key("Tab"));
    assert(text("document.activeElement.id") == "minus-zero");
    js.eval("document.getElementById('b').setAttribute('tabindex','0'); document.getElementById('a').focus()");
    assert(shell.handle_key("Tab"));
    assert(text("document.activeElement.id") == "b");

    // With only negative stops the walk terminates without stealing the focus,
    // both when nothing is focused and when script focused one of them.
    { std::ofstream file(path); file << "<!DOCTYPE html><body><button id='n1' tabindex='-1'>n</button><button id='n2' tabindex='-2'>n</button></body>"; }
    BrowserShell only_negative(400.0f, 300.0f, 40, 12);
    assert(only_negative.navigate_to(path));
    JsInterpreter& js2 = *only_negative.js_engine();
    assert(only_negative.handle_key("Tab"));
    assert(js2.eval("document.activeElement.tagName").to_string() == "BODY");
    only_negative.focus_prev();
    assert(js2.eval("document.activeElement.tagName").to_string() == "BODY");
    js2.eval("document.getElementById('n2').focus()");
    assert(js2.eval("document.activeElement.id").to_string() == "n2");
    assert(only_negative.handle_key("Tab"));
    assert(js2.eval("document.activeElement.id").to_string() == "n2");
    only_negative.focus_prev();
    assert(js2.eval("document.activeElement.id").to_string() == "n2");
    std::remove(path.c_str());
}
