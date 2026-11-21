// A thin driver for the same click/key/paint path as Litebird's interactive shell.
#include "litebird/shell/BrowserShell.hpp"
#include "litebird/dom/Selector.hpp"
#include <fstream>
#include <iostream>
#include <thread>
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

// Commands come from the frozen evaluator, not from the page. eval is for
// observations only. All activation uses BrowserShell and a real hit test.
int main(int argc,char** argv) {
    if (argc!=4) return 2;
    const std::string origin="http://snapshot.test", prefix=argv[3];
    ResourceLoader::set_origin_root_for_testing(origin,argv[1]);
    offline(); BrowserShell shell(800,600);
    auto shot=[&](const std::string& tag){auto png=shell.render_png_frame();std::ofstream out(prefix+tag+".png",std::ios::binary);out.write(reinterpret_cast<const char*>(png.data()),png.size());if(!out)throw std::runtime_error("cannot save frame");};
    std::cout<<std::unitbuf<<"stage: open\n";
    if(!shell.navigate_to(origin+"/"+argv[2]))return 2;
    shot("before");
    for(std::string line; std::getline(std::cin,line);){
        auto i=line.find('\t');std::string op=line.substr(0,i),arg=line.substr(i+1);
        if(op=="read"){
            auto got=shell.js_engine()->eval(arg).to_string();std::cout<<"READ\t"<<got<<'\n';
        }else if(op=="click"){
            SelectorList sel;if(!SelectorList::parse(arg,sel))return 2;
            bool clicked=false;
            for(auto& el:sel.select(*shell.document(),true)){
                auto r=el->bounding_box();if(r.width<=0||r.height<=0)continue;
                shell.scroll_to(0,r.y+r.height/2-300);
                float x=r.x+r.width/2-shell.scroll_x(),y=r.y+r.height/2-shell.scroll_y();
                if(x<0||y<0||x>=800||y>=600)continue;
                auto hit=shell.element_at(x,y);
                std::cout<<"HIT\t"<<arg<<" @ "<<x<<','<<y<<" -> "<<(hit?hit->tag_name()+"#"+hit->id():"(none)")<<'\n';
                for(std::shared_ptr<Node> n=hit;n;n=n->parent())if(n==el){clicked=shell.click_at(x,y);break;}
                if(clicked)break;
            }
            if(!clicked){std::cout<<"BLOCKED\tno clickable target: "<<arg<<'\n';break;}
        }else if(op=="key"||op=="type"){
            bool sent=true;
            if(op=="key")sent=shell.handle_key(arg);
            else for(char ch:arg){if(!shell.handle_key(std::string(1,ch))){sent=false;break;}}
            if(!sent){std::cout<<"BLOCKED\tkey input not delivered: "<<arg<<'\n';break;}
        }else if(op=="shot")shot(arg);else return 2;
    }
    shot("after");std::cout<<"END\n";
}
