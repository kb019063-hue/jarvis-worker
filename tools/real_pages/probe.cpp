// A thin driver for the same click/key/paint path as Litebird's interactive shell.
#include "litebird/shell/BrowserShell.hpp"
#include "litebird/dom/Selector.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <cerrno>

using namespace litebird;

// The frozen origin is backed by files. Never contact live analytics, CDNs, etc.
static void offline() {
    sock_filter ops[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_socket, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
    };
    sock_fprog prog{4, ops};
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) || prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog))
        throw std::runtime_error("could not disable sockets");
}

int main(int argc, char** argv) {
    if (argc != 10) return 2;
    std::string origin="http://snapshot.test", dir=argv[1], path=argv[2], out=argv[3],
        mode=argv[4], selector=argv[5], typed=argv[6], read=argv[7], expect=argv[8], target=argv[9];
    ResourceLoader::set_origin_root_for_testing(origin, dir);
    offline();
    BrowserShell shell(800,600);
    auto shot=[&](const std::string& suffix) {
        auto png=shell.render_png_frame();
        std::ofstream file(out+suffix+".png", std::ios::binary);
        file.write(reinterpret_cast<const char*>(png.data()), png.size());
        if (!file) throw std::runtime_error("cannot save screenshot");
    };
    // The native convenience query only handles simple names. Use the
    // script-facing selector engine to locate the manifest's actual target.
    // Activation still goes through the viewport's hit test.
    auto locate=[&](const std::string& css) -> std::shared_ptr<Element> {
        SelectorList list;
        if (!SelectorList::parse(css, list)) throw std::runtime_error("invalid benchmark selector: "+css);
        auto matches=list.select(*shell.document(), true);
        return matches.empty()?nullptr:matches.front();
    };
    auto click=[&](const std::string& css) {
        auto el=locate(css);
        if (!el) return false;
        auto r=el->bounding_box();
        if (r.width<=0 || r.height<=0) return false;
        shell.scroll_to(0,r.y-200);
        float x=r.x+std::min(r.width/2,10.f), y=r.y+std::min(r.height/2,10.f)-shell.scroll_y();
        if (x<0 || x>=800 || y<0 || y>=600) return false;
        return shell.click_at(x,y);
    };
    std::cerr<<"stage: opening snapshot\n";
    if (!shell.navigate_to(origin+"/"+path)) { std::cerr<<"navigation failed\n"; return 2; }
    std::cerr<<"stage: painting initial frame\n";
    shot("");
    std::cerr<<"stage: driving task\n";
    bool pass=false;
    std::string note;
    if (mode=="heading") {
        auto link=locate(selector);
        if (!link || link->bounding_box().height<=0) click("#contents summary");
        click(selector);
        auto section=shell.document()->get_element_by_id(target);
        std::shared_ptr<Element> h;
        if(section) for(const char* tag:{"h2","h3","h4"}) if((h=section->query_selector(tag))) break;
        float y=h?h->bounding_box().y-shell.scroll_y():-999;
        pass=shell.status_message()=="Scrolled to #"+target && y>=-1 && y<600;
        note=shell.status_message()+"; heading y="+std::to_string(y);
    } else if (mode=="manual") {
        click(selector);
        auto h=locate("h1");
        pass=shell.current_url()==origin+"/"+target+".html" && h && h->text_content().find(target+"(1)")!=std::string::npos;
        note=shell.current_url();
    } else if (mode=="type" && selector=="#decoded") {
        std::cout<<"BENCH not-tested: shell cannot edit contenteditable text\n"; return 0;
    } else if (mode=="type") {
        auto el=locate(selector);
        click(selector);
        if (el && shell.document()->focused_element()==el) {
            for(size_t n=el->value().size(); n>0; --n) shell.handle_key("Backspace");
            shell.type_text(typed);
        }
        auto result=locate(read);
        std::string value=result?(result->tag_name()=="textarea"?result->value():result->text_content()):"(missing)";
        pass=el && el->value()==typed && result && result->bounding_box().height>0 &&
             (read=="#valid"?value.find(expect)!=std::string::npos:value==expect);
        note="input="+(el?el->value():"(missing)")+"; output="+value;
    } else {
        // BrowserShell does not deliver page keydown or mousedown. Do not
        // replace these with script dispatch and call that a browsing pass.
        std::cout<<"BENCH not-tested: shell has no game input adapter\n"; return 0;
    }
    shot("-after");
    std::cout<<"BENCH "<<(pass?"pass: ":"not-completed: ")<<note.substr(0,140)<<'\n';
}
