// Exercise both public terminal-rendering APIs with every possible text byte.
#include "litebird/render/TerminalRenderer.hpp"
#include <iostream>
#include <string>
using namespace litebird;
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::string bytes;
    for (int i = 0; i != 256; ++i) bytes.push_back(static_cast<char>(i));
    DrawTextCommand text;
    text.position = Point(0, 0);
    text.text = bytes;
    text.color = Color::black();
    DisplayList list;
    list.add(text);
    TerminalRenderer term(256, 1, 10, 20);
    const std::string mode = argv[1];
    if (mode == "stream") term.render(list, std::cout);
    else if (mode == "string") std::cout << term.to_ansi_string(list);
    else return 2;
}
