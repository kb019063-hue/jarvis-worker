// HTMLElement and the element interfaces that are not form controls: links,
// images, scripts, tables, templates, slots, media, canvas, dialog; and the
// SVG element interfaces.

#include "litebird/js/DomBindings.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/net/URL.hpp"
#include "litebird/render/Canvas2D.hpp"

#include <cmath>
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
    // Offsets come from the layout box; before layout there is none.
    H.getter("offsetParent", [](Realm& r, Element& el) {
        const Rect box = el.bounding_box();
        if (box.width <= 0 && box.height <= 0) return null();
        auto document = document_of(r, el);
        return wrap(r, document ? document->body() : nullptr);
    });
    H.getter("offsetTop", [](Realm&, Element& el) { return num(std::round(el.bounding_box().y)); });
    H.getter("offsetLeft", [](Realm&, Element& el) { return num(std::round(el.bounding_box().x)); });
    H.getter("offsetWidth", [](Realm&, Element& el) { return num(std::round(el.bounding_box().width)); });
    H.getter("offsetHeight", [](Realm&, Element& el) { return num(std::round(el.bounding_box().height)); });
    define_common_element_members(I, &is_html_node);
}

void setup_svg_element(Iface& I) {
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

bool link_url(Realm& r, Element& el, URLObject& url) {
    const std::string* href = el.attribute("href");
    if (!href) return false;
    auto document = document_of(r, el);
    return url.parse(strip_ascii_space(*href), document ? document->url() : std::string("about:blank"));
}

std::string serialize_url(const URLObject& url) {
    if (url.host.empty() && url.protocol != "http:" && url.protocol != "https:" && url.protocol != "file:") {
        return url.protocol + url.pathname + url.search + url.hash;
    }
    return url.protocol + "//" + url.host + url.pathname + url.search + url.hash;
}

void define_link_part(Iface& I, const char* name, std::string URLObject::*part) {
    ElementMembers<&is_hyperlink> L(I);
    L.accessor(name,
        [part](Realm& r, Element& el) {
            URLObject url;
            return str(link_url(r, el, url) ? url.*part : std::string());
        },
        [part, name](Realm& r, Element& el, const JsValue& value) {
            URLObject url;
            if (!link_url(r, el, url)) return;
            std::string text = to_str(value);
            const std::string which = name;
            if (which == "hash" && !text.empty() && text[0] != '#') text = "#" + text;
            if (which == "search" && !text.empty() && text[0] != '?') text = "?" + text;
            if (which == "protocol" && !text.empty() && text.back() != ':') text += ":";
            if (which == "pathname" && (text.empty() || text[0] != '/')) text = "/" + text;
            url.*part = text;
            if (which == "hostname") url.host = url.port.empty() ? text : text + ":" + url.port;
            if (which == "port") url.host = text.empty() ? url.hostname : url.hostname + ":" + text;
            if (which == "host") {
                const auto colon = text.find(':');
                url.hostname = text.substr(0, colon);
                url.port = colon == std::string::npos ? std::string() : text.substr(colon + 1);
            }
            el.set_attribute("href", serialize_url(url));
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
        URLObject url;
        return str(link_url(r, el, url) ? url.origin : std::string());
    });
    define_link_part(I, "protocol", &URLObject::protocol);
    define_link_part(I, "host", &URLObject::host);
    define_link_part(I, "hostname", &URLObject::hostname);
    define_link_part(I, "port", &URLObject::port);
    define_link_part(I, "pathname", &URLObject::pathname);
    define_link_part(I, "search", &URLObject::search);
    define_link_part(I, "hash", &URLObject::hash);
    L.getter("username", [](Realm&, Element&) { return str(""); });
    L.getter("password", [](Realm&, Element&) { return str(""); });
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
        return cached(wrap(r, el), "context2d", [&]() { return context->to_js_value(context); });
    });
    C.method("toDataURL", 0, [](Realm&, Element&, Args) { return str("data:,"); });
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

void setup_dialog(Iface& I) {
    ElementMembers<&is_dialog> D(I);
    reflect(I, "b:open");
    D.accessor("returnValue",
        [](Realm& r, Element& el) {
            const JsValue* value = el.script_object() ? slot(wrap(r, el), "returnValue") : nullptr;
            return value ? *value : str("");
        },
        [](Realm& r, Element& el, const JsValue& value) { set_slot(wrap(r, el), "returnValue", str(to_str(value))); });
    D.method("show", 0, [](Realm&, Element& el, Args) {
        if (!el.has_attribute("open")) el.set_attribute("open", "");
        return undefined();
    });
    D.method("showModal", 0, [](Realm& r, Element& el, Args) {
        if (el.has_attribute("open")) throw_dom(r, "InvalidStateError", "The dialog is already open.");
        if (!el.is_connected()) throw_dom(r, "InvalidStateError", "The dialog is not in a document.");
        el.set_attribute("open", "");
        return undefined();
    });
    D.method("close", 0, [](Realm& r, Element& el, Args args) {
        if (!el.has_attribute("open")) return undefined();
        el.remove_attribute("open");
        JsValue self = wrap(r, el);
        if (!js_arg(args, 0).is_undefined()) set_slot(self, "returnValue", str(to_str(args[0])));
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
    H.getter("contentWindow", [](Realm&, Element&) { return null(); });
    H.getter("contentDocument", [](Realm&, Element&) { return null(); });
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
    H.getter("sheet", [](Realm&, Element&) { return null(); });
}

void setup_link_element(Iface& I) {
    setup_reflecting(I);
    Html H(I);
    H.getter("sheet", [](Realm&, Element&) { return null(); });
    H.token_list("relList", "rel", {"alternate", "dns-prefetch", "icon", "manifest", "modulepreload", "next",
                                    "pingback", "preconnect", "prefetch", "preload", "search", "stylesheet"});
}

} // namespace

void reflect_attributes(Iface& I, const char* description) {
    reflect(I, description);
}

void register_html_interfaces() {
    const auto element_ctor = &construct_custom_element;
    register_interface({"HTMLElement", "Element", setup_html_element, element_ctor, 0, nullptr});
    register_interface({"SVGElement", "Element", setup_svg_element, nullptr, 0, nullptr});
    register_interface({"SVGGraphicsElement", "SVGElement", nullptr, nullptr, 0, nullptr});
    register_interface({"SVGSVGElement", "SVGGraphicsElement", nullptr, nullptr, 0, nullptr});
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
