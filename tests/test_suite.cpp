#include "litebird/common/Color.hpp"
#include "litebird/common/Geometry.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/html/HtmlParser.hpp"
#include "litebird/html/TreeDump.hpp"
#include "litebird/css/CssParser.hpp"
#include "litebird/css/StyleResolver.hpp"
#include "litebird/layout/LayoutTree.hpp"
#include "litebird/render/PaintCommand.hpp"
#include "litebird/render/ImageDecoder.hpp"
#include <cstdint>
#include <climits>
#include "litebird/render/BitmapRenderer.hpp"
#include "litebird/js/JsInterpreter.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/Json.hpp"
#include "litebird/js/Promise.hpp"
#include "litebird/js/Storage.hpp"
#include "litebird/render/Canvas2D.hpp"
#include "litebird/render/SvgRenderer.hpp"
#include "litebird/dom/Event.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include "litebird/shell/BrowserShell.hpp"
#include "litebird/js/Worker.hpp"
#include "litebird/net/WebSocket.hpp"
#include "litebird/dom/MutationObserver.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include "litebird/dom/CustomElementRegistry.hpp"
#include "litebird/dom/IntersectionObserver.hpp"
#include "litebird/dom/ResizeObserver.hpp"
#include "litebird/dom/History.hpp"
#include "litebird/render/ImageFilter.hpp"
#include "litebird/net/AbortController.hpp"

#include <iostream>
#include <functional>
#include <cassert>
#include <string>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <unistd.h>

#define TEST(name) void test_##name()
// Counted rather than hard-coded: the summary line used to drift behind the
// real number of suites whenever a test was added.
static int g_tests_run = 0;

#define RUN_TEST(name) do { \
    ++g_tests_run; \
    std::cout << "Running test: " #name "... " << std::flush; \
    test_##name(); \
    std::cout << "\033[32mPASSED\033[0m\n"; \
} while(0)

TEST(color_parsing) {
    using namespace litebird;
    Color c1 = Color::parse("#ff0000");
    assert(c1.r == 255 && c1.g == 0 && c1.b == 0);

    Color c2 = Color::parse("#0f0");
    assert(c2.r == 0 && c2.g == 255 && c2.b == 0);

    Color c3 = Color::parse("rgb(10, 20, 30)");
    assert(c3.r == 10 && c3.g == 20 && c3.b == 30);

    Color c4 = Color::parse("blue");
    assert(c4.r == 0 && c4.g == 123 && c4.b == 255);
}

TEST(html_parser) {
    using namespace litebird;
    std::string html = R"(
        <!DOCTYPE html>
        <html>
            <head>
                <title>Test Page</title>
            </head>
            <body>
                <div id="main" class="container active">
                    <h1>Hello Litebird!</h1>
                    <p>Building a browser engine from scratch.</p>
                </div>
            </body>
        </html>
    )";

    auto doc = HtmlParser::parse(html);
    assert(doc != nullptr);
    assert(doc->title() == "Test Page");

    auto main_div = doc->get_element_by_id("main");
    assert(main_div != nullptr);
    assert(main_div->tag_name() == "div");
    assert(main_div->has_class("container"));
    assert(main_div->has_class("active"));

    auto h1 = doc->query_selector("h1");
    assert(h1 != nullptr);
    assert(h1->text_content() == "Hello Litebird!");
}

TEST(css_parser_and_specificity) {
    using namespace litebird;
    std::string css = R"(
        body { margin: 0; color: #111; }
        .card { background-color: #eee; padding: 10px; }
        #hero { color: #ff0000; }
        div.card#hero { width: 400px; }
    )";

    auto sheet = CssParser::parse(css);
    assert(sheet.rules.size() == 4);

    // Specificity test
    assert(sheet.rules[0].selectors[0].specificity() == (Specificity{0, 0, 1}));
    assert(sheet.rules[1].selectors[0].specificity() == (Specificity{0, 1, 0}));
    assert(sheet.rules[2].selectors[0].specificity() == (Specificity{1, 0, 0}));
    assert(sheet.rules[3].selectors[0].specificity() == (Specificity{1, 1, 1}));
}

TEST(style_resolver_and_cascade) {
    using namespace litebird;
    std::string html = R"(
        <div id="box" class="highlight" style="padding: 20px;">
            Content
        </div>
    )";
    std::string css = R"(
        div { color: #000000; background-color: #ffffff; }
        .highlight { color: #0000ff; }
        #box { color: #ff0000; }
    )";

    auto doc = HtmlParser::parse(html);
    auto div = doc->get_element_by_id("box");
    assert(div != nullptr);

    StyleResolver resolver;
    resolver.add_stylesheet(CssParser::parse(css));

    auto style = resolver.resolve_style(div, nullptr);
    // #box has higher specificity than .highlight and div, so color should be red
    assert(style.color == Color::parse("#ff0000"));
    // Inline style has highest priority
    assert(style.padding.top == 20.0f);
    assert(style.padding.left == 20.0f);
}

TEST(layout_box_tree) {
    using namespace litebird;
    std::string html = R"(
        <div style="width: 500px; margin: 10px; padding: 5px;">
            <p style="height: 40px;">Block Child</p>
        </div>
    )";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
    assert(root != nullptr);

    // Root is body, first child is div
    assert(!root->children.empty());
    auto div_box = root->children[0];
    assert(div_box->dimensions.content.width == 500.0f);
    assert(!div_box->children.empty());
}

TEST(javascript_engine) {
    using namespace litebird;
    auto doc = std::make_shared<Document>();
    auto div = std::make_shared<Element>("div");
    div->set_attribute("id", "target");
    div->append_child(std::make_shared<Text>("Initial text"));
    doc->append_child(div);

    JsInterpreter interp(doc);

    // Test basic arithmetic and variables
    auto r1 = interp.eval_ast("let a = 15; let b = 25; a + b;");
    assert(r1.to_number() == 40.0);

    // Test functions and closures
    auto r2 = interp.eval_ast(R"(
        function add(x, y) {
            return x + y;
        }
        add(100, 250);
    )");
    assert(r2.to_number() == 350.0);

    // Test DOM manipulation from JS
    interp.eval_ast(R"(
        let el = document.getElementById("target");
        el.textContent = "Modified by JS engine!";
    )");

    assert(div->text_content() == "Modified by JS engine!");
}

TEST(bytecode_virtual_machine) {
    using namespace litebird;

    // Test direct VM bytecode execution
    auto fn = std::make_shared<BytecodeFunction>("test_chunk");
    size_t c1 = fn->chunk.add_constant(JsValue::make_number(42));
    size_t c2 = fn->chunk.add_constant(JsValue::make_number(58));
    fn->chunk.write_opcode(OpCode::OpConstant);
    fn->chunk.write(static_cast<uint8_t>(c1));
    fn->chunk.write_opcode(OpCode::OpConstant);
    fn->chunk.write(static_cast<uint8_t>(c2));
    fn->chunk.write_opcode(OpCode::OpAdd);
    fn->chunk.write_opcode(OpCode::OpReturn);

    VM vm;
    auto res = vm.interpret(fn);
    assert(res.to_number() == 100.0);

    // Test compiler + VM on arithmetic and variables
    JsInterpreter interp;
    auto compiled_fn = interp.compile_to_bytecode("let x = 12; let y = 8; x * y;");
    assert(compiled_fn != nullptr);
    assert(!compiled_fn->chunk.code.empty());

    // Test execution through VM
    auto v_res = interp.eval_ast("let x = 12; let y = 8; x * y;");
    assert(v_res.to_number() == 96.0);
}

TEST(flexbox_layout) {
    using namespace litebird;
    std::string html = R"(
        <body>
            <div style="display: flex; flex-direction: row; width: 600px; gap: 20px;">
                <div style="width: 100px; height: 50px;">Item 1</div>
                <div style="width: 200px; height: 50px;">Item 2</div>
                <div style="flex-grow: 1; height: 50px;">Item 3</div>
            </div>
        </body>
    )";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
    assert(root != nullptr);
    assert(!root->children.empty());

    auto flex_container = root->children[0];
    assert(flex_container->box_type == BoxType::FlexNode);
    assert(flex_container->dimensions.content.width == 600.0f);
    assert(flex_container->children.size() == 3);

    auto item1 = flex_container->children[0];
    auto item2 = flex_container->children[1];
    auto item3 = flex_container->children[2];

    // Item 1 at x = 0, width = 100
    assert(item1->dimensions.content.x == 0.0f);
    assert(item1->dimensions.content.width == 100.0f);

    // Item 2 at x = 100 + 20(gap) = 120, width = 200
    assert(item2->dimensions.content.x == 120.0f);
    assert(item2->dimensions.content.width == 200.0f);

    // Item 3 at x = 120 + 200 + 20(gap) = 340
    // Remaining space = 600 - (100 + 200 + 40 gaps) = 260. flex-grow: 1 takes 260!
    assert(item3->dimensions.content.x == 340.0f);
    assert(item3->dimensions.content.width == 260.0f);
}

TEST(text_wrapping) {
    using namespace litebird;
    std::string html = R"(
        <body>
            <div style="width: 120px;">
                <p>The quick brown fox jumps over the lazy dog</p>
            </div>
        </body>
    )";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
    assert(root != nullptr);

    auto div = root->children[0];
    auto p = div->children[0];
    auto text_box = p->children[0];

    // Text box wrapped into multiple lines because 120px cannot hold all words on 1 line
    assert(text_box->box_type == BoxType::TextNode);
    assert(text_box->lines.size() >= 3);
}

TEST(anti_aliased_rasterizer_and_shadows) {
    using namespace litebird;
    std::string html = R"(
        <body>
            <div style="border-radius: 12px; box-shadow: 4px 6px 8px #000000; width: 200px; height: 100px; background-color: #ff0000;">
                Rounded Card
            </div>
        </body>
    )";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
    assert(root != nullptr);

    auto card = root->children[0];
    assert(card->style.border_radius == 12.0f);
    assert(card->style.box_shadow.has_shadow);
    assert(card->style.box_shadow.x_offset == 4.0f);
    assert(card->style.box_shadow.y_offset == 6.0f);
    assert(card->style.box_shadow.blur == 8.0f);

    auto display_list = DisplayList::build(root);
    bool found_shadow = false;
    bool found_rounded_rect = false;

    for (const auto& cmd : display_list.commands()) {
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, DrawShadowCommand>) {
                found_shadow = true;
                assert(arg.border_radius == 12.0f);
            } else if constexpr (std::is_same_v<T, DrawRectCommand>) {
                if (arg.border_radius == 12.0f) {
                    found_rounded_rect = true;
                }
            }
        }, cmd);
    }

    assert(found_shadow);
    assert(found_rounded_rect);
}

TEST(inline_formatting_context) {
    using namespace litebird;
    std::string html = R"(
        <body>
            <p style="width: 500px;">
                Hello <b>bold world</b> and <a href="#" style="color: #0066cc;">link text</a> with <span style="background-color: #ffff00; color: #ff0000;">highlight</span>.
            </p>
            <p style="width: 300px; text-align: center;">
                Centered line
            </p>
            <div>
                Inline before
                <p>Block middle</p>
                Inline after
            </div>
        </body>
    )";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
    assert(root != nullptr);
    assert(root->children.size() >= 3);

    // 1. Test mixed inline formatting context on first paragraph
    auto p1 = root->children[0];
    assert(p1->establishes_ifc());
    assert(!p1->line_boxes.empty());

    bool found_bold = false;
    bool found_link = false;
    bool found_highlight = false;

    for (const auto& line : p1->line_boxes) {
        for (const auto& frag : line.fragments) {
            if (frag.text == "bold" || frag.text == "world") {
                if (frag.style.font_weight == 700) {
                    found_bold = true;
                }
            }
            if (frag.text == "link" || frag.text == "text") {
                if (frag.style.color == Color::parse("#0066cc") && frag.style.text_decoration == "underline") {
                    found_link = true;
                }
            }
            if (frag.text == "highlight") {
                if (frag.style.background_color == Color::parse("#ffff00") && frag.style.color == Color::parse("#ff0000")) {
                    found_highlight = true;
                }
            }
        }
    }

    assert(found_bold);
    assert(found_link);
    assert(found_highlight);

    // 2. Test text-align: center
    auto p2 = root->children[1];
    assert(p2->establishes_ifc());
    assert(!p2->line_boxes.empty());
    assert(!p2->line_boxes[0].fragments.empty());
    assert(p2->line_boxes[0].fragments[0].rect.x > p2->dimensions.content.x);

    // 3. Test anonymous block creation for mixed block/inline children
    auto div = root->children[2];
    assert(div->children.size() == 3);
    assert(div->children[0]->box_type == BoxType::AnonymousBlock);
    assert(div->children[1]->box_type == BoxType::BlockNode);
    assert(div->children[2]->box_type == BoxType::AnonymousBlock);

    // 4. Test DisplayList generation for IFC
    auto display_list = DisplayList::build(root);
    bool found_underline = false;
    bool found_yellow_bg = false;

    for (const auto& cmd : display_list.commands()) {
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, DrawRectCommand>) {
                if (arg.color == Color::parse("#ffff00")) {
                    found_yellow_bg = true;
                }
                if (arg.color == Color::parse("#0066cc")) {
                    found_underline = true;
                }
            }
        }, cmd);
    }

    assert(found_yellow_bg);
    assert(found_underline);
}

TEST(garbage_collector_and_cycles) {
    using namespace litebird;
    Heap heap;

    // 1. Allocate cyclic objects a and b
    JsValue a = JsValue::make_object(heap.allocate_object());
    JsValue b = JsValue::make_object(heap.allocate_object());

    a.set_property("b", b);
    b.set_property("a", a);

    assert(heap.stats().total_cells >= 2);

    // 2. While 'a' is a root, both 'a' and 'b' must survive GC
    auto stats = heap.collect_garbage({&a});
    assert(stats.alive_cells >= 2);
    assert(stats.collected_cells == 0);

    // 3. Clear roots; cycle (a <-> b) is now unreachable and must be swept
    a = JsValue::make_undefined();
    b = JsValue::make_undefined();

    auto after_stats = heap.collect_garbage({});
    assert(after_stats.collected_cells >= 2);
    assert(after_stats.alive_cells == 0);

    // 4. Test VM built-in gc() function
    VM vm;
    vm.set_global("x", JsValue::make_object());
    auto gc_fn = vm.get_global("gc");
    assert(gc_fn.type == JsType::NativeFunction);
    auto gc_info = gc_fn.native_fn({});
    assert(gc_info.type == JsType::Object);
    assert(gc_info.get_property("total_cells").to_number() >= 1.0);
}

TEST(dom_event_target_and_bubbling) {
    using namespace litebird;

    auto doc = std::make_shared<Document>();
    auto container = std::make_shared<Element>("div");
    container->set_attribute("id", "container");

    auto button = std::make_shared<Element>("button");
    button->set_attribute("id", "btn");
    button->append_child(std::make_shared<Text>("Click Me"));

    container->append_child(button);
    doc->append_child(container);

    int button_clicks = 0;
    int container_clicks = 0;

    // Listeners
    button->add_event_listener("click", [&](const std::shared_ptr<Event>& ev) {
        assert(ev->type() == "click");
        assert(ev->target() == button);
        assert(ev->current_target() == button);
        button_clicks++;
    });

    container->add_event_listener("click", [&](const std::shared_ptr<Event>& ev) {
        assert(ev->type() == "click");
        assert(ev->target() == button);
        assert(ev->current_target() == container);
        container_clicks++;
    });

    // 1. Dispatch click on button -> should bubble up to container
    button->click();
    assert(button_clicks == 1);
    assert(container_clicks == 1);

    // 2. Test stop_propagation
    button->add_event_listener("click", [&](const std::shared_ptr<Event>& ev) {
        ev->stop_propagation();
    });

    button->click();
    assert(button_clicks == 2); // 1 from previous click + 1 from this click
    assert(container_clicks == 1); // Bubbling was stopped!

    // 3. Test JS DOM Event binding
    JsValue js_btn = JsValue::make_dom_node(button);
    auto add_listener_fn = js_btn.get_property("addEventListener");
    assert(add_listener_fn.type == JsType::NativeFunction);

    int js_event_fired = 0;
    add_listener_fn.native_fn({
        JsValue::make_string("custom"),
        JsValue::make_native_function([&](const std::vector<JsValue>& args) {
            assert(!args.empty());
            assert(args[0].get_property("type").to_string() == "custom");
            js_event_fired++;
            return JsValue::make_undefined();
        })
    });

    auto dispatch_fn = js_btn.get_property("dispatchEvent");
    assert(dispatch_fn.type == JsType::NativeFunction);
    dispatch_fn.native_fn({JsValue::make_string("custom")});
    assert(js_event_fired == 1);
}

TEST(event_loop_and_timers) {
    using namespace litebird;
    EventLoop loop;

    int timeout_count = 0;
    int interval_count = 0;

    // 1. Set a timeout for 20ms
    loop.set_timeout([&]() {
        timeout_count++;
    }, 20);

    // 2. Set an interval for 10ms
    uint32_t interval_id = loop.set_interval([&]() {
        interval_count++;
    }, 10);

    // 3. Post an immediate task
    int task_run = 0;
    loop.post_task([&]() {
        task_run = 1;
    });

    // Immediate tick: should execute task, but not 10ms/20ms timers
    auto start = std::chrono::steady_clock::now();
    size_t ran = loop.tick(start);
    (void)ran;
    assert(task_run == 1);
    assert(timeout_count == 0);
    assert(interval_count == 0);

    // Advance 15ms: interval should fire once, timeout not yet
    loop.tick(start + std::chrono::milliseconds(15));
    assert(interval_count == 1);
    assert(timeout_count == 0);

    // Advance to 25ms: interval should fire again, timeout should fire
    loop.tick(start + std::chrono::milliseconds(25));
    assert(interval_count == 2);
    assert(timeout_count == 1);

    // Cancel interval
    loop.clear_timer(interval_id);

    // Advance to 50ms: interval should NOT fire again
    loop.tick(start + std::chrono::milliseconds(50));
    assert(interval_count == 2);
    assert(timeout_count == 1);

    // 4. Test JS integration with default_loop
    auto doc = std::make_shared<Document>();
    JsInterpreter interp(doc);
    EventLoop::default_loop().clear();

    interp.eval_ast(R"(
        let triggered = 0;
        setTimeout(function() {
            triggered = 100;
        }, 10);
    )");

    assert(EventLoop::default_loop().has_pending_tasks());
    auto now = std::chrono::steady_clock::now();
    EventLoop::default_loop().tick(now);
    EventLoop::default_loop().tick(now + std::chrono::milliseconds(20));
    auto triggered_val = interp.eval_ast("triggered;");
    assert(triggered_val.to_number() == 100.0);
}

TEST(resource_loader_and_url_resolution) {
    using namespace litebird;

    // 1. Relative path resolution
    assert(ResourceLoader::resolve_url("examples/hello.html", "flexbox.html") == "examples/flexbox.html");
    assert(ResourceLoader::resolve_url("examples/sub/index.html", "../hello.html") == "examples/hello.html");
    assert(ResourceLoader::resolve_url("http://example.com/pages/index.html", "about.html") == "http://example.com/pages/about.html");
    assert(ResourceLoader::resolve_url("http://example.com/pages/index.html", "/root.html") == "http://example.com/root.html");
    assert(ResourceLoader::resolve_url("examples/hello.html", "http://domain.com/app") == "http://domain.com/app");

    // 2. MIME type detection
    assert(ResourceLoader::detect_mime_type("index.html") == "text/html");
    assert(ResourceLoader::detect_mime_type("style.css") == "text/css");
    assert(ResourceLoader::detect_mime_type("app.js") == "application/javascript");
    assert(ResourceLoader::detect_mime_type("data.json") == "application/json");

    // 3. Local file loading
    auto resp = ResourceLoader::load("examples/hello.html");
    assert(resp.success);
    assert(resp.status_code == 200);
    assert(resp.content_type == "text/html");
    assert(!resp.body.empty());

    // 4. Missing file error handling
    auto missing_resp = ResourceLoader::load("examples/does_not_exist_xyz.html");
    assert(!missing_resp.success);
    assert(missing_resp.status_code == 404);
}

TEST(browser_shell_navigation_and_hit_testing) {
    using namespace litebird;

    BrowserShell shell(800.0f, 600.0f, 80, 24);

    // 1. Initial navigation to hello.html
    bool ok = shell.navigate_to("examples/hello.html");
    assert(ok);
    assert(shell.current_url() == "examples/hello.html");
    assert(!shell.can_go_back());
    assert(!shell.can_go_forward());
    assert(shell.document() != nullptr);
    assert(shell.layout_root() != nullptr);

    // 2. Second navigation to card.html
    ok = shell.navigate_to("examples/card.html");
    assert(ok);
    assert(shell.current_url() == "examples/card.html");
    assert(shell.can_go_back());
    assert(!shell.can_go_forward());

    // 3. Back navigation
    ok = shell.back();
    assert(ok);
    assert(shell.current_url() == "examples/hello.html");
    assert(!shell.can_go_back());
    assert(shell.can_go_forward());

    // 4. Forward navigation
    ok = shell.forward();
    assert(ok);
    assert(shell.current_url() == "examples/card.html");
    assert(shell.can_go_back());
    assert(!shell.can_go_forward());

    // 5. Scrolling
    float initial_scroll = shell.scroll_y();
    shell.scroll_down(5.0f);
    assert(shell.scroll_y() >= initial_scroll);
    shell.scroll_up(5.0f);
    assert(shell.scroll_y() == 0.0f);

    // 6. Navigation to events_and_gc.html with button and click target
    ok = shell.navigate_to("examples/events_and_gc.html");
    assert(ok);
    const auto& items = shell.interactive_elements();
    assert(!items.empty());
    assert(shell.focused_element_index() == 0);

    // Cycle focus
    int idx0 = shell.focused_element_index();
    shell.focus_next();
    shell.focus_prev();
    assert(shell.focused_element_index() == idx0);

    // Activate focused element (simulates pressing Enter on button)
    bool activated = shell.activate_focused();
    assert(activated);

    // 7. Hit testing
    auto el = shell.element_at(100.0f, 50.0f);
    assert(el != nullptr);

    // 8. Terminal Frame rendering check
    std::string frame = shell.render_terminal_frame(true);
    assert(!frame.empty());
    assert(frame.find("URL:") != std::string::npos);
    assert(frame.find("Back") != std::string::npos);
    assert(frame.find("Litebird DevTools") != std::string::npos);
}

TEST(css_positioning_and_stacking_contexts) {
    using namespace litebird;

    std::string html = R"(
        <div>
            <div id="container" style="position: relative; width: 400px; height: 300px; padding: 20px;">
                <div id="rel" style="position: relative; top: 10px; left: 15px; width: 100px; height: 50px;"></div>
                <div id="abs" style="position: absolute; top: 30px; left: 40px; width: 80px; height: 60px;"></div>
                <div id="fixed" style="position: fixed; bottom: 10px; right: 20px; width: 120px; height: 40px;"></div>
            </div>
        </div>
    )";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
    assert(root != nullptr);

    auto container = doc->get_element_by_id("container");
    auto rel = doc->get_element_by_id("rel");
    auto abs = doc->get_element_by_id("abs");
    auto fix = doc->get_element_by_id("fixed");

    std::function<std::shared_ptr<LayoutBox>(const std::shared_ptr<LayoutBox>&, const std::shared_ptr<Element>&)> find_box =
        [&](const std::shared_ptr<LayoutBox>& b, const std::shared_ptr<Element>& target) -> std::shared_ptr<LayoutBox> {
        if (!b) return nullptr;
        if (b->node == target) return b;
        for (const auto& ch : b->children) {
            auto found = find_box(ch, target);
            if (found) return found;
        }
        return nullptr;
    };

    auto container_box = find_box(root, container);
    auto rel_box = find_box(root, rel);
    auto abs_box = find_box(root, abs);
    auto fix_box = find_box(root, fix);

    assert(container_box != nullptr);
    assert(rel_box != nullptr);
    assert(abs_box != nullptr);
    assert(fix_box != nullptr);

    // Relative positioning offset: container content.x = 20, rel child margin=0, pad=0, so x = 20 + 15 = 35, y = 20 + 10 = 30
    assert(rel_box->dimensions.content.x == 35.0f);
    assert(rel_box->dimensions.content.y == 30.0f);
    assert(rel_box->dimensions.content.y == 30.0f);

    // Absolute positioning: container padding box is x=0, y=0, w=440, h=340.
    // abs top: 30px, left: 40px -> x = 0 + 40 = 40, y = 0 + 30 = 30
    assert(abs_box->dimensions.content.x == 40.0f);
    assert(abs_box->dimensions.content.y == 30.0f);

    // Fixed positioning: viewport 800x600, bottom: 10, right: 20, w: 120, h: 40
    // x = 800 - 20 - 120 = 660, y = 600 - 10 - 40 = 550
    assert(fix_box->dimensions.content.x == 660.0f);
    assert(fix_box->dimensions.content.y == 550.0f);

    // Test Stacking Context paint ordering
    std::string z_html = R"(
        <div id="wrap">
            <div id="item_pos" style="position: absolute; z-index: 10; background-color: #ff0000; width: 50px; height: 50px;"></div>
            <div id="item_neg" style="position: absolute; z-index: -5; background-color: #00ff00; width: 50px; height: 50px;"></div>
            <div id="item_normal" style="background-color: #0000ff; width: 50px; height: 50px;"></div>
        </div>
    )";
    auto z_doc = HtmlParser::parse(z_html);
    auto z_root = LayoutTree::build(z_doc, resolver, 800.0f, 600.0f);
    auto dlist = DisplayList::build(z_root);

    // Negative z-index item green (#00ff00) painted before normal item blue (#0000ff), before positive z-index item red (#ff0000)
    int neg_idx = -1, norm_idx = -1, pos_idx = -1;
    for (size_t i = 0; i < dlist.commands().size(); ++i) {
        if (auto rect_cmd = std::get_if<DrawRectCommand>(&dlist.commands()[i])) {
            if (rect_cmd->color.g == 255 && rect_cmd->color.r == 0) neg_idx = static_cast<int>(i);
            if (rect_cmd->color.b == 255 && rect_cmd->color.r == 0) norm_idx = static_cast<int>(i);
            if (rect_cmd->color.r == 255 && rect_cmd->color.g == 0) pos_idx = static_cast<int>(i);
        }
    }
    assert(neg_idx != -1 && norm_idx != -1 && pos_idx != -1);
    assert(neg_idx < norm_idx && norm_idx < pos_idx);
}

TEST(box_sizing_and_constraints) {
    using namespace litebird;

    std::string html = R"(
        <div>
            <div id="content_box" style="box-sizing: content-box; width: 200px; height: 100px; padding: 15px; border-width: 5px;"></div>
            <div id="border_box" style="box-sizing: border-box; width: 200px; height: 100px; padding: 15px; border-width: 5px;"></div>
            <div id="clamped_min" style="box-sizing: border-box; width: 50px; min-width: 120px; height: 40px; min-height: 80px;"></div>
            <div id="clamped_max" style="box-sizing: border-box; width: 500px; max-width: 250px; height: 300px; max-height: 150px;"></div>
            <div id="centered" style="width: 200px; margin-left: auto; margin-right: auto; height: 50px;"></div>
        </div>
    )";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);

    std::function<std::shared_ptr<LayoutBox>(const std::shared_ptr<LayoutBox>&, const std::string&)> find_by_id =
        [&](const std::shared_ptr<LayoutBox>& b, const std::string& id) -> std::shared_ptr<LayoutBox> {
        if (!b) return nullptr;
        if (b->node && b->node->is_element()) {
            if (b->node->as_element()->id() == id) return b;
        }
        for (const auto& ch : b->children) {
            auto f = find_by_id(ch, id);
            if (f) return f;
        }
        return nullptr;
    };

    auto c_box = find_by_id(root, "content_box");
    auto b_box = find_by_id(root, "border_box");
    auto min_box = find_by_id(root, "clamped_min");
    auto max_box = find_by_id(root, "clamped_max");
    auto center_box = find_by_id(root, "centered");

    assert(c_box != nullptr && b_box != nullptr && min_box != nullptr && max_box != nullptr && center_box != nullptr);

    // Content box: content.width = 200px, border_box.width = 200 + 30 (padding) + 10 (border) = 240px
    assert(c_box->dimensions.content.width == 200.0f);
    assert(c_box->dimensions.border_box().width == 240.0f);

    // Border box: border_box.width = 200px, content.width = 200 - 30 (padding) - 10 (border) = 160px
    assert(b_box->dimensions.border_box().width == 200.0f);
    assert(b_box->dimensions.content.width == 160.0f);

    // Clamped min: min-width: 120px clamped width to 120px, min-height clamped to 80px
    assert(min_box->dimensions.content.width == 120.0f);
    assert(min_box->dimensions.content.height == 80.0f);

    // Clamped max: max-width: 250px clamped width to 250px, max-height clamped to 150px
    assert(max_box->dimensions.content.width == 250.0f);
    assert(max_box->dimensions.content.height == 150.0f);

    // Centered: the document now parses through the spec tree builder, so the
    // snippet is wrapped in the implied <html>/<body> elements and the body's
    // 8px user-agent margin applies. Containing width is therefore 800 - 16 =
    // 784px, and centering a 200px box yields (784 - 200) / 2 = 292px.
    assert(center_box->dimensions.margin.left == 292.0f);
    assert(center_box->dimensions.margin.right == 292.0f);
    assert(center_box->dimensions.content.x == 292.0f);
}

TEST(javascript_prototype_chain_and_arrays) {
    using namespace litebird;

    JsInterpreter interp;

    // 1. Prototype inheritance via __proto__ and delegation
    JsValue proto_res = interp.eval(R"(
        var animal = { legs: 4, sound: "generic" };
        var dog = { sound: "woof" };
        dog.__proto__ = animal;
        dog.legs + " " + dog.sound;
    )");
    assert(proto_res.to_string() == "4 woof");

    // 2. Object.create and Object.getPrototypeOf
    JsValue obj_res = interp.eval(R"(
        var proto = { greeting: "hello" };
        var child = Object.create(proto);
        child.name = "world";
        var p = Object.getPrototypeOf(child);
        child.greeting + " " + child.name + " " + (p == proto);
    )");
    assert(obj_res.to_string() == "hello world true");

    // 3. Array literal, indexing, length, push, pop
    JsValue arr_res = interp.eval(R"(
        var list = [10, 20, 30];
        list.push(40);
        var popped = list.pop();
        list[1] = 99;
        list.length + " " + list[0] + " " + list[1] + " " + popped;
    )");
    assert(arr_res.to_string() == "3 10 99 40");

    // 4. Array.map and Array.filter with functions
    JsValue map_res = interp.eval(R"(
        var nums = [1, 2, 3, 4, 5];
        var doubled = nums.map(function(x) { return x * 2; });
        var evens = doubled.filter(function(x) { return x > 5; });
        evens.join("-");
    )");
    assert(map_res.to_string() == "6-8-10");

    // 5. Array.isArray
    JsValue is_arr = interp.eval(R"(
        Array.isArray([1, 2]) + " " + Array.isArray({ a: 1 });
    )");
    assert(is_arr.to_string() == "true false");
}

TEST(incremental_layout_and_invalidation) {
    using namespace litebird;

    std::string html = R"(
        <div id="parent">
            <div id="child1"><span>Initial</span></div>
            <div id="child2"><p>Clean Subtree</p></div>
        </div>
    )";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);

    auto parent = doc->get_element_by_id("parent");
    auto child1 = doc->get_element_by_id("child1");
    auto child2 = doc->get_element_by_id("child2");

    assert(parent != nullptr && child1 != nullptr && child2 != nullptr);

    // Initially, layout has executed and flags cleared. The document now has
    // the implied <html>/<body> wrapper, so clearing walks the whole tree.
    std::function<void(const std::shared_ptr<Node>&)> clear_flags =
        [&](const std::shared_ptr<Node>& node) {
            if (!node) return;
            node->clear_layout_flags();
            for (const auto& child : node->children()) clear_flags(child);
        };
    clear_flags(doc);
    root->clear_layout_flags();

    assert(!child1->needs_layout());
    assert(!parent->child_needs_layout());

    // Mutate child1 attribute
    child1->set_attribute("class", "updated");

    // Verification: child1 is dirty, and child_needs_layout propagated up to parent and document!
    assert(child1->needs_layout());
    assert(parent->child_needs_layout());
    assert(doc->child_needs_layout());

    // child2 was not mutated and remains clean!
    assert(!child2->needs_layout());
    assert(!child2->child_needs_layout());

    // Now test LayoutBox invalidation
    assert(!root->needs_layout);
    root->children[0]->mark_needs_layout();
    assert(root->children[0]->needs_layout);
    assert(root->child_needs_layout);

    // Relayout clears dirty flags
    root->layout(800.0f, 600.0f);
    assert(!root->needs_layout);
    assert(!root->child_needs_layout);
}

TEST(js_async_promises_and_json) {
    using namespace litebird;

    // 1. JSON parsing and stringifying
    std::string json_input = R"({"name":"litebird","version":2.5,"active":true,"features":["dom","async","grid"],"nested":{"code":200},"empty":null})";
    JsValue parsed = Json::parse(json_input);
    assert(parsed.type == JsType::Object);
    assert(parsed.get_property("name").to_string() == "litebird");
    assert(parsed.get_property("version").to_number() == 2.5);
    assert(parsed.get_property("active").bool_val == true);
    assert(parsed.get_property("empty").type == JsType::Null);

    JsValue features = parsed.get_property("features");
    assert(features.is_array());
    assert(features.object_val->array_elements.size() == 3);
    assert(features.object_val->array_elements[0].to_string() == "dom");
    assert(features.object_val->array_elements[1].to_string() == "async");
    assert(features.object_val->array_elements[2].to_string() == "grid");

    assert(parsed.get_property("nested").get_property("code").to_number() == 200.0);

    // Serialization & round-trip
    std::string stringified = Json::stringify(parsed);
    JsValue round_tripped = Json::parse(stringified);
    assert(round_tripped.get_property("name").to_string() == "litebird");
    assert(round_tripped.get_property("features").object_val->array_elements.size() == 3);

    // 2. Native Promises & Chaining
    JsInterpreter interp;
    int step1 = 0;
    int step2 = 0;

    JsValue p = Promise::make_resolved(JsValue::make_number(10));
    JsValue p_chained = p.get_property("then").call({
        JsValue::make_native_function([&step1](const std::vector<JsValue>& args) {
            step1 = static_cast<int>(args[0].to_number()) * 2; // 20
            return JsValue::make_number(step1);
        })
    });
    p_chained.get_property("then").call({
        JsValue::make_native_function([&step2](const std::vector<JsValue>& args) {
            step2 = static_cast<int>(args[0].to_number()) + 5; // 25
            return JsValue::make_number(step2);
        })
    });

    EventLoop::default_loop().run_microtasks();
    assert(step1 == 20);
    assert(step2 == 25);

    // 3. Microtask queue drains before macrotasks (Timers)
    std::vector<std::string> exec_order;
    EventLoop::default_loop().set_timeout([&exec_order]() {
        exec_order.push_back("macrotask_timer");
    }, 0);
    EventLoop::default_loop().post_microtask([&exec_order]() {
        exec_order.push_back("microtask_1");
    });
    EventLoop::default_loop().post_microtask([&exec_order]() {
        exec_order.push_back("microtask_2");
    });

    EventLoop::default_loop().run_microtasks();
    assert(exec_order.size() == 2);
    assert(exec_order[0] == "microtask_1");
    assert(exec_order[1] == "microtask_2");

    EventLoop::default_loop().tick(std::chrono::steady_clock::now() + std::chrono::milliseconds(10));
    assert(exec_order.size() == 3);
    assert(exec_order[2] == "macrotask_timer");

    // 4. Promise.all
    JsValue p_all_1 = Promise::make_resolved(JsValue::make_string("alpha"));
    JsValue p_all_2 = Promise::make_resolved(JsValue::make_string("beta"));
    JsValue p_ctor = interp.eval("Promise");
    JsValue all_fn = p_ctor.get_property("all");
    JsValue all_res = all_fn.call({JsValue::make_array({p_all_1, p_all_2})});
    bool all_passed = false;
    all_res.get_property("then").call({
        JsValue::make_native_function([&all_passed](const std::vector<JsValue>& args) {
            if (args[0].is_array() && args[0].object_val->array_elements.size() == 2) {
                if (args[0].object_val->array_elements[0].to_string() == "alpha" &&
                    args[0].object_val->array_elements[1].to_string() == "beta") {
                    all_passed = true;
                }
            }
            return JsValue::make_undefined();
        })
    });
    EventLoop::default_loop().run_microtasks();
    assert(all_passed);

    // 5. fetch() with Data URI and .json()
    interp.eval("var _code = 0; function set_code(x) { _code = x; }");
    interp.eval(R"(
        fetch("data:application/json,{\"status\":200,\"msg\":\"ok\"}").then(function(resp) {
            return resp.json();
        }).then(function(json_data) {
            set_code(json_data.status);
        });
    )");
    EventLoop::default_loop().tick(std::chrono::steady_clock::now() + std::chrono::milliseconds(10));
    EventLoop::default_loop().run_microtasks();
    auto result_code = interp.eval("_code");
    assert(result_code.to_number() == 200.0);
}

TEST(css_flex_wrapping_and_grid_layout) {
    using namespace litebird;

    // 1. Flex Wrap Multi-Line Positioning
    std::string flex_html = "<body><div id=\"flex\" style=\"display: flex; flex-direction: row; flex-wrap: wrap; width: 300px;\"><div id=\"item1\" style=\"width: 180px; height: 50px;\"></div><div id=\"item2\" style=\"width: 150px; height: 60px;\"></div><div id=\"item3\" style=\"width: 100px; height: 40px;\"></div></div></body>";
    auto flex_doc = HtmlParser::parse(flex_html);
    StyleResolver resolver;
    auto flex_root = LayoutTree::build(flex_doc, resolver, 300.0f, 600.0f);
    assert(flex_root != nullptr);

    auto flex_box = flex_root->children[0];
    assert(flex_box->box_type == BoxType::FlexNode);
    assert(flex_box->children.size() == 3);

    auto item1 = flex_box->children[0];
    auto item2 = flex_box->children[1];
    auto item3 = flex_box->children[2];

    // item1 fits on line 1 at (0, 0)
    assert(item1->dimensions.content.x == 0.0f);
    assert(item1->dimensions.content.y == 0.0f);
    assert(item1->dimensions.content.width == 180.0f);
    assert(item1->dimensions.content.height == 50.0f);

    // item2 overflows line 1 (180 + 150 = 330 > 300), so wraps to line 2 at (0, 50)
    assert(item2->dimensions.content.x == 0.0f);
    assert(item2->dimensions.content.y == 50.0f);
    assert(item2->dimensions.content.width == 150.0f);
    assert(item2->dimensions.content.height == 60.0f);

    // item3 fits on line 2 (150 + 100 = 250 <= 300) at (150, 50)
    assert(item3->dimensions.content.x == 150.0f);
    assert(item3->dimensions.content.y == 50.0f);
    assert(item3->dimensions.content.width == 100.0f);
    assert(item3->dimensions.content.height == 40.0f);

    // Total flex container height = line1_h (50) + line2_h (60) = 110
    assert(flex_box->dimensions.content.height == 110.0f);

    // 2. CSS Grid Multi-Column and Multi-Row Track Placement
    std::string grid_html = "<body><div id=\"grid\" style=\"display: grid; grid-template-columns: 100px 1fr 2fr; gap: 20px; width: 360px;\"><div id=\"g1\" style=\"height: 40px;\"></div><div id=\"g2\" style=\"height: 40px;\"></div><div id=\"g3\" style=\"height: 40px;\"></div><div id=\"g4\" style=\"height: 30px;\"></div></div></body>";
    auto grid_doc = HtmlParser::parse(grid_html);
    auto grid_root = LayoutTree::build(grid_doc, resolver, 360.0f, 600.0f);
    assert(grid_root != nullptr);

    auto grid_box = grid_root->children[0];
    assert(grid_box->box_type == BoxType::GridNode);
    assert(grid_box->children.size() == 4);

    auto g1 = grid_box->children[0];
    auto g2 = grid_box->children[1];
    auto g3 = grid_box->children[2];
    auto g4 = grid_box->children[3];

    // Col 0: 100px, Col 1: (360 - 40 - 100)/3 = 73.33px, Col 2: 146.67px
    assert(std::abs(g1->dimensions.content.x - 0.0f) < 0.1f);
    assert(std::abs(g1->dimensions.content.width - 100.0f) < 0.1f);

    assert(std::abs(g2->dimensions.content.x - 120.0f) < 0.1f);
    assert(std::abs(g2->dimensions.content.width - (220.0f / 3.0f)) < 0.1f);

    assert(std::abs(g3->dimensions.content.x - (120.0f + 220.0f / 3.0f + 20.0f)) < 0.1f);
    assert(std::abs(g3->dimensions.content.width - (440.0f / 3.0f)) < 0.1f);

    // g4 wraps to next row!
    assert(std::abs(g4->dimensions.content.x - 0.0f) < 0.1f);
    assert(std::abs(g4->dimensions.content.y - (40.0f + 20.0f)) < 0.1f);
}

TEST(dom_3phase_events_and_form_controls) {
    using namespace litebird;

    // 1. 3-Phase Event Dispatch (Capturing -> AtTarget -> Bubbling)
    auto grandparent = std::make_shared<Element>("div");
    grandparent->set_attribute("id", "grandparent");

    auto parent = std::make_shared<Element>("div");
    parent->set_attribute("id", "parent");

    auto child = std::make_shared<Element>("button");
    child->set_attribute("id", "child");

    grandparent->append_child(parent);
    parent->append_child(child);

    std::vector<std::string> log;

    grandparent->add_event_listener("click", [&log](const std::shared_ptr<Event>& ev) {
        assert(ev->event_phase() == EventPhase::Capturing);
        log.push_back("grandparent_capture");
    }, true);

    grandparent->add_event_listener("click", [&log](const std::shared_ptr<Event>& ev) {
        assert(ev->event_phase() == EventPhase::Bubbling);
        log.push_back("grandparent_bubble");
    }, false);

    parent->add_event_listener("click", [&log](const std::shared_ptr<Event>& ev) {
        assert(ev->event_phase() == EventPhase::Capturing);
        log.push_back("parent_capture");
    }, true);

    parent->add_event_listener("click", [&log](const std::shared_ptr<Event>& ev) {
        assert(ev->event_phase() == EventPhase::Bubbling);
        log.push_back("parent_bubble");
    }, false);

    child->add_event_listener("click", [&log](const std::shared_ptr<Event>& ev) {
        assert(ev->event_phase() == EventPhase::AtTarget);
        log.push_back("child_target");
    }, false);

    auto click_event = std::make_shared<Event>("click", true, true);
    child->dispatch_event(click_event);

    assert(log.size() == 5);
    assert(log[0] == "grandparent_capture");
    assert(log[1] == "parent_capture");
    assert(log[2] == "child_target");
    assert(log[3] == "parent_bubble");
    assert(log[4] == "grandparent_bubble");

    // Test stop_propagation during capture phase
    log.clear();
    parent->add_event_listener("cancel_test", [&log](const std::shared_ptr<Event>& ev) {
        log.push_back("parent_capture_stop");
        ev->stop_propagation();
    }, true);

    child->add_event_listener("cancel_test", [&log](const std::shared_ptr<Event>&) {
        log.push_back("child_target_unreachable");
    }, false);

    auto cancel_event = std::make_shared<Event>("cancel_test", true, true);
    child->dispatch_event(cancel_event);
    assert(log.size() == 1);
    assert(log[0] == "parent_capture_stop");

    // 2. Form Controls: Element properties & JS Bridge
    auto input_el = std::make_shared<Element>("input");
    input_el->set_attribute("type", "text");
    input_el->set_attribute("value", "initial_username");

    assert(input_el->input_type() == "text");
    assert(input_el->value() == "initial_username");
    assert(!input_el->disabled());
    assert(!input_el->checked());

    input_el->set_value("updated_username");
    assert(input_el->value() == "updated_username");

    input_el->set_disabled(true);
    assert(input_el->disabled());

    input_el->set_checked(true);
    assert(input_el->checked());

    // JS DOM Node binding reflection
    JsValue js_node = JsValue::make_dom_node(input_el);
    assert(js_node.get_property("value").to_string() == "updated_username");
    assert(js_node.get_property("disabled").bool_val == true);
    assert(js_node.get_property("checked").bool_val == true);

    // Set value via JS property
    js_node.set_property("value", JsValue::make_string("js_controlled_value"));
    assert(input_el->value() == "js_controlled_value");

    // Focus / Blur events
    bool focused = false;
    bool blurred = false;
    input_el->add_event_listener("focus", [&focused](const std::shared_ptr<Event>&) { focused = true; });
    input_el->add_event_listener("blur", [&blurred](const std::shared_ptr<Event>&) { blurred = true; });

    js_node.get_property("focus").call({});
    assert(focused);

    js_node.get_property("blur").call({});
    assert(blurred);
}

TEST(image_decoding_and_rendering) {
    using namespace litebird;

    // 1. PPM P3 ASCII Decoding
    std::string ppm_p3 = "P3\n2 2\n255\n255 0 0  0 255 0\n0 0 255  255 255 255\n";
    auto img_p3 = ImageDecoder::decode_ppm(reinterpret_cast<const uint8_t*>(ppm_p3.data()), ppm_p3.size());
    assert(img_p3 != nullptr);
    assert(img_p3->is_valid());
    assert(img_p3->width == 2 && img_p3->height == 2);
    assert(img_p3->get_pixel(0, 0) == 0xFFFF0000); // Red
    assert(img_p3->get_pixel(1, 0) == 0xFF00FF00); // Green
    assert(img_p3->get_pixel(0, 1) == 0xFF0000FF); // Blue
    assert(img_p3->get_pixel(1, 1) == 0xFFFFFFFF); // White

    // 2. BMP Generation and Test Pattern
    auto pattern = ImageDecoder::create_test_pattern(16, 16, Color(255, 128, 0));
    assert(pattern != nullptr && pattern->is_valid());
    assert(pattern->width == 16 && pattern->height == 16);
    assert(pattern->get_pixel_color(0, 0).r == 255);
    assert(pattern->get_pixel_color(0, 0).g == 128);

    // 3. Base64 Data URI Decoding
    // "P3\n1 1\n255\n0 255 0\n" base64 is "UDMKMSAxCjI1NQowIDI1NSAwCg=="
    auto data_uri_img = ImageDecoder::decode_data_uri("data:image/ppm;base64,UDMKMSAxCjI1NQowIDI1NSAwCg==");
    assert(data_uri_img != nullptr);
    assert(data_uri_img->is_valid());
    assert(data_uri_img->width == 1 && data_uri_img->height == 1);
    assert(data_uri_img->get_pixel_color(0, 0).g == 255);

    // 4. End-to-End Image Pipeline: HTML -> Layout -> DisplayList -> Bitmap Rasterization
    std::string html = "<body><div style=\"width: 100px; height: 100px;\"><img id=\"test_img\" src=\"data:image/ppm;base64,UDMKMSAxCjI1NQowIDI1NSAwCg==\" style=\"width: 50px; height: 50px;\" /></div></body>";
    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 100.0f, 100.0f);
    assert(root != nullptr);

    auto img_el = doc->get_element_by_id("test_img");
    assert(img_el != nullptr);

    auto container_box = root->children[0];
    auto img_box = container_box->children[0];
    assert(img_box->box_type == BoxType::ImageNode);
    assert(img_box->image != nullptr);
    assert(img_box->dimensions.content.width == 50.0f);
    assert(img_box->dimensions.content.height == 50.0f);

    // Build DisplayList
    DisplayList dlist = DisplayList::build(root);
    bool found_cmd = false;
    for (const auto& cmd : dlist.commands()) {
        if (std::holds_alternative<DrawImageCommand>(cmd)) {
            found_cmd = true;
            const auto& img_cmd = std::get<DrawImageCommand>(cmd);
            assert(img_cmd.rect.width == 50.0f);
            assert(img_cmd.rect.height == 50.0f);
            assert(img_cmd.image != nullptr);
        }
    }
    assert(found_cmd);

    // Rasterize into BitmapRenderer
    BitmapRenderer renderer(100, 100);
    renderer.clear(Color::white());
    renderer.render(dlist);

    // The image was rendered at (0, 0) to (50, 50) with green color (0, 255, 0)
    Color pixel = renderer.get_pixel(25, 25);
    assert(pixel.r == 0 && pixel.g == 255 && pixel.b == 0);
}

void test_js_template_literals_and_arrow_functions() {
    using namespace litebird;
    auto doc = std::make_shared<Document>();
    JsInterpreter interp(doc);

    // 1. Arrow functions
    interp.eval("const square = x => x * x;");
    interp.eval("const add = (a, b) => a + b;");
    interp.eval("const getConst = () => 42;");

    JsValue res1 = interp.eval("square(9);");
    assert(res1.to_number() == 81.0);

    JsValue res2 = interp.eval("add(15, 27);");
    assert(res2.to_number() == 42.0);

    JsValue res3 = interp.eval("getConst();");
    assert(res3.to_number() == 42.0);

    // Arrow function with block body
    interp.eval("const max = (a, b) => { if (a > b) return a; else return b; };");
    JsValue res4 = interp.eval("max(10, 25);");
    assert(res4.to_number() == 25.0);

    // 2. Template literals
    interp.eval("const name = 'Litebird';");
    interp.eval("const ver = 2;");
    JsValue res5 = interp.eval("`Welcome to ${name} v${ver}! 5 * 5 = ${square(5)}`;");
    assert(res5.to_string() == "Welcome to Litebird v2! 5 * 5 = 25");

    // Template literal with no substitution
    JsValue res6 = interp.eval("`Plain template string`;");
    assert(res6.to_string() == "Plain template string");

    // Template literal with nested expression
    JsValue res7 = interp.eval("`Result: ${ (x => x + 10)(20) }`;");
    assert(res7.to_string() == "Result: 30");
}

void test_web_storage_and_cookies() {
    using namespace litebird;
    auto doc = std::make_shared<Document>();
    JsInterpreter interp(doc);

    // 1. localStorage API
    interp.eval("localStorage.clear();");
    assert(interp.eval("localStorage.length;").to_number() == 0.0);

    interp.eval("localStorage.setItem('engine', 'Litebird');");
    interp.eval("localStorage.setItem('phase', '8');");
    assert(interp.eval("localStorage.length;").to_number() == 2.0);
    assert(interp.eval("localStorage.getItem('engine');").to_string() == "Litebird");
    assert(interp.eval("localStorage.getItem('phase');").to_string() == "8");
    assert(interp.eval("localStorage.getItem('nonexistent');").type == JsType::Null);

    interp.eval("localStorage.removeItem('phase');");
    assert(interp.eval("localStorage.length;").to_number() == 1.0);
    assert(interp.eval("localStorage.getItem('phase');").type == JsType::Null);

    // 2. sessionStorage API
    interp.eval("sessionStorage.setItem('sessionKey', 'val123');");
    assert(interp.eval("sessionStorage.getItem('sessionKey');").to_string() == "val123");
    assert(interp.eval("window.sessionStorage.getItem('sessionKey');").to_string() == "val123");

    // 3. document.cookie & CookieJar
    CookieJar::default_jar().clear();
    interp.eval("document.cookie = 'user=admin; Path=/';");
    interp.eval("document.cookie = 'theme=dark; Path=/';");
    std::string cookies = interp.eval("document.cookie;").to_string();
    assert(cookies.find("user=admin") != std::string::npos);
    assert(cookies.find("theme=dark") != std::string::npos);

    std::string req_header = CookieJar::default_jar().get_cookie_header();
    assert(req_header.find("user=admin") != std::string::npos);
    assert(req_header.find("theme=dark") != std::string::npos);
}

void test_html5_canvas_2d_context() {
    using namespace litebird;
    std::string html = R"(
        <body>
            <canvas id="myCanvas" width="80" height="60"></canvas>
        </body>
    )";

    auto doc = HtmlParser::parse(html);
    JsInterpreter interp(doc);

    // Retrieve canvas and context
    interp.eval("const canvas = document.getElementById('myCanvas');");
    interp.eval("const ctx = canvas.getContext('2d');");

    // Verify context properties
    assert(interp.eval("canvas.width;").to_number() == 80.0);
    assert(interp.eval("canvas.height;").to_number() == 60.0);

    // Fill rectangle with red color #ff0000
    interp.eval("ctx.fillStyle = '#ff0000';");
    interp.eval("ctx.fillRect(10, 10, 30, 20);");

    // Fill another rectangle with green color #00ff00
    interp.eval("ctx.fillStyle = '#00ff00';");
    interp.eval("ctx.fillRect(50, 10, 20, 20);");

    // Clear a sub-portion with clearRect
    interp.eval("ctx.clearRect(15, 15, 10, 10);");

    // Check pixel data on element's canvas context
    auto canvas_el = doc->get_element_by_id("myCanvas");
    assert(canvas_el != nullptr);
    auto ctx_ptr = canvas_el->canvas_context();
    assert(ctx_ptr != nullptr);
    auto img = ctx_ptr->image_data();
    assert(img != nullptr);
    assert(img->width == 80 && img->height == 60);

    // Pixel at (12, 12) is filled with red (255, 0, 0, 255)
    Color c_red = img->get_pixel_color(12, 12);
    assert(c_red.r == 255 && c_red.g == 0 && c_red.b == 0);

    // Pixel at (16, 16) was cleared to transparent
    assert(img->get_pixel(16, 16) == 0);

    // Pixel at (55, 15) is filled with green (0, 255, 0, 255)
    Color c_green = img->get_pixel_color(55, 15);
    assert(c_green.r == 0 && c_green.g == 255 && c_green.b == 0);

    // Layout verification: canvas builds as BoxType::ImageNode with 80x60
    StyleResolver resolver;
    LayoutTree layout_tree;
    auto root_box = layout_tree.build(doc, resolver);
    root_box->layout(800.0f);

    auto canvas_box = root_box->children[0];
    assert(canvas_box->box_type == BoxType::ImageNode);
    assert(canvas_box->dimensions.content.width == 80.0f);
    assert(canvas_box->dimensions.content.height == 60.0f);
    assert(canvas_box->image == img);
}

void test_css_media_queries() {
    using namespace litebird;
    std::string css = R"(
        .responsive-box {
            color: #000000;
            font-size: 14px;
        }

        @media (max-width: 600px) {
            .responsive-box {
                color: #ff0000;
                font-size: 12px;
            }
        }

        @media (min-width: 800px) {
            .responsive-box {
                color: #0000ff;
                font-size: 18px;
            }
        }
    )";

    auto sheet = CssParser::parse(css);
    StyleResolver resolver;
    resolver.add_stylesheet(sheet);

    auto el = std::make_shared<Element>("div");
    el->set_attribute("class", "responsive-box");

    // Case 1: Small screen (mobile, 480px width) -> max-width: 600px matches
    resolver.set_viewport(480.0f, 800.0f);
    ComputedStyle style_mobile = resolver.resolve_style(el);
    assert(style_mobile.color.r == 255 && style_mobile.color.g == 0 && style_mobile.color.b == 0);
    assert(style_mobile.font_size == 12.0f);

    // Case 2: Large screen (desktop, 1024px width) -> min-width: 800px matches
    resolver.set_viewport(1024.0f, 768.0f);
    ComputedStyle style_desktop = resolver.resolve_style(el);
    assert(style_desktop.color.r == 0 && style_desktop.color.g == 0 && style_desktop.color.b == 255);
    assert(style_desktop.font_size == 18.0f);

    // Case 3: Medium screen (tablet, 700px width) -> neither media query matches, default applies
    resolver.set_viewport(700.0f, 900.0f);
    ComputedStyle style_tablet = resolver.resolve_style(el);
    assert(style_tablet.color.r == 0 && style_tablet.color.g == 0 && style_tablet.color.b == 0);
    assert(style_tablet.font_size == 14.0f);
}

TEST(css_pseudo_classes) {
    using namespace litebird;
    std::string html = R"(
        <ul id="list">
            <li class="item" id="first">Item 1</li>
            <li class="item active" id="second">Item 2</li>
            <li class="item">Item 3</li>
            <li class="item" id="last">Item 4</li>
        </ul>
        <input type="checkbox" id="chk" checked>
        <button id="btn" disabled>Disabled Btn</button>
    )";

    auto doc = HtmlParser::parse(html);
    auto list = doc->get_element_by_id("list");
    auto first = doc->get_element_by_id("first");
    auto second = doc->get_element_by_id("second");
    auto last = doc->get_element_by_id("last");
    auto chk = doc->get_element_by_id("chk");
    auto btn = doc->get_element_by_id("btn");

    std::string css = R"(
        li:first-child { color: red; }
        li:last-child { color: blue; }
        li:nth-child(even) { background-color: #222222; }
        li:nth-child(2n+1) { background-color: #111111; }
        li:not(.active) { font-size: 12px; }
        input:checked { border-color: green; }
        button:disabled { opacity: 0.5; }
        button:hover { color: yellow; }
    )";

    auto sheet = CssParser::parse(css);
    StyleResolver resolver;
    resolver.add_stylesheet(sheet);

    // Verify :first-child
    auto style_first = resolver.resolve_style(first);
    assert(style_first.color == Color::parse("red"));

    // Verify :last-child
    auto style_last = resolver.resolve_style(last);
    assert(style_last.color == Color::parse("blue"));

    // Verify :nth-child(even) and :nth-child(2n+1)
    // Item 1 is index 1 (odd) -> background #111111
    assert(style_first.background_color == Color::parse("#111111"));
    // Item 4 is index 4 (even) -> background #222222
    assert(style_last.background_color == Color::parse("#222222"));

    // Verify :not(.active)
    assert(style_first.font_size == 12.0f);
    auto style_item2 = resolver.resolve_style(second);
    assert(style_item2.font_size == 16.0f); // Default font-size, not 12px because it has .active!

    // Verify :checked
    auto style_chk = resolver.resolve_style(chk);
    assert(style_chk.border_color == Color::parse("green"));

    // Verify :disabled
    auto style_btn = resolver.resolve_style(btn);
    assert(style_btn.opacity == 0.5f);

    // Verify :hover dynamic state
    assert(style_btn.color != Color::parse("yellow"));
    btn->set_hovered(true);
    auto style_btn_hover = resolver.resolve_style(btn);
    assert(style_btn_hover.color == Color::parse("yellow"));
}

TEST(svg_vector_rendering) {
    using namespace litebird;
    std::string html = R"(
        <body>
            <svg width="80" height="60" viewBox="0 0 80 60">
                <rect x="5" y="5" width="30" height="20" fill="#ff0000" stroke="#000000" stroke-width="1" />
                <circle cx="55" cy="20" r="12" fill="#00ff00" />
                <line x1="10" y1="40" x2="70" y2="40" stroke="#0000ff" stroke-width="2" />
                <polygon points="40,35 50,55 30,55" fill="#ffff00" />
                <path d="M 60 45 L 75 45 L 75 55 Z" fill="#ff00ff" />
            </svg>
        </body>
    )";

    auto doc = HtmlParser::parse(html);
    auto svgs = doc->get_elements_by_tag_name("svg");
    assert(!svgs.empty());
    auto svg_el = svgs[0];
    assert(svg_el != nullptr && svg_el->tag_name() == "svg");

    auto img = SvgRenderer::render(svg_el, 80.0f, 60.0f);
    assert(img != nullptr);
    assert(img->width == 80 && img->height == 60);

    // Center of rect (10, 10) should be red (#ff0000)
    Color c_rect = img->get_pixel_color(10, 10);
    assert(c_rect.r == 255 && c_rect.g == 0 && c_rect.b == 0);

    // Center of circle (55, 20) should be green (#00ff00)
    Color c_circle = img->get_pixel_color(55, 20);
    assert(c_circle.r == 0 && c_circle.g == 255 && c_circle.b == 0);

    // On line (20, 40) should be blue (#0000ff)
    Color c_line = img->get_pixel_color(20, 40);
    assert(c_line.r == 0 && c_line.g == 0 && c_line.b == 255);

    // Verify layout integration as BoxType::ImageNode
    StyleResolver resolver;
    LayoutTree layout_tree;
    auto root_box = layout_tree.build(doc, resolver);
    root_box->layout(800.0f);

    std::shared_ptr<LayoutBox> svg_box = nullptr;
    for (const auto& ch : root_box->children) {
        if (ch->box_type == BoxType::ImageNode) {
            svg_box = ch;
            break;
        }
    }
    assert(svg_box != nullptr);
    assert(svg_box->dimensions.content.width == 80.0f);
    assert(svg_box->dimensions.content.height == 60.0f);
    assert(svg_box->image != nullptr);
}

TEST(es6_map_set_and_destructuring) {
    using namespace litebird;
    JsInterpreter js;

    // 1. Map API
    std::string map_script = R"(
        const m = new Map([["apple", 5], ["banana", 10]]);
        m.set("orange", 15);
        const appleCount = m.get("apple");
        const hasBanana = m.has("banana");
        const initSize = m.size;
        m.delete("banana");
        const postSize = m.size;
        const hasDeleted = m.has("banana");
    )";
    js.eval_ast(map_script);
    assert(js.global_env()->get("appleCount").to_number() == 5);
    assert(js.global_env()->get("hasBanana").bool_val == true);
    assert(js.global_env()->get("initSize").to_number() == 3);
    assert(js.global_env()->get("postSize").to_number() == 2);
    assert(js.global_env()->get("hasDeleted").bool_val == false);

    // 2. Set API
    std::string set_script = R"(
        const s = new Set(["alpha", "beta"]);
        s.add("gamma");
        s.add("alpha"); // duplicate ignored
        const setSize = s.size;
        const hasAlpha = s.has("alpha");
        s.delete("beta");
        const setSizeAfter = s.size;
        const hasBeta = s.has("beta");
    )";
    js.eval_ast(set_script);
    assert(js.global_env()->get("setSize").to_number() == 3);
    assert(js.global_env()->get("hasAlpha").bool_val == true);
    assert(js.global_env()->get("setSizeAfter").to_number() == 2);
    assert(js.global_env()->get("hasBeta").bool_val == false);

    // 3. Default Function Parameters
    std::string default_script = R"(
        const multiply = (x, y = 10) => x * y;
        const r1 = multiply(5);
        const r2 = multiply(5, 3);
        const formatInfo = (eng = "Litebird", ver = "Next", extraCount = 0) => eng + ver;
        const r3 = formatInfo();
    )";
    js.eval_ast(default_script);
    assert(js.global_env()->get("r1").to_number() == 50);
    assert(js.global_env()->get("r2").to_number() == 15);
    assert(js.global_env()->get("r3").to_string() == "LitebirdNext");

    // 4. Destructuring Declarations
    std::string destruct_script = R"(
        const [firstNum, secondNum] = [100, 200];
        const { username, role } = { username: "agent", role: "admin" };
    )";
    js.eval_ast(destruct_script);
    assert(js.global_env()->get("firstNum").to_number() == 100);
    assert(js.global_env()->get("secondNum").to_number() == 200);
    assert(js.global_env()->get("username").to_string() == "agent");
    assert(js.global_env()->get("role").to_string() == "admin");
}

TEST(css_transitions_and_animations) {
    using namespace litebird;
    std::string css = R"(
        @keyframes slide_fade {
            0% {
                opacity: 0.0;
                width: 100px;
                color: #000000;
            }
            100% {
                opacity: 1.0;
                width: 300px;
                color: #ffffff;
            }
        }

        .animated-box {
            animation-name: slide_fade;
            animation-duration: 2s;
            animation-timing-function: linear;
            transition: opacity 0.5s ease;
        }
    )";

    auto sheet = CssParser::parse(css);
    assert(sheet.keyframes.size() == 1);
    assert(sheet.keyframes[0].name == "slide_fade");
    assert(sheet.keyframes[0].keyframes.size() == 2);
    assert(sheet.keyframes[0].keyframes[0].offset == 0.0f);
    assert(sheet.keyframes[0].keyframes[1].offset == 1.0f);

    StyleResolver resolver;
    resolver.add_stylesheet(sheet);

    auto kfr = resolver.find_keyframes("slide_fade");
    assert(kfr != nullptr);
    assert(kfr->name == "slide_fade");

    ComputedStyle base_style;
    base_style.animation_name = "slide_fade";
    base_style.animation_duration = 2.0f;
    base_style.animation_timing_function = "linear";

    // At t = 0.0s (start)
    ComputedStyle style_0 = base_style;
    resolver.apply_animation(style_0, 0.0f);
    assert(style_0.opacity == 0.0f);
    assert(style_0.width == 100.0f);

    // At t = 1.0s (midpoint of 2.0s linear animation) -> 50% interpolation
    ComputedStyle style_mid = base_style;
    resolver.apply_animation(style_mid, 1.0f);
    assert(std::abs(style_mid.opacity - 0.5f) < 0.01f);
    assert(std::abs(style_mid.width - 200.0f) < 1.0f);

    // At t = 2.0s (end)
    ComputedStyle style_end = base_style;
    resolver.apply_animation(style_end, 2.0f);
    assert(style_end.opacity == 1.0f);
    assert(style_end.width == 300.0f);

    // Direct interpolation helper test
    ComputedStyle from_style;
    from_style.opacity = 0.2f;
    from_style.color = Color(0, 0, 0);
    ComputedStyle to_style;
    to_style.opacity = 0.8f;
    to_style.color = Color(200, 100, 50);
    ComputedStyle target;
    StyleResolver::interpolate_styles(target, from_style, to_style, 0.5f);
    assert(std::abs(target.opacity - 0.5f) < 0.001f);
    assert(target.color.r == 100 && target.color.g == 50 && target.color.b == 25);

    // requestAnimationFrame test
    JsInterpreter js;
    std::string raf_script = R"(
        let called = false;
        requestAnimationFrame(() => {
            called = true;
        });
    )";
    js.eval_ast(raf_script);
    EventLoop::default_loop().tick(std::chrono::steady_clock::now() + std::chrono::milliseconds(50));
    assert(js.global_env()->get("called").bool_val == true);
}

TEST(css_2d_transforms) {
    using namespace litebird;

    // 1. Matrix operations
    TransformMatrix2D t = TransformMatrix2D::translation(20.0f, 30.0f);
    Point p = t.map_point(10.0f, 5.0f);
    assert(p.x == 30.0f && p.y == 35.0f);

    TransformMatrix2D s = TransformMatrix2D::scale(2.0f, 3.0f);
    Point ps = s.map_point(10.0f, 5.0f);
    assert(ps.x == 20.0f && ps.y == 15.0f);

    TransformMatrix2D inv = t.inverse();
    Point orig = inv.map_point(p);
    assert(std::abs(orig.x - 10.0f) < 0.001f && std::abs(orig.y - 5.0f) < 0.001f);

    // 2. CSS Transform parsing and cascade
    std::string html = R"(
        <html>
        <head>
            <style>
                .box {
                    transform: translate(15px, 25px) scale(2.0);
                }
            </style>
        </head>
        <body>
            <div id="target" class="box" style="width: 50px; height: 50px; background-color: #ff0000;"></div>
        </body>
        </html>
    )";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    for (const auto& el : doc->get_elements_by_tag_name("style")) {
        resolver.add_stylesheet(CssParser::parse(el->text_content()));
    }

    auto target = doc->get_element_by_id("target");
    assert(target != nullptr);
    auto style = resolver.resolve_style(target);
    assert(style.has_transform == true);
    assert(!style.transform_matrix.is_identity());

    // 3. Layout & DisplayList contains PushTransform / PopTransform
    auto root_box = LayoutTree::build(doc, resolver, 400.0f, 300.0f);
    assert(root_box != nullptr);
    DisplayList dl = DisplayList::build(root_box);

    bool found_push = false;
    bool found_pop = false;
    for (const auto& cmd : dl.commands()) {
        if (std::holds_alternative<PushTransformCommand>(cmd)) found_push = true;
        if (std::holds_alternative<PopTransformCommand>(cmd)) found_pop = true;
    }
    assert(found_push == true);
    assert(found_pop == true);
}

TEST(canvas_gradients_and_curves) {
    using namespace litebird;
    CanvasRenderingContext2D ctx(100, 100);

    // 1. Linear Gradient
    auto lin_grad = ctx.create_linear_gradient(0.0f, 0.0f, 100.0f, 0.0f);
    lin_grad->add_color_stop(0.0f, Color(255, 0, 0));   // Red
    lin_grad->add_color_stop(1.0f, Color(0, 0, 255));   // Blue

    Color c_start = lin_grad->sample_at(0.0f, 50.0f);
    assert(c_start.r == 255 && c_start.b == 0);

    Color c_end = lin_grad->sample_at(100.0f, 50.0f);
    assert(c_end.r == 0 && c_end.b == 255);

    Color c_mid = lin_grad->sample_at(50.0f, 50.0f);
    assert(c_mid.r > 100 && c_mid.b > 100);

    // 2. Fill rect with gradient
    ctx.fill_gradient = lin_grad;
    ctx.fill_rect(0.0f, 0.0f, 100.0f, 100.0f);
    Color px_left = ctx.image_data()->get_pixel_color(5, 50);
    Color px_right = ctx.image_data()->get_pixel_color(95, 50);
    assert(px_left.r > px_left.b);
    assert(px_right.b > px_right.r);

    // 3. Radial Gradient
    auto rad_grad = ctx.create_radial_gradient(50.0f, 50.0f, 0.0f, 50.0f, 50.0f, 50.0f);
    rad_grad->add_color_stop(0.0f, Color(0, 255, 0));   // Green center
    rad_grad->add_color_stop(1.0f, Color(0, 0, 0));     // Black edge
    Color rad_center = rad_grad->sample_at(50.0f, 50.0f);
    Color rad_edge = rad_grad->sample_at(99.0f, 50.0f);
    assert(rad_center.g == 255);
    assert(rad_edge.g < 50);

    // 4. Bézier curves in path
    ctx.begin_path();
    ctx.move_to(10.0f, 10.0f);
    ctx.quadratic_curve_to(50.0f, 90.0f, 90.0f, 10.0f);
    ctx.close_path();

    ctx.begin_path();
    ctx.move_to(0.0f, 0.0f);
    ctx.bezier_curve_to(20.0f, 80.0f, 80.0f, 80.0f, 100.0f, 0.0f);
    ctx.close_path();
}

TEST(es6_classes_and_inheritance) {
    using namespace litebird;
    JsInterpreter js;

    std::string class_script = R"(
        class Animal {
            constructor(name) {
                this.name = name;
            }
            speak() {
                return this.name + " makes a noise";
            }
            static species() {
                return "Fauna";
            }
        }

        class Dog extends Animal {
            constructor(name, breed) {
                super(name);
                this.breed = breed;
            }
            speak() {
                return this.name + " barks";
            }
            parentSpeak() {
                return super.speak();
            }
        }

        const a = new Animal("Generic");
        const a_name = a.name;
        const a_sound = a.speak();
        const a_species = Animal.species();

        const d = new Dog("Rover", "Retriever");
        const d_name = d.name;
        const d_breed = d.breed;
        const d_sound = d.speak();
        const d_parent_sound = d.parentSpeak();

        const isDogInstance = (d instanceof Dog);
        const isAnimalInstance = (d instanceof Animal);
        const isAnimalDog = (a instanceof Dog);
    )";

    js.eval_ast(class_script);
    assert(js.global_env()->get("a_name").to_string() == "Generic");
    assert(js.global_env()->get("a_sound").to_string() == "Generic makes a noise");
    assert(js.global_env()->get("a_species").to_string() == "Fauna");

    assert(js.global_env()->get("d_name").to_string() == "Rover");
    assert(js.global_env()->get("d_breed").to_string() == "Retriever");
    assert(js.global_env()->get("d_sound").to_string() == "Rover barks");
    assert(js.global_env()->get("d_parent_sound").to_string() == "Rover makes a noise");

    assert(js.global_env()->get("isDogInstance").bool_val == true);
    assert(js.global_env()->get("isAnimalInstance").bool_val == true);
    assert(js.global_env()->get("isAnimalDog").bool_val == false);
}

TEST(async_await_language_sugar) {
    using namespace litebird;
    JsInterpreter js;

    std::string async_script = R"(
        async function fetchValue(x) {
            return x * 3;
        }

        const multiplyAsync = async (a, b) => {
            return a * b;
        };

        async function compute() {
            const a = await fetchValue(7);
            const b = await fetchValue(5);
            const m = await multiplyAsync(a, 2);
            return m + b;
        }

        let result = 0;
        compute().then((val) => {
            result = val;
        });
    )";

    js.eval_ast(async_script);
    EventLoop::default_loop().tick();
    assert(js.global_env()->get("result").to_number() == 57);
}

TEST(css_variables_and_cascade) {
    using namespace litebird;

    std::string html = R"(
        <html>
        <head>
            <style>
                :root {
                    --main-bg: #0f172a;
                    --primary-color: #38bdf8;
                    --base-pad: 14px;
                }
                .container {
                    background-color: var(--main-bg);
                    padding: var(--base-pad);
                    --local-color: #34d399;
                }
                .child {
                    color: var(--local-color);
                    margin: var(--unknown-margin, 20px);
                    padding-left: var(--missing1, var(--missing2, 8px));
                }
            </style>
        </head>
        <body>
            <div id="cont" class="container">
                <div id="item" class="child">Content</div>
            </div>
        </body>
        </html>
    )";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    for (const auto& el : doc->get_elements_by_tag_name("style")) {
        resolver.add_stylesheet(CssParser::parse(el->text_content()));
    }

    auto cont = doc->get_element_by_id("cont");
    assert(cont != nullptr);
    auto cont_style = resolver.resolve_style(cont);
    assert(cont_style.background_color == Color::parse("#0f172a"));
    assert(cont_style.padding == EdgeSizes(14.0f));
    assert(cont_style.custom_properties.find("--local-color") != cont_style.custom_properties.end());
    assert(cont_style.custom_properties.find("--main-bg") != cont_style.custom_properties.end());

    auto item = doc->get_element_by_id("item");
    assert(item != nullptr);
    auto item_style = resolver.resolve_style(item, &cont_style);
    assert(item_style.color == Color::parse("#34d399"));
    assert(item_style.margin == EdgeSizes(20.0f));
    assert(item_style.padding.left == 8.0f);
}

TEST(css_calc_expressions) {
    using namespace litebird;

    StyleResolver resolver;
    float val1 = resolver.evaluate_calc_expression("100px - 20px");
    assert(val1 == 80.0f);

    float val2 = resolver.evaluate_calc_expression("2 * (50px + 10px)");
    assert(val2 == 120.0f);

    float val3 = resolver.evaluate_calc_expression("200px / 4 + 15px");
    assert(val3 == 65.0f);

    float val4 = resolver.evaluate_calc_expression("50% - 30px", 400.0f);
    assert(val4 == 170.0f);

    std::string html = R"(
        <html>
        <head>
            <style>
                :root {
                    --item-size: 40px;
                    --margin-base: 6px;
                }
                .box {
                    width: calc(var(--item-size) * 3 + 10px);
                    height: calc(200px / 2);
                    margin: calc(var(--margin-base) + 4px);
                }
            </style>
        </head>
        <body>
            <div id="target" class="box"></div>
        </body>
        </html>
    )";

    auto doc = HtmlParser::parse(html);
    for (const auto& el : doc->get_elements_by_tag_name("style")) {
        resolver.add_stylesheet(CssParser::parse(el->text_content()));
    }

    auto target = doc->get_element_by_id("target");
    assert(target != nullptr);
    auto style = resolver.resolve_style(target);
    assert(style.width == 130.0f);
    assert(style.height == 100.0f);
    assert(style.margin == EdgeSizes(10.0f));
}

TEST(fetch_api_and_headers) {
    using namespace litebird;
    JsInterpreter js;

    std::string script = R"(
        const h = new Headers({ "content-type": "application/json", "X-Custom": "test-val" });
        const hasCT = h.has("Content-Type");
        const ctVal = h.get("Content-Type");
        h.set("Accept", "text/html");
        h.append("X-Custom", "extra");
        const acceptVal = h.get("accept");
        const customVal = h.get("x-custom");

        const resp = new Response('{"status":"ok","code":200}', {
            status: 200,
            statusText: "OK",
            headers: { "Content-Type": "application/json" }
        });

        const isOk = resp.ok;
        const resStatus = resp.status;
        const resHeader = resp.headers.get("content-type");

        let fetchedJsonVal = "";
        fetch("data:application/json;base64,eyJoZWxsbyI6IndvcmxkIn0=").then((res) => {
            return res.json();
        }).then((data) => {
            fetchedJsonVal = data.hello;
        });
    )";

    js.eval_ast(script);
    EventLoop::default_loop().tick();

    assert(js.global_env()->get("hasCT").bool_val == true);
    assert(js.global_env()->get("ctVal").to_string() == "application/json");
    assert(js.global_env()->get("acceptVal").to_string() == "text/html");
    assert(js.global_env()->get("customVal").to_string() == "test-val, extra");

    assert(js.global_env()->get("isOk").bool_val == true);
    assert(js.global_env()->get("resStatus").to_number() == 200);
    assert(js.global_env()->get("resHeader").to_string() == "application/json");

    assert(js.global_env()->get("fetchedJsonVal").to_string() == "world");
}

TEST(url_and_url_search_params) {
    using namespace litebird;
    JsInterpreter js;

    std::string script = R"(
        const sp = new URLSearchParams("?topic=browser&sort=asc&page=1");
        const topic = sp.get("topic");
        const hasSort = sp.has("sort");
        sp.set("page", "2");
        sp.append("filter", "engine");
        sp.delete("sort");
        const spString = sp.toString();

        const u = new URL("https://api.example.com:8080/v1/search?q=litebird#results");
        const uProtocol = u.protocol;
        const uHostname = u.hostname;
        const uPort = u.port;
        const uHost = u.host;
        const uPathname = u.pathname;
        const uSearch = u.search;
        const uHash = u.hash;
        const uOrigin = u.origin;
        const uQueryParam = u.searchParams.get("q");

        u.searchParams.set("q", "browser-engine");
        u.searchParams.append("sort", "desc");
        const uNewSearch = u.search;
        const uNewHref = u.href;

        const relUrl = new URL("/docs/guide", "https://example.org/home/index.html");
        const relHref = relUrl.href;
    )";

    js.eval_ast(script);

    assert(js.global_env()->get("topic").to_string() == "browser");
    assert(js.global_env()->get("hasSort").bool_val == true);
    assert(js.global_env()->get("spString").to_string().find("page=2") != std::string::npos);
    assert(js.global_env()->get("spString").to_string().find("filter=engine") != std::string::npos);
    assert(js.global_env()->get("spString").to_string().find("sort") == std::string::npos);

    assert(js.global_env()->get("uProtocol").to_string() == "https:");
    assert(js.global_env()->get("uHostname").to_string() == "api.example.com");
    assert(js.global_env()->get("uPort").to_string() == "8080");
    assert(js.global_env()->get("uHost").to_string() == "api.example.com:8080");
    assert(js.global_env()->get("uPathname").to_string() == "/v1/search");
    assert(js.global_env()->get("uSearch").to_string() == "?q=litebird");
    assert(js.global_env()->get("uHash").to_string() == "#results");
    assert(js.global_env()->get("uOrigin").to_string() == "https://api.example.com:8080");
    assert(js.global_env()->get("uQueryParam").to_string() == "litebird");

    assert(js.global_env()->get("uNewSearch").to_string().find("q=browser-engine") != std::string::npos);
    assert(js.global_env()->get("uNewHref").to_string().find("api.example.com:8080/v1/search?") != std::string::npos);

    assert(js.global_env()->get("relHref").to_string() == "https://example.org/docs/guide");
}

TEST(web_workers_and_concurrent_execution) {
    using namespace litebird;

    JsInterpreter js(nullptr);

    std::string script = R"(
        var workerResponse = 0;
        var workerSender = "";
        var workerError = "";

        const workerCode = "data:text/javascript,self.onmessage = function(e) { postMessage({ doubled: e.data.val * 2, sender: 'worker-thread' }); };";
        const w = new Worker(workerCode);

        w.onmessage = function(e) {
            workerResponse = e.data.doubled;
            workerSender = e.data.sender;
        };

        w.onerror = function(err) {
            workerError = err.message;
        };

        w.postMessage({ val: 21 });
    )";

    js.eval(script);

    auto start = std::chrono::steady_clock::now();
    while (js.global_env()->get("workerResponse").to_number() == 0.0) {
        EventLoop::default_loop().tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(2000)) {
            break;
        }
    }

    assert(js.global_env()->get("workerResponse").to_number() == 42.0);
    assert(js.global_env()->get("workerSender").to_string() == "worker-thread");
    assert(js.global_env()->get("workerError").to_string().empty());

    js.eval("w.terminate();");
}

TEST(css_flexbox_and_grid_modern_refinements) {
    using namespace litebird;

    // 1. Flexbox row layout with gap: 15px
    {
        std::string html = R"(
            <body>
            <div id="flex-row" style="display: flex; flex-direction: row; gap: 15px; width: 400px;">
                <div id="i1" style="width: 100px; height: 30px;">Item 1</div>
                <div id="i2" style="width: 100px; height: 30px;">Item 2</div>
                <div id="i3" style="width: 100px; height: 30px;">Item 3</div>
            </div>
            </body>
        )";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver;
        auto root_box = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
        assert(root_box != nullptr);

        auto flex_box = root_box->children[0];
        assert(flex_box->box_type == BoxType::FlexNode);
        assert(flex_box->children.size() == 3);

        float x0 = flex_box->children[0]->dimensions.content.x;
        float x1 = flex_box->children[1]->dimensions.content.x;
        float x2 = flex_box->children[2]->dimensions.content.x;

        assert(x1 - x0 == 115.0f);
        assert(x2 - x1 == 115.0f);
    }

    // 2. Flexbox column layout with row-gap: 20px
    {
        std::string html = R"(
            <body>
            <div id="flex-col" style="display: flex; flex-direction: column; row-gap: 20px; width: 200px;">
                <div id="c1" style="width: 100px; height: 40px;">Col 1</div>
                <div id="c2" style="width: 100px; height: 40px;">Col 2</div>
            </div>
            </body>
        )";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver;
        auto root_box = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
        assert(root_box != nullptr);

        auto flex_box = root_box->children[0];
        assert(flex_box->children.size() == 2);
        float y0 = flex_box->children[0]->dimensions.content.y;
        float y1 = flex_box->children[1]->dimensions.content.y;

        assert(y1 - y0 == 60.0f);
        assert(flex_box->dimensions.content.height == 100.0f);
    }

    // 3. CSS Grid with minmax(100px, 1fr) minmax(200px, 2fr)
    {
        std::string html = R"(
            <body>
            <div id="grid-minmax" style="display: grid; grid-template-columns: minmax(100px, 1fr) minmax(200px, 2fr); gap: 0px; width: 600px;">
                <div id="g1">Grid 1</div>
                <div id="g2">Grid 2</div>
            </div>
            </body>
        )";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver;
        auto root_box = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
        assert(root_box != nullptr);

        auto grid_box = root_box->children[0];
        assert(grid_box->box_type == BoxType::GridNode);
        assert(grid_box->children.size() == 2);

        float w1 = grid_box->children[0]->dimensions.content.width;
        float w2 = grid_box->children[1]->dimensions.content.width;

        assert(w1 == 200.0f);
        assert(w2 == 400.0f);
    }

    // 4. CSS Grid with repeat(auto-fit, minmax(150px, 1fr))
    {
        std::string html = R"(
            <body>
            <div id="grid-autofit" style="display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 10px; width: 470px;">
                <div id="af1">Item 1</div>
                <div id="af2">Item 2</div>
                <div id="af3">Item 3</div>
            </div>
            </body>
        )";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver;
        auto root_box = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
        assert(root_box != nullptr);

        auto grid_box = root_box->children[0];
        assert(grid_box->box_type == BoxType::GridNode);
        assert(grid_box->children.size() == 3);
        float w1 = grid_box->children[0]->dimensions.content.width;
        assert(w1 == 150.0f);
    }
}

TEST(websocket_rfc6455_client_and_handshake) {
    using namespace litebird;

    // 1. Verify RFC 6455 Accept Key derivation
    std::string test_client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    std::string accept_key = WebSocketObject::calculate_accept_key(test_client_key);
    assert(accept_key == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    // 2. Start loopback WebSocket test server on 127.0.0.1
    std::atomic<bool> server_got_msg{false};
    std::string received_from_client;

    auto loopback = WebSocketObject::start_loopback_server([&](int client_fd) {
        std::string req;
        char ch;
        while (::recv(client_fd, &ch, 1, 0) > 0) {
            req.push_back(ch);
            if (req.size() >= 4 && req.substr(req.size() - 4) == "\r\n\r\n") break;
        }

        std::string key_header = "Sec-WebSocket-Key: ";
        size_t kpos = req.find(key_header);
        std::string client_k;
        if (kpos != std::string::npos) {
            size_t endpos = req.find("\r\n", kpos);
            client_k = req.substr(kpos + key_header.size(), endpos - (kpos + key_header.size()));
        }

        std::string s_accept = WebSocketObject::calculate_accept_key(client_k);
        std::ostringstream resp;
        resp << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << s_accept << "\r\n\r\n";
        std::string resp_str = resp.str();
        ::send(client_fd, resp_str.data(), resp_str.size(), 0);

        uint8_t hdr[2];
        ::recv(client_fd, hdr, 2, 0);
        uint8_t plen = hdr[1] & 0x7F;
        uint8_t mask[4];
        ::recv(client_fd, mask, 4, 0);
        std::vector<uint8_t> payload(plen);
        ::recv(client_fd, payload.data(), plen, 0);
        for (size_t i = 0; i < plen; ++i) {
            payload[i] ^= mask[i % 4];
        }
        received_from_client = std::string(payload.begin(), payload.end());
        server_got_msg = true;

        std::string reply = "pong-from-server";
        std::vector<uint8_t> reply_frame;
        reply_frame.push_back(0x81); // FIN + Text
        reply_frame.push_back(static_cast<uint8_t>(reply.size())); // Unmasked
        reply_frame.insert(reply_frame.end(), reply.begin(), reply.end());
        ::send(client_fd, reply_frame.data(), reply_frame.size(), 0);

        std::vector<uint8_t> close_frame = {0x88, 0x02, 0x03, 0xE8}; // 1000 OK
        ::send(client_fd, close_frame.data(), close_frame.size(), 0);
        ::close(client_fd);
    });

    // 3. Connect client using JavaScript
    JsInterpreter js(nullptr);

    std::string test_js = R"(
        var wsOpened = false;
        var wsReply = "";
        var wsClosed = false;

        const ws = new WebSocket("ws://127.0.0.1:)" + std::to_string(loopback->port) + R"(/chat");

        ws.onopen = function() {
            wsOpened = true;
            ws.send("ping-from-client");
        };

        ws.onmessage = function(e) {
            wsReply = e.data;
        };

        ws.onclose = function(e) {
            wsClosed = true;
        };
    )";

    js.eval(test_js);

    auto start_t = std::chrono::steady_clock::now();
    while (!js.global_env()->get("wsClosed").is_truthy()) {
        EventLoop::default_loop().tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (std::chrono::steady_clock::now() - start_t > std::chrono::milliseconds(2000)) {
            break;
        }
    }

    assert(js.global_env()->get("wsOpened").is_truthy() == true);
    assert(server_got_msg == true);
    assert(received_from_client == "ping-from-client");
    assert(js.global_env()->get("wsReply").to_string() == "pong-from-server");
    assert(js.global_env()->get("wsClosed").is_truthy() == true);
}

TEST(mutation_observer_api) {
    using namespace litebird;
    auto doc = std::make_shared<Document>();
    auto container = std::make_shared<Element>("div");
    container->set_attribute("id", "container");
    doc->append_child(container);

    JsInterpreter js(doc);

    std::string script = R"(
        var recordsReceived = 0;
        var lastType = "";
        var lastOldValue = "";
        var lastAddedCount = 0;

        var target = document.getElementById("container");
        var observer = new MutationObserver(function(mutations, obs) {
            recordsReceived = recordsReceived + mutations.length;
            var last = mutations[mutations.length - 1];
            lastType = last.type;
            if (last.oldValue) {
                lastOldValue = last.oldValue;
            }
            if (last.addedNodes) {
                lastAddedCount = last.addedNodes.length;
            }
        });

        observer.observe(target, {
            childList: true,
            attributes: true,
            attributeOldValue: true,
            subtree: true
        });

        // 1. Child mutation
        var p = document.createElement("p");
        p.setAttribute("class", "msg");
        target.appendChild(p);

        // 2. Attribute mutation
        target.setAttribute("title", "initial-title");
    )";

    js.eval_ast(script);

    // Microtasks run asynchronously
    EventLoop::default_loop().run_microtasks();

    assert(js.global_env()->get("recordsReceived").to_number() >= 2.0);
    assert(js.global_env()->get("lastType").to_string() == "attributes");

    // 3. Attribute change with oldValue tracking
    js.eval_ast(R"(
        target.setAttribute("title", "updated-title");
    )");
    EventLoop::default_loop().run_microtasks();
    assert(js.global_env()->get("lastOldValue").to_string() == "initial-title");

    // 4. takeRecords drains queued records immediately
    js.eval_ast(R"(
        target.setAttribute("data-test", "val1");
        var drained = observer.takeRecords();
        var drainedCount = drained.length;
        var drainedType = drained[0].type;
    )");
    assert(js.global_env()->get("drainedCount").to_number() == 1.0);
    assert(js.global_env()->get("drainedType").to_string() == "attributes");

    // After draining, microtask callback receives nothing
    double count_before = js.global_env()->get("recordsReceived").to_number();
    EventLoop::default_loop().run_microtasks();
    assert(js.global_env()->get("recordsReceived").to_number() == count_before);

    // 5. Disconnect stops observing
    js.eval_ast(R"(
        observer.disconnect();
        target.setAttribute("title", "after-disconnect");
    )");
    EventLoop::default_loop().run_microtasks();
    assert(js.global_env()->get("recordsReceived").to_number() == count_before);
}

TEST(node_connected_state_tracks_attach_detach_and_shadow_hosts) {
    using namespace litebird;

    auto doc = std::make_shared<Document>();
    auto host = std::make_shared<Element>("x-host");
    auto shadow = host->attach_shadow("open");
    auto shadow_child = std::make_shared<Element>("span");
    shadow->append_child(shadow_child);
    auto light_child = std::make_shared<Element>("div");
    auto light_grandchild = std::make_shared<Element>("span");
    light_child->append_child(light_grandchild);
    host->append_child(light_child);

    assert(!host->is_connected());
    assert(!light_grandchild->is_connected());
    assert(!shadow->is_connected());
    assert(!shadow_child->is_connected());

    doc->append_child(host);
    assert(host->is_connected());
    assert(light_grandchild->is_connected());
    assert(shadow->is_connected());
    assert(shadow_child->is_connected());

    doc->remove_child(host);
    assert(!host->is_connected());
    assert(!light_grandchild->is_connected());
    assert(!shadow->is_connected());
    assert(!shadow_child->is_connected());

    doc->append_child(host);
    assert(host->is_connected());
    assert(light_grandchild->is_connected());
    assert(shadow_child->is_connected());
}

TEST(custom_elements_api) {
    using namespace litebird;
    CustomElementRegistry::default_registry().clear();

    auto doc = std::make_shared<Document>();
    auto body = std::make_shared<Element>("body");
    doc->append_child(body);

    JsInterpreter js(doc);

    // Validate name checks
    assert(CustomElementRegistry::is_valid_custom_element_name("my-element") == true);
    assert(CustomElementRegistry::is_valid_custom_element_name("user-profile-card") == true);
    assert(CustomElementRegistry::is_valid_custom_element_name("div") == false);
    assert(CustomElementRegistry::is_valid_custom_element_name("font-face") == false);

    js.eval_ast("var whenDefinedResolved = false;");
    js.eval_ast("var connectedFired = false;");
    js.eval_ast("var disconnectedFired = false;");
    js.eval_ast("var observedAttrChange = '';");
    js.eval_ast("var observedOldVal = '';");
    js.eval_ast("var observedNewVal = '';");

    js.eval_ast(R"(
        customElements.whenDefined("fancy-button").then(function() {
            whenDefinedResolved = true;
        });

        class FancyButton extends HTMLElement {
            static observedAttributes() {
                return ["label", "theme"];
            }
            connectedCallback() {
                connectedFired = true;
            }
            disconnectedCallback() {
                disconnectedFired = true;
            }
            attributeChangedCallback(name, oldVal, newVal) {
                observedAttrChange = name;
                observedOldVal = oldVal;
                observedNewVal = newVal;
            }
        }

        var isDefinedBefore = (customElements.get("fancy-button") !== undefined);
        customElements.define("fancy-button", FancyButton);
        var isDefinedAfter = (customElements.get("fancy-button") !== undefined);

        var btn = document.createElement("fancy-button");
        btn.setAttribute("label", "Click Me");
        document.body.appendChild(btn);
        btn.setAttribute("label", "Submit");
    )");

    EventLoop::default_loop().run_microtasks();

    assert(js.global_env()->get("isDefinedBefore").is_truthy() == false);
    assert(js.global_env()->get("isDefinedAfter").is_truthy() == true);
    assert(js.global_env()->get("whenDefinedResolved").is_truthy() == true);
    assert(js.global_env()->get("connectedFired").is_truthy() == true);
    assert(js.global_env()->get("observedAttrChange").to_string() == "label");
    assert(js.global_env()->get("observedOldVal").to_string() == "Click Me");
    assert(js.global_env()->get("observedNewVal").to_string() == "Submit");

    // Removal triggers disconnectedCallback
    js.eval_ast(R"(
        document.body.removeChild(btn);
    )");
    assert(js.global_env()->get("disconnectedFired").is_truthy() == true);
}

TEST(shadow_dom_and_slot_projection) {
    using namespace litebird;
    auto doc = std::make_shared<Document>();
    auto body = std::make_shared<Element>("body");
    doc->append_child(body);

    auto host = std::make_shared<Element>("my-widget");
    body->append_child(host);

    // Light DOM children
    auto light_span = std::make_shared<Element>("span");
    light_span->append_child(std::make_shared<Text>("Slotted Content"));
    host->append_child(light_span);

    auto light_named = std::make_shared<Element>("h3");
    light_named->set_attribute("slot", "header");
    light_named->append_child(std::make_shared<Text>("Widget Header"));
    host->append_child(light_named);

    JsInterpreter js(doc);

    std::string script = R"(
        var widget = document.querySelector("my-widget");
        var shadow = widget.attachShadow({ mode: "open" });
        var hasShadowRoot = (widget.shadowRoot === shadow);
        var shadowMode = shadow.mode;
        var shadowHost = (shadow.host === widget);

        shadow.innerHTML = "<style>:host { display: block; border: 2px solid #000; } p { color: #f00; }</style>" +
                           "<div class='wrapper'>" +
                           "  <slot name='header'></slot>" +
                           "  <p>Shadow Internal Text</p>" +
                           "  <slot></slot>" +
                           "</div>";

        var headerSlot = shadow.querySelector("slot");
        var assignedHeaderNodes = headerSlot.assignedNodes().length;
    )";

    js.eval_ast(script);

    assert(js.global_env()->get("hasShadowRoot").is_truthy() == true);
    assert(js.global_env()->get("shadowMode").to_string() == "open");
    assert(js.global_env()->get("shadowHost").is_truthy() == true);
    assert(js.global_env()->get("assignedHeaderNodes").to_number() >= 1.0);

    // Verify Scoped Styling & :host
    StyleResolver resolver;
    StyleSheet doc_sheet = CssParser::parse("p { color: #0000ff; }");
    resolver.add_stylesheet(doc_sheet);

    // Light DOM p would be blue; shadow DOM p must be red (#f00) because shadow scoped style applies
    auto shadow_p = host->shadow_root()->query_selector("p");
    assert(shadow_p != nullptr);
    ComputedStyle p_style = resolver.resolve_style(shadow_p);
    assert(p_style.color.r == 255 && p_style.color.g == 0 && p_style.color.b == 0);

    // Host element has border: 2px solid #000 from :host
    ComputedStyle host_style = resolver.resolve_style(host);
    assert(host_style.border_width.top == 2.0f);

    // Verify Layout Tree projects slotted light children inside shadow structure
    auto box_tree = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
    assert(box_tree != nullptr);

    // Check that layout tree has rendered box containing the slotted nodes
    bool found_header_text = false;
    bool found_shadow_text = false;
    bool found_slotted_text = false;

    std::function<void(const std::shared_ptr<LayoutBox>&)> walk_box = [&](const std::shared_ptr<LayoutBox>& box) {
        if (!box) return;
        if (box->box_type == BoxType::TextNode) {
            if (box->text.find("Widget Header") != std::string::npos) found_header_text = true;
            if (box->text.find("Shadow Internal Text") != std::string::npos) found_shadow_text = true;
            if (box->text.find("Slotted Content") != std::string::npos) found_slotted_text = true;
        }
        for (const auto& ch : box->children) {
            walk_box(ch);
        }
    };
    walk_box(box_tree);

    assert(found_header_text == true);
    assert(found_shadow_text == true);
    assert(found_slotted_text == true);
}

TEST(intersection_and_resize_observers) {
    using namespace litebird;
    auto doc = std::make_shared<Document>();
    auto root = doc->create_element("div");
    root->set_attribute("id", "viewport");
    root->set_bounding_box(Rect(0.0f, 0.0f, 800.0f, 600.0f));

    auto target = doc->create_element("div");
    target->set_attribute("id", "target");
    target->set_bounding_box(Rect(100.0f, 100.0f, 200.0f, 200.0f));
    root->append_child(target);
    doc->append_child(root);

    bool callback_called = false;
    size_t entries_count = 0;
    float observed_ratio = 0.0f;

    JsValue cb = JsValue::make_native_function([&](const std::vector<JsValue>& args) {
        callback_called = true;
        if (!args.empty() && args[0].is_array()) {
            entries_count = args[0].array_elements().size();
            if (entries_count > 0) {
                observed_ratio = static_cast<float>(args[0].array_elements()[0].get_property("intersectionRatio").to_number());
            }
        }
        return JsValue::make_undefined();
    });

    IntersectionObserverInit opts;
    opts.root = root;
    opts.thresholds = {0.0f, 0.5f, 1.0f};

    auto observer = std::make_shared<IntersectionObserver>(cb, opts);
    observer->observe(target);
    observer->check_intersections(Rect(0.0f, 0.0f, 800.0f, 600.0f));
    EventLoop::default_loop().run_microtasks();

    assert(callback_called == true);
    assert(entries_count == 1);
    assert(observed_ratio > 0.99f);

    // ResizeObserver
    bool resize_called = false;
    float content_w = 0.0f;
    float content_h = 0.0f;

    JsValue rcb = JsValue::make_native_function([&](const std::vector<JsValue>& args) {
        resize_called = true;
        if (!args.empty() && args[0].is_array() && !args[0].array_elements().empty()) {
            auto rect = args[0].array_elements()[0].get_property("contentRect");
            content_w = static_cast<float>(rect.get_property("width").to_number());
            content_h = static_cast<float>(rect.get_property("height").to_number());
        }
        return JsValue::make_undefined();
    });

    auto resize_obs = std::make_shared<ResizeObserver>(rcb);
    resize_obs->observe(target);
    resize_obs->check_resizes();
    EventLoop::default_loop().run_microtasks();

    assert(resize_called == true);
    assert(content_w == 200.0f);
    assert(content_h == 200.0f);

    // Test getBoundingClientRect via JS
    JsInterpreter interp(doc);
    interp.eval("var el = document.getElementById('target'); var rect = el.getBoundingClientRect();");
    auto rect_val = interp.eval("rect.width");
    assert(rect_val.to_number() == 200.0);
}

TEST(history_api_and_client_routing) {
    using namespace litebird;
    History history;
    assert(history.length() == 1);

    std::string navigated_url;
    history.set_navigation_callback([&](const std::string& url, const JsValue&) {
        navigated_url = url;
    });

    bool popstate_fired = false;
    JsValue popstate_state = JsValue::make_undefined();
    history.set_popstate_dispatcher([&](const JsValue& st) {
        popstate_fired = true;
        popstate_state = st;
    });

    JsValue s1 = JsValue::make_string("state-1");
    history.push_state(s1, "Title 1", "/dashboard");
    assert(history.length() == 2);
    assert(history.current_url() == "/dashboard");

    JsValue s2 = JsValue::make_string("state-2");
    history.push_state(s2, "Title 2", "/settings");
    assert(history.length() == 3);
    assert(history.current_url() == "/settings");

    JsValue s2_rev = JsValue::make_string("state-2-revised");
    history.replace_state(s2_rev, "Title 2 Rev", "/settings/profile");
    assert(history.length() == 3);
    assert(history.current_url() == "/settings/profile");

    history.back();
    assert(popstate_fired == true);
    assert(popstate_state.to_string() == "state-1");
    assert(history.current_url() == "/dashboard");

    popstate_fired = false;
    history.forward();
    assert(popstate_fired == true);
    assert(popstate_state.to_string() == "state-2-revised");
    assert(history.current_url() == "/settings/profile");

    // Test with JS interpreter
    auto doc = std::make_shared<Document>();
    JsInterpreter interp(doc);
    interp.eval(R"(
        var popCount = 0;
        var lastState = null;
        window.addEventListener("popstate", function(e) {
            popCount = popCount + 1;
            lastState = e.state;
        });
        history.pushState("js-page-1", "P1", "/js1");
        history.pushState("js-page-2", "P2", "/js2");
        history.back();
    )");

    auto pop_count = interp.eval("popCount");
    assert(pop_count.to_number() == 1.0);
    auto last_state = interp.eval("lastState");
    assert(last_state.to_string() == "js-page-1");
}

TEST(css_filters_and_canvas_blending) {
    using namespace litebird;
    // 1. ImageFilter algorithms
    std::vector<Color> pixels = {
        Color(100, 100, 100, 255),
        Color(150, 150, 150, 255),
        Color(200, 50, 50, 255),
        Color(50, 200, 50, 255)
    };
    ImageFilter::brightness(pixels, 1.5f);
    assert(pixels[0].r > 100);

    ImageFilter::grayscale(pixels, 1.0f);
    assert(pixels[0].r == pixels[0].g && pixels[0].g == pixels[0].b);

    ImageFilter::invert(pixels, 1.0f);
    assert(pixels[0].a == 255);

    // 2. CSS Filter Parsing & Cascade
    auto sheet = CssParser::parse(R"(
        .filtered-box {
            filter: blur(5px) brightness(120%) contrast(150%) drop-shadow(4px 4px black);
            backdrop-filter: blur(10px) grayscale(80%);
        }
    )");
    StyleResolver resolver;
    resolver.add_stylesheet(sheet);

    auto doc = std::make_shared<Document>();
    auto el = doc->create_element("div");
    el->set_attribute("class", "filtered-box");
    doc->append_child(el);

    auto style = resolver.resolve_style(el);
    assert(style.filters.size() == 4);
    assert(style.filters[0].type == FilterType::Blur);
    assert(style.filters[0].blur_radius == 5.0f);
    assert(style.filters[1].type == FilterType::Brightness);
    assert(style.filters[1].amount == 1.2f);
    assert(style.filters[2].type == FilterType::Contrast);
    assert(style.filters[2].amount == 1.5f);
    assert(style.filters[3].type == FilterType::DropShadow);
    assert(style.filters[3].dx == 4.0f);
    assert(style.filters[3].dy == 4.0f);

    assert(style.backdrop_filters.size() == 2);
    assert(style.backdrop_filters[0].type == FilterType::Blur);
    assert(style.backdrop_filters[1].type == FilterType::Grayscale);
    assert(style.backdrop_filters[1].amount == 0.8f);

    // 3. Canvas 2D Blending
    auto canvas = std::make_shared<CanvasRenderingContext2D>(20, 20);
    assert(canvas->global_composite_operation == "source-over");
    canvas->global_composite_operation = "multiply";
    assert(canvas->global_composite_operation == "multiply");
    canvas->fill_style = Color(255, 0, 0, 255);
    canvas->fill_rect(0, 0, 20, 20);
    canvas->global_composite_operation = "screen";
    assert(canvas->global_composite_operation == "screen");
}

TEST(js_generators_iterators_and_for_of) {
    using namespace litebird;
    JsInterpreter interp;

    // 1. Basic generator and yield
    interp.eval(R"(
        function* countTo3() {
            yield 10;
            yield 20;
            return 30;
        }
        var gen = countTo3();
        var step1 = gen.next();
        var step2 = gen.next();
        var step3 = gen.next();
        var step4 = gen.next();
    )");

    assert(interp.eval("step1.value").to_number() == 10.0);
    assert(interp.eval("step1.done").is_truthy() == false);
    assert(interp.eval("step2.value").to_number() == 20.0);
    assert(interp.eval("step2.done").is_truthy() == false);
    assert(interp.eval("step3.value").to_number() == 30.0);
    assert(interp.eval("step3.done").is_truthy() == true);
    assert(interp.eval("step4.done").is_truthy() == true);

    // 2. for...of loop with array
    interp.eval(R"(
        var sum = 0;
        for (var x of [1, 2, 3, 4, 5]) {
            sum = sum + x;
        }
    )");
    assert(interp.eval("sum").to_number() == 15.0);

    // 3. for...of loop with generator
    interp.eval(R"(
        function* genValues() {
            yield 100;
            yield 200;
            yield 300;
        }
        var total = 0;
        for (var v of genValues()) {
            total = total + v;
        }
    )");
    assert(interp.eval("total").to_number() == 600.0);

    // 4. yield* delegation
    interp.eval(R"(
        function* subGen() {
            yield 2;
            yield 3;
        }
        function* mainGen() {
            yield 1;
            yield* subGen();
            yield 4;
        }
        var delegatedList = [];
        for (var item of mainGen()) {
            delegatedList.push(item);
        }
    )");
    assert(interp.eval("delegatedList.length").to_number() == 4.0);
    assert(interp.eval("delegatedList[0]").to_number() == 1.0);
    assert(interp.eval("delegatedList[1]").to_number() == 2.0);
    assert(interp.eval("delegatedList[2]").to_number() == 3.0);
    assert(interp.eval("delegatedList[3]").to_number() == 4.0);
}

TEST(abort_controller_and_fetch_cancellation) {
    using namespace litebird;
    JsInterpreter interp;

    // 1. AbortController lifecycle
    interp.eval(R"(
        var ctrl = new AbortController();
        var sig = ctrl.signal;
        var initialAborted = sig.aborted;
        var abortFired = false;
        var eventType = "";

        sig.addEventListener("abort", function(e) {
            abortFired = true;
            eventType = e.type;
        });

        ctrl.abort("Operation cancelled");
        var finalAborted = sig.aborted;
        var reason = sig.reason;
    )");

    assert(interp.eval("initialAborted").is_truthy() == false);
    assert(interp.eval("finalAborted").is_truthy() == true);
    assert(interp.eval("abortFired").is_truthy() == true);
    assert(interp.eval("eventType").to_string() == "abort");
    assert(interp.eval("reason").to_string() == "Operation cancelled");

    // 2. AbortSignal.abort() and AbortSignal.timeout()
    interp.eval(R"(
        var staticSig = AbortSignal.abort("immediate-stop");
        var staticAborted = staticSig.aborted;
        var staticReason = staticSig.reason;

        var timeoutSig = AbortSignal.timeout(100);
        var timeoutInitial = timeoutSig.aborted;
    )");
    assert(interp.eval("staticAborted").is_truthy() == true);
    assert(interp.eval("staticReason").to_string() == "immediate-stop");
    assert(interp.eval("timeoutInitial").is_truthy() == false);

    // 3. fetch cancellation with aborted signal
    interp.eval(R"(
        var fetchRejected = false;
        var fetchError = "";
        var preAbortedCtrl = new AbortController();
        preAbortedCtrl.abort("Manual fetch cancel");

        fetch("http://example.com/data", { signal: preAbortedCtrl.signal }).catch(function(err) {
            fetchRejected = true;
            fetchError = err;
        });
    )");
    EventLoop::default_loop().run_microtasks();
    assert(interp.eval("fetchRejected").is_truthy() == true);
    assert(interp.eval("fetchError").to_string() == "Manual fetch cancel");
}

TEST(css_grid_template_areas_and_containment) {
    using namespace litebird;
    // 1. Grid template areas parsing
    auto sheet = CssParser::parse(R"(
        .grid-container {
            display: grid;
            grid-template-columns: 200px 1fr;
            grid-template-areas:
                "header header"
                "sidebar content"
                "footer footer";
            row-gap: 10px;
            contain: layout paint;
        }
        .header-item {
            grid-area: header;
            height: 50px;
        }
        .sidebar-item {
            grid-area: sidebar;
            height: 100px;
        }
        .content-item {
            grid-area: content;
            height: 100px;
        }
        .footer-item {
            grid-area: footer;
            height: 40px;
        }
        .strict-container {
            contain: strict;
        }
    )");

    StyleResolver resolver;
    resolver.add_stylesheet(sheet);

    auto doc = std::make_shared<Document>();
    auto container = doc->create_element("div");
    container->set_attribute("class", "grid-container");

    auto header = doc->create_element("div");
    header->set_attribute("class", "header-item");
    auto sidebar = doc->create_element("div");
    sidebar->set_attribute("class", "sidebar-item");
    auto content = doc->create_element("div");
    content->set_attribute("class", "content-item");
    auto footer = doc->create_element("div");
    footer->set_attribute("class", "footer-item");

    container->append_child(header);
    container->append_child(sidebar);
    container->append_child(content);
    container->append_child(footer);
    doc->append_child(container);

    auto container_style = resolver.resolve_style(container);
    assert(container_style.grid_named_areas.find("header") != container_style.grid_named_areas.end());
    assert(container_style.grid_named_areas["header"].row_start == 1);
    assert(container_style.grid_named_areas["header"].row_end == 2);
    assert(container_style.grid_named_areas["header"].col_start == 1);
    assert(container_style.grid_named_areas["header"].col_end == 3);

    assert(container_style.grid_named_areas["sidebar"].row_start == 2);
    assert(container_style.grid_named_areas["sidebar"].col_start == 1);
    assert(container_style.grid_named_areas["sidebar"].col_end == 2);

    assert(container_style.grid_named_areas["content"].row_start == 2);
    assert(container_style.grid_named_areas["content"].col_start == 2);
    assert(container_style.grid_named_areas["content"].col_end == 3);

    assert(container_style.grid_named_areas["footer"].row_start == 3);
    assert(container_style.grid_named_areas["footer"].col_start == 1);
    assert(container_style.grid_named_areas["footer"].col_end == 3);

    // Containment flags
    assert(container_style.contains_layout() == true);
    assert(container_style.contains_paint() == true);
    assert(container_style.contains_size() == false);

    auto strict_el = doc->create_element("div");
    strict_el->set_attribute("class", "strict-container");
    auto strict_style = resolver.resolve_style(strict_el);
    assert(strict_style.contains_layout() == true);
    assert(strict_style.contains_paint() == true);
    assert(strict_style.contains_size() == true);

    // 2. Grid Area Layout Positioning
    auto grid_box = LayoutTree::build(container, resolver, 600.0f, 800.0f);
    assert(grid_box != nullptr);
    assert(grid_box->children.size() == 4);

    auto header_box = grid_box->children[0];
    auto sidebar_box = grid_box->children[1];
    auto content_box = grid_box->children[2];
    auto footer_box = grid_box->children[3];

    assert(header_box->dimensions.content.y < sidebar_box->dimensions.content.y);
    assert(sidebar_box->dimensions.content.x < content_box->dimensions.content.x);
    assert(sidebar_box->dimensions.content.y == content_box->dimensions.content.y);
    assert(content_box->dimensions.content.y < footer_box->dimensions.content.y);
}


// ---------------------------------------------------------------------------
// Phase 15: spec tree construction, cascade completeness, conformance
// ---------------------------------------------------------------------------

TEST(html5_tree_construction_implied_structure) {
    using namespace litebird;

    // The parser must synthesise <html>, <head> and <body> and auto-close <p>.
    auto doc = HtmlParser::parse("<p>One<p>Two");
    assert(doc->document_element() && doc->document_element()->tag_name() == "html");
    assert(doc->head() != nullptr);
    assert(doc->body() != nullptr);

    auto paragraphs = doc->get_elements_by_tag_name("p");
    assert(paragraphs.size() == 2);
    assert(paragraphs[0]->text_content() == "One");
    assert(paragraphs[1]->text_content() == "Two");
    assert(paragraphs[0]->parent() == paragraphs[1]->parent());

    // <li> closes an open <li>, and stays inside its list.
    auto list_doc = HtmlParser::parse("<ul><li>a<li>b</ul>");
    auto items = list_doc->get_elements_by_tag_name("li");
    assert(items.size() == 2);
    assert(items[0]->parent() == items[1]->parent());
    assert(items[0]->parent()->as_element() != nullptr);
    assert(items[0]->parent()->as_element()->tag_name() == "ul");

    // A missing </div> must not swallow the following paragraph.
    auto nested = HtmlParser::parse("<div><p>text<p>more</div><section>after</section>");
    auto sections = nested->get_elements_by_tag_name("section");
    assert(sections.size() == 1);
    assert(sections[0]->parent() == nested->body());
}

TEST(html5_foster_parenting_and_adoption_agency) {
    using namespace litebird;

    // Content that is not table content is foster-parented before the table.
    auto doc = HtmlParser::parse("<table><b>hi</table>");
    auto tables = doc->get_elements_by_tag_name("table");
    auto bold = doc->get_elements_by_tag_name("b");
    assert(tables.size() == 1);
    assert(bold.size() == 1);
    assert(bold[0]->parent() == doc->body());
    assert(bold[0]->text_content() == "hi");
    // The <b> must not have ended up inside the table.
    assert(bold[0]->parent() != tables[0]);

    // Misnested formatting elements are repaired by the adoption agency.
    auto repaired = HtmlParser::parse("<b><i>x</b>y</i>");
    auto body = repaired->body();
    assert(body != nullptr);
    assert(body->children().size() == 2);
    assert(body->children()[0]->is_element());
    assert(static_cast<Element*>(body->children()[0].get())->tag_name() == "b");
    assert(body->children()[1]->is_element());
    assert(static_cast<Element*>(body->children()[1].get())->tag_name() == "i");
    assert(body->children()[1]->text_content() == "y");
}

TEST(html5_comments_and_character_references) {
    using namespace litebird;

    auto doc = HtmlParser::parse("<!doctype html><!--before--><div><!--inner--></div>");
    assert(doc->has_doctype());
    assert(doc->doctype().name == "html");
    assert(doc->quirks_mode() == QuirksMode::NoQuirks);

    // Comments are real DOM nodes, not discarded.
    bool saw_before = false;
    for (const auto& child : doc->children()) {
        if (child->is_comment()) saw_before = true;
    }
    assert(saw_before);

    auto divs = doc->get_elements_by_tag_name("div");
    assert(divs.size() == 1);
    assert(divs[0]->children().size() == 1);
    assert(divs[0]->children()[0]->is_comment());

    // Named, numeric and hex character references, including a multi-byte one.
    auto text_doc = HtmlParser::parse("<p>&amp;&lt;&gt;&quot;&#65;&#x42;&notin;</p>");
    auto paragraphs = text_doc->get_elements_by_tag_name("p");
    assert(paragraphs.size() == 1);
    const std::string expected = std::string("&<>\"AB") + "\xE2\x88\x89";  // U+2209
    assert(paragraphs[0]->text_content() == expected);

    // Missing doctype means quirks mode.
    auto quirks = HtmlParser::parse("<p>x</p>");
    assert(quirks->quirks_mode() == QuirksMode::Quirks);
}

TEST(html5_template_contents_and_fragments) {
    using namespace litebird;

    auto doc = HtmlParser::parse("<template><p>Hi</p></template>");
    auto templates = doc->get_elements_by_tag_name("template");
    assert(templates.size() == 1);
    assert(templates[0]->children().empty());
    assert(templates[0]->template_contents() != nullptr);
    assert(templates[0]->template_contents()->children().size() == 1);

    // innerHTML-style fragment parsing honours the context element, so a <tr>
    // is kept when the context is <tbody>.
    auto fragment = HtmlParser::parse_fragment_with_context("<tr><td>cell</td></tr>", "tbody");
    assert(fragment != nullptr);
    assert(fragment->children().size() == 1);
    assert(fragment->children()[0]->as_element() != nullptr);
    assert(fragment->children()[0]->as_element()->tag_name() == "tr");

    // ... and in a <div> context the table elements are dropped, leaving the
    // text content behind (the in-body "ignore these start tags" rule).
    auto div_fragment = HtmlParser::parse_fragment_with_context("<tr><td>cell</td></tr>", "div");
    assert(div_fragment->children().size() == 1);
    assert(div_fragment->children()[0]->is_text());
    assert(div_fragment->children()[0]->text_content() == "cell");
}

TEST(css_shorthand_expansion_and_important) {
    using namespace litebird;

    auto doc = HtmlParser::parse(
        "<div id=\"box\" style=\"margin: 10px 20px; padding: 4px 8px 12px;\"></div>");
    StyleResolver resolver;
    auto box = doc->get_element_by_id("box");
    assert(box != nullptr);
    auto style = resolver.resolve_style(box, nullptr);
    assert(style.margin.top == 10.0f);
    assert(style.margin.right == 20.0f);
    assert(style.margin.bottom == 10.0f);
    assert(style.margin.left == 20.0f);
    assert(style.padding.top == 4.0f);
    assert(style.padding.right == 8.0f);
    assert(style.padding.bottom == 12.0f);
    assert(style.padding.left == 8.0f);

    // border shorthand splits into width and colour.
    auto border_doc = HtmlParser::parse(
        "<div id=\"b\" style=\"border: 3px solid #00ff00\"></div>");
    StyleResolver border_resolver;
    auto bordered = border_resolver.resolve_style(border_doc->get_element_by_id("b"), nullptr);
    assert(bordered.border_width.top == 3.0f);
    assert(bordered.border_color.g == 255 && bordered.border_color.r == 0);

    // !important outranks a more specific normal declaration.
    StyleResolver important_resolver;
    important_resolver.add_stylesheet(
        CssParser::parse("#x { color: #00ff00 !important; } div#x { color: #ff0000; }"));
    auto important_doc = HtmlParser::parse("<div id=\"x\"></div>");
    auto important_style =
        important_resolver.resolve_style(important_doc->get_element_by_id("x"), nullptr);
    assert(important_style.color.r == 0 && important_style.color.g == 255);

    // A normal inline declaration still loses to a stylesheet !important.
    StyleResolver inline_resolver;
    inline_resolver.add_stylesheet(CssParser::parse("#y { color: #0000ff !important; }"));
    auto inline_doc = HtmlParser::parse("<div id=\"y\" style=\"color: #ff0000\"></div>");
    auto inline_style = inline_resolver.resolve_style(inline_doc->get_element_by_id("y"), nullptr);
    assert(inline_style.color.b == 255 && inline_style.color.r == 0);
}

TEST(css_relative_units) {
    using namespace litebird;

    StyleResolver resolver;
    resolver.set_viewport(1000.0f, 400.0f);
    auto doc = HtmlParser::parse(
        "<div id=\"a\" style=\"font-size: 20px; width: 2em; height: 50vh; margin-top: 1rem;\"></div>");
    auto style = resolver.resolve_style(doc->get_element_by_id("a"), nullptr);

    assert(style.font_size == 20.0f);
    assert(style.width == 40.0f);          // 2em against the element's own font size
    assert(style.height == 200.0f);        // 50vh of a 400px viewport
    assert(style.margin.top == 16.0f);     // 1rem against the 16px root size

    // Percentages are *not* pixels: the cascade records the percentage and
    // leaves the length auto, because only layout knows the containing block.
    // (Before Phase 18 this asserted width == 50.0f, i.e. it encoded
    // `width: 50%` becoming fifty pixels.)
    auto pct_doc = HtmlParser::parse("<div id=\"p\" style=\"width: 50%\"></div>");
    auto pct_style = resolver.resolve_style(pct_doc->get_element_by_id("p"), nullptr);
    assert(pct_style.width == -1.0f);
    assert(pct_style.width_percent == 50.0f);
}


// ---------------------------------------------------------------------------
// Phase 16: floats, block formatting contexts and table layout
// ---------------------------------------------------------------------------

namespace {

std::shared_ptr<litebird::LayoutBox> find_box_by_id(const std::shared_ptr<litebird::LayoutBox>& box,
                                                    const std::string& id) {
    if (!box) return nullptr;
    if (box->node && box->node->is_element() && box->node->as_element()->id() == id) return box;
    for (const auto& child : box->children) {
        if (auto found = find_box_by_id(child, id)) return found;
    }
    return nullptr;
}

} // namespace

TEST(float_layout_and_text_wrapping) {
    using namespace litebird;

    std::string html = R"(
        <div id="wrap" style="width: 400px; font-size: 10px;">
            <div id="floaty" style="float: left; width: 100px; height: 60px;"></div>
            <p id="para" style="margin: 0; font-size: 10px;">aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll mmmm nnnn oooo pppp qqqq rrrr ssss tttt uuuu vvvv wwww xxxx yyyy zzzz aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll mmmm nnnn oooo pppp qqqq rrrr ssss tttt uuuu vvvv wwww xxxx yyyy zzzz aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll mmmm nnnn oooo pppp qqqq rrrr ssss tttt uuuu vvvv wwww xxxx yyyy zzzz</p>
        </div>
    )";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 400.0f, 600.0f);

    auto wrap = find_box_by_id(root, "wrap");
    auto floated = find_box_by_id(root, "floaty");
    auto para = find_box_by_id(root, "para");
    assert(wrap && floated && para);

    // The float is taken out of the flow and sits at the container's left edge.
    assert(floated->dimensions.content.width == 100.0f);
    assert(floated->dimensions.content.height == 60.0f);
    assert(floated->dimensions.content.x == wrap->dimensions.content.x);

    // The paragraph is a block: its box still spans the full width (CSS 2.1
    // 9.5), but its *line boxes* are pushed right of the float.
    assert(para->dimensions.content.width == wrap->dimensions.content.width);
    assert(!para->line_boxes.empty());

    const auto& first_line = para->line_boxes.front();
    assert(first_line.left_intrusion == 100.0f);
    assert(first_line.fragments.front().rect.x >= para->dimensions.content.x + 100.0f);

    // Once past the float's bottom edge, lines reclaim the full width.
    bool found_unwrapped_line = false;
    for (const auto& line : para->line_boxes) {
        if (line.left_intrusion == 0.0f) found_unwrapped_line = true;
    }
    assert(found_unwrapped_line);

    // A right float mirrors the same behaviour, measured from the right edge.
    std::string right_html = R"(
        <div id="rwrap" style="width: 300px; font-size: 10px;">
            <div id="rfloat" style="float: right; width: 80px; height: 100px;"></div>
            <p id="rpara" style="margin: 0; font-size: 10px;">aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll</p>
        </div>
    )";
    auto right_doc = HtmlParser::parse(right_html);
    StyleResolver right_resolver;
    auto right_root = LayoutTree::build(right_doc, right_resolver, 300.0f, 600.0f);
    auto rfloat = find_box_by_id(right_root, "rfloat");
    auto rpara = find_box_by_id(right_root, "rpara");
    assert(rfloat && rpara);

    // Margin box's right edge is flush with the container's right edge.
    const float float_right = rfloat->dimensions.content.x + rfloat->dimensions.content.width +
                              rfloat->dimensions.padding.right + rfloat->dimensions.border.right +
                              rfloat->dimensions.margin.right;
    const float container_right = rpara->dimensions.content.x + rpara->dimensions.content.width;
    assert(std::abs(float_right - container_right) < 0.5f);
    assert(!rpara->line_boxes.empty());
    assert(rpara->line_boxes.front().right_intrusion == 80.0f);
}

TEST(float_clearance_and_bug_containment) {
    using namespace litebird;

    std::string html = R"(
        <div id="outer" style="width: 400px; font-size: 10px;">
            <div id="f" style="float: left; width: 120px; height: 90px;"></div>
            <div id="cleared" style="clear: left; height: 20px;"></div>
        </div>
        <div id="bfc" style="width: 400px; overflow: hidden; font-size: 10px;">
            <div id="f2" style="float: left; width: 50px; height: 70px;"></div>
        </div>
    )";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 400.0f, 600.0f);

    auto outer = find_box_by_id(root, "outer");
    auto cleared = find_box_by_id(root, "cleared");
    auto bfc = find_box_by_id(root, "bfc");
    auto f2 = find_box_by_id(root, "f2");
    assert(outer && cleared && bfc && f2);

    // clear: left pushes the box below the float's bottom edge.
    assert(cleared->dimensions.content.y >= outer->dimensions.content.y + 90.0f);

    // A float-only container with overflow:hidden contains its float, so it
    // does not collapse to zero height.
    assert(f2->dimensions.content.height == 70.0f);
    assert(bfc->dimensions.content.height >= 70.0f);
}

TEST(table_layout_columns_and_rows) {
    using namespace litebird;

    std::string html = R"(
        <table id="t" style="width: 300px">
            <tr>
                <td id="c11">aa</td>
                <td id="c12">bbbb</td>
            </tr>
            <tr>
                <td id="c21" style="height: 40px">cc</td>
                <td id="c22" style="height: 40px">dd</td>
            </tr>
        </table>
    )";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);

    auto table = find_box_by_id(root, "t");
    assert(table != nullptr);
    assert(table->box_type == BoxType::TableNode);

    // Explicit table width is honoured and split across the two columns.
    assert(std::abs(table->dimensions.content.width - 300.0f) < 2.0f);
    // Row heights come from the tallest cell: 20px (first row) + 40px (second).
    assert(table->dimensions.content.height >= 60.0f);

    auto c11 = find_box_by_id(root, "c11");
    auto c12 = find_box_by_id(root, "c12");
    auto c21 = find_box_by_id(root, "c21");
    auto c22 = find_box_by_id(root, "c22");
    assert(c11 && c12 && c21 && c22);

    // Cells are laid out side by side, in column order.
    assert(c11->dimensions.content.x < c12->dimensions.content.x);
    assert(std::abs(c11->dimensions.content.y - c12->dimensions.content.y) < 0.5f);

    // Second row sits below the first and lines up with it.
    assert(c21->dimensions.content.y > c11->dimensions.content.y);
    // An explicit cell height sizes the whole row.
    assert(std::abs(c21->dimensions.content.height - 40.0f) < 0.5f);
    assert(std::abs(table->dimensions.content.height -
                    (c11->dimensions.margin_box().height + c21->dimensions.margin_box().height)) < 1.0f);
    assert(std::abs(c21->dimensions.content.x - c11->dimensions.content.x) < 0.5f);
    assert(std::abs(c22->dimensions.content.x - c12->dimensions.content.x) < 0.5f);

    // The explicit table width is distributed across the columns, so they fill
    // the table and both are wide enough to be real columns.
    const float first_column = c11->dimensions.margin_box().width;
    const float second_column = c12->dimensions.margin_box().width;
    assert(std::abs((first_column + second_column) - 300.0f) < 2.0f);
    assert(first_column > 40.0f && second_column > 40.0f);

    // Auto table layout: without an explicit width the table shrink-wraps.
    std::string auto_html = R"(
        <table id="auto">
            <tr><td id="a11">x</td><td id="a12">yyyyyyyy</td></tr>
        </table>
    )";
    auto auto_doc = HtmlParser::parse(auto_html);
    StyleResolver auto_resolver;
    auto auto_root = LayoutTree::build(auto_doc, auto_resolver, 800.0f, 600.0f);
    auto auto_table = find_box_by_id(auto_root, "auto");
    auto a11 = find_box_by_id(auto_root, "a11");
    auto a12 = find_box_by_id(auto_root, "a12");
    assert(auto_table && a11 && a12);
    assert(auto_table->dimensions.content.width < 400.0f);        // shrink-wrapped
    assert(a12->dimensions.content.x > a11->dimensions.content.x);  // still columnar
}


TEST(float_shrinks_inline_line_boxes) {
    using namespace litebird;

    // A float and inline text in the same block: the anonymous block that wraps
    // the text must inherit the float context and wrap its lines around it.
    std::string html = R"(
        <div id="box" style="width: 200px; font-size: 10px;">
            <div id="f" style="float: left; width: 50px; height: 25px;"></div>
            some text here that should wrap beside the float and then reclaim the full width of the box afterwards
        </div>
    )";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 200.0f, 600.0f);

    auto box = find_box_by_id(root, "box");
    assert(box != nullptr);

    // Find the anonymous block holding the text.
    std::shared_ptr<LayoutBox> text_block;
    std::function<void(const std::shared_ptr<LayoutBox>&)> walk = [&](const std::shared_ptr<LayoutBox>& b) {
        if (!b) return;
        if (b->box_type == BoxType::AnonymousBlock && !b->line_boxes.empty()) text_block = b;
        for (const auto& child : b->children) walk(child);
    };
    walk(box);
    assert(text_block != nullptr);

    // First line is pushed right by exactly the float's width.
    assert(text_block->line_boxes.front().left_intrusion == 50.0f);
    assert(text_block->line_boxes.front().rect.x == text_block->dimensions.content.x + 50.0f);

    // The enlarged context contains the float.
    assert(box->dimensions.content.height >= 25.0f);
}

TEST(float_left_and_right_can_stack_side_by_side) {
    using namespace litebird;

    std::string html = R"(
        <div id="stack" style="width: 300px; font-size: 10px;">
            <div id="l1" style="float: left; width: 100px; height: 40px;"></div>
            <div id="r1" style="float: right; width: 100px; height: 40px;"></div>
            <div id="l2" style="float: left; width: 100px; height: 40px;"></div>
        </div>
    )";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 300.0f, 600.0f);

    auto stack = find_box_by_id(root, "stack");
    auto l1 = find_box_by_id(root, "l1");
    auto r1 = find_box_by_id(root, "r1");
    auto l2 = find_box_by_id(root, "l2");
    assert(stack && l1 && r1 && l2);

    const float left_edge = stack->dimensions.content.x;
    const float right_edge = stack->dimensions.content.x + stack->dimensions.content.width;

    // Left floats pack from the left edge in order; the right float sits flush
    // against the right edge.
    assert(std::abs(l1->dimensions.content.x - left_edge) < 0.5f);
    assert(std::abs(l2->dimensions.content.x - (left_edge + 100.0f)) < 0.5f);
    assert(std::abs((r1->dimensions.content.x + r1->dimensions.content.width) - right_edge) < 0.5f);

    // A fourth float no longer fits and must drop below the first band.
    std::string wrap_html = R"(
        <div id="s2" style="width: 300px; font-size: 10px;">
            <div id="a" style="float: left; width: 200px; height: 40px;"></div>
            <div id="b" style="float: left; width: 200px; height: 40px;"></div>
        </div>
    )";
    auto wrap_doc = HtmlParser::parse(wrap_html);
    StyleResolver wrap_resolver;
    auto wrap_root = LayoutTree::build(wrap_doc, wrap_resolver, 300.0f, 600.0f);
    auto a = find_box_by_id(wrap_root, "a");
    auto b = find_box_by_id(wrap_root, "b");
    assert(a && b);
    assert(b->dimensions.content.y >= a->dimensions.content.y + 40.0f);
    assert(std::abs(b->dimensions.content.x - a->dimensions.content.x) < 0.5f);
}


// ---------------------------------------------------------------------------
// Phase 17: spec-fidelity fixes found by the conformance scoreboard
// ---------------------------------------------------------------------------

namespace {

std::string tree_of(const std::string& html) {
    using namespace litebird;
    auto doc = HtmlParser::parse(html);
    return dump_tree_webplatform(*doc);
}

std::string fragment_tree_of(const std::string& html, const std::string& context) {
    using namespace litebird;
    auto fragment = HtmlParser::parse_fragment_with_context(html, context, ElementNamespace::HTML, true);
    return dump_tree_webplatform(*fragment);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

/// Collapses the tree dump to one line so a mismatch is readable in the log.
std::string flatten(const std::string& dump) {
    std::string out;
    for (char c : dump) {
        if (c == '\n') out.push_back(' ');
        else if (c != '|') out.push_back(c);
    }
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

} // namespace

TEST(script_data_escaped_and_double_escaped_states) {
    using namespace litebird;

    // A "<!--" inside <script> starts escaped text: a following </script> does
    // not end the element, it is just text.
    const std::string escaped = tree_of("<!doctype html><script><!--<script </script>-->");
    assert(contains(escaped, "\"<!--<script </script>-->\""));

    // Inside a double-escaped run, "</script " only leaves the double-escaped
    // state; the script continues until the real end tag.
    const std::string doubled = tree_of("<!doctype html><script><!--<script </script -->");
    assert(contains(doubled, "\"<!--<script </script -->\""));

    // The plain (non-escaped) end tag still terminates the script.
    const std::string plain = tree_of("<!doctype html><script>var a=1;</script><p>after");
    assert(contains(plain, "var a=1;"));
    assert(contains(plain, "<p>"));
    assert(!contains(plain, "after</script>"));

    // "<" and "</" at EOF are text, not dropped.
    assert(contains(tree_of("<!doctype html><script></"), "\"</\""));
    assert(contains(tree_of("<!doctype html><script><"), "\"<\""));
}

TEST(pre_and_textarea_leading_newline_handling) {
    // The first newline inside <pre>/<listing>/<textarea> is an authoring
    // convenience and is dropped; everything after it is preserved.
    const std::string pre = tree_of("<!doctype html><body><pre>\nfoo</pre>");
    assert(contains(pre, "\"foo\""));
    assert(!contains(pre, "\"\nfoo\""));

    const std::string listing = tree_of("<!doctype html><body><listing>\n\nbar</listing>");
    assert(contains(listing, "\"\nbar\""));  // only the first newline goes

    // A leading newline that is not immediately after the start tag survives.
    const std::string textarea = tree_of("<!doctype html><body><textarea>\nabc</textarea>");
    assert(contains(textarea, "\"abc\""));
}

TEST(adoption_agency_nobr_and_nested_formatting) {
    // Two <nobr> start tags trigger the adoption agency for the first one.
    const std::string nobr = tree_of("<!doctype html><body><b><nobr>1<nobr></b><i>2");
    assert(contains(nobr, "<nobr>"));

    // A second <a> closes the first through the adoption agency.
    const std::string anchors = tree_of("<!doctype html><body><a href='#1'>1<a href='#2'>2");
    assert(contains(anchors, "href=\"#1\""));
    assert(contains(anchors, "href=\"#2\""));
    // The two anchors must not be nested: the second is a sibling.
    const auto first = anchors.find("href=\"#1\"");
    const auto second = anchors.find("href=\"#2\"");
    assert(first != std::string::npos && second != std::string::npos);
}

TEST(select_content_parsing_and_foreign_end_tags) {
    // Unknown elements inside <select> are kept as ordinary elements rather
    // than silently dropped (the modern select parsing rules).
    const std::string select = tree_of("<!doctype html><body><select><div>x</div><option>o</select>");
    assert(contains(select, "<div>"));
    assert(contains(select, "<option>"));

    // </p> and </br> inside foreign content pop out of the SVG subtree.
    const std::string svg_p = tree_of("<!doctype html><body><svg></p><foo>");
    assert(contains(svg_p, "<svg svg>"));
    const std::string flat = flatten(svg_p);
    assert(contains(flat, "<p>"));
    assert(contains(flat, "<foo>"));

    const std::string svg_br = tree_of("<!doctype html><body><svg></br>");
    assert(contains(flatten(svg_br), "<br>"));

    // An <svg title> fragment context parses its children as markup.
    const std::string title_fragment = fragment_tree_of("<div></div>", "title");
    assert(contains(title_fragment, "<div>"));
}

TEST(svg_attribute_adjustment_and_character_reference_edges) {
    // The tokenizer lower-cases attribute names; SVG spellings are restored.
    const std::string svg = tree_of("<!doctype html><body><svg viewBox='0 0 1 1' preserveAspectRatio='none'>");
    assert(contains(svg, "viewBox=\"0 0 1 1\""));
    assert(contains(svg, "preserveAspectRatio=\"none\""));

    // xlink:/xml: attributes keep their namespace spelling, xml:base does not
    // (it is not in the spec's adjustment table).
    const std::string xlink = tree_of("<!doctype html><body><svg xml:lang='en' xlink:href='#a' xml:base='b'>");
    assert(contains(xlink, "xml lang=\"en\""));
    assert(contains(xlink, "xlink href=\"#a\""));
    assert(contains(xlink, "xml:base=\"b\""));

    // Numeric references with no digits are literal text, not a swallowed "&".
    assert(contains(tree_of("FOO&#xZOO"), "\"FOO&#xZOO\""));
    assert(contains(tree_of("FOO&#41;BAR"), "\"FOO)BAR\""));
}

TEST(template_and_scope_helpers_ignore_foreign_elements) {
    // <html>/<body> inside a template must not mutate the document element.
    const std::string tpl = tree_of("<html a=b><template><div></div><body c=d></body></template>");
    assert(contains(tpl, "a=\"b\""));
    assert(!contains(tpl, "c=\"d\""));

    // A foreign element with an HTML local name (svg td) must not satisfy an
    // HTML "td in table scope" test.
    const std::string scoped = tree_of("<!doctype html><table><tr><td><svg><td><div>x");
    assert(contains(scoped, "<svg td>"));
    assert(contains(scoped, "<div>"));
}


// ---------------------------------------------------------------------------
// Phase 18: percentage resolution and overflow clipping
// ---------------------------------------------------------------------------

namespace {

std::shared_ptr<litebird::LayoutBox> build_doc(const std::string& html, float width = 800.0f) {
    using namespace litebird;
    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    resolver.set_viewport(width, 600.0f);
    return LayoutTree::build(doc, resolver, width, 600.0f);
}

bool has_command(const litebird::DisplayList& list,
                 const std::function<bool(const litebird::PaintCommand&)>& predicate) {
    for (const auto& cmd : list.commands()) {
        if (predicate(cmd)) return true;
    }
    return false;
}

} // namespace

TEST(percentage_lengths_resolve_against_containing_block) {
    using namespace litebird;

    // width: 50% of a 400px containing block is 200px -- not 50px.
    auto root = build_doc(
        "<div style='width: 400px'>"
        "  <div id='half' style='width: 50%; height: 20px'></div>"
        "  <div id='padded' style='width: 100px; padding: 10px 5%; margin-left: 10%'></div>"
        "  <div id='capped' style='max-width: 25%; height: 10px'></div>"
        "</div>"
        "<div style='width: 300px; height: 200px'>"
        "  <div id='tall' style='width: 10px; height: 50%'></div>"
        "</div>");
    auto half = find_box_by_id(root, "half");
    auto padded = find_box_by_id(root, "padded");
    auto capped = find_box_by_id(root, "capped");
    auto tall = find_box_by_id(root, "tall");
    assert(half && padded && capped && tall);

    assert(std::abs(half->dimensions.content.width - 200.0f) < 0.5f);

    // Percentage padding and margins resolve against the containing block's
    // *width* (400px), giving 20px each side and a 40px left margin.
    assert(std::abs(padded->dimensions.padding.left - 20.0f) < 0.5f);
    assert(std::abs(padded->dimensions.padding.right - 20.0f) < 0.5f);
    assert(std::abs(padded->dimensions.margin.left - 40.0f) < 0.5f);

    // max-width: 25% caps an otherwise auto-width box at 25% of 400 = 100px,
    // and percentage heights resolve against a definite containing height.
    assert(std::abs(capped->dimensions.content.width - 100.0f) < 0.5f);
    assert(std::abs(tall->dimensions.content.height - 100.0f) < 0.5f);

    // Table cells take percentage widths against the table's own width.
    auto table_root = build_doc(
        "<table style='width: 300px'><tr>"
        "  <td id='quarter' style='width: 25%'>a</td><td>b</td><td>c</td><td>d</td>"
        "</tr></table>");
    auto quarter = find_box_by_id(table_root, "quarter");
    assert(quarter);
    assert(std::abs(quarter->dimensions.margin_box().width - 75.0f) < 1.0f);
}

TEST(compound_assignment_and_update_operators) {
    using namespace litebird;

    // `+=`, `-=`, `*=`, `/=`, `%=`, `++` and `--` had no token types at all
    // before this -- the parser saw `s` `+` `=` and reported "Unexpected token
    // in JS expression: =". They are covered here for BOTH execution paths,
    // because the CLI exercises the bytecode VM while <script> tags and the
    // test harness use the AST interpreter, and the two must not diverge.
    const char* program = R"(
        var a = 0;  a += 5;
        var b = 10; b -= 3;
        var c = 4;  c *= 3;
        var d = 12; d /= 4;
        var e = 13; e %= 5;
        var s = "x"; s += "y";
        var mixed = "n" + 1; mixed += 2;

        var post = 1; var postResult = post++;
        var pre  = 1; var preResult  = ++pre;
        var postDec = 5; var postDecResult = postDec--;
        var preDec  = 5; var preDecResult  = --preDec;

        var obj = { n: 1 }; obj.n += 10; obj.n++;
        var arr = [1]; arr[0] *= 7; arr[0]--;

        var acc = 0;
        for (var i = 0; i < 5; i++) { acc += i; }
    )";

    struct Expected { const char* name; const char* value; };
    const Expected expected[] = {
        {"a", "5"},   {"b", "7"},   {"c", "12"},  {"d", "3"},   {"e", "3"},
        {"s", "xy"},
        // `+=` must concatenate, and `"n"+1` is already the string "n1".
        {"mixed", "n12"},
        // Postfix yields the OLD value; prefix yields the NEW one.
        {"post", "2"}, {"postResult", "1"},
        {"pre", "2"},  {"preResult", "2"},
        {"postDec", "4"}, {"postDecResult", "5"},
        {"preDec", "4"},  {"preDecResult", "4"},
        {"acc", "10"},   // 0+1+2+3+4
    };

    // Path 1: bytecode VM (what the CLI runs).
    JsInterpreter vm;
    vm.eval(program);
    for (const auto& exp : expected) {
        std::string got = vm.global_env()->get(exp.name).to_string();
        if (got != exp.value) {
            std::cerr << "bytecode VM: " << exp.name << " expected "
                      << exp.value << " got " << got << "\n";
        }
        assert(got == exp.value);
    }

    // Path 2: AST interpreter. Same source, same answers.
    JsInterpreter ast;
    ast.eval_ast(program);
    for (const auto& exp : expected) {
        std::string got = ast.global_env()->get(exp.name).to_string();
        if (got != exp.value) {
            std::cerr << "AST interpreter: " << exp.name << " expected "
                      << exp.value << " got " << got << "\n";
        }
        assert(got == exp.value);
    }

    // `obj.n` and `arr[0]` must really have been written through.
    // 1 + 10 (compound) then ++ (update) => 12.
    assert(ast.global_env()->get("obj").get_property("n").to_string() == "12");
    assert(ast.global_env()->get("arr").get_property("0").to_string() == "6");
}

TEST(deeply_nested_markup_does_not_overflow_the_stack) {
    using namespace litebird;

    // `LayoutTree::build_box` recurses once per nesting level and kept a
    // 920-byte ComputedStyle alive across each recursive call, so an ordinary
    // 8 MB stack was exhausted at roughly 3,000 levels of nesting. The process
    // died mid-layout with no diagnostic; parsing and DOM dumping alone were
    // fine, which is what made it look like a parser bug.
    //
    // The limit is the remaining native stack, not a fixed depth, because the
    // per-frame cost differs ~12x between an -O2 and an ASan build. This depth
    // is deliberately far past what an 8 MB stack can hold uninstrumented, so
    // the test exercises the guard rather than the payload.
    const int depth = 6000;
    std::string html;
    html.reserve(static_cast<size_t>(depth) * 11 + 8);
    for (int i = 0; i < depth; ++i) html += "<div>";
    html += "x";
    for (int i = 0; i < depth; ++i) html += "</div>";

    auto doc = HtmlParser::parse(html);
    assert(doc != nullptr);

    // Verify the whole nesting survived tree construction, without recursively
    // walking the DOM from the test itself.
    auto current = std::static_pointer_cast<Node>(doc->body());
    int parsed_depth = 0;
    while (current) {
        std::shared_ptr<Node> nested;
        for (const auto& child : current->children()) {
            if (child->is_element() && child->as_element()->tag_name() == "div") {
                nested = child;
                break;
            }
        }
        if (!nested) break;
        ++parsed_depth;
        current = std::move(nested);
    }
    assert(parsed_depth == depth);

    // Building the box tree must terminate without a stack overflow.
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
    assert(root != nullptr);

    DisplayList::build(root);   // painting must survive it too

    // The limit must not truncate ordinary documents: a normal depth still
    // produces the full tree.
    auto shallow = HtmlParser::parse("<div><div><div><p>hi</p></div></div></div>");
    auto shallow_root = LayoutTree::build(shallow, resolver, 800.0f, 600.0f);
    assert(shallow_root != nullptr);
    assert(!shallow_root->children.empty());
}

TEST(runaway_js_recursion_reports_range_error) {
    using namespace litebird;

    // Every JS call nests several native frames in the AST interpreter, with no
    // depth limit, so `f(2000)` overflowed the C++ stack and killed the process.
    // It must now stop before the stack dies and report a RangeError instead.
    //
    // The stopping depth is deliberately *not* asserted: it is set by how much
    // native stack is left, and one JS level costs about 1 KB in an -O2 build
    // but about 55 KB under AddressSanitizer, so the limit falls from thousands
    // to ~130 between the two. Asserting a number would only encode which build
    // the test happened to run in -- that is what broke the first version of
    // this test. What must hold in every build is that a runaway recursion is
    // reported and the interpreter survives it.

    // A recursion shallow enough to fit in any build still evaluates correctly.
    {
        JsInterpreter js;
        js.eval_ast("function f(n){ if (n === 0) { return 0; } return 1 + f(n - 1); } var r = f(50);");
        assert(js.global_env()->get("r").to_string() == "50");
    }

    // Far past the limit: reports rather than crashing. The result is undefined
    // because the call never completed.
    {
        JsInterpreter js;
        js.eval_ast("function g(n){ if (n === 0) { return 0; } return 1 + g(n - 1); } var r = g(50000);");
        assert(js.global_env()->get("r").is_undefined());
    }

    // Infinitely recursive function: must not hang or crash.
    {
        JsInterpreter js;
        js.eval_ast("function forever(){ return forever(); } var r = forever();");
        assert(js.global_env()->get("r").is_undefined());
    }

    // The guard must leave the interpreter usable: the counter has to be
    // restored on the unwinding path, not just on the success path. If it
    // leaked, this follow-up call would be refused even though the stack is
    // empty again.
    {
        JsInterpreter js;
        js.eval_ast("function g(n){ if (n === 0) { return 0; } return 1 + g(n - 1); } g(50000);");
        js.eval_ast("function f(n){ if (n === 0) { return 0; } return 1 + f(n - 1); } var r = f(50);");
        assert(js.global_env()->get("r").to_string() == "50");
    }
}

TEST(gc_roots_are_complete_under_empty_root_set) {
    using namespace litebird;

    // A `JsObject` is a raw pointer. The collector only keeps one alive if it is
    // reachable from the root set, so anything reachable solely through a
    // closure environment or a queued microtask has to be registered
    // explicitly. It was not: the DevTools pane called collect_garbage() with
    // an empty root list and freed every live object (an ASan
    // heap-use-after-free, reproduced end to end).
    //
    // This test performs exactly that collection -- no explicit roots -- and
    // then touches everything the interpreter still holds.
    {
        JsInterpreter js;
        js.eval_ast(R"(
            var box = { payload: "keepme", nested: { deep: 42 } };
            var list = [box, { a: 1 }, { b: 2 }];
            function makeClosure() { var captured = { retained: true }; return function () { return captured.retained; }; }
            var fn = makeClosure();
            var closureResult = fn();
        )");

        const size_t alive_before = Heap::default_heap().stats().alive_cells;
        assert(alive_before > 0);

        // Empty root set. Registered providers are the only thing keeping
        // script-visible objects alive here.
        Heap::default_heap().collect_garbage();

        // Every one of these dereferences a JsObject. If the root set were
        // incomplete this is a use-after-free, not a failed assertion.
        assert(js.global_env()->get("box").get_property("payload").to_string() == "keepme");
        assert(js.global_env()->get("box").get_property("nested").get_property("deep").to_string() == "42");
        assert(js.global_env()->get("list").get_property("length").to_string() == "3");
        assert(js.global_env()->get("list").get_property("0").get_property("payload").to_string() == "keepme");
        // A value reachable only through a closure environment.
        assert(js.global_env()->get("closureResult").bool_val == true);
        assert(js.global_env()->get("fn").call({}).bool_val == true);

        // Collecting again must be idempotent, not cumulative damage.
        Heap::default_heap().collect_garbage();
        assert(js.global_env()->get("box").get_property("payload").to_string() == "keepme");

        // The garbage we actually produced must still be reclaimed, i.e. the
        // fix must not have turned the collector into a no-op.
        js.eval_ast("for (var i = 0; i < 200; i++) { var junk = { idx: i }; }");
        Heap::default_heap().collect_garbage();
        assert(js.global_env()->get("box").get_property("payload").to_string() == "keepme");
    }

    // Destroying the interpreter must withdraw its provider. A collection
    // afterwards must not call into freed memory.
    Heap::default_heap().collect_garbage();
    assert(Heap::default_heap().stats().alive_cells == 0);
}

TEST(gc_keeps_objects_captured_by_pending_microtasks) {
    using namespace litebird;

    // Promise handlers and queueMicrotask callbacks capture JsValue copies
    // inside std::function storage the collector cannot inspect. Collecting
    // before the queue drains used to free them, and the handler then read
    // freed memory: it silently returned 'undefined' instead of 'keepme'.
    JsInterpreter js;
    js.eval_ast(R"(
        var box = { payload: "keepme" };
        Promise.resolve(box).then(function (v) { microtask_value = v.payload; });
    )");

    // Collect while the callback is still queued.
    Heap::default_heap().collect_garbage();

    EventLoop::default_loop().tick();
    assert(js.global_env()->get("microtask_value").to_string() == "keepme");
}

TEST(strict_equality_does_not_coerce) {
    using namespace litebird;

    // `===` used to be folded into `==` by the lexer, so `1 === "1"` was true.
    // Strict equality must never coerce, compare null/undefined as distinct, and
    // still compare DOM wrappers by node identity rather than wrapper address.
    JsInterpreter js;

    js.eval_ast(R"(
        var n_eq = (1 === 1);
        var n_string = (1 === "1");
        var loose = (1 == "1");
        var null_undef = (null === undefined);
        var nulleq = (null == undefined);
        var zero_false = (0 === false);
        var not_strict = (1 !== "1");

        var el = document.createElement("div");
        var sr = el.attachShadow({ mode: "open" });
        var same_shadow = (el.shadowRoot === sr);
        var self_identity = (el === el);
    )");

    assert(js.global_env()->get("n_eq").bool_val == true);
    assert(js.global_env()->get("n_string").bool_val == false);
    // `==` keeps coercing -- the fix must not have made both operators strict.
    assert(js.global_env()->get("loose").bool_val == true);
    assert(js.global_env()->get("null_undef").bool_val == false);
    assert(js.global_env()->get("zero_false").bool_val == false);
    assert(js.global_env()->get("not_strict").bool_val == true);
    assert(js.global_env()->get("same_shadow").bool_val == true);
    assert(js.global_env()->get("self_identity").bool_val == true);
}

TEST(image_decoder_rejects_hostile_dimensions) {
    using namespace litebird;

    // A 154-byte BMP header claiming 65536 x 65536 pixels overflows the
    // `width * height` product in `int`. Before the guard this either wrapped
    // positive (buffer far smaller than the coordinates written through it) or
    // wrapped negative (uncaught std::length_error -> std::terminate).
    auto bmp_with_dims = [](int32_t w, int32_t h) {
        std::vector<uint8_t> v(54, 0);
        v[0] = 'B'; v[1] = 'M';
        auto put32 = [&](size_t at, uint32_t val) {
            v[at + 0] = static_cast<uint8_t>(val & 0xFF);
            v[at + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
            v[at + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
            v[at + 3] = static_cast<uint8_t>((val >> 24) & 0xFF);
        };
        put32(10, 54);                                   // pixel data offset
        put32(18, static_cast<uint32_t>(w));             // width
        put32(22, static_cast<uint32_t>(h));             // height
        v[28] = 24; v[29] = 0;                           // bpp = 24
        put32(30, 0);                                    // BI_RGB
        return v;
    };

    // Overflow wraps to 0 and to a negative value respectively.
    assert(ImageDecoder::decode(bmp_with_dims(65536, 65536)) == nullptr);
    assert(ImageDecoder::decode(bmp_with_dims(65536, 65535)) == nullptr);
    // Wraps to a small positive count -- the out-of-bounds-write shape.
    assert(ImageDecoder::decode(bmp_with_dims(65537, 65537)) == nullptr);
    // INT32_MIN height: std::abs would overflow.
    assert(ImageDecoder::decode(bmp_with_dims(4, INT32_MIN)) == nullptr);
    // Absurd but non-overflowing dimensions must still be refused.
    assert(ImageDecoder::decode(bmp_with_dims(100000, 100000)) == nullptr);

    // A well-formed 2x2 24bpp BMP still decodes, so the guard is not over-broad.
    auto good = bmp_with_dims(2, 2);
    good.resize(54 + 16, 0x41);
    auto img = ImageDecoder::decode(good);
    assert(img != nullptr);
    assert(img->width == 2 && img->height == 2);
    assert(img->pixels.size() == 4);
}

TEST(overflow_hidden_clips_painting) {
    using namespace litebird;

    // A 300x200 child inside a 100x50 overflow:hidden box must be clipped to
    // the 100x50 padding box; without clipping the red child paints over the
    // whole area.
    auto root = build_doc(
        "<div style='width: 100px; height: 50px; overflow: hidden'>"
        "  <div style='width: 300px; height: 200px; background: rgb(255, 0, 0)'></div>"
        "</div>");
    auto list = DisplayList::build(root);

    int clips = 0;
    int unclips = 0;
    for (const auto& cmd : list.commands()) {
        if (std::holds_alternative<PushClipCommand>(cmd)) ++clips;
        if (std::holds_alternative<PopClipCommand>(cmd)) ++unclips;
    }
    assert(clips == 1);
    assert(unclips == 1);

    BitmapRenderer renderer(160, 120);
    renderer.clear(Color::white());
    renderer.render(list);

    // Inside the clipping box: the red child is visible.
    const Color inside = renderer.get_pixel(10, 10);
    assert(inside.r > 200 && inside.g < 60 && inside.b < 60);

    // Outside it: the child's paint was clipped away.
    const Color outside_right = renderer.get_pixel(130, 10);
    assert(outside_right.r > 240 && outside_right.g > 240 && outside_right.b > 240);
    const Color outside_bottom = renderer.get_pixel(10, 90);
    assert(outside_bottom.r > 240 && outside_bottom.g > 240 && outside_bottom.b > 240);

    // overflow: visible emits no clip at all, so the same child leaks out.
    auto bleeding_root = build_doc(
        "<div style='width: 100px; height: 50px'>"
        "  <div style='width: 300px; height: 200px; background: rgb(255, 0, 0)'></div>"
        "</div>");
    auto bleeding = DisplayList::build(bleeding_root);
    assert(!has_command(bleeding, [](const PaintCommand& cmd) {
        return std::holds_alternative<PushClipCommand>(cmd);
    }));

    BitmapRenderer bleeding_renderer(160, 120);
    bleeding_renderer.clear(Color::white());
    bleeding_renderer.render(bleeding);
    const Color leaked = bleeding_renderer.get_pixel(130, 10);
    assert(leaked.r > 200 && leaked.g < 60 && leaked.b < 60);
}

int main() {
    std::cout << "\n=== Running Litebird Browser Engine Test Suite ===\n\n";

    RUN_TEST(color_parsing);
    RUN_TEST(html_parser);
    RUN_TEST(css_parser_and_specificity);
    RUN_TEST(style_resolver_and_cascade);
    RUN_TEST(layout_box_tree);
    RUN_TEST(javascript_engine);
    RUN_TEST(bytecode_virtual_machine);
    RUN_TEST(flexbox_layout);
    RUN_TEST(text_wrapping);
    RUN_TEST(anti_aliased_rasterizer_and_shadows);
    RUN_TEST(inline_formatting_context);
    RUN_TEST(garbage_collector_and_cycles);
    RUN_TEST(dom_event_target_and_bubbling);
    RUN_TEST(event_loop_and_timers);
    RUN_TEST(resource_loader_and_url_resolution);
    RUN_TEST(browser_shell_navigation_and_hit_testing);
    RUN_TEST(css_positioning_and_stacking_contexts);
    RUN_TEST(box_sizing_and_constraints);
    RUN_TEST(javascript_prototype_chain_and_arrays);
    RUN_TEST(incremental_layout_and_invalidation);
    RUN_TEST(js_async_promises_and_json);
    RUN_TEST(css_flex_wrapping_and_grid_layout);
    RUN_TEST(dom_3phase_events_and_form_controls);
    RUN_TEST(image_decoding_and_rendering);
    RUN_TEST(js_template_literals_and_arrow_functions);
    RUN_TEST(web_storage_and_cookies);
    RUN_TEST(html5_canvas_2d_context);
    RUN_TEST(css_media_queries);
    RUN_TEST(css_pseudo_classes);
    RUN_TEST(svg_vector_rendering);
    RUN_TEST(es6_map_set_and_destructuring);
    RUN_TEST(css_transitions_and_animations);
    RUN_TEST(css_2d_transforms);
    RUN_TEST(canvas_gradients_and_curves);
    RUN_TEST(es6_classes_and_inheritance);
    RUN_TEST(async_await_language_sugar);
    RUN_TEST(css_variables_and_cascade);
    RUN_TEST(css_calc_expressions);
    RUN_TEST(fetch_api_and_headers);
    RUN_TEST(url_and_url_search_params);
    RUN_TEST(web_workers_and_concurrent_execution);
    RUN_TEST(css_flexbox_and_grid_modern_refinements);
    RUN_TEST(websocket_rfc6455_client_and_handshake);
    RUN_TEST(mutation_observer_api);
    RUN_TEST(node_connected_state_tracks_attach_detach_and_shadow_hosts);
    RUN_TEST(custom_elements_api);
    RUN_TEST(shadow_dom_and_slot_projection);
    RUN_TEST(intersection_and_resize_observers);
    RUN_TEST(history_api_and_client_routing);
    RUN_TEST(css_filters_and_canvas_blending);
    RUN_TEST(js_generators_iterators_and_for_of);
    RUN_TEST(abort_controller_and_fetch_cancellation);
    RUN_TEST(css_grid_template_areas_and_containment);
    RUN_TEST(html5_tree_construction_implied_structure);
    RUN_TEST(html5_foster_parenting_and_adoption_agency);
    RUN_TEST(html5_comments_and_character_references);
    RUN_TEST(html5_template_contents_and_fragments);
    RUN_TEST(css_shorthand_expansion_and_important);
    RUN_TEST(css_relative_units);
    RUN_TEST(float_layout_and_text_wrapping);
    RUN_TEST(float_clearance_and_bug_containment);
    RUN_TEST(table_layout_columns_and_rows);
    RUN_TEST(script_data_escaped_and_double_escaped_states);
    RUN_TEST(pre_and_textarea_leading_newline_handling);
    RUN_TEST(adoption_agency_nobr_and_nested_formatting);
    RUN_TEST(select_content_parsing_and_foreign_end_tags);
    RUN_TEST(svg_attribute_adjustment_and_character_reference_edges);
    RUN_TEST(template_and_scope_helpers_ignore_foreign_elements);
    RUN_TEST(percentage_lengths_resolve_against_containing_block);
    RUN_TEST(compound_assignment_and_update_operators);
    RUN_TEST(deeply_nested_markup_does_not_overflow_the_stack);
    RUN_TEST(runaway_js_recursion_reports_range_error);
    RUN_TEST(gc_roots_are_complete_under_empty_root_set);
    RUN_TEST(gc_keeps_objects_captured_by_pending_microtasks);
    RUN_TEST(strict_equality_does_not_coerce);
    RUN_TEST(image_decoder_rejects_hostile_dimensions);
    RUN_TEST(overflow_hidden_clips_painting);
    RUN_TEST(float_shrinks_inline_line_boxes);
    RUN_TEST(float_left_and_right_can_stack_side_by_side);

    std::cout << "\n\033[1;32mAll " << g_tests_run
              << " engine test suites passed successfully!\033[0m\n\n";
    return 0;
}
