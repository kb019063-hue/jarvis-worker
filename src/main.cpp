#include "litebird/dom/Document.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/html/HtmlParser.hpp"
#include "litebird/html/TreeDump.hpp"
#include "litebird/css/CssParser.hpp"
#include "litebird/css/StyleResolver.hpp"
#include "litebird/layout/LayoutTree.hpp"
#include "litebird/render/PaintCommand.hpp"
#include "litebird/render/TerminalRenderer.hpp"
#include "litebird/render/BitmapRenderer.hpp"
#include "litebird/js/JsInterpreter.hpp"
#include "litebird/shell/BrowserShell.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

void print_banner() {
    std::cout << "\033[1;36m";
    std::cout << "=========================================================\n";
    std::cout << "   Litebird Browser Engine (Phase 1 — Native C++20)     \n";
    std::cout << "=========================================================\033[0m\n";
}

void print_help(const char* prog) {
    print_banner();
    std::cout << "Usage: " << prog << " [options] <html-file>\n\n";
    std::cout << "Options:\n";
    std::cout << "  --dump-dom             Dump parsed DOM tree\n";
    std::cout << "  --dump-dom-webplatform Dump DOM in WPT/html5lib tree-dump format\n";
    std::cout << "  --fragment-context=C   Parse input as a fragment with context element C\n";
    std::cout << "  --scripting=off        Disable the scripting flag while parsing\n";
    std::cout << "  --dump-css             Dump parsed CSS stylesheets & rules\n";
    std::cout << "  --dump-layout          Dump computed Layout Box tree\n";
    std::cout << "  --dump-paint           Dump display list paint commands\n";
    std::cout << "  --dump-bytecode        Dump compiled JS bytecode disassembly\n";
    std::cout << "  --render-terminal      Render web page to ANSI terminal (default)\n";
    std::cout << "  --render-bmp <file>    Render web page to 24-bit BMP image\n";
    std::cout << "  --run-js <code>        Execute JavaScript code directly\n";
    std::cout << "  --repl                 Start interactive JavaScript REPL\n";
    std::cout << "  --shell [file/url]     Launch interactive TUI Browser Shell\n";
    std::cout << "  --help, -h             Show this help message\n";
}

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Error: Could not open file '" << path << "'\n";
        return "";
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void run_repl() {
    print_banner();
    std::cout << "Interactive JavaScript & DOM REPL. Type 'exit' to quit.\n\n";

    auto doc = std::make_shared<litebird::Document>();
    doc->set_title("REPL Document");
    litebird::JsInterpreter interp(doc);

    std::string line;
    while (true) {
        std::cout << "\033[1;32mlitebird> \033[0m";
        if (!std::getline(std::cin, line)) break;
        if (line == "exit" || line == "quit") break;
        if (line.empty()) continue;

        auto result = interp.eval(line);
        std::cout << "\033[90m=> \033[0m" << result.to_string() << "\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    bool dump_dom = false;
    bool dump_css = false;
    bool dump_layout = false;
    bool dump_paint = false;
    bool dump_bytecode = false;
    bool render_terminal = true;
    std::string bmp_output;
    std::string js_code;
    bool repl_mode = false;
    bool shell_mode = false;
    bool dump_dom_webplatform = false;
    bool scripting = true;
    std::string fragment_context;
    std::string input_file;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "--dump-dom") {
            dump_dom = true;
        } else if (arg == "--dump-dom-webplatform") {
            dump_dom_webplatform = true;
        } else if (arg.rfind("--fragment-context=", 0) == 0) {
            fragment_context = arg.substr(std::string("--fragment-context=").size());
        } else if (arg == "--scripting=off") {
            scripting = false;
        } else if (arg == "--dump-css") {
            dump_css = true;
        } else if (arg == "--dump-layout") {
            dump_layout = true;
        } else if (arg == "--dump-paint") {
            dump_paint = true;
        } else if (arg == "--dump-bytecode") {
            dump_bytecode = true;
        } else if (arg == "--render-terminal") {
            render_terminal = true;
        } else if (arg == "--shell") {
            shell_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                input_file = argv[++i];
            }
        } else if (arg == "--render-bmp") {
            if (i + 1 < argc) {
                bmp_output = argv[++i];
            } else {
                std::cerr << "Error: --render-bmp requires output filename\n";
                return 1;
            }
        } else if (arg == "--run-js") {
            if (i + 1 < argc) {
                js_code = argv[++i];
            } else {
                std::cerr << "Error: --run-js requires code string\n";
                return 1;
            }
        } else if (arg == "--repl") {
            repl_mode = true;
        } else if (arg[0] != '-') {
            input_file = arg;
        }
    }

    if (repl_mode) {
        run_repl();
        return 0;
    }

    if (shell_mode) {
        litebird::BrowserShell shell;
        std::string start_url = input_file.empty() ? "examples/browser_portal.html" : input_file;
        shell.navigate_to(start_url);
        shell.run_interactive();
        return 0;
    }

    if (!js_code.empty()) {
        auto doc = std::make_shared<litebird::Document>();
        doc->set_title("Standalone JS Execution");
        litebird::JsInterpreter interp(doc);
        if (dump_bytecode) {
            try {
                auto fn = interp.compile_to_bytecode(js_code);
                fn->chunk.disassemble(std::cout, "CLI Script");
            } catch (const std::exception& ex) {
                // The bytecode compiler covers a subset of the language.
                std::cerr << "Cannot compile to bytecode: " << ex.what() << "\n";
                return 1;
            }
        } else {
            // Nobody is watching the clock while a one-shot script runs, so the
            // loop does not sleep until a timer is due: it jumps its clock
            // there. The script sees the time pass (Date.now() follows the
            // loop's clock) and timers still fire in due order. The budget
            // bounds a script that reschedules itself forever.
            auto& loop = litebird::EventLoop::default_loop();
            loop.set_time_skipping(true);
            interp.eval(js_code);
            loop.run_until_idle(std::chrono::seconds(60));
            // A script that ended with an uncaught exception or a syntax error
            // is a failure; say so through the exit status.
            if (litebird::js_uncaught_error_count() > 0) return 1;
        }
        return 0;
    }

    if (input_file.empty()) {
        print_help(argv[0]);
        return 1;
    }

    std::string html_content = read_file(input_file);
    if (html_content.size() == 0 && input_file != "/dev/stdin") return 1;

    // 1. HTML Parsing
    if (dump_dom_webplatform) {
        if (!fragment_context.empty()) {
            litebird::ElementNamespace ns = litebird::ElementNamespace::HTML;
            std::string tag = fragment_context;
            if (tag.rfind("svg ", 0) == 0) { ns = litebird::ElementNamespace::SVG; tag = tag.substr(4); }
            else if (tag.rfind("math ", 0) == 0) { ns = litebird::ElementNamespace::MathML; tag = tag.substr(5); }
            auto fragment = litebird::HtmlParser::parse_fragment_with_context(html_content, tag, ns, scripting);
            std::cout << litebird::dump_tree_webplatform(*fragment);
        } else {
            auto document = litebird::HtmlParser::parse(html_content, scripting);
            std::cout << litebird::dump_tree_webplatform(*document);
        }
        return 0;
    }

    auto doc = litebird::HtmlParser::parse(html_content, scripting);

    // 2. CSS Parsing & Style Resolution
    litebird::StyleResolver resolver;

    // Extract inline <style> from DOM
    for (const auto& el : doc->get_elements_by_tag_name("style")) {
        auto sheet = litebird::CssParser::parse(el->text_content());
        resolver.add_stylesheet(sheet);
    }

    // 3. JavaScript Execution for <script> tags
    litebird::JsInterpreter js_engine(doc);
    for (const auto& el : doc->get_elements_by_tag_name("script")) {
        std::string script = el->text_content();
        if (!script.empty()) {
            if (dump_bytecode) {
                std::cout << "\033[1;33m--- [Compiled Bytecode Disassembly] ---\033[0m\n";
                try {
                    auto fn = js_engine.compile_to_bytecode(script);
                    fn->chunk.disassemble(std::cout, "Inline <script>");
                } catch (const std::exception& ex) {
                    std::cout << "(not compiled: " << ex.what() << ")";
                }
                std::cout << "\n";
            }
            js_engine.eval_ast(script);
        }
    }

    // 4. Layout Box Tree Construction
    float viewport_w = 800.0f;
    float viewport_h = 600.0f;
    auto layout_root = litebird::LayoutTree::build(doc, resolver, viewport_w, viewport_h);

    // 5. Display List Generation
    auto display_list = litebird::DisplayList::build(layout_root);

    // Outputs requested by flags
    if (dump_dom) {
        std::cout << "\033[1;33m--- [DOM Tree] ---\033[0m\n";
        doc->dump(std::cout);
        std::cout << "\n";
    }

    if (dump_css) {
        std::cout << "\033[1;33m--- [CSS Stylesheets] ---\033[0m\n";
        for (const auto& el : doc->get_elements_by_tag_name("style")) {
            auto sheet = litebird::CssParser::parse(el->text_content());
            std::cout << "Rules count: " << sheet.rules.size() << "\n";
            for (const auto& r : sheet.rules) {
                std::cout << "  Selector specificity: ids=" << r.selectors[0].specificity().ids
                          << " cls=" << r.selectors[0].specificity().classes
                          << " tags=" << r.selectors[0].specificity().tags << "\n";
                for (const auto& d : r.declarations) {
                    std::cout << "    " << d.property << ": " << d.value << ";\n";
                }
            }
        }
        std::cout << "\n";
    }

    if (dump_layout) {
        std::cout << "\033[1;33m--- [Layout Box Tree] ---\033[0m\n";
        if (layout_root) {
            layout_root->dump(std::cout);
        }
        std::cout << "\n";
    }

    if (dump_paint) {
        std::cout << "\033[1;33m--- [Display List Paint Commands] ---\033[0m\n";
        std::cout << "Total commands: " << display_list.commands().size() << "\n";
        for (const auto& cmd : display_list.commands()) {
            std::visit([](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, litebird::DrawRectCommand>) {
                    std::cout << "  DrawRect: [" << arg.rect.x << ", " << arg.rect.y
                              << ", " << arg.rect.width << ", " << arg.rect.height
                              << "] Color: " << arg.color.to_hex() << "\n";
                } else if constexpr (std::is_same_v<T, litebird::DrawBorderCommand>) {
                    std::cout << "  DrawBorder: [" << arg.rect.x << ", " << arg.rect.y
                              << ", " << arg.rect.width << ", " << arg.rect.height
                              << "] Color: " << arg.color.to_hex() << "\n";
                } else if constexpr (std::is_same_v<T, litebird::DrawTextCommand>) {
                    std::cout << "  DrawText: (" << arg.position.x << ", " << arg.position.y
                              << ") \"" << arg.text << "\" Color: " << arg.color.to_hex() << "\n";
                }
            }, cmd);
        }
        std::cout << "\n";
    }

    // Render BMP if specified
    if (!bmp_output.empty()) {
        litebird::BitmapRenderer bmp(800, 600);
        bmp.clear(litebird::Color::white());
        bmp.render(display_list);
        if (bmp.save_bmp(bmp_output)) {
            std::cout << "\033[1;32mRendered bitmap saved to: " << bmp_output << "\033[0m\n";
        } else {
            std::cerr << "Failed to save bitmap to: " << bmp_output << "\n";
        }
    }

    // Terminal Render
    if (render_terminal && !dump_dom && !dump_css && !dump_layout && !dump_paint && bmp_output.empty()) {
        std::cout << "\033[1;36m┌──────────────────── [Litebird Viewport: 80x25] ────────────────────┐\033[0m\n";
        litebird::TerminalRenderer term(80, 25);
        term.render(display_list);
        std::cout << "\033[1;36m└──────────────────────────────────────────────────────────────────────┘\033[0m\n";
    }

    return 0;
}
