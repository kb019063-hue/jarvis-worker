#include "litebird/common/Color.hpp"
#include "litebird/common/Geometry.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/dom/AttributeList.hpp"
#include "litebird/xml/XmlParser.hpp"
#include "litebird/xml/XmlTools.hpp"
#include "litebird/xml/XmlDom.hpp"
#include "litebird/net/DataUrl.hpp"
#include <fstream>
#include "litebird/html/HtmlParser.hpp"
#include "litebird/html/HtmlTokenizer.hpp"
#include "litebird/html/HtmlTreeBuilder.hpp"
#include "litebird/html/TreeDump.hpp"
#include "litebird/css/CssParser.hpp"
#include "litebird/css/CssText.hpp"
#include "litebird/css/DocumentStyles.hpp"
#include "litebird/css/StyleResolver.hpp"
#include "litebird/layout/LayoutTree.hpp"
#include "litebird/render/PaintCommand.hpp"
#include "litebird/render/ImageDecoder.hpp"
#include "litebird/render/FontRenderer.hpp"
#include "litebird/render/JpegDecoder.hpp"
#include "litebird/render/GifDecoder.hpp"
#include "litebird/net/Deflate.hpp"
#include "litebird/shell/WebBrowserServer.hpp"
#include <filesystem>
#include <array>
#include <cstdint>
#include <climits>
#include "litebird/render/BitmapRenderer.hpp"
#include "litebird/js/JsInterpreter.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/Json.hpp"
#include "litebird/js/Promise.hpp"
#include "litebird/js/Storage.hpp"
#include "litebird/render/Canvas2D.hpp"
#include "litebird/render/RasterBudget.hpp"
#include "litebird/render/SvgRenderer.hpp"
#include "litebird/dom/Event.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include "litebird/net/UrlParser.hpp"
#include "litebird/shell/BrowserShell.hpp"
#include "litebird/js/Worker.hpp"
#include "litebird/net/WebSocket.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include "litebird/dom/CustomElementRegistry.hpp"
#include "litebird/js/DomBindings.hpp"
#include "litebird/dom/IntersectionObserver.hpp"
#include "litebird/dom/ResizeObserver.hpp"
#include "litebird/dom/History.hpp"
#include "litebird/render/ImageFilter.hpp"
#include "litebird/net/AbortController.hpp"
#include "js_string_stress.hpp"

#if defined(__SANITIZE_ADDRESS__)
// From the sanitizer's runtime (sanitizer/allocator_interface.h, which not
// every compiler installs). Weak: a runtime without it links all the same.
extern "C" void __sanitizer_purge_allocator(void) __attribute__((weak));
#endif

#include <iostream>
#include <cmath>
#include <random>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <set>
#include <sstream>
#include <vector>
#include <cassert>
#include <string>
#include <thread>
#include <chrono>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#define TEST(name) void test_##name()
// Counted rather than hard-coded: the summary line used to drift behind the
// real number of suites whenever a test was added.
static int g_tests_run = 0;

/// Called after every suite. In the sanitizer build it gives memory back;
/// in the normal build it does nothing.
///
/// The suites run in one process and nothing in the engine collects
/// unasked, so what the scripts of a suite made stays until the process
/// ends. AddressSanitizer adds to that: every block that is freed is kept
/// for a while (256 MB of quarantine), and the pages behind freed blocks
/// are not handed back. One suite that nests 100,000 arrays leaves 1.4 GB
/// behind that way, and by the last of the large suites the process had
/// 3.8 GB and was killed on a machine with 4 GB (the sandbox these checks
/// are run in; CI's runners have 16 GB and did not notice).
///
/// So after each suite, when its interpreters are gone: collect what their
/// scripts made, and tell the sanitizer's allocator to return what it holds.
/// A block freed in one suite and used in a later one would no longer be
/// reported; within a suite nothing changes.
#if defined(__SANITIZE_ADDRESS__)
static void give_memory_back() {
    litebird::Heap::default_heap().collect_garbage();
    if (__sanitizer_purge_allocator) __sanitizer_purge_allocator();
}
#else
static void give_memory_back() {}
#endif

#define RUN_TEST(name) do { \
    ++g_tests_run; \
    std::cout << "Running test: " #name "... " << std::flush; \
    test_##name(); \
    give_memory_back(); \
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
    assert(c4.r == 0 && c4.g == 0 && c4.b == 255);
}


TEST(css_color_keywords_match_the_css_specification) {
    using namespace litebird;

    // The accepted value for every CSS color keyword. This is the table that
    // used to hold 19 Bootstrap-looking colors and return black for names it
    // did not know. Keep all 148 here so a regression is not a silent repaint.
    const char* const names[] = { "aliceblue", "antiquewhite", "aqua", "aquamarine", "azure", "beige", "bisque", "black", "blanchedalmond", "blue", "blueviolet", "brown", "burlywood", "cadetblue", "chartreuse", "chocolate", "coral", "cornflowerblue", "cornsilk", "crimson", "cyan", "darkblue", "darkcyan", "darkgoldenrod", "darkgray", "darkgreen", "darkgrey", "darkkhaki", "darkmagenta", "darkolivegreen", "darkorange", "darkorchid", "darkred", "darksalmon", "darkseagreen", "darkslateblue", "darkslategray", "darkslategrey", "darkturquoise", "darkviolet", "deeppink", "deepskyblue", "dimgray", "dimgrey", "dodgerblue", "firebrick", "floralwhite", "forestgreen", "fuchsia", "gainsboro", "ghostwhite", "gold", "goldenrod", "gray", "green", "greenyellow", "grey", "honeydew", "hotpink", "indianred", "indigo", "ivory", "khaki", "lavender", "lavenderblush", "lawngreen", "lemonchiffon", "lightblue", "lightcoral", "lightcyan", "lightgoldenrodyellow", "lightgray", "lightgreen", "lightgrey", "lightpink", "lightsalmon", "lightseagreen", "lightskyblue", "lightslategray", "lightslategrey", "lightsteelblue", "lightyellow", "lime", "limegreen", "linen", "magenta", "maroon", "mediumaquamarine", "mediumblue", "mediumorchid", "mediumpurple", "mediumseagreen", "mediumslateblue", "mediumspringgreen", "mediumturquoise", "mediumvioletred", "midnightblue", "mintcream", "mistyrose", "moccasin", "navajowhite", "navy", "oldlace", "olive", "olivedrab", "orange", "orangered", "orchid", "palegoldenrod", "palegreen", "paleturquoise", "palevioletred", "papayawhip", "peachpuff", "peru", "pink", "plum", "powderblue", "purple", "rebeccapurple", "red", "rosybrown", "royalblue", "saddlebrown", "salmon", "sandybrown", "seagreen", "seashell", "sienna", "silver", "skyblue", "slateblue", "slategray", "slategrey", "snow", "springgreen", "steelblue", "tan", "teal", "thistle", "tomato", "turquoise", "violet", "wheat", "white", "whitesmoke", "yellow", "yellowgreen" };
    const uint32_t rgb[] = { 0xf0f8ff, 0xfaebd7, 0x00ffff, 0x7fffd4, 0xf0ffff, 0xf5f5dc, 0xffe4c4, 0x000000, 0xffebcd, 0x0000ff, 0x8a2be2, 0xa52a2a, 0xdeb887, 0x5f9ea0, 0x7fff00, 0xd2691e, 0xff7f50, 0x6495ed, 0xfff8dc, 0xdc143c, 0x00ffff, 0x00008b, 0x008b8b, 0xb8860b, 0xa9a9a9, 0x006400, 0xa9a9a9, 0xbdb76b, 0x8b008b, 0x556b2f, 0xff8c00, 0x9932cc, 0x8b0000, 0xe9967a, 0x8fbc8f, 0x483d8b, 0x2f4f4f, 0x2f4f4f, 0x00ced1, 0x9400d3, 0xff1493, 0x00bfff, 0x696969, 0x696969, 0x1e90ff, 0xb22222, 0xfffaf0, 0x228b22, 0xff00ff, 0xdcdcdc, 0xf8f8ff, 0xffd700, 0xdaa520, 0x808080, 0x008000, 0xadff2f, 0x808080, 0xf0fff0, 0xff69b4, 0xcd5c5c, 0x4b0082, 0xfffff0, 0xf0e68c, 0xe6e6fa, 0xfff0f5, 0x7cfc00, 0xfffacd, 0xadd8e6, 0xf08080, 0xe0ffff, 0xfafad2, 0xd3d3d3, 0x90ee90, 0xd3d3d3, 0xffb6c1, 0xffa07a, 0x20b2aa, 0x87cefa, 0x778899, 0x778899, 0xb0c4de, 0xffffe0, 0x00ff00, 0x32cd32, 0xfaf0e6, 0xff00ff, 0x800000, 0x66cdaa, 0x0000cd, 0xba55d3, 0x9370db, 0x3cb371, 0x7b68ee, 0x00fa9a, 0x48d1cc, 0xc71585, 0x191970, 0xf5fffa, 0xffe4e1, 0xffe4b5, 0xffdead, 0x000080, 0xfdf5e6, 0x808000, 0x6b8e23, 0xffa500, 0xff4500, 0xda70d6, 0xeee8aa, 0x98fb98, 0xafeeee, 0xdb7093, 0xffefd5, 0xffdab9, 0xcd853f, 0xffc0cb, 0xdda0dd, 0xb0e0e6, 0x800080, 0x663399, 0xff0000, 0xbc8f8f, 0x4169e1, 0x8b4513, 0xfa8072, 0xf4a460, 0x2e8b57, 0xfff5ee, 0xa0522d, 0xc0c0c0, 0x87ceeb, 0x6a5acd, 0x708090, 0x708090, 0xfffafa, 0x00ff7f, 0x4682b4, 0xd2b48c, 0x008080, 0xd8bfd8, 0xff6347, 0x40e0d0, 0xee82ee, 0xf5deb3, 0xffffff, 0xf5f5f5, 0xffff00, 0x9acd32 };
    static_assert(sizeof(names) / sizeof(names[0]) == 148);
    for (size_t i = 0; i < 148; ++i) {
        unsigned value = 0;
        assert(Color::named_color_value(names[i], value));
        assert(value == rgb[i]);
        Color parsed = Color::parse(names[i]);
        assert(parsed.r == ((value >> 16) & 0xFF));
        assert(parsed.g == ((value >> 8) & 0xFF));
        assert(parsed.b == (value & 0xFF));
        assert(parsed.a == 255);
    }
    assert(Color::parse("  RED  ").r == 255 && Color::parse("  RED  ").g == 0);
    assert(Color::parse("transparent") == Color::transparent());
    assert(Color::parse("aqua") == Color::parse("cyan"));
    assert(Color::parse("fuchsia") == Color::parse("magenta"));
    assert(Color::parse("tomato") == Color(255, 99, 71));
    assert(Color::parse("silver") == Color(192, 192, 192));
    assert(Color::parse("definitely-not-a-color") == Color::black());
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

TEST(style_resolver_indexed_candidates_preserve_cascade) {
    using namespace litebird;
    auto doc = HtmlParser::parse(R"(
        <div id="target" class="hot card" data-role="hero"></div>
        <span class="card"></span>
    )");
    auto target = doc->get_element_by_id("target");
    assert(target);

    StyleResolver resolver;
    resolver.add_stylesheet(CssParser::parse(R"(
        * { color: #000000; }
        div { color: #0000ff; }
        .hot { color: #00ff00; }
        div.card { background-color: #eeeeee; }
        [data-role="hero"] { font-weight: 700; }
        #target { color: #ff0000; }
        div:hover { text-decoration: underline; }
    )"));
    resolver.add_stylesheet(CssParser::parse("#target { color: #123456; }"));

    auto style = resolver.resolve_style(target);
    assert(style.color == Color::parse("#123456")); // specificity and sheet order
    assert(style.background_color == Color::parse("#eeeeee")); // indexed compound class
    assert(style.font_weight == 700); // universal fallback bucket for attribute selector
    assert(style.text_decoration == "none"); // candidate must still pass full matching

    // A class attribute is a list separated by any ASCII whitespace, and
    // markup formatted by a tool breaks a long list across lines. The index
    // split the list on spaces only (its tests for the other separators were
    // written '\\t', '\\n'..., which are no characters), so two classes with a
    // newline or a tab between them were looked up as one name, and no rule
    // for either was a candidate.
    for (const std::string separator : {" ", "\n", "\t", "\r\n", "\f", " \n\t "}) {
        auto listed = HtmlParser::parse("<div id=\"t\" class=\"first" + separator + "second\"></div>");
        auto element = listed->get_element_by_id("t");
        assert(element && element->has_class("first") && element->has_class("second"));
        StyleResolver by_class;
        by_class.add_stylesheet(CssParser::parse(
            ".first { color: #010203; } .second { background-color: #040506; } .firstsecond { color: #ffffff; }"));
        auto computed = by_class.resolve_style(element);
        assert(computed.color == Color::parse("#010203"));
        assert(computed.background_color == Color::parse("#040506"));
    }
}

TEST(percentage_font_size_and_line_height) {
    using namespace litebird;
    // Percentages compound down the tree: each level resolves against the
    // parent's computed size, and em lengths on the element resolve against
    // its own. A 50% child of 16px is 8px, and 50% of that is 4px.
    auto doc = HtmlParser::parse(R"(
        <div id="outer" style="font-size: 16px;">
            <div id="half" style="font-size: 50%;"><span id="quarter" style="font-size: 50%;">x</span></div>
        </div>
    )");
    StyleResolver resolver;
    auto half = doc->get_element_by_id("half");
    auto quarter = doc->get_element_by_id("quarter");
    assert(half && quarter);
    auto half_style = resolver.resolve_style(half, nullptr);
    assert(half_style.font_size == 8.0f);
    auto quarter_style = resolver.resolve_style(quarter, &half_style);
    assert(quarter_style.font_size == 4.0f);

    // line-height keywords, numbers, percentages and lengths. The used value
    // multiplies the element's own font size; percentages resolve where they
    // were declared.
    auto line_height_of = [&](const std::string& declaration, float font_size_px) {
        auto element = HtmlParser::parse("<p id=\"p\">x</p>");
        auto p = element->get_element_by_id("p");
        StyleResolver scoped;
        scoped.add_stylesheet(CssParser::parse(
            "#p { font-size: " + std::to_string(static_cast<int>(font_size_px)) + "px; line-height: " + declaration + "; }"));
        return scoped.resolve_style(p, nullptr);
    };

    // normal: not declared -> the 1.25 fallback the renderer always used
    assert(line_height_of("normal", 16.0f).effective_line_height() == 20.0f);
    // unchanged font size: normal tracks the element's size
    assert(line_height_of("normal", 20.0f).effective_line_height() == 25.0f);
    // bare number: a multiplier of the element's own font size
    assert(line_height_of("1.5", 16.0f).effective_line_height() == 24.0f);
    assert(line_height_of("2", 20.0f).effective_line_height() == 40.0f);
    // percentage: of the font size where it was declared
    assert(line_height_of("150%", 16.0f).effective_line_height() == 24.0f);
    assert(line_height_of("37.5%", 16.0f).effective_line_height() == 6.0f);
    // absolute length
    assert(line_height_of("40px", 16.0f).effective_line_height() == 40.0f);

    // Inheritance: a declared multiplier is inherited as a multiplier (the
    // child multiplies its own font size); a declared percentage is resolved
    // once and inherited as a length; a declared length is inherited as a length.
    auto inheritance_doc = HtmlParser::parse(R"(
        <div id="parent"><span id="child">x</span></div>
    )");
    auto parent = inheritance_doc->get_element_by_id("parent");
    auto child = inheritance_doc->get_element_by_id("child");
    assert(parent && child);

    StyleResolver multiplier;
    multiplier.add_stylesheet(CssParser::parse("#parent { font-size: 16px; line-height: 1.5; } #child { font-size: 20px; }"));
    auto parent_style = multiplier.resolve_style(parent, nullptr);
    auto child_style = multiplier.resolve_style(child, &parent_style);
    assert(child_style.effective_line_height() == 30.0f); // 1.5 * 20px, not 24px

    StyleResolver percentage;
    percentage.add_stylesheet(CssParser::parse("#parent { font-size: 16px; line-height: 150%; } #child { font-size: 20px; }"));
    parent_style = percentage.resolve_style(parent, nullptr);
    child_style = percentage.resolve_style(child, &parent_style);
    assert(child_style.effective_line_height() == 24.0f); // inherited as a length

    StyleResolver length;
    length.add_stylesheet(CssParser::parse("#parent { font-size: 16px; line-height: 40px; } #child { font-size: 20px; }"));
    parent_style = length.resolve_style(parent, nullptr);
    child_style = length.resolve_style(child, &parent_style);
    assert(child_style.effective_line_height() == 40.0f);

    // normal is not declared and stays undeclared down the tree
    StyleResolver normal;
    normal.add_stylesheet(CssParser::parse("#parent { font-size: 16px; } #child { font-size: 20px; }"));
    parent_style = normal.resolve_style(parent, nullptr);
    child_style = normal.resolve_style(child, &parent_style);
    assert(child_style.line_height < 0.0f && child_style.line_height_number < 0.0f);
    assert(child_style.effective_line_height() == 25.0f); // 1.25 * own font size

    // font: size/line-height shorthand feeds both properties
    StyleResolver shorthand;
    shorthand.add_stylesheet(CssParser::parse("#parent { font: 16px/2 serif; }"));
    parent_style = shorthand.resolve_style(parent, nullptr);
    assert(parent_style.font_size == 16.0f);
    assert(parent_style.effective_line_height() == 32.0f);

    // Order independence: line-height: <percentage> must see the element's
    // own font size even when font-size is declared after it on the same
    // element, in the stylesheet and in the longhand pass.
    StyleResolver reversed;
    reversed.add_stylesheet(CssParser::parse("#parent { line-height: 150%; font-size: 20px; }"));
    parent_style = reversed.resolve_style(parent, nullptr);
    assert(parent_style.effective_line_height() == 30.0f); // 1.5 * 20px

    // The layout stage actually uses the property: a paragraph with
    // line-height: 48px lays its line boxes out 48px apart, replacing the
    // hardcoded 1.25x estimate.
    auto layout_doc = HtmlParser::parse(R"(
        <p id="p" style="width: 500px; font-size: 16px; line-height: 48px;">
            one two three four five six seven eight nine ten eleven twelve
        </p>
    )");
    StyleResolver laid;
    auto p = layout_doc->get_element_by_id("p");
    auto root = LayoutTree::build(layout_doc, laid, 800.0f, 600.0f);
    assert(root);
    Node* wanted = static_cast<Node*>(p.get());
    auto find_p = [](const auto& self, const std::shared_ptr<LayoutBox>& box, Node* wanted) -> std::shared_ptr<LayoutBox> {
        if (!box) return nullptr;
        if (box->node.get() == wanted) return box;
        for (const auto& ch : box->children) {
            if (auto found = self(self, ch, wanted)) return found;
        }
        return nullptr;
    };
    auto paragraph = find_p(find_p, root, wanted);
    assert(paragraph);
    assert(!paragraph->line_boxes.empty());
    for (const auto& line : paragraph->line_boxes) {
        assert(line.rect.height == 48.0f);
    }
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
    auto gc_info = gc_fn.native_function()({});
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

    // 3. The same through the node's script object. The methods live on
    // EventTarget.prototype, so they are called on the object, and what is
    // dispatched is an Event, as in a browser.
    JsValue js_btn = JsValue::make_dom_node(button);
    auto add_listener_fn = js_btn.get_property("addEventListener");
    assert(add_listener_fn.type == JsType::NativeFunction);

    int js_event_fired = 0;
    add_listener_fn.call_with_this(js_btn, {
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
    JsValue custom_event = dom::current_realm().ctor("Event").construct({JsValue::make_string("custom")});
    dispatch_fn.call_with_this(js_btn, {custom_event});
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
    auto resp = ResourceLoader::load_navigation("examples/hello.html");
    assert(resp.success);
    assert(resp.status_code == 200);
    assert(resp.content_type == "text/html");
    assert(!resp.body.empty());

    // 4. Missing file error handling
    auto missing_resp = ResourceLoader::load_navigation("examples/does_not_exist_xyz.html");
    assert(!missing_resp.success);
    assert(missing_resp.status_code == 404);

    // Caller-controlled request-line/header components must be rejected before
    // any network lookup or socket operation.
    auto injected_method = ResourceLoader::load_http("http://127.0.0.1/", "GET\r\nX-Injected: yes");
    assert(!injected_method.success);
    assert(injected_method.error_message.find("method") != std::string::npos);
    auto injected_header = ResourceLoader::load_http("http://127.0.0.1/", "GET", {{"X-Test", "ok\r\nX-Injected: yes"}});
    assert(!injected_header.success);
    assert(injected_header.error_message.find("header") != std::string::npos);
    auto injected_target = ResourceLoader::load_http("http://127.0.0.1/path\r\nX-Injected: yes");
    assert(!injected_target.success);

    // A tiny loopback server lets us assert the response cap without external
    // network dependencies.
    auto fetch_from_local_server = [](const std::string& wire_response) {
        const int server = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(server >= 0);
        int reuse = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        assert(bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        assert(listen(server, 1) == 0);
        socklen_t address_size = sizeof(address);
        assert(getsockname(server, reinterpret_cast<sockaddr*>(&address), &address_size) == 0);
        const auto port = ntohs(address.sin_port);
        std::thread peer([server, &wire_response]() {
            const int client = accept(server, nullptr, nullptr);
            if (client >= 0) {
                char request_buffer[2048];
                (void)recv(client, request_buffer, sizeof(request_buffer), 0);
                std::size_t offset = 0;
                while (offset < wire_response.size()) {
                    const auto count = send(client, wire_response.data() + offset,
                                            wire_response.size() - offset, MSG_NOSIGNAL);
                    if (count <= 0) break;
                    offset += static_cast<std::size_t>(count);
                }
                close(client);
            }
            close(server);
        });
        auto response = ResourceLoader::load_http("http://127.0.0.1:" + std::to_string(port) + "/");
        peer.join();
        return response;
    };

    auto accepted_response = fetch_from_local_server("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    assert(accepted_response.success);
    assert(accepted_response.body == "ok");
    std::string oversized_response = "HTTP/1.1 200 OK\r\nContent-Length: " +
        std::to_string(ResourceLoader::MaxHttpResponseBytes) + "\r\n\r\n";
    oversized_response.append(ResourceLoader::MaxHttpResponseBytes, 'x');
    auto rejected_response = fetch_from_local_server(oversized_response);
    assert(!rejected_response.success);
    assert(rejected_response.error_message.find("16 MiB") != std::string::npos);
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
    // `then` lives on Promise.prototype, so the receiver has to be supplied.
    JsValue p_chained = p.get_property("then").call_with_this(p, {
        JsValue::make_native_function([&step1](const std::vector<JsValue>& args) {
            step1 = static_cast<int>(args[0].to_number()) * 2; // 20
            return JsValue::make_number(step1);
        })
    });
    p_chained.get_property("then").call_with_this(p_chained, {
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
    all_res.get_property("then").call_with_this(all_res, {
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

    // As in a browser, focus() does nothing to a disabled control or to one
    // that is not in a document.
    js_node.get_property("focus").call_with_this(js_node, {});
    assert(!focused);
    auto focus_doc = std::make_shared<Document>();
    auto focus_body = std::make_shared<Element>("body");
    focus_doc->append_child(focus_body);
    focus_body->append_child(input_el);
    js_node.get_property("focus").call_with_this(js_node, {});
    assert(!focused);
    input_el->set_disabled(false);

    js_node.get_property("focus").call_with_this(js_node, {});
    assert(focused);
    assert(focus_doc->focused_element() == input_el);

    js_node.get_property("blur").call_with_this(js_node, {});
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
            // A getter, as the standard reads it: define() takes the list
            // from the property. (This test used to declare a method, which
            // only the old registry accepted; a browser throws on it.)
            static get observedAttributes() {
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

    // Test with JS interpreter. The page needs an address of its own: one
    // at about:blank has no paths to move between, and pushState("/js1")
    // there is a SecurityError, as in a browser.
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/app/");
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

    // back() only asks for the move, as in a browser: the page is at the
    // same entry when the call returns, and popstate comes in a later task.
    assert(interp.eval("popCount").to_number() == 0.0);
    assert(interp.eval("location.pathname").to_string() == "/js2");
    EventLoop::default_loop().tick();

    auto pop_count = interp.eval("popCount");
    assert(pop_count.to_number() == 1.0);
    assert(interp.eval("location.pathname").to_string() == "/js1");
    auto last_state = interp.eval("lastState");
    assert(last_state.to_string() == "js-page-1");
}

// A URL as text and back, for the tests below: the href `reference` makes
// against `base`, or "(not a URL)".
static std::string url_href(const std::string& reference, const std::string& base = "") {
    litebird::Url url;
    return litebird::Url::parse(reference, base, url) ? url.href() : std::string("(not a URL)");
}

TEST(url_parser_follows_the_url_standard) {
    using namespace litebird;
    const std::string page = "https://example.test/app/page.html?tab=2#top";

    // Relative references: each keeps as much of the base as it does not replace.
    assert(url_href("sub/file.html", page) == "https://example.test/app/sub/file.html");
    assert(url_href("?q=1", page) == "https://example.test/app/page.html?q=1");
    assert(url_href("#frag", page) == "https://example.test/app/page.html?tab=2#frag");
    assert(url_href("", page) == "https://example.test/app/page.html?tab=2");
    assert(url_href("//cdn.test/lib.js", page) == "https://cdn.test/lib.js");
    assert(url_href("/send", page) == "https://example.test/send");
    assert(url_href("../a/b?x=1&y=2#h", page) == "https://example.test/a/b?x=1&y=2#h");
    assert(url_href("../../../up", page) == "https://example.test/up");
    assert(url_href(".", page) == "https://example.test/app/");
    assert(url_href("https:other.html", page) == "https://example.test/app/other.html");
    assert(url_href("http://other.test", page) == "http://other.test/");

    // What is not a URL.
    assert(url_href("nope") == "(not a URL)");
    assert(url_href("/path/only") == "(not a URL)");
    assert(url_href("https://") == "(not a URL)");
    assert(url_href("https://a.test:99999/") == "(not a URL)");
    assert(url_href("https://a.test:8x/") == "(not a URL)");
    assert(url_href("https://[::1/") == "(not a URL)");
    assert(url_href("https://a b/") == "(not a URL)");
    assert(url_href("https://1.2.3.4.5/") == "(not a URL)");
    assert(url_href("x", "mailto:ann@x.test") == "(not a URL)");
    assert(url_href("#only-this", "mailto:ann@x.test") == "mailto:ann@x.test#only-this");
    assert(url_href("x", "not a base") == "(not a URL)");

    // One spelling for each URL.
    assert(url_href("HTTP://EXAMPLE.test:80/a/./b/../c") == "http://example.test/a/c");
    assert(url_href("https://example.test:443") == "https://example.test/");
    assert(url_href("https://example.test:8443") == "https://example.test:8443/");
    assert(url_href("http:\\\\x.test\\a\\b") == "http://x.test/a/b");
    assert(url_href("  https://a.test/x y?q= z#f g \n") == "https://a.test/x%20y?q=%20z#f%20g");
    assert(url_href("https://a.test/a\tb\nc") == "https://a.test/abc");
    assert(url_href("https://a.test/%7Efoo/%2e%2E/bar") == "https://a.test/bar");
    assert(url_href("https://user:pa ss@a.test/") == "https://user:pa%20ss@a.test/");
    assert(url_href("https://a.test/?q='x'") == "https://a.test/?q=%27x%27");
    assert(url_href("custom://a.test/?q='x'") == "custom://a.test/?q='x'");
    assert(url_href("https://a.test/\xC3\xA9?\xC3\xA9#\xC3\xA9") == "https://a.test/%C3%A9?%C3%A9#%C3%A9");

    // Hosts that are numbers.
    assert(url_href("http://0x7f.1/") == "http://127.0.0.1/");
    assert(url_href("http://192.168.0.257/") == "(not a URL)");
    assert(url_href("http://3232235521/") == "http://192.168.0.1/");
    assert(url_href("http://[0:0:0:0:0:0:0:1]:8080/") == "http://[::1]:8080/");
    assert(url_href("http://[2001:DB8:0:0:1:0:0:1]/") == "http://[2001:db8::1:0:0:1]/");
    assert(url_href("http://[::ffff:192.168.0.1]/") == "http://[::ffff:c0a8:1]/");
    assert(url_href("http://[1:2:3:4:5:6:7:8:9]/") == "(not a URL)");

    // Addresses that are not places on a network have one opaque path.
    Url mail;
    assert(Url::parse("mailto:ann@x.test?subject=hi", nullptr, mail));
    assert(mail.protocol() == "mailto:" && mail.pathname() == "ann@x.test" && mail.hostname().empty());
    assert(mail.search() == "?subject=hi" && mail.origin() == "null" && mail.opaque_path);
    assert(url_href("javascript:void(0)") == "javascript:void(0)");
    assert(url_href("data:text/plain,a b#c d") == "data:text/plain,a b#c%20d");
    assert(url_href("about:blank") == "about:blank");

    // Files.
    assert(url_href("file:///C|/dir/x.txt") == "file:///C:/dir/x.txt");
    assert(url_href("file://localhost/etc/hosts") == "file:///etc/hosts");
    assert(url_href("other.html", "file:///home/me/page.html") == "file:///home/me/other.html");
    assert(url_href("/abs", "file:///C:/dir/page.html") == "file:///C:/abs");
    assert(file_url_from_path("/home/a b/x#1%.html") == "file:///home/a%20b/x%231%25.html");
    assert(url_href(file_url_from_path("/home/a b/x?y.html")) == "file:///home/a%20b/x%3Fy.html");

    // Origins.
    Url url;
    assert(Url::parse("https://a.test:8443/x", nullptr, url) && url.origin() == "https://a.test:8443");
    assert(Url::parse("blob:https://a.test/1234-5678", nullptr, url) && url.origin() == "https://a.test");
    assert(Url::parse("file:///x", nullptr, url) && url.origin() == "null");

    // Assigning to a part parses the value as that part, and changes nothing
    // when it cannot be one.
    assert(Url::parse("https://example.test/app/page.html?tab=2#top", nullptr, url));
    url.set_pathname("/new path");
    assert(url.pathname() == "/new%20path");
    url.set_hash("");
    assert(url.hash().empty() && url.href() == "https://example.test/new%20path?tab=2");
    url.set_hash("a b");
    assert(url.hash() == "#a%20b");
    url.set_search("q=1 2");
    assert(url.search() == "?q=1%202");
    url.set_search("");
    assert(!url.has_query && url.href() == "https://example.test/new%20path#a%20b");
    url.set_port("8080");
    assert(url.host_and_port() == "example.test:8080");
    url.set_port("x");
    assert(url.port_text() == "8080");
    url.set_port("443");
    assert(url.port_text().empty());
    url.set_host("b.test:81");
    assert(url.hostname() == "b.test" && url.port_text() == "81");
    url.set_hostname("c.test:99");                      // a port is not part of a host name
    assert(url.hostname() == "b.test");
    url.set_hostname("");                               // a web address must have a host
    assert(url.hostname() == "b.test");
    url.set_protocol("mailto");                         // nor can it stop being a web address
    assert(url.protocol() == "https:");
    url.set_protocol("http");
    assert(url.protocol() == "http:" && url.port_text() == "81");
    url.set_username("ann");
    url.set_password("p@ss");
    assert(url.href() == "http://ann:p%40ss@b.test:81/new%20path#a%20b");
    assert(Url::parse("mailto:ann@x.test", nullptr, url));
    url.set_pathname("/x");
    url.set_host("y.test");
    assert(url.href() == "mailto:ann@x.test");

    // Query strings as pairs, and back.
    auto pairs = parse_urlencoded("a=1&b=x+y&c=%41%zz&&d&=e&f=%FF");
    assert(pairs.size() == 6);
    assert(pairs[0] == std::make_pair(std::string("a"), std::string("1")));
    assert(pairs[1].second == "x y" && pairs[2].second == "A%zz");
    assert(pairs[3] == std::make_pair(std::string("d"), std::string()));
    assert(pairs[4] == std::make_pair(std::string(), std::string("e")));
    assert(pairs[5].second == "\xEF\xBF\xBD");            // not UTF-8: U+FFFD
    assert(serialize_urlencoded({{"a b", "c&d=e"}, {"\xC3\xA9", "*-._~!'()"}}) == "a+b=c%26d%3De&%C3%A9=*-._%7E%21%27%28%29");
}

TEST(url_objects_and_addresses_in_the_dom) {
    using namespace litebird;
    // The page's own address decides what every relative address means.
    auto doc = HtmlParser::parse(
        "<!DOCTYPE html><body><a id=plain>no href</a><a id=frag href='#frag'>frag</a>"
        "<a id=mail href='mailto:ann@x.test'>mail</a><form id=f></form></body>");
    doc->set_url("HTTPS://Example.test/app/page.html?tab=2#top");
    JsInterpreter interp(doc);
    JsValue failed = interp.eval(R"JS(
        (function () {
            var failed = [];
            function check(name, actual, expected) {
                if (actual !== expected) failed.push(name + ': ' + JSON.stringify(actual) + ', expected ' + JSON.stringify(expected));
            }
            function throws(name, kind, fn) {
                try { fn(); failed.push(name + ': no error'); } catch (e) { check(name, e.name, kind); }
            }
            var page = 'https://example.test/app/page.html?tab=2#top';
            check('the address is normalised', location.href + ' ' + document.URL, page + ' ' + page);
            check('location parts', [location.protocol, location.host, location.port, location.pathname, location.search, location.hash, location.origin].join(' '),
                  'https: example.test  /app/page.html ?tab=2 #top https://example.test');

            // URL objects.
            var u = new URL('../a/b?x=1&y=2#h', page);
            check('href', u.href, 'https://example.test/a/b?x=1&y=2#h');
            check('instanceof', u instanceof URL && u.searchParams instanceof URLSearchParams, true);
            check('searchParams is one object', u.searchParams === u.searchParams, true);
            u.searchParams.append('z', 'a b');
            check('the list changes the URL', u.search, '?x=1&y=2&z=a+b');
            u.search = '?only=1';
            check('the URL changes the list', u.searchParams.get('only') + ' ' + u.searchParams.has('x'), '1 false');
            u.searchParams.delete('only');
            check('an empty list leaves no question mark', u.href, 'https://example.test/a/b#h');
            u.hash = '';
            check('hash removed', u.href, 'https://example.test/a/b');
            u.href = 'http://other.test:8080/p?q=2';
            check('href replaces everything', [u.host, u.pathname, u.searchParams.get('q')].join(' '), 'other.test:8080 /p 2');
            throws('href must be a URL', 'TypeError', function () { u.href = 'nope'; });
            check('a failed assignment changes nothing', u.href, 'http://other.test:8080/p?q=2');
            check('as text', String(u) + ' ' + JSON.stringify({u: u}), 'http://other.test:8080/p?q=2 {"u":"http://other.test:8080/p?q=2"}');
            throws('not a URL', 'TypeError', function () { new URL('nope'); });
            throws('no arguments', 'TypeError', function () { new URL(); });
            throws('without new', 'TypeError', function () { URL('https://a.test'); });
            check('canParse', [URL.canParse('https://a.test'), URL.canParse('nope'), URL.canParse('x', page)].join(' '), 'true false true');
            check('parse', URL.parse('nope') + ' ' + URL.parse('x', page).href, 'null https://example.test/app/x');
            check('members are on the prototype', Object.keys(u).length + ' ' + ('href' in URL.prototype), '0 true');
            class Api extends URL { get first() { return this.pathname.split('/')[1]; } }
            var api = new Api('https://api.test/v1/users');
            check('a subclass', api.first + ' ' + (api instanceof Api) + ' ' + (api instanceof URL) + ' ' + api.host, 'v1 true true api.test');

            // URLSearchParams.
            var p = new URLSearchParams('?b=2&a=1&b=3&c=x+y%21');
            check('from a string', p.getAll('b').join() + ' ' + p.get('c') + ' ' + p.get('none') + ' ' + p.size, '2,3 x y! null 4');
            p.sort();
            check('sort keeps equal names in order', p.toString(), 'a=1&b=2&b=3&c=x+y%21');
            p.set('b', '9');
            p.delete('c', 'no such value');
            check('set, and delete by value', p.toString() + ' ' + p.has('b', '9') + ' ' + p.has('b', '2'), 'a=1&b=9&c=x+y%21 true false');
            check('iteration', Array.from(p).join('|') + ' ' + Array.from(p.keys()).join() + ' ' + Array.from(p.values()).join(), 'a,1|b,9|c,x y! a,b,c 1,9,x y!');
            var seen = [];
            p.forEach(function (value, name, owner) { seen.push(name + '=' + value + (owner === p)); });
            check('forEach', seen.join(), 'a=1true,b=9true,c=x y!true');
            check('from pairs', new URLSearchParams([['a', '1'], ['a', '2']]).toString(), 'a=1&a=2');
            check('from a record', new URLSearchParams({q: 'x y', n: 1}).toString(), 'q=x+y&n=1');
            check('from a Map', new URLSearchParams(new Map([['k', 'v']])).toString(), 'k=v');
            check('from another list', new URLSearchParams(p).toString(), p.toString());
            throws('a pair of three', 'TypeError', function () { new URLSearchParams([['a', 'b', 'c']]); });
            check('a list of its own changes no URL', new URLSearchParams('a=1').toString(), 'a=1');

            // Links.
            var a = document.createElement('a');
            check('no href', JSON.stringify([a.href, a.protocol, a.host, a.pathname, a.origin]), '["",":","","",""]');
            a.href = '#top';
            check('fragment only', a.href, page);
            a.href = '?q=1';
            check('query only', a.href, 'https://example.test/app/page.html?q=1');
            a.href = '//cdn.test/lib.js';
            check('another host', a.href + ' ' + a.host + ' ' + a.getAttribute('href'), 'https://cdn.test/lib.js cdn.test //cdn.test/lib.js');
            a.pathname = '/new path';
            check('assigning a part rewrites the attribute', a.getAttribute('href') + ' ' + a.pathname, 'https://cdn.test/new%20path /new%20path');
            a.hash = 'x';
            a.search = 'y=1';
            a.port = '8080';
            check('more parts', a.href, 'https://cdn.test:8080/new%20path?y=1#x');
            a.protocol = 'mailto';
            check('a web address stays one', a.protocol, 'https:');
            var mail = document.getElementById('mail');
            check('mailto', [mail.protocol, mail.pathname, mail.host, mail.origin, mail.href].join(' '), 'mailto: ann@x.test  null mailto:ann@x.test');
            a.setAttribute('href', 'https://exa mple.test/');
            check('not a URL: the attribute as written', a.href + ' ' + a.protocol + ' ' + a.host, 'https://exa mple.test/ : ');

            // The page's address.
            check('form.action without an attribute', document.getElementById('f').action, page);
            check('baseURI', document.baseURI + ' ' + document.body.baseURI, page + ' ' + page);
            var base = document.createElement('base');
            base.href = 'https://static.test/assets/';
            document.head.appendChild(base);
            a.setAttribute('href', 'logo.png');
            check('a base element moves relative addresses', a.href + ' ' + document.baseURI, 'https://static.test/assets/logo.png https://static.test/assets/');
            check('but not the page', location.href, page);
            base.remove();

            var before = history.length;
            history.pushState({n: 1}, '', '');
            check('pushState with an empty address stays', location.href + ' ' + (history.length - before), page + ' 1');
            history.pushState(null, '', '/other?x=1');
            check('pushState to another path', location.href, 'https://example.test/other?x=1');
            throws('another site', 'SecurityError', function () { history.pushState(null, '', 'https://other.test/'); });
            throws('another scheme', 'SecurityError', function () { history.replaceState(null, '', 'http://example.test/other'); });
            throws('not a URL at all', 'SecurityError', function () { history.pushState(null, '', 'https://[bad'); });
            check('a refused move changes nothing', location.href, 'https://example.test/other?x=1');
            throws('location.href must be a URL', 'SyntaxError', function () { location.href = 'https://[bad'; });
            return failed.join('\n');
        })()
    )JS");
    if (!failed.to_string().empty()) std::cerr << failed.to_string() << "\n";
    assert(failed.to_string().empty());

    // A change of fragment moves within the page: the address changes at
    // once, the events come later.
    interp.eval(R"JS(
        var log = [];
        window.addEventListener('hashchange', function (e) { log.push('hashchange ' + e.oldURL.split('/').pop() + ' > ' + e.newURL.split('/').pop()); });
        window.addEventListener('popstate', function () { log.push('popstate'); });
        var entries = history.length;
        location.hash = 'a b';
    )JS");
    assert(interp.eval("location.href").to_string() == "https://example.test/other?x=1#a%20b");
    assert(interp.eval("log.length").to_number() == 0.0);
    EventLoop::default_loop().tick();
    assert(interp.eval("log.join()").to_string() == "popstate,hashchange other?x=1 > other?x=1#a%20b");
    // The same fragment again is no move; an empty one is: the address ends in "#".
    interp.eval("log = []; location.hash = '#a b'; location.hash = '';");
    assert(interp.eval("location.href + ' ' + JSON.stringify(location.hash)").to_string() == "https://example.test/other?x=1# \"\"");
    EventLoop::default_loop().tick();
    assert(interp.eval("log.join()").to_string() == "popstate,hashchange other?x=1#a%20b > other?x=1#");
    // A link to a fragment does the same when it is clicked; replace() does
    // not add an entry.
    interp.eval("log = []; entries = history.length; document.getElementById('frag').click();");
    assert(interp.eval("location.hash + ' ' + (history.length - entries)").to_string() == "#frag 1");
    interp.eval("location.replace('#second');");
    assert(interp.eval("location.hash + ' ' + (history.length - entries)").to_string() == "#second 1");
    EventLoop::default_loop().tick();
    assert(interp.eval("log.join()").to_string() ==
           "popstate,hashchange other?x=1# > other?x=1#frag,popstate,hashchange other?x=1#frag > other?x=1#second");
    // Anywhere else would be another page: nothing happens.
    interp.eval("location.href = '/elsewhere'; location.assign('https://other.test/#x');");
    assert(interp.eval("location.href").to_string() == "https://example.test/other?x=1#second");

    // A page that is a file may change its query and fragment, not its path.
    auto file_doc = HtmlParser::parse("<!DOCTYPE html><body></body>");
    file_doc->set_url(file_url_from_path("/home/me/my page.html"));
    JsInterpreter file_interp(file_doc);
    assert(file_interp.eval("location.href + ' ' + location.origin").to_string() == "file:///home/me/my%20page.html null");
    assert(file_interp.eval("history.pushState(null, '', '?tab=2#x'); location.href").to_string() ==
           "file:///home/me/my%20page.html?tab=2#x");
    assert(file_interp.eval("try { history.pushState(null, '', 'other.html'); 'no error' } catch (e) { e.name }").to_string() ==
           "SecurityError");

    // A script with no page has URL and URLSearchParams too.
    JsInterpreter alone;
    assert(alone.eval("new URL('/a?b=c', 'https://x.test').searchParams.get('b') + ' ' + (new URL('https://x.test') instanceof URL)")
               .to_string() == "c true");
}

TEST(page_scripts_run_by_the_rules_of_a_page_load) {
    using namespace litebird;
    // Which <script> elements are JavaScript to run, and when: each one as
    // the parser reaches its end tag, seeing the page up to itself.
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    js.load_page(
        "<!DOCTYPE html><head><title>T</title>"
        "<script id=first>window.log = ['first:' + document.currentScript.id + ':' + document.readyState + ':' +"
        "    document.scripts.length + ':' + document.body + ':' + document.getElementById('later') + ':' + document.title];</script>"
        "</head><body><p id=early>early</p>"
        "<script type='text/plain'>log.push('text/plain');</script>"
        "<script type=' TEXT/JavaScript '>log.push('any case, spaces around');</script>"
        "<script type='text/javascript; charset=utf-8'>log.push('with a parameter');</script>"
        "<script language='JavaScript1.5'>log.push('language');</script>"
        "<script language='vbscript'>log.push('vbscript');</script>"
        "<script type='module'>log.push('module');</script>"
        "<script type='application/json'>{\"not\": \"code\"}</script>"
        "<script src='elsewhere.js'>log.push('has a src');</script>"
        "<script type=''>log.push('empty type');</script>"
        "<template><script>log.push('inside a template');</script></template>"
        "<script>log.push('sees:' + document.getElementById('early').textContent + ':' + document.getElementById('later') + ':' +"
        "    document.scripts.length); document.body.appendChild(document.createElement('hr')).id = 'added';</script>"
        "<p id=later>later</p>"
        "<script>var late = document.createElement('script'); late.textContent = \"log.push('inserted')\";"
        "    document.body.appendChild(late);"
        "    log.push('inserter:' + document.getElementById('added').previousElementSibling.tagName + ':' +"
        "             document.getElementById('later').previousElementSibling.id);</script>"
        "<script>document.addEventListener('readystatechange', function () { log.push('state:' + document.readyState); });"
        "    document.addEventListener('DOMContentLoaded', function () { log.push('DOMContentLoaded:' + document.currentScript); });"
        "    window.addEventListener('load', function () { log.push('load'); });</script>"
        "</body>");
    assert(js.eval("log.join()").to_string() ==
           "first:first:loading:1:null:null:T,any case, spaces around,language,empty type,sees:early:null:11,"
           "inserted,inserter:SCRIPT:added,state:interactive");
    assert(js.eval("document.readyState").to_string() == "interactive");
    js.notify_document_loaded();
    assert(js.eval("log.slice(8).join()").to_string() == "DOMContentLoaded:null,state:complete,load");

    // A script may take apart the tree the parser is working in. The parser
    // goes on inserting into the elements it has open, wherever they are
    // now, and into the page once they are closed.
    auto doc2 = std::make_shared<Document>();
    doc2->set_url("https://example.test/");
    JsInterpreter js2(doc2);
    js2.load_page(
        "<!DOCTYPE html><body><div id=box><script>window.box = document.getElementById('box'); box.remove();</script>"
        "<b id=inner>x</b></div><p id=after>after</p>"
        "<table id=t><tr><td><script>document.getElementById('t').remove();</script>cell</td></tr></table><p id=last>last</p>"
        "<script>document.body.innerHTML = '<i id=fresh>fresh</i>';</script><u id=tail>tail</u>"
        "<script>window.seen = [document.getElementById('inner'), box.querySelector('#inner').textContent, box.isConnected,"
        "    document.getElementById('after'), document.getElementById('fresh').textContent,"
        "    document.getElementById('tail').previousElementSibling.id, document.body.children.length].join();</script>"
        "</body>");
    assert(js2.eval("seen").to_string() == ",x,false,,fresh,fresh,3");

    // A custom element defined by a script is one from then on: the
    // elements further down the page are made as that class, and told they
    // are connected before their content is parsed.
    auto doc3 = std::make_shared<Document>();
    doc3->set_url("https://example.test/");
    JsInterpreter js3(doc3);
    js3.load_page(
        "<!DOCTYPE html><body><x-tag id=before>b</x-tag>"
        "<script>window.log = []; customElements.define('x-tag', class extends HTMLElement {"
        "    connectedCallback() { log.push(this.id + ':' + this.childNodes.length); } });</script>"
        "<x-tag id=one>text</x-tag>"
        "<script>log.push(document.getElementById('one') instanceof customElements.get('x-tag'));</script>"
        "<x-tag id=two></x-tag>"
        "</body>");
    assert(js3.eval("log.join()").to_string() == "before:1,one:0,true,two:0");

    // A mutation observer hears of what the parser adds. Its callback runs
    // when no script is running: before the next script, and at the end.
    auto doc4 = std::make_shared<Document>();
    doc4->set_url("https://example.test/");
    JsInterpreter js4(doc4);
    js4.load_page(
        "<!DOCTYPE html><body><script>window.log = [];"
        "    new MutationObserver(function (records) {"
        "        log.push(records.map(function (r) { return r.addedNodes[0].nodeName; }).join('+')); })"
        "        .observe(document.body, {childList: true});"
        "    Promise.resolve().then(function () { log.push('promise'); });</script>"
        "<p>one</p><div></div><script>log.push('script');</script><span></span></body>");
    assert(js4.eval("log.join()").to_string() == "promise,P+DIV+SCRIPT,script,SPAN");

    // The shell loads a page the same way, and the page knows its address.
    BrowserShell shell(800.0f, 600.0f, 80, 24);
    assert(shell.navigate_to("examples/hello.html"));
    const std::string address = shell.document()->url();
    assert(address.rfind("file:///", 0) == 0 && address.find("/examples/hello.html") != std::string::npos);
    assert(shell.js_engine()->eval("location.href === document.URL && location.protocol === 'file:' && document.readyState")
               .to_string() == "complete");
    assert(shell.js_engine()->eval("var a = document.createElement('a'); a.href = 'card.html'; a.href").to_string() ==
           address.substr(0, address.rfind('/') + 1) + "card.html");
    assert(ResourceLoader::address_of("https://Example.test") == "https://example.test/");
    assert(ResourceLoader::address_of("/tmp/a b.html") == "file:///tmp/a%20b.html");
}

TEST(get_element_by_id_answers_from_an_index) {
    using namespace litebird;
    // getElementById walked the whole document on every call. The second
    // lookup with no change in between now builds an index
    // (Document::get_element_by_id), so every question here is asked twice:
    // once of the walk and once of the index, and after every kind of change
    // that must make the index forget. jsdom prints the same line.
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    js.load_page(
        "<!DOCTYPE html><body><p id=\"a\" class=\"first\">x</p><span id=\"dup\" class=\"one\"></span><div id=\"b\"></div>"
        "<span id=\"dup\" class=\"two\"></span><em id=\"null\"></em><template><s id=\"intemplate\"></s></template></body>");
    js.notify_document_loaded();
    js.eval(R"JS(
        var out = [];
        function id(x) { var e = document.getElementById(x); return e ? e.tagName + (e.className ? '.' + e.className : '') : 'null'; }
        out.push(id('a'), id('a'), id('b'), id('nope'), id(''));
        var first = document.getElementById('dup'); out.push(id('dup'), id('dup'));
        first.remove(); out.push(id('dup'), id('dup'));
        document.body.insertBefore(first, document.body.firstChild); out.push(id('dup'), id('dup'));
        var a = document.getElementById('a'); a.id = 'z'; out.push(id('a'), id('a'), id('z'), id('z'));
        a.removeAttribute('id'); out.push(id('z'), id('z'));
        a.setAttribute('id', ''); out.push(id(''), id(''));
        var d = document.createElement('div'); d.id = 'new'; out.push(id('new'), id('new'));
        document.body.appendChild(d); out.push(id('new'), id('new'));
        d.innerHTML = '<i id="deep"><b id="deeper"></b></i>'; out.push(id('deeper'), id('deeper'));
        d.textContent = ''; out.push(id('deeper'), id('deeper'), id('deep'));
        var t = document.querySelector('template');
        out.push(id('intemplate'), id('intemplate'), t.content.getElementById('intemplate').tagName);
        var host = document.getElementById('b'); var root = host.attachShadow({mode: 'open'});
        root.innerHTML = '<u id="shadowed"></u>';
        out.push(id('shadowed'), id('shadowed'), root.getElementById('shadowed').tagName);
        out.push(String(document.getElementById(null)), String(document.getElementById(undefined)), id('null'), id('null'));
        var other = document.implementation.createHTMLDocument(''); other.body.innerHTML = '<p id="a2">x</p>';
        out.push(id('a2'), id('a2'), other.getElementById('a2').tagName, other.getElementById('a2').tagName);
        document.body.appendChild(other.getElementById('a2'));
        out.push(id('a2'), id('a2'), String(other.getElementById('a2')), String(other.getElementById('a2')));
    )JS");
    assert(js.eval("out.join(' ')").to_string() ==
           "P.first P.first DIV null null SPAN.one SPAN.one SPAN.two SPAN.two SPAN.one SPAN.one null null "
           "P.first P.first null null null null null null DIV DIV B B null null null null null S null null U "
           "[object HTMLElement] null EM EM null null P P P P null null");

    // The reason for it: 20,000 lookups in a document of 20,000 elements took
    // 5.6 seconds. No clock here: with the walk this is 200 million elements
    // visited, and the suite would visibly stop.
    js.eval(R"JS(
        var holder = document.createElement('div'); document.body.appendChild(holder);
        for (var i = 0; i < 20000; i++) { var el = document.createElement('i'); el.id = 'n' + i; holder.appendChild(el); }
        var found = 0;
        for (var i = 0; i < 20000; i++) if (document.getElementById('n' + i).id === 'n' + i) found++;
    )JS");
    assert(js.eval("found").to_number() == 20000);
}

TEST(computed_style_answers_for_every_property) {
    using namespace litebird;
    // getComputedStyle answered "" for a property nobody had declared and took
    // the last rule that matched, whatever its specificity. Seven cases of the
    // DOM corpus hold what it does now (tests/dom-conformance/style.dat:
    // initial values, specificity, colours, lengths, the defaults of HTML
    // elements, shorthands, inherit and initial). Here is what those cannot
    // hold, because jsdom answers something else and CSS says this.
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    js.load_page(
        "<!DOCTYPE html><html><head><style>"
        "#a { line-height: 200%; font-size: 20px; border: 0.6px solid; outline: 2px dotted green; word-spacing: 0.5em }"
        "#b { line-height: 1.5; font-size: 10px; border-spacing: 2px; transition: opacity 1s; background: url(x.png) red }"
        "#c { width: calc(100% - 2em); height: calc(2 * (1px + 1em)); font-size: 10px; margin-top: calc(1em + 10%);"
        "     top: calc(100vh - 1px) }"
        "#e { color: #11223380; border: 2.9px solid; border-left-width: 0.2px; outline: 1px solid; outline-width: 3.7px;"
        "     font-size: calc(1em + 4px) }"
        "</style></head><body><div id=a><span id=a1>x</span></div><div id=b><span id=b1 style='font-size: 30px'>y</span></div>"
        "<div id=c>z</div><textarea id=t></textarea><div id=e>e</div></body></html>");
    js.notify_document_loaded();
    auto cs = [&](const char* id, const char* property) {
        return js.eval(std::string("getComputedStyle(document.getElementById('") + id + "')).getPropertyValue('" +
                       property + "')")
            .to_string();
    };

    // The body has the margin every browser gives it (jsdom: 0px).
    assert(js.eval("getComputedStyle(document.body).marginTop").to_string() == "8px");

    // A percentage line-height is a length where it is declared and is
    // inherited as that length (jsdom: "200%"). A number is inherited as a
    // number, and multiplies the font size of whoever has it.
    assert(cs("a", "line-height") == "40px" && cs("a1", "line-height") == "40px");
    assert(cs("b", "line-height") == "15px" && cs("b1", "line-height") == "45px");

    // A border or an outline that is drawn is a whole number of pixels wide
    // and not less than one; one that is not drawn is 0px wide, whatever
    // its width was declared to be (jsdom: 3px for an outline nobody drew).
    assert(cs("a", "border-top-width") == "1px");
    assert(cs("e", "border-top-width") == "2px" && cs("e", "border-left-width") == "1px");
    assert(cs("e", "outline-width") == "3px" && cs("a", "outline-width") == "2px" && cs("b", "outline-width") == "0px");

    // Shorthands are given whole, the way browsers write them (jsdom leaves
    // the `none` and the `solid` out, and has `currentcolor none` for a
    // box-shadow nobody declared).
    assert(cs("a", "border-top") == "1px solid rgb(0, 0, 0)");
    assert(cs("a", "border") == "1px solid rgb(0, 0, 0)" && cs("b", "border") == "0px none rgb(0, 0, 0)");
    assert(cs("a", "outline") == "rgb(0, 128, 0) dotted 2px");
    assert(cs("b", "text-decoration") == "none solid rgb(0, 0, 0)" && cs("b", "box-shadow") == "none");
    assert(cs("b", "list-style") == "outside none disc" && cs("b", "flex-flow") == "row nowrap");
    assert(cs("a", "word-spacing") == "10px" && cs("b", "word-spacing") == "0px");
    assert(cs("b", "border-spacing") == "2px 2px" && cs("a", "border-spacing") == "0px 0px");
    assert(cs("t", "display") == "inline-block");

    // A shorthand nothing here takes apart keeps its text, and its parts are
    // "" (not known) and not an initial value that would be wrong.
    assert(cs("b", "transition") == "opacity 1s" && cs("b", "transition-duration") == "");
    assert(cs("a", "transition") == "all" && cs("a", "transition-duration") == "0s");
    assert(cs("b", "background") == "url(x.png) red" && cs("b", "background-color") == "rgb(255, 0, 0)");
    assert(cs("b", "background-image") == "" && cs("a", "background-image") == "none");

    // calc(): lengths are added up; a percentage needs layout and stays; a
    // unit nothing here knows (the viewport's) leaves the value as declared.
    assert(cs("c", "height") == "22px" && cs("e", "font-size") == "20px");
    assert(cs("c", "width") == "calc(100% - 20px)" && cs("c", "margin-top") == "calc(10% + 10px)");
    assert(cs("c", "top") == "calc(100vh - 1px)");
    // Eight bits of alpha that two decimals give back exactly.
    assert(cs("e", "color") == "rgba(17, 34, 51, 0.5)");

    // No recursion anywhere: what is inherited, the font size and the weight
    // are found by loops up the tree, and a calc() nested deeper than anyone
    // writes is given back as text.
    js.eval(R"JS(
        var top = document.createElement('div');
        top.style.fontSize = '10px'; top.style.color = 'red'; top.style.fontWeight = '100';
        var at = top;
        for (var i = 0; i < 20000; i++) { var next = document.createElement(i % 2 ? 'b' : 'span'); at.appendChild(next); at = next; }
        document.body.appendChild(top);
        var deep = getComputedStyle(at);
        var answers = [deep.color, deep.fontSize, deep.fontWeight, deep.lineHeight, deep.display, deep.marginTop].join(' ');
        at.style.fontSize = '2em'; at.style.textIndent = '1rem';
        answers += ' ' + [deep.fontSize, deep.textIndent, deep.borderTopColor].join(' ');
        top.remove();
        var nested = 'calc(' + new Array(100001).join('(') + '1px' + new Array(100001).join(')') + ')';
        document.getElementById('c').style.width = nested;
        var kept = getComputedStyle(document.getElementById('c')).width === nested;
        document.getElementById('c').style.width = 'calc(' + new Array(13).join('(') + '1px' + new Array(13).join(')') + ' + 1px)';
        var summed = getComputedStyle(document.getElementById('c')).width;
    )JS");
    assert(js.eval("answers").to_string() == "rgb(255, 0, 0) 10px 900 normal inline 0px 20px 16px rgb(255, 0, 0)");
    assert(js.eval("String(kept)").to_string() == "true");
    assert(js.eval("summed").to_string() == "2px");
}

TEST(ranges_follow_the_tree) {
    using namespace litebird;
    // A Range kept its two points where they were put, whatever happened to
    // the tree afterwards, and surroundContents() inside one text node put
    // the wrapper after the text. Ranges are live now (DomLive.hpp); eight
    // cases of the DOM corpus hold the rules (tests/dom-conformance/
    // ranges.dat). Here: the two answers jsdom gets wrong, and trees and
    // numbers of ranges no person writes.
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    js.load_page("<!DOCTYPE html><body><p id=p></p><div id=d><span>one</span><span>two</span></div></body>");
    js.notify_document_loaded();

    // normalize(): a boundary in a text node that is merged away moves into
    // the node it was merged into, at the same character (jsdom leaves it on
    // the parent).
    js.eval(R"JS(
        var p = document.getElementById('p');
        p.append('ab', '', 'cd', document.createElement('i'), 'ef', 'gh');
        var r = document.createRange();
        r.setStart(p.childNodes[2], 1); r.setEnd(p.childNodes[5], 1);
        p.normalize();
        var merged = [p.childNodes.length, r.startContainer.data, r.startOffset, r.endContainer.data, r.endOffset, r.toString()].join(' ');
    )JS");
    assert(js.eval("merged").to_string() == "3 abcd 3 efgh 3 defg");

    // containsNode(node, true): a node before the selection is not partly
    // inside it (jsdom: true for anything that ends before the selection does).
    js.eval(R"JS(
        var sel = getSelection();
        sel.selectAllChildren(document.getElementById('d'));
        var partly = [sel.containsNode(p, true), sel.containsNode(document.getElementById('d'), true),
                      sel.containsNode(document.getElementById('d').firstChild.firstChild, true),
                      sel.containsNode(document.body, true), sel.containsNode(document.body, false)].join(' ');
        sel.removeAllRanges();
    )JS");
    assert(js.eval("partly").to_string() == "false true true true false");

    // A boundary 50,000 levels down, and its whole tree removed from the
    // top: found by walking up, in a loop.
    js.eval(R"JS(
        var top = document.createElement('div'), at = top;
        for (var i = 0; i < 50000; i++) { var next = document.createElement('div'); at.appendChild(next); at = next; }
        at.textContent = 'bottom';
        document.body.appendChild(top);
        var deep = document.createRange();
        deep.setStart(at.firstChild, 2); deep.setEnd(at.firstChild, 4);
        var before = deep.toString();
        top.remove();
        var after = [deep.startContainer === document.body, deep.startOffset, deep.collapsed].join(' ');
    )JS");
    assert(js.eval("before").to_string() == "tt");
    assert(js.eval("after").to_string() == "true 2 true");

    // A hundred thousand ranges nobody keeps do not stay on the list: the
    // collector frees them and the list is cleared out as it grows. The
    // changes to the tree that follow would take minutes otherwise.
    js.eval(R"JS(
        var kept = document.createRange(); kept.selectNodeContents(document.getElementById('d'));
        for (var i = 0; i < 100000; i++) { var gone = document.createRange(); gone.selectNode(p); }
        gone = null;
        gc();
        var d = document.getElementById('d');
        for (var i = 0; i < 20000; i++) d.appendChild(document.createElement('b'));
        for (var i = 0; i < 20000; i++) d.removeChild(d.lastChild);
        var still = [kept.startOffset, kept.endOffset, kept.toString()].join(' ');
    )JS");
    assert(js.eval("still").to_string() == "0 2 onetwo");
}

TEST(dom_follows_the_standard_where_jsdom_does_not) {
    using namespace litebird;
    // The DOM corpus records what jsdom prints. In these five places jsdom
    // and the HTML and DOM standards differ, so the corpus stays away from
    // them (tests/dom-conformance/README.md) and the standard's answer is
    // asserted here.
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    js.load_page(
        "<!DOCTYPE html><body><div id=d tabindex='junk'></div><button id=b tabindex='junk'>b</button>"
        "<a id=a href='#x' tabindex='junk'>a</a><span id=s></span>"
        "<form id=f><input id=req name=req required><button id=plain>go</button>"
        "<button id=skip formnovalidate>skip</button></form><select id=sel></select></body>");
    js.notify_document_loaded();

    // 1. A tabindex that is not a number is no tabindex: the element's own
    //    default, -1 for a div and 0 for what takes focus anyway.
    assert(js.eval("[d.tabIndex, b.tabIndex, a.tabIndex, s.tabIndex].join()").to_string() == "-1,0,0,-1");

    // 2. A submit button with formnovalidate submits a form that is not
    //    valid; any other button is stopped by the validation.
    js.eval(R"JS(
        var log = [];
        f.addEventListener('submit', function (e) { log.push('submit:' + e.submitter.id); e.preventDefault(); });
        req.addEventListener('invalid', function () { log.push('invalid'); });
        plain.click();
        skip.click();
    )JS");
    assert(js.eval("log.join()").to_string() == "invalid,submit:skip");

    // 3. A form that is not in a document cannot be submitted: requestSubmit()
    //    does nothing, not even fire the event.
    js.eval(R"JS(
        var loose = document.createElement('form'), fired = 0;
        loose.addEventListener('submit', function (e) { fired++; e.preventDefault(); });
        loose.requestSubmit();
        var firedLoose = fired;
        document.body.appendChild(loose);
        loose.requestSubmit();
    )JS");
    assert(js.eval("firedLoose + ',' + fired").to_string() == "0,1");

    // 4. A <select> keeps what is written inside it: the parser has no
    //    special rules for its content any more.
    js.eval("sel.innerHTML = '<option>a</option><p>no</p><div>d</div>';");
    assert(js.eval("sel.innerHTML + ' ' + sel.children.length + ' ' + sel.options.length").to_string() ==
           "<option>a</option><p>no</p><div>d</div> 3 1");

    // 5. There is one microtask queue. A promise callback queued before a
    //    mutation runs before the observer hears of the mutation.
    js.eval(R"JS(
        var order = [], target = document.createElement('div');
        new MutationObserver(function () { order.push('observer'); }).observe(target, {childList: true});
        Promise.resolve().then(function () { order.push('promise'); });
        target.appendChild(document.createElement('i'));
        Promise.resolve().then(function () { order.push('promise 2'); });
    )JS");
    assert(js.eval("order.join()").to_string() == "promise,observer,promise 2");
}

// ---------------------------------------------------------------------------
// Phase 28: XML. The parser alone, then XML documents in the DOM.
// ---------------------------------------------------------------------------

TEST(xml_parser_reads_xml_1_0_and_nothing_outside_the_document) {
    using namespace litebird;
    auto events = [](const std::string& text) {
        std::string out;
        xml::XmlError error;
        return xml::xml_event_dump(text, out, error) ? out : std::string("error");
    };

    // 1. Everything a document holds, each name with its namespace: the
    //    internal subset is read (an entity, a default for an attribute, a
    //    type that makes the parser normalize a value), CDATA is text, and a
    //    character reference in an attribute is not normalized away.
    assert(events("<?xml version=\"1.0\"?>\n"
                  "<!DOCTYPE r [<!ENTITY e \"v &amp; w\"><!ATTLIST r d CDATA \"def\" t NMTOKENS #IMPLIED>]>\n"
                  "<r xmlns:p=\"urn:p\" t=\"  a   b  \" p:x=\"1&#10;2\">&e;<![CDATA[<c>]]><p:k/><!--n--><?pi  data ?></r>\n") ==
           "doctype r||\nelement r\nattr d=\"def\"\nattr t=\"a b\"\nattr {urn:p}x=\"1\\n2\"\ntext \"v & w<c>\"\n"
           "element {urn:p}k\nend\ncomment \"n\"\npi pi \"data \"\nend\n");

    // 2. Line ends become one line feed in text; in an attribute value every
    //    white space character becomes a space, and none is taken out.
    assert(events("<a b=\" x \n\ty \">1\r\n2\r3</a>") == "element a\nattr b=\" x   y \"\ntext \"1\\n2\\n3\"\nend\n");

    // 3. Well formed: accepted.
    const char* const accepted[] = {
        "<a/>", "<a></a>", "<a b='1' c=\"2\"/>", "<a>&#x10FFFF;</a>", "<\xC3\xA9/>", "<_a.b-c/>",
        "<a><![CDATA[]]]]><![CDATA[>]]></a>", "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?><a/>",
        "<a xmlns:p=\"u\"><p:b p:c=\"1\"/></a>", "<!DOCTYPE a [<!ELEMENT a EMPTY>]><a/>",
        "<a>\n</a>\n<!--after-->\n<?pi?>\n", "<a b=\"x&#9;y\"/>", "<a xmlns=\"\"/>",
        "<a xml:lang=\"en\" xml:space=\"preserve\"/>", "<a xmlns:xml=\"http://www.w3.org/XML/1998/namespace\"/>",
        // A 1.x document is read as 1.0 (XML 1.0 Fifth Edition, 2.8).
        "<?xml version=\"1.1\"?><a/>",
        // With an external subset that was not read, an entity nobody
        // declared is not an error of form: the declaration may be there.
        "<!DOCTYPE a SYSTEM \"x.dtd\"><a>&undef;</a>",
        "<!DOCTYPE a [<!ENTITY % p SYSTEM \"p.ent\">%p;]><a>&undef;</a>",
    };
    for (const char* text : accepted) assert(events(text) != "error");

    // 4. Not well formed: refused, each for its own reason.
    const std::string refused[] = {
        "", "<a>", "<a></b>", "<a/><b/>", "text", "<a/>text", "<a b=1/>", "<a b=\"1\" b=\"2\"/>", "<a b=\"1\"c=\"2\"/>",
        "<a b=\"<\"/>", "<a>&undefined;</a>", "<a>&#0;</a>", "<a>&#xD800;</a>", "<a>]]></a>", "<a><!-- a -- b --></a>",
        "<?xml version=\"1.0\"?><?xml version=\"1.0\"?><a/>", " <?xml version=\"1.0\"?><a/>", "<a>\x01</a>",
        "<a><?xml foo?></a>", "<1a/>", "<a b=\"&lt\"/>", "<a>&amp</a>", "<!DOCTYPE a><!DOCTYPE a><a/>", "<a><b></a></b>",
        std::string("<a\0/>", 5), "<a>\xFF</a>", "<a>\xC3</a>", "<a>\xEF\xBF\xBE</a>",
        // An entity that names itself; one whose text is half an element;
        // a '<' that reaches an attribute value through an entity.
        "<!DOCTYPE a [<!ENTITY e \"&e;\">]><a>&e;</a>", "<!DOCTYPE a [<!ENTITY e \"<b>\">]><a>&e;</a>",
        "<!DOCTYPE a [<!ENTITY e \"<\">]><a b=\"&e;\"/>",
        // Namespaces in XML: a prefix nobody declared, one attribute twice
        // under two prefixes, the reserved prefixes and names, two colons.
        "<a xmlns:p=\"u\"><q:b/></a>", "<a p:b=\"1\"/>", "<a xmlns:p=\"u\" xmlns:q=\"u\" p:x=\"1\" q:x=\"2\"/>",
        "<a xmlns:xmlns=\"u\"/>", "<a xmlns:xml=\"u\"/>", "<a xmlns:p=\"\"/>", "<a:b:c xmlns:a=\"u\"/>", "<:a/>", "<a:/>",
        "<a xmlns=\"http://www.w3.org/2000/xmlns/\"/>", "<a xmlns:p=\"http://www.w3.org/XML/1998/namespace\"/>",
    };
    for (const std::string& text : refused) assert(events(text) == "error");

    // 5. An error says where.
    {
        std::string out;
        xml::XmlError error;
        assert(!xml::xml_event_dump("<a>\n  <b></a>", out, error));
        assert(error.line == 2);
        assert(error.describe().find("line 2") != std::string::npos);
    }

    // 6. Nothing outside the document is read. An external entity is a
    //    file on this machine; its text must not come back.
    {
        const std::string path = "/tmp/litebird-xml-test-secret.txt";
        FILE* file = fopen(path.c_str(), "w");
        assert(file);
        fputs("SECRET", file);
        fclose(file);
        const std::string read = events("<!DOCTYPE a [<!ENTITY x SYSTEM \"file://" + path + "\">"
                                        "<!ENTITY % pe SYSTEM \"file://" + path + "\">%pe;]><a>&x;</a>");
        assert(read == "doctype a||\nelement a\nend\n");
        assert(events("<!DOCTYPE a SYSTEM \"file://" + path + "\"><a/>").find("SECRET") == std::string::npos);
        // In an attribute value such a reference is forbidden outright.
        assert(events("<!DOCTYPE a [<!ENTITY x SYSTEM \"file://" + path + "\">]><a b=\"&x;\"/>") == "error");
        remove(path.c_str());
    }

    // 7. Bytes to text: the byte order mark, UTF-16 with and without one,
    //    an encoding the declaration names, one that nobody knows.
    auto decoded = [](const std::string& bytes, std::string_view transport = {}) {
        std::string text, encoding;
        xml::XmlError error;
        if (!xml::decode_xml(bytes, text, encoding, error, transport)) {
            return std::string(error.unsupported_encoding ? "unsupported" : "error");
        }
        return text;
    };
    auto utf16 = [](const std::string& ascii, bool big_endian) {
        std::string out;
        for (char c : ascii) {
            if (big_endian) out += '\0';
            out += c;
            if (!big_endian) out += '\0';
        }
        return out;
    };
    assert(decoded("\xEF\xBB\xBF<a/>") == "<a/>");
    assert(decoded("\xFF\xFE" + utf16("<a>x</a>", false)) == "<a>x</a>");
    assert(decoded("\xFE\xFF" + utf16("<a>x</a>", true)) == "<a>x</a>");
    assert(decoded(utf16("<?xml version=\"1.0\" encoding=\"UTF-16\"?><a/>", true)).find("<a/>") != std::string::npos);
    assert(decoded("<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><a>\xE9</a>").find("<a>\xC3\xA9</a>") != std::string::npos);
    assert(decoded("<?xml version=\"1.0\" encoding=\"no-such-thing\"?><a/>") == "unsupported");
    // What the transport says (a Content-Type charset) comes first.
    assert(decoded("<a>\xE9</a>", "iso-8859-1") == "<a>\xC3\xA9</a>");

    // 8. Names.
    assert(xml::is_xml_name("a") && xml::is_xml_name("_a.b-c") && xml::is_xml_name("a:b:c") && xml::is_xml_name("\xC3\xA9"));
    assert(!xml::is_xml_name("") && !xml::is_xml_name("1a") && !xml::is_xml_name("a b") && !xml::is_xml_name("-a"));
    assert(xml::is_xml_qname("a") && xml::is_xml_qname("p:a"));
    assert(!xml::is_xml_qname("a:b:c") && !xml::is_xml_qname(":a") && !xml::is_xml_qname("a:") && !xml::is_xml_qname("p:1"));

    // 9. The canonical form the conformance suite compares.
    {
        std::string out;
        xml::XmlError error;
        assert(xml::xml_canonical_form("<!DOCTYPE r [<!ATTLIST r d CDATA \"def\">]>\n<r xmlns:p=\"urn:p\" p:x=\"1&#10;2\" b='\"'><![CDATA[<c>]]><p:k/><!--n--><?pi  data ?></r>\n", out, error));
        assert(out == "<r b=\"&quot;\" d=\"def\" p:x=\"1&#10;2\" xmlns:p=\"urn:p\">&lt;c&gt;<p:k></p:k><?pi data ?></r>");
    }
}

TEST(xml_parser_bounds_what_a_hostile_document_costs) {
    using namespace litebird;
    struct Counter : xml::XmlHandler {
        std::size_t elements = 0, depth = 0, deepest = 0, text = 0, attributes = 0;
        void start_element(const xml::XmlName&, std::vector<xml::XmlAttribute>& list) override {
            ++elements;
            attributes += list.size();
            if (++depth > deepest) deepest = depth;
        }
        void end_element(const xml::XmlName&) override { --depth; }
        void characters(std::string_view data) override { text += data.size(); }
    };
    auto seconds_since = [](std::chrono::steady_clock::time_point start) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };

    // 1. A billion laughs: ten entities, each ten of the one before. Written
    //    out it is ten gigabytes. It is refused, and soon.
    {
        std::string text = "<!DOCTYPE l [<!ENTITY a \"aaaaaaaaaa\">";
        const std::string names = "abcdefghij";
        for (std::size_t i = 1; i < names.size(); ++i) {
            text += std::string("<!ENTITY ") + names[i] + " \"";
            for (int k = 0; k < 10; ++k) text += std::string("&") + names[i - 1] + ";";
            text += "\">";
        }
        Counter counter;
        xml::XmlError error;
        const auto start = std::chrono::steady_clock::now();
        assert(!xml::parse_xml(text + "]><l>&j;</l>", counter, error));
        assert(seconds_since(start) < 30.0);
        assert(counter.text < 64u * 1024u * 1024u);
        // The same in an attribute value.
        Counter second;
        assert(!xml::parse_xml(text + "]><l b=\"&j;\"/>", second, error));
        assert(seconds_since(start) < 60.0);
    }

    // 2. One long entity used many times: 50,000 characters, 50,000 times,
    //    2.5 GB from 200 kB. Refused too.
    {
        std::string text = "<!DOCTYPE q [<!ENTITY e \"" + std::string(50000, 'x') + "\">]><q>";
        for (int i = 0; i < 50000; ++i) text += "&e;";
        text += "</q>";
        Counter counter;
        xml::XmlError error;
        assert(!xml::parse_xml(text, counter, error));
        assert(counter.text < 64u * 1024u * 1024u);
    }

    // 3. And a document that uses entities the way documents do is read:
    //    a megabyte of text out of a small entity is nobody's attack.
    {
        std::string text = "<!DOCTYPE q [<!ENTITY e \"" + std::string(100, 'x') + "\">]><q>";
        for (int i = 0; i < 10000; ++i) text += "&e;";
        text += "</q>";
        Counter counter;
        xml::XmlError error;
        assert(xml::parse_xml(text, counter, error));
        assert(counter.text == 1000000);
    }

    // 4. 200,000 elements inside each other: read in a loop, not by
    //    functions calling themselves.
    {
        std::string text;
        for (int i = 0; i < 200000; ++i) text += "<a>";
        for (int i = 0; i < 200000; ++i) text += "</a>";
        Counter counter;
        xml::XmlError error;
        assert(xml::parse_xml(text, counter, error));
        assert(counter.elements == 200000 && counter.deepest == 200000);
    }

    // 5. 50,000 attributes on one element, 50,000 namespace declarations
    //    with 50,000 children that use them: the cost of each is its size.
    //    (Finding a repeated attribute by comparing each with all the others
    //    is the square of it: minutes.)
    {
        std::string many = "<a";
        for (int i = 0; i < 50000; ++i) many += " a" + std::to_string(i) + "=\"v\"";
        many += "/>";
        Counter counter;
        xml::XmlError error;
        const auto start = std::chrono::steady_clock::now();
        assert(xml::parse_xml(many, counter, error));
        assert(counter.attributes == 50000);
        // The same with the last one a repeat of the first: found.
        std::string repeated = many.substr(0, many.size() - 2) + " a0=\"again\"/>";
        assert(!xml::parse_xml(repeated, counter, error));

        std::string declared = "<r";
        for (int i = 0; i < 50000; ++i) declared += " xmlns:p" + std::to_string(i) + "=\"urn:n" + std::to_string(i) + "\"";
        declared += ">";
        for (int i = 0; i < 50000; ++i) declared += "<p" + std::to_string(i) + ":c/>";
        declared += "</r>";
        Counter second;
        assert(xml::parse_xml(declared, second, error));
        assert(second.elements == 50001);
        assert(seconds_since(start) < 60.0);
    }

    // 6. The DOCTYPE is text from the same hand. 50,000 attributes declared
    //    for one element (each must be looked up: the first declaration of
    //    a name is the one that counts), the same one declared 50,000 times,
    //    a content model 200,000 parentheses deep, one naming 50,000
    //    elements, an entity that is a chain of 50,000.
    {
        const auto start = std::chrono::steady_clock::now();
        xml::XmlError error;
        std::string list = "<!DOCTYPE a [<!ATTLIST a";
        for (int i = 0; i < 50000; ++i) list += " a" + std::to_string(i) + " CDATA \"v\"";
        list += ">]><a/>";
        Counter declared;
        assert(xml::parse_xml(list, declared, error));
        assert(declared.attributes == 50000);

        std::string again = "<!DOCTYPE a [";
        for (int i = 0; i < 50000; ++i) again += "<!ATTLIST a x CDATA \"v" + std::to_string(i) + "\">";
        again += "]><a/>";
        struct First : xml::XmlHandler {
            std::string value;
            void start_element(const xml::XmlName&, std::vector<xml::XmlAttribute>& attributes) override {
                assert(attributes.size() == 1 && !attributes[0].specified);
                value = attributes[0].value;
            }
        } first;
        assert(xml::parse_xml(again, first, error));
        assert(first.value == "v0");

        Counter model;
        assert(xml::parse_xml("<!DOCTYPE a [<!ELEMENT a " + std::string(200000, '(') + "b" + std::string(200000, ')') + ">]><a/>", model, error));
        assert(!xml::parse_xml("<!DOCTYPE a [<!ELEMENT a " + std::string(200000, '(') + "b" + std::string(199999, ')') + ">]><a/>", model, error));
        std::string mixed = "<!DOCTYPE a [<!ELEMENT a (#PCDATA";
        for (int i = 0; i < 50000; ++i) mixed += "|e" + std::to_string(i);
        assert(xml::parse_xml(mixed + ")*>]><a/>", model, error));

        std::string chain = "<!DOCTYPE a [<!ENTITY e0 \"x\">";
        for (int i = 1; i < 50000; ++i) chain += "<!ENTITY e" + std::to_string(i) + " \"&e" + std::to_string(i - 1) + ";\">";
        Counter linked;
        assert(xml::parse_xml(chain + "]><a b=\"&e49999;\">&e49999;</a>", linked, error));
        assert(linked.text == 1);
        //    And the chain closed into a ring is an entity that contains
        //    itself: refused, not followed.
        assert(!xml::parse_xml(chain + "<!ENTITY ring \"&last;\"><!ENTITY last \"&ring;\">]><a>&ring;</a>", linked, error));
        assert(seconds_since(start) < 60.0);
    }

    // 7. An attribute default is text the document did not write where it
    //    appears. One default of a megabyte and 100,000 empty tags are 100
    //    GB of attribute values from 1.4 MB: counted with what entities
    //    add, and refused under the same limit.
    {
        const auto start = std::chrono::steady_clock::now();
        xml::XmlError error;
        std::string empty_tags;
        for (int i = 0; i < 100000; ++i) empty_tags += "<e/>";
        Counter counter;
        assert(!xml::parse_xml("<!DOCTYPE a [<!ATTLIST e x CDATA \"" + std::string(1000000, 'v') + "\">]><a>" + empty_tags + "</a>", counter, error));
        assert(error.message.find("expand") != std::string::npos);
        assert(counter.elements < 1000);
        //    A hundred defaults on each of 10,000 elements: 12 MB from 43 kB.
        std::string hundred = "<!DOCTYPE a [<!ATTLIST e";
        for (int i = 0; i < 100; ++i) hundred += " a" + std::to_string(i) + " CDATA \"0123456789\"";
        Counter many;
        assert(!xml::parse_xml(hundred + ">]><a>" + empty_tags.substr(0, 40000) + "</a>", many, error));
        //    The same on ten elements is a document like any other, and so
        //    is one small default on 100,000.
        Counter few;
        assert(xml::parse_xml(hundred + ">]><a>" + empty_tags.substr(0, 40) + "</a>", few, error));
        assert(few.attributes == 1000);
        Counter small;
        assert(xml::parse_xml("<!DOCTYPE a [<!ATTLIST e x CDATA \"v\">]><a>" + empty_tags + "</a>", small, error));
        assert(small.attributes == 100000);
        assert(seconds_since(start) < 60.0);
    }
}

TEST(xml_documents_in_the_dom) {
    using namespace litebird;
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/dir/page.html");
    JsInterpreter js(doc);
    js.load_page("<!DOCTYPE html><html><head><title>t</title></head><body></body></html>");
    js.notify_document_loaded();
    js.eval(R"JS(
        var P = new DOMParser(), S = new XMLSerializer();
        function xml(text, type) { return P.parseFromString(text, type || 'application/xml'); }
        function caught(f) { try { f(); return 'no error'; } catch (e) { return e.name; } }
    )JS");
    auto S = [&](const char* expression) { return js.eval(expression).to_string(); };

    // 1. The four XML types give a document of that content type whose
    //    elements keep their names and have no namespace unless the text
    //    gives them one. (This used to be the HTML parser: <Root> came back
    //    as <html><head></head><body><root>.)
    assert(S("['application/xml', 'text/xml', 'image/svg+xml', 'application/xhtml+xml'].map(function (t) {"
             "  var d = xml('<Root/>', t); return d.contentType + ' ' + d.documentElement.nodeName + ' ' + d.documentElement.namespaceURI + ' ' + d.childNodes.length; }).join('|')") ==
           "application/xml Root null 1|text/xml Root null 1|image/svg+xml Root null 1|application/xhtml+xml Root null 1");
    assert(S("caught(function () { xml('<a/>', 'text/plain'); })") == "TypeError");
    assert(S("var d = xml('<a/>'); [d.URL, d.characterSet, d.compatMode, d.defaultView, d.location, d.doctype].join(' ')") ==
           "https://example.test/dir/page.html UTF-8 CSS1Compat   ");

    // 2. Names are case-sensitive, in the tree and for whoever searches it.
    assert(S("var d = xml('<Root A=\"1\" a=\"2\"><Child/></Root>'), r = d.documentElement;"
             "[r.tagName, r.getAttribute('A'), r.getAttribute('a'), r.firstChild.nodeName, d.querySelector('Child') !== null,"
             " d.querySelector('child'), d.getElementsByTagName('child').length, d.getElementsByTagName('Child').length].join(' ')") ==
           "Root 1 2 Child true  0 1");

    // 3. Namespaces: of elements, of attributes, declarations as attributes
    //    of their own, and the three questions a node answers about them.
    assert(S("var d = xml('<r xmlns=\"urn:d\" xmlns:p=\"urn:p\" p:a=\"1\" b=\"2\"><p:c/><d xmlns=\"\"/></r>'), r = d.documentElement, a = r.attributes;"
             "[r.namespaceURI, r.prefix, r.localName, a.length, a[0].name, a[0].namespaceURI, a[1].name, a[1].prefix, a[1].localName,"
             " a[2].name, a[2].namespaceURI, a[2].prefix, a[2].localName, a[3].namespaceURI, r.firstChild.namespaceURI, r.firstChild.prefix,"
             " r.firstChild.localName, r.firstChild.tagName, r.lastChild.namespaceURI].join(' ')") ==
           "urn:d  r 4 xmlns http://www.w3.org/2000/xmlns/ xmlns:p xmlns p p:a urn:p p a  urn:p p c p:c ");
    assert(S("[r.getAttributeNS('urn:p', 'a'), r.getAttributeNS(null, 'b'), r.getAttributeNS('urn:p', 'b'), r.hasAttributeNS('urn:p', 'a'),"
             " r.lookupNamespaceURI('p'), r.lookupNamespaceURI(null), r.lookupNamespaceURI('q'), r.lookupPrefix('urn:p'), r.lookupPrefix('urn:d'),"
             " r.isDefaultNamespace('urn:d'), r.lastChild.isDefaultNamespace(null), r.lastChild.lookupNamespaceURI('p'),"
             " r.firstChild.lookupNamespaceURI(null)].join(' ')") ==
           "1 2  true urn:p urn:d  p  true true urn:p urn:d");

    // 4. An element in the XHTML or SVG namespace is the element HTML and
    //    SVG know, whatever document it is in; any other is an Element.
    assert(S("var d = xml('<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><p id=\"p\">x</p><svg xmlns=\"http://www.w3.org/2000/svg\"/><P/></body></html>');"
             "var p = d.getElementById('p');"
             "[p.constructor.name, p.tagName, d.body.constructor.name, p.nextSibling.constructor.name, p.nextSibling.nextSibling.constructor.name,"
             " xml('<p/>').documentElement.constructor.name].join(' ')") ==
           "HTMLParagraphElement p HTMLBodyElement SVGSVGElement HTMLUnknownElement Element");

    // 5. CDATA sections and processing instructions are nodes of their own
    //    kinds, in the document and above its element; the XML declaration
    //    is not a node.
    assert(S("var d = xml('<?xml version=\"1.0\"?><!--c--><?pi d a t a?><a><![CDATA[<x>&]]>t<?q?></a>'), a = d.documentElement;"
             "[d.childNodes.length, d.firstChild.nodeType, d.childNodes[1].nodeType, d.childNodes[1].target, d.childNodes[1].data,"
             " d.childNodes[1].nodeName, d.childNodes[1].constructor.name, a.childNodes.length, a.firstChild.nodeType,"
             " a.firstChild.constructor.name, a.firstChild.nodeName, a.firstChild.data, a.firstChild instanceof Text,"
             " a.lastChild.target, a.lastChild.data === '', a.textContent].join('|')") ==
           "3|8|7|pi|d a t a|pi|ProcessingInstruction|3|4|CDATASection|#cdata-section|<x>&|true|q|true|<x>&t");

    // 6. The five entities and character references; nothing else unless
    //    the document declares it.
    assert(S("xml('<a b=\"&lt;&amp;&gt;&quot;&apos;&#65;&#x42;\">&lt;&amp;&gt;&quot;&apos;&#67;</a>').documentElement.getAttribute('b')") == "<&>\"'AB");
    assert(S("xml('<a>&lt;&amp;&gt;&quot;&apos;&#67;</a>').documentElement.textContent") == "<&>\"'C");
    assert(S("xml('<a>&nbsp;</a>').documentElement.nodeName") == "parsererror");
    assert(S("var d = xml('<!DOCTYPE r PUBLIC \"pub\" \"sys\" [<!ENTITY e \"v\"><!ATTLIST r d CDATA \"def\">]><r>&e;</r>');"
             "[d.doctype.name, d.doctype.publicId, d.doctype.systemId, d.documentElement.textContent, d.documentElement.getAttribute('d'),"
             " S.serializeToString(d)].join('|')") ==
           "r|pub|sys|v|def|<!DOCTYPE r PUBLIC \"pub\" \"sys\"><r d=\"def\">v</r>");

    // 7. Not well formed: no exception. The document is one parsererror
    //    element, in the namespace Firefox gave it and the HTML standard
    //    took, holding the reason as text.
    assert(S("var d = xml('<a><b></a>'), r = d.documentElement;"
             "[r.nodeName, r.namespaceURI, d.childNodes.length, r.childNodes.length, r.firstChild.nodeType, r.textContent.indexOf('line 1') >= 0,"
             " d.getElementsByTagName('parsererror').length,"
             " d.getElementsByTagNameNS('http://www.mozilla.org/newlayout/xml/parsererror.xml', 'parsererror').length].join(' ')") ==
           "parsererror http://www.mozilla.org/newlayout/xml/parsererror.xml 1 1 3 true 1 1");
    assert(S("['', 'text', '<a>', '<a/><b/>', '<p:a/>', '<a b=1/>', '<a>&x;</a>'].map(function (t) { return xml(t).documentElement.nodeName; }).join()") ==
           "parsererror,parsererror,parsererror,parsererror,parsererror,parsererror,parsererror");
    // Nothing of the half-read document is left beside the error.
    assert(S("var d = xml('<!DOCTYPE a><!--c--><a><b/><c>'); d.childNodes.length + ' ' + d.doctype") == "1 null");

    // 8. A script is not run, and nothing is fetched: this document has no
    //    window. An external entity is not read.
    js.eval("var ran = 0; xml('<html xmlns=\"http://www.w3.org/1999/xhtml\"><script>ran = 1;</script><img src=\"x\" onerror=\"ran = 2\"/></html>');");
    assert(S("ran") == "0");
    assert(S("var d = xml('<!DOCTYPE r [<!ENTITY x SYSTEM \"file:///etc/passwd\">]><r>&x;</r>'); d.documentElement.nodeName + '[' + d.documentElement.textContent + ']'") == "r[]");

    // 9. A lone surrogate in the string cannot be in an XML document: the
    //    string is made well-formed Unicode first, as the standard says.
    assert(S("xml('<a>\\uD800</a>').documentElement.textContent === '\\uFFFD'") == "true");

    // 10. What the hostile documents of the parser's own test do here.
    assert(S("var e = '<!DOCTYPE l [<!ENTITY a \"aaaaaaaaaa\">', n = 'abcdefghij';"
             "for (var i = 1; i < 10; i++) { var r = ''; for (var k = 0; k < 10; k++) r += '&' + n[i - 1] + ';'; e += '<!ENTITY ' + n[i] + ' \"' + r + '\">'; }"
             "xml(e + ']><l>&j;</l>').documentElement.nodeName") == "parsererror");
    assert(S("var n = 200000, d = xml(new Array(n + 1).join('<a>') + new Array(n + 1).join('</a>')), depth = 0;"
             "for (var e = d.documentElement; e; e = e.firstChild) depth++;"
             "depth + ' ' + S.serializeToString(d).length + ' ' + d.getElementsByTagName('a').length") == "200000 1399997 200000");
    js.eval("d = null; e = null; gc();");

    // 11. Documents a script makes: createDocument gives an XMLDocument with
    //     the content type of its namespace; new Document() an XML document
    //     with no element. Neither lowercases a name.
    assert(S("var I = document.implementation, a = I.createDocument('http://www.w3.org/1999/xhtml', 'html'), b = I.createDocument('http://www.w3.org/2000/svg', 'svg'),"
             " c = I.createDocument('urn:x', 'p:r', I.createDocumentType('r', '', '')), n = I.createDocument(null, null);"
             "[a.contentType, a.documentElement.constructor.name, b.contentType, b.documentElement.constructor.name, c.contentType, c.doctype.name,"
             " S.serializeToString(c), n.documentElement, I.createDocument(null, '').documentElement, I.createDocument(null, undefined).documentElement.nodeName,"
             " a.constructor.name, a instanceof XMLDocument, a instanceof Document, b.rootElement === b.documentElement, a.rootElement].join(' ')") ==
           "application/xhtml+xml HTMLHtmlElement image/svg+xml SVGSVGElement application/xml r <!DOCTYPE r><p:r xmlns:p=\"urn:x\"/>   undefined XMLDocument true true true ");
    assert(S("var d = new Document(); [d.contentType, d.constructor.name, d.URL, d.documentElement, d.createElement('A').localName,"
             " d.createElement('a').namespaceURI, d.createElement('a').constructor.name].join(' ')") ==
           "application/xml Document about:blank  A  Element");
    assert(S("var d = xml('<a/>'); [caught(function () { d.write('x'); }), caught(function () { d.open(); }), caught(function () { d.close(); }),"
             " d.createElement('B').localName, d.body, d.head, d.title, d.forms.length].join(' ')") ==
           "InvalidStateError InvalidStateError InvalidStateError B    0");
    assert(S("caught(function () { document.createCDATASection('x'); }) + ' ' + xml('<a/>').createCDATASection('x').data") == "NotSupportedError x");
}

TEST(xml_serializer_writes_a_tree_that_reads_back_the_same) {
    using namespace litebird;
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    js.load_page("<!DOCTYPE html><body></body>");
    js.notify_document_loaded();
    js.eval(R"JS(
        var P = new DOMParser(), S = new XMLSerializer(), XMLNS = 'http://www.w3.org/2000/xmlns/';
        function xml(text) { return P.parseFromString(text, 'application/xml'); }
        var X = document.implementation.createDocument(null, null);
        function el(ns, name) { return X.createElementNS(ns, name); }
        function strict(build) { var r = el(null, 'r'); build(r); try { return r.innerHTML; } catch (e) { return e.name; } }
    )JS");
    auto S = [&](const char* expression) { return js.eval(expression).to_string(); };

    // 1. A document: the DOCTYPE, comments, processing instructions and
    //    CDATA sections as they are; no XML declaration.
    assert(S("S.serializeToString(xml('<?xml version=\"1.0\" encoding=\"UTF-8\"?><!DOCTYPE a><!--c--><a><?p d?><![CDATA[x]]><b/></a>'))") ==
           "<!DOCTYPE a><!--c--><a><?p d?><![CDATA[x]]><b/></a>");

    // 2. What is escaped. In text: & < >. In an attribute value: & < > "
    //    and the three characters a parser would turn into spaces. (The
    //    working draft of September 2026 dropped '>' from the attribute
    //    list while its note still names it; every browser writes &gt;.)
    assert(S("var r = el(null, 'r'); r.setAttribute('a', '<>&\"\\'\\t\\n\\r\\u00a0'); r.textContent = '<>&\"\\'\\t\\n\\u00a0'; S.serializeToString(r)") ==
           "<r a=\"&lt;&gt;&amp;&quot;'&#9;&#xA;&#xD;\xC2\xA0\">&lt;&gt;&amp;\"'\t\n\xC2\xA0</r>");

    // 3. The identifiers of a DOCTYPE in the quote they do not hold.
    assert(S("[['p', 's'], ['p\"q', \"s'\"], [\"p'q\", 's\"t'], ['', 's'], ['p', '']].map(function (ids) {"
             "  return S.serializeToString(document.implementation.createDocumentType('r', ids[0], ids[1])); }).join(' ')") ==
           "<!DOCTYPE r PUBLIC \"p\" \"s\"> <!DOCTYPE r PUBLIC 'p\"q' \"s'\"> <!DOCTYPE r PUBLIC \"p'q\" 's\"t'> <!DOCTYPE r SYSTEM \"s\"> <!DOCTYPE r PUBLIC \"p\">");

    // 4. HTML elements: the names that have no end tag in HTML are written
    //    with " /", the others with one; the namespace is declared once.
    assert(S("var d = document.createElement('div'); d.innerHTML = '<br><img src=\"a.png\"><p>x</p><input disabled>'; S.serializeToString(d)") ==
           "<div xmlns=\"http://www.w3.org/1999/xhtml\"><br /><img src=\"a.png\" /><p>x</p><input disabled=\"\" /></div>");
    assert(S("var f = new DocumentFragment(); f.append(document.createElement('div'), document.createElement('span')); S.serializeToString(f)") ==
           "<div xmlns=\"http://www.w3.org/1999/xhtml\"></div><span xmlns=\"http://www.w3.org/1999/xhtml\"></span>");

    // 5. A declaration that says what is already so is left out; one that
    //    is needed to leave a default namespace is written.
    assert(S("S.serializeToString(xml('<root xmlns=\"uri1\"><child xmlns=\"uri1\"/></root>'))") == "<root xmlns=\"uri1\"><child/></root>");
    assert(S("var r = el('urn:a', 'r'); r.appendChild(el(null, 'c')).appendChild(el('urn:a', 'd')); S.serializeToString(r)") ==
           "<r xmlns=\"urn:a\"><c xmlns=\"\"><d xmlns=\"urn:a\"/></c></r>");
    // The xml prefix is never declared.
    assert(S("var r = el(null, 'r'); r.setAttributeNS('http://www.w3.org/XML/1998/namespace', 'xml:lang', 'en');"
             "r.appendChild(el('http://www.w3.org/XML/1998/namespace', 'xml:x')); S.serializeToString(r)") == "<r xml:lang=\"en\"><xml:x/></r>");

    // 6. An attribute keeps its prefix (the draft of September 2026, and
    //    what browsers always did); it is declared where it is used, again
    //    if an ancestor has the prefix for another namespace.
    assert(S("var r = el(null, 'r'); r.setAttributeNS('uri2', 'p:name', 'v'); S.serializeToString(r)") == "<r xmlns:p=\"uri2\" p:name=\"v\"/>");
    assert(S("var r = xml('<root xmlns:p=\"uri1\"><child/></root>').documentElement; r.firstChild.setAttributeNS('uri2', 'p:foobar', 'v'); S.serializeToString(r)") ==
           "<root xmlns:p=\"uri1\"><child xmlns:p=\"uri2\" p:foobar=\"v\"/></root>");
    // With no prefix it gets a made-up one: ns1, ns2, ... counted over the
    // whole text.
    assert(S("var r = el(null, 'r'); r.setAttributeNS('u1', 'a', '1'); r.setAttributeNS('u2', 'b', '2');"
             "var c = el(null, 'c'); c.setAttributeNS('u3', 'c', '3'); r.appendChild(c); S.serializeToString(r)") ==
           "<r xmlns:ns1=\"u1\" ns1:a=\"1\" xmlns:ns2=\"u2\" ns2:b=\"2\"><c xmlns:ns3=\"u3\" ns3:c=\"3\"/></r>");

    // 7. Where the standard's algorithm writes text that reads back as
    //    another tree, or as no tree. Litebird does not follow it there
    //    (w3c/DOM-Parsing issues 44, 45, 75, 83; the web-platform-tests
    //    that assert the algorithm are listed in tests/wpt/README.md).
    //
    //    44: a made-up prefix must not be one that is declared here.
    //    The standard: <child xmlns:ns1="uri1" xmlns:ns1="uri3" ...>.
    assert(S("var r = xml('<root xmlns:ns2=\"uri2\"><child xmlns:ns1=\"uri1\"/></root>').documentElement;"
             "r.firstChild.setAttributeNS('uri3', 'attr1', 'value1'); S.serializeToString(r)") ==
           "<root xmlns:ns2=\"uri2\"><child xmlns:ns1=\"uri1\" xmlns:ns3=\"uri3\" ns3:attr1=\"value1\"/></root>");
    //    45, 75: q was u1 on the outer element and is u2 on the inner one.
    //    The standard writes q:name for an attribute in u1; read back, it
    //    is in u2.
    assert(S("var r = xml('<el1 xmlns:p=\"u1\" xmlns:q=\"u1\"><el2 xmlns:q=\"u2\"/></el1>').documentElement;"
             "r.firstChild.setAttributeNS('u1', 'name', 'v'); S.serializeToString(r)") ==
           "<el1 xmlns:p=\"u1\" xmlns:q=\"u1\"><el2 xmlns:q=\"u2\" p:name=\"v\"/></el1>");
    assert(S("var back = xml(S.serializeToString(r)).documentElement.firstChild; back.getAttributeNS('u1', 'name') + ' ' + back.attributes.length") == "v 2");
    //    83: an attribute named xmlns in no namespace (setAttribute, or the
    //    HTML parser on <html xmlns="...">) is not a declaration, and
    //    written out it would be one: of another namespace than the
    //    element's, or a second xmlns on one tag.
    assert(S("var r = el(null, 'r'); r.setAttribute('xmlns', 'urn:q'); var a = el('urn:a', 'a'); a.setAttribute('xmlns', 'urn:q');"
             "S.serializeToString(r) + ' ' + S.serializeToString(a)") == "<r/> <a xmlns=\"urn:a\"/>");
    assert(S("var h = P.parseFromString('<html xmlns=\"http://www.w3.org/1999/xhtml\" lang=\"en\"><body></body></html>', 'text/html');"
             "h.documentElement.attributes[0].namespaceURI + ' ' + S.serializeToString(h.documentElement)") ==
           "null <html xmlns=\"http://www.w3.org/1999/xhtml\" lang=\"en\"><head></head><body></body></html>");

    // 8. XMLSerializer does not refuse what XML cannot hold: it writes it,
    //    and the text is not XML. That is the standard, and scripts rely on
    //    serializeToString not throwing.
    assert(S("var r = el(null, 'r'); r.appendChild(X.createComment('a--b-')); var c = X.createCDATASection('x'); c.data = 'a]]>b'; r.appendChild(c);"
             "r.appendChild(X.createTextNode('cr\\rhere')); var p = X.createProcessingInstruction('t', 'x'); p.data = 'a?>b'; r.appendChild(p);"
             "S.serializeToString(r)") == "<r><!--a--b---><![CDATA[a]]>b]]>cr\rhere<?t a?>b?></r>");
    assert(S("var r = el(null, 'root'); r.setAttributeNS(XMLNS, 'xmlns:foo', ''); S.serializeToString(r)") == "<root xmlns:foo=\"\"/>");
    //    A carriage return in text is written as it is, so it reads back as
    //    a line feed: the one thing here that is lost without a word.
    assert(S("var r = el(null, 'r'); r.textContent = 'a\\rb'; xml(S.serializeToString(r)).documentElement.textContent === 'a\\nb'") == "true");

    // 9. innerHTML and outerHTML of an XML document do refuse ("require
    //    well-formed"): InvalidStateError for each of those, and for a CDATA
    //    section holding ]]>, which the standard would write.
    assert(S("[strict(function (r) { r.appendChild(X.createComment('a--b')); }), strict(function (r) { r.appendChild(X.createComment('a-')); }),"
             " strict(function (r) { var c = X.createCDATASection('x'); c.data = 'a]]>b'; r.appendChild(c); }),"
             " strict(function (r) { var p = X.createProcessingInstruction('t', 'x'); p.data = 'a?>b'; r.appendChild(p); }),"
             " strict(function (r) { r.appendChild(X.createTextNode('bad\\u0001char')); }),"
             " strict(function (r) { var c = el(null, 'c'); c.setAttributeNS(XMLNS, 'xmlns:p', ''); r.appendChild(c); }),"
             " strict(function (r) { r.appendChild(X.createTextNode('fine')); })].join(' ')") ==
           "InvalidStateError InvalidStateError InvalidStateError InvalidStateError InvalidStateError InvalidStateError fine");
    //    xmlns="" on an element with a prefix is well formed and harmless;
    //    the standard's text refuses it (issue 48).
    assert(S("strict(function (r) { var c = el('urn:a', 'p:c'); c.setAttributeNS(XMLNS, 'xmlns', ''); r.appendChild(c); })") ==
           "<p:c xmlns:p=\"urn:a\" xmlns=\"\"/>");

    // 10. What is not a node, and what is a node with nothing to write.
    assert(S("var name = 'no error'; try { S.serializeToString({}); } catch (e) { name = e.name; } name") == "TypeError");
    assert(S("var r = el(null, 'r'); r.setAttribute('a', '1'); JSON.stringify([S.serializeToString(r.attributes[0]), S.serializeToString(X.createDocumentFragment())])") == "[\"\",\"\"]");

    // 11. A wide tree and a wide element cost their size: one scope of
    //     prefixes for the whole walk, taken back as the walk leaves each
    //     element. (A copy of the scope per element, which is how the
    //     standard is written, made 50,000 declarations on a root with
    //     50,000 children cost their product.)
    const auto start = std::chrono::steady_clock::now();
    assert(S("var r = el(null, 'r'); for (var i = 0; i < 50000; i++) r.setAttributeNS(XMLNS, 'xmlns:p' + i, 'urn:n' + i);"
             "for (var i = 0; i < 50000; i++) r.appendChild(el('urn:n' + i, 'p' + i + ':c'));"
             "var text = S.serializeToString(r), back = xml(text).documentElement;"
             "[text.length > 1000000, back.childNodes.length, back.lastChild.namespaceURI, back.lastChild.prefix, text.indexOf('ns1')].join(' ')") ==
           "true 50000 urn:n49999 p49999 -1");
    assert(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < 60.0);
}

TEST(inner_html_of_an_xml_document_is_xml) {
    using namespace litebird;
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    js.load_page("<!DOCTYPE html><body></body>");
    js.notify_document_loaded();
    js.eval(R"JS(
        var P = new DOMParser();
        function xml(text) { return P.parseFromString(text, 'application/xml'); }
        function caught(f) { try { f(); return 'no error'; } catch (e) { return e.name; } }
    )JS");
    auto S = [&](const char* expression) { return js.eval(expression).to_string(); };

    // 1. The text is parsed as XML, with the namespaces in scope where it
    //    goes: the default one and every prefix.
    assert(S("var d = xml('<r xmlns=\"urn:d\" xmlns:p=\"urn:p\"><host/></r>'), h = d.documentElement.firstChild;"
             "h.innerHTML = '<p:a/><b>t</b><![CDATA[c]]><?pi x?><!--k-->';"
             "[h.childNodes.length, h.firstChild.namespaceURI, h.childNodes[1].namespaceURI, h.childNodes[2].nodeType, h.childNodes[3].nodeType,"
             " h.childNodes[4].nodeType].join(' ')") == "5 urn:p urn:d 4 7 8");
    //    And written as XML: each child says where it stands.
    assert(S("h.innerHTML") == "<p:a xmlns:p=\"urn:p\"/><b xmlns=\"urn:d\">t</b><![CDATA[c]]><?pi x?><!--k-->");
    assert(S("h.outerHTML") == "<host xmlns=\"urn:d\"><p:a xmlns:p=\"urn:p\"/><b>t</b><![CDATA[c]]><?pi x?><!--k--></host>");

    // 2. Text that is not well formed is a SyntaxError, and the element
    //    keeps what it had. HTML would have made something of it.
    assert(S("var d = xml('<r><host>keep</host></r>'), h = d.documentElement.firstChild;"
             "[caught(function () { h.innerHTML = '<unclosed>'; }), caught(function () { h.innerHTML = '<q:a/>'; }),"
             " caught(function () { h.innerHTML = 'a &nbsp; b'; }), caught(function () { h.innerHTML = '<a></b>'; }), h.textContent].join(' ')") ==
           "SyntaxError SyntaxError SyntaxError SyntaxError keep");
    assert(S("h.innerHTML = 'text &amp; <more/>'; h.innerHTML + ' ' + h.childNodes.length") == "text &amp; <more/> 2");

    // 3. No HTML rules: names keep their case, <br></br> is one element,
    //    <title> holds elements, nothing is moved to a <head>.
    assert(S("var d = xml('<r/>'); d.documentElement.innerHTML = '<DIV><br></br><Title><i/></Title></DIV>';"
             "d.documentElement.innerHTML + ' ' + d.documentElement.firstChild.constructor.name") == "<DIV><br/><Title><i/></Title></DIV> Element");

    // 4. outerHTML, insertAdjacentHTML and createContextualFragment go the
    //    same way.
    assert(S("var d = xml('<r><a/></r>'), a = d.documentElement.firstChild; a.insertAdjacentHTML('beforebegin', '<x/>');"
             "a.insertAdjacentHTML('afterend', 'tail'); a.outerHTML = '<y/><z/>'; d.documentElement.innerHTML") == "<x/><y/><z/>tail");
    assert(S("var d = xml('<r xmlns=\"urn:d\"/>'), range = d.createRange(); range.selectNodeContents(d.documentElement);"
             "range.createContextualFragment('<a/>').firstChild.namespaceURI") == "urn:d");
    assert(S("var d = xml('<r><a/></r>'); caught(function () { d.documentElement.firstChild.insertAdjacentHTML('afterbegin', '<b>'); })") == "SyntaxError");

    // 5. An XHTML <template> in an XML document: what the parser puts in
    //    it goes to its content, as in HTML.
    assert(S("var d = xml('<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><template><b/>t</template></body></html>'), t = d.querySelector('template');"
             "[t.childNodes.length, t.content.childNodes.length, t.innerHTML].join(' ')") == "0 2 <b xmlns=\"http://www.w3.org/1999/xhtml\"></b>t");

    // 6. An HTML document is not touched by any of this: its innerHTML is
    //    HTML whatever namespace the element is in.
    assert(S("var s = document.createElementNS('http://www.w3.org/2000/svg', 'svg'); document.body.appendChild(s); s.innerHTML = '<circle r=1><unclosed>';"
             "s.innerHTML + ' ' + s.firstChild.namespaceURI") == "<circle r=\"1\"><unclosed></unclosed></circle> http://www.w3.org/2000/svg");
}

TEST(dom_names_are_checked_by_the_dom_standard) {
    using namespace litebird;
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    js.load_page("<!DOCTYPE html><body></body>");
    js.notify_document_loaded();
    js.eval(R"JS(
        var X = document.implementation.createDocument(null, null);
        function each(list, f) { return list.map(function (item) { try { return String(f(item)); } catch (e) { return e.name; } }).join(' '); }
    )JS");
    auto S = [&](const char* expression) { return js.eval(expression).to_string(); };

    // The DOM stopped asking for XML names in 2025: a name is what the HTML
    // parser could have made. So 'a<b' is an element and 'a b' is not.
    //
    // 1. An element's local name. One that begins with a letter may hold
    //    anything but white space, NUL, '/' and '>'; any other must begin
    //    with ':', '_' or a character past ASCII and go on with name
    //    characters.
    assert(S("each(['a', 'a<b', 'a:b', 'A', '', 'a b', '1a', 'a\\u00e9', ':a', '_a', 'a>b', '-a', 'a/b', 'a=b', '_a<'], function (n) { return document.createElement(n).localName; })") ==
           "a a<b a:b a InvalidCharacterError InvalidCharacterError InvalidCharacterError a\xC3\xA9 :a _a InvalidCharacterError InvalidCharacterError InvalidCharacterError a=b InvalidCharacterError");

    // 2. A qualified name with its namespace: the prefix is what stands
    //    before the first colon; xml and xmlns go with their namespaces
    //    only; a prefix needs a namespace.
    assert(S("each([[null, 'p:a'], ['u', 'p:a'], ['u', 'xml:a'], ['http://www.w3.org/XML/1998/namespace', 'xml:a'], ['u', 'xmlns'],"
             " ['http://www.w3.org/2000/xmlns/', 'xmlns'], ['http://www.w3.org/2000/xmlns/', 'xmlns:p'], ['http://www.w3.org/2000/xmlns/', 'a'], ['u', 'a:b:c'], ['u', ':a'],"
             " ['u', 'a:'], ['', 'a'], ['u', '1a']], function (c) { var e = document.createElementNS(c[0], c[1]); return e.prefix + '|' + e.localName + '|' + e.namespaceURI; })") ==
           "NamespaceError p|a|u NamespaceError xml|a|http://www.w3.org/XML/1998/namespace NamespaceError null|xmlns|http://www.w3.org/2000/xmlns/ "
           "xmlns|p|http://www.w3.org/2000/xmlns/ NamespaceError a|b:c|u InvalidCharacterError InvalidCharacterError null|a|null InvalidCharacterError");

    // 3. An attribute's name: anything but white space, NUL, '/', '=', '>'.
    assert(S("var e = document.createElement('div');"
             "each(['a', 'A', 'a:b', '', 'a b', 'a=b', '1', '<', 'a>b', 'a/b', '\\u00e9'], function (n) { e.setAttribute(n, 'v'); return 'ok'; }) + ' ' + e.getAttributeNames().join()") ==
           "ok ok ok InvalidCharacterError InvalidCharacterError InvalidCharacterError ok ok InvalidCharacterError InvalidCharacterError ok a,a:b,1,<,\xC3\xA9");
    assert(S("each(['a', 'a b', '', 'a=b'], function (n) { return document.createAttribute(n).name; }) + ' ' +"
             "each([['u', 'p:a'], [null, 'p:a'], ['u', 'a b'], ['u', 'xmlns:p']], function (c) { e.setAttributeNS(c[0], c[1], 'v'); return 'ok'; })") ==
           "a InvalidCharacterError InvalidCharacterError InvalidCharacterError ok NamespaceError InvalidCharacterError NamespaceError");
    assert(S("each(['b', 'a b', ''], function (n) { return e.toggleAttribute(n); })") == "true InvalidCharacterError InvalidCharacterError");

    // 4. A processing instruction's target is still an XML name, and its
    //    data cannot hold the two characters that would end it; nor a CDATA
    //    section the three that end it. A doctype's name may be empty.
    assert(S("each([function () { return X.createProcessingInstruction('xml', 'x').target; }, function () { return X.createProcessingInstruction('a b', 'x'); },"
             " function () { return X.createProcessingInstruction('', 'x'); }, function () { return X.createProcessingInstruction('t', 'a?>b'); },"
             " function () { return X.createCDATASection('a]]>b'); }, function () { return document.createProcessingInstruction('t', 'd').nodeName; },"
             " function () { return document.implementation.createDocumentType('a b', '', '').name; },"
             " function () { return '[' + document.implementation.createDocumentType('', '', '').name + ']'; },"
             " function () { return document.implementation.createDocumentType('a>', '', '').name; },"
             " function () { return document.implementation.createDocumentType('a<b', '', '').name; }], function (f) { return f(); })") ==
           "xml InvalidCharacterError InvalidCharacterError InvalidCharacterError InvalidCharacterError t InvalidCharacterError [] InvalidCharacterError a<b");
}

TEST(a_document_keeps_one_doctype_and_one_element_in_that_order) {
    using namespace litebird;
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    js.load_page("<!DOCTYPE html><body></body>");
    js.notify_document_loaded();
    js.eval(R"JS(
        var S = new XMLSerializer(), I = document.implementation;
        function caught(f) { try { f(); return 'ok'; } catch (e) { return e.name; } }
    )JS");
    auto S = [&](const char* expression) { return js.eval(expression).to_string(); };

    // 1. What a document may hold, checked before anything is moved: one
    //    element, one doctype, the doctype first, no text.
    assert(S("var d = I.createDocument(null, null), dt = I.createDocumentType('r', '', ''), r = d.createElement('r'); d.appendChild(r);"
             "[caught(function () { d.appendChild(dt); }), caught(function () { d.insertBefore(dt, r); }), d.doctype === dt, d.childNodes.length, d.firstChild === dt,"
             " caught(function () { d.appendChild(d.createElement('second')); }), caught(function () { d.insertBefore(d.createElement('x'), dt); }),"
             " caught(function () { d.appendChild(d.createTextNode('t')); }), caught(function () { d.insertBefore(I.createDocumentType('q', '', ''), r); }),"
             " caught(function () { d.replaceChild(d.createElement('x'), dt); }), caught(function () { d.replaceChild(I.createDocumentType('q', '', ''), r); }),"
             " d.childNodes.length].join(' ')") ==
           "HierarchyRequestError ok true 2 true HierarchyRequestError HierarchyRequestError HierarchyRequestError HierarchyRequestError HierarchyRequestError HierarchyRequestError 2");

    // 2. document.doctype is the node a script put there, follows it out
    //    and back in, and is written where it stands among the children.
    assert(S("d.removeChild(dt); var gone = d.doctype; d.replaceChild(dt, r); var swapped = [d.doctype === dt, d.documentElement]; d.appendChild(r);"
             "var first = S.serializeToString(d); d.insertBefore(d.createComment('c'), dt);"
             "[String(gone), swapped.join(), first, S.serializeToString(d), dt.ownerDocument === d, dt.parentNode === d].join(' ')") ==
           "null true, <!DOCTYPE r><r/> <!--c--><!DOCTYPE r><r/> true true");

    // 3. A fragment counts as its children.
    assert(S("var d = I.createDocument(null, null), f = d.createDocumentFragment(); f.appendChild(d.createElement('a')); f.appendChild(d.createElement('b'));"
             "var two = caught(function () { d.appendChild(f); }); f.removeChild(f.lastChild); f.appendChild(d.createComment('c')); d.appendChild(f);"
             "var g = d.createDocumentFragment(); g.appendChild(d.createTextNode('t'));"
             "[two, S.serializeToString(d), caught(function () { d.appendChild(g); }), caught(function () { d.prepend('text'); })].join(' ')") ==
           "HierarchyRequestError <a/><!--c--> HierarchyRequestError HierarchyRequestError");

    // 4. The same in an HTML document, whose doctype the parser made; the
    //    mode the parser chose does not change with it.
    assert(S("var d = I.createHTMLDocument('t'), old = d.doctype, out = [old.name, d.childNodes.length]; d.removeChild(old); out.push(String(d.doctype), d.compatMode);"
             "var n = I.createDocumentType('html', 'p', 's'); d.insertBefore(n, d.documentElement); out.push(d.doctype === n, d.doctype.publicId, d.firstChild === n);"
             "d.doctype.remove(); out.push(String(d.doctype), d.childNodes.length); d.prepend(n); out.push(d.doctype === n);"
             "var clone = d.cloneNode(true); out.push(clone.doctype !== n, clone.doctype.publicId, clone.doctype.ownerDocument === clone,"
             " new XMLSerializer().serializeToString(d).indexOf('<!DOCTYPE html PUBLIC \"p\" \"s\">')); out.join(' ')") ==
           "html 2 null CSS1Compat true p true null 1 true true p true 0");
    //    The page's own document.
    assert(S("var mine = document.doctype; document.removeChild(mine); var none = document.doctype; document.insertBefore(mine, document.documentElement);"
             "[String(none), document.doctype === mine, document.childNodes.length, caught(function () { document.appendChild(I.createDocumentType('x', '', '')); })].join(' ')") ==
           "null true 2 HierarchyRequestError");
}

TEST(foreign_attributes_from_the_html_parser_have_namespaces) {
    using namespace litebird;
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    // The HTML parser gives eleven attributes of SVG and MathML elements a
    // namespace ("adjust foreign attributes"). They used to be stored under
    // the names of the html5lib test format ("xlink href"), so no script
    // found xlink:href on any inline <svg>, and innerHTML wrote a name no
    // parser reads.
    js.load_page("<!DOCTYPE html><body><svg id=\"s\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" viewbox=\"0 0 1 1\">"
                 "<a id=\"lnk\" xlink:href=\"#x\" xlink:nope=\"n\" xml:lang=\"en\"><foreignObject><b id=\"b\" xlink:href=\"q\" xmlns=\"u\"></b></foreignObject></a></svg>"
                 "<p id=\"p\" xlink:href=\"x\" xml:lang=\"en\" xmlns:xlink=\"v\">t</p><math id=\"m\" xlink:title=\"T\" definitionurl=\"d\"></math></body>");
    js.notify_document_loaded();
    js.eval("function names(e) { var out = []; for (var i = 0; i < e.attributes.length; i++) { var a = e.attributes[i]; out.push(a.name + '=' + a.prefix + '|' + a.localName + '|' + a.namespaceURI); } return out.join(' '); }"
            "var s = document.getElementById('s'), lnk = document.getElementById('lnk'), b = document.getElementById('b'), p = document.getElementById('p'), m = document.getElementById('m');");
    auto S = [&](const char* expression) { return js.eval(expression).to_string(); };

    assert(S("names(lnk)") == "id=null|id|null xlink:href=xlink|href|http://www.w3.org/1999/xlink xlink:nope=null|xlink:nope|null "
                               "xml:lang=xml|lang|http://www.w3.org/XML/1998/namespace");
    assert(S("names(s)") == "id=null|id|null xmlns=null|xmlns|http://www.w3.org/2000/xmlns/ xmlns:xlink=xmlns|xlink|http://www.w3.org/2000/xmlns/ viewBox=null|viewBox|null");
    assert(S("names(m)") == "id=null|id|null xlink:title=xlink|title|http://www.w3.org/1999/xlink definitionURL=null|definitionURL|null");
    // On an HTML element they are plain names, in the page and inside
    // <foreignObject> alike.
    assert(S("names(p)") == "id=null|id|null xlink:href=null|xlink:href|null xml:lang=null|xml:lang|null xmlns:xlink=null|xmlns:xlink|null");
    assert(S("names(b)") == "id=null|id|null xlink:href=null|xlink:href|null xmlns=null|xmlns|null");

    // A script finds them by either name.
    assert(S("[lnk.getAttribute('xlink:href'), lnk.getAttributeNS('http://www.w3.org/1999/xlink', 'href'), lnk.hasAttribute('xlink:href'),"
             " lnk.getAttributeNS('http://www.w3.org/XML/1998/namespace', 'lang'), lnk.getAttributeNS(null, 'xlink:href'),"
             " p.getAttributeNS('http://www.w3.org/1999/xlink', 'href'), p.getAttribute('xlink:href'),"
             " document.querySelectorAll('[*|href]').length, document.querySelectorAll('[xlink\\\\:href]').length].join(' ')") ==
           "#x #x true en   x 1 2");

    // HTML serialization writes the name the standard gives each; XML
    // serialization declares what needs declaring.
    assert(S("s.outerHTML") ==
           "<svg id=\"s\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" viewBox=\"0 0 1 1\">"
           "<a id=\"lnk\" xlink:href=\"#x\" xlink:nope=\"n\" xml:lang=\"en\"><foreignObject><b id=\"b\" xlink:href=\"q\" xmlns=\"u\"></b></foreignObject></a></svg>");
    assert(S("new XMLSerializer().serializeToString(lnk)") ==
           "<a xmlns=\"http://www.w3.org/2000/svg\" id=\"lnk\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" xlink:href=\"#x\" xlink:nope=\"n\" xml:lang=\"en\">"
           "<foreignObject><b xmlns=\"http://www.w3.org/1999/xhtml\" id=\"b\" xlink:href=\"q\"></b></foreignObject></a>");
    // innerHTML written and read again is the same tree.
    assert(S("var copy = document.createElement('div'); copy.innerHTML = s.outerHTML; names(copy.querySelector('a')) === names(lnk) && copy.innerHTML === s.outerHTML") == "true");

    // The tree dump of the html5lib tests still writes them its way.
    const std::string dump = dump_tree_webplatform(*doc);
    assert(dump.find("xlink href=\"#x\"") != std::string::npos);
    assert(dump.find("xml lang=\"en\"") != std::string::npos);
    assert(dump.find("xmlns xlink=\"http://www.w3.org/1999/xlink\"") != std::string::npos);
    assert(dump.find("xlink:nope=\"n\"") != std::string::npos);
}

TEST(xml_dom_follows_the_standard_where_jsdom_does_not) {
    using namespace litebird;
    // tests/dom-conformance/xml.dat records what jsdom prints. In these
    // places jsdom and the standards differ, so the corpus stays away from
    // them (tests/dom-conformance/README.md) and the standard's answer is
    // asserted here.
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    js.load_page("<!DOCTYPE html><body></body>");
    js.notify_document_loaded();
    js.eval(R"JS(
        var P = new DOMParser(), S = new XMLSerializer();
        function xml(text, type) { return P.parseFromString(text, type || 'application/xml'); }
    )JS");
    auto S = [&](const char* expression) { return js.eval(expression).to_string(); };

    // 1. A processing instruction in an HTML element is written by
    //    innerHTML ("<?" target " " data ">"); jsdom leaves it out.
    assert(S("var d = document.createElement('div'); d.appendChild(document.createProcessingInstruction('t', 'd')); d.appendChild(document.createTextNode('x')); d.innerHTML") == "<?t d>x");

    // 2. document.title on a document whose root is <svg> with no <title>:
    //    a title element as the first child of the root. When the root is
    //    empty, jsdom puts it after the root: a document with two elements,
    //    whose title is still "".
    assert(S("var d = xml('<svg xmlns=\"http://www.w3.org/2000/svg\"/>', 'image/svg+xml'); d.title = 'T'; S.serializeToString(d) + ' ' + d.title + ' ' + d.childNodes.length") ==
           "<svg xmlns=\"http://www.w3.org/2000/svg\"><title>T</title></svg> T 1");
    assert(S("var d = xml('<svg xmlns=\"http://www.w3.org/2000/svg\"><g/></svg>', 'image/svg+xml'); d.title = 'T'; S.serializeToString(d) + ' ' + d.title") ==
           "<svg xmlns=\"http://www.w3.org/2000/svg\"><title>T</title><g/></svg> T");

    // 3. A DOCTYPE with one of the public identifiers the HTML standard
    //    lists (the XHTML and MathML DTDs) declares the entity names of
    //    HTML; jsdom gives a parsererror.
    assert(S("var d = xml('<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">"
             "<html xmlns=\"http://www.w3.org/1999/xhtml\"><body>&nbsp;&eacute;&hellip;</body></html>');"
             "d.documentElement.nodeName + ' ' + (d.documentElement.textContent === '\\u00a0\\u00e9\\u2026')") == "html true");
    assert(S("xml('<!DOCTYPE html PUBLIC \"-//WAPFORUM//DTD XHTML Mobile 1.2//EN\" \"x\"><html xmlns=\"http://www.w3.org/1999/xhtml\">&copy;</html>').documentElement.textContent === '\\u00a9'") == "true");
    //    Any other DOCTYPE does not: with no external subset the name is
    //    not declared, and that is an error of form. With an external
    //    subset nobody reads (this parser reads none) XML does not call it
    //    one (4.1, "Entity Declared"): the reference stands for nothing.
    assert(S("xml('<!DOCTYPE html><html xmlns=\"http://www.w3.org/1999/xhtml\">&nbsp;</html>').documentElement.nodeName") == "parsererror");
    assert(S("var d = xml('<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"x\"><html>a&nbsp;b</html>');"
             "d.documentElement.nodeName + ' [' + d.documentElement.textContent + ']'") == "html [ab]");

    // 4. The copy of a document DOMParser made is a Document, as it is
    //    itself; jsdom makes the copy an XMLDocument.
    assert(S("var d = xml('<a/>'), x = document.implementation.createDocument(null, 'a');"
             "[d.constructor.name, d.cloneNode(true).constructor.name, d.cloneNode(true).contentType, x.constructor.name, x.cloneNode(true).constructor.name].join(' ')") ==
           "Document Document application/xml XMLDocument XMLDocument");

    // 5. dir and designMode are on every Document; jsdom has them on HTML
    //    documents only.
    assert(S("var d = xml('<a/>'); typeof d.dir + ' [' + d.dir + '] ' + d.designMode") == "string [] off");

    // 6. activeElement with no body is the document element; jsdom: null.
    assert(S("var h = document.implementation.createHTMLDocument('t'); h.body.remove();"
             "[xml('<a/>').activeElement.nodeName, h.activeElement.nodeName, document.activeElement.nodeName, new Document().activeElement].join(' ')") == "a HTML BODY ");

    // 7. A document nobody looks at is hidden; jsdom: "prerender".
    assert(S("var d = xml('<a/>'); [d.visibilityState, d.hidden, document.implementation.createHTMLDocument('t').visibilityState].join(' ')") == "hidden true hidden");

    // 8. The XML serializer is the working draft of September 2026; jsdom's
    //    is the one before (xmlns:ns1 for an attribute that had a prefix,
    //    &#x9; for a tab, double quotes around any DOCTYPE identifier).
    assert(S("var r = document.implementation.createDocument(null, 'r').documentElement; r.setAttributeNS('urn:u', 'p:a', 'tab\\there'); S.serializeToString(r)") ==
           "<r xmlns:p=\"urn:u\" p:a=\"tab&#9;here\"/>");
    assert(S("S.serializeToString(document.implementation.createDocumentType('r', 'say \"hi\"', ''))") == "<!DOCTYPE r PUBLIC 'say \"hi\"'>");
}

TEST(selectors_know_namespaces) {
    using namespace litebird;
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    js.load_page("<!DOCTYPE html><body><svg><a xlink:href=\"#x\"></a></svg><a href=\"#y\"></a></body>");
    js.notify_document_loaded();
    js.eval("function count(d, selector) { try { return d.querySelectorAll(selector).length; } catch (e) { return e.name; } }"
            "var d = new DOMParser().parseFromString('<r xmlns:x=\"urn:x\" xmlns=\"urn:d\"><a x:k=\"1\" k=\"2\"/><x:a/><b xmlns=\"\"/></r>', 'application/xml');");
    auto S = [&](const char* expression) { return js.eval(expression).to_string(); };

    // A name alone is an element of any namespace; *| says so aloud; a bar
    // with nothing before it is "in no namespace". A prefix needs a
    // declaration, and querySelector has nowhere to make one.
    assert(S("['a', '*|a', '|a', '|b', 'b', '*|*', '|*', '*', 'x|a', 'A'].map(function (s) { return count(d, s); }).join(' ')") == "2 2 0 1 1 4 1 4 SyntaxError 0");
    // An attribute selector with no bar is an attribute in no namespace.
    assert(S("['[k]', '[*|k]', '[|k=\"2\"]', '[*|k=\"1\"]', '[x\\\\:k]', '[x|k]'].map(function (s) { return count(d, s); }).join(' ')") == "1 1 1 1 0 SyntaxError");
    // In an HTML page: the xlink:href of an inline <svg> is not [href], and
    // is [*|href].
    assert(S("['a', '[href]', '[*|href]', '[xlink\\\\:href]', 'svg a', 'svg|a'].map(function (s) { return count(document, s); }).join(' ')") == "2 1 2 0 1 SyntaxError");
    assert(S("document.querySelector('svg a').matches('[*|href=\"#x\"]') + ' ' + document.querySelector('svg a').closest('*|svg').localName") == "true svg");
}

TEST(attribute_lists_keep_their_order_and_stay_fast) {
    using namespace litebird;

    // 1. The list against a plain vector, under random edits, across the
    //    size at which it starts to keep an index (32).
    {
        std::mt19937 random(20261114);
        for (int round = 0; round < 200; ++round) {
            AttributeList list;
            std::vector<std::pair<std::string, std::string>> model;
            const int names = round < 100 ? 12 + round : 400;
            const int steps = round < 100 ? 300 : 3000;
            for (int step = 0; step < steps; ++step) {
                const std::string name = "n" + std::to_string(random() % names);
                const auto in_model = std::find_if(model.begin(), model.end(), [&](const auto& item) { return item.first == name; });
                switch (random() % 6) {
                    case 0: case 1: case 2: {
                        const std::string value = std::to_string(step);
                        list[name] = value;
                        if (in_model == model.end()) model.emplace_back(name, value);
                        else in_model->second = value;
                        break;
                    }
                    case 3: {
                        assert(list.erase(name) == (in_model == model.end() ? 0u : 1u));
                        if (in_model != model.end()) model.erase(in_model);
                        break;
                    }
                    case 4: {
                        const auto found = list.find(name);
                        assert((found == list.end()) == (in_model == model.end()));
                        if (found != list.end()) {
                            assert(found->second == in_model->second);
                            assert(found - list.begin() == in_model - model.begin());
                            if (random() % 4 == 0) {
                                list.erase(found);
                                model.erase(in_model);
                            }
                        }
                        break;
                    }
                    default: {
                        assert(list.count(name) == (in_model == model.end() ? 0u : 1u));
                        AttributeList copy = list;
                        assert(copy == list && copy.size() == model.size());
                        break;
                    }
                }
                assert(list.size() == model.size());
            }
            std::size_t at = 0;
            for (const auto& item : list) {
                assert(item == model[at]);
                assert(list[at] == model[at]);
                ++at;
            }
            assert(at == model.size());
        }
    }

    // 2. The side table of namespaces, against a plain vector.
    {
        std::mt19937 random(7);
        for (int round = 0; round < 100; ++round) {
            AttributeNamespaces table;
            struct Row { std::string qualified, uri; };
            std::vector<Row> model;
            const int names = round < 50 ? 10 + round : 300;
            for (int step = 0; step < 2000; ++step) {
                const int n = static_cast<int>(random() % names);
                const std::string local = "a" + std::to_string(n);
                const std::string qualified = (n % 3 == 0 ? "" : "p" + std::to_string(n % 3) + ":") + local;
                const auto in_model = std::find_if(model.begin(), model.end(), [&](const Row& row) { return row.qualified == qualified; });
                switch (random() % 5) {
                    case 0: case 1: {
                        // One namespace per name most of the time; now and then another.
                        const std::string uri = "urn:" + std::to_string(random() % 8 == 0 ? (n + 1) % 4 : n % 4);
                        table.set(qualified, uri);
                        if (in_model == model.end()) model.push_back({qualified, uri});
                        else in_model->uri = uri;
                        break;
                    }
                    case 2: {
                        table.erase(qualified);
                        if (in_model != model.end()) model.erase(in_model);
                        break;
                    }
                    case 3: {
                        const std::string* found = table.find(qualified);
                        assert((found == nullptr) == (in_model == model.end()));
                        if (found) assert(*found == in_model->uri);
                        break;
                    }
                    default: {
                        // A name has one local part here, so namespace and
                        // local name find at most one entry.
                        for (int k = 0; k < 4; ++k) {
                            const std::string uri = "urn:" + std::to_string(k);
                            const std::string* found = table.find_expanded(uri, local);
                            const bool expected = in_model != model.end() && in_model->uri == uri;
                            assert((found != nullptr) == expected);
                            if (found) assert(*found == qualified);
                        }
                        break;
                    }
                }
                assert(table.size() == model.size());
            }
        }
    }

    // 3. From a script: 50,000 attributes set, read, half of them removed,
    //    and the order is still the order they were added in.
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    js.load_page("<!DOCTYPE html><body></body>");
    js.notify_document_loaded();
    const auto start = std::chrono::steady_clock::now();
    assert(js.eval("var e = document.createElement('div'); for (var i = 0; i < 50000; i++) e.setAttribute('a' + i, i);"
                   "var sum = 0; for (var i = 0; i < 50000; i++) sum += Number(e.getAttribute('a' + i));"
                   "for (var j = 49999; j >= 25000; j--) e.removeAttribute('a' + j);"
                   "e.setAttribute('a3', 'again'); e.setAttribute('z', 'last');"
                   "[sum, e.attributes.length, e.attributes[0].name, e.attributes[3].value, e.attributes[24999].name, e.attributes[25000].name,"
                   " e.getAttribute('a25000'), e.hasAttribute('a24999'), e.outerHTML.length > 300000].join(' ')").to_string() ==
           "1249975000 25001 a0 again a24999 z  true true");
    //    With namespaces: found by namespace and local name.
    assert(js.eval("var n = document.createElementNS('urn:x', 'e'); for (var i = 0; i < 20000; i++) n.setAttributeNS('urn:n' + (i % 7), 'p' + (i % 7) + ':a' + i, i);"
                   "for (var i = 19999; i >= 10000; i--) n.removeAttributeNS('urn:n' + (i % 7), 'a' + i);"
                   "[n.attributes.length, n.getAttributeNS('urn:n3', 'a17'), n.getAttributeNS('urn:n4', 'a17'), n.hasAttributeNS('urn:n' + (9999 % 7), 'a9999'),"
                   " n.hasAttributeNS('urn:n' + (10000 % 7), 'a10000'), n.getAttribute('p3:a17'), n.attributes[9999].localName].join(' ')").to_string() ==
           "10000 17  true false 17 a9999");
    assert(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < 60.0);
}

TEST(xml_http_request_answers_data_urls_and_nothing_else) {
    using namespace litebird;

    // --- MIME types (the MIME Sniffing Standard) ---------------------------
    {
        MimeType type;
        assert(MimeType::parse(" Text/XML ; Charset=\"UTF-8\"; x=1;x=2 ;y ; z=\"a\\\"b\" ;w= ", type));
        assert(type.essence() == "text/xml");
        assert(type.parameter("charset") && *type.parameter("charset") == "UTF-8");
        assert(*type.parameter("x") == "1");                // given twice: the first counts
        assert(!type.parameter("y") && !type.parameter("w"));   // a name and no value
        assert(type.serialize() == "text/xml;charset=UTF-8;x=1;z=\"a\\\"b\"");
        assert(MimeType::parse("text/plain;x=\"a;b\"junk;y=2", type) && type.serialize() == "text/plain;x=\"a;b\";y=2");
        assert(MimeType::parse("a/b;c=\"unclosed", type) && *type.parameter("c") == "unclosed");
        for (const char* bad : {"", "text", "text/", "/xml", "te xt/xml", "text/x ml", "text/xml/", "text/\"xml\"", ";charset=x"}) {
            assert(!MimeType::parse(bad, type));
        }
        const auto kind = [](const char* text) {
            MimeType parsed;
            assert(MimeType::parse(text, parsed));
            return parsed.is_xml() ? 'x' : parsed.is_html() ? 'h' : '-';
        };
        assert(kind("text/xml") == 'x' && kind("application/xml") == 'x' && kind("image/svg+xml") == 'x');
        assert(kind("APPLICATION/RSS+XML;charset=x") == 'x' && kind("a/b+xml") == 'x');
        assert(kind("text/html") == 'h' && kind("application/xhtml+xml") == 'x');
        assert(kind("text/xml-external-parsed-entity") == '-' && kind("application/xml-dtd") == '-' && kind("image/xml") == '-');
        assert(kind("text/plain") == '-' && kind("application/+xml") == '-' && kind("text/xmlx") == '-');
        assert(is_http_token("GET") && is_http_token("X-Custom_1") && !is_http_token("") && !is_http_token("GE T") &&
               !is_http_token("a:b") && !is_http_token("a\n"));
    }

    // --- data: URLs (the Fetch Standard) ------------------------------------
    {
        MimeType type;
        std::string body;
        assert(process_data_url("data:text/xml;base64,PGEvPg==", type, body) && body == "<a/>" && type.serialize() == "text/xml");
        assert(process_data_url("data:text/xml;base64,PGEv Pg", type, body) && body == "<a/>");     // spaces and no padding
        assert(process_data_url("data:text/plain ;  BASE64,YQ==", type, body) && body == "a" && type.serialize() == "text/plain");
        assert(process_data_url("data:;base64,YQ", type, body) && body == "a" && type.serialize() == "text/plain;charset=US-ASCII");
        assert(process_data_url("data:application/octet-stream;base64,AP8=", type, body) && body == std::string("\0\xFF", 2));
        assert(process_data_url("data:,a%20b%zz%4", type, body) && body == "a b%zz%4" && type.serialize() == "text/plain;charset=US-ASCII");
        assert(process_data_url("data:;charset=x,y", type, body) && type.serialize() == "text/plain;charset=x");
        assert(process_data_url("data:text,y", type, body) && type.serialize() == "text/plain;charset=US-ASCII");
        assert(process_data_url("data: TEXT/XML ,%3Ca/%3E", type, body) && body == "<a/>" && type.serialize() == "text/xml");
        assert(process_data_url("data:text/xml;base64;x=1,PGEvPg==", type, body) && body == "PGEvPg==");    // base64 is not last
        assert(process_data_url("data:text/plain,a,b", type, body) && body == "a,b");
        assert(process_data_url("data:text/plain,", type, body) && body.empty());
        assert(!process_data_url("data:text/xml;base64,@@@", type, body));
        assert(!process_data_url("data:text/xml;base64,PGEvP", type, body));     // a length no base64 text has
        assert(!process_data_url("data:text/plain", type, body));                // no comma
        assert(!process_data_url("http://example.test/,", type, body));
    }

    // --- Bytes into text (the Encoding Standard) ------------------------------
    {
        assert(decode_text("\xC3\xA9", "") == "\xC3\xA9");
        assert(decode_text("\xE9", "") == "\xEF\xBF\xBD");
        assert(decode_text("\xE9\x80", " Latin1 ") == "\xC3\xA9\xE2\x82\xAC");   // windows-1252: 0x80 is the euro sign
        assert(decode_text("\xE9", "us-ascii") == "\xC3\xA9" && decode_text("\xE9", "ISO-8859-1") == "\xC3\xA9");
        assert(decode_text(std::string("a\0b\0", 4), "utf-16le") == "ab");
        assert(decode_text(std::string("\0a\0b", 4), "UTF-16BE") == "ab");
        assert(decode_text(std::string("\x3D\xD8\x00\xDE", 4), "utf-16") == "\xF0\x9F\x98\x80");
        assert(decode_text(std::string("\xFE\xFF\0a", 4), "latin1") == "a");     // a byte order mark wins
        assert(decode_text(std::string("\xFF\xFE" "a\0", 4), "utf-8") == "a");
        assert(decode_text("\xEF\xBB\xBF" "a", "utf-16be") == "a");
        assert(decode_text("\xC3\xA9", "bogus") == "\xC3\xA9");                  // an unknown name: UTF-8
        assert(decode_text("\xF0\x9F\x98\x80\xC3", "") == "\xF0\x9F\x98\x80\xEF\xBF\xBD");
        assert(decode_text("\xED\xA0\x80", "") == "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");     // a surrogate is three errors
        assert(decode_text("\xC0\xAF" "a", "") == "\xEF\xBF\xBD\xEF\xBF\xBD" "a");              // no overlong forms
        assert(decode_text("\xE2\x82" "a", "") == "\xEF\xBF\xBD" "a");                          // cut short: one error, and the next byte is read
        assert(decode_text(std::string("\x3D\xD8" "a", 3), "utf-16le") == "\xEF\xBF\xBD");      // half a pair and half a unit: one error
        assert(decode_text(std::string("\x3D\xD8" "a\0", 4), "utf-16le") == "\xEF\xBF\xBD" "a");
        assert(decode_text(std::string("\x00\xDE" "a\0", 4), "utf-16le") == "\xEF\xBF\xBD" "a");
        assert(decode_text(std::string("a\0b", 3), "utf-16le") == "a\xEF\xBF\xBD");
        assert(utf8_decode("\xEF\xBB\xBF" "a") == "a");
        assert(utf8_decode("\xFF\xFE" "a") == "\xEF\xBF\xBD\xEF\xBF\xBD" "a");   // UTF-8 whatever the bytes say

        // The names of the four encodings, as the Encoding Standard lists them.
        assert(encoding_name_for_label(" UTF8 ") == "UTF-8" && encoding_name_for_label("unicode-1-1-utf-8") == "UTF-8");
        assert(encoding_name_for_label("utf-16") == "UTF-16LE" && encoding_name_for_label("UCS-2") == "UTF-16LE");
        assert(encoding_name_for_label("utf-16be") == "UTF-16BE");
        assert(encoding_name_for_label("ascii") == "windows-1252" && encoding_name_for_label("ISO-8859-1") == "windows-1252" &&
               encoding_name_for_label("cp819") == "windows-1252");
        // Any other name is no name: one the standard has and this build
        // cannot read, like one nobody has.
        assert(encoding_name_for_label("").empty() && encoding_name_for_label("bogus").empty() &&
               encoding_name_for_label("shift_jis").empty() && encoding_name_for_label("utf-32").empty() &&
               encoding_name_for_label("\"utf-8\"").empty());
    }

    // --- The encoding of an XML entity (XML 1.0, 4.3.3 and appendix F) ------
    {
        using litebird::xml::decode_xml;
        using litebird::xml::xml_encoding;
        using litebird::xml::XmlError;
        const auto encoding_of = [](std::string_view bytes, std::string_view transport = {}) {
            std::string name;
            XmlError error;
            if (!xml_encoding(bytes, name, error, transport)) return std::string(error.unsupported_encoding ? "unsupported" : "error");
            return name;
        };
        const auto decoded = [](std::string_view bytes, std::string_view transport = {}) {
            std::string utf8, name;
            XmlError error;
            if (!decode_xml(bytes, utf8, name, error, transport)) return std::string("error");
            return utf8;
        };
        const std::string latin1 = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><a>\xE9</a>";
        const std::string le = std::string("<\0a\0/\0>\0", 8), be = std::string("\0<\0a\0/\0>", 8);
        // With nothing from outside: a mark, then the declaration, then UTF-8.
        assert(encoding_of("<a/>") == "UTF-8" && encoding_of(latin1) == "windows-1252");
        assert(encoding_of("\xEF\xBB\xBF<a/>") == "UTF-8" && encoding_of("\xFF\xFE" + le) == "UTF-16LE" && encoding_of("\xFE\xFF" + be) == "UTF-16BE");
        assert(encoding_of("<?xml version=\"1.0\" encoding=\"EUC-JP\"?><a/>") == "unsupported");
        //   A declaration that the bytes contradict is an error of form.
        assert(encoding_of("<?xml version=\"1.0\" encoding=\"UTF-16\"?><a/>") == "error");
        assert(encoding_of("\xEF\xBB\xBF" + latin1) == "error");
        //   UTF-16 with no mark and no declaration cannot be told: it is read as UTF-8.
        assert(encoding_of(le) == "UTF-8");
        // What the transport says outranks all but a mark, and then the
        // declaration has no say (it is not even an error to contradict it).
        assert(encoding_of(le, "UTF-16LE") == "UTF-16LE" && encoding_of(be, "UTF-16BE") == "UTF-16BE");
        assert(decoded(le, "UTF-16LE") == "<a/>" && decoded(be, "UTF-16BE") == "<a/>" && decoded(le, "utf-16") == "<a/>");
        assert(encoding_of(latin1, "UTF-8") == "UTF-8" && encoding_of("<a/>", "windows-1252") == "windows-1252");
        assert(encoding_of("<?xml version=\"1.0\" encoding=\"EUC-JP\"?><a/>", "UTF-8") == "UTF-8");
        assert(encoding_of("\xEF\xBB\xBF<a/>", "windows-1252") == "UTF-8" && encoding_of("\xFF\xFE" + le, "UTF-8") == "UTF-16LE");
        assert(encoding_of("\xEF\xBB\xBF" + latin1, "UTF-8") == "UTF-8");
        //   A name this build does not know is no word from the transport.
        assert(encoding_of(latin1, "bogus") == "windows-1252" && encoding_of("<a/>", "shift_jis") == "UTF-8");
        // Bytes that are no text in the encoding: no document (4.3.3).
        assert(decoded(std::string("<\0a\0>\0\x3D\xD8<\0/\0a\0>\0", 16), "UTF-16LE") == "error");
        assert(decoded(std::string("<\0a\0/\0>", 7), "UTF-16LE") == "error");
        assert(decoded("<a>\xE9</a>", "windows-1252") == "<a>\xC3\xA9</a>");
    }

    // --- The object ---------------------------------------------------------
    // tests/dom-conformance/xhr.dat records what jsdom answers. Here is what
    // jsdom cannot be asked: the places where it differs from the standard,
    // and what a request must not be able to reach.
    const std::string secret_path = "/tmp/litebird-xhr-secret-" + std::to_string(static_cast<long>(::getpid())) + ".xml";
    {
        std::ofstream secret(secret_path);
        secret << "<secret>on disk</secret>";
    }
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/app/page.html");
    JsInterpreter js(doc);
    js.load_page("<!DOCTYPE html><body></body>");
    js.notify_document_loaded();
    auto settle = [&]() {
        for (int i = 0; i < 200 && EventLoop::default_loop().has_pending_tasks(); ++i) EventLoop::default_loop().tick();
        EventLoop::default_loop().run_microtasks();
    };
    auto S = [&](const std::string& expression) { return js.eval(expression).to_string(); };
    js.eval(R"JS(
        // A synchronous request, sent.
        function get(url, setup) {
            var x = new XMLHttpRequest();
            x.open('GET', url, false);
            if (setup) setup(x);
            x.send();
            return x;
        }
        // An asynchronous one: results[name] is what happened, once it has ended.
        var results = {}, requests = {};
        function later(name, url, setup, read) {
            var x = new XMLHttpRequest(), seen = [];
            ['readystatechange', 'loadstart', 'progress', 'load', 'error', 'abort', 'timeout', 'loadend'].forEach(function (type) {
                x.addEventListener(type, function (e) {
                    seen.push(type === 'readystatechange' ? 'state ' + x.readyState : type === 'progress' || type === 'load' ? type + ' ' + e.loaded + '/' + e.total : type);
                    if (type === 'loadend') results[name] = seen.join(', ') + (read ? ' | ' + read(x) : '');
                });
            });
            requests[name] = { x: x, seen: seen };
            if (setup) setup(x);
            x.open('GET', url);
            x.send();
        }
        function asDocument(x) { x.responseType = 'document'; }
    )JS");

    // 1. The document of a response has the response's type; in jsdom every
    //    XML one is application/xml.
    assert(S("get('data:text/xml,<a/>').responseXML.contentType") == "text/xml");
    assert(S("get('data:image/svg+xml,<svg xmlns=\"http://www.w3.org/2000/svg\"/>').responseXML.contentType") == "image/svg+xml");
    assert(S("get('data:application/xhtml+xml,<html xmlns=\"http://www.w3.org/1999/xhtml\"/>').responseXML.contentType") == "application/xhtml+xml");
    assert(S("get('data:Application/XML;charset=utf-8,<a/>').responseXML.contentType") == "application/xml");
    assert(S("get('data:text/plain,<rss/>', function (x) { x.overrideMimeType('application/rss+xml'); }).responseXML.contentType") == "application/rss+xml");
    assert(S("get('data:,<a/>', function (x) { x.overrideMimeType('text/xml'); }).responseXML.documentElement.nodeName") == "a");

    // 2. Every header line ends in CR LF, the last one too; jsdom leaves the
    //    last one open.
    assert(S("get('data:text/xml;charset=UTF-8,<a/>').getAllResponseHeaders()") == "content-type: text/xml;charset=UTF-8\r\n");
    assert(S("new XMLHttpRequest().getAllResponseHeaders()") == "");

    // 3. responseURL has no fragment; the document's address is the address
    //    that was asked for. jsdom keeps the fragment in both.
    assert(S("var x = get('data:text/xml,<a/>#frag'); x.responseURL + ' ' + x.responseXML.URL + ' ' + x.responseText") ==
           "data:text/xml,<a/> data:text/xml,<a/>#frag <a/>");

    // 4. An XML response says what it is encoded in, and both responseXML
    //    and responseText listen; jsdom reads it as UTF-8.
    const std::string latin1 = "'data:text/xml,%3C?xml version=\"1.0\" encoding=\"ISO-8859-1\"?%3E%3Ca%3E%E9%3C/a%3E'";
    assert(S("var x = get(" + latin1 + "); x.responseXML.documentElement.textContent.charCodeAt(0) + ' ' + x.responseText.charCodeAt(x.responseText.length - 5)") == "233 233");
    //    responseType "text" does not look inside: UTF-8, and the byte is no UTF-8.
    js.eval("later('text type', " + latin1 + ", function (x) { x.responseType = 'text'; }, function (x) { return x.response.charCodeAt(x.response.length - 5); });");
    //    The charset of the type outranks the declaration.
    assert(S("get('data:text/xml;charset=utf-8,%3C?xml version=\"1.0\" encoding=\"ISO-8859-1\"?%3E%3Ca%3E%C3%A9%3C/a%3E').responseXML.documentElement.textContent.charCodeAt(0)") == "233");
    //    UTF-16 with a byte order mark.
    assert(S("var x = get('data:text/xml,%FF%FE%3C%00a%00/%00%3E%00'); x.responseXML.documentElement.nodeName + ' ' + x.responseText") == "a <a/>");
    //    An encoding this build cannot read: no document, and the text as UTF-8.
    assert(S("var x = get('data:text/xml,%3C?xml version=\"1.0\" encoding=\"EUC-JP\"?%3E%3Ca/%3E'); String(x.responseXML) + ' ' + x.responseText.length") == "null 43");
    //    Bytes that are no text in the document's encoding: XML calls that
    //    fatal (4.3.3), so there is no document; jsdom puts U+FFFD in their
    //    place and gives one. responseText has the U+FFFD in both.
    assert(S("var x = get('data:text/xml,%3Ca%3E%E9%3C/a%3E'); String(x.responseXML) + ' ' + x.responseText.charCodeAt(3)") == "null 65533");
    assert(S("var x = get('data:text/xml,%3Ca%3E%ED%A0%80%3C/a%3E'); String(x.responseXML) + ' ' + x.responseText.length") == "null 10");
    assert(S("var x = get('data:text/xml;charset=utf-16le,%3C%00a%00%3E%00%3D%D8%3C%00/%00a%00%3E%00'); String(x.responseXML) + ' ' + x.responseText.charCodeAt(3)") == "null 65533");
    //    A declaration the bytes contradict: no document either; jsdom does
    //    not look at the declaration.
    assert(S("String(get('data:text/xml,%3C?xml version=\"1.0\" encoding=\"UTF-16\"?%3E%3Ca/%3E').responseXML)") == "null");
    //    A charset nobody knows is no charset, and then the declaration
    //    decides; jsdom reads UTF-8.
    assert(S("var x = get('data:text/xml;charset=bogus,%3C?xml version=\"1.0\" encoding=\"ISO-8859-1\"?%3E%3Ca%3E%E9%3C/a%3E');"
             "x.responseXML.documentElement.textContent.charCodeAt(0) + ' ' + x.responseText.charCodeAt(x.responseText.length - 5)") == "233 233");
    //    The charset of an overriding type replaces the response's even
    //    when it names nothing ("get a final encoding": the label is taken
    //    first and looked up afterwards); jsdom then goes back to the
    //    response's.
    assert(S("var x = get('data:text/xml;charset=windows-1252,%3Ca%3E%E9%3C/a%3E', function (x) { x.overrideMimeType('text/xml;charset=bogus'); });"
             "String(x.responseXML) + ' ' + x.responseText.charCodeAt(3)") == "null 65533");
    //    UTF-16 named by the type, with no byte order mark.
    assert(S("var x = get('data:text/xml;charset=utf-16be,%00%3C%00a%00%3E%00%E9%00%3C%00/%00a%00%3E'); x.responseXML.documentElement.textContent.charCodeAt(0) + ' ' + x.responseText") == "233 <a>\xC3\xA9</a>");

    // 5. A synchronous request fires no progress event, and no total is
    //    known; jsdom fires one with a total.
    assert(S("var seen = [], x = new XMLHttpRequest();"
             "['readystatechange', 'loadstart', 'progress', 'load', 'loadend'].forEach(function (t) { x.addEventListener(t, function (e) { seen.push(t === 'readystatechange' ? 'state ' + x.readyState : t + ' ' + e.loaded + '/' + e.total + '/' + e.lengthComputable); }); });"
             "x.open('GET', 'data:text/xml,<a><b/></a>', false); x.send(); seen.join(', ')") ==
           "state 1, state 4, load 11/0/false, loadend 11/0/false");

    // 6. After abort() nothing more is fired; jsdom still delivers a
    //    progress event.
    js.eval("later('aborted', 'data:text/xml,<a/>', function (x) { x.addEventListener('readystatechange', function () { if (x.readyState === 3) x.abort(); }); });");

    // 7. An HTML response is parsed with scripting off, as DOMParser parses
    //    (a <noscript> in the head holds elements, and a <p> ends it); jsdom
    //    parses it the way it parsed the page.
    js.eval("later('html', 'data:text/html,<noscript><p>n</p></noscript><img src=x onerror=\"window.ranInResponse = 1\"><script>window.ranInResponse = 2</script>', asDocument,"
            "function (x) { return x.response.head.innerHTML + ' | ' + x.response.body.firstChild.outerHTML + ' | ' + typeof window.ranInResponse + ' | ' + x.response.contentType; });");
    //    An HTML response with nothing in it is still a document; jsdom says null.
    js.eval("later('empty html', 'data:text/html,', asDocument, function (x) { return x.response.documentElement.outerHTML; });");
    //    Scripts of an XML response do not run either.
    assert(S("var x = get('data:application/xhtml+xml,<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><script>window.ranInXml = 1</script></head></html>');"
             "x.responseXML.scripts.length + ' ' + typeof window.ranInXml + ' ' + (x.responseXML.defaultView === null)") == "1 undefined true");

    // 8. What is not a data: URL is not answered. In particular a request
    //    reads no file: not by a file: address, not by a path, and not when
    //    the type is overridden.
    js.eval("var secret = '" + secret_path + "';"
            "later('file', 'file://' + secret, null, function (x) { return x.status + ' [' + x.responseText + '] ' + x.responseXML + ' [' + x.responseURL + ']'; });"
            "later('path', secret, function (x) { x.overrideMimeType('text/xml'); }, function (x) { return x.status + ' [' + x.responseText + '] ' + x.responseXML; });"
            "later('relative', '../../../../../../..' + secret, null, function (x) { return x.status + ' [' + x.responseText + ']'; });"
            "later('http', 'http://127.0.0.1:9/feed.xml', null, function (x) { return x.status + ' [' + x.responseText + ']'; });"
            "later('https', 'https://example.test/feed.xml', null, function (x) { return x.status + ' [' + x.responseText + ']'; });"
            "later('blob', 'blob:https://example.test/1', null, function (x) { return String(x.status); });"
            "later('about', 'about:blank', null, function (x) { return String(x.status); });"
            "later('javascript', 'javascript:window.ranFromUrl = 1', null, function (x) { return x.status + ' ' + typeof window.ranFromUrl; });");
    assert(S("try { get('file://' + secret); 'read' } catch (e) { e.name + ' ' + e.code }") == "NetworkError 19");
    assert(S("try { get(secret); 'read' } catch (e) { e.name }") == "NetworkError");
    assert(S("try { get('http://127.0.0.1:9/'); 'read' } catch (e) { e.name }") == "NetworkError");

    // 9. There is no ArrayBuffer in the engine: that response is null.
    js.eval("later('arraybuffer', 'data:text/plain,abc', function (x) { x.responseType = 'arraybuffer'; }, function (x) { return String(x.response) + ' ' + x.status; });");

    // 10. The XML parser's limits hold here as they do for DOMParser: a
    //     document that expands without end gives no document, at once.
    assert(S("var levels = '<!ENTITY a0 \"aaaaaaaaaa\">';"
             "for (var i = 1; i < 12; i++) levels += '<!ENTITY a' + i + ' \"' + new Array(11).join('&a' + (i - 1) + ';') + '\">';"
             "var bomb = '<!DOCTYPE r [' + levels + ']><r>&a11;</r>', t0 = Date.now();"
             "var x = get('data:application/xml,' + encodeURIComponent(bomb));"
             "String(x.responseXML) + ' ' + (x.responseText === bomb) + ' ' + (Date.now() - t0 < 5000)") == "null true true");
    //     A large response is read in time that grows with its size.
#if defined(__SANITIZE_ADDRESS__)
    const char* big_xhr_budget = "15000";
#else
    const char* big_xhr_budget = "5000";
#endif
    assert(S(std::string("var big = '<r>' + new Array(200001).join('<i a=\"1\">x</i>') + '</r>', t0 = Date.now();"
                         "var x = get('data:text/xml,' + big);"
                         "x.responseXML.documentElement.childNodes.length + ' ' + x.responseText.length + ' ' + (Date.now() - t0 < ") +
             big_xhr_budget + ")") == "200000 2800007 true");

    settle();
    assert(S("results['text type']") == "state 1, loadstart, state 2, state 3, progress 51/0, state 4, load 51/0, loadend | 65533");
    assert(S("results.aborted + ' / ' + requests.aborted.seen.length + ' ' + requests.aborted.x.readyState") ==
           "state 1, loadstart, state 2, state 3, state 4, abort, loadend / 7 0");
    assert(S("results.html") == "state 1, loadstart, state 2, state 3, progress 116/0, state 4, load 116/0, loadend | "
                                "<noscript></noscript> | <p>n</p> | undefined | text/html");
    assert(S("results['empty html']") == "state 1, loadstart, state 2, progress 0/0, state 4, load 0/0, loadend | <html><head></head><body></body></html>");
    const std::string failed = "state 1, loadstart, state 4, error, loadend | ";
    assert(S("results.file") == failed + "0 [] null []");
    assert(S("results.path") == failed + "0 [] null");
    assert(S("results.relative") == failed + "0 []");
    assert(S("results.http") == failed + "0 []");
    assert(S("results.https") == failed + "0 []");
    assert(S("results.blob") == failed + "0");
    assert(S("results.about") == failed + "0");
    assert(S("results.javascript") == failed + "0 undefined");
    assert(S("results.arraybuffer") == "state 1, loadstart, state 2, state 3, progress 3/0, state 4, load 3/0, loadend | null 200");
    std::remove(secret_path.c_str());
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
    // Two things stand between markup like this and the stack now. The parser
    // stops nesting at HtmlTreeBuilder::kMaxTreeDepth, so the tree it builds
    // from 6,000 nested <div> is 512 elements deep; and layout asks at every
    // level whether there is stack left, because the cost of a level differs
    // ~12x between an -O2 and an ASan build.
    const int depth = 6000;
    std::string html;
    html.reserve(static_cast<size_t>(depth) * 11 + 8);
    for (int i = 0; i < depth; ++i) html += "<div>";
    html += "x";
    for (int i = 0; i < depth; ++i) html += "</div>";

    auto doc = HtmlParser::parse(html);
    assert(doc != nullptr);

    // Every <div> is in the tree; none of them was dropped.
    assert(doc->get_elements_by_tag_name("div").size() == static_cast<size_t>(depth));

    // They nest until the tree is 512 deep (<html>, <body> and 510 <div>).
    // From there each new <div> is attached next to the one it was written
    // inside. It is still an open element like any other: the text that
    // follows goes into the last one.
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
    const int max_open = static_cast<int>(HtmlTreeBuilder::kMaxTreeDepth);
    assert(parsed_depth == max_open - 2);
    // `current` is the 510th <div>; the 511th to the 6,000th are its siblings.
    assert(current->children().empty());
    auto widest = current->parent();
    assert(widest->children().size() == static_cast<size_t>(depth - (max_open - 2) + 1));
    // The text went into the last <div> that was opened, as in a browser.
    assert(widest->children().back()->text_content() == "x");
    assert(doc->body()->text_content() == "x");

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

    // The built-in prototypes (Object.prototype, Array.prototype, ...) are
    // heap objects shared by every interpreter and kept for the life of the
    // process, so an "empty" heap holds exactly those.
    JsIntrinsics::get();
    Heap::default_heap().collect_garbage();
    const size_t permanent_cells = Heap::default_heap().stats().alive_cells;
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
    // afterwards must not call into freed memory, and must reclaim everything
    // the script created.
    Heap::default_heap().collect_garbage();
    assert(Heap::default_heap().stats().alive_cells == permanent_cells);
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

TEST(gc_keeps_state_reachable_only_from_listeners_and_frames) {
    using namespace litebird;

    // Two shapes of "the only reference is somewhere the collector used to
    // not look". Both were heap-use-after-free under AddressSanitizer while
    // roots were registered by hand at each boundary; the collector now works
    // out its own roots from handle counts, so neither needs registering.

    // 1. A DOM listener. The closure, and the `state` object it captures, are
    //    referenced from nothing but the listener table of a DOM node -- C++
    //    storage the heap cannot walk. Collect, churn the heap so freed cells
    //    would be reused, then click.
    {
        auto doc = std::make_shared<Document>();
        auto button = std::make_shared<Element>("button");
        button->set_attribute("id", "b");
        auto out = std::make_shared<Element>("div");
        out->set_attribute("id", "out");
        out->append_child(std::make_shared<Text>("initial"));
        doc->append_child(button);
        doc->append_child(out);

        JsInterpreter js(doc);
        js.eval_ast(R"(
            var handler;
            (function () {
                var state = { count: 0, tag: { name: "live" } };
                var listener = function () {
                    state.count++;
                    document.getElementById("out").textContent = state.tag.name + ":" + state.count;
                };
                document.getElementById("b").addEventListener("click", listener);
                handler = { remove: function () { document.getElementById("b").removeEventListener("click", listener); } };
            })();
            var junk = [];
            gc();
            for (var i = 0; i < 3000; i++) junk.push({ k: i, s: "x" + i });
            document.getElementById("b").click();
        )");
        assert(out->text_content() == "live:1");

        // The same again with the collection and the click coming from the
        // host side, the way the shell's heap view and a real mouse click do.
        Heap::default_heap().collect_garbage();
        js.eval_ast("junk = []; for (var j = 0; j < 3000; j++) junk.push({ k: j });");
        button->click();
        assert(out->text_content() == "live:2");

        // removeEventListener must find the listener after collections too:
        // identity is the function object, and that object must not have moved
        // or been replaced.
        js.eval_ast("handler.remove(); gc();");
        button->click();
        assert(out->text_content() == "live:2");
    }

    // 2. A binding that lives only in a `for (let ...)` head while the body
    //    collects. The per-iteration copy of the scope is held by the
    //    evaluator's C++ frame and by nothing else.
    {
        JsInterpreter js;
        js.eval_ast(R"(
            var seen = "";
            for (let o = { a: { b: 1 } }, i = 0; i < 3; i++) {
                gc();
                var filler = [];
                for (var k = 0; k < 500; k++) filler.push({ k: k });
                seen += o.a.b;
            }
        )");
        assert(js.global_env()->get("seen").to_string() == "111");
    }
}

TEST(deep_nesting_never_exhausts_the_native_stack) {
    using namespace litebird;

    // The engine recurses over what a script gives it: the parser over nested
    // syntax, the evaluator over nested statements, and C++ destructors over
    // chains of scopes, promises and syntax tree nodes. Each of those used to
    // overflow the native stack on a deep enough input, which killed the
    // process: a page could take the browser down with one long line.
    //
    // What must hold: too-deep input is a RangeError the script can catch, a
    // later script still runs, and long chains are released without recursion.
    // Depths are not asserted, for the reason given in the test above.

    // Syntax nested far beyond any stack: parentheses, arrays, blocks, unary
    // operators, functions. Every one ends in a caught RangeError.
    {
        JsInterpreter js;
        js.eval_ast(
            "var kinds = [['(', '1', ')'], ['[', '1', ']'], ['{', ';', '}'], ['!', '1', ''],"
            "             ['(function(){return ', '1', '})'], ['`${', '1', '}`'], ['1?', '1', ':0']];"
            "var caught = 0;"
            "for (var i = 0; i < kinds.length; i++) {"
            "  var src = kinds[i][0].repeat(300000) + kinds[i][1] + kinds[i][2].repeat(300000);"
            "  try { eval(src); } catch (e) { if (e instanceof RangeError) caught++; }"
            "}"
            "var after = [1, 2, 3].map(function (x) { return x * 2; }).join();");
        assert(js.global_env()->get("caught").to_string() == "7");
        assert(js.global_env()->get("after").to_string() == "2,4,6");
    }

    // A script that is itself too deep is reported, and the interpreter is
    // still usable for the next script on the page.
    {
        JsInterpreter js;
        js.eval_ast(std::string(300000, '(') + "1" + std::string(300000, ')'));
        js.eval_ast("var next = 6 * 7;");
        assert(js.global_env()->get("next").to_string() == "42");
    }

    // Long chains of operators are not nesting. They are evaluated in a loop
    // and must give the right answer, not an error.
    {
        JsInterpreter js;
        js.eval_ast(
            "var sum = eval(new Array(200000).fill('1').join('+'));"
            "var text = eval(new Array(50000).fill('\"ab\"').join('+')).length;"
            "var last = eval(new Array(100000).fill('0').join('||') + '||7');");
        assert(js.global_env()->get("sum").to_string() == "200000");
        assert(js.global_env()->get("text").to_string() == "100000");
        assert(js.global_env()->get("last").to_string() == "7");
    }

    // Chains that are released link by link: a linked list, nested closures
    // (each holding its parent scope), a settled promise chain and a pending
    // one. Dropping the head must not recurse once per link.
    {
        JsInterpreter js;
        js.eval_ast(
            "var head = null; for (var i = 0; i < 500000; i++) head = { next: head };"
            "function wrap(inner) { return function () { return inner; }; }"
            "var f = function () { return 0; }; for (var i = 0; i < 200000; i++) f = wrap(f);"
            "var settled = Promise.resolve(0);"
            "for (var i = 0; i < 100000; i++) settled = settled.then(function (v) { return v + 1; });"
            "var total = -1; settled.then(function (v) { total = v; });"
            "var pending = new Promise(function () {});"
            "for (var i = 0; i < 100000; i++) pending = pending.then(function (v) { return v; });");
        EventLoop::default_loop().run_microtasks();
        assert(js.global_env()->get("total").to_string() == "100000");
        js.eval_ast("head = null; f = null; settled = null; pending = null; var done = 'released';");
        Heap::default_heap().collect_garbage();
        assert(js.global_env()->get("done").to_string() == "released");
    }
    // ...and the same when the whole interpreter goes away while holding them.
    {
        JsInterpreter js;
        js.eval_ast(
            "var head = null; for (var i = 0; i < 300000; i++) head = { next: head };"
            "function wrap(inner) { return function () { return inner; }; }"
            "var f = function () { return 0; }; for (var i = 0; i < 100000; i++) f = wrap(f);"
            "var pending = new Promise(function () {});"
            "for (var i = 0; i < 50000; i++) pending = pending.then(function (v) { return v; });");
    }

    // Deep data handed to recursive library code: an error or an answer.
    {
        JsInterpreter js;
        js.eval_ast(
            "var a = ['x']; for (var i = 0; i < 200000; i++) a = [a];"
            "var results = [];"
            "function attempt(fn) { try { fn(); results.push('ok'); } catch (e) { results.push(e instanceof RangeError ? 'range' : 'other'); } }"
            "attempt(function () { String(a); });"
            "attempt(function () { a.flat(Infinity); });"
            "attempt(function () { JSON.stringify(a); });"
            "attempt(function () { JSON.parse('['.repeat(200000) + ']'.repeat(200000)); });"
            "attempt(function () { structuredClone(a); });"
            "var bad = results.indexOf('other'); var count = results.length; a = null;");
        assert(js.global_env()->get("bad").to_string() == "-1");
        assert(js.global_env()->get("count").to_string() == "5");
    }

    // A prototype chain longer than the old lookup limit of 10,000 still finds
    // what is at its far end, and a circular chain cannot be built.
    {
        JsInterpreter js;
        js.eval_ast(
            "var o = { far: 'found' }; for (var i = 0; i < 50000; i++) o = Object.create(o);"
            "var far = o.far; var has = 'far' in o; var isObj = o instanceof Object;"
            "var a = {}, b = Object.create(a), cyc = 'none';"
            "try { Object.setPrototypeOf(a, b); } catch (e) { cyc = e.name; }"
            "var cyc2 = 'none'; try { a.__proto__ = b; } catch (e) { cyc2 = e.name; }");
        assert(js.global_env()->get("far").to_string() == "found");
        assert(js.global_env()->get("has").to_string() == "true");
        assert(js.global_env()->get("isObj").to_string() == "true");
        assert(js.global_env()->get("cyc").to_string() == "TypeError");
        assert(js.global_env()->get("cyc2").to_string() == "TypeError");
    }
}

TEST(dom_insertion_cannot_make_a_node_its_own_ancestor) {
    using namespace litebird;

    // `d.appendChild(d)` used to succeed: the node became its own child and
    // its own parent. Every walk over the tree assumes there are no loops, so
    // the next textContent, serialisation, layout pass or destructor recursed
    // until the stack was gone. Three lines of script ended the process.

    // The DOM itself refuses, whoever calls it.
    {
        auto a = std::make_shared<Element>("div");
        auto b = std::make_shared<Element>("span");
        auto c = std::make_shared<Element>("b");
        a->append_child(b);
        b->append_child(c);

        a->append_child(a);                     // into itself
        assert(a->children().size() == 1 && a->parent() == nullptr);
        c->append_child(a);                     // into its grandchild
        assert(c->children().empty() && a->parent() == nullptr);
        b->insert_before(a, c);                 // the same through insertBefore
        assert(b->children().size() == 1 && a->parent() == nullptr);
        b->insert_before(b, c);
        assert(b->children().size() == 1 && b->parent() == a);

        // Moves that create no loop still work: a leaf, and a subtree moved
        // to a sibling.
        auto d = std::make_shared<Element>("i");
        a->append_child(d);
        d->append_child(b);                     // b (with c inside) moves under d
        assert(a->children().size() == 1 && d->children().size() == 1 && b->parent() == d);
        assert(a->text_content().empty());      // and the tree can be walked
    }

    // A host contains its shadow tree.
    {
        auto host = std::make_shared<Element>("div");
        auto root = host->attach_shadow("open");
        auto inner = std::make_shared<Element>("span");
        root->append_child(inner);
        inner->append_child(host);
        assert(inner->children().empty());
        root->append_child(host);
        assert(root->children().size() == 1);
    }

    // Script is told, with the error a browser gives, and keeps running.
    {
        auto doc = std::make_shared<Document>();
        auto host = std::make_shared<Element>("div");
        host->set_attribute("id", "host");
        doc->append_child(host);

        JsInterpreter js(doc);
        js.eval_ast(R"(
            var names = [];
            function attempt(fn) { try { fn(); names.push('ok'); } catch (e) { names.push(e.name + ':' + e.code); } }
            var host = document.getElementById('host');
            var a = document.createElement('div'), b = document.createElement('span');
            attempt(function () { a.appendChild(a); });
            attempt(function () { a.appendChild(b); });
            attempt(function () { b.appendChild(a); });
            attempt(function () { b.insertBefore(a, null); });
            attempt(function () { host.appendChild(a); });
            attempt(function () { b.appendChild(host); });
            attempt(function () { a.insertBefore(document.createElement('i'), b); });
            var result = names.join(' ');
            var shape = host.children.length + '/' + a.children.length + '/' + b.children.length;
            var text = typeof host.textContent + ' ' + host.innerHTML;
        )");
        assert(js.global_env()->get("result").to_string() ==
               "HierarchyRequestError:3 ok HierarchyRequestError:3 HierarchyRequestError:3 ok "
               "HierarchyRequestError:3 ok");
        assert(js.global_env()->get("shape").to_string() == "1/2/0");
        assert(js.global_env()->get("text").to_string() == "string <div><i></i><span></span></div>");
    }
}

TEST(js_strings_are_shared_and_read_in_constant_time) {
    using namespace litebird;

    // AUDIT.md, finding 25. A string value used to own a private std::string:
    // reading a variable copied the text, `s.length` re-counted it and `s[i]`
    // re-scanned it, so a loop over n characters did n*n work, and so did
    // building a string with `s += x`. These checks are about cost. They count
    // work (JsString::stats) instead of timing it, because a count does not
    // depend on how fast or how busy the machine is.

    // --- The value: copying a string value shares its text ---------------------------
    {
        JsValue a = JsValue::make_string(std::string(5000, 'x'));
        const auto before = JsString::stats();
        JsValue b = a;
        std::vector<JsValue> many(100, a);
        JsValue c = many[50];
        const auto after = JsString::stats();
        assert(after.bytes_copied == before.bytes_copied && after.blocks == before.blocks);
        assert(b.string_val.shares_text_with(a.string_val) && c.string_val.shares_text_with(a.string_val));
        assert(a.string_val.use_count() == 103);
        assert(strict_equals(a, c));
        // The handle is a pointer and five 32-bit fields: where the text is,
        // how long it is in bytes and in code units, and the high half it may
        // owe at its end (finding 47). It was four fields, 24 bytes, before the
        // owed half; the fifth costs one word of padding.
        assert(sizeof(JsString) == 32);
    }

    // --- Length and indexing ---------------------------------------------------------
    {
        JsString ascii("hello");
        assert(ascii.length() == 5 && ascii.size() == 5 && ascii.is_ascii() && ascii.unit_at(1) == 'e');
        JsString mixed("h\xC3\xA9llo \xF0\x9F\x98\x80!");       // "héllo 😀!"
        assert(mixed.length() == 9 && mixed.size() == 12 && !mixed.is_ascii());
        assert(mixed.unit_at(1) == 0xE9 && mixed.unit_at(6) == 0xD83D && mixed.unit_at(7) == 0xDE00 && mixed.unit_at(8) == '!');

        // Text that is not ASCII is converted to UTF-16 once, at the first
        // index, and the conversion is kept with the text: a copy of the
        // handle finds it there.
        std::string body;
        for (int i = 0; i < 10000; ++i) body += "ab\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80";       // "abé中😀": 6 code units, 11 bytes
        JsString text(body);
        assert(text.length() == 60000 && text.size() == 110000);
        const auto before = JsString::stats();
        uint64_t sum = 0;
        for (size_t i = 0; i < text.length(); ++i) sum += text.unit_at(i);
        const auto once = JsString::stats();
        assert(sum == 10000ull * ('a' + 'b' + 0xE9 + 0x4E2D + 0xD83D + 0xDE00));
        assert(once.units_widened - before.units_widened == 60000);
        JsString copy = text;
        for (size_t i = 0; i < copy.length(); ++i) sum += copy.unit_at(i);
        const auto twice = JsString::stats();
        assert(twice.units_widened == once.units_widened && twice.bytes_copied == before.bytes_copied);

        JsString empty;
        assert(empty.length() == 0 && empty.view().empty() && empty.use_count() == 0 && empty == JsString(""));
        // The one-character ASCII strings exist once.
        assert(JsString::from_unit('a').shares_text_with(JsString("a")));
        assert(JsString("abc").substring(1, 2).shares_text_with(JsString::from_unit('b')));
    }

    // --- Well-formedness -------------------------------------------------------------
    {
        // Bytes that are not UTF-8 become U+FFFD, as when a browser decodes a page.
        JsString bad(std::string("a\xFF" "b\xC3"));
        assert(bad.view() == "a\xEF\xBF\xBD" "b\xEF\xBF\xBD" && bad.length() == 4);
        JsString overlong(std::string("\xC0\x80"));
        assert(overlong.view() == "\xEF\xBF\xBD\xEF\xBF\xBD" && overlong.length() == 2);
        // Half a surrogate pair can stand alone, since script can make one.
        JsString high = JsString::from_unit(0xD83D);
        JsString low = JsString::from_unit(0xDE00);
        assert(high.size() == 3 && high.length() == 1 && high.unit_at(0) == 0xD83D);
        // Two halves that meet are stored as the one character they spell, so
        // that equal strings have equal bytes.
        JsString joined = JsString::concat(high, low);
        assert(joined.view() == "\xF0\x9F\x98\x80" && joined.length() == 2 && joined == JsString("\xF0\x9F\x98\x80"));
        assert(JsString(std::string("\xED\xA0\xBD\xED\xB8\x80")).view() == "\xF0\x9F\x98\x80");
        // The other way round they are still two halves.
        JsString reversed = JsString::concat(low, high);
        assert(reversed.size() == 6 && reversed.length() == 2 && reversed.unit_at(0) == 0xDE00);

        // A lone half has no UTF-8 encoding. Inside the engine it is kept;
        // text that leaves (the console, an error report) carries U+FFFD.
        assert(!high.is_well_formed() && !reversed.is_well_formed() && joined.is_well_formed());
        assert(JsString("caf\xC3\xA9 \xED\x9F\xBF \xEE\x80\x80").is_well_formed());      // U+D7FF and U+E000 are not halves
        assert(reversed.to_well_formed().view() == "\xEF\xBF\xBD\xEF\xBF\xBD" && reversed.to_well_formed().length() == 2);
        assert(joined.to_well_formed().shares_text_with(joined));
        assert(js_output_text("a" + high.str() + "b" + low.str()) == "a\xEF\xBF\xBD" "b\xEF\xBF\xBD");
        // Two values printed side by side can bring two halves together.
        assert(js_output_text(high.str() + low.str()) == "\xF0\x9F\x98\x80");
        assert(js_output_text(low.str() + high.str() + low.str()) == "\xEF\xBF\xBD\xF0\x9F\x98\x80");
        assert(js_output_text("plain \xC3\xA9 \xED\x9F\xBF \xF0\x9F\x98\x80") == "plain \xC3\xA9 \xED\x9F\xBF \xF0\x9F\x98\x80");
    }

    // --- The representation: appending and taking pieces ----------------------------
    {
        // A block has room after short text. The first append to "abc" is
        // written there; "abc" itself still reads "abc".
        JsString abc("abc");
        JsString abcd = JsString::concat(abc, JsString("d"));
        assert(abcd == std::string_view("abcd") && abc == std::string_view("abc"));
        assert(abcd.shares_text_with(abc));
        // A second append to the same "abc" finds the room taken and copies.
        JsString abce = JsString::concat(abc, JsString("e"));
        assert(abce == std::string_view("abce") && abcd == std::string_view("abcd") && abc == std::string_view("abc"));
        assert(!abce.shares_text_with(abc));
        // The one-character strings are shared by everything: never written into.
        JsString q = JsString::from_unit('q');
        assert(!JsString::concat(q, JsString("r")).shares_text_with(q));
        assert(JsString::from_unit('q') == std::string_view("q"));

        // A piece that is a good part of its text shares the block; a small
        // piece gets its own, so a few characters cannot pin a long text.
        JsString text(std::string(4000, 'x') + "tail");
        JsString most = text.substring(100, 4004);
        JsString few = text.substring(100, 120);
        assert(most.shares_text_with(text) && most.length() == 3904 && most.unit_at(3903) == 'l');
        assert(!few.shares_text_with(text) && few == std::string_view(std::string(20, 'x')));
        assert(most.substring(3900, 3904) == std::string_view("tail"));

        // The same for text that is not ASCII, where a code unit is not a byte.
        std::string accents;
        for (int i = 0; i < 1000; ++i) accents += "caf\xC3\xA9 \xF0\x9F\x98\x80 ";       // "café 😀 ", 8 code units
        JsString wide_text(accents);
        assert(wide_text.length() == 8000 && wide_text.size() == 11000);
        JsString rest = wide_text.substring(803, 8000);         // starts at the "é" of the 101st "café"
        assert(rest.shares_text_with(wide_text) && rest.length() == 7197);
        assert(rest.unit_at(0) == 0xE9 && rest.unit_at(2) == 0xD83D && rest.unit_at(3) == 0xDE00);
        assert(rest.substring(0, 1) == std::string_view("\xC3\xA9"));
        // A piece that starts between the two halves of a pair holds a lone
        // half, which is not in the block as such: it is built, not shared.
        JsString from_low_half = wide_text.substring(6, 8000);
        assert(!from_low_half.shares_text_with(wide_text));
        assert(from_low_half.length() == 7994 && from_low_half.unit_at(0) == 0xDE00 && from_low_half.unit_at(1) == ' ');
        // Appending to a piece that ends where its block ends still works in place.
        JsString grown = JsString::concat(JsString::concat(JsString("0123456789abcdef"), JsString("0123456789abcdef")),
                                          JsString("0123456789abcdef"));
        JsString back = grown.substring(20, 48);
        JsString back_more = JsString::concat(back, JsString("!"));
        assert(back_more == std::string_view("456789abcdef0123456789abcdef!") && back == std::string_view("456789abcdef0123456789abcdef"));
        assert(grown == std::string_view("0123456789abcdef0123456789abcdef0123456789abcdef"));

        // Order is by UTF-16 code unit. An emoji is a surrogate pair there
        // (0xD83D 0xDE00), so it sorts before U+FFFF although its bytes are larger.
        auto order = [](const char* a, const char* b) { return JsString::compare(JsString(a).view(), JsString(b).view()); };
        assert(order("a", "b") < 0 && order("b", "a") > 0 && order("ab", "ab") == 0 && order("ab", "abc") < 0);
        assert(order("\xF0\x9F\x98\x80", "\xEF\xBF\xBF") < 0);                 // U+1F600 < U+FFFF
        assert(order("\xEF\xBF\xBF", "\xF0\x9F\x98\x80") > 0);
        assert(order("\xF0\x9F\x98\x80", "\xED\x9F\xBF") > 0);                 // U+1F600 > U+D7FF
        assert(order("\xF0\x9F\x98\x80", "\xED\xB8\x80") < 0);                 // D83D.. < lone DE00
        assert(order("\xF0\x9F\x98\x80", "\xED\xA0\xBD") > 0);                 // D83D DE00 > lone D83D
        assert(order("\xF0\x9F\x98\x80", "\xED\xA0\xBD" "a") > 0);             // D83D DE00 > D83D 0061
        assert(order("\xF0\x9F\x98\x80", "\xED\xA0\xBD\xEF\xBF\xBF") < 0);     // D83D DE00 < D83D FFFF
        assert(order("\xF0\x9F\x98\x80", "\xF0\x9F\x98\x81") < 0);
    }

    // --- Script: a loop over a long ASCII string ---------------------------------------
    {
        JsInterpreter js;
        js.eval_ast("var s = 'abcdefghij'.repeat(6000);");          // 60,000 characters
        const auto before = JsString::stats();
        js.eval_ast(
            "var total = 0, hits = 0;"
            "for (var i = 0; i < s.length; i++) {"
            "  total += s.charCodeAt(i);"
            "  if (s[i] === 'c') hits++;"
            "  if (s.charAt(i) === 'd') hits++;"
            "  if (s.at(i) === 'e') hits++;"
            "  total += s.codePointAt(i);"
            "}");
        const auto after = JsString::stats();
        assert(!js.last_eval_failed());
        assert(js.global_env()->get("total").to_number() == 12180000);
        assert(js.global_env()->get("hits").to_number() == 18000);
        // 360,000 reads of a 60,000-byte string. Copying or scanning the text
        // on each would be 21,600,000,000 bytes; nothing at all is copied,
        // because the characters handed back are the shared one-character strings.
        assert(after.bytes_copied - before.bytes_copied < 4096);
        assert(after.units_widened == before.units_widened);
    }

    // --- Script: the same over text that is not ASCII -----------------------------------
    {
        JsInterpreter js;
        js.eval_ast("var s = 'ab\\u00e9\\u4e2d\\ud83d\\ude00'.repeat(10000);");     // 60,000 code units, 110,000 bytes
        const auto before = JsString::stats();
        js.eval_ast("var n = 0; for (var i = 0; i < s.length; i++) n++;");
        const auto counted = JsString::stats();
        assert(js.global_env()->get("n").to_number() == 60000);
        // `.length` is a field of the handle: the loop bound reads no text.
        assert(counted.bytes_copied - before.bytes_copied < 4096 && counted.units_widened == before.units_widened);

        js.eval_ast(
            "var total = 0, marks = 0;"
            "for (var i = 0; i < s.length; i++) {"
            "  total += s.charCodeAt(i);"
            "  if (s[i] === '\\u4e2d') marks++;"
            "  if (s.codePointAt(i) > 0xFFFF) marks++;"
            "}");
        const auto after = JsString::stats();
        assert(!js.last_eval_failed());
        assert(js.global_env()->get("total").to_number() == 1326300000);
        assert(js.global_env()->get("marks").to_number() == 20000);
        // The UTF-16 form is built once, at the first index: 60,000 units,
        // not 60,000 units for each of the 180,000 reads.
        assert(after.units_widened - counted.units_widened == 60000);
        // `s[i]` makes a string of one code unit: a few bytes each time.
        assert(after.bytes_copied - counted.bytes_copied < 400000);
    }

    // --- Script: a long string handed around ---------------------------------------------
    {
        JsInterpreter js;
        js.eval_ast("var s = 'abcdefghij'.repeat(6000);");
        const auto before = JsString::stats();
        js.eval_ast(
            "function f(x, i) { return x.charCodeAt(i); }"
            "var o = { text: s }, list = [s, s, s], t = 0;"
            "for (var i = 0; i < 20000; i++) {"
            "  t += f(s, i) + o.text.length + list[i % 3].charCodeAt(i);"
            "  var copy = s; t += copy.length;"
            "}");
        const auto after = JsString::stats();
        assert(!js.last_eval_failed());
        assert(js.global_env()->get("t").to_number() == 2404060000.0);
        // Argument, property, array element, variable: each is the same text.
        assert(after.bytes_copied - before.bytes_copied < 4096);
    }

    // --- Script: building a string by appending ----------------------------------------
    {
        JsInterpreter js;
        const auto before = JsString::stats();
        js.eval_ast(
            "var s = '';"
            "for (var i = 0; i < 20000; i++) s += 'row ' + i + '\\n';"
            "var o = { text: '' }, list = [''];"
            "for (var i = 0; i < 20000; i++) { o.text += 'ab'; list[0] = list[0] + 'cd'; }"
            "var built = s.length + ':' + o.text.length + ':' + list[0].length + ':' + s.slice(-10);");
        const auto after = JsString::stats();
        assert(!js.last_eval_failed());
        assert(js.global_env()->get("built").to_string() == "188890:40000:40000:row 19999\n");
        // About 270,000 bytes are built. Copying the text so far on every one
        // of the 60,000 appends would be about 2,500,000,000 bytes.
        assert(after.bytes_copied - before.bytes_copied < 3000000);
    }

    // --- Script: every prefix kept, and read, while the string grows ----------------------
    {
        JsInterpreter js;
        const auto before = JsString::stats();
        js.eval_ast(
            "var s = '', keep = [], last = 0;"
            "for (var i = 0; i < 6000; i++) { s += '\\u00e9' + (i % 10); keep.push(s); last += s.charCodeAt(s.length - 2); }"
            "var ok = true;"
            "for (var i = 0; i < 6000; i += 13) {"
            "  var p = keep[i];"
            "  if (p.length !== 2 * (i + 1) || p.charCodeAt(2 * i) !== 0xE9 || p[2 * i + 1] !== String(i % 10)) ok = false;"
            "}"
            "var first = keep[0], middle = keep[2999].length;");
        const auto after = JsString::stats();
        assert(!js.last_eval_failed());
        assert(js.global_env()->get("ok").bool_val);
        assert(js.global_env()->get("last").to_number() == 6000.0 * 0xE9);
        assert(js.global_env()->get("first").to_string() == "\xC3\xA9" "0");
        assert(js.global_env()->get("middle").to_number() == 6000);
        // The final text is 12,000 code units. The UTF-16 form grows with the
        // string; rebuilding it at each of the 6,000 reads would be 36,000,000.
        assert(after.units_widened - before.units_widened < 100000);
        assert(after.bytes_copied - before.bytes_copied < 600000);
        // All 6,000 prefixes are ranges of a handful of blocks, not 6,000 copies.
        assert(after.blocks - before.blocks < 20000);
    }

    // --- Script: a tokenizer that eats its input from the front ---------------------------
    {
        JsInterpreter js;
        js.eval_ast("var ascii = 'token,'.repeat(10000), accented = 't\\u00f6k\\u00ebn,'.repeat(10000);");
        const auto before = JsString::stats();
        js.eval_ast(
            "function eat(s) { var n = 0; while (s.length) { s = s.slice(s.indexOf(',') + 1); n++; } return n; }"
            "function eat_regex(s) { var n = 0, m; while (s.length) { m = /^[^,]*,/.exec(s); s = s.substring(m[0].length); n++; } return n; }"
            "var eaten = [eat(ascii), eat(accented), eat_regex(ascii), eat_regex(accented)].join();");
        const auto after = JsString::stats();
        assert(!js.last_eval_failed());
        assert(js.global_env()->get("eaten").to_string() == "10000,10000,10000,10000");
        // 40,000 slices of texts of 60,000 to 80,000 bytes. Copying what is left
        // each time would be about 1,400,000,000 bytes.
        assert(after.bytes_copied - before.bytes_copied < 3000000);
        assert(after.units_widened - before.units_widened < 600000);
    }

    // --- Script: a regular expression applied again and again to one long string ----------
    {
        JsInterpreter js;
        js.eval_ast("var s = 'cl\\u00e9=val\\u00fcr;'.repeat(5000);");      // 50,000 code units
        const auto before = JsString::stats();
        js.eval_ast(
            "var re = /([^=;]+)=([^=;]+);/g, m, found = 0, shared = true;"
            "while ((m = re.exec(s))) { found += m[1].length + m[2].length; if (m.input !== s) shared = false; }");
        const auto after = JsString::stats();
        assert(!js.last_eval_failed());
        assert(js.global_env()->get("found").to_number() == 5000 * 8);
        assert(js.global_env()->get("shared").bool_val);
        // 5,000 calls to exec(). The subject is converted to UTF-16 once, and
        // each match array's `input` is the subject itself.
        assert(after.units_widened - before.units_widened < 70000);
        assert(after.bytes_copied - before.bytes_copied < 500000);
    }

    // --- Script: halves of a surrogate pair -----------------------------------------
    {
        // Every line is true in Node. Ten of them were false while a string was
        // UTF-8 bytes with no rule for two halves that meet.
        JsInterpreter js;
        js.eval_ast(
            "var hi = '\\uD83D', lo = '\\uDE00', face = '\\uD83D\\uDE00';"
            "var checks = ["
            "  (hi + lo) === face, (hi + lo).length === 2, face.charCodeAt(0) === 0xD83D, face[1] === lo,"
            "  face.slice(0, 1) === hi, face.slice(1) === lo, (face.slice(0, 1) + face.slice(1)) === face,"
            "  [...face].length === 1, [...(lo + hi)].length === 2, face.codePointAt(0) === 0x1F600,"
            "  String.fromCharCode(0xD83D, 0xDE00) === face, face.split('').join('') === face,"
            "  hi < face, face < '\\uFFFF', '\\uD7FF' < face, ['\\uFFFF', face, 'a'].sort().join() === 'a,' + face + ',\\uFFFF',"
            "  (lo + hi).length === 2, (lo + hi) !== face, (hi + 'x' + lo).length === 3, face.at(-1) === lo,"
            "  ('a' + hi).concat(lo, 'b') === 'a' + face + 'b', (hi.repeat(2) + lo).codePointAt(1) === 0x1F600,"
            "  'x'.padEnd(3, hi + lo) === 'x' + face, 'x'.padEnd(2, face) === 'x' + hi, [hi, lo].join('') === face,"
            "  `${hi}${lo}` === face, face.localeCompare(hi + lo) === 0"
            "];"
            "var all_true = checks.length === 27 && checks.every(function (c) { return c === true; });"
            "var failed = checks.map(function (c, i) { return c ? '' : i; }).join('');");
        assert(!js.last_eval_failed());
        assert(js.global_env()->get("failed").to_string().empty());
        assert(js.global_env()->get("all_true").bool_val);
    }

    // --- Script: a string cannot grow without limit --------------------------------------
    {
        JsInterpreter js;
        js.eval_ast(
            "var message = 'no error';"
            "try { 'abc'.repeat(Math.pow(2, 29)); } catch (e) { message = (e instanceof RangeError) + ': ' + e.message; }");
        assert(js.global_env()->get("message").to_string() == "true: Invalid string length");
    }
}

TEST(js_string_owes_a_trailing_high_half_until_it_is_read) {
    using namespace litebird;

    // AUDIT.md, finding 47. `a + b` where a ends in a high half and b starts
    // with the low one is a single four-byte character that starts three bytes
    // before the end of a. Written bytes never change, so that join used to
    // copy the whole of a. A join now owes the half instead of writing it, and
    // the character is written where the block's text ends when the low half
    // arrives. These checks pin what script can see of that: nothing.
    const JsString high = JsString::from_unit(0xD83D);
    const JsString low = JsString::from_unit(0xDE00);
    const std::string_view high_bytes("\xED\xA0\xBD", 3);
    const std::string_view pair_bytes("\xF0\x9F\x98\x80", 4);

    // --- Owed: nothing is written, and the text reads as it always did ---------------
    {
        const JsString abc("abc");
        const auto before = JsString::stats();
        const JsString owed = JsString::concat(abc, high);
        const auto after = JsString::stats();
        // No block, no byte: the half is a field of the handle.
        assert(after.blocks == before.blocks && after.bytes_copied == before.bytes_copied);
        assert(owed.shares_text_with(abc));
        assert(owed.length() == 4 && owed.size() == 6 && !owed.is_ascii() && !owed.is_well_formed());
        assert(owed.unit_at(0) == 'a' && owed.unit_at(3) == 0xD83D);
        // Reading it settles it: three bytes, once. Reading it again copies nothing.
        const auto read_before = JsString::stats();
        assert(owed.view() == std::string("abc") + std::string(high_bytes));
        const auto read_once = JsString::stats();
        assert(read_once.bytes_copied - read_before.bytes_copied == 3);
        assert(owed.view() == std::string("abc") + std::string(high_bytes));
        assert(JsString::stats().bytes_copied == read_once.bytes_copied);
        assert(owed.str() == owed.view());
        assert(JsString::stats().bytes_copied - read_once.bytes_copied == 6);      // str() hands out its own copy
        assert(owed == JsString("abc\xED\xA0\xBD"));            // equal to the same text, written out
        assert(abc == std::string_view("abc"));                 // and what it was joined to is untouched
        assert(owed.substring(0, 2) == std::string_view("ab"));
        assert(owed.substring(3, 4).length() == 1 && owed.substring(3, 4).unit_at(0) == 0xD83D);
        assert(owed.to_well_formed() == JsString("abc\xEF\xBF\xBD"));
    }

    // --- The low half arrives: one character, written after the text, nothing copied --
    {
        const JsString abc("abc");
        const JsString owed = JsString::concat(abc, high);
        const auto before = JsString::stats();
        const JsString joined = JsString::concat(owed, low);
        const auto after = JsString::stats();
        assert(joined.view() == std::string("abc") + std::string(pair_bytes));
        assert(joined.length() == 5 && joined.size() == 7 && joined.is_well_formed());
        assert(joined == JsString("abc\xF0\x9F\x98\x80"));
        assert(joined.shares_text_with(abc));                    // the same block: nothing was copied
        assert(after.blocks == before.blocks);
        assert(after.bytes_copied - before.bytes_copied == 4);   // the character, and nothing else
        assert(abc == std::string_view("abc"));
    }

    // --- A handle kept before the pair was completed still ends in a half ------------
    {
        const JsString abc("abc");
        const JsString owed = JsString::concat(abc, high);
        const JsString kept = owed;
        const JsString joined = JsString::concat(owed, low);
        assert(kept.length() == 4 && kept.view() == std::string("abc") + std::string(high_bytes));
        assert(kept.unit_at(3) == 0xD83D && !kept.is_well_formed());
        assert(joined.length() == 5 && joined.view() == std::string("abc") + std::string(pair_bytes));
        // Reading the kept half first writes it into the block; completing the
        // pair then has to copy, because those bytes are now spoken for.
        JsString other("xyz");
        JsString read_first = JsString::concat(other, high);
        assert(read_first.view() == std::string("xyz") + std::string(high_bytes));
        const JsString completed = JsString::concat(read_first, low);
        assert(completed.view() == std::string("xyz") + std::string(pair_bytes));
        assert(read_first.view() == std::string("xyz") + std::string(high_bytes));
    }

    // --- Settling when the room after the text has been taken ------------------------
    {
        JsString base("room enough for a few appends");
        const JsString owed = JsString::concat(base, high);
        const JsString sibling = JsString::concat(base, JsString("!"));      // takes the room
        assert(sibling.view().back() == '!');
        const auto before = JsString::stats();
        const std::string_view text = owed.view();                            // has to copy, once
        assert(text == std::string("room enough for a few appends") + std::string(high_bytes));
        assert(JsString::stats().bytes_copied - before.bytes_copied == owed.size());
        const auto again = JsString::stats();
        assert(owed.view() == text && JsString::stats().blocks == again.blocks);   // the copy is kept
        // The copy outlives the handles that took the room, and `base` itself.
        const std::string expected(text);
        {
            JsString only_owed = owed;
            base = JsString();
            assert(only_owed.view() == expected);
        }
        assert(owed.view() == expected && owed.length() == 30);
    }

    // --- Every way round: two halves, a half and text, raw text on either side --------
    {
        const JsString abc("abc");
        const JsString two_highs = JsString::concat(JsString::concat(abc, high), high);
        assert(two_highs.length() == 5 && two_highs.view() == std::string("abc") + std::string(high_bytes) + std::string(high_bytes));
        assert(JsString::concat(two_highs, low).view() == std::string("abc") + std::string(high_bytes) + std::string(pair_bytes));

        const JsString low_then_high = JsString::concat(JsString::concat(abc, low), high);
        assert(low_then_high.length() == 5 && low_then_high.unit_at(3) == 0xDE00 && low_then_high.unit_at(4) == 0xD83D);
        assert(JsString::concat(low_then_high, low).view() ==
               std::string("abc") + std::string("\xED\xB8\x80", 3) + std::string(pair_bytes));

        // The half is owed by the result however the text arrives.
        assert(JsString::concat(abc, high_bytes).view() == std::string("abc") + std::string(high_bytes));
        assert(JsString::concat(JsString::concat(abc, high_bytes), low).view() == std::string("abc") + std::string(pair_bytes));
        assert(JsString::concat(std::string_view("abc"), JsString::concat(abc, high)).view() == std::string("abcabc") + std::string(high_bytes));
        assert(JsString::concat(JsString(), high).view() == high_bytes);
        assert(JsString::concat(JsString(), high).length() == 1);
        assert(JsString::concat(high, low).view() == pair_bytes);
        assert(JsString::concat(abc, JsString()).shares_text_with(abc));
        // Text appended after the half: the half stands alone and is written.
        const JsString then_text = JsString::concat(JsString::concat(abc, high), JsString("z"));
        assert(then_text.view() == std::string("abc") + std::string(high_bytes) + "z" && then_text.length() == 5);
        // A join of more than one unit owes only the last half.
        const JsString tail = JsString::concat(abc, std::string_view("q\xED\xA0\xBD", 4));
        assert(tail.length() == 5 && tail.unit_at(3) == 'q' && tail.unit_at(4) == 0xD83D);
        assert(JsString::concat(tail, low).view() == std::string("abcq") + std::string(pair_bytes));
    }
}

TEST(js_string_built_half_a_pair_at_a_time_costs_what_it_writes) {
    using namespace litebird;

    // AUDIT.md, finding 47. The corpus case `pairs made a half at a time in a
    // long string` (tests/js-conformance/long-strings.dat) appends 60,000
    // halves and took 5.7 s of the 10 s a case is allowed, so it timed out on a
    // busy machine and the JavaScript floor in CI had to sit one below the
    // score. This counts the work instead of timing it: 20,000 pairs write
    // 100,000 bytes, where copying the text so far at every pair wrote about
    // a thousand million.
    JsInterpreter js;
    const auto before = JsString::stats();
    js.eval_ast(
        "var s = '';"
        "for (var i = 0; i < 20000; i++) { s += '\\uD83D'; s += '\\uDE00'; s += '-'; }"
        "var t = '';"
        "for (var i = 0; i < 20000; i++) t += '\\uD83D\\uDE00-';");
    const auto after = JsString::stats();
    assert(!js.last_eval_failed());
    // Both builds together write 200,000 bytes of text. Allow ten times that
    // for the moves a growing block makes; the old path copied 20,000 times
    // the length of the text so far. The blocks are the moves: a string that
    // grows by half needs a few dozen, not one per append.
    assert(after.bytes_copied - before.bytes_copied < 2000000);
    assert(after.blocks - before.blocks < 500);

    // What the two builds say is the same, and every read of the text agrees.
    js.eval_ast(
        "var same = s === t, pairs = 0;"
        "for (var ch of s) if (ch.length === 2) pairs++;"
        "var found = s.indexOf('\\uDE00-\\uD83D', 5), last = s.lastIndexOf('\\uD83D');");
    assert(!js.last_eval_failed());
    assert(js.global_env()->get("same").bool_val);
    assert(js.global_env()->get("pairs").to_number() == 20000);
    assert(js.global_env()->get("found").to_number() == 7);
    assert(js.global_env()->get("last").to_number() == 59997);

    // Keeping every prefix as the string grows: each one is a range of the
    // same few blocks, and each still ends in the half it was given.
    JsInterpreter kept;
    const auto kept_before = JsString::stats();
    kept.eval_ast(
        "var s = '', all = [], ok = true;"
        "for (var i = 0; i < 6000; i++) { s += '\\uD83D'; all.push(s); s += '\\uDE00'; }"
        "for (var i = 0; i < 6000; i += 7) {"
        "  var p = all[i];"
        "  if (p.length !== 2 * i + 1 || p.charCodeAt(p.length - 1) !== 0xD83D) ok = false;"
        "}"
        "var end = s.length, first = all[0].length;");
    const auto kept_after = JsString::stats();
    assert(!kept.last_eval_failed());
    assert(kept.global_env()->get("ok").bool_val);
    assert(kept.global_env()->get("end").to_number() == 12000);
    assert(kept.global_env()->get("first").to_number() == 1);
    assert(kept_after.bytes_copied - kept_before.bytes_copied < 500000);
    assert(kept_after.blocks - kept_before.blocks < 1000);
}

// Strings share blocks of text; these two check that script can never tell.
// The tests themselves are in js_string_stress.hpp, which `make tsan` also
// builds on its own for ThreadSanitizer.
TEST(js_string_matches_a_utf16_model_under_random_edits) { litebird_tests::js_string_model_test(); }

TEST(js_property_maps_and_arrays_keep_their_order_through_front_edits) {
    using namespace litebird;

    // AUDIT.md, finding 33. `delete` used to rebuild the property index on every
    // erase and `shift()` moved the whole tail each call. Counting work would
    // assert implementation details, so this test pins the observable contract
    // while a 2,000-edit workload takes the paths that used to be quadratic.
    JsInterpreter js;
    js.eval_ast(R"(
        var object = {};
        object.a = 'a';
        object.b = 'b';
        object.c = 'c';
        object.d = 'd';
        delete object.b;
        object.b = 'B';
        object.c = 'C';
        var rearranged = Object.keys(object).join(',');

        var big = {};
        for (var i = 0; i < 2000; i++) big['k' + i] = i;
        var removed = 0;
        for (var i = 0; i < 2000; i += 2) {
            removed += big['k' + i];
            delete big['k' + i];
        }
        var remaining = 0;
        for (var i = 1; i < 2000; i += 2) remaining += big['k' + i];
        var keys = Object.keys(big);
        var last = keys[keys.length - 1];
        var reinserted = last === 'k1999';
        delete big['k1'];
        big['k1'] = 'reinserted';
        var reinsertedFirst = false;
        var reinsertedKeys = Object.keys(big);
        reinsertedFirst = reinsertedKeys[reinsertedKeys.length - 1] === 'k1';

        var values = [{ label: 'kept' }, { label: 'second' }];
        var kept = values[0];
        var shifted = values.shift();
        var sameObject = shifted === kept;
        values.unshift(shifted);
        var roundTripped = values[0] === kept && values[1].label === 'second';

        var front = [];
        for (var i = 0; i < 1000; i++) front.push(i);
        var gone = front.splice(0, 500);
        var spliceFront = gone.length + ':' + front.length + ':' + front[0] + ':' + front[front.length - 1];

        var shiftedTotal = 0;
        var array = [];
        for (var i = 0; i < 2000; i++) array.push(i);
        while (array.length) shiftedTotal += array.shift();

        var unshiftFront = [];
        for (var i = 0; i < 1000; i++) unshiftFront.unshift(i);

        var all = rearranged + '|' + removed + '|' + remaining + '|' +
            keys.length + '|' + last + '|' + reinserted + '|' + reinsertedFirst + '|' +
            sameObject + '|' + roundTripped + '|' + spliceFront + '|' + shiftedTotal + '|' + unshiftFront[0];
    )");
    assert(!js.last_eval_failed());
    assert(js.global_env()->get("rearranged").to_string() == "a,c,d,b");
    assert(js.global_env()->get("removed").to_number() == 999000.0);
    assert(js.global_env()->get("remaining").to_number() == 1000000.0);
    assert(js.global_env()->get("keys").array_elements().size() == 1000);
    assert(js.global_env()->get("last").to_string() == "k1999");
    assert(js.global_env()->get("reinserted").bool_val);
    assert(js.global_env()->get("reinsertedFirst").bool_val);
    assert(js.global_env()->get("sameObject").bool_val);
    assert(js.global_env()->get("roundTripped").bool_val);
    assert(js.global_env()->get("spliceFront").to_string() == "500:500:500:999");
    assert(js.global_env()->get("shiftedTotal").to_number() == 1999000.0);
    assert(js.global_env()->get("unshiftFront").array_elements().front().to_number() == 999.0);
    assert(js.global_env()->get("all").to_string() ==
           "a,c,d,b|999000|1000000|1000|k1999|true|true|true|true|500:500:500:999|1999000|999");
}

TEST(js_strings_can_be_used_from_several_threads) { litebird_tests::js_string_thread_test(); }

TEST(phase19_js_compatibility_basics) {
    using namespace litebird;

    JsInterpreter js;
    js.eval_ast(R"(
        var globalBefore = 8;
        { var fromBlock = 12; let blockOnly = 6; }
        function localVarScope() {
            var beforeInit = typeof later;
            if (true) { var later = 5; var innerOnly = 9; }
            return beforeInit + ":" + later;
        }
        var localResult = localVarScope();
        var leakedLocal = typeof innerOnly;
        var shadowed = 1;
        function shadowVar() { var shadowed = 2; return shadowed; }
        var shadowResult = shadowVar();
        for (var loopVar = 0; loopVar < 2; loopVar++) { }
        var loopResult = loopVar;
        var forOfLast;
        for (var item of [2, 3]) { forOfLast = item; }

        var tUndefined = typeof missingName;
        var tNull = typeof null;
        var tNumber = typeof 1;
        var tString = typeof "x";
        var tBoolean = typeof false;
        var tObject = typeof {};
        var tArray = typeof [];
        var tFunction = typeof (() => 1);

        var powRightAssociative = 2 ** 3 ** 2;
        var powUnaryRight = 2 ** -2;
        var powPrecedence = 2 * 3 ** 2;

        var fallback = null ?? "fallback";
        var keepZero = 0 ?? 4;
        var keepFalse = false ?? true;
        var keepEmpty = "" ?? "fallback";
        var nullishEffects = 0;
        var shortCircuit = 1 ?? nullishEffects++;
        var evaluateRight = null ?? nullishEffects++;

        var switchFallthrough = "";
        switch ("2") {
            case 1: switchFallthrough = "wrong"; break;
            case "2": switchFallthrough = "two";
            default: switchFallthrough += " default";
        }
        var defaultPosition = "";
        switch (9) {
            case 1: defaultPosition = "one"; break;
            default: defaultPosition = "default"; break;
            case 9: defaultPosition = "nine"; break;
        }
        var breakCount = 0;
        while (true) { breakCount++; break; }
    )");

    assert(js.global_env()->get("fromBlock").to_number() == 12);
    assert(js.global_env()->get("blockOnly").is_undefined());
    assert(js.global_env()->get("localResult").to_string() == "undefined:5");
    assert(js.global_env()->get("leakedLocal").to_string() == "undefined");
    assert(js.global_env()->get("shadowResult").to_number() == 2);
    assert(js.global_env()->get("shadowed").to_number() == 1);
    assert(js.global_env()->get("loopResult").to_number() == 2);
    assert(js.global_env()->get("forOfLast").to_number() == 3);

    assert(js.global_env()->get("tUndefined").to_string() == "undefined");
    assert(js.global_env()->get("tNull").to_string() == "object");
    assert(js.global_env()->get("tNumber").to_string() == "number");
    assert(js.global_env()->get("tString").to_string() == "string");
    assert(js.global_env()->get("tBoolean").to_string() == "boolean");
    assert(js.global_env()->get("tObject").to_string() == "object");
    assert(js.global_env()->get("tArray").to_string() == "object");
    assert(js.global_env()->get("tFunction").to_string() == "function");

    assert(js.global_env()->get("powRightAssociative").to_number() == 512);
    assert(js.global_env()->get("powUnaryRight").to_number() == 0.25);
    assert(js.global_env()->get("powPrecedence").to_number() == 18);
    assert(js.global_env()->get("fallback").to_string() == "fallback");
    assert(js.global_env()->get("keepZero").to_number() == 0);
    assert(!js.global_env()->get("keepFalse").is_truthy());
    assert(js.global_env()->get("keepEmpty").to_string().empty());
    assert(js.global_env()->get("shortCircuit").to_number() == 1);
    assert(js.global_env()->get("evaluateRight").to_number() == 0);
    assert(js.global_env()->get("nullishEffects").to_number() == 1);
    assert(js.global_env()->get("switchFallthrough").to_string() == "two default");
    assert(js.global_env()->get("defaultPosition").to_string() == "nine");
    assert(js.global_env()->get("breakCount").to_number() == 1);

    // Features not implemented in the bytecode VM must transparently run via
    // the AST interpreter, without running a partially compiled program first.
    JsInterpreter fallback_eval;
    fallback_eval.eval("var p = 2 ** 4; var n = null ?? 3;");
    assert(fallback_eval.global_env()->get("p").to_number() == 16);
    assert(fallback_eval.global_env()->get("n").to_number() == 3);
    assert(fallback_eval.eval("typeof p").to_string() == "number");
    fallback_eval.eval("function hoistedVar() { if (true) { var local = 5; } return local; } var hoistedResult = hoistedVar();");
    assert(fallback_eval.global_env()->get("hoistedResult").to_number() == 5);
    assert(fallback_eval.global_env()->get("local").is_undefined());

    auto parses = [](const std::string& source) {
        try {
            JsLexer lexer(source);
            JsParser parser(lexer.tokenize_all());
            (void)parser.parse_program();
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };
    assert(!parses("var invalid = null ?? false || true;"));
    assert(!parses("var invalid = null && false ?? true;"));
    assert(parses("var valid = (null ?? false) || true;"));
    assert(parses("var valid = null ?? (false || true);"));
}

TEST(js_value_payload_is_compact_and_copy_safe) {
    using namespace litebird;

    // Keep common VM stack/global values compact as type-specific data grows.
    assert(sizeof(JsValue) <= 128);

    auto original = JsValue::make_native_function([](const std::vector<JsValue>&) {
        return JsValue::make_number(1);
    });
    JsValue copied = original;
    original.set_native_function([](const std::vector<JsValue>&) {
        return JsValue::make_number(2);
    });
    assert(copied.native_function()({}).to_number() == 1);
    assert(original.native_function()({}).to_number() == 2);

    JsValue fn;
    fn.type = JsType::Function;
    fn.set_function_params({"first"});
    JsValue fn_copy = fn;
    fn.set_function_params({"second"});
    assert(fn_copy.function_params().size() == 1);
    assert(fn_copy.function_params().front() == "first");
    assert(fn.function_params().front() == "second");
}

TEST(strict_equality_does_not_coerce) {
    using namespace litebird;

    // `===` used to be folded into `==` by the lexer, so `1 === "1"` was true.
    // Strict equality must never coerce, compare null/undefined as distinct, and
    // still compare DOM wrappers by node identity rather than wrapper address.
    //
    // The interpreter needs a document here. It used to be constructed without
    // one: `document` was undefined, every DOM line below silently evaluated to
    // undefined, and `undefined === undefined` made the identity assertions
    // pass without a single DOM node being involved.
    auto doc = std::make_shared<Document>();
    JsInterpreter js(doc);

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
    assert(!js.last_eval_failed());
    assert(js.global_env()->get("el").dom_node_value() != nullptr);
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

    // PPM dimensions and samples are decimal ints supplied by the file. A long
    // digit run must be consumed and rejected without signed-overflow UB.
    const std::string oversized_ppm_width = "P6\n999999999999999999999999999999 1\n255\n";
    const std::string oversized_ppm_maxval = "P6\n1 1\n999999999999999999999999999999\n";
    assert(ImageDecoder::decode(std::vector<uint8_t>(oversized_ppm_width.begin(), oversized_ppm_width.end())) == nullptr);
    assert(ImageDecoder::decode(std::vector<uint8_t>(oversized_ppm_maxval.begin(), oversized_ppm_maxval.end())) == nullptr);
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

// ---------------------------------------------------------------------------
// Phase 29: table spans, collapsed borders and border spacing
// ---------------------------------------------------------------------------

TEST(table_colspan_and_rowspan_geometry) {
    using namespace litebird;

    // colspan: the wide cell covers the columns of the cells below it.
    auto span_root = build_doc(
        "<table id='t' style='width: 300px'>"
        "  <tr><td id='wide' colspan='2'>A</td><td id='third'>B</td></tr>"
        "  <tr><td id='c1'>C</td><td id='c2'>D</td><td id='c3'>E</td></tr>"
        "</table>");
    auto table = find_box_by_id(span_root, "t");
    auto wide = find_box_by_id(span_root, "wide");
    auto third = find_box_by_id(span_root, "third");
    auto c1 = find_box_by_id(span_root, "c1");
    auto c2 = find_box_by_id(span_root, "c2");
    auto c3 = find_box_by_id(span_root, "c3");
    assert(table && wide && third && c1 && c2 && c3);

    // The broad cell starts where the first column starts and is as wide as
    // the two columns it covers.
    assert(std::abs(wide->dimensions.content.x - c1->dimensions.content.x) < 0.5f);
    const float two_columns = c1->dimensions.margin_box().width + c2->dimensions.margin_box().width;
    assert(std::abs(wide->dimensions.margin_box().width - two_columns) < 1.0f);
    // The third column is not pushed along by the colspan.
    assert(std::abs(third->dimensions.content.x - c3->dimensions.content.x) < 0.5f);

    // rowspan: the tall cell covers both rows, so its height is their sum and
    // the following row still starts below the whole block.
    auto row_root = build_doc(
        "<table id='rt' style='width: 200px'>"
        "  <tr><td id='tall' rowspan='2'>X</td><td id='y'>Y</td></tr>"
        "  <tr><td id='z'>Z</td></tr>"
        "  <tr><td id='w'>W</td><td id='v'>V</td></tr>"
        "</table>");
    auto rt = find_box_by_id(row_root, "rt");
    auto tall = find_box_by_id(row_root, "tall");
    auto y = find_box_by_id(row_root, "y");
    auto z = find_box_by_id(row_root, "z");
    auto w = find_box_by_id(row_root, "w");
    assert(rt && tall && y && z && w);

    const float two_rows = y->dimensions.margin_box().height + z->dimensions.margin_box().height;
    assert(std::abs(tall->dimensions.margin_box().height - two_rows) < 1.0f);
    assert(std::abs(z->dimensions.content.y - (y->dimensions.content.y + y->dimensions.margin_box().height)) < 0.5f);
    // The third row is below the spanned cell, not beside it.
    assert(w->dimensions.content.y >= tall->dimensions.content.y + tall->dimensions.margin_box().height - 0.5f);
    assert(std::abs(w->dimensions.content.x - tall->dimensions.content.x) < 0.5f);

    // rowspan="0" means "to the end of the row group".
    auto zero_root = build_doc(
        "<table id='zt' style='width: 200px'>"
        "  <tr><td id='all' rowspan='0'>A</td><td>B</td></tr>"
        "  <tr><td>C</td></tr>"
        "  <tr><td>D</td></tr>"
        "</table>");
    auto all = find_box_by_id(zero_root, "all");
    auto zt = find_box_by_id(zero_root, "zt");
    assert(all && zt);
    assert(std::abs(all->dimensions.margin_box().height - zt->dimensions.content.height) < 1.0f);

    // A colspan larger than the grid is clamped rather than allocating columns.
    auto clamp_root = build_doc(
        "<table id='ct' style='width: 200px'>"
        "  <tr><td id='huge' colspan='99'>A</td></tr>"
        "  <tr><td id='plain'>B</td></tr>"
        "</table>");
    auto huge = find_box_by_id(clamp_root, "huge");
    auto ct = find_box_by_id(clamp_root, "ct");
    assert(huge && ct);
    assert(std::abs(huge->dimensions.margin_box().width - ct->dimensions.content.width) < 1.0f);
}

TEST(table_border_collapse_and_spacing) {
    using namespace litebird;

    // Separate (default): every cell owns its full 4px border.
    auto separate_root = build_doc(
        "<table id='st' style='width: 200px'>"
        "  <tr><td id='sa' style='border: 4px solid #000'>A</td>"
        "      <td id='sb' style='border: 4px solid #000'>B</td></tr>"
        "</table>");
    auto sa = find_box_by_id(separate_root, "sa");
    auto sb = find_box_by_id(separate_root, "sb");
    assert(sa && sb);
    assert(std::abs(sa->dimensions.border.left - 4.0f) < 0.01f);
    assert(std::abs(sb->dimensions.border.left - 4.0f) < 0.01f);

    // Collapsed: neighbours share the border, so each cell only paints half.
    // The visual line between A and B is still 4px (2 + 2), not 8px.
    auto collapsed_root = build_doc(
        "<table id='ct' style='width: 200px; border-collapse: collapse'>"
        "  <tr><td id='ca' style='border: 4px solid #000'>A</td>"
        "      <td id='cb' style='border: 4px solid #000'>B</td></tr>"
        "</table>");
    auto ca = find_box_by_id(collapsed_root, "ca");
    auto cb = find_box_by_id(collapsed_root, "cb");
    auto ct = find_box_by_id(collapsed_root, "ct");
    assert(ca && cb && ct);
    assert(std::abs(ca->dimensions.border.left - 2.0f) < 0.01f);
    assert(std::abs(cb->dimensions.border.left - 2.0f) < 0.01f);
    // The shared edge is the sum of the two halves.
    const float shared = ca->dimensions.border.right + cb->dimensions.border.left;
    assert(std::abs(shared - 4.0f) < 0.01f);

    // border-spacing pushes the cells in from the table edge and apart.
    auto spaced_root = build_doc(
        "<table id='pt' style='width: 220px; border-spacing: 10px 6px'>"
        "  <tr><td id='p'>P</td><td id='q'>Q</td></tr>"
        "  <tr><td id='r'>R</td><td id='s'>S</td></tr>"
        "</table>");
    auto pt = find_box_by_id(spaced_root, "pt");
    auto p = find_box_by_id(spaced_root, "p");
    auto q = find_box_by_id(spaced_root, "q");
    auto r = find_box_by_id(spaced_root, "r");
    assert(pt && p && q && r);

    assert(std::abs(p->dimensions.content.x - (pt->dimensions.content.x + 10.0f)) < 0.5f);
    assert(std::abs(q->dimensions.content.x -
                     (p->dimensions.content.x + p->dimensions.margin_box().width + 10.0f)) < 0.5f);
    assert(std::abs(r->dimensions.content.y -
                     (p->dimensions.content.y + p->dimensions.margin_box().height + 6.0f)) < 0.5f);

    // A collapsed table has no spacing at all.
    auto collapsed_spaced_root = build_doc(
        "<table id='cst' style='width: 220px; border-collapse: collapse; border-spacing: 10px'>"
        "  <tr><td id='cp'>P</td><td id='cq'>Q</td></tr>"
        "</table>");
    auto cst = find_box_by_id(collapsed_spaced_root, "cst");
    auto cp = find_box_by_id(collapsed_spaced_root, "cp");
    assert(cst && cp);
    assert(std::abs(cp->dimensions.content.x - cst->dimensions.content.x) < 0.5f);
}

// Depth of the deepest element below `root`, counting `root`'s children as 1.
static int deepest_element_below(const litebird::Node& root) {
    struct Frame { const litebird::Node* node; std::size_t next; };
    std::vector<Frame> open;
    open.push_back({&root, 0});
    int deepest = 0;
    while (!open.empty()) {
        Frame& top = open.back();
        if (top.next >= top.node->children().size()) { open.pop_back(); continue; }
        const litebird::Node* child = top.node->children()[top.next++].get();
        if (child->is_element()) deepest = std::max(deepest, static_cast<int>(open.size()));
        open.push_back({child, 0});
    }
    return deepest;
}

TEST(dom_trees_of_any_depth_can_be_walked_and_released) {
    using namespace litebird;

    // The HTML parser stops nesting at 512 open elements, but a script can
    // still append a child to a child to a child for as long as it likes.
    // Every walk over the DOM used to call itself once per level, and so did
    // the destructor: at some tens of thousands of levels the first of them
    // to run ended the process. Which one that was depended on the page:
    // looking up the <title>, textContent, innerHTML, a selector, a custom
    // element callback, or just dropping the last reference.
    //
    // These walks have no limit now. They are not allowed one: a selector
    // that misses matches below some depth is a wrong answer.
    //
    // A million levels on the usual 8 MB stack leave 8 bytes of stack for each
    // level. A function that calls itself uses at least 16 (the return address
    // and the alignment the calling convention asks for), so a walk that
    // recurses once per level cannot finish here, however small its frame.
    const int depth = 1000000;

    {
        auto doc = std::make_shared<Document>();
        auto top = std::make_shared<Element>("div");
        std::shared_ptr<Element> at = top;
        for (int i = 1; i < depth; ++i) {
            auto next = std::make_shared<Element>("div");
            at->append_child(next);
            at = next;
        }
        at->set_attribute("id", "bottom");
        at->append_child(std::make_shared<Text>("deep"));

        assert(top->text_content() == "deep");
        const std::string innermost = "<div id=\"bottom\">deep</div>";
        assert(top->inner_html().size() == static_cast<size_t>(depth - 2) * 11 + innermost.size());
        assert(top->outer_html().size() == static_cast<size_t>(depth - 1) * 11 + innermost.size());
        assert(top->descendants_with_tag("div").size() == static_cast<size_t>(depth - 1));
        assert(top->descendant_with_id("bottom") == at);
        assert(top->query_selector_all("div").size() == static_cast<size_t>(depth - 1));
        assert(top->query_selector("#bottom") == at);
        assert(top->query_selector("p") == nullptr);
        assert(deepest_element_below(*top) == depth - 1);

        // The bottom cannot take the top as its child, however far apart.
        at->append_child(top);
        assert(at->children().size() == 1 && top->parent() == nullptr);

        // In a document: every node learns it is connected, and the
        // document-wide lookups see all of it.
        doc->append_child(top);
        assert(at->is_connected());
        assert(doc->get_element_by_id("bottom") == at);
        assert(doc->get_elements_by_tag_name("div").size() == static_cast<size_t>(depth));
        assert(doc->query_selector_all("#bottom").size() == 1);

        // The indented dump is for reading. It prints 1,024 levels and says
        // what it left out, instead of a million lines of up to two million
        // spaces.
        std::ostringstream dump;
        doc->dump(dump);
        const std::string text = dump.str();
        assert(text.size() < 8 * 1024 * 1024);
        assert(text.find(" nodes not shown, nested up to ") != std::string::npos);

        // Layout and painting do recurse, and ask at every level whether
        // there is stack left. What matters here is that they return.
        StyleResolver resolver;
        auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
        assert(root != nullptr);
        DisplayList::build(root);

        doc->remove_child(top);
        assert(!at->is_connected() && top->parent() == nullptr);
    }   // and here a million nodes are released, one inside the other

    // Shadow trees inside shadow trees: the same, through the other link.
    {
        auto top = std::make_shared<Element>("div");
        std::shared_ptr<Element> at = top;
        for (int i = 0; i < 50000; ++i) {
            auto shadow = at->attach_shadow("open");
            auto inner = std::make_shared<Element>("div");
            shadow->append_child(inner);
            at = inner;
        }
        at->append_child(top);                  // a host contains its shadow tree, at any depth
        assert(at->children().empty());
    }

    // A fragment appended at every level. Whether the fragment contains the
    // node it is appended to is asked from both ends at once; walking up from
    // the bottom alone made this loop cost depth * depth steps, a minute here.
    {
        auto top = std::make_shared<Element>("div");
        std::shared_ptr<Element> at = top;
        for (int i = 0; i < depth; ++i) {
            auto fragment = std::make_shared<Node>(NodeType::DocumentFragment);
            auto next = std::make_shared<Element>("div");
            fragment->append_child(next);
            at->append_child(fragment);
            at = next;
        }
        assert(at->parent() != nullptr);
        assert(top->descendants_with_tag("div").size() == static_cast<size_t>(depth));
    }

    // The same from script.
    {
        auto doc = HtmlParser::parse("<!doctype html><title>t</title><body></body>");
        JsInterpreter js(doc);
        js.eval_ast(R"(
            var top = document.createElement('div'), at = top;
            for (var i = 1; i < 100000; i++) { var d = document.createElement('div'); at.appendChild(d); at = d; }
            at.appendChild(document.createTextNode('deep'));
            var text = top.textContent;
            var markup = top.innerHTML.length;
            var found = top.querySelectorAll('div').length;
            var error = 'none';
            try { at.appendChild(top); } catch (e) { error = e.name; }
            document.body.appendChild(top);
            var connected = at.isConnected;
            var all = document.querySelectorAll('div').length;
            var steps = 0; for (var p = at; p && p !== document.body; p = p.parentNode) steps++;
            var copy = document.createElement('div'); copy.innerHTML = top.outerHTML;
            var copied = copy.querySelectorAll('div').length;
            document.body.removeChild(top);
            top = at = d = p = copy = null;
            var done = 'released';
        )");
        auto value = [&](const char* name) { return js.global_env()->get(name).to_string(); };
        assert(value("text") == "deep");
        assert(value("markup") == std::to_string(99999 * 11 + 4));
        assert(value("found") == "99999");
        assert(value("error") == "HierarchyRequestError");
        assert(value("connected") == "true");
        assert(value("all") == "100000");
        assert(value("steps") == "100000");
        assert(value("copied") == "100000");
        assert(value("done") == "released");
    }

    // A callback that changes the tree it is being told about. The elements
    // to notify are listed before the first callback runs, so one that
    // removes its siblings does not pull the tree out from under the walk.
    {
        auto doc = HtmlParser::parse("<!doctype html><body></body>");
        JsInterpreter js(doc);
        js.eval_ast(R"(
            var calls = 0;
            class Eater extends HTMLElement {
                connectedCallback() {
                    calls++;
                    var p = this.parentNode;
                    while (p && p.firstChild) p.removeChild(p.firstChild);
                }
            }
            customElements.define('eat-el', Eater);
            var top = document.createElement('div');
            for (var i = 0; i < 50; i++) {
                var e = document.createElement('eat-el');
                e.appendChild(document.createElement('eat-el'));
                top.appendChild(e);
            }
            document.body.appendChild(top);
            var left = top.childNodes.length;
        )");
        assert(js.global_env()->get("calls").to_number() >= 1);
        assert(js.global_env()->get("left").to_string() == "0");
    }
}

TEST(html_parser_nesting_limit_holds_for_every_kind_of_element) {
    using namespace litebird;

    // deeply_nested_markup_does_not_overflow_the_stack pins the rule down for
    // <div>. Elements reach the tree by other roads too: the table modes
    // insert rows and cells the markup never mentions, formatting elements
    // are reconstructed, the adoption agency algorithm moves whole subtrees,
    // foreign elements have an insertion routine of their own, and a
    // template's children go into its contents. Each must stop at the limit
    // and lose nothing.
    const int max_open = static_cast<int>(HtmlTreeBuilder::kMaxTreeDepth);
    auto repeat = [](const std::string& piece, int times) {
        std::string out;
        out.reserve(piece.size() * static_cast<size_t>(times));
        for (int i = 0; i < times; ++i) out += piece;
        return out;
    };
    struct Case { const char* open; const char* count_tag; int per_level; };
    const Case cases[] = {
        {"<span>", "span", 1},
        {"<b>", "b", 1},
        {"<ul><li>", "li", 1},
        {"<table><tr><td>", "td", 1},
        {"<blockquote><p>", "blockquote", 1},
        {"<details><summary>s</summary>", "details", 1},
    };
    for (const auto& c : cases) {
        const int levels = 3000;
        auto doc = HtmlParser::parse("<!doctype html>" + repeat(c.open, levels) + "bottom");
        assert(doc->get_elements_by_tag_name(c.count_tag).size() == static_cast<size_t>(levels * c.per_level));
        // <html> is at depth 1.
        assert(deepest_element_below(*doc) <= max_open);
        assert(deepest_element_below(*doc) >= max_open - 3);
        assert(doc->body()->text_content().find("bottom") != std::string::npos);

        StyleResolver resolver;
        auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
        assert(root != nullptr);
        DisplayList::build(root);
    }

    // Foreign content: SVG groups.
    {
        auto doc = HtmlParser::parse("<!doctype html><svg>" + repeat("<g>", 3000) + "<rect/>");
        assert(doc->get_elements_by_tag_name("g").size() == 3000);
        assert(deepest_element_below(*doc) <= max_open);
    }

    // Templates: the children of a template are in its contents, which the
    // html5lib dump shows one step further in. 3,000 of them nested must
    // neither crash nor print 3,000 levels.
    {
        auto doc = HtmlParser::parse("<!doctype html>" + repeat("<template>", 3000) + "x");
        const std::string dump = dump_tree_webplatform(*doc);
        size_t longest_indent = 0;
        size_t line_start = 0;
        while (line_start < dump.size()) {
            size_t first = dump.find_first_not_of(' ', line_start + 1);
            if (first == std::string::npos) break;
            longest_indent = std::max(longest_indent, first - line_start);
            size_t end = dump.find('\n', line_start);
            if (end == std::string::npos) break;
            line_start = end + 1;
        }
        // Two spaces a level, and a template counts twice (element, contents).
        assert(longest_indent <= static_cast<size_t>(max_open) * 4 + 8);
    }

    // The adoption agency algorithm moves a block, with everything in it,
    // to another parent, and then puts the block's children one level
    // further down. In markup like this each </b> used to move one <div>
    // under the previous one, limit or no limit: 2,000 levels more.
    {
        auto doc = HtmlParser::parse("<!doctype html>" + repeat("<div>", 600) + repeat("<b><div>x</b>", 2000));
        assert(deepest_element_below(*doc) <= max_open);
        assert(doc->body()->text_content() == std::string(2000, 'x'));
        assert(doc->get_elements_by_tag_name("div").size() == 2600);
    }
    // The same block as the target of many formatting elements, one after
    // another: each wrapped its children in one more element.
    //
    // Everything written is in the tree, but not always in the order it was
    // written. The <p> belongs inside the <section> and, at the limit, was
    // attached next to it; the algorithm then moves the <section> and has no
    // reason to take the <p> along. The limit keeps content, not order, once
    // misnested formatting tags move a block that sits on it.
    {
        std::string html = "<!doctype html>" + repeat("<div>", 505);
        for (int i = 0; i < 300; ++i) html += "<b id=\"b" + std::to_string(i) + "\">";
        html += "<section>x<p>y</p>";
        html += repeat("</b>", 300);
        auto doc = HtmlParser::parse(html);
        assert(deepest_element_below(*doc) <= max_open);
        std::string text = doc->body()->text_content();
        std::sort(text.begin(), text.end());
        assert(text == "xy");
        assert(doc->get_elements_by_tag_name("section").size() == 1);
        assert(doc->get_elements_by_tag_name("p").size() == 1);
    }
    // Below the limit the same markup is untouched by it: the tree is the
    // standard's, and so is the order.
    {
        std::string html = "<!doctype html>" + repeat("<div>", 100);
        for (int i = 0; i < 300; ++i) html += "<b id=\"b" + std::to_string(i) + "\">";
        html += "<section>x<p>y</p>";
        html += repeat("</b>", 300);
        auto doc = HtmlParser::parse(html);
        assert(deepest_element_below(*doc) <= max_open);
        assert(doc->body()->text_content() == "xy");
        auto paragraph = doc->get_elements_by_tag_name("p").front();
        std::shared_ptr<Node> up = paragraph;
        while (up && !(up->type() == NodeType::Element &&
                       std::static_pointer_cast<Element>(up)->tag_name() == "section")) up = up->parent();
        assert(up != nullptr);     // the <p> is inside the <section>
    }
    // </form> takes the form off the stack of open elements and leaves what
    // is open inside it where it is, so here the tree is twice as deep as the
    // stack is long. The limit is about the tree.
    {
        auto doc = HtmlParser::parse("<!doctype html>" + repeat("<form><div></form>", 600) + "x");
        assert(deepest_element_below(*doc) <= max_open);
        assert(deepest_element_below(*doc) >= max_open - 3);
        assert(doc->get_elements_by_tag_name("div").size() == 600);
    }

    // The limit changes where a node is attached and nothing else. An earlier
    // version closed the current node at the limit instead, and the insertion
    // mode went on assuming it was open: here "in select in table" without a
    // <select>, whose rule for </table> then popped every open element,
    // <html> included. Found by random pages at the limit.
    {
        auto doc = HtmlParser::parse("<!doctype html>" + repeat("<div>", 504) +
                                     "<pre><div><table><a><pre><select><svg></table>x<li a=7><a>after");
        assert(doc->body() != nullptr);
        assert(deepest_element_below(*doc) <= max_open);
        assert(doc->body()->text_content().find("after") != std::string::npos);
        // What follows is still parsed inside <body>, not at the root.
        assert(doc->get_elements_by_tag_name("li").size() == 1);
        std::shared_ptr<Node> up = doc->get_elements_by_tag_name("li").front();
        while (up && up != doc->body()) up = up->parent();
        assert(up == doc->body());
    }

    // 60 bytes with no nesting at all did the same on their own: the second
    // <button> closes the first and the <select> with it, the insertion mode
    // stays "in select in table", and <tr> pops until a select has been
    // popped. With none open that emptied the stack, and the adoption agency
    // algorithm then read the open element above the first one
    // (heap-buffer-overflow under AddressSanitizer, SIGSEGV without).
    {
        auto doc = HtmlParser::parse("<template><th><a><button><select><button><tr a=0><button><a>");
        assert(doc->get_elements_by_tag_name("html").size() == 1);
        assert(doc->get_elements_by_tag_name("head").size() == 1);
        assert(!dump_tree_webplatform(*doc).empty());
    }

    // Many elements left open, then many tokens. The stack of open elements
    // is as long as the markup is deep, and each of these used to walk down
    // it once per token: 40,000 open <div> and 40,000 list items took two
    // minutes. No time is asserted; a walk per token would make this block
    // take minutes instead of a second.
    {
        const int open = 60000;
        const int then = 60000;
        const std::pair<const char*, const char*> shapes[] = {
            {"<div>", "<li>x</li>"},          {"<div>", "<dd>x</dd>"},       {"<span>", "</x>"},
            {"<div>", "<table></table>"},     {"<div>", "<b>x</b>"},         {"<div>", "</td>"},
            {"<div>", "<b><p>x</b>"},         {"<div>", "<form><div></form>"},
        };
        for (const auto& [open_tag, token] : shapes) {
            auto doc = HtmlParser::parse("<!doctype html>" + repeat(open_tag, open) + repeat(token, then));
            assert(doc->body() != nullptr);
            assert(deepest_element_below(*doc) <= max_open);
        }
    }

    // Found by random pages at the limit: the handler for a second <a> held a
    // pointer into the list of active formatting elements across the adoption
    // agency algorithm, which rewrites that list. Afterwards the pointer was
    // at whatever formatting element had moved into the slot, and that one
    // was closed instead of the <a>: here the <em>, so "y" lost its emphasis
    // and the tree ended up one level deeper than the stack of open elements.
    {
        auto doc = HtmlParser::parse("<!doctype html><a><em><div><a>x</div>y");
        const std::string expected =
            "| <!DOCTYPE html>\n"
            "| <html>\n"
            "|   <head>\n"
            "|   <body>\n"
            "|     <a>\n"
            "|       <em>\n"
            "|     <em>\n"
            "|       <div>\n"
            "|         <a>\n"
            "|         <a>\n"
            "|           \"x\"\n"
            "|       <a>\n"
            "|         \"y\"\n";
        assert(dump_tree_webplatform(*doc) == expected);
    }
}

// ---------------------------------------------------------------------------
// Phase 26: the tree builder against pages nobody wrote
// ---------------------------------------------------------------------------

namespace {

/// A tree dump as one line: each node followed by its attributes in brackets
/// and its children in parentheses. "<p class=k>a<br>b" is
/// html(head body(p[class="k"]("a" br "b"))). An SVG or MathML element is
/// written svg:name or math:name, the contents of a template as content(...).
std::string outline(const std::string& dump) {
    struct Entry {
        std::size_t depth;
        std::string label;
        std::string attributes;
        bool is_node;
    };
    std::vector<Entry> entries;
    std::size_t at = 0;
    while (at < dump.size()) {
        std::size_t end = dump.find('\n', at);
        if (end == std::string::npos) end = dump.size();
        const std::string line = dump.substr(at, end - at);
        at = end + 1;
        if (line.rfind("| ", 0) != 0) {
            // Text with a newline in it runs on to the next line of the dump.
            if (!entries.empty()) entries.back().label += "\n" + line;
            continue;
        }
        std::size_t indent = 2;
        while (indent < line.size() && line[indent] == ' ') ++indent;
        std::string label = line.substr(indent);
        const bool element = !label.empty() && label.front() == '<' && label.back() == '>';
        const bool node = element || (!label.empty() && label.front() == '"') || label == "content";
        if (!node) {
            // An attribute: it belongs to the last node one level up.
            for (std::size_t i = entries.size(); i-- > 0;) {
                if (!entries[i].is_node || entries[i].depth + 1 != (indent - 2) / 2) continue;
                entries[i].attributes += (entries[i].attributes.empty() ? "" : " ") + label;
                break;
            }
            continue;
        }
        if (element && label.rfind("<!", 0) != 0) {
            label = label.substr(1, label.size() - 2);
            for (char& c : label) {
                if (c == ' ') c = ':';
            }
        }
        entries.push_back({(indent - 2) / 2, std::move(label), "", true});
    }

    std::string out;
    std::vector<std::size_t> open;      // depths of the nodes whose "(" is not closed yet
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        while (!open.empty() && open.back() >= entry.depth) {
            out += ")";
            open.pop_back();
        }
        if (!out.empty() && out.back() != '(') out += " ";
        out += entry.label;
        if (!entry.attributes.empty()) out += "[" + entry.attributes + "]";
        if (i + 1 < entries.size() && entries[i + 1].depth > entry.depth) {
            out += "(";
            open.push_back(entry.depth);
        }
    }
    out.append(open.size(), ')');
    return out;
}

std::string outline_of(const std::string& html) {
    return outline(tree_of(html));
}

/// The outline of a fragment parsed as the content of `context`, which is
/// written as in the corpus: "td", "svg title", "math mi".
std::string fragment_outline(const std::string& html, std::string context, bool scripting = true) {
    using namespace litebird;
    ElementNamespace ns = ElementNamespace::HTML;
    if (context.rfind("svg ", 0) == 0) { ns = ElementNamespace::SVG; context = context.substr(4); }
    else if (context.rfind("math ", 0) == 0) { ns = ElementNamespace::MathML; context = context.substr(5); }
    auto fragment = HtmlParser::parse_fragment_with_context(html, context, ns, scripting);
    return outline(dump_tree_webplatform(*fragment));
}

} // namespace

TEST(html_parser_void_element_with_a_slash_closes_nothing_else) {
    // <br/>, <img ... />, <meta ... />: the XHTML way of writing an element
    // that has no end tag. The slash means nothing in HTML. It used to close
    // the element's parent as well, so the text after a <br/> was outside the
    // paragraph, and a <link/> in the head put the rest of the head, and the
    // body, outside <html>. The corpus has no void element written this way.
    assert(outline_of("<p>a<br/>b</p>c") == "html(head body(p(\"a\" br \"b\") \"c\"))");
    assert(outline_of("<div><img src=\"x\"/>text</div>") == "html(head body(div(img[src=\"x\"] \"text\")))");
    assert(outline_of("<head><meta charset=\"utf-8\"/><link rel=\"x\"/><title>t</title></head><body>b") ==
           "html(head(meta[charset=\"utf-8\"] link[rel=\"x\"] title(\"t\")) body(\"b\"))");
    assert(outline_of("<ul><li><input type=\"text\" /> label</li></ul>") ==
           "html(head body(ul(li(input[type=\"text\"] \" label\"))))");
    assert(outline_of("<p>a<hr/>b") == "html(head body(p(\"a\") hr \"b\"))");
    assert(outline_of("<p><wbr/><embed/><area/>x") == "html(head body(p(wbr embed area \"x\")))");
    assert(outline_of("<object><param name=a />x</object>") == "html(head body(object(param[name=\"a\"] \"x\")))");
    // The void elements that have rules of their own in the table and
    // frameset modes.
    assert(outline_of("<table><col/><tr><td>x") == "html(head body(table(colgroup(col) tbody(tr(td(\"x\"))))))");
    assert(outline_of("<table><input type=hidden /><tr><td>x") ==
           "html(head body(table(input[type=\"hidden\"] tbody(tr(td(\"x\"))))))");
    assert(outline_of("<frameset><frame/></frameset>") == "html(head frameset(frame))");

    // The same page with and without the slashes is the same tree.
    assert(tree_of("<p>a<br/>b<img/>c<input/>d</p>e") == tree_of("<p>a<br>b<img>c<input>d</p>e"));

    // On an element that is not void the slash is ignored: the <div> stays
    // open. In SVG and MathML it does close the element.
    assert(outline_of("<div/>x") == "html(head body(div(\"x\")))");
    assert(outline_of("<svg><g/>x</svg>y") == "html(head body(svg:svg(svg:g \"x\") \"y\"))");
}

TEST(html_parser_end_tags_close_html_elements_only) {
    // </button> closes the button whatever is open inside it, like </div>.
    assert(outline_of("<button><p>a</button>x") == "html(head body(button(p(\"a\")) \"x\"))");
    assert(outline_of("<button><div></button>x") == "html(head body(button(div) \"x\"))");

    // An <svg> is an ordinary element to the adoption agency algorithm: the
    // </a> closes it together with the <a>, and nothing is moved.
    assert(outline_of("<a><svg></a>x") == "html(head body(a(svg:svg) \"x\"))");
    // A MathML <mi> bounds a scope, so the <b> opened outside it is out of
    // reach and its end tag is ignored.
    assert(outline_of("<b><math><mi></b>x") == "html(head body(b(math:math(math:mi(\"x\")))))");

    // An end tag handled by the HTML rules is matched against HTML elements.
    // The SVG <title> is not a title to close, the <math> not a math; both
    // end tags are ignored, and so is the <tr>, which is still inside an
    // <em>.
    assert(outline_of("<svg><title><em></title><tr>x") == "html(head body(svg:svg(svg:title(em(\"x\")))))");
    assert(outline_of("<math><mtext><i></math><mtext>x") ==
           "html(head body(math:math(math:mtext(i(mtext(\"x\"))))))");
    assert(outline_of("<svg><desc><b></svg>x") == "html(head body(svg:svg(svg:desc(b(\"x\")))))");
    // An SVG <wbr> is not special, so it does not stand in the way of </span>.
    assert(outline_of("<span><svg><wbr></span>x") == "html(head body(span(svg:svg(svg:wbr)) \"x\"))");
    // An SVG <rt> has no implied end tag: </form> leaves it open.
    assert(outline_of("<form><svg><rt></form><g>") == "html(head body(form(svg:svg(svg:rt(svg:g)))))");
    // An SVG <template> is not where a table's rows end up.
    assert(outline_of("<table><svg><template><desc><tr>") ==
           "html(head body(svg:svg(svg:template(svg:desc)) table(tbody(tr))))");

    // In a fragment for an <svg>, with only the root open: an end tag that
    // matches nothing is ignored, and </p> makes a paragraph.
    assert(fragment_outline("</foo>x", "svg svg") == "\"x\"");
    assert(fragment_outline("</p>x", "svg svg") == "p \"x\"");
}

TEST(html_parser_formatting_elements_follow_the_standard) {
    // "Noah's Ark": the list of active formatting elements keeps three of a
    // kind, and the fourth pushes out the earliest. Two of the three that
    // are left were opened inside the <div>, so two are opened again after it.
    assert(outline_of("<i><i><div><i><i></div>x") == "html(head body(i(i(div(i(i)) i(i(\"x\"))))))");
    // Which one goes shows in the order of what is opened again: of
    // b b i b b the first <b> is dropped, leaving b i b b.
    assert(outline_of("<p><b><b><i><b><b></p>x") == "html(head body(p(b(b(i(b(b))))) b(i(b(b(\"x\"))))))");
    assert(outline_of("<p><b><b><b><b></p>x") == "html(head body(p(b(b(b(b)))) b(b(b(\"x\")))))");
    // Attributes are part of the kind: five <b> here, no four of them alike,
    // and all five are opened again.
    assert(outline_of("<p><b a=1><b a=1><b a=2><b a=1><b a=2></p>x") ==
           "html(head body(p(b[a=\"1\"](b[a=\"1\"](b[a=\"2\"](b[a=\"1\"](b[a=\"2\"]))))) "
           "b[a=\"1\"](b[a=\"1\"](b[a=\"2\"](b[a=\"1\"](b[a=\"2\"](\"x\")))))))");

    // A second <nobr> closes the first. With the first one behind a marker
    // (the <marquee>, whose end tag never came) the adoption agency finds no
    // formatting element and does what any other end tag does.
    assert(outline_of("<nobr><table><marquee></table><nobr>x") ==
           "html(head body(nobr(marquee table) nobr(\"x\")))");
}

TEST(html_parser_text_in_a_table_follows_the_standard) {
    // Text in an element that is open inside a table (a <div> that was
    // foster-parented) is ordinary text: the formatting elements are opened
    // again around it, even when it is only a newline.
    assert(outline_of("<table><em><table><div>\n") == "html(head body(em table div(em(\"\n\")) table))");
    assert(outline_of("<table><b><tr><td>x</td></tr><div> y") ==
           "html(head body(b div(b(\" y\")) table(tbody(tr(td(\"x\"))))))");

    // U+0000 is dropped from text in a table before the text is looked at,
    // so the spaces around it stay in the table.
    assert(outline_of(std::string("<table> \0 <tr>", 14)) == "html(head body(table(\"  \" tbody(tr))))");

    // Whitespace directly in a template that holds rows is placed as it is,
    // like whitespace directly in a table. (parse5 and Blink do not have the
    // template in this rule and open the <b> again around the space.)
    assert(outline_of("<template><tr></tr><b><td></td></tr> ") ==
           "html(head(template(content(tr b tr(td) \" \"))) body)");
}

TEST(html_parser_frameset_follows_the_frameset_ok_flag) {
    // A <noscript> does not rule out a frameset page.
    assert(outline_of("<h2><noscript></noscript><frameset>") == "html(head frameset)");
    // Text does.
    assert(outline_of("<noscript></noscript>x<frameset>") == "html(head(noscript) body(\"x\"))");
    // A <template> in the head sets the flag to "not ok"; the body that is
    // then made for the <p> sets it back.
    assert(outline_of("<head><template></template></head><p><frameset>") ==
           "html(head(template(content)) frameset)");

    // A second <head> is ignored and the first stays open.
    assert(outline_of("<head><head>\n") == "html(head(\"\n\") body)");
}

TEST(html_parser_fragments_for_every_kind_of_context) {
    // A fragment for a <template>: this ended the process, whatever the
    // fragment was.
    assert(fragment_outline("<b>x", "template") == "b(\"x\")");
    assert(fragment_outline("<tr><td>x", "template") == "tr(td(\"x\"))");
    assert(fragment_outline("", "template") == "");
    // Inside a template there is no form pointer, so a form may be nested,
    // and a form in a table is kept.
    assert(fragment_outline("<form><form>", "template") == "form(form)");
    assert(outline_of("<template><table><form></table></template>") ==
           "html(head(template(content(table(form)))) body)");

    // The content of an element whose content is text is all text. No start
    // tag was seen, so no end tag ends it.
    assert(fragment_outline("<!-- inside </script> -->", "script") == "\"<!-- inside </script> -->\"");
    assert(fragment_outline("</textarea>x", "textarea") == "\"</textarea>x\"");
    assert(fragment_outline("a</title>b", "title") == "\"a</title>b\"");
    // <noscript> is text when scripting is enabled and markup when it is not.
    assert(fragment_outline("<b>x", "noscript") == "\"<b>x\"");
    assert(fragment_outline("<b>x", "noscript", false) == "b(\"x\")");

    // CDATA sections exist where the content is SVG or MathML, and that
    // includes the top level of a fragment parsed for such an element.
    assert(fragment_outline("<![CDATA[x]]><g/>", "svg svg") == "\"x\" svg:g");
    assert(fragment_outline("<![CDATA[x]]>", "svg title") == "\"x\"");
    assert(fragment_outline("<![CDATA[x]]>", "div") == "<!-- [CDATA[x]] -->");
}

TEST(html_parser_reads_malformed_markup_as_written) {
    // A numeric reference with no digits is text, in the case it was typed.
    assert(outline_of("&#X") == "html(head body(\"&#X\"))");
    assert(outline_of("FOO&#xZOO") == "html(head body(\"FOO&#xZOO\"))");

    // A comment the page ends in: the dashes that had begun to close it are
    // not its text.
    assert(outline_of("<!--x--") == "<!-- x --> html(head body)");
    assert(outline_of("<!--x-") == "<!-- x --> html(head body)");
    assert(outline_of("<!--x--!") == "<!-- x --> html(head body)");
    assert(outline_of("<!--x---") == "<!-- x- --> html(head body)");
    assert(outline_of("<!---") == "<!--  --> html(head body)");

    // "=" where an attribute name should begin is the start of the name.
    assert(outline_of("<a =>") == "html(head body(a[==\"\"]))");
    assert(outline_of("<a =x=y b>") == "html(head body(a[=x=\"y\" b=\"\"]))");

    // The page with nothing in it has a tree like any other.
    assert(outline_of("") == "html(head body)");

    // The doctype is written where it stood: after the comments before it.
    assert(outline_of("<!--c--><!doctype html>") == "<!-- c --> <!DOCTYPE html> html(head body)");
    assert(outline_of("<!--a--><!--b--><!DOCTYPE html><!--c-->x") ==
           "<!-- a --> <!-- b --> <!DOCTYPE html> <!-- c --> html(head body(\"x\"))");
    assert(outline_of("<!DOCTYPE html><!--c-->x") == "<!DOCTYPE html> <!-- c --> html(head body(\"x\"))");
}

TEST(inner_html_is_parsed_as_the_content_of_its_element) {
    using namespace litebird;

    // What a piece of markup is depends on the element it is assigned to.
    // All of it used to be parsed as the content of a <body>, where a <td>
    // is dropped: a table filled through innerHTML came out as loose text.
    auto doc = HtmlParser::parse(
        "<table id=table></table><table><tbody id=body></tbody></table><table><tr id=row></tr></table>"
        "<select id=select></select><textarea id=text></textarea><form><div id=inform></div></form>"
        "<svg id=svg></svg><math><annotation-xml id=html encoding=\"text/html\"></annotation-xml>"
        "<annotation-xml id=xml></annotation-xml></math><template id=template></template><div id=div></div>");
    // The outline of what the element holds after the assignment.
    auto set = [&](const char* id, const std::string& html) {
        auto element = doc->descendant_with_id(id);
        assert(element);
        element->set_inner_html(html);
        return outline(dump_tree_webplatform(*element));
    };

    assert(set("table", "<tr><td>1</td><td>2</td></tr>") == "tbody(tr(td(\"1\") td(\"2\")))");
    assert(set("body", "<tr><td>a<tr><td>b") == "tr(td(\"a\")) tr(td(\"b\"))");
    assert(set("row", "<td>x<th>y") == "td(\"x\") th(\"y\")");
    assert(set("select", "<option>a<option>b") == "option(\"a\") option(\"b\")");
    // In a <div> the same cell is still only its text.
    assert(set("div", "<td>cell</td><p>para") == "\"cell\" p(\"para\")");
    // The content of a <textarea> is text, tags and all.
    assert(set("text", "<b>x</b> &amp; y") == "\"<b>x</b> & y\"");
    // Inside a form a <form> is ignored, as it is in a document.
    assert(set("inform", "<form><input name=q></form>after") == "input[name=\"q\"] \"after\"");
    // In an <svg> the elements are SVG elements, a start tag may close
    // itself, and the content of a <foreignObject> is HTML again.
    assert(set("svg", "<circle r=4 /><g><path/></g><foreignObject><p>x</p></foreignObject>") ==
           "svg:circle[r=\"4\"] svg:g(svg:path) svg:foreignObject(p(\"x\"))");
    // A MathML <annotation-xml> holds HTML when its encoding says so.
    assert(set("html", "<foo>x") == "foo(\"x\")");
    assert(set("xml", "<foo>x") == "math:foo(\"x\")");
    // A <template> takes rows without a table around them.
    assert(set("template", "<tr><td>x") == "tr(td(\"x\"))");

    // An element that is in no document is parsed for all the same.
    auto row = std::make_shared<Element>("tr");
    row->set_inner_html("<td>x");
    assert(outline(dump_tree_webplatform(*row)) == "td(\"x\")");

    // Assigning again replaces what was there.
    assert(set("row", "<td>z") == "td(\"z\")");
    assert(set("row", "") == "");
    assert(doc->descendant_with_id("row")->children().empty());

    // The form above is found from any depth, and whatever the case of the
    // tag in the markup.
    auto deep = HtmlParser::parse("<form><div><p><span id=inner></span></p></div></form><span id=outer></span>");
    auto inner = deep->descendant_with_id("inner");
    inner->set_inner_html("<FORM><input></FORM>x");
    assert(outline(dump_tree_webplatform(*inner)) == "input \"x\"");
    auto outer = deep->descendant_with_id("outer");
    outer->set_inner_html("<FORM><input></FORM>x");
    assert(outline(dump_tree_webplatform(*outer)) == "form(input) \"x\"");

    // What an assignment costs does not grow with how deep the element is.
    // Looking for a form above it at every assignment made this loop cost
    // the square of its length: 44 seconds, where it takes a fifth of one.
    // (Markup with a form in it does make the walk to the root.)
    {
        auto top = std::make_shared<Element>("div");
        std::shared_ptr<Element> at = top;
        const auto started = std::chrono::steady_clock::now();
        for (int level = 0; level < 30000; ++level) {
            at->set_inner_html("<div><div></div></div>");
            at = std::static_pointer_cast<Element>(at->children().front()->children().front());
        }
        const auto took = std::chrono::steady_clock::now() - started;
        assert(took < std::chrono::seconds(5));
        std::size_t depth = 0;
        for (std::shared_ptr<Node> node = at; node; node = node->parent()) ++depth;
        assert(depth == 60001);
    }

    // Copying one element's markup into another gives the same tree. The
    // markup read back has <br /> and <img ... /> in it, and the slash used
    // to close the paragraph and the list item around them.
    auto copy = HtmlParser::parse(
        "<div id=from><p>a<br>b</p><ul><li>one <img src=i> two<li>three</ul></div><div id=to></div>");
    auto from = copy->descendant_with_id("from");
    auto to = copy->descendant_with_id("to");
    to->set_inner_html(from->inner_html());
    assert(to->inner_html() == from->inner_html());
    assert(outline(dump_tree_webplatform(*to)) ==
           "p(\"a\" br \"b\") ul(li(\"one \" img[src=\"i\"] \" two\") li(\"three\"))");
}

TEST(css_values_survive_hostile_nesting) {
    using namespace litebird;

    // `width: calc(1px` is all it took: the length parser handed the text to
    // the calc() evaluator, which handed what it could not read back to the
    // length parser, forever. Parentheses and signs inside calc() recursed
    // once each, and every var( that is never closed made the resolver read
    // the rest of the value again.
    StyleResolver resolver;
    auto width_of = [&](const std::string& declarations) {
        auto doc = HtmlParser::parse("<div id=\"t\" style=\"" + declarations + "\"></div>");
        return resolver.resolve_style(doc->get_element_by_id("t")).width;
    };
    auto repeat = [](const std::string& piece, int times) {
        std::string out;
        for (int i = 0; i < times; ++i) out += piece;
        return out;
    };
    const float automatic = width_of("color: red");

    // A calc( that is not closed is closed by the end of the value, as every
    // open bracket in CSS is.
    assert(width_of("width: calc(100px") == 100.0f);
    assert(width_of("width: calc(100px + 20px") == 120.0f);
    assert(width_of("width: calc((100px + 20px") == 120.0f);

    // calc() inside calc() is a parenthesis. (It used to evaluate to 0.)
    assert(width_of("width: calc(calc(50px))") == 50.0f);
    assert(width_of("width: calc(10px + calc(20px * 2) + (5px + 5px))") == 60.0f);

    // Signs are read in a loop.
    assert(width_of("width: calc(--60px)") == 60.0f);
    assert(width_of("width: calc(100px - -+-60px)") == 40.0f);
    assert(width_of("width: calc(" + std::string(200000, '-') + "7px)") == 7.0f);

    // 100 levels of nesting are an expression; more is not one, and the
    // declaration is dropped. The number is Blink's.
    assert(width_of("width: calc(" + repeat("(", 100) + "50px" + repeat(")", 100) + ")") == 50.0f);
    assert(width_of("width: calc(" + repeat("(", 101) + "50px" + repeat(")", 101) + ")") == automatic);
    assert(width_of("width: " + repeat("calc(", 101) + "50px" + repeat(")", 101)) == 50.0f);
    assert(width_of("width: " + repeat("calc(", 102) + "50px" + repeat(")", 102)) == automatic);

    // Far past the limit, closed or not: an answer, not a crash.
    assert(width_of("width: calc(" + repeat("(", 200000) + "1px" + repeat(")", 200000) + ")") == automatic);
    assert(width_of("width: " + repeat("calc(", 200000) + "1px" + repeat(")", 200000)) == automatic);
    assert(width_of("width: " + repeat("calc(", 200000) + "1px") == automatic);

    // var(): a fallback, a fallback in a fallback, and a var( that is never
    // closed, which is not a var() at all.
    assert(width_of("width: var(--none, 33px)") == 33.0f);
    assert(width_of("width: var(--none, var(--none-either, 34px))") == 34.0f);
    assert(width_of("width: var( --none ,   35px  )") == 35.0f);
    assert(width_of("width: var(--none, 33px") == automatic);
    // 200,000 of them, never closed. This took minutes.
    assert(width_of("width: " + repeat("var(--x, ", 200000) + "33px") == automatic);
    assert(width_of("width: " + repeat("var(--x, ", 200000) + "33px" + repeat(")", 200000)) == automatic);
}

TEST(canvas_and_svg_bitmaps_are_bounded) {
    using namespace litebird;

    // Every <canvas> and every <svg> is drawn into a bitmap as large as the
    // element says it is, and nothing bounded what it could say:
    // <svg width="1e9" height="1e9"> ended the process with an uncaught
    // std::length_error, width="100000" with std::bad_alloc, and 20,000
    // <svg> of the default size asked for 3.6 GB a little at a time.
    const std::int64_t before = RasterBudget::live_pixels();

    // One bitmap that is too large: the canvas keeps the size it was given,
    // has one pixel behind it, and accepts every drawing call.
    {
        CanvasRenderingContext2D canvas(100000, 100000);
        assert(!canvas.has_backing_store());
        assert(canvas.width() == 100000 && canvas.height() == 100000);
        assert(canvas.image_data()->width == 1 && canvas.image_data()->height == 1);
        assert(canvas.image_data()->pixels.size() == 1);
        assert(RasterBudget::live_pixels() == before);
        canvas.fill_style = Color(255, 0, 0);
        canvas.fill_rect(0.0f, 0.0f, 50000.0f, 50000.0f);
        canvas.clear_rect(10.0f, 10.0f, 99999.0f, 99999.0f);
        canvas.begin_path();
        canvas.move_to(1.0f, 1.0f);
        canvas.line_to(90000.0f, 90000.0f);
        canvas.line_to(1.0f, 90000.0f);
        canvas.close_path();
        canvas.fill();
        canvas.stroke();

        // Given a size it can have, it is a canvas again.
        canvas.resize(20, 20);
        assert(canvas.has_backing_store());
        assert(canvas.image_data()->width == 20 && canvas.image_data()->pixels.size() == 400);
        assert(RasterBudget::live_pixels() == before + 400);
        canvas.fill_rect(0.0f, 0.0f, 10.0f, 10.0f);
        assert(canvas.image_data()->get_pixel(5, 5) == Color(255, 0, 0).to_argb());
        assert(canvas.image_data()->get_pixel(15, 15) == 0);
    }
    assert(RasterBudget::live_pixels() == before);      // and its share went back

    // The limits for one bitmap: a side, and the area.
    assert(RasterBudget::allows(RasterBudget::kMaxSide, 1));
    assert(!RasterBudget::allows(RasterBudget::kMaxSide + 1, 1));
    assert(RasterBudget::allows(4096, 4096));
    assert(!RasterBudget::allows(4097, 4096));
    assert(!RasterBudget::allows(INT_MAX, INT_MAX));
    assert(!RasterBudget::allows(0, 10) && !RasterBudget::allows(10, -1));
    assert(!CanvasRenderingContext2D(INT_MAX, INT_MAX).has_backing_store());
    assert(!CanvasRenderingContext2D(RasterBudget::kMaxSide + 1, 1).has_backing_store());
    assert(CanvasRenderingContext2D(RasterBudget::kMaxSide, 1).has_backing_store());

    // The limit for all of them together. With the budget used up, the next
    // canvas and the next <svg> are blank; when it is given back they work.
    {
        RasterReservation most, rest;
        assert(most.resize(RasterBudget::kMaxLivePixels - before - 100));
        assert(!rest.resize(101));
        assert(rest.pixels() == 0);
        assert(rest.resize(100));
        assert(RasterBudget::live_pixels() == RasterBudget::kMaxLivePixels);

        assert(!CanvasRenderingContext2D(300, 150).has_backing_store());
        auto svg = std::make_shared<Element>("svg");
        assert(SvgRenderer::render(svg, 40.0f, 30.0f) == nullptr);

        assert(rest.resize(0));
        assert(!CanvasRenderingContext2D(11, 10).has_backing_store());
        assert(CanvasRenderingContext2D(10, 10).has_backing_store());
    }
    assert(RasterBudget::live_pixels() == before);

    // A page. Each element keeps its box; the ones too large to draw have no
    // image, the small one has, and layout and painting go on.
    {
        auto doc = HtmlParser::parse(
            "<!doctype html>"
            "<svg id=\"vast\" width=\"1e9\" height=\"1e9\"><rect width=\"5\" height=\"5\"/></svg>"
            "<svg id=\"wide\" width=\"100000\" height=\"100000\"></svg>"
            "<svg id=\"icon\" width=\"40\" height=\"30\"><rect width=\"5\" height=\"5\"/></svg>"
            "<canvas id=\"sheet\" width=\"100000\" height=\"100000\"></canvas>"
            "<p id=\"after\">after</p>");
        StyleResolver resolver;
        auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
        assert(root != nullptr);
        auto vast = find_box_by_id(root, "vast");
        auto wide = find_box_by_id(root, "wide");
        auto icon = find_box_by_id(root, "icon");
        auto sheet = find_box_by_id(root, "sheet");
        assert(vast && vast->image == nullptr && vast->style.width == 1e9f);
        assert(wide && wide->image == nullptr && wide->style.width == 100000.0f);
        assert(icon && icon->image != nullptr && icon->image->width == 40 && icon->image->height == 30);
        assert(sheet && sheet->style.width == 100000.0f);
        assert(sheet->image != nullptr && sheet->image->pixels.size() == 1);
        assert(find_box_by_id(root, "after") != nullptr);
        DisplayList::build(root);
    }
    assert(RasterBudget::live_pixels() == before);
}

TEST(inline_children_know_their_lines_in_a_paragraph_of_any_length) {
    using namespace litebird;

    // After line breaking, every text child is told which of its words ended
    // up on which line, and every image where it was put. What each child is
    // told must be what the line boxes say.
    auto check = [](const std::string& html, float width) {
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver;
        auto root = LayoutTree::build(doc, resolver, width, 600.0f);
        assert(root != nullptr);

        size_t contexts = 0;
        size_t text_children = 0;
        size_t text_on_several_lines = 0;
        size_t images = 0;
        std::vector<std::shared_ptr<LayoutBox>> pending{root};
        while (!pending.empty()) {
            auto box = pending.back();
            pending.pop_back();
            for (const auto& child : box->children) pending.push_back(child);
            if (box->line_boxes.empty()) continue;
            ++contexts;
            for (const auto& child : box->children) {
                if (child->box_type == BoxType::TextNode) {
                    std::vector<std::string> expected;
                    for (const auto& line : box->line_boxes) {
                        std::string on_this_line;
                        for (const auto& frag : line.fragments) {
                            if (frag.box == child) on_this_line += frag.text;
                        }
                        if (!on_this_line.empty()) expected.push_back(on_this_line);
                    }
                    if (expected.empty() && !child->text.empty()) expected.push_back(child->text);
                    assert(child->lines == expected);
                    ++text_children;
                    if (expected.size() > 1) ++text_on_several_lines;
                } else if (child->box_type == BoxType::ImageNode) {
                    bool found = false;
                    Rect expected;
                    for (const auto& line : box->line_boxes) {
                        for (const auto& frag : line.fragments) {
                            if (frag.box != child) continue;
                            expected = frag.rect;
                            found = true;
                            break;
                        }
                    }
                    assert(found);
                    assert(child->dimensions.content.x == expected.x);
                    assert(child->dimensions.content.y == expected.y);
                    assert(child->dimensions.content.width == expected.width);
                    assert(child->dimensions.content.height == expected.height);
                    ++images;
                }
            }
        }
        return std::array<size_t, 4>{contexts, text_children, text_on_several_lines, images};
    };

    {
        // Text that wraps over several lines on both sides of an image, and
        // inline elements whose text belongs to no direct child.
        const auto seen = check(R"(<div style="width: 120px; font-size: 10px;">one two three four five six seven eight nine ten<img width="30" height="20">eleven twelve <b>bold words inside</b> thirteen fourteen fifteen sixteen<br>after the break<img width="100" height="10"><img width="100" height="10"></div>)", 400.0f);
        assert(seen[0] >= 1);
        assert(seen[1] >= 3);
        assert(seen[2] >= 2);      // some text child is on more than one line
        assert(seen[3] == 3);
    }
    {
        // Nothing but images, more than one line of them.
        const auto seen = check(R"(<p style="width: 100px">)" + []{ std::string images; for (int i = 0; i < 40; ++i) images += "<img width=\"30\" height=\"10\">"; return images; }(), 400.0f);
        assert(seen[3] == 40);
    }

    // A long paragraph. Each child used to look for itself in every fragment
    // of every line, as the check above does, which made the cost the square
    // of the length: layout of this one took a minute, and now takes about as
    // long as parsing it. No time is asserted; a test that takes a minute is
    // the signal. (So the check here is one pass too: every child was told
    // something, and every image was put on a line of the paragraph.)
    {
        std::string html = "<!doctype html><p id=\"long\">";
        const int runs = 40000;
        for (int i = 0; i < runs; ++i) html += "word <b>bold</b> <img width=\"10\" height=\"10\">x<br>";
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver;
        auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
        auto paragraph = find_box_by_id(root, "long");
        assert(paragraph != nullptr);
        assert(paragraph->line_boxes.size() > 100);

        size_t fragments = 0;
        for (const auto& line : paragraph->line_boxes) fragments += line.fragments.size();
        assert(fragments >= static_cast<size_t>(runs) * 4);    // word, bold, the image, x

        const float top = paragraph->dimensions.content.y;
        const float bottom = top + paragraph->dimensions.content.height;
        size_t texts = 0;
        size_t images = 0;
        for (const auto& child : paragraph->children) {
            if (child->box_type == BoxType::TextNode) {
                assert(!child->lines.empty());
                ++texts;
            } else if (child->box_type == BoxType::ImageNode) {
                assert(child->dimensions.content.width == 10.0f);
                assert(child->dimensions.content.y >= top && child->dimensions.content.y < bottom);
                ++images;
            }
        }
        assert(texts >= static_cast<size_t>(runs));
        assert(images == static_cast<size_t>(runs));
    }
}

TEST(many_sized_floats_pack_without_escaping_the_box) {
    using namespace litebird;

    // Finding 28 measured 46 s for 10,000 floats of 100 by 10 pixels in an
    // 800-pixel box, and placing every float past the first 1,024 (row 128)
    // wherever the 512-step slide gave up, running out of the box to the
    // right. The placement now sweeps the band edges: the float lands on the
    // exact bottom edge of whatever is in the way, or at the top of the box
    // if it is wider than the containing block.
    std::string html = R"(<body style="margin:0"><div id="box" style="width:800px">)";
    for (int i = 0; i < 1056; ++i) {
        html += "<div id=\"f" + std::to_string(i) + "\" style=\"float:left; width:100px; height:10px\"></div>";
    }
    html += "</div></body>";

    auto doc = HtmlParser::parse(html);
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 800.0f, 600.0f);
    auto box = find_box_by_id(root, "box");
    assert(box);
    for (const auto& child : box->children) {
        const float x = child->dimensions.content.x - box->dimensions.content.x;
        assert(x >= 0.0f && x + 100.0f <= 800.0f);
    }
    // Floats past row 128 used to be placed in one row that ran out of the
    // box to the right. Rows of eight 100px floats still tile exactly:
    // row r sits at y = top + r * 10.
    for (int row = 128; row < 132; ++row) {
        for (int k = 0; k < 8; ++k) {
            auto f = find_box_by_id(root, "f" + std::to_string(row * 8 + k));
            assert(f);
            assert(f->dimensions.content.y == box->dimensions.content.y + row * 10.0f);
            assert(f->dimensions.content.x == box->dimensions.content.x + k * 100.0f);
        }
    }
    // A float wider than the containing block does not spin: it is placed and
    // overflows to the left, the way CSS 2.1 9.5.1 puts it.
    auto wide_doc = HtmlParser::parse(R"(<body style="margin:0"><div id="widebox" style="width:800px">
        <div id="wide" style="float:left; width:900px; height:10px"></div>
    </div></body>)");
    StyleResolver wide_resolver;
    auto wide_root = LayoutTree::build(wide_doc, wide_resolver, 800.0f, 600.0f);
    auto wide_box = find_box_by_id(wide_root, "widebox");
    auto wide = find_box_by_id(wide_root, "wide");
    assert(wide_box && wide);
    assert(wide->dimensions.content.x == wide_box->dimensions.content.x);
}

TEST(floats_of_no_height_cost_nothing_and_still_count) {
    using namespace litebird;

    // A float with no height covers no line, so nothing ever wraps around it
    // and it is not kept as a band to look through. It is still a float:
    // `clear` goes below it, and a box that contains its floats reaches down
    // to it.
    auto layout = [](const std::string& html) {
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver;
        return LayoutTree::build(doc, resolver, 800.0f, 600.0f);
    };

    {
        // Beside an empty float, the next float and the text start at the edge.
        auto root = layout(R"(<body style="margin:0"><div id="box" style="width:300px; font-size:10px">
            <div style="float:left; width:100px"></div>
            <div id="second" style="float:left; width:50px; height:20px">a</div>
            some text beside the floats</div>)");
        auto box = find_box_by_id(root, "box");
        auto second = find_box_by_id(root, "second");
        assert(box && second);
        assert(second->dimensions.content.x == box->dimensions.content.x);
        assert(box->float_bands.size() == 1);       // the float that has a height
    }

    // A is 200 wide and 50 high on the left, B is 80 wide and 90 high on the
    // right. C is 250 wide and has no height: there is no room for it beside
    // A (20 left over) or beside B alone (220), so it goes below both, at 90.
    const std::string floats = R"(
            <div style="float:left; width:200px; height:50px">A</div>
            <div style="float:right; width:80px; height:90px">B</div>
            <div id="c" style="float:left; width:250px"></div>)";
    {
        // `clear: left` goes below the lowest float on the left, and that is
        // C at 90, not A at 50.
        auto root = layout(R"(<body style="margin:0"><div id="box" style="width:300px; font-size:10px">)" + floats +
                           R"(<div id="cleared" style="clear:left">below</div></div>)");
        auto box = find_box_by_id(root, "box");
        auto c = find_box_by_id(root, "c");
        auto cleared = find_box_by_id(root, "cleared");
        assert(box && c && cleared);
        assert(c->dimensions.content.y == box->dimensions.content.y + 90.0f);
        assert(cleared->dimensions.content.y == box->dimensions.content.y + 90.0f);
        assert(box->float_bands.size() == 2);       // A and B
    }
    {
        // `clear: right` has only B to go below, which also ends at 90; with
        // B 40 high it is 40, and C (on the left, at 50) does not count.
        auto root = layout(R"(<body style="margin:0"><div id="box" style="width:300px; font-size:10px">
            <div style="float:left; width:200px; height:50px">A</div>
            <div style="float:right; width:80px; height:40px">B</div>
            <div id="c" style="float:left; width:250px"></div>
            <div id="cleared" style="clear:right">below</div></div>)");
        auto box = find_box_by_id(root, "box");
        auto c = find_box_by_id(root, "c");
        auto cleared = find_box_by_id(root, "cleared");
        assert(box && c && cleared);
        assert(c->dimensions.content.y == box->dimensions.content.y + 50.0f);
        assert(cleared->dimensions.content.y == box->dimensions.content.y + 40.0f);
    }
    {
        // A box that contains its floats is as high as the lowest of them.
        // The float with no height is placed as high as it fits: flush with
        // the bottom edge of B, at 89.5. (Before the placement swept the
        // band edges it slid in one-pixel steps and landed at 90.)
        auto root = layout(R"(<body style="margin:0"><div id="box" style="width:300px; overflow:hidden">
            <div style="float:left; width:200px; height:50px">A</div>
            <div style="float:right; width:80px; height:89.5px">B</div>
            <div id="c" style="float:left; width:250px"></div></div>)");
        auto box = find_box_by_id(root, "box");
        auto c = find_box_by_id(root, "c");
        assert(box && c);
        assert(c->dimensions.content.y == box->dimensions.content.y + 89.5f);
        assert(box->dimensions.content.height == 89.5f);
        assert(box->float_bands.size() == 2);
    }

    // Many of them. Every float placed looked at every band before it, so
    // 200,000 empty floats in one box took 20 seconds. What is asserted is
    // the reason they no longer do: they leave no band behind.
    {
        std::string html = R"(<body style="margin:0"><div id="box" style="width:300px; font-size:10px">)";
        for (int i = 0; i < 20000; ++i) html += R"(<div style="float:left"></div>)";
        html += R"(<div id="last" style="float:left; width:40px; height:20px">z</div>text<div id="cleared" style="clear:both">below</div></div>)";
        auto root = layout(html);
        auto box = find_box_by_id(root, "box");
        auto last = find_box_by_id(root, "last");
        auto cleared = find_box_by_id(root, "cleared");
        assert(box && last && cleared);
        assert(box->children.size() >= 20002);
        assert(box->float_bands.size() == 1);
        assert(last->dimensions.content.x == box->dimensions.content.x);
        assert(last->dimensions.content.y == box->dimensions.content.y);
        assert(cleared->dimensions.content.y == box->dimensions.content.y + 20.0f);
    }

    // In a paragraph the floats are children of a box that lays out lines.
    // They all start at its top: each goes beside the ones before it on its
    // side, and the first line is narrowed by all of them.
    {
        auto root = layout(R"(<body style="margin:0"><p id="para" style="margin:0; width:300px; font-size:10px">
            <span id="l1" style="float:left; width:40px; height:30px">a</span>
            <span id="l2" style="float:left; width:25px; height:30px">b</span>
            <span id="r1" style="float:right; width:50px; height:30px">c</span>
            <span id="r2" style="float:right; width:35px; height:30px">d</span>
            words that wrap between the floats on both sides of the paragraph</p>)");
        auto para = find_box_by_id(root, "para");
        auto l1 = find_box_by_id(root, "l1");
        auto l2 = find_box_by_id(root, "l2");
        auto r1 = find_box_by_id(root, "r1");
        auto r2 = find_box_by_id(root, "r2");
        assert(para && l1 && l2 && r1 && r2);
        const float left_edge = para->dimensions.content.x;
        assert(l1->dimensions.content.x == left_edge);
        assert(l2->dimensions.content.x == left_edge + 40.0f);
        assert(r1->dimensions.content.x == left_edge + 300.0f - 50.0f);
        assert(r2->dimensions.content.x == left_edge + 300.0f - 50.0f - 35.0f);
        assert(para->float_bands.size() == 4);
        assert(!para->line_boxes.empty());
        assert(para->line_boxes.front().left_intrusion == 65.0f);
        assert(para->line_boxes.front().right_intrusion == 85.0f);
    }

    // And many with no height there.
    {
        std::string html = R"(<body style="margin:0"><p id="para" style="margin:0; width:300px; font-size:10px">)";
        for (int i = 0; i < 10000; ++i) html += R"(<span style="float:left"></span>)";
        html += R"(<span id="last" style="float:left; width:40px; height:30px">z</span>words that wrap beside the float</p>)";
        auto root = layout(html);
        auto para = find_box_by_id(root, "para");
        auto last = find_box_by_id(root, "last");
        assert(para && last);
        assert(para->float_bands.size() == 1);
        assert(last->dimensions.content.x == para->dimensions.content.x);
        assert(!para->line_boxes.empty());
        assert(para->line_boxes.front().left_intrusion == 40.0f);
    }
}

TEST(replacing_children_detaches_the_old_ones) {
    using namespace litebird;

    // Setting textContent used to clear the element's list of children and
    // nothing else. The removed children went on naming the element as their
    // parent: parentNode was wrong, isConnected was wrong, appending the
    // element to one of them was refused as a loop, and custom elements among
    // them never heard that they had left the document.
    {
        auto doc = HtmlParser::parse("<!doctype html><body></body>");
        JsInterpreter js(doc);
        js.eval_ast(R"(
            var el = document.createElement('div'), child = document.createElement('span');
            el.appendChild(child);
            el.textContent = 'x';
            var orphaned = child.parentNode === null;
            var moved = 'ok';
            try { child.appendChild(el); } catch (e) { moved = e.name; }

            var emptied = document.createElement('div');
            emptied.appendChild(document.createElement('i'));
            emptied.textContent = '';
            var emptiedCount = emptied.childNodes.length;

            var gone = 0;
            class Leaf extends HTMLElement { disconnectedCallback() { gone++; } }
            customElements.define('leaf-el', Leaf);
            var host = document.createElement('div');
            document.body.appendChild(host);
            var leaf = document.createElement('leaf-el');
            host.appendChild(leaf);
            host.appendChild(document.createElement('leaf-el'));
            host.textContent = 'bye';
            var leafConnected = leaf.isConnected;
            var hostText = host.textContent + ':' + host.childNodes.length;
        )");
        auto value = [&](const char* name) { return js.global_env()->get(name).to_string(); };
        assert(value("orphaned") == "true");
        assert(value("moved") == "ok");
        assert(value("emptiedCount") == "0");
        assert(value("gone") == "2");
        assert(value("leafConnected") == "false");
        assert(value("hostText") == "bye:1");
    }

    // Emptying a node takes its children in one step. One at a time, each
    // removal moved all the remaining children down a place.
    const int count = 100000;
    auto list = std::make_shared<Element>("ul");
    for (int i = 0; i < count; ++i) list->append_child(std::make_shared<Element>("li"));
    auto first = list->children().front();
    auto last = list->children().back();
    auto removed = list->take_children();
    assert(removed.size() == static_cast<size_t>(count) && list->children().empty());
    assert(removed.front() == first && removed.back() == last);
    assert(first->parent() == nullptr && last->parent() == nullptr);

    // A fragment gives up its children the same way, in order.
    auto fragment = std::make_shared<Node>(NodeType::DocumentFragment);
    for (const auto& item : removed) fragment->append_child(item);
    removed.clear();
    list->append_child(fragment);
    assert(fragment->children().empty());
    assert(list->children().size() == static_cast<size_t>(count));
    assert(list->children().front() == first && list->children().back() == last && first->parent() == list);

    auto more = std::make_shared<Node>(NodeType::DocumentFragment);
    auto one = std::make_shared<Element>("li");
    auto two = std::make_shared<Element>("li");
    more->append_child(one);
    more->append_child(two);
    list->insert_before(more, first);
    assert(more->children().empty());
    assert(list->children()[0] == one && list->children()[1] == two && list->children()[2] == first);

    // A fragment cannot be put inside something it contains, and is left as
    // it was when that is tried.
    auto outer = std::make_shared<Node>(NodeType::DocumentFragment);
    auto inside = std::make_shared<Element>("div");
    outer->append_child(inside);
    inside->append_child(outer);
    assert(outer->children().size() == 1 && inside->children().empty() && inside->parent() == outer);

    // innerHTML replaces the children too, and observers get one record for
    // everything that was removed.
    list->set_inner_html("<li>only</li>");
    assert(list->children().size() == 1 && first->parent() == nullptr && one->parent() == nullptr);

    // What innerHTML parses arrives the same way: the parser hands over all
    // the nodes it made at once, twice (from its root element to a fragment,
    // from the fragment to the caller). One at a time from the front, a
    // string with 100,000 elements in it took 9 seconds to assign.
    {
        auto nodes = HtmlParser::parse_fragment("<i>a</i>text<b>b</b><!--c-->");
        assert(nodes.size() == 4);
        assert(nodes[0]->is_element() && nodes[0]->as_element()->tag_name() == "i");
        assert(nodes[1]->type() == NodeType::Text);
        assert(nodes[2]->is_element() && nodes[2]->as_element()->tag_name() == "b");
        assert(nodes[3]->type() == NodeType::Comment);
        for (const auto& node : nodes) assert(node->parent() == nullptr);

        auto parsed = HtmlParser::parse_fragment_with_context("<td>1<td>2", "tr");
        assert(parsed->type() == NodeType::DocumentFragment);
        assert(parsed->children().size() == 2);
        for (const auto& cell : parsed->children()) {
            assert(cell->as_element()->tag_name() == "td" && cell->parent() == parsed);
        }

        std::string markup;
        const int items = 50000;
        for (int i = 0; i < items; ++i) markup += (i % 2 == 0) ? "<li>even</li>" : "<li>odd</li>";
        list->set_inner_html(markup);
        assert(list->children().size() == static_cast<size_t>(items));
        assert(list->children().front()->text_content() == "even");
        assert(list->children().back()->text_content() == "odd");
        assert(list->children()[items / 2]->parent() == list);
        assert(list->text_content().size() == static_cast<size_t>(items / 2) * 7);
    }
}

// Phase 31: CSS whitespace trimming. Keep this block together: the XML work
// in flight appends its own suites here too, and neither reorders the other.
TEST(css_text_trim_matches_the_character_by_character_model) {
    using namespace litebird;
    const std::string spaces = " \t\n\r\f\v";
    const std::vector<std::string> cases = {
        "", "x", "  x  ", "\t\nx\r\f", "  \t\n\r\f\v", "a b",
        std::string(" \tx\0y\n ", 8), std::string("\x80x\xff", 3)
    };
    for (const auto& input : cases) {
        std::string model = input;
        while (!model.empty() && std::isspace(static_cast<unsigned char>(model.front()))) model.erase(model.begin());
        while (!model.empty() && std::isspace(static_cast<unsigned char>(model.back()))) model.pop_back();
        std::string actual = input;
        css_trim(actual);
        assert(actual == model);
        assert(css_trim_view(input) == model);
    }
    // A view points into the caller's text; trimming an empty string is safe.
    const std::string source = "  value  ";
    const auto view = css_trim_view(source);
    assert(view.data() == source.data() + 2 && view.size() == 5);
    std::string empty;
    css_trim(empty);
    assert(empty.empty());
    // The C locale's VT is preserved as a trimming character. This task does
    // not silently change the parser's whitespace set to the CSS production.
    assert(css_is_space('\v'));
    assert(!css_is_space('\x80') && !css_is_space('\xff'));
    for (char c : spaces) {
        std::string text(1000, c);
        text += "keep";
        text.append(1000, c);
        css_trim(text);
        assert(text == "keep");
    }
}

TEST(css_whitespace_does_not_change_selector_or_value_meaning) {
    using namespace litebird;
    const std::string pad(256, ' ');
    auto padded = [&](std::string text) {
        std::size_t pos = 0;
        while ((pos = text.find("{p}", pos)) != std::string::npos) {
            text.replace(pos, 3, pad);
            pos += pad.size();
        }
        return text;
    };
    auto resolve = [](const std::string& css, const std::string& html, const std::string& id) {
        auto doc = HtmlParser::parse(html);
        StyleResolver resolver;
        resolver.add_stylesheet(CssParser::parse(css));
        auto element = doc->get_element_by_id(id);
        assert(element != nullptr);
        return resolver.resolve_style(element);
    };
    const std::string box = "<div id='box' data-kind='card' data-ready>x</div>";
    // Attribute names, quoted values and presence selectors reach the parser's
    // two trimming paths. The parsed selector, not just the paint, is checked.
    for (const std::string& css : {
        std::string("div[{p}data-kind{p}={p}\"card\"{p}]{color:rgb(255,0,0)}"),
        std::string("div[{p}data-ready{p}]{color:rgb(255,0,0)}")}) {
        auto sheet = CssParser::parse(padded(css));
        assert(sheet.rules.size() == 1);
        const auto& attrs = sheet.rules[0].selectors[0].parts[0].attributes;
        assert(attrs.size() == 1);
        assert(attrs[0].first == (css.find('=') == std::string::npos ? "data-ready" : "data-kind"));
        assert(attrs[0].second == (css.find('=') == std::string::npos ? "" : "card"));
        assert(resolve(padded(css), box, "box").color == Color(255, 0, 0));
    }
    // The old nth matcher copied and lowercased all padding for every child.
    const std::string items = "<ul><li id='a'>a</li><li id='b'>b</li><li id='c'>c</li></ul>";
    const std::string nth = "li:nth-child({p}2n{p}+1{p}){color:rgb(255,0,0)}";
    assert(resolve(padded(nth), items, "a").color == Color(255, 0, 0));
    assert(resolve(padded(nth), items, "b").color != Color(255, 0, 0));
    assert(resolve(padded(nth), items, "c").color == Color(255, 0, 0));
    for (const std::string formula : {"odd", "even", "3n", "-n+2", "+n+1", "0n+2", "2"}) {
        const std::string compact_rule = "li:nth-child(" + formula + "){color:rgb(0,0,255)}";
        const std::string padded_rule = "li:nth-child({p}" + formula + "{p}){color:rgb(0,0,255)}";
        for (const std::string id : {"a", "b", "c"}) {
            assert(resolve(padded(padded_rule), items, id).color == resolve(compact_rule, items, id).color);
        }
    }
    // Keyframe offsets use the parser's trim; invalid all-space offsets do not
    // become a new keyword or crash.
    auto frames = CssParser::parse(padded("@keyframes fade{0%{opacity:1}{p}50%{p}{opacity:0.5}100%{opacity:1}}"));
    assert(frames.keyframes.size() == 1 && frames.keyframes[0].keyframes.size() == 3);
    assert(frames.keyframes[0].keyframes[1].offset == 0.5f);
    // Grid track arguments and repeat arguments are resolved, not merely stored.
    auto grid = resolve(padded("#box{display:grid;grid-template-columns:minmax({p}100px{p},{p}1fr{p})}"), box, "box");
    assert(grid.grid_col_track_defs.size() == 1);
    assert(grid.grid_col_track_defs[0].min_px == 100.0f && grid.grid_col_track_defs[0].max_fr == 1.0f);
    auto repeat = resolve(padded("#box{display:grid;grid-template-columns:repeat({p}auto-fit{p},{p}minmax({p}100px{p},{p}1fr{p}){p})}"), box, "box");
    assert(repeat.grid_auto_fit_columns && repeat.grid_auto_fit_def.min_px == 100.0f);
    // Filter names, arguments and the drop-shadow colour.
    auto filter = resolve(padded("#box{filter:{p}blur({p}5px{p}){p}drop-shadow({p}2px 3px 4px{p}#ff0000{p})}"), box, "box");
    assert(filter.filters.size() == 2);
    assert(filter.filters[0].type == FilterType::Blur && filter.filters[0].blur_radius == 5.0f);
    assert(filter.filters[1].type == FilterType::DropShadow && filter.filters[1].color == Color(255, 0, 0));
    // Shorthand tokens keep their separators; only outer padding is removed.
    auto shorthand = resolve(padded("#box{margin:{p}1px {p}2px {p}3px {p}4px;font:{p}bold {p}20px {p}serif;border:{p}2px solid rgb(0,0,255)}"), box, "box");
    assert(shorthand.margin == EdgeSizes(1, 2, 3, 4));
    assert(shorthand.font_size == 20.0f && shorthand.font_weight == 700);
    assert(shorthand.border_width.top == 2.0f && shorthand.border_color == Color(0, 0, 255));
    auto timing = resolve(padded("#box{animation-duration:{p}250ms{p};transition-duration:{p}500ms{p};grid-area:{p}1{p}/{p}2{p}/{p}3{p}/{p}4{p}}"), box, "box");
    assert(timing.animation_duration == 0.25f && timing.transition_duration == 0.5f);
    assert(timing.grid_row_start == 1 && timing.grid_column_start == 2 && timing.grid_row_end == 3 && timing.grid_column_end == 4);
    const std::string transform = "#box{transform:{p}translate({p}10px{p},{p}20px{p}){p}scale({p}2{p},{p}3{p})}";
    auto moved = resolve(padded(transform), box, "box").transform_matrix;
    auto compact = resolve("#box{transform:translate(10px,20px) scale(2,3)}", box, "box").transform_matrix;
    assert(moved.a == compact.a && moved.d == compact.d && moved.e == compact.e && moved.f == compact.f);
    assert(moved.e == 10.0f && moved.f == 20.0f);
}

TEST(nth_child_formula_parsing) {
    using namespace litebird;
    // The <an+b> micro-syntax ignores whitespace tokens: "2n + 1" and
    // "2n+1" are the same formula, and the signs may be surrounded by spaces.
    // Before the fix the tail "+ 1" was read as 0 (std::stoi rejected the
    // sign-then-space), painting only the even items.
    const std::string items = "<ul><li id='a'>a</li><li id='b'>b</li><li id='c'>c</li></ul>";
    auto matches = [&](const std::string& formula) {
        auto doc = HtmlParser::parse(items);
        StyleResolver resolver;
        resolver.add_stylesheet(CssParser::parse("li:nth-child(" + formula + "){color:rgb(255,0,0)}"));
        std::array<bool, 3> hit{};
        for (int i = 0; i < 3; ++i) {
            const char id[3] = {static_cast<char>('a' + i), '\0', '\0'};
            hit[i] = resolver.resolve_style(doc->get_element_by_id(id)).color == Color(255, 0, 0);
        }
        return hit;
    };
    const std::array<bool, 3> none{false, false, false};
    const auto expect = [&](const std::string& formula, bool a, bool b, bool c) {
        const auto hit = matches(formula);
        assert(hit[0] == a);
        assert(hit[1] == b);
        assert(hit[2] == c);
    };
    // The reported bug and its neighbours.
    expect("2n + 1", true, false, true);
    expect("2n+ 1", true, false, true);
    expect("2n +1", true, false, true);
    expect("2n\t+\n1", true, false, true);
    expect("2n\f+\r1", true, false, true);
    expect("2N+1", true, false, true);
    expect("+ 2n + 1", true, false, true);
    expect("- n + 2", true, true, false);
    expect("+ n + 1", true, true, true);
    expect("n - 1", true, true, true);
    expect("-n+2", true, true, false);
    expect("-2n+5", true, false, true);
    expect("3n-2", true, false, false);
    expect("0n+2", false, true, false);
    expect("0n+5", false, false, false);
    expect("odd", true, false, true);
    expect("even", false, true, false);
    expect("2", false, true, false);
    expect("+2", false, true, false);
    expect("-2", false, false, false);
    expect("N", true, true, true);
    // Invalid formulas match nothing and never crash.
    for (const std::string& formula : {"2n+a", "n+x", "2nn", "+", "-", "1.5", "++1", "n++1", "1+1", "2n+3n",
                                       "2++1", "--1", "n n", "", "   ", "n+", "2n-", "5n"}) {
        if (formula == "5n") {
            expect("5n", false, false, false);  // a=5: matches only idx 5, which is not in three items
            continue;
        }
        const auto hit = matches(formula);
        assert(hit == none);
    }
}

TEST(css_long_whitespace_runs_keep_their_meaning) {
    using namespace litebird;
    // A size the old character-by-character loops took seconds to pass. The
    // timing guard lives in tools/bench/bench_css_whitespace.py and CI; this
    // suite checks the answer and runs under the sanitizers too.
    const std::string pad(300000, ' ');
    auto doc = HtmlParser::parse("<ul><li id='a'>a</li><li id='b'>b</li></ul>");
    StyleResolver resolver;
    resolver.add_stylesheet(CssParser::parse("li:nth-child(" + pad + "odd" + pad + "){color:rgb(255,0,0)}"));
    assert(resolver.resolve_style(doc->get_element_by_id("a")).color == Color(255, 0, 0));
    assert(resolver.resolve_style(doc->get_element_by_id("b")).color != Color(255, 0, 0));
    auto attr = CssParser::parse("div[" + pad + "data-x" + pad + "=" + pad + "\"y\"" + pad + "]{color:rgb(0,0,255)}");
    assert(attr.rules.size() == 1);
    assert(attr.rules[0].selectors[0].parts[0].attributes.at(0) == std::make_pair(std::string("data-x"), std::string("y")));
    StyleResolver values;
    values.add_stylesheet(CssParser::parse("#x{grid-area:" + pad + "1" + pad + "/" + pad + "2" + pad + "}"));
    auto xdoc = HtmlParser::parse("<div id='x'></div>");
    auto style = values.resolve_style(xdoc->get_element_by_id("x"));
    assert(style.grid_row_start == 1 && style.grid_column_start == 2);
}

// --- Phase 33: page resource loads are origin-aware -------------------------

TEST(resource_loader_page_requests_are_origin_aware) {
    using namespace litebird;

    const std::string secret_path = "/tmp/litebird-fetch-secret-" + std::to_string(static_cast<long>(::getpid())) + ".txt";
    const std::string marker = "LOCAL_FILE_MARKER_37";
    {
        std::ofstream secret(secret_path);
        secret << marker;
    }
    const std::string secret_name = secret_path.substr(secret_path.rfind('/') + 1);
    const std::string secret_url = file_url_from_path(secret_path);

    ResourceRequestContext web;
    web.document_url = "https://example.test/app/page.html";
    ResourceRequestContext file;
    file.document_url = "file:///tmp/litebird-fetch-page.html";
    ResourceRequestContext opaque;
    opaque.document_url = "about:blank";
    ResourceRequestContext empty;

    // A web page must not read a local path, a file: URL, or a file: URL
    // smuggled in through <base href>. The absolute path resolves against
    // the https document, so it never reaches the filesystem either.
    for (const std::string& request : std::array<std::string, 5>{secret_path, secret_url, "ftp://example.test/x", "blob:https://example.test/1", "javascript:alert(1)"}) {
        auto resp = ResourceLoader::load_for_page(request, web);
        assert(!resp.success);
        assert(resp.body.empty());
        assert(resp.body.find(marker) == std::string::npos);
    }
    ResourceRequestContext base_smuggled = web;
    base_smuggled.base_url = "file:///tmp/";
    auto smuggled = ResourceLoader::load_for_page(secret_name, base_smuggled);
    assert(!smuggled.success);
    assert(smuggled.body.empty());

    // Opaque and missing contexts are data: only; they do not become local
    // file readers just because the URL has no scheme.
    for (const auto* context : {&opaque, &empty}) {
        auto resp = ResourceLoader::load_for_page(secret_path, *context);
        assert(!resp.success);
        auto data = ResourceLoader::load_for_page("data:text/plain,ok", *context);
        assert(data.success);
        assert(data.body == "ok");
    }

    // A document the embedder loaded from file: keeps local-file access, and
    // relative references resolve against that document rather than the
    // process working directory.
    auto local = ResourceLoader::load_for_page(secret_name, file);
    assert(local.success);
    assert(local.body == marker);
    assert(local.url == secret_url);
    auto local_absolute = ResourceLoader::load_for_page(secret_url, file);
    assert(local_absolute.success);
    assert(local_absolute.body == marker);

    // data: is self-contained in every context.
    auto data = ResourceLoader::load_for_page("data:text/plain,ok", web);
    assert(data.success);
    assert(data.body == "ok");

    // The embedder's trusted navigation path still opens local files for the
    // CLI/TUI, but it is a different entry point from page requests.
    auto navigation = ResourceLoader::load_navigation(secret_path);
    assert(navigation.success);
    assert(navigation.body == marker);
    auto navigation_url = ResourceLoader::load_navigation(secret_url);
    assert(navigation_url.success);
    assert(navigation_url.body == marker);
    auto bad_navigation = ResourceLoader::load_navigation("javascript:alert(1)");
    assert(!bad_navigation.success);

    std::remove(secret_path.c_str());
}

TEST(fetch_and_workers_use_the_document_origin) {
    using namespace litebird;

    const std::string suffix = std::to_string(static_cast<long>(::getpid()));
    const std::string secret_path = "/tmp/litebird-fetch-secret-" + suffix + ".txt";
    const std::string worker_path = "/tmp/litebird-fetch-worker-" + suffix + ".js";
    const std::string marker = "LOCAL_FILE_MARKER_37";
    {
        std::ofstream secret(secret_path);
        secret << marker;
        std::ofstream worker(worker_path);
        worker << "postMessage('worker-file-ok');";
    }
    const std::string secret_name = secret_path.substr(secret_path.rfind('/') + 1);
    const std::string worker_name = worker_path.substr(worker_path.rfind('/') + 1);

    auto wait_for = [](JsInterpreter& js, const std::string& expression) {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(4)) {
            EventLoop::default_loop().tick();
            EventLoop::default_loop().run_microtasks();
            if (js.eval(expression).is_truthy()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return js.eval(expression).is_truthy();
    };

    // A web document: fetch and both worker entry points refuse the file.
    auto web_doc = std::make_shared<Document>();
    web_doc->set_url("https://example.test/app/page.html");
    JsInterpreter web(web_doc);
    web.load_page("<!DOCTYPE html><body></body>");
    web.eval(
        "var results = {}, workerErrors = {}, workerMessages = {};"
        "function f(name, url) { return fetch(url).then(function (r) { return r.text().then(function (t) { results[name] = r.status + ':' + t; }); }, function (e) { results[name] = 'ERR:' + e; }); }"
        "Promise.all([f('path', '" + secret_path + "'), f('file', 'file://" + secret_path + "'), f('data', 'data:text/plain,ok')]).then(function () { results.done = true; });"
        "var w = new Worker('" + secret_path + "'); w.onerror = function (e) { workerErrors.load = e.message; };"
        "var w2 = new Worker('data:text/javascript,fetch(\"" + secret_path + "\").then(function (r) { return r.text(); }).then(function (t) { postMessage(t); }, function (e) { postMessage(\"ERR:\" + e); });');"
        "w2.onmessage = function (e) { workerMessages.fetch = String(e.data); };");
    assert(wait_for(web, "!!(results.done && workerErrors.load && workerMessages.fetch)"));
    const std::string path_result = web.eval("results.path").to_string();
    const std::string file_result = web.eval("results.file").to_string();
    const std::string data_result = web.eval("results.data").to_string();
    const std::string worker_load_error = web.eval("workerErrors.load").to_string();
    const std::string worker_fetch_result = web.eval("workerMessages.fetch").to_string();
    assert(path_result.rfind("ERR:", 0) == 0);
    assert(file_result.rfind("ERR:", 0) == 0);
    assert(data_result == "200:ok");
    assert(!worker_load_error.empty());
    assert(worker_fetch_result.rfind("ERR:", 0) == 0);
    assert(path_result.find(marker) == std::string::npos);
    assert(file_result.find(marker) == std::string::npos);
    assert(worker_fetch_result.find(marker) == std::string::npos);
    web.eval("w.terminate(); w2.terminate();");

    // A local CLI/TUI document keeps its local-file capability, including a
    // relative worker script.
    auto file_doc = std::make_shared<Document>();
    file_doc->set_url("file:///tmp/litebird-fetch-page-" + suffix + ".html");
    JsInterpreter local(file_doc);
    local.load_page("<!DOCTYPE html><body></body>");
    local.eval(
        "var localFetch = '', localWorker = '', localWorkerError = '';"
        "fetch('" + secret_name + "').then(function (r) { return r.text(); }).then(function (t) { localFetch = t; }, function (e) { localFetch = 'ERR:' + e; });"
        "var lw = new Worker('" + worker_name + "'); lw.onmessage = function (e) { localWorker = String(e.data); }; lw.onerror = function (e) { localWorkerError = e.message; };");
    assert(wait_for(local, "!!(localFetch && localWorker)"));
    assert(local.eval("localFetch").to_string() == marker);
    assert(local.eval("localWorker").to_string() == "worker-file-ok");
    assert(local.eval("localWorkerError").to_string().empty());
    local.eval("lw.terminate();");

    std::remove(secret_path.c_str());
    std::remove(worker_path.c_str());
}

TEST(fetch_relative_requests_use_the_document_base_url) {
    using namespace litebird;

    // A loopback server proves that a relative request from a web document
    // goes to the resolved network address, not to a file of the same name.
    const int server = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(server >= 0);
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    assert(bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    assert(listen(server, 1) == 0);
    socklen_t address_size = sizeof(address);
    assert(getsockname(server, reinterpret_cast<sockaddr*>(&address), &address_size) == 0);
    const auto port = ntohs(address.sin_port);
    timeval timeout{};
    timeout.tv_sec = 3;
    setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    std::string request_line;
    std::thread peer([server, &request_line]() {
        const int client = accept(server, nullptr, nullptr);
        if (client >= 0) {
            char buffer[2048];
            const ssize_t count = recv(client, buffer, sizeof(buffer) - 1, 0);
            if (count > 0) {
                buffer[count] = '\0';
                request_line = buffer;
                if (auto end = request_line.find("\r\n"); end != std::string::npos) request_line.resize(end);
            }
            const std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: close\r\n\r\nnet";
            (void)send(client, response.data(), response.size(), MSG_NOSIGNAL);
            close(client);
        }
        close(server);
    });

    ResourceRequestContext context;
    context.document_url = "http://127.0.0.1:" + std::to_string(port) + "/app/page.html";
    context.base_url = "http://127.0.0.1:" + std::to_string(port) + "/sub/dir/";
    auto response = ResourceLoader::load_for_page("../secret.txt", context);
    peer.join();

    assert(response.success);
    assert(response.body == "net");
    assert(response.url == "http://127.0.0.1:" + std::to_string(port) + "/sub/secret.txt");
    assert(request_line.rfind("GET /sub/secret.txt ", 0) == 0);
}

// =============================================================================
// Phase 36: Usable Real-World Browser & Security (Finding 44) Test Suites
// =============================================================================

TEST(deflate_zlib_and_gzip_roundtrip_and_bomb_limits) {
    using namespace litebird;

    // 1. Zlib stored round-trip and Adler-32 integrity check
    const std::string payload = "Litebird Phase 36 DEFLATE + zlib + gzip verification payload! Repeated payload!";
    std::vector<uint8_t> zlib_bytes;
    Deflate::compress_zlib_stored(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), zlib_bytes);
    assert(zlib_bytes.size() > payload.size() + 6);

    std::vector<uint8_t> inflated;
    assert(Deflate::inflate_zlib(zlib_bytes.data(), zlib_bytes.size(), inflated));
    assert(std::string(inflated.begin(), inflated.end()) == payload);

    // Corrupting the Adler-32 trailer must fail verification.
    std::vector<uint8_t> corrupted = zlib_bytes;
    corrupted.back() ^= 0x55u;
    std::vector<uint8_t> bad_out;
    assert(!Deflate::inflate_zlib(corrupted.data(), corrupted.size(), bad_out));

    // 2. Fixed-Huffman (BTYPE=1) and Dynamic-Huffman (BTYPE=2) gzip streams
    const std::vector<uint8_t> fixed_gzip = {
        0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x03,
        0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40, 0x90, 0x5c,
        0x00, 0x3b, 0x7c, 0x8a, 0xdf, 0x12, 0x00, 0x00, 0x00
    };
    std::vector<uint8_t> gz_out;
    assert(Deflate::inflate_gzip(fixed_gzip.data(), fixed_gzip.size(), gz_out));
    assert(std::string(gz_out.begin(), gz_out.end()) == "hello hello hello\n");

    const std::vector<uint8_t> dyn_gzip = {
        0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x03, 0x35, 0x8f, 0xc1, 0x4e, 0xc3, 0x30,
        0x0c, 0x86, 0x5f, 0xc5, 0xeb, 0x79, 0x5a, 0xd5, 0x03, 0x07, 0xa4, 0x2c, 0x07, 0xba, 0xa1, 0x09,
        0x0d, 0x81, 0xe8, 0x2e, 0x1c, 0xbd, 0xc4, 0x59, 0xb3, 0xa5, 0x49, 0x64, 0x87, 0xa1, 0xbe, 0x3d,
        0x11, 0x85, 0x8b, 0x65, 0xf9, 0xff, 0xf5, 0xe9, 0xb3, 0x5a, 0xed, 0xde, 0xfa, 0xd3, 0xe7, 0xfb,
        0x1e, 0xc6, 0x32, 0x05, 0xad, 0xfe, 0x26, 0xa1, 0xd5, 0x2a, 0xf8, 0x78, 0x03, 0xa6, 0xb0, 0x6d,
        0xa4, 0xcc, 0x81, 0x64, 0x24, 0x2a, 0x0d, 0x8c, 0x4c, 0xee, 0xff, 0xb2, 0x31, 0x22, 0x8d, 0x56,
        0x62, 0xd8, 0xe7, 0x02, 0xc2, 0x66, 0xdb, 0x60, 0xce, 0x9b, 0xab, 0x34, 0x60, 0xc9, 0x11, 0x6b,
        0xd5, 0x2e, 0x59, 0x5d, 0x16, 0xe6, 0x39, 0xd9, 0xb9, 0xf2, 0x3b, 0x7d, 0xf4, 0x85, 0xce, 0x9e,
        0x2d, 0x3c, 0x71, 0xfa, 0x16, 0x62, 0xd8, 0xc7, 0x8b, 0x8f, 0x54, 0x7b, 0x9d, 0x56, 0x59, 0xef,
        0xe6, 0x88, 0x93, 0x37, 0x70, 0xf8, 0x72, 0x6e, 0xc2, 0x08, 0x1f, 0xcf, 0x3d, 0x74, 0x8f, 0x0f,
        0x1d, 0xdc, 0x89, 0xbd, 0xf3, 0x06, 0x8b, 0x4f, 0x11, 0xd0, 0x70, 0x12, 0xa9, 0x8e, 0x18, 0xe0,
        0x70, 0x7a, 0x3d, 0xae, 0xa1, 0x1f, 0x86, 0x35, 0x60, 0xb4, 0xf0, 0x82, 0x77, 0x1c, 0x16, 0xaf,
        0x8c, 0x73, 0x48, 0x68, 0x65, 0xa5, 0xda, 0x5c, 0x45, 0x16, 0x85, 0xf6, 0xf7, 0xd3, 0x1f, 0x1b,
        0x99, 0x9e, 0x24, 0xff, 0x00, 0x00, 0x00
    };
    std::vector<uint8_t> dyn_out;
    assert(Deflate::inflate_gzip(dyn_gzip.data(), dyn_gzip.size(), dyn_out));
    assert(std::string(dyn_out.begin(), dyn_out.end()) ==
           "<!DOCTYPE html><html><head><link rel=\"stylesheet\" href=\"styles.css\"><script src=\"app.js\" defer></script></head><body><h1>Litebird Browser Engine</h1><p>Dynamic Huffman RFC 1951 verification across real HTML, CSS, and JavaScript payloads!</p></body></html>");

    // Multi-member concatenated gzip stream decodes both members seamlessly.
    std::vector<uint8_t> multi_gz = fixed_gzip;
    multi_gz.insert(multi_gz.end(), fixed_gzip.begin(), fixed_gzip.end());
    std::vector<uint8_t> multi_out;
    assert(Deflate::inflate_gzip(multi_gz.data(), multi_gz.size(), multi_out));
    assert(std::string(multi_out.begin(), multi_out.end()) == "hello hello hello\nhello hello hello\n");

    // 3. Decompression bomb output cap must fail deterministically before OOM.
    std::vector<uint8_t> large_in(8192, 'A');
    std::vector<uint8_t> large_z;
    Deflate::compress_zlib_stored(large_in.data(), large_in.size(), large_z);
    std::vector<uint8_t> capped_out;
    assert(!Deflate::inflate_zlib(large_z.data(), large_z.size(), capped_out, 1024));
}

TEST(png_and_svg_subresource_decoding_and_roundtrip) {
    using namespace litebird;

    // 1. RGBA PNG lossless encode + decode pixel-exact round-trip
    auto src_img = std::make_shared<ImageData>();
    src_img->width = 16;
    src_img->height = 12;
    src_img->pixels.resize(16 * 12, 0);
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 16; ++x) {
            src_img->set_pixel(x, y, Color(
                static_cast<uint8_t>(x * 15),
                static_cast<uint8_t>(y * 20),
                static_cast<uint8_t>((x + y) * 7),
                static_cast<uint8_t>(120 + (x * 8))
            ).to_argb());
        }
    }

    std::vector<uint8_t> png_bytes = ImageDecoder::encode_png(*src_img);
    assert(png_bytes.size() > 32);
    assert(png_bytes[0] == 0x89 && png_bytes[1] == 'P' && png_bytes[2] == 'N' && png_bytes[3] == 'G');

    auto decoded_png = ImageDecoder::decode_png(png_bytes.data(), png_bytes.size());
    assert(decoded_png != nullptr);
    assert(decoded_png->width == 16 && decoded_png->height == 12);
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 16; ++x) {
            Color a = src_img->get_pixel_color(x, y);
            Color b = decoded_png->get_pixel_color(x, y);
            assert(a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a);
        }
    }

    // 2. SVG byte decoding and data:image/svg+xml URI decoding
    const std::string svg_src =
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="20"><rect x="0" y="0" width="24" height="20" fill="#2563eb"/></svg>)";
    auto svg_img = ImageDecoder::decode_bytes(
        reinterpret_cast<const uint8_t*>(svg_src.data()), svg_src.size(), "image/svg+xml"
    );
    assert(svg_img != nullptr);
    assert(svg_img->width == 24 && svg_img->height == 20);
    Color center = svg_img->get_pixel_color(12, 10);
    assert(center.b > center.r && center.a == 255);

    auto data_svg = ImageDecoder::decode_data_uri(
        "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><rect width='10' height='10' fill='%23ff0000'/></svg>"
    );
    assert(data_svg != nullptr && data_svg->width == 10 && data_svg->height == 10);
    assert(data_svg->get_pixel_color(5, 5).r > 200);

    // 3. Hostile PNG dimensions (50000 x 50000) rejected before allocation
    std::vector<uint8_t> hostile_png = png_bytes;
    // IHDR width at offset 16..19, height at 20..23
    hostile_png[16] = 0x00; hostile_png[17] = 0x00; hostile_png[18] = 0xC3; hostile_png[19] = 0x50;
    hostile_png[20] = 0x00; hostile_png[21] = 0x00; hostile_png[22] = 0xC3; hostile_png[23] = 0x50;
    assert(ImageDecoder::decode_png(hostile_png.data(), hostile_png.size()) == nullptr);
}

TEST(img_subresources_enforce_document_origin_security) {
    using namespace litebird;
    namespace fs = std::filesystem;

    // Create a temporary local PNG file to verify Finding 40 origin isolation.
    const fs::path tmp_dir = fs::temp_directory_path() / "litebird_phase36_f40";
    fs::create_directories(tmp_dir);
    const fs::path secret_png = tmp_dir / "secret.png";

    auto img = ImageDecoder::create_test_pattern(8, 8, Color(12, 200, 45, 255));
    assert(img != nullptr && ImageDecoder::save_png(*img, secret_png.string()));

    const std::string file_uri = "file://" + secret_png.string();

    // 1. A local file: document CAN load a local relative or file: <img>.
    {
        auto doc = HtmlParser::parse("<img id='pic' src='secret.png'>");
        doc->set_url("file://" + (tmp_dir / "index.html").string());
        StyleResolver resolver = DocumentStyles::build_resolver(*doc);
        auto root = LayoutTree::build(doc, resolver, 400.0f, 300.0f);
        assert(root != nullptr);
        auto pic = doc->get_element_by_id("pic");
        assert(pic != nullptr && pic->bounding_box().width == 8.0f && pic->bounding_box().height == 8.0f);
    }

    // 2. An https: web document CANNOT read local files via file://, absolute path, or <base href="file://...">.
    for (const std::string& doc_url : {
             std::string("https://attacker.example/page.html"),
             std::string("http://attacker.example/page.html"),
             std::string("data:text/html,<h1>opaque</h1>"),
             std::string("about:blank")
         }) {
        const std::string html =
            "<head><base href='file://" + tmp_dir.string() + "/'></head>"
            "<body>"
            "<img id='i1' src='" + file_uri + "'>"
            "<img id='i2' src='" + secret_png.string() + "'>"
            "<img id='i3' src='secret.png'>"
            "</body>";
        auto doc = HtmlParser::parse(html);
        doc->set_url(doc_url);
        StyleResolver resolver = DocumentStyles::build_resolver(*doc);
        auto root = LayoutTree::build(doc, resolver, 400.0f, 300.0f);
        assert(root != nullptr);
        assert(doc->get_element_by_id("i1")->bounding_box().width == 0.0f);
        assert(doc->get_element_by_id("i2")->bounding_box().width == 0.0f);
        assert(doc->get_element_by_id("i3")->bounding_box().width == 0.0f);
    }

    fs::remove_all(tmp_dir);
}

TEST(document_styles_load_external_links_base_href_media_and_imports) {
    using namespace litebird;
    namespace fs = std::filesystem;

    const fs::path tmp_dir = fs::temp_directory_path() / "litebird_phase36_css";
    fs::create_directories(tmp_dir / "assets");

    {
        std::ofstream f(tmp_dir / "assets" / "imported.css");
        f << "@import 'cycle.css';\n";
        f << "#box { width: 210px; height: 45px; }\n";
    }
    {
        std::ofstream f(tmp_dir / "assets" / "cycle.css");
        f << "@import 'imported.css';\n#box { padding-left: 12px; }\n";
    }
    {
        std::ofstream f(tmp_dir / "assets" / "main.css");
        f << "@import 'imported.css';\n#box { height: 70px; background-color: rgb(10, 20, 30); }\n";
    }
    {
        std::ofstream f(tmp_dir / "assets" / "mobile.css");
        f << "#box { width: 99px; }\n";
    }

    const std::string html =
        "<!DOCTYPE html><html><head>"
        "<base href='file://" + (tmp_dir / "assets").string() + "/'>"
        "<style>#box { width: 50px; height: 20px; border: 2px solid black; }</style>"
        "<link rel='stylesheet' href='main.css'>"
        "<link rel='stylesheet' href='mobile.css' media='(max-width: 500px)'>"
        "<style>#box { border-top-width: 6px; }</style>"
        "</head><body style='margin:0'><div id='box'>Styled</div></body></html>";

    auto doc = HtmlParser::parse(html);
    doc->set_url("file://" + (tmp_dir / "index.html").string());

    StyleSheetCache cache;
    StyleResolver desktop = DocumentStyles::build_resolver(*doc, &cache, 1024.0f, 768.0f);
    const std::size_t fetches_after_first = cache.external_fetches;
    assert(fetches_after_first >= 3);

    // Rebuilding with the same cache performs zero redundant stylesheet loads.
    StyleResolver desktop_cached = DocumentStyles::build_resolver(*doc, &cache, 1024.0f, 768.0f);
    assert(cache.external_fetches == fetches_after_first);

    auto box_el = doc->get_element_by_id("box");
    assert(box_el != nullptr);
    ComputedStyle st = desktop_cached.resolve_style(box_el);
    assert(st.width == 210.0f);       // from imported.css (overrides earlier <style>, mobile.css skipped at 1024px)
    assert(st.height == 70.0f);       // main.css overrides imported.css
    assert(st.padding.left == 12.0f); // cycle.css loaded once without infinite recursion
    assert(st.border_width.top == 6.0f); // trailing inline <style> wins in tree order

    // Narrow viewport activates media='(max-width: 500px)'
    StyleResolver mobile = DocumentStyles::build_resolver(*doc, &cache, 420.0f, 768.0f);
    assert(mobile.resolve_style(box_el).width == 99.0f);

    // Web documents cannot load local stylesheets via file://
    auto web_doc = HtmlParser::parse(
        "<link rel='stylesheet' href='file://" + (tmp_dir / "assets" / "main.css").string() + "'>"
        "<div id='box'>Web</div>"
    );
    web_doc->set_url("https://example.org/");
    StyleResolver web_res = DocumentStyles::build_resolver(*web_doc);
    assert(web_res.resolve_style(web_doc->get_element_by_id("box")).height < 0.0f);

    fs::remove_all(tmp_dir);
}

TEST(external_classic_and_deferred_scripts_execute_in_html_order) {
    using namespace litebird;
    namespace fs = std::filesystem;

    const fs::path tmp_dir = fs::temp_directory_path() / "litebird_phase36_scripts";
    fs::create_directories(tmp_dir);

    {
        std::ofstream f(tmp_dir / "sync1.js");
        f << "window.log.push('sync1:' + (document.getElementById('after') !== null) + ':' + document.currentScript.getAttribute('src'));\n";
    }
    {
        std::ofstream f(tmp_dir / "defer1.js");
        f << "window.log.push('defer1:' + document.readyState + ':' + (document.getElementById('after') !== null));\n";
    }
    {
        std::ofstream f(tmp_dir / "defer2.js");
        f << "window.log.push('defer2:' + document.getElementById('after').textContent);\n";
    }

    auto doc = std::make_shared<Document>();
    doc->set_url("file://" + (tmp_dir / "index.html").string());
    JsInterpreter js(doc);
    js.load_page(
        "<!DOCTYPE html><html><head>"
        "<script>window.log = []; document.addEventListener('DOMContentLoaded', () => window.log.push('dcl'));"
        "window.addEventListener('load', () => window.log.push('load'));</script>"
        "<script src='defer1.js' defer></script>"
        "<script src='sync1.js'></script>"
        "<script src='defer2.js' defer></script>"
        "</head><body>"
        "<script>window.log.push('inline-body:' + (document.getElementById('after') !== null));</script>"
        "<div id='after'>ready</div>"
        "</body></html>"
    );
    js.notify_document_loaded();

    assert(js.eval("window.log.join('|')").to_string() ==
           "sync1:false:sync1.js|inline-body:false|defer1:interactive:true|defer2:ready|dcl|load");

    // Cross-origin web page cannot execute a local file:// script.
    auto web_doc = std::make_shared<Document>();
    web_doc->set_url("https://example.org/app.html");
    JsInterpreter web_js(web_doc);
    web_js.load_page(
        "<script>window.ranLocal = false;</script>"
        "<script src='file://" + (tmp_dir / "sync1.js").string() + "'></script>"
    );
    web_js.notify_document_loaded();
    assert(web_js.eval("typeof window.log").to_string() == "undefined");

    fs::remove_all(tmp_dir);
}

TEST(live_dom_geometry_and_layout_invalidation) {
    using namespace litebird;

    auto doc = std::make_shared<Document>();
    doc->set_url("https://app.example/dashboard");
    doc->set_viewport_size(960.0f, 640.0f);

    JsInterpreter js(doc);
    js.load_page(
        "<!DOCTYPE html><html><head><style>"
        "body { margin: 0; }"
        "#panel { width: 200px; height: 80px; padding: 10px; border: 4px solid #333; }"
        "#card-link { text-decoration: none; }"
        "#card-inner { width: 240px; height: 60px; }"
        "</style></head><body>"
        "<div id='panel'>Hello</div>"
        "<a id='card-link' href='/details'><div id='card-inner'>Block inside inline link</div></a>"
        "<p style='margin:0'>Prefix <span id='chip'>inline-span</span> suffix</p>"
        "</body></html>"
    );
    js.notify_document_loaded();

    assert(js.eval("window.innerWidth").to_number() == 960.0);
    assert(js.eval("window.innerHeight").to_number() == 640.0);

    // Initial border-box: 200 + 20 padding + 8 border = 228 x 108; client box: 220 x 100; clientLeft/Top: 4
    assert(js.eval("document.getElementById('panel').offsetWidth").to_number() == 228.0);
    assert(js.eval("document.getElementById('panel').offsetHeight").to_number() == 108.0);
    assert(js.eval("document.getElementById('panel').clientWidth").to_number() == 220.0);
    assert(js.eval("document.getElementById('panel').clientHeight").to_number() == 100.0);
    assert(js.eval("document.getElementById('panel').clientLeft").to_number() == 4.0);
    assert(js.eval("document.getElementById('panel').getBoundingClientRect().width").to_number() == 228.0);

    // Mutating inline style invalidates layout and reflows automatically on next geometry read!
    js.eval("document.getElementById('panel').style.width = '340px';");
    assert(js.eval("document.getElementById('panel').getBoundingClientRect().width").to_number() == 368.0);
    assert(js.eval("document.getElementById('panel').offsetWidth").to_number() == 368.0);

    // Block-in-inline <a><div>...</div></a> reports unioned non-zero geometry and remains clickable.
    assert(js.eval("document.getElementById('card-link').getBoundingClientRect().width").to_number() == 240.0);
    assert(js.eval("document.getElementById('card-link').getBoundingClientRect().height").to_number() == 60.0);

    // Inline <span> inside IFC reports accurate non-zero text fragment bounds.
    assert(js.eval("document.getElementById('chip').getBoundingClientRect().width > 20").is_truthy());
    assert(js.eval("document.getElementById('chip').getClientRects().length === 1").is_truthy());

    // Detached elements report 0x0 geometry.
    js.eval("const p = document.getElementById('panel'); p.remove(); window.removedW = p.offsetWidth;");
    assert(js.eval("window.removedW").to_number() == 0.0);
}

TEST(truetype_font_rasterizer_and_utf8_proportional_metrics) {
    using namespace litebird;

    FontRenderer& fr = FontRenderer::instance();
    // Proportional metrics: narrow glyphs ('iiii') measure substantially narrower than wide glyphs ('WWWW')
    float narrow_w = fr.measure_text("iiii", 18.0f, 400);
    float wide_w = fr.measure_text("WWWW", 18.0f, 400);
    assert(narrow_w > 0.0f);
    if (fr.has_truetype_font()) {
        assert(narrow_w * 1.5f < wide_w);
    }

    // UTF-8 multi-byte text ("Café — Zürich • Δx → ∞") renders anti-aliased pixels cleanly
    auto doc = HtmlParser::parse(
        "<body style='margin:0;background:#ffffff'>"
        "<div style='font-size:20px;font-weight:700;color:#112233;padding:8px'>Café — Zürich • Δx → ∞</div>"
        "</body>"
    );
    StyleResolver resolver;
    auto root = LayoutTree::build(doc, resolver, 360.0f, 80.0f);
    DisplayList dl = DisplayList::build(root);
    BitmapRenderer bmp(360, 80);
    bmp.render(dl);

    int ink_pixels = 0;
    int aa_edge_pixels = 0;
    for (int y = 0; y < 80; ++y) {
        for (int x = 0; x < 360; ++x) {
            Color c = bmp.get_pixel(x, y);
            if (c.r < 250 || c.g < 250 || c.b < 250) {
                ++ink_pixels;
                if (c.r > 25 && c.r < 240) ++aa_edge_pixels;
            }
        }
    }
    assert(ink_pixels > 100);
    if (fr.has_truetype_font()) {
        assert(aa_edge_pixels > 20);
    }
}

TEST(web_browser_server_and_http11_gzip_chunked_cors_and_gui_e2e) {
    using namespace litebird;

    WebBrowserServer server(1024.0f, 680.0f);
    server.set_custom_handler([](const HttpServerRequest& req, HttpServerResponse& resp) -> bool {
        if (req.path == "/test/redirect") {
            resp.status = 302;
            resp.status_text = "Found";
            resp.headers.push_back({"Location", "/test/chunked-gzip"});
            resp.body = "";
            return true;
        }
        if (req.path == "/test/chunked-gzip") {
            const uint8_t gz[] = {
                0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x03,
                0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40, 0x90, 0x5c,
                0x00, 0x3b, 0x7c, 0x8a, 0xdf, 0x12, 0x00, 0x00, 0x00
            };
            resp.status = 200;
            resp.content_type = "text/plain; charset=utf-8";
            resp.headers.push_back({"Content-Encoding", "gzip"});
            resp.chunked = true;
            resp.body.assign(reinterpret_cast<const char*>(gz), sizeof(gz));
            return true;
        }
        if (req.path == "/test/cors-open") {
            resp.status = 200;
            resp.content_type = "application/json";
            resp.headers.push_back({"Access-Control-Allow-Origin", "*"});
            resp.body = "{\"cors\":true}";
            return true;
        }
        if (req.path == "/test/cors-closed") {
            resp.status = 200;
            resp.content_type = "application/json";
            resp.body = "{\"secret\":1}";
            return true;
        }
        return false;
    });

    assert(server.start_background(0, "127.0.0.1"));
    const uint16_t port = server.bound_port();
    assert(port > 0);
    const std::string base = "http://127.0.0.1:" + std::to_string(port);

    // 1. HTTP/1.1 302 redirect -> chunked + gzip transfer decoding
    {
        ResourceResponse r = ResourceLoader::load_navigation(base + "/test/redirect");
        assert(r.success && r.status_code == 200);
        assert(r.url == base + "/test/chunked-gzip");
        assert(r.body == "hello hello hello\n");
    }

    // 2. CORS enforcement on cross-origin XHR & fetch()
    {
        auto doc = std::make_shared<Document>();
        doc->set_url("https://webapp.example/index.html");
        JsInterpreter js(doc);
        js.load_page("<script>window.openRes = ''; window.closedErr = false;</script>");
        js.notify_document_loaded();

        js.eval(
            "const x1 = new XMLHttpRequest();"
            "x1.open('GET', '" + base + "/test/cors-open', false);"
            "x1.send();"
            "window.openRes = x1.status + ':' + x1.responseText;"
            "const x2 = new XMLHttpRequest();"
            "x2.onerror = () => { window.closedErr = true; };"
            "x2.open('GET', '" + base + "/test/cors-closed', false);"
            "try { x2.send(); } catch (e) { window.closedErr = true; }"
        );
        assert(js.eval("window.openRes").to_string() == "200:{\"cors\":true}");
        assert(js.eval("window.closedErr || x2.status === 0").is_truthy());
    }

    // 3. Drive the live `--gui` browser server end-to-end over HTTP/1.1
    {
        ResourceResponse nav_resp = ResourceLoader::load_http(
            base + "/api/navigate",
            "POST",
            {{"Content-Type", "application/json"}},
            "{\"url\":\"" + base + "/demo/index.html\"}"
        );
        assert(nav_resp.success && nav_resp.status_code == 200);
        assert(nav_resp.body.find("Litebird Multi-File Workspace") != std::string::npos);

        // Fetch rendered PNG viewport frame and verify it decodes to a rich 1024x680 bitmap
        ResourceResponse frame_resp = ResourceLoader::load_navigation(base + "/api/frame.png");
        assert(frame_resp.success && frame_resp.status_code == 200);
        assert(frame_resp.content_type.find("image/png") != std::string::npos);
        auto frame_img = ImageDecoder::decode_png(
            reinterpret_cast<const uint8_t*>(frame_resp.body.data()), frame_resp.body.size()
        );
        assert(frame_img != nullptr);
        assert(frame_img->width == 1024 && frame_img->height == 680);

        // Serve the browser chrome HTML shell (uses relative URLs for proxy compatibility)
        ResourceResponse ui_resp = ResourceLoader::load_navigation(base + "/");
        assert(ui_resp.success && ui_resp.body.find("api/frame.png") != std::string::npos);

        // Navigate to details page and test /api/back history restoration
        ResourceResponse nav2 = ResourceLoader::load_http(
            base + "/api/navigate",
            "POST",
            {{"Content-Type", "application/json"}},
            "{\"url\":\"" + base + "/demo/details.html\"}"
        );
        assert(nav2.success && nav2.body.find("canBack\":true") != std::string::npos);

        ResourceResponse back_resp = ResourceLoader::load_http(base + "/api/back", "POST", {}, "{}");
        assert(back_resp.success && back_resp.body.find("/demo/index.html") != std::string::npos);
    }

    server.stop();
}

// ---------------------------------------------------------------------------
// Phase 34: a function as the prototype of a function, and of an object
// ---------------------------------------------------------------------------

TEST(js_a_function_is_the_prototype_of_a_function) {
    using namespace litebird;

    // `Object.getPrototypeOf(Derived)` gave back an object that was not
    // `Base` and could not be called: a prototype link is a pointer to a
    // cell, a function is a value, and nothing led from one to the other.
    JsInterpreter js;
    js.eval_ast(R"(
        class Base { constructor(x) { this.x = x; } static s() { return 'static of ' + this.name; } }
        class Derived extends Base {}
        class Leaf extends Derived {}
        function Plain() {}
        Plain.own = 1;
        function Proto() { return 'called'; }
        var child = Object.create(Proto);
    )");
    auto text = [&](const char* source) { return js.eval(source).to_string(); };

    assert(text("Object.getPrototypeOf(Derived) === Base") == "true");
    assert(text("typeof Object.getPrototypeOf(Derived)") == "function");
    assert(text("Derived.__proto__ === Base && Reflect.getPrototypeOf(Leaf) === Derived") == "true");
    assert(text("new (Object.getPrototypeOf(Derived))(7).x") == "7");
    // Static members are found along the link, through any number of levels,
    // and run with the class they were asked on as `this`.
    assert(text("Leaf.s()") == "static of Leaf");
    assert(text("Leaf.hasOwnProperty('s')") == "false");
    assert(text("Base.isPrototypeOf(Leaf) && !Leaf.isPrototypeOf(Base)") == "true");
    // The six error constructors: what every assert_throws_js of
    // web-platform-tests walks before it looks at anything else.
    assert(text("[TypeError, RangeError, SyntaxError, ReferenceError, EvalError, URIError]"
                ".every(function (C) { return Object.getPrototypeOf(C) === Error; })") == "true");
    assert(text("Object.getPrototypeOf(Error) === Function.prototype") == "true");

    // The same through the engine's own interface.
    JsValue base = js.eval("Base");
    JsValue derived = js.eval("Derived");
    assert(derived.prototype_object() != nullptr);
    assert(derived.prototype_object() == base.as_prototype());
    assert(base.as_prototype() == base.as_prototype());            // one cell, however often it is asked for
    JsValue link = JsValue::make_object(derived.prototype_object());
    assert(link.is_function());
    assert(link.identity() == base.identity());
    assert(link.get_property("name").to_string() == "Base");
    assert(base.as_prototype()->function_value() != nullptr);

    // A function that is nobody's prototype has a cell for its properties
    // and no way back from it: the cell and the function would keep each
    // other alive, and only the collector could take the pair apart.
    JsValue plain = js.eval("Plain");
    assert(plain.property_cell() != nullptr);
    assert(plain.property_cell()->function_value() == nullptr);
    // An object's cell has none either.
    assert(js.eval("({ a: 1 })").property_cell()->function_value() == nullptr);

    // Function.prototype is a function: the prototype of every function is.
    assert(JsIntrinsics::get().function_proto->function_value() != nullptr);
    assert(text("typeof Function.prototype + ':' + typeof Object.getPrototypeOf(Plain) + ':' + typeof Plain.__proto__") ==
           "function:function:function");
    assert(text("Object.getPrototypeOf(Plain) === Function.prototype && Object.getPrototypeOf(Math.max) === Function.prototype") == "true");
    assert(text("String(Function.prototype()) + ':' + Function.prototype.length + ':' + (Function.prototype.name === '')") ==
           "undefined:0:true");
    assert(text("Object.getPrototypeOf(Function.prototype) === Object.prototype") == "true");
    assert(text("Function.prototype.constructor === Function && Plain instanceof Function && Function.prototype.isPrototypeOf(Plain)") ==
           "true");
    // It is not a constructor's prototype property that arrow functions and
    // methods would find: they have none.
    assert(text("typeof (() => 1).prototype + ':' + typeof ({ m() {} }).m.prototype + ':' + typeof Function.prototype.prototype") ==
           "undefined:undefined:undefined");
    assert(text("typeof Object.create(Function.prototype) + ':' + typeof Object.create(Function.prototype).call") ==
           "object:function");

    // A function as the prototype of an object.
    assert(text("Object.getPrototypeOf(child) === Proto") == "true");
    assert(text("Object.getPrototypeOf(child)()") == "called");
    assert(text("typeof child") == "object");
    assert(text("child.name + ':' + child.length + ':' + (child.prototype === Proto.prototype)") == "Proto:0:true");
    assert(text("var o = { __proto__: Proto }; Object.getPrototypeOf(o) === Proto") == "true");
    assert(text("function C() {} C.prototype = Proto; Object.getPrototypeOf(new C()) === Proto") == "true");
    // `__proto__:` in a literal sets a prototype and defines no property, so
    // a function written there is not named after it. It was, on `main` too,
    // and nothing could see it: the function could not be got back.
    assert(text("Object.getPrototypeOf({ __proto__: function () {} }).name === ''") == "true");
    assert(text("({ p: function () {} }).p.name") == "p");

    // Function.prototype is callable now, so `x instanceof Function.prototype`
    // no longer stops at "not callable". With a `prototype` that is there and
    // is not an object, instanceof throws, as it does for any function; with
    // none it answers false (js_instance_of says why it does not throw).
    assert(text("var r; try { r = String([] instanceof Function.prototype); } catch (e) { r = e.name; } r") == "false");
    assert(text("function N() {} N.prototype = 5; var r; try { r = String(({}) instanceof N); } catch (e) { r = e.name; } r") ==
           "TypeError");
    assert(text("N.prototype = 5; 1 instanceof N") == "false");       // a primitive is answered first
    assert(text("N.prototype = {}; new N() instanceof N") == "true");

    // A built-in function gets a cell the first time it needs one, and keeps
    // what it had: its name, its length, its text, and what it does.
    JsValue native = js.eval("Math.max");
    JsObject* native_cell = native.function_cell();
    assert(native_cell != nullptr);
    assert(native.function_cell() == native_cell);
    assert(text("Math.max.name + ':' + Math.max.length + ':' + Math.max(1, 5)") == "max:2:5");
    assert(text("var n = Object.create(Math.max); Object.getPrototypeOf(n) === Math.max") == "true");
    assert(text("Object.getPrototypeOf(n)(2, 9)") == "9");
    assert(text("n.name + ':' + n.length + ':' + typeof n.call") == "max:2:function");
    assert(text("String(Object.getPrototypeOf(n)) === String(Math.max)") == "true");

    // A chain cannot be closed into a loop through functions either.
    assert(text("var r; try { Object.setPrototypeOf(Base, Leaf); r = 'no error'; } catch (e) { r = e.name; } r") ==
           "TypeError");
    assert(text("Reflect.setPrototypeOf(Proto, child)") == "false");
    assert(text("Object.getPrototypeOf(Base) === Function.prototype") == "true");

    // The bytecode VM has an Object of its own with these three functions
    // (a VM that no interpreter gave its globals to). The same answers.
    auto on_vm = [](const char* source) {
        JsInterpreter compiler;
        VM vm;
        return vm.interpret(compiler.compile_to_bytecode(source)).to_string();
    };
    assert(on_vm("function F() { return 7; } let c = Object.create(F); Object.getPrototypeOf(c) === F;") == "true");
    assert(on_vm("function F() { return 7; } let c = Object.create(F); Object.getPrototypeOf(c)();") == "7");
    assert(on_vm("function F() {} function G() {} Object.setPrototypeOf(F, G); Object.getPrototypeOf(F) === G;") == "true");
    assert(on_vm("let o = {}; function G() {} Object.setPrototypeOf(o, G); Object.getPrototypeOf(o) === G;") == "true");
    assert(on_vm("function F() {} Object.getPrototypeOf(F) === null;") == "false");
    // A loop is refused there too.
    assert(on_vm("function F() {} function G() {} Object.setPrototypeOf(F, G); Object.setPrototypeOf(G, F);"
                 "Object.getPrototypeOf(G) === F;") == "false");
}

TEST(js_classes_and_their_parents_are_collected) {
    using namespace litebird;

    // A function that is a prototype and its cell keep each other alive, so
    // only the collector frees them. It has to: every `class B extends A`
    // makes such a pair.
    JsIntrinsics::get();
    // Twice: the suites before this one leave hundreds of thousands of cells
    // behind, and those that something still held a handle to during the
    // first collection are freed by the second (300,309 alive after one,
    // 269 after two, when this was written).
    Heap::default_heap().collect_garbage();
    Heap::default_heap().collect_garbage();
    const size_t permanent_cells = Heap::default_heap().stats().alive_cells;
    {
        JsInterpreter js;
        js.eval_ast(R"(
            function make(i) { class Base { static s() { return i; } } class D extends Base {} return D; }
            var kept = (function () { var secret = 'called'; function Hidden() { return secret; } Hidden.tag = 't'; return Object.create(Hidden); })();
            var instance = (function () { class A { static s() { return 'static of A'; } } class B extends A {} return new B(); })();
        )");
        Heap::default_heap().collect_garbage();
        const size_t before = Heap::default_heap().stats().alive_cells;

        js.eval_ast("var keep = []; for (var i = 0; i < 500; i++) keep.push(make(i));");
        Heap::default_heap().collect_garbage();
        const size_t with_classes = Heap::default_heap().stats().alive_cells;
        // Two classes a pair, each a cell and a prototype object.
        assert(with_classes >= before + 2000);
        assert(js.eval("keep[499].s() + ':' + Object.getPrototypeOf(keep[5]).s()").to_string() == "499:5");

        js.eval_ast("keep = null;");
        Heap::default_heap().collect_garbage();
        const size_t after = Heap::default_heap().stats().alive_cells;
        assert(after <= before + 4);         // the array's cell and such; nothing that grows with 500

        // What is reachable only as a prototype is not collected, and neither
        // is what it closed over.
        Heap::default_heap().collect_garbage();
        assert(js.eval("var H = Object.getPrototypeOf(kept); typeof H + ':' + H() + ':' + kept.tag + ':' + H.name")
                   .to_string() == "function:called:t:Hidden");
        assert(js.eval("Object.getPrototypeOf(instance.constructor).s()").to_string() == "static of A");

        // A prototype that was replaced goes; the one in use stays.
        js.eval_ast(R"(
            function F() {}
            for (var j = 0; j < 500; j++) Object.setPrototypeOf(F, function Temp() {});
            Object.setPrototypeOf(F, function Last() { return 'last'; });
            H = null;
        )");
        Heap::default_heap().collect_garbage();
        assert(Heap::default_heap().stats().alive_cells <= after + 8);
        assert(js.eval("Object.getPrototypeOf(F).name + ':' + Object.getPrototypeOf(F)()").to_string() == "Last:last");
    }
    // With the interpreter gone, so is everything its scripts made.
    Heap::default_heap().collect_garbage();
    assert(Heap::default_heap().stats().alive_cells == permanent_cells);
}

TEST(js_classes_compiled_to_es5_run) {
    using namespace litebird;

    // What a compiler turns `class B extends A` into. Each line of these
    // helpers failed before Phase 34: `Object.defineProperty(B, "prototype",
    // { writable: false })` threw, `_getPrototypeOf(B)` was not `A`, and
    // `Reflect.construct` ignored its third argument.
    JsInterpreter js;
    js.eval_ast(R"(
        function _getPrototypeOf(o) { return Object.getPrototypeOf(o); }
        function _inherits(subClass, superClass) {
            if (typeof superClass !== 'function' && superClass !== null) throw new TypeError('Super expression must either be null or a function');
            subClass.prototype = Object.create(superClass && superClass.prototype, { constructor: { value: subClass, writable: true, configurable: true } });
            Object.defineProperty(subClass, 'prototype', { writable: false });
            if (superClass) Object.setPrototypeOf(subClass, superClass);
        }
        function _createSuper(Derived) {
            return function () {
                var Super = _getPrototypeOf(Derived);
                return Reflect.construct(Super, arguments, _getPrototypeOf(this).constructor);
            };
        }
        var Animal = function Animal(name) { this.name = name; };
        Animal.prototype.speak = function () { return this.name + ' makes a sound'; };
        Animal.create = function (name) { return new this(name); };
        var Dog = (function (_Animal) {
            _inherits(Dog, _Animal);
            var _super = _createSuper(Dog);
            function Dog() { return _super.apply(this, arguments); }
            Dog.prototype.speak = function () { return _Animal.prototype.speak.call(this) + ', woof'; };
            return Dog;
        })(Animal);
        var MyError = (function (_Error) {
            _inherits(MyError, _Error);
            var _super = _createSuper(MyError);
            function MyError() { return _super.apply(this, arguments); }
            return MyError;
        })(Error);

        var __extends = (function () {
            var extendStatics = function (d, b) { extendStatics = Object.setPrototypeOf; return extendStatics(d, b); };
            return function (d, b) { extendStatics(d, b); function __() { this.constructor = d; } d.prototype = (__.prototype = b.prototype, new __()); };
        })();
        var Shape = (function () { function Shape(n) { this.n = n; } Shape.make = function (n) { return new this(n); }; return Shape; })();
        var Circle = (function (_super) { __extends(Circle, _super); function Circle() { return _super !== null && _super.apply(this, arguments) || this; } return Circle; })(Shape);
    )");
    auto text = [&](const char* source) { return js.eval(source).to_string(); };

    assert(text("var d = Dog.create('Rex'); d.speak()") == "Rex makes a sound, woof");
    assert(text("d instanceof Dog && d instanceof Animal && Object.getPrototypeOf(Dog) === Animal") == "true");
    assert(text("var e = new MyError('boom'); (e instanceof MyError) + ':' + (e instanceof Error) + ':' + e.message") ==
           "true:true:boom");
    assert(text("Circle.make(3).n + ':' + (Circle.make(3) instanceof Circle) + ':' + (Object.getPrototypeOf(Circle) === Shape)") ==
           "3:true:true");

    // The prototype property of a constructor: there before it is read,
    // writable and nothing else, and it can be made read-only once.
    assert(text("function F() {} var pd = Object.getOwnPropertyDescriptor(F, 'prototype');"
                "[pd.writable, pd.enumerable, pd.configurable, F.hasOwnProperty('prototype')].join()") ==
           "true,false,false,true");
    assert(text("Object.defineProperty(F, 'prototype', { writable: false }); F.prototype = 5; typeof F.prototype") == "object");
    assert(text("var r; try { Object.defineProperty(F, 'prototype', { writable: true }); r = 'no error'; } catch (x) { r = x.name; } r") ==
           "TypeError");
    assert(text("Reflect.defineProperty(F, 'prototype', { value: 1 })") == "false");
    // An assigned prototype is not enumerable: a loop that copies static
    // members must not copy it.
    assert(text("function P() {} P.prototype = { m: 1 }; P.s = 1; Object.keys(P).join()") == "s");

    // Reflect.construct with a third argument, also for a built-in.
    assert(text("function A2(x) { this.x = x; } function B2() {} var made = Reflect.construct(A2, [3], B2);"
                "[made.x, made instanceof B2, made instanceof A2].join()") == "3,true,false");
    assert(text("var arr = Reflect.construct(Array, [1, 2, 3], B2); [Array.isArray(arr), arr.length, arr instanceof B2].join()") ==
           "true,3,true");
    assert(text("try { Reflect.construct(A2, [], function () {}.bind ? (() => 1) : 0); r = 'no error'; } catch (x) { r = x.name; } r") ==
           "TypeError");
}

TEST(js_a_class_that_extends_a_dom_interface_has_it_as_prototype) {
    using namespace litebird;

    auto doc = std::make_shared<Document>();
    JsInterpreter js(doc);
    js.eval_ast(R"(
        class MyEvent extends Event { constructor(type, extra) { super(type); this.extra = extra; } }
        class Bus extends EventTarget { emit(name) { return this.dispatchEvent(new MyEvent(name, 7)); } }
        class Card extends HTMLElement { static get observedAttributes() { return ['title']; } }
        var seen = [];
        var bus = new Bus();
        bus.addEventListener('x', function (e) { seen.push(e.type, e.extra, e instanceof MyEvent, e instanceof Event); });
        bus.emit('x');
    )");
    auto text = [&](const char* source) { return js.eval(source).to_string(); };

    assert(text("seen.join()") == "x,7,true,true");
    assert(text("Object.getPrototypeOf(MyEvent) === Event") == "true");
    assert(text("Object.getPrototypeOf(Bus) === EventTarget") == "true");
    assert(text("Object.getPrototypeOf(Card) === HTMLElement") == "true");
    assert(text("typeof Object.getPrototypeOf(Card)") == "function");
    // A static member of the interface is found through the class.
    assert(text("MyEvent.CAPTURING_PHASE + ':' + MyEvent.hasOwnProperty('CAPTURING_PHASE')") == "1:false");
    assert(text("Card.observedAttributes.join()") == "title");
    // The interface objects themselves keep working once they have a cell.
    assert(text("Event.name + ':' + (new Event('y').type) + ':' + (document.createElement('div') instanceof HTMLElement)") ==
           "Event:y:true");

    // The interface objects are linked the same way: each has its parent's
    // as prototype, as Web IDL says. Until Phase 34 a hidden property named
    // the parent, and nothing but `class X extends` read it.
    assert(text("var names = [], c = HTMLDivElement;"
                "while (typeof c === 'function' && c.name) { names.push(c.name); c = Object.getPrototypeOf(c); }"
                "names.join(' < ')") == "HTMLDivElement < HTMLElement < Element < Node < EventTarget");
    assert(text("Object.getPrototypeOf(EventTarget) === Function.prototype && Object.getPrototypeOf(Event) === Function.prototype") ==
           "true");
    assert(text("Object.getPrototypeOf(MouseEvent) === UIEvent && Object.getPrototypeOf(UIEvent) === Event") == "true");
    assert(text("Object.getPrototypeOf(DOMException) === Function.prototype && HTMLDivElement instanceof Function") == "true");
    // Constants are inherited, and are own properties where they are defined.
    assert(text("[HTMLElement.ELEMENT_NODE, Text.COMMENT_NODE, Card.TEXT_NODE, MouseEvent.AT_TARGET].join()") == "1,8,3,2");
    assert(text("[Node.hasOwnProperty('ELEMENT_NODE'), HTMLElement.hasOwnProperty('ELEMENT_NODE'), 'ELEMENT_NODE' in HTMLElement,"
                " 'ELEMENT_NODE' in Event].join()") == "true,false,true,false");
    // No property stands in for the link any more.
    assert(text("('%super' in HTMLElement) + ':' + Object.getOwnPropertyNames(HTMLElement).indexOf('%super')") == "false:-1");
    // The link is the script's to change, like any other.
    assert(text("function Old() {} Object.setPrototypeOf(Old, HTMLElement); var a = Old.ELEMENT_NODE;"
                "Object.setPrototypeOf(Old, Function.prototype); a + ':' + typeof Old.ELEMENT_NODE") == "1:undefined");
}

TEST(js_interface_objects_are_collected_with_their_page) {
    using namespace litebird;

    // An interface object that is a prototype and its cell keep each other
    // alive, like a parent class and its cell. When the page goes, the
    // collector has to take them: a browser makes a realm per page.
    JsIntrinsics::get();
    Heap::default_heap().collect_garbage();
    Heap::default_heap().collect_garbage();
    const size_t permanent_cells = Heap::default_heap().stats().alive_cells;
    for (int round = 0; round < 3; ++round) {
        {
            auto doc = HtmlParser::parse("<!DOCTYPE html><body><div id='a'><p>x</p></div></body>");
            JsInterpreter js(doc);
            js.eval_ast(R"(
                var o = HTMLDivElement, depth = 0;
                while (o) { o = Object.getPrototypeOf(o); depth++; }
                class Card extends HTMLElement { constructor() { super(); this.made = true; } }
                customElements.define('x-card', Card);
                var card = document.createElement('x-card');
                document.body.appendChild(card);
                class MyEvent extends Event {}
                class Bus extends EventTarget {}
                new Bus().dispatchEvent(new MyEvent('x'));
                var constants = [Card.ELEMENT_NODE, MyEvent.AT_TARGET, Text.TEXT_NODE];
            )");
            assert(js.eval("depth + ':' + card.made + ':' + constants.join()").to_string() == "7:true:1,2,3");
            Heap::default_heap().collect_garbage();
            assert(js.eval("Object.getPrototypeOf(Object.getPrototypeOf(Card)) === Element && card instanceof Card").to_string() == "true");
        }
        Heap::default_heap().collect_garbage();
        Heap::default_heap().collect_garbage();
        assert(Heap::default_heap().stats().alive_cells == permanent_cells);
    }

    // Two pages at once. Each realm has a Function.prototype and an Error of
    // its own, and the questions about chains get the same answers in each,
    // whichever ran last, and after the other is gone and collected.
    const char* questions =
        "[Object.getPrototypeOf(TypeError) === Error, typeof Function.prototype,"
        " Object.getPrototypeOf(function () {}) === Function.prototype,"
        " Object.getPrototypeOf(Math.max) === Function.prototype,"
        " Object.getPrototypeOf(HTMLElement) === Element, HTMLElement.ELEMENT_NODE,"
        " Object.getPrototypeOf(Object.getPrototypeOf(class A extends Error {})) === Function.prototype].join()";
    const std::string answers = "true,function,true,true,true,1,true";
    {
        auto first_doc = HtmlParser::parse("<!DOCTYPE html><body><div id='a'></div></body>");
        JsInterpreter first(first_doc);
        assert(first.eval(questions).to_string() == answers);
        {
            auto second_doc = HtmlParser::parse("<!DOCTYPE html><body><p>x</p></body>");
            JsInterpreter second(second_doc);
            assert(second.eval(questions).to_string() == answers);
            assert(first.eval(questions).to_string() == answers);       // the second was the last to run
            assert(second.eval(questions).to_string() == answers);
            // An error type of the other page has the other page's Error above it.
            first.global_env()->define("otherTypeError", second.eval("TypeError"));
            assert(first.eval("Object.getPrototypeOf(otherTypeError) === Error").to_string() == "false");
            assert(first.eval("Object.getPrototypeOf(otherTypeError).name").to_string() == "Error");
        }
        Heap::default_heap().collect_garbage();
        assert(first.eval(questions).to_string() == answers);
        // What the first page kept of the second is still a function with a
        // function above it.
        assert(first.eval("typeof otherTypeError + ':' + typeof Object.getPrototypeOf(otherTypeError)").to_string() ==
               "function:function");
    }
    Heap::default_heap().collect_garbage();
    Heap::default_heap().collect_garbage();
    assert(Heap::default_heap().stats().alive_cells == permanent_cells);
}

// --- Phase 35: attribute-list end removals ---------------------------------

TEST(attribute_list_end_removals_keep_order_and_index) {
    using namespace litebird;

    // Cross the scan/index boundary in both directions. Erase through both
    // overloads, reinsert names (their numbers must keep increasing), update
    // values in place, and compare every observable with a vector model.
    for (const int count : {0, 1, 31, 32, 33, 129, 513}) {
        AttributeList list;
        std::vector<AttributeList::value_type> model;
        for (int i = 0; i < count; ++i) {
            const std::string name = "a" + std::to_string(i);
            list[name] = std::to_string(i);
            model.emplace_back(name, std::to_string(i));
        }
        // Force the long list's index before the first removal.
        assert(list.count("missing") == 0);
        int removed_front = 0;
        int removed_back = 0;
        while (!model.empty()) {
            const bool front = ((removed_front + removed_back) % 3) != 2;
            const auto at = front ? model.begin() : model.end() - 1;
            const std::string name = at->first;
            if ((removed_front + removed_back) % 2 == 0) {
                auto found = list.find(name);
                assert(found != list.end());
                const bool was_front = at == model.begin();
                const bool was_back = at == model.end() - 1;
                const std::string next_name = was_front
                    ? (model.size() > 1 ? std::next(at)->first : std::string())
                    : std::string();
                auto next = list.erase(found);
                if (was_front && !next_name.empty()) {
                    assert(next != list.end() && next->first == next_name);
                } else if (was_back) {
                    assert(next == list.end());
                }
                if (front) ++removed_front; else ++removed_back;
            } else {
                assert(list.erase(name) == 1);
                if (front) ++removed_front; else ++removed_back;
            }
            // The model erase uses the name because the list's erase may
            // compact its storage and invalidate the endpoint we read above.
            const auto model_at = std::find_if(model.begin(), model.end(), [&](const auto& item) { return item.first == name; });
            assert(model_at != model.end());
            model.erase(model_at);
            assert(list.size() == model.size());
            assert(list.empty() == model.empty());
            assert(list.count(name) == 0);
            assert(list.erase(name) == 0);
            for (std::size_t i = 0; i < model.size(); ++i) {
                assert(list[i] == model[i]);
                auto found = list.find(model[i].first);
                assert(found != list.end() && found - list.begin() == static_cast<std::ptrdiff_t>(i));
                assert(found->second == model[i].second);
            }
            std::size_t walked = 0;
            for (const auto& item : list) {
                assert(item == model[walked++]);
            }
            assert(walked == model.size());
            if (!model.empty()) {
                // Updating keeps the position. Now and then a fresh name is
                // appended while the ends are being drained; its number must
                // be greater than every surviving one. Most steps shrink the
                // model, so this loop terminates.
                const std::string kept = model.back().first;
                list[kept] = "updated";
                model.back().second = "updated";
                assert(list.count(kept) == 1);
                if ((removed_front + removed_back) % 7 == 0) {
                    const std::string returned = front ? ("front" + std::to_string(removed_front)) : ("back" + std::to_string(removed_back));
                    list[returned] = "new";
                    model.emplace_back(returned, "new");
                    assert(list.find(returned) == list.end() - 1);
                }
            }
        }
        assert(list.empty());
        list["after-clear"] = "ok";
        assert(list.size() == 1 && std::as_const(list)[std::size_t{0}].first == "after-clear" && std::as_const(list)[std::size_t{0}].second == "ok");
    }

    // Copy and assignment preserve the visible order/values and remain
    // independently editable after the source has had its ends drained.
    AttributeList source;
    for (int i = 0; i < 80; ++i) source["k" + std::to_string(i)] = std::to_string(i);
    assert(source.count("k79") == 1);
    AttributeList copy(source);
    AttributeList assigned;
    assigned["old"] = "gone";
    assigned = source;
    assert(copy == source && assigned == source);
    assert(source.erase("k0") == 1 && source.erase("k79") == 1);
    assert(copy.count("k0") == 1 && copy.count("k79") == 1);
    assert(assigned.count("k0") == 1 && assigned.count("k79") == 1);
    copy["k1"] = "changed";
    assert(assigned["k1"] == "1" && source["k1"] == "1");
    AttributeList moved(std::move(copy));
    assert(copy.empty());
    assert(moved.size() == 80 && moved["k1"] == "changed" && moved.count("k79") == 1);
    AttributeList move_assigned;
    move_assigned = std::move(assigned);
    assert(assigned.empty());
    assert(move_assigned.size() == 80 && move_assigned.count("k0") == 1);
    move_assigned.clear();
    assert(move_assigned.empty() && move_assigned.find("k0") == move_assigned.end());
    move_assigned["fresh"] = "1";
    assert(move_assigned.size() == 1 && std::as_const(move_assigned)[std::size_t{0}].first == "fresh");

    // The index can be built after a dead prefix already exists: stay below
    // the scan threshold while erasing the front, grow past it, then search.
    AttributeList late;
    for (int i = 0; i < 31; ++i) late["l" + std::to_string(i)] = std::to_string(i);
    for (int i = 0; i < 10; ++i) late.erase(late.begin());
    for (int i = 31; i < 51; ++i) late["l" + std::to_string(i)] = std::to_string(i);
    assert(late.size() == 41 && std::as_const(late)[std::size_t{0}].first == "l10");
    assert(late.count("l50") == 1); // builds the index with a nonzero start
    for (int i = 10; i < 51; ++i) {
        assert(late.erase("l" + std::to_string(i)) == 1);
        assert(late.size() == static_cast<std::size_t>(51 - i - 1));
    }
    assert(late.empty());
}

TEST(attribute_list_front_removal_is_linear_in_practice) {
    using namespace litebird;

    // The old vector shifted both the items and the parallel number vector
    // on every front erase: 50,000 script removals took about six seconds on
    // the verification machine. The logical-start vector compacts only when
    // the dead prefix reaches the live tail. This is a generous wall-clock
    // regression guard, not a microbenchmark; the reproducible comparison is
    // tools/bench/bench_attributes.py / make bench-attributes.
    AttributeList direct;
    for (int i = 0; i < 50000; ++i) direct["a" + std::to_string(i)] = std::to_string(i);
    assert(direct.count("a49999") == 1);
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 50000; ++i) {
        assert(direct.erase("a" + std::to_string(i)) == 1);
        assert(direct.size() == static_cast<std::size_t>(50000 - i - 1));
        if (i + 1 < 50000) assert(std::as_const(direct)[std::size_t{0}].first == "a" + std::to_string(i + 1));
    }
    const double direct_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    assert(direct.empty());
    assert(direct_seconds < 5.0);

    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    js.load_page("<!DOCTYPE html><body></body>");
    js.notify_document_loaded();
    start = std::chrono::steady_clock::now();
    assert(js.eval(
        "var e = document.createElement('div');"
        "for (var i = 0; i < 50000; i++) e.setAttribute('a' + i, String(i));"
        "for (var i = 0; i < 50000; i++) e.removeAttribute('a' + i);"
        "e.setAttribute('last', 'ok');"
        "[e.attributes.length, e.attributes[0].name, e.getAttribute('last'), e.hasAttribute('a0'), e.outerHTML].join('|')").to_string() ==
        "1|last|ok|false|<div last=\"ok\"></div>");
    const double script_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    assert(script_seconds < 10.0);
}

// --- Phase 39: the HTML tokenizer's repeated attribute names -----------------

namespace {

using WrittenAttributes = std::vector<std::pair<std::string, std::string>>;

/// The first tag token of `markup`, or the EOF token when the tokenizer
/// dropped the tag (EOF inside it).
litebird::HtmlToken first_tag_token(const std::string& markup) {
    litebird::HtmlTokenizer tokenizer(markup);
    while (tokenizer.has_more()) {
        litebird::HtmlToken token = tokenizer.next_token();
        if (token.type == litebird::HtmlTokenType::StartTag ||
            token.type == litebird::HtmlTokenType::EndTag) return token;
        if (token.type == litebird::HtmlTokenType::EndOfFile) break;
    }
    return litebird::HtmlToken::make_eof();
}

std::string ascii_lower_name(const std::string& name) {
    std::string out = name;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string ascii_upper_name(const std::string& name) {
    std::string out = name;
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

/// What the standard says a tag's attributes are: in the order written, the
/// first of each ASCII-lower-cased name kept with its value, every later one
/// dropped. A set, not the tokenizer's own scan or index.
WrittenAttributes first_attribute_wins(const WrittenAttributes& written) {
    WrittenAttributes kept;
    std::set<std::string> names;
    for (const auto& [name, value] : written) {
        std::string lowered = ascii_lower_name(name);
        if (names.insert(lowered).second) kept.emplace_back(std::move(lowered), value);
    }
    return kept;
}

/// One attribute as markup. `form` 3 is valueless, so its value must be "".
std::string write_attribute(const std::string& name, const std::string& value, unsigned form) {
    switch (form % 4) {
        case 0: return name + "=\"" + value + "\"";
        case 1: return name + "='" + value + "'";
        case 2: return name + "=" + value;
        default: return name;
    }
}

} // namespace

TEST(tokenizer_repeated_attribute_names_keep_the_first) {
    using namespace litebird;

    // Every spelling of a duplicate: the first one wins, the name is
    // lower-cased before the comparison, and the order is the order of the
    // first occurrences. These tags are all below the size where the
    // tokenizer builds an index, so this is the short-tag path.
    struct Case { const char* markup; WrittenAttributes expected; bool self_closing; };
    const Case cases[] = {
        {"<div a=1 A=2 a=3 b=4 B=5>", {{"a", "1"}, {"b", "4"}}, false},
        {"<div a=\"first\" a a='third' a=fourth>", {{"a", "first"}}, false},
        {"<div a a=\"later\">", {{"a", ""}}, false},
        {"<div TITLE=\"x\" title=\"y\">", {{"title", "x"}}, false},
        // "=" starts a name, and after a name a "=" still opens that name's
        // value: <div bare =q=1> is one attribute "bare" with the value "q=1".
        {"<div =x=y =X=z =a =a=\"later\">", {{"=x", "y"}, {"=a", "a=\"later\""}}, false},
        {"<div bare =q=1>", {{"bare", "q=1"}}, false},
        {"<div bare / =q=1>", {{"bare", ""}, {"=q", "1"}}, false},
        {"<div a=\"&amp;&quot;&#65;\" a=\"&lt;\">", {{"a", "&\"A"}}, false},
        // Case folding is ASCII-only: Ä and ä are two different names.
        {"<div \xC3\x84=1 \xC3\xA4=2 \xC3\x84=3>", {{"\xC3\x84", "1"}, {"\xC3\xA4", "2"}}, false},
        {"<div a-b=1 A-B=2 a:b=3 A:B=4>", {{"a-b", "1"}, {"a:b", "3"}}, false},
        {"<div a=1 / a=2 />", {{"a", "1"}}, true},
        {"<div a=\"x>y\" a=z>", {{"a", "x>y"}}, false},
        {"<div a=\"x\"  a='y'\tb='z'\nB='w'>", {{"a", "x"}, {"b", "z"}}, false},
        {"<input disabled DISABLED type=text TYPE=hidden>", {{"disabled", ""}, {"type", "text"}}, false},
        {"<div a=\"\" a=\"full\">", {{"a", ""}}, false},
        {"<div a=1 b=2>", {{"a", "1"}, {"b", "2"}}, false},
    };
    for (const Case& one : cases) {
        const HtmlToken token = first_tag_token(one.markup);
        assert(token.type == HtmlTokenType::StartTag);
        assert(token.attributes == one.expected);
        assert(token.self_closing == one.self_closing);
    }

    // An end tag's attributes are tokenized the same way (the tree builder
    // is what drops them), and EOF inside a tag still drops the whole token.
    const HtmlToken end = first_tag_token("</div a=1 A=2 b=3>");
    assert(end.type == HtmlTokenType::EndTag);
    assert(end.name == "div");
    assert((end.attributes == WrittenAttributes{{"a", "1"}, {"b", "3"}}));
    assert(first_tag_token("<div a=1 a=2").type == HtmlTokenType::EndOfFile);
    assert(first_tag_token("<div a=1 a=2 ").type == HtmlTokenType::EndOfFile);

    // Around the size where the index is built, and around every growth of
    // the attribute vector after it: a duplicate of the first and of the
    // last name, in the other case, must still be dropped.
    for (int n : {1, 7, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 257}) {
        WrittenAttributes written;
        std::string markup = "<div";
        for (int i = 0; i < n; ++i) {
            // Every third name is longer than std::string's own buffer, so
            // the name's bytes move when the vector grows.
            const std::string name = (i % 3 == 0)
                ? "long-attribute-name-" + std::to_string(i) + "-" + std::string(48, 'x')
                : "n" + std::to_string(i);
            const unsigned form = static_cast<unsigned>(i);
            const std::string value = (form % 4 == 3) ? "" : "v" + std::to_string(i);
            written.emplace_back(name, value);
            markup += " " + write_attribute(name, value, form);
        }
        const std::string first_name = written.front().first;
        const std::string last_name = written.back().first;
        written.emplace_back(first_name, "dup-first");
        markup += " " + ascii_upper_name(first_name) + "=\"dup-first\"";
        written.emplace_back(last_name, "dup-last");
        markup += " " + ascii_upper_name(last_name) + "=\"dup-last\"";
        markup += ">x</div>";
        const HtmlToken token = first_tag_token(markup);
        assert(token.type == HtmlTokenType::StartTag);
        assert(token.attributes == first_attribute_wins(written));
        assert(token.attributes.size() == static_cast<std::size_t>(n));
    }

    // Random tags against the model: names short and long, ASCII and not,
    // plain and starting with '=', quoted, unquoted and valueless, with
    // duplicates in both cases. The pool size moves the tag across the
    // index threshold and across several vector growths.
    for (unsigned round = 0; round < 60; ++round) {
        std::mt19937 rng(20261114u + round);
        const int pool_size = 1 + static_cast<int>(rng() % 300);
        std::vector<std::string> pool;
        for (int i = 0; i < pool_size; ++i) {
            switch (i % 5) {
                case 0: pool.push_back("a" + std::to_string(i)); break;
                case 1: pool.push_back("data-very-long-attribute-" + std::to_string(i) + "-" + std::string(60, 'y')); break;
                case 2: pool.push_back("ns:b" + std::to_string(i)); break;
                case 3: pool.push_back("=eq" + std::to_string(i)); break;
                default: pool.push_back((i % 2 ? "\xC3\x84" : "\xC3\xA4") + std::to_string(i)); break;
            }
        }
        const bool end_tag = (round % 7 == 0);
        WrittenAttributes written;
        std::string markup = end_tag ? "</div" : "<div";
        const int count = 1 + static_cast<int>(rng() % 400);
        for (int k = 0; k < count; ++k) {
            std::string name = pool[rng() % pool.size()];
            // A valueless attribute followed by "=" takes what comes after
            // it as its value (the after-attribute-name state), so a name
            // that starts with "=" may not follow one.
            if (!written.empty() && written.back().second.empty() && name.front() == '=') name[0] = 'e';
            if (rng() % 2) name = ascii_upper_name(name);
            const unsigned form = rng() % 4;
            const std::string value = (form == 3) ? "" : "v" + std::to_string(k);
            written.emplace_back(name, value);
            markup += " " + write_attribute(name, value, form);
        }
        markup += ">";
        const HtmlToken token = first_tag_token(markup);
        assert(token.type == (end_tag ? HtmlTokenType::EndTag : HtmlTokenType::StartTag));
        assert(token.attributes == first_attribute_wins(written));
    }

    // The same shapes through the parser and the DOM, which is where a page
    // meets this: the element keeps the first value and the first position.
    auto document = HtmlParser::parse("<!DOCTYPE html><body><p id=p b=2 A=1 a=3 B=4 c>x</p></body>");
    const std::shared_ptr<Element> p = document->get_element_by_id("p");
    assert(p != nullptr);
    assert(p->get_attribute("a") == "1");
    assert(p->get_attribute("b") == "2");
    assert(p->get_attribute("c") == "");
    assert(p->attributes().size() == 4);
    assert(p->outer_html() == "<p id=\"p\" b=\"2\" a=\"1\" c=\"\">x</p>");
}

TEST(tokenizer_duplicate_attribute_check_is_linear) {
    using namespace litebird;

    // One element with 50,000 attributes took about 2.9 s in the parser when
    // every name was compared with every accepted attribute: the square of
    // the count. These are generous wall-clock regression guards, not
    // microbenchmarks; the reproducible comparison against an older binary
    // is tools/bench/bench_tokenizer_attributes.py / make
    // bench-tokenizer-attributes.
    constexpr int kWide = 50000;

    std::string distinct = "<div";
    for (int i = 0; i < kWide; ++i) distinct += " a" + std::to_string(i) + "=\"v" + std::to_string(i) + "\"";
    distinct += ">x</div>";
    auto start = std::chrono::steady_clock::now();
    const HtmlToken wide = first_tag_token(distinct);
    const double distinct_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    assert(wide.type == HtmlTokenType::StartTag);
    assert(wide.attributes.size() == static_cast<std::size_t>(kWide));
    for (int i = 0; i < kWide; ++i) {
        assert(wide.attributes[static_cast<std::size_t>(i)].first == "a" + std::to_string(i));
        assert(wide.attributes[static_cast<std::size_t>(i)].second == "v" + std::to_string(i));
    }
    assert(distinct_seconds < 2.0);

    // Half of them repeats, in the other case and with names longer than
    // std::string's buffer: the index must survive the vector growing under
    // it, and must still drop the second value of every name.
    std::string repeated = "<div";
    WrittenAttributes written;
    for (int i = 0; i < kWide; ++i) {
        const std::string name = "attribute-" + std::to_string(i / 2) + "-" + std::string(40, 'n');
        const bool second = (i % 2 != 0);
        const std::string value = second ? "dup" + std::to_string(i) : "v" + std::to_string(i / 2);
        written.emplace_back(second ? ascii_upper_name(name) : name, value);
        repeated += " " + write_attribute(written.back().first, value, static_cast<unsigned>(i));
    }
    repeated += ">x</div>";
    const WrittenAttributes expected = first_attribute_wins(written);
    start = std::chrono::steady_clock::now();
    const HtmlToken folded = first_tag_token(repeated);
    const double repeated_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    assert(folded.attributes == expected);
    assert(folded.attributes.size() == static_cast<std::size_t>(kWide / 2));
    assert(repeated_seconds < 2.0);

    // An end tag pays the same check; the tree builder drops its attributes
    // afterwards, so nothing but the tokenizer's time is left.
    std::string end_markup = "</div";
    for (int i = 0; i < kWide; ++i) end_markup += " a" + std::to_string(i) + "=v" + std::to_string(i);
    end_markup += ">";
    start = std::chrono::steady_clock::now();
    const HtmlToken end = first_tag_token(end_markup);
    const double end_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    assert(end.type == HtmlTokenType::EndTag);
    assert(end.attributes.size() == static_cast<std::size_t>(kWide));
    assert(end.attributes.back().first == "a" + std::to_string(kWide - 1));
    assert(end_seconds < 2.0);

    // Ordinary markup must not pay for the index: many tags of four
    // attributes each, one of them a repeat.
    std::string small;
    for (int i = 0; i < 5000; ++i) small += "<div a0=\"0\" a1=\"1\" a2=\"2\" A0=\"dup\">t</div>";
    start = std::chrono::steady_clock::now();
    HtmlTokenizer tokenizer(small);
    int tags = 0;
    while (tokenizer.has_more()) {
        const HtmlToken token = tokenizer.next_token();
        if (token.type == HtmlTokenType::EndOfFile) break;
        if (token.type != HtmlTokenType::StartTag) continue;
        assert((token.attributes == WrittenAttributes{{"a0", "0"}, {"a1", "1"}, {"a2", "2"}}));
        ++tags;
    }
    const double small_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    assert(tags == 5000);
    assert(small_seconds < 5.0);
}

// Phase 38: Real-web JPEG/GIF decoders, Canvas 2D, and SVG vector rendering.
static void test_jpeg_decoder_baseline_progressive_and_guards() {
    using namespace litebird;
    // 8x8 4-quadrant test image (TL=red, TR=green, BL=blue, BR=yellow)
    static constexpr uint8_t kTestJpegBaseline8x8[387] = {
        0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
        0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02,
        0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x04, 0x03, 0x02, 0x02, 0x02, 0x02, 0x05, 0x04,
        0x04, 0x03, 0x04, 0x06, 0x05, 0x06, 0x06, 0x06, 0x05, 0x06, 0x06, 0x06, 0x07, 0x09, 0x08, 0x06,
        0x07, 0x09, 0x07, 0x06, 0x06, 0x08, 0x0B, 0x08, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x06, 0x08,
        0x0B, 0x0C, 0x0B, 0x0A, 0x0C, 0x09, 0x0A, 0x0A, 0x0A, 0xFF, 0xDB, 0x00, 0x43, 0x01, 0x02, 0x02,
        0x02, 0x02, 0x02, 0x02, 0x05, 0x03, 0x03, 0x05, 0x0A, 0x07, 0x06, 0x07, 0x0A, 0x0A, 0x0A, 0x0A,
        0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
        0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
        0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0xFF, 0xC0,
        0x00, 0x11, 0x08, 0x00, 0x08, 0x00, 0x08, 0x03, 0x01, 0x11, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11,
        0x01, 0xFF, 0xC4, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0xFF, 0xC4, 0x00, 0x24, 0x10, 0x00, 0x00, 0x03, 0x05,
        0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x12, 0x14, 0x03,
        0x06, 0x09, 0x13, 0x16, 0x15, 0x17, 0x18, 0x21, 0x25, 0x41, 0x43, 0x61, 0x62, 0xFF, 0xC4, 0x00,
        0x15, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x05, 0x07, 0xFF, 0xC4, 0x00, 0x27, 0x11, 0x00, 0x01, 0x02, 0x02, 0x08, 0x07, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x13, 0x14, 0x08, 0x15, 0x03, 0x04,
        0x05, 0x07, 0x18, 0x22, 0x41, 0x62, 0x11, 0x21, 0x24, 0x42, 0x43, 0x61, 0x63, 0xFF, 0xDA, 0x00,
        0x0C, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3F, 0x00, 0x37, 0x07, 0x87, 0x27, 0x17,
        0x77, 0x8B, 0xA9, 0xD3, 0xD4, 0xF5, 0x91, 0xC2, 0xAD, 0x44, 0xF5, 0xBE, 0x99, 0x90, 0xB2, 0x7B,
        0x13, 0x6C, 0x19, 0xC5, 0xA3, 0x6A, 0xE8, 0xF0, 0xD3, 0x20, 0xEB, 0x5F, 0xBF, 0x75, 0xE3, 0x41,
        0x34, 0x1B, 0x7D, 0x29, 0x88, 0x96, 0xDA, 0x23, 0xAF, 0x1E, 0x4F, 0xC4, 0x6D, 0xA7, 0x8B, 0xF9,
        0x5E, 0x49, 0x64, 0xB1, 0x7D, 0x5C, 0xAA, 0xE5, 0x1F, 0x55, 0x70, 0x06, 0xFB, 0xC8, 0xFB, 0x47,
        0x37, 0xFF, 0xD9,
    };
    static constexpr uint8_t kTestJpegProgressive8x8[627] = {
        0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
        0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02,
        0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x04, 0x03, 0x02, 0x02, 0x02, 0x02, 0x05, 0x04,
        0x04, 0x03, 0x04, 0x06, 0x05, 0x06, 0x06, 0x06, 0x05, 0x06, 0x06, 0x06, 0x07, 0x09, 0x08, 0x06,
        0x07, 0x09, 0x07, 0x06, 0x06, 0x08, 0x0B, 0x08, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x06, 0x08,
        0x0B, 0x0C, 0x0B, 0x0A, 0x0C, 0x09, 0x0A, 0x0A, 0x0A, 0xFF, 0xDB, 0x00, 0x43, 0x01, 0x02, 0x02,
        0x02, 0x02, 0x02, 0x02, 0x05, 0x03, 0x03, 0x05, 0x0A, 0x07, 0x06, 0x07, 0x0A, 0x0A, 0x0A, 0x0A,
        0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
        0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
        0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0xFF, 0xC2,
        0x00, 0x11, 0x08, 0x00, 0x08, 0x00, 0x08, 0x03, 0x01, 0x11, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11,
        0x01, 0xFF, 0xC4, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0xFF, 0xC4, 0x00, 0x15, 0x01, 0x01, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x06, 0xFF, 0xDA,
        0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x10, 0x03, 0x10, 0x00, 0x00, 0x01, 0x36, 0x29, 0xFF, 0x00,
        0xFF, 0xC4, 0x00, 0x17, 0x10, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x07, 0x16, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00,
        0x01, 0x05, 0x02, 0x8F, 0x25, 0xAE, 0x3F, 0xFF, 0xC4, 0x00, 0x1F, 0x11, 0x00, 0x01, 0x03, 0x03,
        0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x12, 0x13, 0x14,
        0x04, 0x07, 0x21, 0x17, 0x41, 0x61, 0x63, 0x73, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x03, 0x01, 0x01,
        0x3F, 0x01, 0xB8, 0xD5, 0x3A, 0xBF, 0x17, 0x11, 0xA3, 0x2F, 0xB1, 0x4E, 0x23, 0xCC, 0x06, 0xF9,
        0x27, 0x61, 0x9F, 0xFF, 0xC4, 0x00, 0x20, 0x11, 0x00, 0x02, 0x01, 0x01, 0x09, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x12, 0x13, 0x00, 0x02, 0x06, 0x07, 0x14,
        0x17, 0x23, 0x42, 0x61, 0x62, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x02, 0x01, 0x01, 0x3F, 0x01, 0xC6,
        0xDB, 0xA3, 0xA6, 0x99, 0x0D, 0xE9, 0xE7, 0x97, 0x8A, 0x2A, 0x47, 0xEA, 0xD9, 0x2F, 0xD0, 0x15,
        0xFF, 0xC4, 0x00, 0x1D, 0x10, 0x00, 0x01, 0x02, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x14, 0x05, 0x11, 0x13, 0x23, 0x41, 0x51, 0x71, 0xFF,
        0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x06, 0x3F, 0x02, 0x88, 0xDC, 0x6E, 0xDE, 0x96, 0x08, 0xA6,
        0x7C, 0xD2, 0xFF, 0xC4, 0x00, 0x18, 0x10, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x51, 0xB1, 0xF0, 0xFF, 0xDA, 0x00, 0x08,
        0x01, 0x01, 0x00, 0x01, 0x3F, 0x21, 0xDD, 0x3A, 0x94, 0x57, 0x3F, 0xFF, 0xDA, 0x00, 0x0C, 0x03,
        0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x7F, 0xFF, 0xC4, 0x00, 0x16, 0x11, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xA1, 0xB1, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x03, 0x01, 0x01, 0x3F, 0x10, 0xBD, 0xEB, 0x86, 0xA3,
        0xFF, 0xC4, 0x00, 0x17, 0x11, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x21, 0xF0, 0x41, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x02, 0x01,
        0x01, 0x3F, 0x10, 0x87, 0x97, 0x00, 0xD1, 0x5F, 0xFF, 0xC4, 0x00, 0x18, 0x10, 0x00, 0x02, 0x03,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x21, 0x51,
        0xE1, 0xF0, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x01, 0x3F, 0x10, 0xD7, 0xEB, 0xB2, 0x18,
        0x07, 0xFF, 0xD9,
    };
    static constexpr uint8_t kTestJpegGray8x8[195] = {
        0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
        0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02,
        0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x04, 0x03, 0x02, 0x02, 0x02, 0x02, 0x05, 0x04,
        0x04, 0x03, 0x04, 0x06, 0x05, 0x06, 0x06, 0x06, 0x05, 0x06, 0x06, 0x06, 0x07, 0x09, 0x08, 0x06,
        0x07, 0x09, 0x07, 0x06, 0x06, 0x08, 0x0B, 0x08, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x06, 0x08,
        0x0B, 0x0C, 0x0B, 0x0A, 0x0C, 0x09, 0x0A, 0x0A, 0x0A, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x08,
        0x00, 0x08, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0xFF, 0xC4, 0x00, 0x22,
        0x10, 0x00, 0x00, 0x04, 0x04, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x11, 0x12, 0x13, 0x15, 0x02, 0x06, 0x09, 0x61, 0x14, 0x17, 0x18, 0x21, 0x24, 0x41, 0x43,
        0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00, 0x06, 0x80, 0xF2, 0xBE, 0xA3, 0x73,
        0x63, 0x9C, 0xCC, 0xCC, 0xC3, 0xE5, 0x88, 0x59, 0x67, 0x1B, 0xC0, 0x50, 0x4A, 0xE2, 0x6E, 0x83,
        0x7F, 0xFF, 0xD9,
    };

    assert(JpegDecoder::is_jpeg(kTestJpegBaseline8x8, sizeof(kTestJpegBaseline8x8)));
    auto base = JpegDecoder::decode_detailed(kTestJpegBaseline8x8, sizeof(kTestJpegBaseline8x8));
    assert(base.ok() && !base.progressive && base.width == 8 && base.height == 8 && base.components == 3);
    auto tl = Color::from_argb(base.image->get_pixel(1, 1));
    auto tr = Color::from_argb(base.image->get_pixel(6, 1));
    auto bl = Color::from_argb(base.image->get_pixel(1, 6));
    auto br = Color::from_argb(base.image->get_pixel(6, 6));
    assert(tl.r > 200 && tl.g < 60 && tl.b < 60);
    assert(tr.g > 180 && tr.r < 60 && tr.b < 60);
    assert(bl.b > 190 && bl.r < 60 && bl.g < 60);
    assert(br.r > 200 && br.g > 190 && br.b < 60);

    // Progressive SOF2 must produce identical quadrant pixels to Baseline SOF0.
    auto prog = JpegDecoder::decode_detailed(kTestJpegProgressive8x8, sizeof(kTestJpegProgressive8x8));
    assert(prog.ok() && prog.progressive && prog.width == 8 && prog.height == 8);
    assert(prog.image->pixels == base.image->pixels);

    // Grayscale JPEG
    auto gray = JpegDecoder::decode_detailed(kTestJpegGray8x8, sizeof(kTestJpegGray8x8));
    assert(gray.ok() && gray.components == 1 && gray.width == 8 && gray.height == 8);
    auto gp = Color::from_argb(gray.image->get_pixel(6, 6));
    assert(gp.r == gp.g && gp.g == gp.b && gp.r > 180);

    // Hostile/malformed inputs must fail cleanly without allocating oversized buffers
    static constexpr uint8_t kDimBomb[] = {
        0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xD9
    };
    assert(!JpegDecoder::decode(kDimBomb, sizeof(kDimBomb)));
    assert(!JpegDecoder::decode(kTestJpegBaseline8x8, 40)); // truncated
    static constexpr uint8_t kNotJpeg[] = {'P', 'N', 'G', 0, 1, 2};
    assert(!JpegDecoder::is_jpeg(kNotJpeg, sizeof(kNotJpeg)));
    assert(!JpegDecoder::decode(kNotJpeg, sizeof(kNotJpeg)));
}

static void test_gif_decoder_87a_89a_interlaced_and_animation() {
    using namespace litebird;
    // 8x8 2-frame animated GIF89a with Dispose=Background, transparency, loop=1 (NETSCAPE2.0)
    static constexpr uint8_t kTestGifAnim8x8[110] = {
        0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x08, 0x00, 0x08, 0x00, 0xF0, 0x00, 0x00, 0xFF, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x21, 0xF9, 0x04, 0x09, 0x0C, 0x00, 0x01, 0x00, 0x21, 0xFF, 0x0B, 0x4E, 0x45,
        0x54, 0x53, 0x43, 0x41, 0x50, 0x45, 0x32, 0x2E, 0x30, 0x03, 0x01, 0x01, 0x00, 0x00, 0x2C, 0x00,
        0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x00, 0x02, 0x0C, 0x84, 0x11, 0x19, 0x87, 0xCA, 0xBA,
        0x0E, 0x9C, 0xB4, 0x5A, 0x5A, 0x00, 0x00, 0x21, 0xF9, 0x04, 0x09, 0x0C, 0x00, 0x01, 0x00, 0x2C,
        0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x80, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x02,
        0x0B, 0x8C, 0x8F, 0xA9, 0xCB, 0xE0, 0x80, 0x1E, 0x3C, 0x32, 0xBE, 0x02, 0x00, 0x3B,
    };
    // 9x9 interlaced GIF89a: row 0 red, row 1 blue, rows 2..8 green
    static constexpr uint8_t kTestGifInterlaced9x9[59] = {
        0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x09, 0x00, 0x09, 0x00, 0xF1, 0x00, 0x00, 0xFF, 0x00, 0x00,
        0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x21, 0xF9, 0x04, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x09, 0x00, 0x40, 0x02, 0x0C, 0x84, 0x7F, 0xA1,
        0xCB, 0xED, 0x0F, 0x85, 0x9C, 0x13, 0xDA, 0x5B, 0x00, 0x00, 0x3B,
    };

    assert(GifDecoder::is_gif(kTestGifAnim8x8, sizeof(kTestGifAnim8x8)));
    auto anim = GifDecoder::decode_frames(kTestGifAnim8x8, sizeof(kTestGifAnim8x8));
    assert(anim.ok() && anim.width == 8 && anim.height == 8);
    assert(anim.frames.size() == 2);
    assert(anim.loop_count == 1);
    assert(anim.frames[0].delay_ms == 120);
    // Frame 0: TL 4x4 red, BR transparent
    assert(anim.frames[0].image->get_pixel(1, 1) == Color(255, 0, 0, 255).to_argb());
    assert(Color::from_argb(anim.frames[0].image->get_pixel(6, 6)).a == 0);
    // Frame 1 (after Dispose=Background): TL cleared to transparent, BR 4x4 blue
    assert(Color::from_argb(anim.frames[1].image->get_pixel(1, 1)).a == 0);
    assert(anim.frames[1].image->get_pixel(6, 6) == Color(0, 0, 255, 255).to_argb());

    // Single-frame helper returns frame 0
    auto first = GifDecoder::decode(kTestGifAnim8x8, sizeof(kTestGifAnim8x8));
    assert(first && first->get_pixel(1, 1) == Color(255, 0, 0, 255).to_argb());

    // 4-pass interlaced GIF
    auto inter = GifDecoder::decode(kTestGifInterlaced9x9, sizeof(kTestGifInterlaced9x9));
    assert(inter && inter->width == 9 && inter->height == 9);
    assert(inter->get_pixel(4, 0) == Color(255, 0, 0, 255).to_argb());
    assert(inter->get_pixel(4, 1) == Color(0, 0, 255, 255).to_argb());
    assert(inter->get_pixel(4, 5) == Color(0, 255, 0, 255).to_argb());
    assert(inter->get_pixel(4, 8) == Color(0, 255, 0, 255).to_argb());

    // Hostile dimension bomb & truncated GIF rejected cleanly
    static constexpr uint8_t kGifBomb[] = {
        'G', 'I', 'F', '8', '9', 'a', 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x3B
    };
    assert(!GifDecoder::decode(kGifBomb, sizeof(kGifBomb)));
    assert(!GifDecoder::decode(kTestGifAnim8x8, 12));
}

static void test_canvas2d_ctm_scanline_thick_stroke_clip_and_js_bindings() {
    using namespace litebird;
    CanvasRenderingContext2D ctx(100, 80);

    // 1. Even-odd vs non-zero winding on concentric squares
    ctx.fill_style = Color(255, 0, 0);
    ctx.begin_path();
    ctx.rect(5.0f, 5.0f, 30.0f, 30.0f);
    ctx.rect(12.0f, 12.0f, 16.0f, 16.0f);
    assert(ctx.is_point_in_path(8.0f, 8.0f, FillRule::EvenOdd));
    assert(!ctx.is_point_in_path(20.0f, 20.0f, FillRule::EvenOdd));
    assert(ctx.is_point_in_path(20.0f, 20.0f, FillRule::NonZero));
    ctx.fill(FillRule::EvenOdd);
    assert(ctx.image_data()->get_pixel(8, 20) == Color(255, 0, 0).to_argb());
    assert(ctx.image_data()->get_pixel(20, 20) == 0);

    // 2. CTM save/restore + translate/rotate + clip intersection
    ctx.save();
    ctx.begin_path();
    ctx.rect(45.0f, 5.0f, 20.0f, 30.0f);
    ctx.clip();
    ctx.translate(60.0f, 20.0f);
    ctx.rotate(0.78539816f); // 45 deg
    ctx.fill_style = Color(0, 255, 0);
    ctx.fill_rect(-15.0f, -15.0f, 30.0f, 30.0f);
    ctx.restore();
    // Inside both rotated square and clip rect [45..65]
    assert(ctx.image_data()->get_pixel(55, 20) == Color(0, 255, 0).to_argb());
    // Inside rotated square but outside clip rect (>65) -> clipped!
    assert(ctx.image_data()->get_pixel(70, 20) == 0);

    // 3. Thick dashed stroke + global_alpha
    ctx.stroke_style = Color(0, 0, 255);
    ctx.line_width = 6.0f;
    ctx.set_line_dash({10.0f, 10.0f});
    ctx.begin_path();
    ctx.move_to(10.0f, 55.0f);
    ctx.line_to(70.0f, 55.0f);
    ctx.stroke();
    assert(ctx.image_data()->get_pixel(15, 55) == Color(0, 0, 255).to_argb());
    assert(ctx.image_data()->get_pixel(15, 53) == Color(0, 0, 255).to_argb());
    assert(ctx.image_data()->get_pixel(25, 55) == 0); // gap
    assert(ctx.image_data()->get_pixel(35, 55) == Color(0, 0, 255).to_argb());

    // 4. get_image_data_rgba / put_image_data_rgba roundtrip
    auto patch = ctx.get_image_data_rgba(14, 54, 2, 2);
    assert(patch.size() == 16 && patch[0] == 0 && patch[1] == 0 && patch[2] == 255 && patch[3] == 255);
    ctx.put_image_data_rgba(patch.data(), 2, 2, 85, 65);
    assert(ctx.image_data()->get_pixel(85, 65) == Color(0, 0, 255).to_argb());

    // 5. End-to-end JS accessor bindings via document.getElementById().getContext('2d')
    auto cdoc = HtmlParser::parse("<body><canvas id='c' width='40' height='40'></canvas></body>");
    JsInterpreter interp(cdoc);
    interp.eval(R"(
        var ctx = document.getElementById('c').getContext('2d');
        ctx.save();
        ctx.globalAlpha = 0.5;
        ctx.lineCap = 'round';
        ctx.setLineDash([6, 4]);
        var dashLen = ctx.getLineDash().length;
        var capVal = ctx.lineCap;
        var alphaVal = ctx.globalAlpha;
        ctx.translate(10, 10);
        ctx.fillStyle = '#ff0000';
        ctx.fillRect(0, 0, 20, 20);
        ctx.restore();
        var afterRestoreAlpha = ctx.globalAlpha;
        var img = ctx.getImageData(15, 15, 1, 1);
        var pxR = img.data[0];
        var pxA = img.data[3];
    )");
    assert(interp.eval("dashLen").to_number() == 2.0);
    assert(interp.eval("capVal").to_string() == "round");
    assert(std::abs(interp.eval("alphaVal").to_number() - 0.5) < 0.01);
    assert(std::abs(interp.eval("afterRestoreAlpha").to_number() - 1.0) < 0.01);
    assert(interp.eval("pxR").to_number() == 255.0);
    const double pxA = interp.eval("pxA").to_number();
    assert(pxA >= 120.0 && pxA <= 135.0);
}

static void test_svg_real_world_paths_arcs_gradients_use_clip_and_guards() {
    using namespace litebird;
    // 1. Paths with W3C elliptical arc + compact flags, <defs>, gradient href inheritance,
    //    <use>, <symbol>, <clipPath>, inline style, currentColor, display:none
    auto doc = HtmlParser::parse(R"svg(
        <svg viewBox="0 0 120 60">
          <defs>
            <linearGradient id="baseG">
              <stop offset="0%" stop-color="#ff0000"/>
              <stop offset="100%" stop-color="#0000ff"/>
            </linearGradient>
            <linearGradient id="grad1" href="#baseG" x1="0%" y1="0%" x2="100%" y2="0%"/>
            <clipPath id="halfClip"><rect x="0" y="0" width="20" height="40"/></clipPath>
            <symbol id="dot" viewBox="0 0 20 20"><circle cx="10" cy="10" r="9"/></symbol>
          </defs>
          <rect x="0" y="0" width="40" height="40" fill="url(#grad1)" clip-path="url(#halfClip)"/>
          <g color="#00ff00" transform="translate(50, 10)">
            <use href="#dot" width="20" height="20" fill="currentColor"/>
          </g>
          <path d="M80,30 A15,15 0 11110,30 A15,15 0 1180,30 Z" style="fill: #ffff00"/>
          <rect x="0" y="0" width="120" height="60" fill="#ffffff" display="none"/>
        </svg>
    )svg");
    auto svgs = doc->get_elements_by_tag_name("svg");
    assert(!svgs.empty());
    auto img = SvgRenderer::render(svgs[0], 120.0f, 60.0f);
    assert(img && img->width == 120 && img->height == 60);

    auto left_grad = Color::from_argb(img->get_pixel(2, 20));
    assert(left_grad.a == 255 && left_grad.r > 220 && left_grad.b < 35);
    assert(img->get_pixel(30, 20) == 0); // clipped by halfClip
    assert(img->get_pixel(60, 20) == Color(0, 255, 0, 255).to_argb()); // <use> + <symbol> + currentColor
    assert(img->get_pixel(95, 30) == Color(255, 255, 0, 255).to_argb()); // arc circle

    // 2. Hostile <use> and <linearGradient> reference cycles must terminate cleanly without stack overflow
    auto cyclic_doc = HtmlParser::parse(R"svg(
        <svg viewBox="0 0 40 40">
          <defs>
            <linearGradient id="lgA" href="#lgB"/>
            <linearGradient id="lgB" href="#lgA"/>
            <g id="uA"><use href="#uB"/></g>
            <g id="uB"><use href="#uA"/></g>
          </defs>
          <rect x="0" y="0" width="40" height="40" fill="url(#lgA)"/>
          <use href="#uA"/>
        </svg>
    )svg");
    auto c_svgs = cyclic_doc->get_elements_by_tag_name("svg");
    auto c_img = SvgRenderer::render(c_svgs[0], 40.0f, 40.0f);
    assert(c_img && c_img->width == 40 && c_img->height == 40);
}

// ============================================================================
// Phase 40: Worker MIME/module policy, EventTarget dispatch, lifecycle safety
// ============================================================================

TEST(worker_mime_policy_enforces_javascript_content_types) {
    using namespace litebird;

    // 1. Direct WHATWG MIME Sniffing §4.3 table checks (all 16 standard types,
    //    case-insensitivity, parameters, and surrounding HTTP whitespace).
    const char* allowed[] = {
        "application/ecmascript",
        "application/javascript",
        "application/x-ecmascript",
        "application/x-javascript",
        "text/ecmascript",
        "text/javascript",
        "text/javascript1.0",
        "text/javascript1.1",
        "text/javascript1.2",
        "text/javascript1.3",
        "text/javascript1.4",
        "text/javascript1.5",
        "text/jscript",
        "text/livescript",
        "text/x-ecmascript",
        "text/x-javascript",
        "TEXT/JAVASCRIPT; charset=utf-8",
        "  application/javascript ; charset=UTF-8 ",
        "\ttext/javascript\t",
    };
    for (const char* ct : allowed) {
        assert(WorkerObject::is_javascript_mime_type(ct));
    }

    const char* rejected[] = {
        "",
        "   ",
        "text/plain",
        "text/plain; charset=utf-8",
        "text/html",
        "application/json",
        "application/octet-stream",
        "image/png",
        "image/svg+xml",
        "text/javascript/extra",
        "text/ javascript",
        "application/javascript2",
        ";charset=utf-8",
    };
    for (const char* ct : rejected) {
        assert(!WorkerObject::is_javascript_mime_type(ct));
    }

    auto wait_for = [](JsInterpreter& js, const std::string& expr) {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(4)) {
            EventLoop::default_loop().tick();
            EventLoop::default_loop().run_microtasks();
            if (js.eval(expr).is_truthy()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return js.eval(expr).is_truthy();
    };

    // 2. Real loopback HTTP server returning diverse Content-Type headers & status codes.
    const int server = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(server >= 0);
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    assert(::bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    assert(::listen(server, 16) == 0);
    socklen_t addr_len = sizeof(addr);
    assert(::getsockname(server, reinterpret_cast<sockaddr*>(&addr), &addr_len) == 0);
    const int port = ntohs(addr.sin_port);

    std::atomic<bool> stop_server{false};
    std::thread server_thread([server, &stop_server]() {
        while (!stop_server.load(std::memory_order_acquire)) {
            timeval tv{0, 50000};
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(server, &rfds);
            if (::select(server + 1, &rfds, nullptr, nullptr, &tv) <= 0) continue;
            const int client = ::accept(server, nullptr, nullptr);
            if (client < 0) continue;
            char buf[2048];
            const ssize_t n = ::recv(client, buf, sizeof(buf) - 1, 0);
            std::string req = (n > 0) ? std::string(buf, static_cast<size_t>(n)) : "";
            std::string path = "/";
            auto sp1 = req.find(' ');
            if (sp1 != std::string::npos) {
                auto sp2 = req.find(' ', sp1 + 1);
                if (sp2 != std::string::npos) path = req.substr(sp1 + 1, sp2 - sp1 - 1);
            }

            std::string reply;
            if (path == "/ok.js") {
                std::string body = "postMessage('http-js-ok');";
                reply = "HTTP/1.1 200 OK\r\nContent-Type: text/javascript; charset=utf-8\r\nContent-Length: " +
                        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            } else if (path == "/legacy.js") {
                std::string body = "postMessage('http-legacy-ok');";
                reply = "HTTP/1.1 200 OK\r\nContent-Type: application/x-javascript\r\nContent-Length: " +
                        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            } else if (path == "/plain.js") {
                std::string body = "postMessage('MUST_NOT_RUN_PLAIN');";
                reply = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            } else if (path == "/html.js") {
                std::string body = "postMessage('MUST_NOT_RUN_HTML');";
                reply = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            } else if (path == "/missing-ct.js") {
                std::string body = "postMessage('MUST_NOT_RUN_NO_CT');";
                reply = "HTTP/1.1 200 OK\r\nContent-Length: " +
                        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            } else if (path == "/redirect.js") {
                std::string body = "redirecting";
                reply = "HTTP/1.1 302 Found\r\nLocation: /ok.js\r\nContent-Type: text/javascript\r\nContent-Length: " +
                        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            } else if (path == "/status300.js") {
                std::string body = "postMessage('MUST_NOT_RUN_300');";
                reply = "HTTP/1.1 300 Multiple Choices\r\nContent-Type: text/javascript\r\nContent-Length: " +
                        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            } else {
                std::string body = "postMessage('MUST_NOT_RUN_404');";
                reply = "HTTP/1.1 404 Not Found\r\nContent-Type: text/javascript\r\nContent-Length: " +
                        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            }
            (void)::send(client, reply.data(), reply.size(), 0);
            ::close(client);
        }
    });

    const std::string base = "http://127.0.0.1:" + std::to_string(port);
    auto doc = std::make_shared<Document>();
    doc->set_url(base + "/index.html");
    JsInterpreter js(doc);
    js.load_page("<!DOCTYPE html><body></body>");
    js.eval(
        "var msgs = {}, errs = {};"
        "function spawn(k, url) {"
        "  var w = new Worker(url);"
        "  w.onmessage = function(e) { msgs[k] = String(e.data); };"
        "  w.addEventListener('error', function(e) { errs[k] = e.message; });"
        "  return w;"
        "}"
        "var ws = ["
        "  spawn('data_js', 'data:text/javascript,postMessage(\"data-js-ok\")'),"
        "  spawn('data_app_js', 'data:application/javascript;charset=utf-8,postMessage(\"data-app-ok\")'),"
        "  spawn('data_plain', 'data:text/plain,postMessage(\"BAD\")'),"
        "  spawn('data_default', 'data:,postMessage(\"BAD\")'),"
        "  spawn('data_html', 'data:text/html,postMessage(\"BAD\")'),"
        "  spawn('http_ok', '" + base + "/ok.js'),"
        "  spawn('http_legacy', '" + base + "/legacy.js'),"
        "  spawn('http_plain', '" + base + "/plain.js'),"
        "  spawn('http_html', '" + base + "/html.js'),"
        "  spawn('http_no_ct', '" + base + "/missing-ct.js'),"
        "  spawn('http_redirect', '" + base + "/redirect.js'),"
        "  spawn('http_300', '" + base + "/status300.js'),"
        "  spawn('http_404', '" + base + "/notfound.js')"
        "];"
    );

    assert(wait_for(js,
        "!!(msgs.data_js && msgs.data_app_js && msgs.http_ok && msgs.http_legacy && msgs.http_redirect && "
        "errs.data_plain && errs.data_default && errs.data_html && "
        "errs.http_plain && errs.http_html && errs.http_no_ct && errs.http_300 && errs.http_404)"));

    assert(js.eval("msgs.data_js").to_string() == "data-js-ok");
    assert(js.eval("msgs.data_app_js").to_string() == "data-app-ok");
    assert(js.eval("msgs.http_ok").to_string() == "http-js-ok");
    assert(js.eval("msgs.http_legacy").to_string() == "http-legacy-ok");
    assert(js.eval("msgs.http_redirect").to_string() == "http-js-ok");
    assert(js.eval("!msgs.data_plain && !msgs.data_default && !msgs.data_html").is_truthy());
    assert(js.eval("!msgs.http_plain && !msgs.http_html && !msgs.http_no_ct && !msgs.http_300 && !msgs.http_404").is_truthy());
    assert(js.eval("errs.data_plain").to_string().find("text/plain") != std::string::npos);
    assert(js.eval("errs.http_html").to_string().find("text/html") != std::string::npos);
    assert(js.eval("errs.http_no_ct").to_string().find("missing Content-Type") != std::string::npos);
    assert(js.eval("errs.http_300").to_string().find("300") != std::string::npos);
    assert(js.eval("errs.http_404").to_string().find("404") != std::string::npos);

    js.eval("ws.forEach(function(w) { w.terminate(); });");
    stop_server.store(true, std::memory_order_release);
    server_thread.join();
    ::close(server);

    // 3. Local file document: .txt file is rejected by MIME policy even when local files are allowed.
    const std::string suffix = std::to_string(static_cast<long>(::getpid()));
    const std::string txt_path = "/tmp/litebird-worker-mime-" + suffix + ".txt";
    {
        std::ofstream f(txt_path);
        f << "postMessage('MUST_NOT_RUN_TXT');";
    }
    auto file_doc = std::make_shared<Document>();
    file_doc->set_url("file:///tmp/litebird-worker-mime-page-" + suffix + ".html");
    JsInterpreter local(file_doc);
    local.load_page("<!DOCTYPE html><body></body>");
    local.eval(
        "var localMsg = '', localErr = '';"
        "var lw = new Worker('" + txt_path + "');"
        "lw.onmessage = function(e) { localMsg = String(e.data); };"
        "lw.addEventListener('error', function(e) { localErr = e.message; });"
    );
    assert(wait_for(local, "!!localErr"));
    assert(local.eval("localMsg").to_string().empty());
    assert(local.eval("localErr").to_string().find("text/plain") != std::string::npos);
    local.eval("lw.terminate();");
    std::remove(txt_path.c_str());
}

TEST(worker_options_module_policy_and_global_scope) {
    using namespace litebird;

    auto wait_for = [](JsInterpreter& js, const std::string& expr) {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(4)) {
            EventLoop::default_loop().tick();
            EventLoop::default_loop().run_microtasks();
            if (js.eval(expr).is_truthy()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return js.eval(expr).is_truthy();
    };

    const std::string suffix = std::to_string(static_cast<long>(::getpid()));
    const std::string secret_js = "/tmp/litebird-import-secret-" + suffix + ".js";
    {
        std::ofstream f(secret_js);
        f << "var LEAKED_SECRET = 1;";
    }

    auto web_doc = std::make_shared<Document>();
    web_doc->set_url("https://example.test/app/page.html");
    JsInterpreter js(web_doc);
    js.load_page("<!DOCTYPE html><body></body>");

    // 1. Constructor validation: missing `new`, missing arg, bad options, invalid type enum.
    js.eval(
        "var ctorErrs = {};"
        "try { Worker('data:text/javascript,1'); } catch (e) { ctorErrs.noNew = e.name; }"
        "try { new Worker(); } catch (e) { ctorErrs.noArg = e.name; }"
        "try { new Worker('data:text/javascript,1', 42); } catch (e) { ctorErrs.badOpts = e.name; }"
        "try { new Worker('data:text/javascript,1', { type: 'esm' }); } catch (e) { ctorErrs.badType = e.name; }"
    );
    assert(js.eval("ctorErrs.noNew").to_string() == "TypeError");
    assert(js.eval("ctorErrs.noArg").to_string() == "TypeError");
    assert(js.eval("ctorErrs.badOpts").to_string() == "TypeError");
    assert(js.eval("ctorErrs.badType").to_string() == "TypeError");

    // 2. Module worker policy: { type: 'module' } must NOT silently run as classic JS;
    //    it queues an error event to both onerror and addEventListener('error').
    js.eval(
        "var modRan = false, modOnErr = '', modEvErr = '', modFilename = '';"
        "var wm = new Worker('data:text/javascript,postMessage(\"SHOULD_NOT_RUN\")', { type: 'module', name: 'mod1' });"
        "wm.onmessage = function() { modRan = true; };"
        "wm.onerror = function(e) { modOnErr = e.message; modFilename = e.filename; };"
        "wm.addEventListener('error', function(e) { modEvErr = e.message; });"
    );
    assert(wait_for(js, "!!(modOnErr && modEvErr)"));
    assert(!js.eval("modRan").is_truthy());
    assert(js.eval("modOnErr").to_string().find("Module workers") != std::string::npos);
    assert(js.eval("modEvErr").to_string() == js.eval("modOnErr").to_string());
    assert(js.eval("modFilename").to_string().rfind("data:text/javascript", 0) == 0);
    js.eval("wm.terminate();");

    // 3. Classic options (name, self.location, null/undefined options) and importScripts().
    js.eval(
        "var classicInfo = null, nullOptsOk = false, importResult = null;"
        "var wc = new Worker('data:text/javascript,postMessage({ name: self.name, globalName: name, sameSelf: self.self === self, proto: self.location.protocol })', { type: 'classic', name: 'compute-7' });"
        "wc.onmessage = function(e) { classicInfo = e.data; };"
        "var wn = new Worker('data:text/javascript,postMessage(123)', null);"
        "wn.onmessage = function(e) { nullOptsOk = (e.data === 123); };"
        "var wi = new Worker('data:text/javascript,"
        "  importScripts(\"data:text/javascript,var helperVal = 40;\", \"data:application/javascript,helperVal += 2;\");"
        "  var plainErr = \"\", localErr = \"\";"
        "  try { importScripts(\"data:text/plain,helperVal = 999;\"); } catch (e) { plainErr = String(e); }"
        "  try { importScripts(\"" + secret_js + "\"); } catch (e) { localErr = String(e); }"
        "  postMessage({ val: helperVal, plainErr: plainErr, localErr: localErr });"
        "');"
        "wi.onmessage = function(e) { importResult = e.data; };"
    );
    assert(wait_for(js, "!!(classicInfo && nullOptsOk && importResult)"));
    assert(js.eval("classicInfo.name").to_string() == "compute-7");
    assert(js.eval("classicInfo.globalName").to_string() == "compute-7");
    assert(js.eval("classicInfo.sameSelf").is_truthy());
    assert(js.eval("classicInfo.proto").to_string() == "data:");
    assert(js.eval("importResult.val").to_number() == 42.0);
    assert(js.eval("importResult.plainErr").to_string().find("text/plain") != std::string::npos);
    assert(!js.eval("importResult.localErr").to_string().empty());

    js.eval("wc.terminate(); wn.terminate(); wi.terminate();");
    std::remove(secret_js.c_str());
}

TEST(worker_event_target_error_and_message_dispatch) {
    using namespace litebird;

    auto wait_for = [](JsInterpreter& js, const std::string& expr) {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(4)) {
            EventLoop::default_loop().tick();
            EventLoop::default_loop().run_microtasks();
            if (js.eval(expr).is_truthy()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return js.eval(expr).is_truthy();
    };

    JsInterpreter js(nullptr);

    // 1. addEventListener('error') alone (no onerror), plus onerror + multiple listeners,
    //    dedup, removeEventListener, once, handleEvent, preventDefault, stopImmediatePropagation.
    js.eval(
        "var log = [];"
        "var w1 = new Worker('data:text/javascript,throw new Error(\"boom-1\")');"
        "function dupFn(e) { log.push('dup:' + e.type + ':' + (e.target === w1)); }"
        "function removedFn() { log.push('REMOVED'); }"
        "var objListener = { handleEvent: function(e) { log.push('obj:' + e.cancelable); e.preventDefault(); log.push('dp:' + e.defaultPrevented); } };"
        "w1.onerror = function(e) { log.push('onerror:' + e.message.indexOf('boom-1')); };"
        "w1.addEventListener('error', dupFn);"
        "w1.addEventListener('error', dupFn);"
        "w1.addEventListener('error', removedFn);"
        "w1.removeEventListener('error', removedFn);"
        "w1.addEventListener('error', objListener);"
        "w1.addEventListener('error', function(e) { log.push('stop'); e.stopImmediatePropagation(); });"
        "w1.addEventListener('error', function() { log.push('AFTER_STOP'); });"
        ""
        "var onlyListenerErr = '';"
        "var w2 = new Worker('data:text/plain,not-js');"
        "w2.addEventListener('error', function(e) { onlyListenerErr = e.message; });"
        ""
        "var onceCount = 0, allCount = 0;"
        "var w3 = new Worker('data:text/javascript,self.addEventListener(\"message\", function(e) { postMessage(e.data * 10); });');"
        "w3.addEventListener('message', function() { onceCount++; }, { once: true });"
        "w3.addEventListener('message', function() { allCount++; });"
        "w3.postMessage(1); w3.postMessage(2); w3.postMessage(3);"
        ""
        "var internalHandled = false, leakedToPage = false;"
        "var w4 = new Worker('data:text/javascript,"
        "  self.addEventListener(\"error\", function(e) { e.preventDefault(); postMessage(\"caught-inside\"); });"
        "  self.onmessage = function() { throw new Error(\"inner\"); };"
        "');"
        "w4.onmessage = function(e) { if (e.data === 'caught-inside') internalHandled = true; };"
        "w4.addEventListener('error', function() { leakedToPage = true; });"
        "w4.postMessage('go');"
    );

    assert(wait_for(js, "log.length >= 5 && !!onlyListenerErr && allCount === 3 && internalHandled"));
    assert(js.eval("log.join('|')").to_string() == "onerror:7|dup:error:true|obj:true|dp:true|stop");
    assert(js.eval("onlyListenerErr").to_string().find("text/plain") != std::string::npos);
    assert(js.eval("onceCount").to_number() == 1.0);
    assert(js.eval("allCount").to_number() == 3.0);
    assert(!js.eval("leakedToPage").is_truthy());

    js.eval("w1.terminate(); w2.terminate(); w3.terminate(); w4.terminate();");

    // 2. Direct C++ WorkerObject::create_js_instance also wires onmessage/onerror/addEventListener.
    ResourceRequestContext ctx;
    JsValue direct = WorkerObject::create_js_instance(
        "data:text/javascript,postMessage('direct-ok')", ctx, WorkerOptions{WorkerType::Classic, "d"});
    js.global_env()->define("directW", direct);
    js.eval("var directMsg = ''; directW.onmessage = function(e) { directMsg = e.data; };");
    assert(wait_for(js, "directMsg === 'direct-ok'"));
    js.eval("directW.terminate();");
}

TEST(worker_concurrent_startup_nested_and_clean_shutdown) {
    using namespace litebird;

    auto wait_for = [](JsInterpreter& js, const std::string& expr) {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
            EventLoop::default_loop().tick();
            EventLoop::default_loop().run_microtasks();
            if (js.eval(expr).is_truthy()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return js.eval(expr).is_truthy();
    };

    JsInterpreter js(nullptr);

    // 1. 8 concurrent workers starting simultaneously + self.close() cleanup.
    js.eval(
        "var sum = 0, done = 0;"
        "for (var i = 1; i <= 8; i++) {"
        "  (function(n) {"
        "    var w = new Worker('data:text/javascript,postMessage(' + n + '); self.close();');"
        "    w.onmessage = function(e) { sum += e.data; done++; };"
        "  })(i);"
        "}"
    );
    assert(wait_for(js, "done === 8"));
    assert(js.eval("sum").to_number() == 36.0);

    // 2. Nested worker (page -> parent worker -> child worker -> parent -> page)
    //    and clean parent termination (child must be stopped before parent's EventLoop dies).
    js.eval(R"js(
        var nestedReply = '';
        var childUrl = "data:text/javascript,self.onmessage = function(e) { postMessage(e.data + ':child'); };";
        var parentUrl = "data:text/javascript,"
            + "var child = new Worker(" + JSON.stringify(childUrl) + ");"
            + "child.onmessage = function(e) { postMessage(e.data + ':parent'); };"
            + "child.postMessage('ping');";
        var parent = new Worker(parentUrl);
        parent.onmessage = function(e) { nestedReply = e.data; };
    )js");
    assert(wait_for(js, "nestedReply === 'ping:child:parent'"));
    js.eval("parent.terminate();");

    // 3. Collect garbage after workers close/terminate to verify zero UAF/cycles.
    EventLoop::default_loop().tick();
    Heap::default_heap().collect_garbage();

    // 4. Spawn 4 idle workers without calling terminate(), then verify shutdown_all_workers()
    //    joins every thread deterministically.
    js.eval(
        "for (var j = 0; j < 4; j++) {"
        "  new Worker('data:text/javascript,self.onmessage = function(e) { postMessage(e.data); };');"
        "}"
    );
    WorkerObject::shutdown_all_workers();
    EventLoop::default_loop().tick();
}

// =========================================================================
// Phase 42: Nested <iframe> Browsing Contexts, Origin/Sandbox Isolation,
//           Cross-Realm DOM Wrappers, XHTML Execution, & Subdocument Layout
// =========================================================================

TEST(iframe_browsing_context_hierarchy_and_navigation) {
    using namespace litebird;
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(1));

    // 1. Recursive load ordering (grandchild -> child -> parent), WindowProxy
    //    stability across navigation, window.frames[i] / window[name],
    //    <template> inertness, and detached iframe cleanup.
    auto doc = std::make_shared<Document>();
    doc->set_url("https://app.example.test/index.html");
    JsInterpreter js(doc);

    const std::string html = R"html(<!doctype html>
<html><body>
  <template id="tmpl"><iframe id="inert" srcdoc="<p>no</p>"></iframe></template>
  <iframe id="f1" name="alpha" srcdoc="<script>parent.events.push('child-inline');</script><iframe id='gc' srcdoc='<script>top.events.push(&quot;grandchild-inline&quot;); window.onload = function(){ top.events.push(&quot;grandchild-win-load&quot;); };</script>'></iframe><script>document.getElementById('gc').onload = function(){ parent.events.push('grandchild-el-load'); }; window.onload = function(){ parent.events.push('child-win-load'); };</script>"></iframe>
  <script>
    window.events = [];
    document.addEventListener('DOMContentLoaded', function() { events.push('parent-dcl'); });
    window.addEventListener('load', function() { events.push('parent-win-load'); });
    var f1 = document.getElementById('f1');
    f1.addEventListener('load', function() { events.push('child-el-load'); });
    // Synchronous initial about:blank exists immediately upon connection.
    window.syncHasWin = (f1.contentWindow !== null && f1.contentDocument !== null);
    window.savedProxy = f1.contentWindow;
    window.tmplHasWin = (document.getElementById('tmpl').content.querySelector('iframe').contentWindow !== null);
  </script>
</body></html>)html";

    js.load_page(html, true);
    js.notify_document_loaded();
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));

    assert(js.eval("syncHasWin === true").is_truthy());
    assert(js.eval("tmplHasWin === false").is_truthy());
    assert(js.eval("window.length === 1").is_truthy());
    assert(js.eval("window[0] === f1.contentWindow").is_truthy());
    assert(js.eval("window.alpha === f1.contentWindow").is_truthy());
    assert(js.eval("f1.contentWindow.parent === window").is_truthy());
    assert(js.eval("f1.contentWindow.top === window").is_truthy());
    assert(js.eval("f1.contentWindow.frameElement === f1").is_truthy());
    assert(js.eval("f1.contentWindow[0].top === window").is_truthy());
    assert(js.eval("f1.contentWindow[0].parent === f1.contentWindow").is_truthy());
    assert(js.eval("events.join(',')").to_string() ==
           "parent-dcl,child-inline,grandchild-inline,grandchild-win-load,grandchild-el-load,child-win-load,child-el-load,parent-win-load");

    // 2. Navigating `srcdoc` replaces Document while keeping `WindowProxy` === `savedProxy`.
    js.eval(R"js(
        window.oldChildDoc = f1.contentDocument;
        f1.setAttribute('name', 'beta');
        f1.setAttribute('srcdoc', '<p id="updated">second</p>');
    )js");
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
    assert(js.eval("f1.contentWindow === savedProxy").is_truthy());
    assert(js.eval("f1.contentDocument !== oldChildDoc").is_truthy());
    assert(js.eval("f1.contentDocument.getElementById('updated').textContent").to_string() == "second");
    assert(js.eval("window.beta === savedProxy && window.alpha === undefined").is_truthy());

    // 3. Detaching the iframe marks the child window closed and clears contentWindow/contentDocument.
    js.eval("f1.remove();");
    assert(js.eval("f1.contentWindow === null && f1.contentDocument === null").is_truthy());
    assert(js.eval("savedProxy.closed === true").is_truthy());
    assert(js.eval("window.length === 0").is_truthy());

    // 4. Recursive self-nesting depth cap prevents unbounded recursion.
    const auto tmp_dir = std::filesystem::temp_directory_path() / "litebird_phase42_depth_test";
    std::filesystem::create_directories(tmp_dir);
    {
        std::ofstream ofs(tmp_dir / "loop.html");
        ofs << "<!doctype html><script>top.depthCount = (top.depthCount || 0) + 1;</script>"
               "<iframe src=\"/loop.html\"></iframe>";
    }
    ResourceLoader::set_origin_root_for_testing("https://loop.example.test", tmp_dir.string());
    {
        auto loop_doc = std::make_shared<Document>();
        loop_doc->set_url("https://loop.example.test/loop.html");
        JsInterpreter loop_js(loop_doc);
        loop_js.load_page("<script>window.depthCount = 0;</script><iframe src='/loop.html'></iframe>", true);
        loop_js.notify_document_loaded();
        EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
        const double depth_seen = loop_js.eval("depthCount").to_number();
        assert(depth_seen == 16.0);
    }
    ResourceLoader::clear_origin_roots_for_testing();
    std::filesystem::remove_all(tmp_dir);
}

TEST(iframe_sandbox_origin_isolation_and_postmessage) {
    using namespace litebird;
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(1));

    const auto tmp_dir = std::filesystem::temp_directory_path() / "litebird_phase42_origin_test";
    std::filesystem::create_directories(tmp_dir / "public");
    {
        std::ofstream ofs(tmp_dir / "secret.txt");
        ofs << "OUTSIDE_ROOT_SECRET";
    }
    {
        std::ofstream ofs(tmp_dir / "public" / "xorig.html");
        ofs << R"html(<!doctype html><script>
            window.addEventListener('message', function(e) {
                e.source.postMessage({echo: e.data, seenOrigin: e.origin, myOrigin: window.origin}, e.origin);
            });
        </script><p id="secret">cross-origin-body</p>)html";
    }

    ResourceLoader::set_origin_root_for_testing("https://other.example.test", (tmp_dir / "public").string());

    // Path traversal (both `..` and a symlink pointing outside the mapped root) must be rejected.
    std::error_code sym_ec;
    std::filesystem::create_symlink(tmp_dir / "secret.txt", tmp_dir / "public" / "escape_link.txt", sym_ec);
    {
        ResourceRequestContext ctx;
        ctx.document_url = "https://other.example.test/xorig.html";
        ResourceResponse escaped = ResourceLoader::load_for_page("https://other.example.test/../secret.txt", ctx);
        assert(!escaped.success && escaped.body.empty());
        if (!sym_ec) {
            ResourceResponse sym_escaped = ResourceLoader::load_for_page("https://other.example.test/escape_link.txt", ctx);
            assert(!sym_escaped.success && sym_escaped.status_code == 404 && sym_escaped.body.empty());
        }
    }

    auto doc = std::make_shared<Document>();
    doc->set_url("https://main.example.test/page.html");
    JsInterpreter js(doc);

    const std::string html = R"html(<!doctype html><html><body>
      <iframe id="nojs" sandbox srcdoc="<script>parent.ranBlocked = true;</script><p>hi</p>"></iframe>
      <iframe id="opaque" sandbox="allow-scripts" srcdoc="<script>
        window.cookieErr = '';
        window.storageErr = '';
        try { var c = document.cookie; } catch (e) { window.cookieErr = e.name; }
        try { var s = window.localStorage; } catch (e) { window.storageErr = e.name; }
        parent.postMessage({kind: 'sandbox', origin: window.origin, cookieErr: window.cookieErr, storageErr: window.storageErr}, '*');
      </script>"></iframe>
      <iframe id="same" sandbox="allow-scripts allow-same-origin" srcdoc="<p id='ok'>allowed</p>"></iframe>
      <iframe id="xorig" src="https://other.example.test/xorig.html"></iframe>
      <script>
        window.ranBlocked = false;
        window.sandboxMsg = null;
        window.echoMsg = null;
        window.addEventListener('message', function(e) {
          if (e.data && e.data.kind === 'sandbox') {
            window.sandboxMsg = {data: e.data, origin: e.origin, fromOpaque: e.source === document.getElementById('opaque').contentWindow};
          } else if (e.data && e.data.echo) {
            window.echoMsg = {data: e.data, origin: e.origin, fromXorig: e.source === document.getElementById('xorig').contentWindow};
          }
        });
      </script>
    </body></html>)html";

    js.load_page(html, true);
    js.notify_document_loaded();
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));

    // 1. `sandbox` without `allow-scripts` blocks script execution; without `allow-same-origin`
    //    gives an opaque origin hiding `contentDocument`.
    assert(js.eval("ranBlocked === false").is_truthy());
    assert(js.eval("document.getElementById('nojs').contentDocument === null").is_truthy());
    assert(js.eval("document.getElementById('nojs').sandbox.contains('allow-scripts') === false").is_truthy());
    assert(js.eval("document.getElementById('opaque').contentDocument === null").is_truthy());
    assert(js.eval("sandboxMsg !== null && sandboxMsg.origin === 'null' && sandboxMsg.fromOpaque === true").is_truthy());
    assert(js.eval("sandboxMsg.data.origin === 'null'").is_truthy());
    assert(js.eval("sandboxMsg.data.cookieErr === 'SecurityError'").is_truthy());
    assert(js.eval("sandboxMsg.data.storageErr === 'SecurityError'").is_truthy());

    // 2. `sandbox="allow-scripts allow-same-origin"` preserves same-origin DOM access.
    assert(js.eval("document.getElementById('same').contentDocument.getElementById('ok').textContent").to_string() == "allowed");

    // 3. Cross-origin iframe hides `contentDocument`, blocks `contentWindow.document`,
    //    enforces `targetOrigin` on `postMessage`, and delivers matching `postMessage`.
    assert(js.eval("document.getElementById('xorig').contentDocument === null").is_truthy());
    assert(js.eval("document.getElementById('xorig').getSVGDocument() === null").is_truthy());
    assert(js.eval("(function(){ try { void document.getElementById('xorig').contentWindow.document; return 'NONE'; } catch(e) { return e.name; } })()").to_string() == "SecurityError");

    // Mismatched targetOrigin must be dropped silently; matching targetOrigin must round-trip.
    js.eval(R"js(
        var xw = document.getElementById('xorig').contentWindow;
        xw.postMessage('wrong-origin', 'https://wrong.example.test');
        xw.postMessage('hello-xorig', 'https://other.example.test');
    )js");
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
    assert(js.eval("echoMsg !== null && echoMsg.origin === 'https://other.example.test' && echoMsg.fromXorig === true").is_truthy());
    assert(js.eval("echoMsg.data.echo === 'hello-xorig' && echoMsg.data.seenOrigin === 'https://main.example.test' && echoMsg.data.myOrigin === 'https://other.example.test'").is_truthy());

    ResourceLoader::clear_origin_roots_for_testing();
    std::filesystem::remove_all(tmp_dir);
}

TEST(iframe_cross_realm_wrappers_gc_and_subdocument_rendering) {
    using namespace litebird;
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(1));

    // 1. Zero-leak GC check across parent + nested child + grandchild realms,
    //    cross-realm node adoption, relevant-realm constructors, and closures.
    JsIntrinsics::get();
    Heap& heap = Heap::default_heap();
    heap.collect_garbage();
    heap.collect_garbage();
    const std::size_t permanent_cells = heap.stats().alive_cells;

    for (int round = 0; round < 3; ++round) {
        {
            auto doc = std::make_shared<Document>();
            doc->set_url("https://gc.example.test/root.html");
            JsInterpreter js(doc);
            js.load_page(R"html(<!doctype html><html><body>
              <iframe id="child" srcdoc="<div id='item'>from-child</div><iframe srcdoc='<p>leaf</p>'></iframe><script>class CustomBox {} window.box = new CustomBox();</script>"></iframe>
            </body></html>)html", true);
            js.notify_document_loaded();
            EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));

            assert(js.eval("var cw = document.getElementById('child').contentWindow; var cd = document.getElementById('child').contentDocument; cd.body instanceof cw.HTMLElement").is_truthy());
            assert(js.eval("(new cw.Comment('x')).ownerDocument === cd && (new cw.Text('y')).ownerDocument === cd").is_truthy());
            assert(js.eval("(new cw.DOMParser()).parseFromString('<r/>', 'text/xml').URL === 'about:srcdoc'").is_truthy());
            assert(js.eval("var item = cd.getElementById('item'); document.body.appendChild(item); item.ownerDocument === document").is_truthy());
        }
        EventLoop::default_loop().run_until_idle(std::chrono::seconds(1));
        heap.collect_garbage();
        heap.collect_garbage();
        assert(heap.stats().alive_cells == permanent_cells);
    }

    // 2. XHTML `load_xml_page` executes `<![CDATA[...]]>` scripts in XML mode,
    //    and `LayoutTree` + `BitmapRenderer` paints an `<iframe>`'s subdocument pixels.
    {
        auto xdoc = std::make_shared<Document>();
        xdoc->set_url("https://xhtml.example.test/app.xhtml");
        xdoc->set_viewport_size(200.0f, 160.0f);
        JsInterpreter xjs(xdoc);
        const std::string xhtml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml">
<body style="margin:0; background:#ffffff;">
  <iframe id="paintFrame" width="80" height="60" style="border:none; display:block;"
          srcdoc="&lt;body style='margin:0'&gt;&lt;div style='width:80px;height:60px;background:#ff0000'&gt;&lt;/div&gt;&lt;/body&gt;"></iframe>
  <script><![CDATA[
    window.xmlModeTag = document.createElement('MixedCase').tagName;
  ]]></script>
</body>
</html>)xml";
        xjs.load_xml_page(xhtml, "application/xhtml+xml", true);
        xjs.notify_document_loaded();
        EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
        assert(xjs.eval("xmlModeTag").to_string() == "MixedCase");

        StyleResolver resolver = DocumentStyles::build_resolver(*xdoc, &xdoc->stylesheet_cache(), 200.0f, 160.0f);
        auto layout_root = LayoutTree::build(xdoc, resolver, 200.0f, 160.0f);
        DisplayList dl = DisplayList::build(layout_root);
        BitmapRenderer bmp(200, 160);
        bmp.clear(Color::white());
        bmp.render(dl);

        const Color inside = bmp.get_pixel(20, 20);
        const Color outside = bmp.get_pixel(120, 20);
        assert(inside.r == 255 && inside.g == 0 && inside.b == 0);
        assert(outside.r == 255 && outside.g == 255 && outside.b == 255);
    }

    EventLoop::default_loop().run_until_idle(std::chrono::seconds(1));
    heap.collect_garbage();
    heap.collect_garbage();
    assert(heap.stats().alive_cells == permanent_cells);
}

int main() {
    std::cout << "\n=== Running Litebird Browser Engine Test Suite ===\n\n";

    RUN_TEST(color_parsing);
    RUN_TEST(css_color_keywords_match_the_css_specification);
    RUN_TEST(html_parser);
    RUN_TEST(css_parser_and_specificity);
    RUN_TEST(style_resolver_and_cascade);
    RUN_TEST(style_resolver_indexed_candidates_preserve_cascade);
    RUN_TEST(percentage_font_size_and_line_height);
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
    RUN_TEST(url_parser_follows_the_url_standard);
    RUN_TEST(url_objects_and_addresses_in_the_dom);
    RUN_TEST(page_scripts_run_by_the_rules_of_a_page_load);
    RUN_TEST(get_element_by_id_answers_from_an_index);
    RUN_TEST(dom_follows_the_standard_where_jsdom_does_not);
    RUN_TEST(xml_parser_reads_xml_1_0_and_nothing_outside_the_document);
    RUN_TEST(xml_parser_bounds_what_a_hostile_document_costs);
    RUN_TEST(xml_documents_in_the_dom);
    RUN_TEST(xml_serializer_writes_a_tree_that_reads_back_the_same);
    RUN_TEST(inner_html_of_an_xml_document_is_xml);
    RUN_TEST(dom_names_are_checked_by_the_dom_standard);
    RUN_TEST(a_document_keeps_one_doctype_and_one_element_in_that_order);
    RUN_TEST(foreign_attributes_from_the_html_parser_have_namespaces);
    RUN_TEST(xml_dom_follows_the_standard_where_jsdom_does_not);
    RUN_TEST(selectors_know_namespaces);
    RUN_TEST(attribute_lists_keep_their_order_and_stay_fast);
    RUN_TEST(xml_http_request_answers_data_urls_and_nothing_else);
    RUN_TEST(computed_style_answers_for_every_property);
    RUN_TEST(ranges_follow_the_tree);
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
    RUN_TEST(gc_keeps_state_reachable_only_from_listeners_and_frames);
    RUN_TEST(deep_nesting_never_exhausts_the_native_stack);
    RUN_TEST(dom_insertion_cannot_make_a_node_its_own_ancestor);
    RUN_TEST(dom_trees_of_any_depth_can_be_walked_and_released);
    RUN_TEST(html_parser_nesting_limit_holds_for_every_kind_of_element);
    RUN_TEST(html_parser_void_element_with_a_slash_closes_nothing_else);
    RUN_TEST(html_parser_end_tags_close_html_elements_only);
    RUN_TEST(html_parser_formatting_elements_follow_the_standard);
    RUN_TEST(html_parser_text_in_a_table_follows_the_standard);
    RUN_TEST(html_parser_frameset_follows_the_frameset_ok_flag);
    RUN_TEST(html_parser_fragments_for_every_kind_of_context);
    RUN_TEST(html_parser_reads_malformed_markup_as_written);
    RUN_TEST(inner_html_is_parsed_as_the_content_of_its_element);
    RUN_TEST(css_values_survive_hostile_nesting);
    RUN_TEST(canvas_and_svg_bitmaps_are_bounded);
    RUN_TEST(replacing_children_detaches_the_old_ones);
    RUN_TEST(inline_children_know_their_lines_in_a_paragraph_of_any_length);
    RUN_TEST(many_sized_floats_pack_without_escaping_the_box);
    RUN_TEST(floats_of_no_height_cost_nothing_and_still_count);
    RUN_TEST(js_strings_are_shared_and_read_in_constant_time);
    RUN_TEST(js_string_owes_a_trailing_high_half_until_it_is_read);
    RUN_TEST(js_string_built_half_a_pair_at_a_time_costs_what_it_writes);
    RUN_TEST(js_string_matches_a_utf16_model_under_random_edits);
    RUN_TEST(js_property_maps_and_arrays_keep_their_order_through_front_edits);
    RUN_TEST(js_strings_can_be_used_from_several_threads);
    RUN_TEST(phase19_js_compatibility_basics);
    RUN_TEST(js_value_payload_is_compact_and_copy_safe);
    RUN_TEST(strict_equality_does_not_coerce);
    RUN_TEST(image_decoder_rejects_hostile_dimensions);
    RUN_TEST(overflow_hidden_clips_painting);
    RUN_TEST(float_shrinks_inline_line_boxes);
    RUN_TEST(float_left_and_right_can_stack_side_by_side);
    RUN_TEST(table_colspan_and_rowspan_geometry);
    RUN_TEST(table_border_collapse_and_spacing);

    // Phase 31: CSS whitespace trimming.
    RUN_TEST(css_text_trim_matches_the_character_by_character_model);
    RUN_TEST(css_whitespace_does_not_change_selector_or_value_meaning);
    RUN_TEST(css_long_whitespace_runs_keep_their_meaning);

    // CSS <an+b> formula parsing (finding 35's tail).
    RUN_TEST(nth_child_formula_parsing);

    // Phase 35: attribute-list end removals.
    RUN_TEST(attribute_list_end_removals_keep_order_and_index);
    RUN_TEST(attribute_list_front_removal_is_linear_in_practice);

    // Phase 39: the HTML tokenizer's repeated attribute names.
    RUN_TEST(tokenizer_repeated_attribute_names_keep_the_first);
    RUN_TEST(tokenizer_duplicate_attribute_check_is_linear);

    // Phase 33: page resource loads are origin-aware.
    RUN_TEST(resource_loader_page_requests_are_origin_aware);
    RUN_TEST(fetch_and_workers_use_the_document_origin);
    RUN_TEST(fetch_relative_requests_use_the_document_base_url);

    // Phase 34: a function as the prototype of a function.
    RUN_TEST(js_a_function_is_the_prototype_of_a_function);
    RUN_TEST(js_classes_and_their_parents_are_collected);
    RUN_TEST(js_classes_compiled_to_es5_run);
    RUN_TEST(js_a_class_that_extends_a_dom_interface_has_it_as_prototype);
    RUN_TEST(js_interface_objects_are_collected_with_their_page);

    // Phase 36: usable real-world browser & Finding 44 security tests.
    RUN_TEST(deflate_zlib_and_gzip_roundtrip_and_bomb_limits);
    RUN_TEST(png_and_svg_subresource_decoding_and_roundtrip);
    RUN_TEST(img_subresources_enforce_document_origin_security);
    RUN_TEST(document_styles_load_external_links_base_href_media_and_imports);
    RUN_TEST(external_classic_and_deferred_scripts_execute_in_html_order);
    RUN_TEST(live_dom_geometry_and_layout_invalidation);
    RUN_TEST(truetype_font_rasterizer_and_utf8_proportional_metrics);
    RUN_TEST(web_browser_server_and_http11_gzip_chunked_cors_and_gui_e2e);

    // Phase 38: real-web JPEG/GIF decoders, Canvas 2D, and SVG vector rendering.
    RUN_TEST(jpeg_decoder_baseline_progressive_and_guards);
    RUN_TEST(gif_decoder_87a_89a_interlaced_and_animation);
    RUN_TEST(canvas2d_ctm_scanline_thick_stroke_clip_and_js_bindings);
    RUN_TEST(svg_real_world_paths_arcs_gradients_use_clip_and_guards);

    // Phase 40: Worker MIME/module policy, EventTarget dispatch, and lifecycle safety.
    RUN_TEST(worker_mime_policy_enforces_javascript_content_types);
    RUN_TEST(worker_options_module_policy_and_global_scope);
    RUN_TEST(worker_event_target_error_and_message_dispatch);
    RUN_TEST(worker_concurrent_startup_nested_and_clean_shutdown);

    // Phase 42: Nested <iframe> browsing contexts, sandbox/origin isolation, and XHTML execution.
    RUN_TEST(iframe_browsing_context_hierarchy_and_navigation);
    RUN_TEST(iframe_sandbox_origin_isolation_and_postmessage);
    RUN_TEST(iframe_cross_realm_wrappers_gc_and_subdocument_rendering);

    std::cout << "\n\033[1;32mAll " << g_tests_run
              << " engine test suites passed successfully!\033[0m\n\n";
    return 0;
}
