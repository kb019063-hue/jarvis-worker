#include "litebird/xml/XmlTools.hpp"
#include "litebird/xml/XmlDom.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/html/HtmlParser.hpp"
#include "litebird/html/TreeDump.hpp"
#include "litebird/css/CssParser.hpp"
#include "litebird/css/DocumentStyles.hpp"
#include "litebird/css/StyleResolver.hpp"
#include "litebird/layout/LayoutTree.hpp"
#include "litebird/render/PaintCommand.hpp"
#include "litebird/render/TerminalRenderer.hpp"
#include "litebird/render/BitmapRenderer.hpp"
#include "litebird/render/ImageDecoder.hpp"
#include "litebird/js/JsInterpreter.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Storage.hpp"
#include "litebird/net/DataUrl.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include "litebird/shell/BrowserShell.hpp"
#include "litebird/shell/WebBrowserServer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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

/// Runs the loop of a one-shot host until it is idle, or until `budget` of
/// loop time or `wall_limit_seconds` of real time are spent, and says so on
/// standard error when it was the real-time limit that ended it with
/// callbacks still due. What the page printed is then not all it would have
/// printed, the exit status does not show it, and until this line existed
/// nothing did: a web-platform-test whose load handlers took 11 seconds in a
/// sanitizer build on a busy machine ended with no result and no message.
void run_page_loop(litebird::EventLoop& loop, std::chrono::milliseconds budget, int wall_limit_seconds) {
    const auto wall_limit = std::chrono::seconds(wall_limit_seconds);
    const auto began = std::chrono::steady_clock::now();
    loop.run_until_idle(budget, 100000, wall_limit);
    if (std::chrono::steady_clock::now() - began < wall_limit || !loop.has_pending_tasks()) return;
    const auto wait = loop.time_until_next();
    if (!wait || wait->count() > 0) return;      // what is left is a timer that is not due yet
    std::cerr << "litebird: the event loop was stopped after " << wall_limit_seconds
              << " s of real time with callbacks still due (--wall-limit=SECONDS allows more)\n";
}

void print_help(const char* prog) {
    print_banner();
    std::cout << "Usage: " << prog << " [options] <html-file>\n\n";
    std::cout << "Options:\n";
    std::cout << "  --dump-dom             Dump parsed DOM tree\n";
    std::cout << "  --dump-dom-webplatform Dump DOM in WPT/html5lib tree-dump format\n";
    std::cout << "  --fragment-context=C   Parse input as a fragment with context element C\n";
    std::cout << "  --scripting=off        Disable the scripting flag while parsing\n";
    std::cout << "  --virtual-time-budget=MS  Let the page's timers run for MS of virtual time\n";
    std::cout << "                         before it is rendered (default 16: one frame)\n";
    std::cout << "  --dump-css             Dump parsed CSS stylesheets & rules\n";
    std::cout << "  --dump-layout          Dump computed Layout Box tree\n";
    std::cout << "  --dump-paint           Dump display list paint commands\n";
    std::cout << "  --dump-bytecode        Dump compiled JS bytecode disassembly\n";
    std::cout << "  --render-terminal      Render web page to ANSI terminal (default)\n";
    std::cout << "  --render-bmp <file>    Render web page to 24-bit BMP image\n";
    std::cout << "  --render-png <file>    Render web page to lossless PNG image\n";
    std::cout << "  --gui [url]            Launch interactive graphical Web Browser UI server\n";
    std::cout << "  --port=PORT            TCP port for --gui (default: 8080, bound to 0.0.0.0)\n";
    std::cout << "  --run-js <code>        Execute JavaScript code directly\n";
    std::cout << "  --run-script <file>    Load the page, run <file> in it as a deferred script,\n";
    std::cout << "                         let the page finish loading and its timers run, and\n";
    std::cout << "                         exit without rendering (the DOM scoreboard uses this)\n";
    std::cout << "  --wall-limit=SECONDS   Real time the page's callbacks may take once it has\n";
    std::cout << "                         loaded (default 10). A test runner with a timeout of\n";
    std::cout << "                         its own passes that, so that it is the one limit\n";
    std::cout << "  --url=URL              The address the page is said to come from\n";
    std::cout << "  --map-origin=ORIG=DIR  Serve requests to HTTP(S) origin ORIG from local DIR\n";
    std::cout << "  --xml-canonical <file> Read <file> as XML and print its canonical form (the\n";
    std::cout << "                         one the XML conformance suite compares). Exits 0 for\n";
    std::cout << "                         a well-formed document, 1 for one that is not, 3 for\n";
    std::cout << "                         an encoding this build cannot read\n";
    std::cout << "  --xml-events <file>    Read <file> as XML and print what the parser reports,\n";
    std::cout << "                         one line an event. The same exit statuses\n";
    std::cout << "  --repl                 Start interactive JavaScript REPL\n";
    std::cout << "  --shell [file/url]     Launch interactive TUI Browser Shell\n";
    std::cout << "  --help, -h             Show this help message\n";
}

/// False when the file cannot be opened. An empty file is a file: it is the
/// page with nothing in it, and it has a tree like any other.
bool read_file(const std::string& path, std::string& content) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Error: Could not open file '" << path << "'\n";
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    content = buffer.str();
    return true;
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
    std::string png_output;
    std::string js_code;
    bool repl_mode = false;
    bool shell_mode = false;
    bool gui_mode = false;
    uint16_t gui_port = 8080;
    bool dump_dom_webplatform = false;
    bool scripting = true;
    // How much timer time a page gets to settle before it is rendered: one
    // animation frame by default.
    int virtual_time_budget_ms = 16;
    // How much real time the page's callbacks get once it has loaded.
    int wall_limit_seconds = 10;
    std::string fragment_context;
    std::string input_file;
    std::string run_script_file;
    bool run_script = false;
    std::string page_url;
    std::string cli_content_type;
    // "canonical" or "events": read the file as XML and print that form.
    std::string xml_dump;

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
        } else if (arg.rfind("--virtual-time-budget=", 0) == 0) {
            virtual_time_budget_ms = std::max(0, std::atoi(arg.substr(std::string("--virtual-time-budget=").size()).c_str()));
        } else if (arg.rfind("--wall-limit=", 0) == 0) {
            wall_limit_seconds = std::max(1, std::atoi(arg.substr(std::string("--wall-limit=").size()).c_str()));
        } else if (arg == "--xml-canonical") {
            xml_dump = "canonical";
        } else if (arg == "--xml-events") {
            xml_dump = "events";
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
        } else if (arg == "--gui") {
            gui_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                input_file = argv[++i];
            }
        } else if (arg.rfind("--port=", 0) == 0) {
            gui_port = static_cast<uint16_t>(std::clamp(std::atoi(arg.substr(7).c_str()), 0, 65535));
        } else if (arg == "--render-png") {
            if (i + 1 < argc) {
                png_output = argv[++i];
            } else {
                std::cerr << "Error: --render-png requires output filename\n";
                return 1;
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
        } else if (arg == "--run-script") {
            if (i + 1 < argc) {
                run_script_file = argv[++i];
                run_script = true;
            } else {
                std::cerr << "Error: --run-script requires a file name\n";
                return 1;
            }
        } else if (arg.rfind("--url=", 0) == 0) {
            page_url = arg.substr(std::string("--url=").size());
        } else if (arg.rfind("--content-type=", 0) == 0) {
            cli_content_type = arg.substr(std::string("--content-type=").size());
        } else if (arg.rfind("--map-origin=", 0) == 0) {
            const std::string spec = arg.substr(std::string("--map-origin=").size());
            const auto eq = spec.find('=');
            if (eq != std::string::npos && eq > 0 && eq + 1 < spec.size()) {
                litebird::ResourceLoader::set_origin_root_for_testing(spec.substr(0, eq), spec.substr(eq + 1));
            } else {
                std::cerr << "Error: --map-origin expects ORIGIN=DIR\n";
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

    if (gui_mode) {
        litebird::Storage::local_storage().persist_to("/tmp/litebird_localstorage.db");
        litebird::WebBrowserServer server(1120.0f, 700.0f);
        if (!server.start_background(gui_port, "0.0.0.0")) {
            std::cerr << "Error: Failed to bind Litebird GUI server on 0.0.0.0:" << gui_port << "\n";
            return 1;
        }
        const uint16_t actual_port = server.bound_port();
        const std::string start_url = input_file.empty()
                                          ? ("http://127.0.0.1:" + std::to_string(actual_port) + "/demo")
                                          : input_file;
        server.navigate_to(start_url);
        std::cout << "Litebird Graphical Browser Server listening on http://0.0.0.0:" << actual_port << "\n"
                  << "Initial page: " << start_url << "\n" << std::flush;
        while (server.is_running()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return 0;
    }

    if (shell_mode) {
        // The browser a person uses keeps localStorage between sessions.
        // Every other mode is one run of one page and starts empty.
        litebird::Storage::local_storage().persist_to("/tmp/litebird_localstorage.db");
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
            run_page_loop(loop, std::chrono::seconds(60), wall_limit_seconds);
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

    std::string html_content;
    std::string nav_content_type = cli_content_type.empty() ? std::string("text/html") : cli_content_type;
    std::string effective_url;
    const bool is_remote_or_scheme =
        input_file.rfind("http://", 0) == 0 ||
        input_file.rfind("https://", 0) == 0 ||
        input_file.rfind("data:", 0) == 0 ||
        input_file.rfind("file://", 0) == 0 ||
        input_file.rfind("about:", 0) == 0;

    if (!dump_dom_webplatform && xml_dump.empty() && is_remote_or_scheme) {
        auto nav = litebird::ResourceLoader::load_navigation(input_file);
        if (!nav.success) {
            std::cerr << "Error loading '" << input_file << "': " << nav.error_message << "\n";
            return 1;
        }
        html_content = std::move(nav.body);
        nav_content_type = nav.content_type;
        effective_url = nav.url.empty() ? litebird::ResourceLoader::address_of(input_file) : nav.url;
    } else {
        if (!read_file(input_file, html_content)) return 1;
        effective_url = litebird::ResourceLoader::address_of(input_file);
    }

    // --xml-canonical, --xml-events: the file through the XML parser alone.
    // Exit status 0 for a well-formed document, 1 for one that is not (the
    // reason goes to standard error), 3 for an encoding this build cannot read.
    if (!xml_dump.empty()) {
        std::string text, encoding, out;
        litebird::xml::XmlError error;
        if (!litebird::xml::decode_xml(html_content, text, encoding, error)) {
            std::cerr << error.describe() << "\n";
            return error.unsupported_encoding ? 3 : 1;
        }
        const bool well_formed = xml_dump == "canonical" ? litebird::xml::xml_canonical_form(text, out, error)
                                                         : litebird::xml::xml_event_dump(text, out, error);
        if (!well_formed) {
            std::cerr << error.describe() << "\n";
            return 1;
        }
        std::cout << out;
        return 0;
    }

    // 1. HTML Parsing
    if (dump_dom_webplatform) {
        if (!fragment_context.empty()) {
            litebird::ElementNamespace ns = litebird::ElementNamespace::HTML;
            std::string tag = fragment_context;
            if (tag.rfind("svg ", 0) == 0) { ns = litebird::ElementNamespace::SVG; tag = tag.substr(4); }
            else if (tag.rfind("math ", 0) == 0) { ns = litebird::ElementNamespace::MathML; tag = tag.substr(5); }
            auto fragment = litebird::HtmlParser::parse_fragment_with_context(html_content, tag, ns, scripting);
            litebird::dump_tree_webplatform(std::cout, *fragment);
        } else {
            auto document = litebird::HtmlParser::parse(html_content, scripting);
            // Written as it is produced. Building the dump as one string
            // first took two copies of it: 2 GB for a page whose dump is
            // 900 MB (800,000 nodes at the nesting limit).
            litebird::dump_tree_webplatform(std::cout, *document);
        }
        return 0;
    }

    // A page is rendered once, so nobody is waiting for its timers: the event
    // loop jumps its clock to the next one instead of sleeping.
    auto& page_loop = litebird::EventLoop::default_loop();
    page_loop.set_time_skipping(true);

    // 2. The page: its tree, and its scripts run as the parser reaches them.
    // The document exists first, with the address scripts see as
    // location.href and document.URL, and the script engine attached to it.
    float viewport_w = 800.0f;
    float viewport_h = 600.0f;
    auto doc = std::make_shared<litebird::Document>();
    doc->set_url(page_url.empty() ? effective_url : page_url);
    doc->set_viewport_size(viewport_w, viewport_h);

    litebird::MimeType nav_mime;
    const bool parsed_nav_mime = litebird::MimeType::parse(nav_content_type, nav_mime);
    auto url_path_has_ext = [](std::string_view u, std::string_view ext) -> bool {
        if (const auto q = u.find_first_of("?#"); q != std::string_view::npos) u = u.substr(0, q);
        if (u.size() < ext.size()) return false;
        const std::string_view tail = u.substr(u.size() - ext.size());
        for (std::size_t i = 0; i < ext.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(tail[i])) != ext[i]) return false;
        }
        return true;
    };
    const std::string& check_addr = doc->url();
    const bool url_is_svg = input_file.ends_with(".svg") || url_path_has_ext(check_addr, ".svg");
    const bool url_is_xhtml = input_file.ends_with(".xhtml") || input_file.ends_with(".xht") ||
                              url_path_has_ext(check_addr, ".xhtml") || url_path_has_ext(check_addr, ".xht");
    const bool url_is_xml = input_file.ends_with(".xml") || url_path_has_ext(check_addr, ".xml");
    const bool is_xml_nav =
        (parsed_nav_mime && nav_mime.is_xml()) || url_is_svg || url_is_xhtml || url_is_xml;

    litebird::JsInterpreter js_engine(doc);
    auto maybe_disassemble = [&](const std::string& script) {
        if (!dump_bytecode) return;
        std::cout << "\033[1;33m--- [Compiled Bytecode Disassembly] ---\033[0m\n";
        try {
            auto fn = js_engine.compile_to_bytecode(script);
            fn->chunk.disassemble(std::cout, "Inline <script>");
        } catch (const std::exception& ex) {
            std::cout << "(not compiled: " << ex.what() << ")";
        }
        std::cout << "\n";
    };

    std::string transport_charset;
    if (parsed_nav_mime) {
        if (const std::string* cs = nav_mime.parameter("charset")) transport_charset = *cs;
    }

    if (is_xml_nav) {
        const std::string essence = (parsed_nav_mime && nav_mime.is_xml())
                                        ? nav_mime.essence()
                                        : (url_is_svg ? "image/svg+xml"
                                                      : (url_is_xhtml ? "application/xhtml+xml" : "application/xml"));
        js_engine.load_xml_page(html_content, essence, scripting, maybe_disassemble, transport_charset);
    } else {
        std::string resolved_cs;
        const std::string decoded_html =
            litebird::HtmlParser::decode_bytes(html_content, transport_charset, "UTF-8", &resolved_cs);
        doc->set_character_set(resolved_cs);
        js_engine.load_page(decoded_html, scripting, maybe_disassemble);
    }

    // --run-script: one more script, run the way a deferred script is: the
    // tree is complete and the page has not been told so yet. Then the page
    // finishes loading and its timers run to the end, on the loop's virtual
    // clock. Nothing is laid out or drawn; what the script printed is the
    // result.
    if (run_script) {
        std::string script;
        if (!read_file(run_script_file, script)) return 1;
        js_engine.eval_ast(script);
        js_engine.notify_document_loaded();
        run_page_loop(page_loop, std::chrono::seconds(60), wall_limit_seconds);
        return litebird::js_uncaught_error_count() > 0 ? 1 : 0;
    }

    // The page has loaded. Then let it settle before the one picture is taken:
    // promise callbacks, completed fetches, zero-delay timers and the first
    // animation frame have run by the time a browser first paints, so they
    // run here too. Timers further out than the budget stay pending.
    js_engine.notify_document_loaded();
    run_page_loop(page_loop, std::chrono::milliseconds(virtual_time_budget_ms), wall_limit_seconds);

    // 3. CSS Parsing & Style Resolution: all <style>, <link rel="stylesheet">,
    // and @import sheets in document tree order.
    litebird::StyleResolver resolver = litebird::DocumentStyles::build_resolver(
        *doc, &doc->stylesheet_cache(), viewport_w, viewport_h
    );

    // 4. Layout Box Tree Construction
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

    // Render BMP / PNG if specified
    if (!bmp_output.empty() || !png_output.empty()) {
        litebird::BitmapRenderer bmp(static_cast<int>(viewport_w), static_cast<int>(viewport_h));
        bmp.clear(litebird::Color::white());
        bmp.render(display_list);
        if (!bmp_output.empty()) {
            if (bmp.save_bmp(bmp_output)) {
                std::cout << "\033[1;32mRendered bitmap saved to: " << bmp_output << "\033[0m\n";
            } else {
                std::cerr << "Failed to save bitmap to: " << bmp_output << "\n";
            }
        }
        if (!png_output.empty()) {
            litebird::ImageData img;
            img.width = static_cast<int>(viewport_w);
            img.height = static_cast<int>(viewport_h);
            img.pixels.resize(static_cast<std::size_t>(img.width * img.height));
            for (int y = 0; y < img.height; ++y) {
                for (int x = 0; x < img.width; ++x) {
                    img.set_pixel(x, y, bmp.get_pixel(x, y).to_argb());
                }
            }
            if (litebird::ImageDecoder::save_png(img, png_output)) {
                std::cout << "\033[1;32mRendered PNG saved to: " << png_output << "\033[0m\n";
            } else {
                std::cerr << "Failed to save PNG to: " << png_output << "\n";
            }
        }
    }

    // Terminal Render
    if (render_terminal && !dump_dom && !dump_css && !dump_layout && !dump_paint && bmp_output.empty() && png_output.empty()) {
        std::cout << "\033[1;36m┌──────────────────── [Litebird Viewport: 80x25] ────────────────────┐\033[0m\n";
        litebird::TerminalRenderer term(80, 25);
        term.render(display_list);
        std::cout << "\033[1;36m└──────────────────────────────────────────────────────────────────────┘\033[0m\n";
    }

    return 0;
}
