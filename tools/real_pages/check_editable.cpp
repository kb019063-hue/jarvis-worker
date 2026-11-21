// Focus, Selection, cancellation and Unicode. A separate offline control,
// not part of the 20-page score. No calls to application event handlers.
#include "litebird/shell/BrowserShell.hpp"
#include <iostream>
#include <stdexcept>
using namespace litebird;

int main() {
    BrowserShell shell(800,600);
    if (!shell.navigate_to(R"(data:text/html,<body><code id='edit' contenteditable>old</code><p id='outside'>keep</p><div id='rich' contenteditable><b>bold</b></div><script>
        var host=document.getElementById('edit'), denied=false, updates=0, valid=true;
        host.addEventListener('beforeinput',function(e){if(denied)e.preventDefault();});
        host.addEventListener('input',function(e){updates++;valid=valid&&e.isTrusted&&e.bubbles&&!e.cancelable;});
    </script></body>)")) return 1;
    auto eval=[&](const char* js){return shell.js_engine()->eval(js).to_string();};
    auto expect=[&](bool ok,const char* why){if(!ok) throw std::runtime_error(why);};
    auto click=[&](const char* id){auto r=shell.document()->get_element_by_id(id)->bounding_box();return shell.click_at(r.x+1,r.y+r.height/2);};
    auto el=shell.document()->get_element_by_id("edit");
    expect(click("edit") && shell.document()->focused_element()==el,"click must focus host");
    expect(shell.handle_key("SelectAll") && eval("getSelection().toString()") == "old","select original text");
    expect(shell.handle_key("😀") && el->text_content()=="😀" && eval("getSelection().anchorOffset")=="2","replace with one astral scalar");
    expect(shell.handle_key("Backspace") && el->text_content().empty(),"remove both surrogate units");
    expect(shell.handle_key("ü") && el->text_content()=="ü","insert into empty field");
    eval("denied=true");
    shell.handle_key("x");
    expect(el->text_content()=="ü" && eval("updates")=="3" && eval("valid")=="true","canceled beforeinput must not mutate or emit input");
    eval("denied=false;getSelection().selectAllChildren(document.getElementById('outside'))");
    expect(!shell.handle_key("x") && el->text_content()=="ü" && eval("getSelection().toString()")=="keep","never replace a selection in another element");
    eval("document.getElementById('rich').focus()");
    expect(!shell.handle_key("SelectAll") && !shell.handle_key("x") && eval("document.getElementById('rich').firstChild.tagName")=="B","never flatten unsupported markup");
    std::cout<<"8/8 editable input checks passed\n";
}
