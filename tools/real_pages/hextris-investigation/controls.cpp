// Offline reductions of the Hextris start action. Non-zero until all three
// independent requirements work. These controls are not browsing-task passes.
#include "litebird/shell/BrowserShell.hpp"
#include <iostream>
using namespace litebird;

int main() {
    int failed = 0;
    auto check = [&](bool ok, const char* label) {
        std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
        failed += !ok;
    };
    {
        BrowserShell shell(800, 600);
        if (!shell.navigate_to("data:text/html,<!doctype html><style>body{margin:0}div{position:absolute;left:40%;top:38%;width:25%;height:25%}</style><div id=start></div>")) return 2;
        auto start = shell.document()->get_element_by_id("start");
        if (!start) return 2;
        auto r = start->bounding_box();
        std::cout << "rect " << r.x << ',' << r.y << ',' << r.width << ',' << r.height << '\n';
        check(r.x==320 && r.y==228 && r.width==200 && r.height==150, "percentage rectangle in the initial containing block");
    }
    {
        BrowserShell shell(800, 600);
        if (!shell.navigate_to("data:text/html,<!doctype html><style>body{margin:0;height:600px}div{position:absolute;left:320px;top:228px;width:200px;height:150px}</style><div id=start style='z-index:10'></div><div id=under style='z-index:1'></div>")) return 2;
        auto hit = shell.element_at(420,303);
        std::cout << "hit " << (hit ? hit->id() : "(none)") << '\n';
        check(hit && hit->id()=="start", "topmost positioned sibling wins the hit");
    }
    {
        BrowserShell shell(800, 600);
        if (!shell.navigate_to("data:text/html,<!doctype html><style>body{margin:0}button{width:200px;height:150px}</style><button id=start>Start</button><script>var trace=[];for(const t of ['mousedown','mouseup','click'])document.getElementById('start').addEventListener(t,e=>trace.push([e.type,e.target.id,e.button,e.buttons,e.clientX,e.clientY,e.isTrusted].join(':')));</script>")) return 2;
        auto hit = shell.element_at(20,20);
        bool clicked = hit && hit->id()=="start" && shell.click_at(20,20);
        auto trace = shell.js_engine()->eval("trace.join('|')").to_string();
        std::cout << "events " << trace << '\n';
        check(clicked && trace=="mousedown:start:0:1:20:20:true|mouseup:start:0:0:20:20:true|click:start:0:0:20:20:true", "trusted primary press, release and click on an unobstructed control");
    }
    {
        BrowserShell shell(800, 600);
        if (!shell.navigate_to("data:text/html,<!doctype html><style>body{margin:0}button{width:80px;height:80px}</style><button>Go</button><script>var log=[]; for (const t of ['mousedown','mouseup','click']) document.addEventListener(t,e=>{log.push(t);if(t==='mousedown')e.preventDefault()});</script>")) return 2;
        bool clicked=shell.click_at(20,20);
        auto result=shell.js_engine()->eval("log.join(',') + ':' + document.activeElement.tagName").to_string();
        check(clicked && result=="mousedown,mouseup,click:BODY", "canceled mousedown skips focus, not mouseup or click");
    }
    {
        BrowserShell shell(800, 600);
        if (!shell.navigate_to("data:text/html,<!doctype html><style>body{margin:0;height:300px}div{height:100px}a,button{position:absolute;left:0;top:0;width:100px;height:100px}</style><div id=parent><button>lower</button><a href='/wrong'>upper</a></div><script>var log=[];for (const t of ['mousedown','mouseup','click']) document.addEventListener(t,e=>{log.push(t+':'+e.target.tagName);if(t==='mousedown')e.target.style.display='none'});</script>")) return 2;
        auto url=shell.current_url();
        bool clicked=shell.click_at(20,20);
        auto result=shell.js_engine()->eval("log.join(',')").to_string();
        check(clicked && shell.current_url()==url && result=="mousedown:A,mouseup:BUTTON,click:DIV", "release retargets after a press hides the link; ancestor click does not navigate");
    }
    {
        BrowserShell shell(800, 600);
        if (!shell.navigate_to("data:text/html,<!doctype html><style>body{margin:0}section{height:10px}button{position:fixed;right:10px;bottom:10px;width:60px;height:60px}</style><section><button id=pause>Pause</button></section>")) return 2;
        auto hit=shell.element_at(760,560);
        check(hit && hit->id()=="pause", "a fixed control is reachable outside its short in-flow parent");
    }
    return failed ? 1 : 0;
}
