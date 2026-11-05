#include "litebird/common/Color.hpp"
#include "litebird/common/Geometry.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/html/HtmlParser.hpp"
#include "litebird/css/CssParser.hpp"
#include "litebird/css/StyleResolver.hpp"
#include "litebird/layout/LayoutTree.hpp"
#include "litebird/render/PaintCommand.hpp"
#include "litebird/render/ImageDecoder.hpp"
#include "litebird/render/BitmapRenderer.hpp"
#include "litebird/js/JsInterpreter.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/Json.hpp"
#include "litebird/js/Promise.hpp"
#include "litebird/dom/Event.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include "litebird/shell/BrowserShell.hpp"

#include <iostream>
#include <cassert>
#include <string>

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
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

    // Centered: 800px containing width, 200px width -> (800 - 200) / 2 = 300px left and right margins
    assert(center_box->dimensions.margin.left == 300.0f);
    assert(center_box->dimensions.margin.right == 300.0f);
    assert(center_box->dimensions.content.x == 300.0f);
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

    // Initially, layout has executed and flags cleared
    doc->clear_layout_flags();
    parent->clear_layout_flags();
    child1->clear_layout_flags();
    child2->clear_layout_flags();
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

    std::cout << "\n\033[1;32mAll 24 engine test suites passed successfully!\033[0m\n\n";
    return 0;
}
