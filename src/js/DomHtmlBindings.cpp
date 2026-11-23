// HTMLElement and the element interfaces that are not form controls: links,
// images, scripts, tables, templates, slots, media, canvas, dialog; and the
// SVG element interfaces.

#include "litebird/common/Base64.hpp"
#include "litebird/js/DomBindings.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/render/Canvas2D.hpp"
#include "litebird/render/ImageDecoder.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace litebird {
namespace dom {

namespace {

bool is_html_node(const Node& node) {
    return node.is_element() && !static_cast<const Element&>(node).is_foreign();
}
bool is_svg_node(const Node& node) {
    return node.is_element() && static_cast<const Element&>(node).element_namespace() == ElementNamespace::SVG;
}

LITEBIRD_DOM_TAG(is_table, "table")
LITEBIRD_DOM_TAG(is_row, "tr")
LITEBIRD_DOM_TAG(is_template, "template")
LITEBIRD_DOM_TAG(is_slot, "slot")
LITEBIRD_DOM_TAG(is_canvas, "canvas")
LITEBIRD_DOM_TAG(is_dialog, "dialog")
LITEBIRD_DOM_TAG(is_image, "img")
LITEBIRD_DOM_TAG(is_script, "script")

bool is_table_section(const Node& node) {
    return is_html_element(node, "tbody") || is_html_element(node, "thead") || is_html_element(node, "tfoot");
}
bool is_table_cell(const Node& node) {
    return is_html_element(node, "td") || is_html_element(node, "th");
}
bool is_hyperlink(const Node& node) {
    return is_html_element(node, "a") || is_html_element(node, "area");
}
bool is_media(const Node& node) {
    return is_html_element(node, "audio") || is_html_element(node, "video");
}

using Html = ElementMembers<&is_html_node>;

// ---------------------------------------------------------------------------
// Reflection by description
// ---------------------------------------------------------------------------

// Defines reflecting attributes from a compact description: words of the
// form kind:property[:attribute][=default]. Kinds: s string, b boolean,
// l long, u unsigned long, p unsigned long that is never zero, r URL.
void reflect(Iface& I, const char* description) {
    Html H(I);
    for (const std::string& word : split_tokens(description)) {
        std::string spec = word;
        std::string fallback;
        const auto equals = spec.find('=');
        if (equals != std::string::npos) {
            fallback = spec.substr(equals + 1);
            spec = spec.substr(0, equals);
        }
        const char kind = spec[0];
        spec = spec.substr(2);
        std::string property = spec, attribute;
        const auto colon = spec.find(':');
        if (colon != std::string::npos) {
            property = spec.substr(0, colon);
            attribute = spec.substr(colon + 1);
        }
        // The names are copied by what they are handed to.
        const char* name = property.c_str();
        const char* attr = attribute.empty() ? nullptr : attribute.c_str();
        const long number = fallback.empty() ? 0 : std::atol(fallback.c_str());
        switch (kind) {
            case 's': H.string(name, attr); break;
            case 'b': H.flag(name, attr); break;
            case 'l': H.integer(name, number, attr); break;
            case 'u': H.unsigned_integer(name, static_cast<unsigned long>(number), false, attr); break;
            case 'p': H.unsigned_integer(name, static_cast<unsigned long>(number), true, attr); break;
            case 'r': H.url(name, attr); break;
            default: break;
        }
    }
}

void setup_reflecting(Iface& I) {
    if (I.def.data) reflect(I, static_cast<const char*>(I.def.data));
}

// ---------------------------------------------------------------------------
// HTMLElement
// ---------------------------------------------------------------------------

bool has_default_tab_stop(const Element& element) {
    const std::string& tag = element.tag_name();
    if (tag == "a" || tag == "area") return element.has_attribute("href");
    if (tag == "button" || tag == "input" || tag == "select" || tag == "textarea" || tag == "iframe" ||
        tag == "summary") {
        return true;
    }
    if (const std::string* editable = element.attribute("contenteditable")) return ascii_lower(*editable) != "false";
    return false;
}

void define_common_element_members(Iface& I, bool (*accepts)(const Node&)) {
    // What HTML and SVG elements share.
    I.node_accessor(accepts, "style",
        [](Realm& r, Node& self) { return inline_style(r, static_cast<Element&>(self)); },
        [](Realm& r, Node& self, const JsValue& value) { set_inline_css_text(r, static_cast<Element&>(self), to_str(value)); });
    I.node_getter(accepts, "dataset", [](Realm& r, Node& self) {
        auto& element = static_cast<Element&>(self);
        return cached(wrap(r, self), "dataset", [&]() { return make_dataset(r, element); });
    });
    I.node_accessor(accepts, "tabIndex",
        [](Realm&, Node& self) {
            auto& element = static_cast<Element&>(self);
            long value = 0;
            const std::string* text = element.attribute("tabindex");
            if (text && parse_html_integer(*text, value)) return num(static_cast<double>(value));
            return num(!element.is_foreign() && has_default_tab_stop(element) ? 0 : -1);
        },
        [](Realm&, Node& self, const JsValue& value) {
            static_cast<Element&>(self).set_attribute("tabindex", std::to_string(to_long(value)));
        });
    I.node_method(accepts, "focus", 0, [](Realm& r, Node& self, Args) {
        focus_element(r, &static_cast<Element&>(self));
        return undefined();
    });
    I.node_method(accepts, "blur", 0, [](Realm& r, Node& self, Args) {
        auto document = document_of(r, self);
        if (document && document->focused_element().get() == &self) focus_element(r, nullptr, document);
        return undefined();
    });
    I.node_accessor(accepts, "nonce",
        [](Realm&, Node& self) { return str(static_cast<Element&>(self).get_attribute("nonce")); },
        [](Realm&, Node& self, const JsValue& value) { static_cast<Element&>(self).set_attribute("nonce", to_str(value)); });
    I.node_accessor(accepts, "autofocus",
        [](Realm&, Node& self) { return boolean(static_cast<Element&>(self).has_attribute("autofocus")); },
        [](Realm&, Node& self, const JsValue& value) {
            auto& element = static_cast<Element&>(self);
            if (to_bool(value)) element.set_attribute("autofocus", "");
            else element.remove_attribute("autofocus");
        });
    define_event_handlers(I, false);
}

void setup_html_element(Iface& I) {
    Html H(I);
    reflect(I, "s:title s:lang b:hidden s:accessKey:accesskey b:inert s:inputMode:inputmode "
               "s:enterKeyHint:enterkeyhint s:autocapitalize s:popover s:role");
    H.keyword("dir", {"ltr", "rtl", "auto"}, "", "");
    H.accessor("draggable",
        [](Realm&, Element& el) {
            const std::string value = ascii_lower(el.get_attribute("draggable"));
            if (value == "true") return boolean(true);
            if (value == "false") return boolean(false);
            return boolean(el.tag_name() == "img" || (el.tag_name() == "a" && el.has_attribute("href")));
        },
        [](Realm&, Element& el, const JsValue& value) { el.set_attribute("draggable", to_bool(value) ? "true" : "false"); });
    H.accessor("spellcheck",
        [](Realm&, Element& el) { return boolean(ascii_lower(el.get_attribute("spellcheck")) != "false"); },
        [](Realm&, Element& el, const JsValue& value) { el.set_attribute("spellcheck", to_bool(value) ? "true" : "false"); });
    H.accessor("translate",
        [](Realm&, Element& el) { return boolean(ascii_lower(el.get_attribute("translate")) != "no"); },
        [](Realm&, Element& el, const JsValue& value) { el.set_attribute("translate", to_bool(value) ? "yes" : "no"); });
    H.accessor("contentEditable",
        [](Realm&, Element& el) {
            const std::string* text = el.attribute("contenteditable");
            if (!text) return str("inherit");
            const std::string value = ascii_lower(*text);
            if (value.empty() || value == "true") return str("true");
            if (value == "false") return str("false");
            if (value == "plaintext-only") return str("plaintext-only");
            return str("inherit");
        },
        [](Realm& r, Element& el, const JsValue& value) {
            const std::string text = ascii_lower(to_str(value));
            if (text == "inherit") el.remove_attribute("contenteditable");
            else if (text == "true" || text == "false" || text == "plaintext-only") el.set_attribute("contenteditable", text);
            else throw_dom(r, "SyntaxError", "The value provided ('" + text + "') is not one of 'true', 'false', 'plaintext-only', or 'inherit'.");
        });
    H.getter("isContentEditable", [](Realm&, Element& el) {
        for (std::shared_ptr<Node> at = el.shared_from_this(); at && at->is_element(); at = at->parent()) {
            if (const std::string* text = static_cast<Element&>(*at).attribute("contenteditable")) {
                const std::string value = ascii_lower(*text);
                if (value.empty() || value == "true" || value == "plaintext-only") return boolean(true);
                if (value == "false") return boolean(false);
            }
        }
        return boolean(false);
    });
    // There is no rendered text to return: the text of the tree stands in.
    for (const char* name : {"innerText", "outerText"}) {
        H.accessor(name,
            [](Realm&, Element& el) { return str(el.text_content()); },
            [](Realm&, Element& el, const JsValue& value) { set_text_content(el, to_str_null_empty(value)); });
    }
    H.method("click", 0, [](Realm& r, Element& el, Args) {
        // A disabled control ignores it.
        if (el.has_attribute("disabled") && (el.tag_name() == "button" || el.tag_name() == "input" ||
                                             el.tag_name() == "select" || el.tag_name() == "textarea")) {
            return undefined();
        }
        JsValue event = make_event(r, "PointerEvent", "click", true, true, {{"view", window_of(r)}}, true);
        dispatch(r, wrap(r, el), event, false);
        return undefined();
    });
    // Offsets come from the live layout box; detached elements return 0/null.
    auto ensure_html_layout = [](Realm& r, Element& el) -> bool {
        if (!el.is_connected()) return false;
        if (auto doc = document_of(r, el)) {
            doc->ensure_layout();
        }
        return true;
    };
    H.getter("offsetParent", [ensure_html_layout](Realm& r, Element& el) {
        if (!ensure_html_layout(r, el)) return null();
        if (!el.is_foreign() && (el.tag_name() == "body" || el.tag_name() == "html")) return null();
        const Rect box = el.bounding_box();
        if (box.width <= 0 && box.height <= 0) return null();
        auto document = document_of(r, el);
        return wrap(r, document ? document->body() : nullptr);
    });
    H.getter("offsetTop", [ensure_html_layout](Realm& r, Element& el) {
        if (!ensure_html_layout(r, el)) return num(0);
        return num(std::round(el.bounding_box().y));
    });
    H.getter("offsetLeft", [ensure_html_layout](Realm& r, Element& el) {
        if (!ensure_html_layout(r, el)) return num(0);
        return num(std::round(el.bounding_box().x));
    });
    H.getter("offsetWidth", [ensure_html_layout](Realm& r, Element& el) {
        if (!ensure_html_layout(r, el)) return num(0);
        return num(std::round(el.bounding_box().width));
    });
    H.getter("offsetHeight", [ensure_html_layout](Realm& r, Element& el) {
        if (!ensure_html_layout(r, el)) return num(0);
        return num(std::round(el.bounding_box().height));
    });
    define_common_element_members(I, &is_html_node);
}

// SVGAnimatedString is a live view of an element's class attribute. Keep the
// node and its wrapper alive when script retains the view on its own, just as
// DOMTokenList does. Animation itself is not implemented for this attribute.
struct SvgAnimatedClassState : JsExotic {
    std::shared_ptr<Element> element;
    void trace(Heap& heap) override { trace_node(heap, element.get()); }
};

void setup_svg_animated_string(Iface& I) {
    I.accessor("baseVal", [](Realm&) {
        const auto& state = this_state<SvgAnimatedClassState>("SVGAnimatedString");
        const auto* text = state.element->attribute("class");
        return str(text ? *text : "");
    }, [](Realm&, const JsValue& value) {
        auto& state = this_state<SvgAnimatedClassState>("SVGAnimatedString");
        state.element->set_attribute("class", to_str(value));
    });
    I.getter("animVal", [](Realm&) {
        const auto& state = this_state<SvgAnimatedClassState>("SVGAnimatedString");
        const auto* text = state.element->attribute("class");
        return str(text ? *text : "");
    });
}

// A viewBox has one base rectangle and an animated rectangle. Both are live
// views of the same element. Without SVG animation the animated value follows
// the base, but is a distinct, read-only object.
struct SvgViewBoxState {
    std::shared_ptr<Element> element;
    std::array<float, 4> values{};
    std::string source;
    bool has_attribute{false};
    bool loaded{false};
};

struct SvgAnimatedRectState : JsExotic {
    std::shared_ptr<SvgViewBoxState> box;
    void trace(Heap& heap) override { trace_node(heap, box->element.get()); }
};

struct SvgRectState : JsExotic {
    std::shared_ptr<SvgViewBoxState> box;
    bool animated{false};
    void trace(Heap& heap) override { trace_node(heap, box->element.get()); }
};

bool svg_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
bool svg_digit(char c) { return c >= '0' && c <= '9'; }

// SVG's number grammar permits adjacent signs and decimal points (1-2 or
// 1.2.3), as well as comma/whitespace separators. strtof alone is not the
// grammar: it would also accept infinities, hex numbers, and NaN.
// SVG's parser accumulates decimal digits in float precision, including
// the successive powers of one tenth. This is observable for values such as
// 0.333333; parsing directly as a double and narrowing rounds differently.
float svg_decimal_float(const std::string& text) {
    std::size_t at = 0;
    bool negative = false;
    if (text[at] == '+' || text[at] == '-') { negative = text[at] == '-'; ++at; }
    float value = 0.0f;
    while (at < text.size() && svg_digit(text[at])) {
        value = value * 10.0f + static_cast<float>(text[at++] - '0');
    }
    if (at < text.size() && text[at] == '.') {
        ++at;
        float scale = 0.1f;
        float fraction = 0.0f;
        while (at < text.size() && svg_digit(text[at])) {
            const float part = static_cast<float>(text[at++] - '0') * scale;
            fraction += part;
            scale *= 0.1f;
        }
        value += fraction;
    }
    if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
        const long exponent = std::strtol(text.c_str() + at + 1, nullptr, 10);
        if (exponent < -1000) value = 0.0f;
        else if (exponent > 1000) value = std::numeric_limits<float>::infinity();
        else value *= std::pow(10.0f, static_cast<float>(exponent));
    }
    return negative ? -value : value;
}

bool parse_svg_view_box(const std::string& text, std::array<float, 4>& out) {
    std::array<float, 4> parsed{};
    std::size_t at = 0;
    for (std::size_t i = 0; i < parsed.size(); ++i) {
        while (at < text.size() && svg_space(text[at])) ++at;
        const std::size_t start = at;
        if (at < text.size() && (text[at] == '+' || text[at] == '-')) ++at;
        bool digits = false;
        while (at < text.size() && svg_digit(text[at])) { ++at; digits = true; }
        if (at < text.size() && text[at] == '.') {
            ++at;
            const std::size_t fraction = at;
            while (at < text.size() && svg_digit(text[at])) { ++at; digits = true; }
            if (at == fraction) return false;
        }
        if (!digits) return false;
        if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
            ++at;
            if (at < text.size() && (text[at] == '+' || text[at] == '-')) ++at;
            const std::size_t exponent = at;
            while (at < text.size() && svg_digit(text[at])) ++at;
            if (at == exponent) return false;
        }
        const std::string number = text.substr(start, at - start);
        if (!std::isfinite(std::strtof(number.c_str(), nullptr))) return false;
        parsed[i] = svg_decimal_float(number);
        if (!std::isfinite(parsed[i])) return false;
        while (at < text.size() && svg_space(text[at])) ++at;
        if (i + 1 < parsed.size() && at < text.size() && text[at] == ',') ++at;
    }
    if (at != text.size()) return false;
    out = parsed;
    return true;
}

void sync_svg_view_box(SvgViewBoxState& state) {
    const std::string* attribute = state.element->attribute("viewBox");
    if (state.loaded && state.has_attribute == (attribute != nullptr) &&
        (!attribute || *attribute == state.source)) return;
    state.loaded = true;
    state.has_attribute = attribute != nullptr;
    state.source = attribute ? *attribute : std::string();
    state.values = {};
    if (attribute) parse_svg_view_box(*attribute, state.values);
}

// Chromium serializes the float with six significant digits. Fixed notation
// is used from 1e-6 through values below 1e6; exponent notation keeps six
// significant digits and omits leading zeroes in the exponent.
std::string serialize_svg_float(float value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return std::signbit(value) ? "-Infinity" : "Infinity";
    if (value == 0) return "0";
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.5e", static_cast<double>(value));
    std::string text(buffer);
    const auto e = text.find('e');
    const int exponent = std::atoi(text.c_str() + e + 1);
    const bool negative = text[0] == '-';
    std::string mantissa = text.substr(negative ? 1 : 0, e - (negative ? 1 : 0));
    if (exponent < -6 || exponent >= 6) {
        return (negative ? "-" : "") + mantissa + "e" + (exponent >= 0 ? "+" : "-") +
               std::to_string(std::abs(exponent));
    }
    mantissa.erase(1, 1); // decimal point: six digits remain
    const int point = exponent + 1;
    std::string result;
    if (point <= 0) result = "0." + std::string(static_cast<std::size_t>(-point), '0') + mantissa;
    else if (point >= static_cast<int>(mantissa.size())) result = mantissa + std::string(point - mantissa.size(), '0');
    else result = mantissa.substr(0, point) + "." + mantissa.substr(point);
    if (result.find('.') != std::string::npos) {
        while (!result.empty() && result.back() == '0') result.pop_back();
        if (!result.empty() && result.back() == '.') result.pop_back();
    }
    return (negative ? "-" : "") + result;
}

void setup_svg_rect(Iface& I) {
    static const char* names[] = {"x", "y", "width", "height"};
    for (std::size_t i = 0; i < 4; ++i) {
        I.accessor(names[i], [i](Realm&) {
            auto& state = this_state<SvgRectState>("SVGRect");
            sync_svg_view_box(*state.box);
            return num(state.box->values[i]);
        }, [i](Realm& realm, const JsValue& value) {
            auto& state = this_state<SvgRectState>("SVGRect");
            // WebIDL converts before the read-only animated rectangle refuses.
            const double number = to_double(value);
            const float converted = std::isfinite(number) && std::fabs(number) > std::numeric_limits<float>::max()
                ? std::copysign(std::numeric_limits<float>::infinity(), number)
                : static_cast<float>(number);
            if (state.animated) throw_dom(realm, "NoModificationAllowedError", "The animated rectangle is read-only.");
            sync_svg_view_box(*state.box);
            state.box->values[i] = converted;
            std::string text;
            for (float n : state.box->values) {
                if (!text.empty()) text += ' ';
                text += serialize_svg_float(n);
            }
            state.box->element->set_attribute("viewBox", text);
            state.box->source = text;
            state.box->has_attribute = true;
            state.box->loaded = true;
        });
    }
}

void setup_svg_animated_rect(Iface& I) {
    for (const auto& member : {std::pair<const char*, bool>{"baseVal", false}, {"animVal", true}}) {
        const bool animated = member.second;
        I.getter(member.first, [animated](Realm& realm) {
            auto& state = this_state<SvgAnimatedRectState>("SVGAnimatedRect");
            return cached(JsValue::current_this(), animated ? "svgRectAnim" : "svgRectBase", [&]() {
                auto rect = std::make_shared<SvgRectState>();
                rect->box = state.box;
                rect->animated = animated;
                return realm.make("SVGRect", rect);
            });
        });
    }
}

bool is_svg_svg(const Node& node) {
    return is_svg_node(node) && static_cast<const Element&>(node).tag_name() == "svg";
}

void setup_svg_svg_element(Iface& I) {
    I.node_getter(&is_svg_svg, "viewBox", [](Realm& realm, Node& node) {
        return cached(wrap(realm, node), "svgViewBox", [&]() {
            auto state = std::make_shared<SvgAnimatedRectState>();
            state->box = std::make_shared<SvgViewBoxState>();
            state->box->element = shared(static_cast<Element&>(node));
            return realm.make("SVGAnimatedRect", state);
        });
    });
}

void setup_svg_element(Iface& I) {
    I.node_getter(&is_svg_node, "className", [](Realm& r, Node& self) {
        return cached(wrap(r, self), "svgClassName", [&]() {
            auto state = std::make_shared<SvgAnimatedClassState>();
            state->element = shared(static_cast<Element&>(self));
            return r.make("SVGAnimatedString", state);
        });
    });
    I.node_getter(&is_svg_node, "ownerSVGElement", [](Realm& r, Node& self) {
        for (auto at = self.parent(); at && at->is_element(); at = at->parent()) {
            auto& element = static_cast<Element&>(*at);
            if (element.element_namespace() == ElementNamespace::SVG && element.tag_name() == "svg") return wrap(r, at);
        }
        return null();
    });
    I.node_getter(&is_svg_node, "viewportElement", [](Realm& r, Node& self) {
        for (auto at = self.parent(); at && at->is_element(); at = at->parent()) {
            auto& element = static_cast<Element&>(*at);
            if (element.element_namespace() == ElementNamespace::SVG && element.tag_name() == "svg") return wrap(r, at);
        }
        return null();
    });
    define_common_element_members(I, &is_svg_node);
}

// ---------------------------------------------------------------------------
// Links: a and area
// ---------------------------------------------------------------------------

// The URL a link points at: its href read against the document's address.
// False for a link with no href, or one that is not a URL.
bool link_url(Realm& r, Element& el, Url& url) {
    const std::string* href = el.attribute("href");
    return href && parse_url(r, el, *href, url);
}

// One part of a link's URL, to read and to assign: `a.pathname = '/x'`
// rewrites the href attribute. `absent` is what a link without a URL says.
void define_link_part(Iface& I, const char* name, std::string (Url::*get)() const,
                      void (Url::*set)(std::string_view), const char* absent = "") {
    ElementMembers<&is_hyperlink> L(I);
    L.accessor(name,
        [get, absent](Realm& r, Element& el) {
            Url url;
            return str(link_url(r, el, url) ? (url.*get)() : std::string(absent));
        },
        [set](Realm& r, Element& el, const JsValue& value) {
            Url url;
            if (!link_url(r, el, url)) return;
            (url.*set)(to_str(value));
            el.set_attribute("href", url.href());
        });
}

void setup_hyperlink(Iface& I) {
    ElementMembers<&is_hyperlink> L(I);
    L.accessor("href",
        [](Realm& r, Element& el) {
            const std::string* href = el.attribute("href");
            return str(href ? resolve_url(r, el, *href) : std::string());
        },
        [](Realm&, Element& el, const JsValue& value) { el.set_attribute("href", to_str(value)); });
    L.method("toString", 0, [](Realm& r, Element& el, Args) {
        const std::string* href = el.attribute("href");
        return str(href ? resolve_url(r, el, *href) : std::string());
    });
    L.getter("origin", [](Realm& r, Element& el) {
        Url url;
        return str(link_url(r, el, url) ? url.origin() : std::string());
    });
    define_link_part(I, "protocol", &Url::protocol, &Url::set_protocol, ":");
    define_link_part(I, "host", &Url::host_and_port, &Url::set_host);
    define_link_part(I, "hostname", &Url::hostname, &Url::set_hostname);
    define_link_part(I, "port", &Url::port_text, &Url::set_port);
    define_link_part(I, "pathname", &Url::pathname, &Url::set_pathname);
    define_link_part(I, "search", &Url::search, &Url::set_search);
    define_link_part(I, "hash", &Url::hash, &Url::set_hash);
    L.accessor("username",
        [](Realm& r, Element& el) {
            Url url;
            return str(link_url(r, el, url) ? url.username : std::string());
        },
        [](Realm& r, Element& el, const JsValue& value) {
            Url url;
            if (!link_url(r, el, url)) return;
            url.set_username(to_str(value));
            el.set_attribute("href", url.href());
        });
    L.accessor("password",
        [](Realm& r, Element& el) {
            Url url;
            return str(link_url(r, el, url) ? url.password : std::string());
        },
        [](Realm& r, Element& el, const JsValue& value) {
            Url url;
            if (!link_url(r, el, url)) return;
            url.set_password(to_str(value));
            el.set_attribute("href", url.href());
        });
    L.token_list("relList", "rel", {"noreferrer", "noopener", "opener"});
    reflect(I, "s:target s:download s:ping s:rel s:hreflang s:type s:referrerPolicy:referrerpolicy s:name s:rev "
               "s:charset s:coords s:shape s:alt b:noHref:nohref");
    L.accessor("text",
        [](Realm&, Element& el) { return str(el.text_content()); },
        [](Realm&, Element& el, const JsValue& value) { set_text_content(el, to_str(value)); });
}

// ---------------------------------------------------------------------------
// Images, scripts, canvas, media
// ---------------------------------------------------------------------------

void setup_image(Iface& I) {
    ElementMembers<&is_image> M(I);
    reflect(I, "s:alt r:src s:srcset s:sizes s:useMap:usemap b:isMap:ismap s:name s:align s:border s:decoding "
               "s:referrerPolicy:referrerpolicy r:longDesc:longdesc r:lowsrc u:hspace u:vspace");
    M.keyword("loading", {"lazy", "eager"}, "eager", "eager");
    M.accessor("crossOrigin",
        [](Realm&, Element& el) { return nullable_str(el.attribute("crossorigin")); },
        [](Realm&, Element& el, const JsValue& value) {
            if (value.is_nullish()) el.remove_attribute("crossorigin");
            else el.set_attribute("crossorigin", to_str(value));
        });
    for (const char* name : {"width", "height"}) {
        const std::string attribute = name;
        M.accessor(name,
            [attribute](Realm&, Element& el) {
                unsigned long value = 0;
                const std::string* text = el.attribute(attribute);
                if (text && parse_html_non_negative_integer(*text, value)) return num(static_cast<double>(value));
                const Rect box = el.bounding_box();
                return num(std::round(attribute == "width" ? box.width : box.height));
            },
            [attribute](Realm&, Element& el, const JsValue& value) {
                el.set_attribute(attribute, std::to_string(to_ulong(value)));
            });
    }
    M.getter("naturalWidth", [](Realm&, Element&) { return num(0); });
    M.getter("naturalHeight", [](Realm&, Element&) { return num(0); });
    M.getter("complete", [](Realm&, Element& el) {
        // Nothing is ever fetched: an image without a source is complete,
        // one with a source never arrives.
        const std::string* src = el.attribute("src");
        return boolean(!src || src->empty());
    });
    M.getter("currentSrc", [](Realm&, Element&) { return str(""); });
    M.getter("x", [](Realm&, Element& el) { return num(std::round(el.bounding_box().x)); });
    M.getter("y", [](Realm&, Element& el) { return num(std::round(el.bounding_box().y)); });
    M.method("decode", 0, [](Realm& r, Element&, Args) {
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        if (!promise.is_function()) return undefined();
        return js_call(promise.get_property("resolve"), promise, {});
    });
}

void setup_script(Iface& I) {
    ElementMembers<&is_script> S(I);
    reflect(I, "r:src s:type b:noModule:nomodule b:defer s:charset s:event s:htmlFor:for s:integrity "
               "s:referrerPolicy:referrerpolicy");
    S.accessor("async",
        [](Realm& r, Element& el) {
            if (el.has_attribute("async")) return boolean(true);
            // A script made by createElement loads asynchronously unless told otherwise.
            const JsValue* flag = el.script_object() ? slot(wrap(r, el), "sync") : nullptr;
            const JsValue* made = el.script_object() ? slot(wrap(r, el), "runnable") : nullptr;
            return boolean(made != nullptr && flag == nullptr);
        },
        [](Realm& r, Element& el, const JsValue& value) {
            set_slot(wrap(r, el), "sync", boolean(true));
            if (to_bool(value)) el.set_attribute("async", "");
            else el.remove_attribute("async");
        });
    S.accessor("crossOrigin",
        [](Realm&, Element& el) { return nullable_str(el.attribute("crossorigin")); },
        [](Realm&, Element& el, const JsValue& value) {
            if (value.is_nullish()) el.remove_attribute("crossorigin");
            else el.set_attribute("crossorigin", to_str(value));
        });
    S.accessor("text",
        [](Realm&, Element& el) {
            std::string text;
            for (const auto& child : el.children()) {
                if (child->is_text()) text += character_data(*child);
            }
            return str(text);
        },
        [](Realm&, Element& el, const JsValue& value) { set_text_content(el, to_str(value)); });
    I.static_method("supports", 1, [](Realm&, Args args) {
        const std::string type = to_str(js_arg(args, 0));
        return boolean(type == "classic" || type == "module");
    });
}

// ---------------------------------------------------------------------------
// ImageData
// ---------------------------------------------------------------------------
//
// HTML §4.12.5: the four-byte-per-pixel block a canvas reads out and writes
// back. Until Phase 139 `createImageData` answered a plain object whose `data`
// was an Array of numbers, `getImageData` did the same, `putImageData` wrote
// the whole image whatever rectangle was asked for, and `ImageData` itself did
// not exist — so `data instanceof Uint8ClampedArray` was false and a script
// could not build one to hand to a canvas. The bytes now live in a real
// `Uint8ClampedArray` (there was none to put them in before Phase 106) and the
// interface is the one the standard describes.

/// 16M pixels, the same ceiling the C++ side keeps: an area past it is refused
/// rather than allocated.
constexpr unsigned long long kMaxImageDataPixels = 16ull * 1024ull * 1024ull;

struct ImageDataState : JsExotic {
    unsigned long width{0};
    unsigned long height{0};
    /// The colour space the pixels are in. This engine has one, sRGB, and
    /// refuses the other the standard names rather than answering for a
    /// conversion it cannot make.
    std::string color_space{"srgb"};
    /// The pixels: 4 * width * height bytes of RGBA, a Uint8ClampedArray.
    JsValue data;
    void trace(Heap& heap) override { heap.mark_value(data); }
};

ImageDataState* image_data_of(const JsValue& value) { return state_of<ImageDataState>(value); }

ImageDataState& this_image_data() { return this_state<ImageDataState>("ImageData"); }

/// WebIDL `unsigned long`: any number, brought into range. A negative or a
/// fractional one is not refused here; the size check below refuses what it
/// makes impossible.
unsigned long to_unsigned_long(const JsValue& value) {
    double number = to_double(value);
    if (!std::isfinite(number)) return 0;
    number = std::trunc(number);
    const double modulo = std::fmod(number, 4294967296.0);
    const double in_range = modulo < 0 ? modulo + 4294967296.0 : modulo;
    return static_cast<unsigned long>(in_range);
}

void throw_index_size(Realm& realm, const std::string& why) {
    throw_dom(realm, "IndexSizeError", why);
}

/// The realm's `Uint8ClampedArray`, `length` bytes of zero.
JsValue clamped_array(Realm& realm, std::size_t length) {
    const JsValue& global = realm.window;
    const JsValue array_ctor = global.is_object() ? global.get_property("Uint8ClampedArray") : JsValue();
    if (!array_ctor.is_function()) {
        js_throw_type_error("ImageData needs a realm with Uint8ClampedArray.");
    }
    return js_call(array_ctor, array_ctor, {JsValue::make_number(static_cast<double>(length))});
}

/// Writes `bytes` into `array`. Through the elements rather than through an
/// intermediate Array: a 16M-pixel image is 64 MB of bytes, and an Array of
/// them would be two gigabytes of JsValue before the typed array was even made.
void store_bytes(JsValue array, const std::vector<uint8_t>& bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] == 0) continue;
        array.set_property(std::to_string(i), JsValue::make_number(bytes[i]));
    }
}

/// A new ImageData carrying `data` (its own, not a copy) and the size given.
JsValue image_data_with(Realm& realm, unsigned long width, unsigned long height, const JsValue& data,
                        const JsValue* derived, const std::string& color_space = "srgb") {
    auto state = std::make_shared<ImageDataState>();
    state->width = width;
    state->height = height;
    state->data = data;
    state->color_space = color_space;
    return realm.make("ImageData", state, derived);
}

/// A new ImageData of `width` x `height` transparent black pixels.
JsValue image_data_blank(Realm& realm, unsigned long width, unsigned long height, const JsValue* derived = nullptr,
                         const std::string& color_space = "srgb") {
    const std::size_t length = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    return image_data_with(realm, width, height, clamped_array(realm, length), derived, color_space);
}

/// A new ImageData of `width` x `height` holding `bytes`.
JsValue image_data_of_bytes(Realm& realm, unsigned long width, unsigned long height,
                            const std::vector<uint8_t>& bytes, const JsValue* derived,
                            const std::string& color_space) {
    JsValue array = clamped_array(realm, bytes.size());
    store_bytes(array, bytes);
    return image_data_with(realm, width, height, array, derived, color_space);
}


/// The `settings` argument's `colorSpace`, refused when it is not sRGB.
std::string color_space_of(const JsValue& settings) {
    if (settings.is_undefined() || settings.is_nullish()) return "srgb";
    const JsValue asked = member(settings, "colorSpace");
    if (asked.is_undefined()) return "srgb";
    const std::string name = to_str(asked);
    if (name != "srgb") {
        js_throw_type_error("ImageData: this engine stores its pixels in sRGB and cannot make the '" + name +
                            "' colour space the settings asked for.");
    }
    return "srgb";
}

/// `new ImageData(width, height)` or `new ImageData(data, sw[, sh])`.
JsValue construct_image_data(Realm& realm, const InterfaceDef&, Args args, const JsValue* derived) {
    if (args.empty() || js_arg(args, 0).is_undefined()) {
        js_throw_type_error("Failed to construct 'ImageData': 1 argument required, but only 0 present.");
    }
    const JsValue first = js_arg(args, 0);
    const JsValue& global = realm.window;
    const JsValue array_ctor = global.is_object() ? global.get_property("Uint8ClampedArray") : JsValue();
    JsValue tag_of;
    if (array_ctor.is_function() && first.is_object_like()) {
        // `[object Uint8ClampedArray]` and nothing else: the internal slot the
        // interface is defined over, not a shape that looks like one.
        const JsValue object_ctor = global.get_property("Object");
        const JsValue prototype = object_ctor.get_property("prototype");
        tag_of = js_call(prototype.get_property("toString"), first, {});
    }
    if (array_ctor.is_function() && tag_of.type == JsType::String &&
        tag_of.to_string() == "[object Uint8ClampedArray]") {
        const double length_value = first.get_property("length").to_number();
        const unsigned long long length =
            std::isfinite(length_value) && length_value > 0 ? static_cast<unsigned long long>(length_value) : 0;
        if (length == 0 || length % 4 != 0) {
            throw_index_size(realm, "Failed to construct 'ImageData': the data must be a non-zero multiple of "
                                    "four bytes.");
        }
        const JsValue second = js_arg(args, 1);
        const JsValue third = js_arg(args, 2);
        unsigned long width = 0;
        unsigned long height = 0;
        if (second.is_undefined()) {
            throw_index_size(realm, "Failed to construct 'ImageData': the width of the data given was not "
                                    "provided.");
        }
        width = to_unsigned_long(second);
        if (width == 0 || static_cast<unsigned long long>(width) * 4ull > length) {
            throw_index_size(realm, "Failed to construct 'ImageData': the data is shorter than the width given.");
        }
        if (third.is_undefined()) {
            if (length % (static_cast<unsigned long long>(width) * 4ull) != 0) {
                throw_index_size(realm, "Failed to construct 'ImageData': the data is not a whole number of "
                                        "rows of the width given.");
            }
            height = static_cast<unsigned long>(length / (static_cast<unsigned long long>(width) * 4ull));
        } else {
            height = to_unsigned_long(third);
            if (height == 0 ||
                static_cast<unsigned long long>(width) * static_cast<unsigned long long>(height) * 4ull != length) {
                throw_index_size(realm, "Failed to construct 'ImageData': the data is not width times height "
                                        "pixels of four bytes.");
            }
        }
        const std::string color_space = color_space_of(js_arg(args, 3));
        return image_data_with(realm, width, height, first, derived, color_space);
    }
    if (first.is_object_like()) {
        // The overload that takes pixels was asked for, and the object given is
        // not the one it takes: WebIDL picks the overload by the argument's
        // shape and then refuses the value (HTML §4.12.5, the ImageData(data,
        // sw[, sh]) form).
        js_throw_type_error("Failed to construct 'ImageData': the data must be a Uint8ClampedArray.");
    }
    if (args.size() < 2) {
        js_throw_type_error("Failed to construct 'ImageData': 2 arguments required.");
    }
    const unsigned long width = to_unsigned_long(first);
    const unsigned long height = to_unsigned_long(js_arg(args, 1));
    if (width == 0 || height == 0) {
        throw_index_size(realm, "Failed to construct 'ImageData': the width and height must not be zero.");
    }
    if (static_cast<unsigned long long>(width) * static_cast<unsigned long long>(height) > kMaxImageDataPixels) {
        throw_index_size(realm, "Failed to construct 'ImageData': the area asked for is past the 16M pixel "
                                "ceiling this engine keeps.");
    }
    const std::string color_space = color_space_of(js_arg(args, 2));
    return image_data_blank(realm, width, height, derived, color_space);
}

void setup_image_data(Iface& I) {
    I.getter("width", [](Realm&) { return num(static_cast<double>(this_image_data().width)); });
    I.getter("height", [](Realm&) { return num(static_cast<double>(this_image_data().height)); });
    I.getter("data", [](Realm&) { return this_image_data().data; });
    I.getter("colorSpace", [](Realm&) { return str(this_image_data().color_space); });
}

// ---------------------------------------------------------------------------
// The three canvas methods that speak ImageData
// ---------------------------------------------------------------------------

/// The size an ImageData must have, from the arguments given.
void require_size(Realm& realm, unsigned long width, unsigned long height, const char* what) {
    if (width == 0 || height == 0) {
        throw_index_size(realm, std::string(what) + ": the width and height must not be zero.");
    }
    if (static_cast<unsigned long long>(width) * static_cast<unsigned long long>(height) > kMaxImageDataPixels) {
        throw_index_size(realm, std::string(what) +
                                    ": the area asked for is past the 16M pixel ceiling this engine keeps.");
    }
}

/// `createImageData`, `getImageData` and `putImageData` on a fresh 2D context.
/// They are here, and not beside the rest of the context in Canvas2D.cpp,
/// because an ImageData's `data` is a `Uint8ClampedArray`, which only a realm
/// can make.
void install_image_data_methods(Realm& r, JsValue context_object,
                                const std::shared_ptr<CanvasRenderingContext2D>& context) {
    auto get_ctx = [context]() -> CanvasRenderingContext2D* { return context.get(); };

    context_object.set_property("createImageData", make_function(r, "createImageData", 1, [](Realm& realm, Args args) {
        if (const ImageDataState* source = image_data_of(js_arg(args, 0))) {
            // The same size, and never the same pixels: transparent black.
            return image_data_blank(realm, source->width, source->height);
        }
        const unsigned long width = to_unsigned_long(js_arg(args, 0));
        const unsigned long height = to_unsigned_long(js_arg(args, 1));
        require_size(realm, width, height, "Failed to execute 'createImageData' on 'CanvasRenderingContext2D'");
        return image_data_blank(realm, width, height);
    }));

    context_object.set_property("getImageData", make_function(r, "getImageData", 4, [get_ctx](Realm& realm, Args args) {
        require_args(args, 4, "CanvasRenderingContext2D.getImageData");
        long sx = to_long(args[0]);
        long sy = to_long(args[1]);
        long sw = to_long(args[2]);
        long sh = to_long(args[3]);
        if (sw == 0 || sh == 0) {
            throw_index_size(realm, "Failed to execute 'getImageData' on 'CanvasRenderingContext2D': the width "
                                    "and height must not be zero.");
        }
        if (sw < 0) { sx += sw; sw = -sw; }
        if (sh < 0) { sy += sh; sh = -sh; }
        if (static_cast<unsigned long long>(sw) * static_cast<unsigned long long>(sh) > kMaxImageDataPixels) {
            throw_index_size(realm, "Failed to execute 'getImageData' on 'CanvasRenderingContext2D': the area "
                                    "asked for is past the 16M pixel ceiling this engine keeps.");
        }
        const std::vector<uint8_t> rgba = get_ctx()->get_image_data_rgba(
            static_cast<int>(std::clamp(sx, -2147483647l, 2147483647l)),
            static_cast<int>(std::clamp(sy, -2147483647l, 2147483647l)),
            static_cast<int>(std::clamp(sw, 0l, 2147483647l)),
            static_cast<int>(std::clamp(sh, 0l, 2147483647l)));
        return image_data_of_bytes(realm, static_cast<unsigned long>(sw), static_cast<unsigned long>(sh), rgba,
                                   nullptr, "srgb");
    }));

    context_object.set_property("putImageData", make_function(r, "putImageData", 3, [get_ctx](Realm&, Args args) {
        require_args(args, 3, "CanvasRenderingContext2D.putImageData");
        const ImageDataState* source = image_data_of(args[0]);
        if (!source) {
            js_throw_type_error("Failed to execute 'putImageData' on 'CanvasRenderingContext2D': the value "
                                "provided is not of type 'ImageData'.");
        }
        const long dx = to_long(args[1]);
        const long dy = to_long(args[2]);
        // The dirty rectangle, and the whole image when none is given.
        long dirty_x = 0;
        long dirty_y = 0;
        long dirty_width = static_cast<long>(source->width);
        long dirty_height = static_cast<long>(source->height);
        if (args.size() > 3) dirty_x = to_long(args[3]);
        if (args.size() > 4) dirty_y = to_long(args[4]);
        if (args.size() > 5) dirty_width = to_long(args[5]);
        if (args.size() > 6) dirty_height = to_long(args[6]);
        if (dirty_width < 0) { dirty_x += dirty_width; dirty_width = -dirty_width; }
        if (dirty_height < 0) { dirty_y += dirty_height; dirty_height = -dirty_height; }
        // Clip it to the image: what is outside has no pixels to write.
        const long left = std::max(dirty_x, 0l);
        const long top = std::max(dirty_y, 0l);
        const long right = std::min(dirty_x + dirty_width, static_cast<long>(source->width));
        const long bottom = std::min(dirty_y + dirty_height, static_cast<long>(source->height));
        if (right <= left || bottom <= top) return undefined();

        const long width = right - left;
        const long height = bottom - top;
        const std::size_t image_width = source->width;
        std::vector<uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
        const JsValue& data = source->data;
        for (long y = 0; y < height; ++y) {
            for (long x = 0; x < width; ++x) {
                const std::size_t source_index =
                    (static_cast<std::size_t>(top + y) * image_width + static_cast<std::size_t>(left + x)) * 4u;
                const double values[4] = {
                    data.get_property(std::to_string(source_index + 0)).to_number(),
                    data.get_property(std::to_string(source_index + 1)).to_number(),
                    data.get_property(std::to_string(source_index + 2)).to_number(),
                    data.get_property(std::to_string(source_index + 3)).to_number(),
                };
                const std::size_t target = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                            static_cast<std::size_t>(x)) * 4u;
                for (int channel = 0; channel < 4; ++channel) {
                    const long rounded = std::isfinite(values[channel]) ? std::lround(values[channel]) : 0;
                    bytes[target + static_cast<std::size_t>(channel)] =
                        static_cast<uint8_t>(std::clamp(rounded, 0l, 255l));
                }
            }
        }
        // The transform, the clip and globalAlpha are not consulted: this is a
        // write of pixels, not a drawing operation.
        get_ctx()->put_image_data_rgba(bytes.data(), static_cast<int>(width), static_cast<int>(height),
                                       static_cast<int>(std::clamp(dx + left, -2147483647l, 2147483647l)),
                                       static_cast<int>(std::clamp(dy + top, -2147483647l, 2147483647l)));
        return undefined();
    }));
}

// --- HTMLCanvasElement.toDataURL() -----------------------------------------
//
// HTML §4.12.5.4, "serializing the bitmap": a type, the pixels encoded as that
// type, and the bytes in a `data:` URL. The engine encodes one type,
// `image/png` (`ImageDecoder::encode_png`), so every other valid type answers
// `data:,` — which is what the standard asks of a type the user agent does not
// support, and what this method answered for *every* type before it did
// anything at all.
//
// Nothing here can taint a canvas yet: `drawImage`, `ImageBitmap` and
// `Response.arrayBuffer()` are all still open, so every pixel a canvas holds
// was put there by a script's own drawing. When one of them lands it has to set
// the origin-clean flag this method must consult; until then there is no flag
// to read.

std::string ascii_trimmed(std::string_view text) {
    const auto blank = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
    };
    std::size_t first = 0;
    std::size_t last = text.size();
    while (first < last && blank(text[first])) ++first;
    while (last > first && blank(text[last - 1])) --last;
    return std::string(text.substr(first, last - first));
}

bool is_mime_token_char(char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
    switch (c) {
        case '!': case '#': case '$': case '%': case '&': case '\'': case '*': case '+':
        case '-': case '.': case '^': case '_': case '`': case '|': case '~': case '/':
            return true;
        default:
            return false;
    }
}

/// A valid MIME type whose essence is the one type this engine encodes. The
/// essence is ASCII-lowercased; parameters are carried into the URL unchanged.
bool png_mime_type(const std::string& raw, std::string& canonical) {
    const std::string trimmed = ascii_trimmed(raw);
    if (trimmed.empty()) return false;
    const std::size_t semicolon = trimmed.find(';');
    const std::string essence =
        ascii_lower(ascii_trimmed(std::string_view(trimmed).substr(0, semicolon)));
    const std::size_t slash = essence.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 == essence.size()) return false;
    for (char c : essence) {
        if (!is_mime_token_char(c)) return false;
    }
    if (essence != "image/png") return false;
    canonical = essence;
    if (semicolon != std::string::npos) {
        canonical += ascii_trimmed(std::string_view(trimmed).substr(semicolon));
    }
    return true;
}

/// The bitmap to serialize. A canvas that has never had a context still has
/// one — the standard's canvas bitmap exists from the element — so this builds
/// a blank one of the element's size rather than creating a context: a page
/// that never asked for `2d` does not get one by asking for a data URL.
std::shared_ptr<ImageData> canvas_bitmap_for_serialization(Element& el) {
    if (auto context = el.canvas_context()) {
        const std::shared_ptr<ImageData> image = context->image_data();
        // Over the raster budget the backing store is a single pixel that
        // drawing does nothing to (RasterBudget.hpp): there is no image here
        // to serialize, and `data:,` is the honest answer.
        if (!image || image->width != context->width() || image->height != context->height()) return nullptr;
        return image;
    }

    unsigned long width = 300;
    unsigned long height = 150;
    if (const std::string* text = el.attribute("width")) {
        if (!parse_html_non_negative_integer(*text, width)) width = 300;
    }
    if (const std::string* text = el.attribute("height")) {
        if (!parse_html_non_negative_integer(*text, height)) height = 150;
    }
    if (width == 0 || height == 0) return nullptr;
    if (!RasterBudget::allows(static_cast<std::int64_t>(width), static_cast<std::int64_t>(height))) return nullptr;

    auto blank = std::make_shared<ImageData>();
    blank->width = static_cast<int>(width);
    blank->height = static_cast<int>(height);
    blank->pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    return blank;
}

JsValue canvas_to_data_url(Element& el, Args args) {
    std::string type = "image/png";
    if (!args.empty() && !args[0].is_undefined()) type = to_str(args[0]);

    std::string canonical;
    if (!png_mime_type(type, canonical)) return str("data:,");

    const std::shared_ptr<ImageData> bitmap = canvas_bitmap_for_serialization(el);
    if (!bitmap || !bitmap->is_valid()) return str("data:,");

    std::vector<uint8_t> png;
    if (!ImageDecoder::encode_png(*bitmap, png)) return str("data:,");
    return str("data:" + canonical + ";base64," + base64_encode(png.data(), png.size()));
}

void setup_canvas(Iface& I) {
    ElementMembers<&is_canvas> C(I);
    for (const char* name : {"width", "height"}) {
        const std::string attribute = name;
        const unsigned long fallback = attribute == "width" ? 300 : 150;
        C.accessor(name,
            [attribute, fallback](Realm&, Element& el) {
                unsigned long value = 0;
                const std::string* text = el.attribute(attribute);
                return num(static_cast<double>(text && parse_html_non_negative_integer(*text, value) ? value : fallback));
            },
            [attribute](Realm&, Element& el, const JsValue& value) {
                // Any number may be assigned; one that does not fit an int
                // is brought into range first. A size the canvas cannot have
                // is kept as its size and leaves it blank (RasterBudget.hpp).
                const double asked = to_double(value);
                const int size = asked >= 1.0 ? static_cast<int>(std::min(asked, 2147483647.0)) : 0;
                el.set_attribute(attribute, std::to_string(size));
                if (auto context = el.canvas_context()) {
                    if (attribute == "width") context->resize(std::max(size, 1), context->height());
                    else context->resize(context->width(), std::max(size, 1));
                }
            });
    }
    C.method("getContext", 1, [](Realm& r, Element& el, Args args) {
        require_args(args, 1, "HTMLCanvasElement.getContext");
        const std::string type = ascii_lower(to_str(args[0]));
        if (type != "2d") return null();
        auto context = el.get_or_create_canvas_context_2d();
        if (!context) return null();
        return cached(wrap(r, el), "context2d", [&]() {
            JsValue object = context->to_js_value(context);
            // ImageData needs a realm, so the three methods that speak it are
            // added here rather than in Canvas2D.cpp.
            install_image_data_methods(r, object, context);
            return object;
        });
    });
    C.method("toDataURL", 0, [](Realm&, Element& el, Args args) { return canvas_to_data_url(el, args); });
}

void setup_media(Iface& I) {
    ElementMembers<&is_media> M(I);
    reflect(I, "r:src s:preload b:autoplay b:loop b:controls b:defaultMuted:muted s:crossOrigin:crossorigin");
    M.getter("currentSrc", [](Realm&, Element&) { return str(""); });
    M.getter("paused", [](Realm&, Element&) { return boolean(true); });
    M.getter("ended", [](Realm&, Element&) { return boolean(false); });
    M.getter("duration", [](Realm&, Element&) { return num(std::numeric_limits<double>::quiet_NaN()); });
    M.getter("readyState", [](Realm&, Element&) { return num(0); });
    M.getter("networkState", [](Realm&, Element&) { return num(0); });
    M.accessor("currentTime", [](Realm&, Element&) { return num(0); }, [](Realm&, Element&, const JsValue&) {});
    M.accessor("volume", [](Realm&, Element&) { return num(1); }, [](Realm&, Element&, const JsValue&) {});
    M.accessor("muted",
        [](Realm&, Element& el) { return boolean(el.has_attribute("muted")); },
        [](Realm&, Element&, const JsValue&) {});
    M.accessor("playbackRate", [](Realm&, Element&) { return num(1); }, [](Realm&, Element&, const JsValue&) {});
    for (const char* name : {"pause", "load"}) {
        M.method(name, 0, [](Realm&, Element&, Args) { return undefined(); });
    }
    M.method("play", 0, [](Realm& r, Element&, Args) {
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        if (!promise.is_function()) return undefined();
        return js_call(promise.get_property("resolve"), promise, {});
    });
    M.method("canPlayType", 1, [](Realm&, Element&, Args) { return str(""); });
}

struct OffscreenCanvasState : JsExotic {
    int width = 0;
    int height = 0;
    decltype(std::declval<Element>().get_or_create_canvas_context_2d()) context;
    JsValue context_obj = JsValue::make_undefined();
};

int clamp_offscreen_dim(const JsValue& value) {
    const double asked = to_double(value);
    return asked >= 1.0 ? static_cast<int>(std::min(asked, 2147483647.0)) : 0;
}

JsValue construct_offscreen_canvas(Realm& r, const InterfaceDef&, Args args, const JsValue* derived) {
    require_args(args, 2, "OffscreenCanvas");
    auto state = std::make_shared<OffscreenCanvasState>();
    state->width = clamp_offscreen_dim(args[0]);
    state->height = clamp_offscreen_dim(args[1]);
    return r.make("OffscreenCanvas", state, derived);
}

void setup_offscreen_canvas(Iface& I) {
    I.accessor("width",
        [](Realm&) { return num(static_cast<double>(this_state<OffscreenCanvasState>("OffscreenCanvas").width)); },
        [](Realm&, const JsValue& val) {
            auto& st = this_state<OffscreenCanvasState>("OffscreenCanvas");
            st.width = clamp_offscreen_dim(val);
            if (st.context) st.context->resize(std::max(st.width, 1), st.context->height());
        });
    I.accessor("height",
        [](Realm&) { return num(static_cast<double>(this_state<OffscreenCanvasState>("OffscreenCanvas").height)); },
        [](Realm&, const JsValue& val) {
            auto& st = this_state<OffscreenCanvasState>("OffscreenCanvas");
            st.height = clamp_offscreen_dim(val);
            if (st.context) st.context->resize(st.context->width(), std::max(st.height, 1));
        });
    I.method("getContext", 1, [](Realm& r, Args args) {
        require_args(args, 1, "OffscreenCanvas.getContext");
        auto& st = this_state<OffscreenCanvasState>("OffscreenCanvas");
        if (ascii_lower(to_str(args[0])) != "2d") return null();
        if (!st.context) {
            using CtxPtr = decltype(st.context);
            st.context = std::make_shared<CtxPtr::element_type>(std::max(st.width, 1), std::max(st.height, 1));
            JsValue object = st.context->to_js_value(st.context);
            install_image_data_methods(r, object, st.context);
            st.context_obj = object;
        }
        return st.context_obj;
    });
    I.method("convertToBlob", 0, [](Realm& r, Args args) {
        auto& st = this_state<OffscreenCanvasState>("OffscreenCanvas");
        std::string type = "image/png";
        if (!args.empty() && args[0].is_object_like()) {
            JsValue t = args[0].get_property("type");
            if (!t.is_undefined()) type = to_str(t);
        }
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        auto reject_with = [&](const std::string& name, const std::string& msg) {
            JsValue err = make_dom_exception(r, name, msg);
            if (promise.is_function()) return js_call(promise.get_property("reject"), promise, {err});
            return undefined();
        };
        std::string canonical;
        if (!png_mime_type(type, canonical)) {
            return reject_with("EncodingError", "OffscreenCanvas.convertToBlob: unsupported type '" + type + "'.");
        }
        if (st.width <= 0 || st.height <= 0 ||
            !RasterBudget::allows(static_cast<std::int64_t>(st.width), static_cast<std::int64_t>(st.height))) {
            return reject_with("IndexSizeError", "OffscreenCanvas.convertToBlob: canvas has invalid or over-budget dimensions.");
        }
        std::shared_ptr<ImageData> bitmap;
        if (st.context) {
            bitmap = st.context->image_data();
            if (!bitmap || bitmap->width != st.width || bitmap->height != st.height) {
                return reject_with("IndexSizeError", "OffscreenCanvas.convertToBlob: backing store size mismatch.");
            }
        } else {
            bitmap = std::make_shared<ImageData>();
            bitmap->width = st.width;
            bitmap->height = st.height;
            bitmap->pixels.assign(static_cast<std::size_t>(st.width) * static_cast<std::size_t>(st.height), 0);
        }
        std::vector<uint8_t> png;
        if (!ImageDecoder::encode_png(*bitmap, png)) {
            return reject_with("EncodingError", "OffscreenCanvas.convertToBlob: PNG encoding failed.");
        }
        JsValue blob = make_blob(r, std::string(reinterpret_cast<const char*>(png.data()), png.size()), canonical);
        if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {blob});
        return undefined();
    });
    I.method("transferToImageBitmap", 0, [](Realm& r, Args) {
        auto& st = this_state<OffscreenCanvasState>("OffscreenCanvas");
        if (st.width <= 0 || st.height <= 0 ||
            !RasterBudget::allows(static_cast<std::int64_t>(st.width), static_cast<std::int64_t>(st.height))) {
            throw_dom(r, "InvalidStateError",
                      "OffscreenCanvas.transferToImageBitmap: canvas has invalid or over-budget dimensions.");
        }
        std::vector<uint32_t> pixels;
        if (st.context) {
            if (auto img = st.context->image_data(); img && img->is_valid()) {
                pixels = img->pixels;
            }
            st.context->resize(std::max(st.width, 1), std::max(st.height, 1));
        } else {
            pixels.assign(static_cast<std::size_t>(st.width) * static_cast<std::size_t>(st.height), 0);
        }
        return make_image_bitmap(r, st.width, st.height, std::move(pixels));
    });
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

std::shared_ptr<Element> make_html(Realm& r, Node& context, const char* tag) {
    auto document = document_of(r, context);
    auto element = std::make_shared<Element>(tag);
    if (document) element->set_owner_document(document);
    return element;
}

std::shared_ptr<Element> first_child_tagged(const Node& parent, const char* tag) {
    for (const auto& child : parent.children()) {
        if (is_html_element(*child, tag)) return std::static_pointer_cast<Element>(child);
    }
    return nullptr;
}

// The rows of a table in the order `rows` gives them: those of the head
// sections, then the body ones and the table's own, then the foot ones.
void table_rows(Node& table, NodeVector& out) {
    for (const auto& child : table.children()) {
        if (!is_html_element(*child, "thead")) continue;
        for (const auto& row : child->children()) {
            if (is_html_element(*row, "tr")) out.push_back(row);
        }
    }
    for (const auto& child : table.children()) {
        if (is_html_element(*child, "tr")) out.push_back(child);
        else if (is_html_element(*child, "tbody")) {
            for (const auto& row : child->children()) {
                if (is_html_element(*row, "tr")) out.push_back(row);
            }
        }
    }
    for (const auto& child : table.children()) {
        if (!is_html_element(*child, "tfoot")) continue;
        for (const auto& row : child->children()) {
            if (is_html_element(*row, "tr")) out.push_back(row);
        }
    }
}

void child_rows(Node& section, NodeVector& out) {
    for (const auto& child : section.children()) {
        if (is_html_element(*child, "tr")) out.push_back(child);
    }
}

void row_cells(Node& row, NodeVector& out) {
    for (const auto& child : row.children()) {
        if (is_table_cell(*child)) out.push_back(child);
    }
}

long index_argument(Realm& r, Args args, std::size_t count, const char* what) {
    const long index = js_arg(args, 0).is_undefined() ? -1 : to_long(args[0]);
    if (index < -1 || index > static_cast<long>(count)) {
        throw_dom(r, "IndexSizeError", std::string(what) + ": the index " + std::to_string(index) + " is out of range.");
    }
    return index;
}

void set_table_part(Realm& r, Element& table, const char* tag, const JsValue& value) {
    Node* node = value.is_nullish() ? nullptr : node_ptr(value);
    if (!value.is_nullish() && (!node || !is_html_element(*node, tag))) {
        js_throw_type_error(std::string("The value provided is not a '") + tag + "' element.");
    }
    if (auto existing = first_child_tagged(table, tag)) table.remove_child(existing);
    if (!node) return;
    auto keep = node->shared_from_this();
    const std::string which = tag;
    std::shared_ptr<Node> reference;
    if (which == "caption") {
        reference = table.first_child();
    } else if (which == "thead") {
        for (const auto& child : table.children()) {
            if (!is_html_element(*child, "caption") && !is_html_element(*child, "colgroup")) {
                reference = child;
                break;
            }
        }
    }
    pre_insert(r, table, keep, reference.get());
}

JsValue create_table_part(Realm& r, Element& table, const char* tag) {
    if (auto existing = first_child_tagged(table, tag)) return wrap(r, existing);
    auto made = make_html(r, table, tag);
    set_table_part(r, table, tag, wrap(r, made));
    return wrap(r, made);
}

void setup_table(Iface& I) {
    ElementMembers<&is_table> T(I);
    T.accessor("caption",
        [](Realm& r, Element& el) { return wrap(r, first_child_tagged(el, "caption")); },
        [](Realm& r, Element& el, const JsValue& value) { set_table_part(r, el, "caption", value); });
    T.accessor("tHead",
        [](Realm& r, Element& el) { return wrap(r, first_child_tagged(el, "thead")); },
        [](Realm& r, Element& el, const JsValue& value) { set_table_part(r, el, "thead", value); });
    T.accessor("tFoot",
        [](Realm& r, Element& el) { return wrap(r, first_child_tagged(el, "tfoot")); },
        [](Realm& r, Element& el, const JsValue& value) { set_table_part(r, el, "tfoot", value); });
    T.method("createCaption", 0, [](Realm& r, Element& el, Args) { return create_table_part(r, el, "caption"); });
    T.method("createTHead", 0, [](Realm& r, Element& el, Args) { return create_table_part(r, el, "thead"); });
    T.method("createTFoot", 0, [](Realm& r, Element& el, Args) { return create_table_part(r, el, "tfoot"); });
    T.method("deleteCaption", 0, [](Realm&, Element& el, Args) {
        if (auto existing = first_child_tagged(el, "caption")) el.remove_child(existing);
        return undefined();
    });
    T.method("deleteTHead", 0, [](Realm&, Element& el, Args) {
        if (auto existing = first_child_tagged(el, "thead")) el.remove_child(existing);
        return undefined();
    });
    T.method("deleteTFoot", 0, [](Realm&, Element& el, Args) {
        if (auto existing = first_child_tagged(el, "tfoot")) el.remove_child(existing);
        return undefined();
    });
    T.method("createTBody", 0, [](Realm& r, Element& el, Args) {
        auto body = make_html(r, el, "tbody");
        std::shared_ptr<Node> last_body;
        for (const auto& child : el.children()) {
            if (is_html_element(*child, "tbody")) last_body = child;
        }
        auto reference = last_body ? last_body->next_sibling() : nullptr;
        pre_insert(r, el, body, reference.get());
        return wrap(r, body);
    });
    T.getter("tBodies", [](Realm& r, Element& el) {
        auto root = el.shared_from_this();
        return cached(wrap(r, el), "tBodies", [&]() {
            return make_live_list(r, "HTMLCollection", root, [](Node& table, NodeVector& out) {
                for (const auto& child : table.children()) {
                    if (is_html_element(*child, "tbody")) out.push_back(child);
                }
            });
        });
    });
    T.getter("rows", [](Realm& r, Element& el) {
        auto root = el.shared_from_this();
        return cached(wrap(r, el), "rows", [&]() { return make_live_list(r, "HTMLCollection", root, &table_rows); });
    });
    T.method("insertRow", 0, [](Realm& r, Element& el, Args args) {
        NodeVector rows;
        table_rows(el, rows);
        const long index = index_argument(r, args, rows.size(), "insertRow");
        auto row = make_html(r, el, "tr");
        if (rows.empty()) {
            // No rows yet: the row goes into the last body, made if needed.
            std::shared_ptr<Node> body;
            for (const auto& child : el.children()) {
                if (is_html_element(*child, "tbody")) body = child;
            }
            if (!body) {
                body = make_html(r, el, "tbody");
                el.append_child(body);
            }
            body->append_child(row);
        } else if (index == -1 || index == static_cast<long>(rows.size())) {
            rows.back()->parent()->append_child(row);
        } else {
            auto before = rows[static_cast<std::size_t>(index)];
            before->parent()->insert_before(row, before);
        }
        return wrap(r, row);
    });
    T.method("deleteRow", 1, [](Realm& r, Element& el, Args args) {
        NodeVector rows;
        table_rows(el, rows);
        long index = to_long(js_arg(args, 0));
        if (index < -1 || index >= static_cast<long>(rows.size())) {
            throw_dom(r, "IndexSizeError", "deleteRow: the index " + std::to_string(index) + " is out of range.");
        }
        if (index == -1) {
            if (rows.empty()) return undefined();
            index = static_cast<long>(rows.size()) - 1;
        }
        auto row = rows[static_cast<std::size_t>(index)];
        row->parent()->remove_child(row);
        return undefined();
    });
    reflect(I, "s:border s:align s:frame s:rules s:summary s:width s:bgColor:bgcolor s:cellPadding:cellpadding "
               "s:cellSpacing:cellspacing");
}

void setup_table_section(Iface& I) {
    ElementMembers<&is_table_section> S(I);
    S.getter("rows", [](Realm& r, Element& el) {
        auto root = el.shared_from_this();
        return cached(wrap(r, el), "rows", [&]() { return make_live_list(r, "HTMLCollection", root, &child_rows); });
    });
    S.method("insertRow", 0, [](Realm& r, Element& el, Args args) {
        NodeVector rows;
        child_rows(el, rows);
        const long index = index_argument(r, args, rows.size(), "insertRow");
        auto row = make_html(r, el, "tr");
        if (index == -1 || index == static_cast<long>(rows.size())) el.append_child(row);
        else el.insert_before(row, rows[static_cast<std::size_t>(index)]);
        return wrap(r, row);
    });
    S.method("deleteRow", 1, [](Realm& r, Element& el, Args args) {
        NodeVector rows;
        child_rows(el, rows);
        long index = to_long(js_arg(args, 0));
        if (index < -1 || index >= static_cast<long>(rows.size())) {
            throw_dom(r, "IndexSizeError", "deleteRow: the index " + std::to_string(index) + " is out of range.");
        }
        if (index == -1) {
            if (rows.empty()) return undefined();
            index = static_cast<long>(rows.size()) - 1;
        }
        el.remove_child(rows[static_cast<std::size_t>(index)]);
        return undefined();
    });
    reflect(I, "s:align s:ch:char s:chOff:charoff s:vAlign:valign");
}

void setup_table_row(Iface& I) {
    ElementMembers<&is_row> R(I);
    R.getter("cells", [](Realm& r, Element& el) {
        auto root = el.shared_from_this();
        return cached(wrap(r, el), "cells", [&]() { return make_live_list(r, "HTMLCollection", root, &row_cells); });
    });
    R.getter("rowIndex", [](Realm&, Element& el) {
        std::shared_ptr<Node> table = el.parent();
        if (table && is_table_section(*table)) table = table->parent();
        if (!table || !is_html_element(*table, "table")) return num(-1);
        NodeVector rows;
        table_rows(*table, rows);
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].get() == &el) return num(static_cast<double>(i));
        }
        return num(-1);
    });
    R.getter("sectionRowIndex", [](Realm&, Element& el) {
        auto parent = el.parent();
        if (!parent) return num(-1);
        NodeVector rows;
        child_rows(*parent, rows);
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].get() == &el) return num(static_cast<double>(i));
        }
        return num(-1);
    });
    R.method("insertCell", 0, [](Realm& r, Element& el, Args args) {
        NodeVector cells;
        row_cells(el, cells);
        const long index = index_argument(r, args, cells.size(), "insertCell");
        auto cell = make_html(r, el, "td");
        if (index == -1 || index == static_cast<long>(cells.size())) el.append_child(cell);
        else el.insert_before(cell, cells[static_cast<std::size_t>(index)]);
        return wrap(r, cell);
    });
    R.method("deleteCell", 1, [](Realm& r, Element& el, Args args) {
        NodeVector cells;
        row_cells(el, cells);
        long index = to_long(js_arg(args, 0));
        if (index < -1 || index >= static_cast<long>(cells.size())) {
            throw_dom(r, "IndexSizeError", "deleteCell: the index " + std::to_string(index) + " is out of range.");
        }
        if (index == -1) {
            if (cells.empty()) return undefined();
            index = static_cast<long>(cells.size()) - 1;
        }
        el.remove_child(cells[static_cast<std::size_t>(index)]);
        return undefined();
    });
    reflect(I, "s:align s:bgColor:bgcolor s:ch:char s:chOff:charoff s:vAlign:valign");
}

void setup_table_cell(Iface& I) {
    ElementMembers<&is_table_cell> C(I);
    C.getter("cellIndex", [](Realm&, Element& el) {
        auto row = el.parent();
        if (!row || !is_html_element(*row, "tr")) return num(-1);
        NodeVector cells;
        row_cells(*row, cells);
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (cells[i].get() == &el) return num(static_cast<double>(i));
        }
        return num(-1);
    });
    C.accessor("colSpan",
        [](Realm&, Element& el) {
            unsigned long value = 0;
            const std::string* text = el.attribute("colspan");
            if (!text || !parse_html_non_negative_integer(*text, value) || value == 0) return num(1);
            return num(static_cast<double>(std::min(value, 1000ul)));
        },
        [](Realm&, Element& el, const JsValue& value) { el.set_attribute("colspan", std::to_string(to_ulong(value))); });
    C.accessor("rowSpan",
        [](Realm&, Element& el) {
            unsigned long value = 0;
            const std::string* text = el.attribute("rowspan");
            if (!text || !parse_html_non_negative_integer(*text, value)) return num(1);
            return num(static_cast<double>(std::min(value, 65534ul)));
        },
        [](Realm&, Element& el, const JsValue& value) { el.set_attribute("rowspan", std::to_string(to_ulong(value))); });
    reflect(I, "s:headers s:scope s:abbr s:align s:axis s:height s:width s:ch:char s:chOff:charoff b:noWrap:nowrap "
               "s:vAlign:valign s:bgColor:bgcolor");
}

// ---------------------------------------------------------------------------
// Templates, slots, dialogs, details
// ---------------------------------------------------------------------------

void setup_template(Iface& I) {
    ElementMembers<&is_template> T(I);
    T.getter("content", [](Realm& r, Element& el) {
        if (!el.template_contents()) {
            auto contents = std::make_shared<Node>(NodeType::DocumentFragment);
            if (auto document = document_of(r, el)) contents->set_owner_document(document->template_document());
            el.set_template_contents(contents);
        }
        return wrap(r, el.template_contents());
    });
    reflect(I, "s:shadowRootMode:shadowrootmode");
}

void setup_slot(Iface& I) {
    ElementMembers<&is_slot> S(I);
    reflect(I, "s:name");
    S.method("assignedNodes", 0, [](Realm& r, Element& el, Args) {
        std::vector<JsValue> out;
        for (const auto& node : ShadowRoot::assigned_nodes(shared(el))) out.push_back(wrap(r, node));
        return JsValue::make_array(out);
    });
    S.method("assignedElements", 0, [](Realm& r, Element& el, Args) {
        std::vector<JsValue> out;
        for (const auto& node : ShadowRoot::assigned_elements(shared(el))) out.push_back(wrap(r, node));
        return JsValue::make_array(out);
    });
    S.method("assign", 0, [](Realm&, Element&, Args) { return undefined(); });
}

namespace {

/// The dialog focusing steps: a descendant with `autofocus`, else the first
/// focusable descendant in tree order, else the dialog itself, which is given
/// `tabindex="-1"` so that it can hold the focus and Tab can still leave it.
/// A dialog that is not in the document takes nothing (is_focusable says so).
void focus_dialog(Realm& r, Element& dialog) {
    Element* autofocus = nullptr;
    Element* first = nullptr;
    for_each_descendant(dialog, [&](Node& node) {
        if (!node.is_element()) return true;
        auto& element = static_cast<Element&>(node);
        if (!is_focusable(element)) return true;
        if (!first) first = &element;
        if (!element.has_attribute("autofocus")) return true;
        autofocus = &element;
        return false;
    });
    if (autofocus) {
        focus_element(r, autofocus);
        return;
    }
    if (first) {
        focus_element(r, first);
        return;
    }
    if (!dialog.has_attribute("tabindex")) dialog.set_attribute("tabindex", "-1");
    focus_element(r, &dialog);
}

} // namespace

void setup_dialog(Iface& I) {
    ElementMembers<&is_dialog> D(I);
    reflect(I, "b:open");
    D.accessor("returnValue",
        [](Realm& r, Element& el) {
            const JsValue* value = el.script_object() ? slot(wrap(r, el), "returnValue") : nullptr;
            return value ? *value : str("");
        },
        [](Realm& r, Element& el, const JsValue& value) { set_slot(wrap(r, el), "returnValue", str(to_str(value))); });
    D.method("show", 0, [](Realm& r, Element& el, Args) {
        // An ordinary dialog that is already open is an error; a modal one is
        // not, and re-running the focusing steps there would take the focus
        // away from inside the dialog.
        if (el.has_attribute("open")) {
            if (!el.is_modal_dialog()) throw_dom(r, "InvalidStateError", "The dialog is already open.");
            return undefined();
        }
        el.set_attribute("open", "");
        focus_dialog(r, el);
        return undefined();
    });
    D.method("showModal", 0, [](Realm& r, Element& el, Args) {
        if (el.has_attribute("open")) throw_dom(r, "InvalidStateError", "The dialog is already open.");
        if (!el.is_connected()) throw_dom(r, "InvalidStateError", "The dialog is not in a document.");
        // The top layer is not painted; the flag and the document's record of
        // it are what the hit test, the Tab order and close() read back.
        el.set_modal_dialog(true);
        std::shared_ptr<Element> previous;
        auto document = document_of(r, el);
        if (document) {
            previous = document->focused_element();
            document->set_modal_dialog(shared(el));
        }
        if (previous && !previous->is_connected()) previous.reset();
        el.set_attribute("open", "");
        focus_dialog(r, el);
        el.set_dialog_previously_focused(std::move(previous));
        return undefined();
    });
    D.method("close", 0, [](Realm& r, Element& el, Args args) {
        if (!el.has_attribute("open")) return undefined();
        JsValue self = wrap(r, el);
        if (!js_arg(args, 0).is_undefined()) set_slot(self, "returnValue", str(to_str(args[0])));
        const bool was_modal = el.is_modal_dialog();
        auto previous = el.dialog_previously_focused();
        el.set_modal_dialog(false);
        el.set_dialog_previously_focused(nullptr);
        auto document = document_of(r, el);
        if (document && document->modal_dialog().get() == &el) document->set_modal_dialog(nullptr);
        el.remove_attribute("open");
        // A modal dialog gives the focus back to the element that had it when
        // it opened. Anything else unfocuses: the dialog's own control is
        // hidden now, so keeping the focus on it would be a focus on nothing.
        if (was_modal && previous && previous->is_connected() && is_focusable(*previous)) {
            focus_element(r, previous.get());
        } else {
            focus_element(r, nullptr, document);
        }
        // The close event comes as a task, after the caller has finished.
        Realm* realm = &r;
        EventLoop::default_loop().post_task([realm, self]() {
            if (realm->alive) fire(*realm, self, "close");
        });
        return undefined();
    });
}

// ---------------------------------------------------------------------------
// The interfaces that are a list of reflected attributes and nothing more
// ---------------------------------------------------------------------------

struct SimpleInterface {
    const char* name;
    const char* reflected;
};

const SimpleInterface kSimple[] = {
    {"HTMLUnknownElement", nullptr},
    {"HTMLHtmlElement", "s:version"},
    {"HTMLHeadElement", nullptr},
    {"HTMLTitleElement", nullptr},
    {"HTMLBaseElement", "r:href s:target"},
    {"HTMLLinkElement", "r:href s:crossOrigin:crossorigin s:rel s:as s:media s:hreflang s:type s:integrity "
                        "s:referrerPolicy:referrerpolicy b:disabled s:charset s:rev s:target"},
    {"HTMLMetaElement", "s:name s:httpEquiv:http-equiv s:content s:scheme s:media"},
    {"HTMLStyleElement", "s:media s:type b:disabled"},
    {"HTMLBodyElement", "s:text s:link s:vLink:vlink s:aLink:alink s:bgColor:bgcolor s:background"},
    {"HTMLHeadingElement", "s:align"},
    {"HTMLParagraphElement", "s:align"},
    {"HTMLHRElement", "s:align s:color b:noShade:noshade s:size s:width"},
    {"HTMLPreElement", "l:width"},
    {"HTMLQuoteElement", "r:cite"},
    {"HTMLOListElement", "b:reversed l:start=1 s:type b:compact"},
    {"HTMLUListElement", "b:compact s:type"},
    {"HTMLLIElement", "l:value s:type"},
    {"HTMLDListElement", "b:compact"},
    {"HTMLDivElement", "s:align"},
    {"HTMLSpanElement", nullptr},
    {"HTMLBRElement", "s:clear"},
    {"HTMLModElement", "r:cite s:dateTime:datetime"},
    {"HTMLTimeElement", "s:dateTime:datetime"},
    {"HTMLDataElement", "s:value"},
    {"HTMLPictureElement", nullptr},
    {"HTMLSourceElement", "r:src s:type s:srcset s:sizes s:media u:width u:height"},
    {"HTMLIFrameElement", "r:src s:srcdoc s:name s:width s:height s:allow b:allowFullscreen:allowfullscreen "
                          "s:referrerPolicy:referrerpolicy s:loading s:align s:scrolling s:frameBorder:frameborder"},
    {"HTMLEmbedElement", "r:src s:type s:width s:height s:align s:name"},
    {"HTMLObjectElement", "r:data s:type s:name s:width s:height s:useMap:usemap s:align s:border"},
    {"HTMLParamElement", "s:name s:value s:type s:valueType:valuetype"},
    {"HTMLTrackElement", "s:kind r:src s:srclang s:label b:default"},
    {"HTMLMapElement", "s:name"},
    {"HTMLTableCaptionElement", "s:align"},
    {"HTMLTableColElement", "p:span=1 s:align s:ch:char s:chOff:charoff s:vAlign:valign s:width"},
    {"HTMLDetailsElement", "b:open s:name"},
    {"HTMLMenuElement", "b:compact"},
    {"HTMLDirectoryElement", "b:compact"},
    {"HTMLFontElement", "s:color s:face s:size"},
    {"HTMLFrameElement", "s:name s:scrolling r:src s:frameBorder:frameborder b:noResize:noresize"},
    {"HTMLFrameSetElement", "s:cols s:rows"},
    {"HTMLMarqueeElement", "s:behavior s:bgColor:bgcolor s:direction s:height s:width"},
};

void setup_title(Iface& I) {
    Html H(I);
    H.accessor("text",
        [](Realm&, Element& el) {
            std::string text;
            for (const auto& child : el.children()) {
                if (child->is_text()) text += character_data(*child);
            }
            return str(text);
        },
        [](Realm&, Element& el, const JsValue& value) { set_text_content(el, to_str(value)); });
}

void setup_iframe(Iface& I) {
    setup_reflecting(I);
    Html H(I);
    H.token_list("sandbox", "sandbox",
                 {"allow-downloads", "allow-forms", "allow-modals", "allow-orientation-lock",
                  "allow-pointer-lock", "allow-popups", "allow-popups-to-escape-sandbox",
                  "allow-presentation", "allow-same-origin", "allow-scripts", "allow-top-navigation",
                  "allow-top-navigation-by-user-activation", "allow-top-navigation-to-custom-protocols"});
    H.getter("contentWindow", [](Realm& r, Element& el) { return iframe_content_window(r, el); });
    H.getter("contentDocument", [](Realm& r, Element& el) { return iframe_content_document(r, el); });
    H.method("getSVGDocument", 0, [](Realm& r, Element& el, Args) {
        JsValue doc_val = iframe_content_document(r, el);
        Node* n = node_ptr(doc_val);
        if (!n || !n->is_document()) return null();
        auto& d = static_cast<Document&>(*n);
        if (d.content_type() == "image/svg+xml") return doc_val;
        auto root = d.document_element();
        if (root && root->element_namespace() == ElementNamespace::SVG && root->tag_name() == "svg") return doc_val;
        return null();
    });
}

void setup_video(Iface& I) {
    reflect(I, "u:width u:height r:poster b:playsInline:playsinline");
    Html H(I);
    H.getter("videoWidth", [](Realm&, Element&) { return num(0); });
    H.getter("videoHeight", [](Realm&, Element&) { return num(0); });
}

void setup_style_element(Iface& I) {
    setup_reflecting(I);
    Html H(I);
    H.getter("sheet", [](Realm& r, Element& el) { return style_sheet(r, el); });
}

void setup_link_element(Iface& I) {
    setup_reflecting(I);
    Html H(I);
    H.getter("sheet", [](Realm&, Element&) { return null(); });
    H.token_list("relList", "rel", {"alternate", "dns-prefetch", "icon", "manifest", "modulepreload", "next",
                                    "pingback", "preconnect", "prefetch", "preload", "search", "stylesheet"});
}

} // namespace

void note_svg_view_box_changed(Element& element, const AttributeChange& change) {
    if (element.element_namespace() != ElementNamespace::SVG || element.tag_name() != "svg" ||
        change.namespace_uri || !change.local_name || *change.local_name != "viewBox") return;
    auto* wrapper = static_cast<JsObject*>(element.script_object());
    if (!wrapper) return;
    auto found = wrapper->properties.find("%svgViewBox");
    if (found == wrapper->properties.end()) return;
    auto* state = state_of<SvgAnimatedRectState>(found->second);
    if (state) state->box->loaded = false;
}

void reflect_attributes(Iface& I, const char* description) {
    reflect(I, description);
}

void register_html_interfaces() {
    const auto element_ctor = &construct_custom_element;
    register_interface({"HTMLElement", "Element", setup_html_element, element_ctor, 0, nullptr});
    register_interface({"SVGAnimatedString", nullptr, setup_svg_animated_string, nullptr, 0, nullptr});
    register_interface({"SVGAnimatedRect", nullptr, setup_svg_animated_rect, nullptr, 0, nullptr});
    register_interface({"SVGRect", nullptr, setup_svg_rect, nullptr, 0, nullptr});
    register_interface({"SVGElement", "Element", setup_svg_element, nullptr, 0, nullptr});
    register_interface({"SVGGraphicsElement", "SVGElement", nullptr, nullptr, 0, nullptr});
    register_interface({"SVGSVGElement", "SVGGraphicsElement", setup_svg_svg_element, nullptr, 0, nullptr});
    register_interface({"SVGTitleElement", "SVGElement", nullptr, nullptr, 0, nullptr});

    for (const auto& simple : kSimple) {
        InterfaceDef def{simple.name, "HTMLElement", setup_reflecting, element_ctor, 0, nullptr, simple.reflected};
        const std::string name = simple.name;
        if (name == "HTMLTitleElement") def.setup = setup_title;
        else if (name == "HTMLIFrameElement") def.setup = setup_iframe;
        else if (name == "HTMLStyleElement") def.setup = setup_style_element;
        else if (name == "HTMLLinkElement") def.setup = setup_link_element;
        register_interface(def);
    }
    register_interface({"HTMLAnchorElement", "HTMLElement", setup_hyperlink, element_ctor, 0, nullptr});
    register_interface({"HTMLAreaElement", "HTMLElement", setup_hyperlink, element_ctor, 0, nullptr});
    register_interface({"HTMLImageElement", "HTMLElement", setup_image, element_ctor, 0, nullptr});
    register_interface({"HTMLScriptElement", "HTMLElement", setup_script, element_ctor, 0, nullptr});
    register_interface({"HTMLCanvasElement", "HTMLElement", setup_canvas, element_ctor, 0, nullptr});
    register_interface({"OffscreenCanvas", nullptr, setup_offscreen_canvas, construct_offscreen_canvas, 2, nullptr});
    register_interface({"ImageData", nullptr, setup_image_data, construct_image_data, 2, nullptr});
    register_interface({"HTMLMediaElement", "HTMLElement", setup_media, element_ctor, 0, nullptr});
    register_interface({"HTMLAudioElement", "HTMLMediaElement", nullptr, element_ctor, 0, nullptr});
    register_interface({"HTMLVideoElement", "HTMLMediaElement", setup_video, element_ctor, 0, nullptr});
    register_interface({"HTMLTableElement", "HTMLElement", setup_table, element_ctor, 0, nullptr});
    register_interface({"HTMLTableSectionElement", "HTMLElement", setup_table_section, element_ctor, 0, nullptr});
    register_interface({"HTMLTableRowElement", "HTMLElement", setup_table_row, element_ctor, 0, nullptr});
    register_interface({"HTMLTableCellElement", "HTMLElement", setup_table_cell, element_ctor, 0, nullptr});
    register_interface({"HTMLTemplateElement", "HTMLElement", setup_template, element_ctor, 0, nullptr});
    register_interface({"HTMLSlotElement", "HTMLElement", setup_slot, element_ctor, 0, nullptr});
    register_interface({"HTMLDialogElement", "HTMLElement", setup_dialog, element_ctor, 0, nullptr});
}

} // namespace dom
} // namespace litebird
