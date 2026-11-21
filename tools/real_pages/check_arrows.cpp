// No game assets needed. Exercise the same key entry point as the Web GUI.
#include "litebird/shell/BrowserShell.hpp"
#include <iostream>

int main() {
    litebird::BrowserShell shell(800, 600);
    if (!shell.navigate_to(R"(data:text/html,<body id='page' style='height:2000px'><button id='button'>focus</button><script>
        document.addEventListener('keydown', function(e) {
            document.body.setAttribute('data-key', [e.target.id,e.key,e.code,e.which,e.keyCode,e.isTrusted,e.bubbles,e.cancelable].join('|'));
            if (e.key === 'ArrowUp') e.preventDefault();
        });
    </script></body>)")) return 1;
    auto doc = shell.document();
    auto body = doc->body();
    auto button = doc->get_element_by_id("button");
    if (!body || !button) return 1;
    auto check = [&](const char* key, const char* observed, float scroll) {
        shell.handle_key(key);
        const auto got = body->get_attribute("data-key");
        if (got == observed && shell.scroll_y() == scroll) return true;
        std::cerr << "After " << key << ": " << got << ", scroll=" << shell.scroll_y() << '\n';
        return false;
    };
    if (!check("ArrowDown", "page|ArrowDown|ArrowDown|40|40|true|true|true", 48) ||
        !check("ArrowUp", "page|ArrowUp|ArrowUp|38|38|true|true|true", 48)) return 1;
    button->focus();
    if (!check("ArrowRight", "button|ArrowRight|ArrowRight|39|39|true|true|true", 48) ||
        !check("ArrowLeft", "button|ArrowLeft|ArrowLeft|37|37|true|true|true", 48)) return 1;
    std::cout << "4/4 arrow input checks passed\n";
}
