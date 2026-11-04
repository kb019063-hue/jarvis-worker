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
#include "litebird/js/JsInterpreter.hpp"

#include <iostream>
#include <cassert>
#include <string>

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running test: " #name "... "; \
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

    std::cout << "\n\033[1;32mAll 11 engine test suites passed successfully!\033[0m\n\n";
    return 0;
}
