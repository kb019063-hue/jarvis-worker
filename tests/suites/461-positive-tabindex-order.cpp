// Phase 461, finding 461: positive tabindex values come before zero/default
// stops, ordered numerically and stably in document order.
TEST(shell_orders_positive_tabindex_stops) {
    using namespace litebird;
    const std::string page =
        "<!DOCTYPE html><body>"
        "<button id='plain'>plain</button>"
        "<button id='ten' tabindex='10'>ten</button>"
        "<button id='two-a' tabindex='2'>two a</button>"
        "<button id='zero' tabindex='0'>zero</button>"
        "<button id='one' tabindex='  +1tail'>one</button>"
        "<button id='two-b' tabindex='02'>two b</button>"
        "<button id='invalid' tabindex='wrong'>invalid</button>"
        "<button id='overflow' tabindex='2147483648'>overflow</button>"
        "<div id='legacy' class='btn'>legacy</div>"
        "<script>window.log=[]; document.querySelectorAll('button').forEach(function(e){"
        " e.addEventListener('focus',function(){log.push('focus:'+e.id)});"
        " e.addEventListener('blur',function(){log.push('blur:'+e.id)});"
        "});</script></body>";
    const std::string path = "/tmp/litebird-tab-461-" + std::to_string(getpid()) + ".html";
    { std::ofstream file(path); file << page; }
    BrowserShell shell(400.0f, 300.0f, 40, 12);
    assert(shell.navigate_to(path));
    JsInterpreter& js = *shell.js_engine();
    auto text = [&](const char* source) { return js.eval(source).to_string(); };
    auto ring = [&]() {
        std::string ids;
        for (const auto& item : shell.interactive_elements()) {
            if (!ids.empty()) ids += ',';
            ids += item.element->id();
        }
        return ids;
    };
    assert(ring() == "one,two-a,two-b,ten,plain,zero,invalid,overflow,legacy");
    for (const char* id : {"one", "two-a", "two-b", "ten", "plain", "zero", "invalid", "overflow"}) {
        assert(shell.handle_key("Tab"));
        assert(text("document.activeElement.id") == id);
    }
    // Reverse walks the same order, with the same blur/focus dispatch.
    js.eval("log=[]");
    shell.focus_prev();
    assert(text("document.activeElement.id") == "invalid");
    assert(text("log.join(',')") == "blur:overflow,focus:invalid");
    // Check a priority boundary in reverse too.
    js.eval("document.getElementById('plain').focus(); log=[]");
    shell.focus_prev();
    assert(text("document.activeElement.id") == "ten");
    assert(text("log.join(',')") == "blur:plain,focus:ten");

    // A later layout scan reads changed attributes, and keeps the actual DOM
    // focus as the authority when the element's index has moved.
    js.eval("document.getElementById('ten').setAttribute('tabindex','1')");
    shell.relayout();
    assert(ring() == "ten,one,two-a,two-b,plain,zero,invalid,overflow,legacy");
    assert(shell.handle_key("Tab"));
    assert(text("document.activeElement.id") == "one");
    std::remove(path.c_str());
}
