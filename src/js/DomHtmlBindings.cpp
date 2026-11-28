// HTMLElement and the element interfaces that are not form controls: links,
// images, scripts, tables, templates, slots, media, canvas, dialog; and the
// SVG element interfaces.

#include "litebird/common/Base64.hpp"
#include "litebird/js/DomBindings.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/render/Canvas2D.hpp"
#include "litebird/render/SvgRenderer.hpp"
#include "litebird/render/ImageDecoder.hpp"
#include "litebird/render/MediaAudioCodecs.hpp"
#include "litebird/render/SvgRenderer.hpp"
#include "litebird/render/WebGL.hpp"
#include "litebird/js/ImageBitmap.hpp"
#include "litebird/js/TypedArray.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <vector>

namespace litebird {
namespace dom {

void register_media_interfaces();

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

// --- HTMLMediaElement state (WHATWG HTML §4.8.11) ---------------------------
//
// A media element keeps a playback state that outlives its content
// attributes: the volume, whether it is muted right now (the `muted` content
// attribute is only the *default*, which `defaultMuted` reflects), where it
// is, and how fast it plays. It lives beside the wrapper the realm caches
// for the element, under hidden names, so nothing enumerates it.
double media_slot_number(const JsValue& wrapper, const char* name, double fallback) {
    if (const JsValue* stored = slot(wrapper, name)) return to_double(*stored);
    return fallback;
}
void media_slot_set_number(const JsValue& wrapper, const char* name, double value) {
    set_slot(wrapper, name, JsValue::make_number(value));
}
bool media_slot_bool(const JsValue& wrapper, const char* name, bool fallback) {
    if (const JsValue* stored = slot(wrapper, name)) return to_double(*stored) != 0.0;
    return fallback;
}
void media_slot_set_bool(const JsValue& wrapper, const char* name, bool value) {
    set_slot(wrapper, name, JsValue::make_number(value ? 1.0 : 0.0));
}
/// The state's number, refused when it is not finite (a WebIDL `double`
/// never is): the message a browser gives for `el.currentTime = NaN`.
double media_finite_double(Realm&, const JsValue& value, const char* what) {
    const double asked = to_double(value);
    if (!std::isfinite(asked)) {
        js_throw_type_error(std::string("Failed to set the '") + what +
                            "' property on 'HTMLMediaElement': The provided double value is non-finite.");
    }
    return asked;
}

// --- TimeRanges (WHATWG HTML, "TimeRanges") ---------------------------------
struct TimeRangesState : JsExotic {
    std::vector<std::pair<double, double>> ranges;
};

void setup_time_ranges(Iface& I) {
    I.getter("length", [](Realm&) {
        if (auto* st = state_of<TimeRangesState>(JsValue::current_this())) {
            return num(static_cast<double>(st->ranges.size()));
        }
        return num(0);
    });
    auto at = [](Realm& r, Args args, const char* name, bool is_end) -> JsValue {
        require_args(args, 1, name);
        const uint32_t index = to_ulong(js_arg(args, 0));
        auto* st = state_of<TimeRangesState>(JsValue::current_this());
        if (!st || index >= st->ranges.size()) {
            throw_dom(r, "IndexSizeError",
                      std::string(name) + ": index " + std::to_string(index) + " is out of range.");
            return undefined();
        }
        return num(is_end ? st->ranges[index].second : st->ranges[index].first);
    };
    I.method("start", 1, [at](Realm& r, Args args) -> JsValue { return at(r, args, "TimeRanges.start", false); });
    I.method("end", 1, [at](Realm& r, Args args) -> JsValue { return at(r, args, "TimeRanges.end", true); });
}

JsValue make_time_ranges(Realm& realm, std::vector<std::pair<double, double>> ranges = {}) {
    auto st = std::make_shared<TimeRangesState>();
    st->ranges = std::move(ranges);
    return realm.make("TimeRanges", st);
}

// --- MediaError (WHATWG HTML §4.8.11.3) -------------------------------------
struct MediaErrorState : JsExotic {
    int code{0};
    std::string message;
};

void setup_media_error(Iface& I) {
    I.constant("MEDIA_ERR_ABORTED", 1);
    I.constant("MEDIA_ERR_NETWORK", 2);
    I.constant("MEDIA_ERR_DECODE", 3);
    I.constant("MEDIA_ERR_SRC_NOT_SUPPORTED", 4);
    I.getter("code", [](Realm&) {
        auto* st = state_of<MediaErrorState>(JsValue::current_this());
        return num(st ? st->code : 0);
    });
    I.getter("message", [](Realm&) {
        auto* st = state_of<MediaErrorState>(JsValue::current_this());
        return str(st ? st->message : "");
    });
}

JsValue make_media_error(Realm& r, int code, const std::string& msg) {
    auto st = std::make_shared<MediaErrorState>();
    st->code = code;
    st->message = msg;
    return r.make("MediaError", st);
}

std::string media_slot_string(const JsValue& wrapper, const char* name) {
    if (const JsValue* stored = slot(wrapper, name)) return to_str(*stored);
    return {};
}
void media_slot_set_string(const JsValue& wrapper, const char* name, const std::string& value) {
    set_slot(wrapper, name, str(value));
}

std::string media_resolve_candidate_src(Element& el) {
    if (const std::string s = el.get_attribute("src"); !s.empty()) return s;
    for (const auto& child : el.children()) {
        if (child && child->is_element()) {
            auto* ce = static_cast<Element*>(child.get());
            if (ascii_lower(ce->tag_name()) == "source") {
                if (const std::string s = ce->get_attribute("src"); !s.empty()) return s;
            }
        }
    }
    return {};
}

void media_sync_source(Realm& r, Element& el, bool force = false) {
    JsValue wrapper = wrap(r, el);
    const std::string src = media_resolve_candidate_src(el);
    const std::string loaded = media_slot_string(wrapper, "mediaLoadedSrc");
    if (!force && src == loaded) return;

    media_slot_set_string(wrapper, "mediaLoadedSrc", src);
    if (src.empty()) {
        media_slot_set_number(wrapper, "mediaReadyState", 0.0);
        media_slot_set_number(wrapper, "mediaNetworkState", 0.0);
        media_slot_set_number(wrapper, "mediaDuration", std::numeric_limits<double>::quiet_NaN());
        media_slot_set_string(wrapper, "mediaCurrentSrc", "");
        media_slot_set_number(wrapper, "mediaErrorCode", 0.0);
        media_slot_set_string(wrapper, "mediaErrorMsg", "");
        return;
    }

    const std::string lower_prefix = ascii_lower(src.substr(0, std::min<std::size_t>(src.size(), 5)));
    if (lower_prefix == "data:") {
        WavAudio wav;
        std::string err;
        if (decode_wav_data_uri(src, wav, err)) {
            media_slot_set_number(wrapper, "mediaReadyState", 4.0);   // HAVE_ENOUGH_DATA
            media_slot_set_number(wrapper, "mediaNetworkState", 1.0); // NETWORK_IDLE
            media_slot_set_number(wrapper, "mediaDuration", wav.duration_seconds);
            media_slot_set_string(wrapper, "mediaCurrentSrc", src);
            media_slot_set_number(wrapper, "mediaErrorCode", 0.0);
            media_slot_set_string(wrapper, "mediaErrorMsg", "");
            return;
        }
        media_slot_set_number(wrapper, "mediaReadyState", 0.0);
        media_slot_set_number(wrapper, "mediaNetworkState", 3.0); // NETWORK_NO_SOURCE
        media_slot_set_number(wrapper, "mediaDuration", std::numeric_limits<double>::quiet_NaN());
        media_slot_set_string(wrapper, "mediaCurrentSrc", "");
        media_slot_set_number(wrapper, "mediaErrorCode", 4.0); // MEDIA_ERR_SRC_NOT_SUPPORTED
        media_slot_set_string(wrapper, "mediaErrorMsg", err);
    }
}

// --- canPlayType ------------------------------------------------------------
//
// The engine has no demux or codec stack, so it cannot say "probably" to
// anything. It can say which container/codec pairs are on the machine every
// browser is installed on, and `maybe` is exactly the answer that is true of
// them ("might be able to play"). Anything unrecognized, or a codec the type
// does not carry, is the empty string.
std::string media_trim(const std::string& text) {
    const std::string::size_type begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return std::string();
    const std::string::size_type end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}
const std::unordered_map<std::string, std::vector<std::string>>& can_play_table() {
    // Prefixes: "avc1.42E01E" is avc1; "mp4a.40.2" is mp4a; "vp09.00.10.08" is vp09.
    static const std::unordered_map<std::string, std::vector<std::string>> table = {
        {"audio/wav", {"1", "pcm"}},
        {"audio/wave", {"1", "pcm"}},
        {"audio/x-wav", {"1", "pcm"}},
        {"audio/mpeg", {"mp3", "mp4a"}},
        {"audio/mp3", {"mp3", "mp4a"}},
        {"audio/ogg", {"vorbis", "opus", "flac"}},
        {"audio/flac", {}},
        {"audio/x-flac", {}},
        {"audio/webm", {"opus", "vorbis"}},
        {"audio/aac", {"aac", "mp4a"}},
        {"audio/mp4", {"mp4a"}},
        {"audio/x-m4a", {"mp4a"}},
        {"video/mp4", {"avc1", "avc3", "mp4v", "mp4a", "vp8", "vp9", "vp09", "av01", "hev1", "hvc1"}},
        {"video/webm", {"vp8", "vp9", "vp09", "av01", "opus", "vorbis"}},
        {"video/ogg", {"theora", "vorbis"}},
    };
    return table;
}
std::string can_play_type(const std::string& input) {
    // Split on ';': the first word is the MIME type, the rest are parameters.
    std::string type;
    std::vector<std::string> codecs;
    bool has_codecs = false;
    const std::string lowered = ascii_lower(input);
    std::string::size_type start = 0;
    for (;;) {
        const std::string::size_type semi = lowered.find(';', start);
        const std::string piece = media_trim(lowered.substr(start, semi == std::string::npos
                                                                   ? std::string::npos
                                                                   : semi - start));
        if (start == 0 && type.empty() && semi == std::string::npos) { type = piece; break; }
        if (type.empty() && piece.find('=') == std::string::npos) {
            type = piece;
        } else {
            const std::string::size_type eq = piece.find('=');
            if (eq != std::string::npos && media_trim(piece.substr(0, eq)) == "codecs") {
                std::string value = media_trim(piece.substr(eq + 1));
                while (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                             (value.front() == '\'' && value.back() == '\'')))
                    value = value.substr(1, value.size() - 2);
                // The codecs parameter is a comma-separated list.
                std::string::size_type list_start = 0;
                for (;;) {
                    const std::string::size_type comma = value.find(',', list_start);
                    std::string token = media_trim(value.substr(list_start, comma == std::string::npos
                                                                     ? std::string::npos
                                                                     : comma - list_start));
                    if (token.size() >= 2 && ((token.front() == '"' && token.back() == '"') ||
                                              (token.front() == '\'' && token.back() == '\'')))
                        token = token.substr(1, token.size() - 2);
                    if (!token.empty()) codecs.push_back(token);
                    if (comma == std::string::npos) break;
                    list_start = comma + 1;
                }
                has_codecs = true;
            }
        }
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    const auto& table = can_play_table();
    const auto it = table.find(type);
    if (it == table.end()) return std::string();
    if (!has_codecs) return "maybe";
    if (codecs.empty()) return std::string();
    for (const std::string& token : codecs) {
        bool ok = false;
        for (const std::string& known : it->second) {
            if (token == known) { ok = true; break; }
            // A dot-separated suffix is the same codec with its profile (avc1.42E01E).
            if (token.size() > known.size() && token.compare(0, known.size(), known) == 0 &&
                token[known.size()] == '.') { ok = true; break; }
        }
        if (!ok) return std::string();
    }
    return "maybe";
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
    // A rectangle either reflects an element's `viewBox` (`box` set) or stands
    // on its own: `getBBox()` and `createSVGRect()` make one with no attribute
    // behind it, whose members a script may still write.
    std::shared_ptr<SvgViewBoxState> box;
    std::array<float, 4> values{};
    bool animated{false};
    void trace(Heap& heap) override { if (box) trace_node(heap, box->element.get()); }
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
            if (!state.box) return num(state.values[i]);
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
            if (!state.box) {
                state.values[i] = converted;
                return;
            }
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

// Which SVG elements carry `viewBox`. SVG 2 §7.7: "All elements that establish a
// new viewport ... plus the marker, pattern and view elements have attribute
// viewBox", and each of those interfaces includes SVGFitToViewBox (§4.6.9), the
// mixin whose one attribute is `viewBox`. `g`, `rect` and the rest have none:
// asking them for it is `undefined`, which is what a browser answers.
bool svg_fits_to_view_box(const Node& node) {
    if (!is_svg_node(node)) return false;
    const std::string& tag = static_cast<const Element&>(node).tag_name();
    return tag == "svg" || tag == "symbol" || tag == "marker" || tag == "pattern" || tag == "view";
}

// ---------------------------------------------------------------------------
// SVGLength: a length that remembers the unit it was written in
// ---------------------------------------------------------------------------
//
// The renderer has a length parser already (`SvgRenderer::parse_float_value`),
// but it resolves to pixels on the way in, which is exactly what SVGLength must
// not do: `unitType` and `valueInSpecifiedUnits` are separate observations of
// the same attribute, and `value` is the one that converts. So the length type
// keeps a unit and a numeric factor, and converts only when asked.
//
// The unit table is the CSS one, in SVG 2 §4.5.2's constant order. UNKNOWN (0)
// is not a unit a value can *have*: it is what `unitType` answers for a value
// the grammar knows and the constants do not (`ch`, `calc(...)`, `auto`).
enum class SvgLengthUnit : unsigned short {
    Unknown = 0, Number = 1, Percentage = 2, Ems = 3, Exs = 4, Px = 5,
    Cm = 6, Mm = 7, In = 8, Pt = 9, Pc = 10,
};

// CSS px in one of this unit; the same table `SvgRenderer` uses to paint.
double svg_length_unit_in_px(SvgLengthUnit unit) {
    switch (unit) {
        case SvgLengthUnit::Number: return 1.0;
        case SvgLengthUnit::Ems: return 16.0;         // the initial font size
        case SvgLengthUnit::Exs: return 8.0;          // half an em
        case SvgLengthUnit::Px: return 1.0;
        case SvgLengthUnit::Cm: return 96.0 / 2.54;
        case SvgLengthUnit::Mm: return 96.0 / 25.4;
        case SvgLengthUnit::In: return 96.0;
        case SvgLengthUnit::Pt: return 96.0 / 72.0;
        case SvgLengthUnit::Pc: return 96.0 / 6.0;
        default: return 1.0;
    }
}

const char* svg_length_unit_suffix(SvgLengthUnit unit) {
    switch (unit) {
        case SvgLengthUnit::Percentage: return "%";
        case SvgLengthUnit::Ems: return "em";
        case SvgLengthUnit::Exs: return "ex";
        case SvgLengthUnit::Px: return "px";
        case SvgLengthUnit::Cm: return "cm";
        case SvgLengthUnit::Mm: return "mm";
        case SvgLengthUnit::In: return "in";
        case SvgLengthUnit::Pt: return "pt";
        case SvgLengthUnit::Pc: return "pc";
        default: return "";
    }
}

// A parsed length: the number, and the unit it came with. `scalar` is false for
// a value the SVGLength can read but not express as (unit, number) -- `calc()`
// and anything else the grammar has grown; those report unitType UNKNOWN and
// valueInSpecifiedUnits 0, which is what §4.5.2 says of a non-scalar value.
struct SvgLengthValue {
    SvgLengthUnit unit{SvgLengthUnit::Number};
    float number{0.0f};
    bool scalar{true};
    // The text a non-scalar value came from (`3ch`, `calc(...)`). §4.5.2 says
    // such a value reads unitType UNKNOWN and valueInSpecifiedUnits 0, but the
    // attribute still holds what the page wrote, so it has to survive a round
    // trip through this type rather than being flattened to "".
    std::string source;
};

bool svg_length_unit_from_suffix(const std::string& suffix, SvgLengthUnit& out) {
    static const std::unordered_map<std::string, SvgLengthUnit> s_table = {
        {"", SvgLengthUnit::Number}, {"%", SvgLengthUnit::Percentage},
        {"em", SvgLengthUnit::Ems}, {"ex", SvgLengthUnit::Exs}, {"px", SvgLengthUnit::Px},
        {"cm", SvgLengthUnit::Cm}, {"mm", SvgLengthUnit::Mm}, {"in", SvgLengthUnit::In},
        {"pt", SvgLengthUnit::Pt}, {"pc", SvgLengthUnit::Pc},
    };
    const auto found = s_table.find(suffix);
    if (found == s_table.end()) return false;
    out = found->second;
    return true;
}

// Parse a `<length>` or `<percentage>` the way §4.5.2 describes: one number
// followed by an optional unit. Returns false when the text is not one.
bool parse_svg_length(const std::string& text, SvgLengthValue& out) {
    std::size_t at = 0;
    auto skip = [&]() { while (at < text.size() && svg_space(text[at])) ++at; };
    skip();
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
        // `e` starts an exponent only when digits follow it. In `7em` it is the
        // first letter of the unit, and taking it as an exponent marker makes
        // every `em`/`ex` length unparseable -- the same shape of bug as a
        // greedy `-` in a number list.
        std::size_t look = at + 1;
        if (look < text.size() && (text[look] == '+' || text[look] == '-')) ++look;
        const std::size_t first_digit = look;
        while (look < text.size() && svg_digit(text[look])) ++look;
        if (look > first_digit) at = look;
    }
    const std::string number = text.substr(start, at - start);
    std::string unit;
    while (at < text.size() && !svg_space(text[at])) unit += text[at++];
    skip();
    if (at != text.size()) return false;            // trailing junk
    SvgLengthUnit parsed_unit = SvgLengthUnit::Number;
    if (!svg_length_unit_from_suffix(unit, parsed_unit)) {
        // A unit the constants do not name: readable, but not scalar.
        out.unit = SvgLengthUnit::Unknown;
        out.number = 0.0f;
        out.scalar = false;
        out.source = text;
        return true;
    }
    out.unit = parsed_unit;
    out.number = svg_decimal_float(number);
    out.scalar = true;
    return true;
}

std::string serialize_svg_length(const SvgLengthValue& value) {
    // A non-scalar has no (unit, number) spelling, so the text it was read
    // from is what the attribute keeps. Flattening it to "" would delete a
    // value the DOM can read but not express, which is a loss, not a no-op.
    if (!value.scalar) return value.source;
    return serialize_svg_float(value.number) + svg_length_unit_suffix(value.unit);
}

// The unit a length is read in: a number has no unit to convert, so it is
// already in user units. Percentages resolve against 100 when the object has no
// associated element (§4.5.2: "size is 100").
constexpr double kSvgLengthUnassociatedPercentageBasis = 100.0;

double svg_length_in_user_units(const SvgLengthValue& value) {
    if (!value.scalar) return 0.0;
    if (value.unit == SvgLengthUnit::Percentage) {
        return static_cast<double>(value.number) * 0.01 * kSvgLengthUnassociatedPercentageBasis;
    }
    if (value.unit == SvgLengthUnit::Unknown) return value.number;
    return static_cast<double>(value.number) * svg_length_unit_in_px(value.unit);
}

// A double from script narrowed to the float an SVG length stores, keeping
// the infinities rather than flattening them to a finite number: the WebIDL
// conversion happens before any rule of §4.5.2 reads the value.
float svg_float_from_double(double converted) {
    if (std::isfinite(converted)) return static_cast<float>(converted);
    if (converted > 0) return std::numeric_limits<float>::infinity();
    if (converted < 0) return -std::numeric_limits<float>::infinity();
    return 0.0f;
}

// A length in user units expressed as a number in `unit`. This is the
// direction §4.5.2's `value` setter needs and `convertToSpecifiedUnits` walks:
// user units are the common currency, and a percentage is a share of the
// unassociated basis of 100.
float svg_length_number_in_unit(SvgLengthUnit unit, float user_units) {
    if (unit == SvgLengthUnit::Percentage) {
        return static_cast<float>(static_cast<double>(user_units) / (0.01 * kSvgLengthUnassociatedPercentageBasis));
    }
    if (unit == SvgLengthUnit::Unknown) return user_units;
    return static_cast<float>(static_cast<double>(user_units) / svg_length_unit_in_px(unit));
}

// ---------------------------------------------------------------------------
// SVGLengthList (SVG 2 §4.5.3) and SVGAnimatedLengthList (§4.6.3)
// ---------------------------------------------------------------------------
//
// `x`, `y`, `dx` and `dy` on the text elements hold a *list* of lengths -- one
// per glyph run -- not one length. Phase 395 gave them an SVGAnimatedLength,
// because every other <length> attribute is one; these four are the exception,
// and a single-length model cannot represent them at all: `x="10 20 30"` read
// as a single length reports 0, and a write to it replaces all three positions
// with one.
//
// The list is kept whole. An attribute the list is created from is parsed into
// its values once, when the list is made, and the attribute is re-written from
// them in full when any one of them changes. Serializing the whole list on
// every write is what a browser does -- the attribute stays the single source
// of truth, and `getItem(0).value = 99` rewrites `10 20 30` as `99 20 30`.
struct SvgLengthListValue {
    std::vector<SvgLengthValue> values;
};

// How a list is written back: between entries, a single space. SVG's list
// grammar accepts comma-wsp, but the attribute a browser writes uses spaces
// uniformly, which is also what its own reader expects.
std::string serialize_svg_length_list(const SvgLengthListValue& list) {
    std::string text;
    for (const auto& value : list.values) {
        if (!text.empty()) text += ' ';
        text += serialize_svg_length(value);
    }
    return text;
}

// Parse an SVG list of lengths: `<length>` entries separated by comma-wsp
// (SVG 1.1 §4.2 "comma-wsp"), in either order, with either count. An entry the
// grammar rejects makes the whole attribute invalid, which §4.5.3 says reads
// as an empty list rather than a partial one -- so `x="10 bogus 30"` is no
// list at all, and the attribute is left alone.
bool parse_svg_length_list(const std::string& text, SvgLengthListValue& out) {
    out.values.clear();
    std::size_t at = 0;
    auto skip = [&]() {
        bool jumped = false;
        while (at < text.size() && svg_space(text[at])) { ++at; jumped = true; }
        return jumped;
    };
    skip();
    while (at < text.size()) {
        // The entry's text runs to the next separator, but a comma is a
        // separator too and is not part of any length, so the whole
        // comma-wsp run is scanned here to find where the entry stops.
        const std::size_t start = at;
        while (at < text.size() && !svg_space(text[at]) && text[at] != ',') ++at;
        if (start == at) return false;                  // an empty entry: ",,"
        SvgLengthValue value;
        if (!parse_svg_length(text.substr(start, at - start), value)) return false;
        out.values.push_back(std::move(value));
        const bool spaced = skip();
        if (at < text.size() && text[at] == ',') {
            ++at;
            // comma-wsp may be followed by more whitespace; `10, 20` is one
            // separator, and `10 ,20` too.
            skip();
            if (at == text.size()) return false;        // trailing comma
        } else if (!spaced && at < text.size()) {
            return false;                               // neither comma nor space
        }
    }
    return true;
}

// `preserveAspectRatio`, the other half of SVGFitToViewBox (SVG 2 §8.14.4,
// §8.14.5). The attribute's value is a keyword pair -- `xMidYMid meet` by
// default -- and the DOM exposes it as an SVGPreserveAspectRatio whose `align`
// and `meetOrSlice` are the constants below, living in an
// SVGAnimatedPreserveAspectRatio whose baseVal and animVal disagree only while
// an animation runs (none here, so animVal follows baseVal).
//
// The two halves are not symmetric: `align` is a required keyword, while
// `<meetOrSlice>?` is optional and defaults to `meet`. An attribute the
// grammar rejects falls back to the initial pair `xMidYMid meet`, as §8.14.4
// says ("using the attribute's initial value if it is not present or invalid").
struct SvgPreserveAspectRatioState : JsExotic {
    std::shared_ptr<Element> element;
    bool animated{false};
    void trace(Heap& heap) override { trace_node(heap, element.get()); }
};

struct SvgAnimatedPreserveAspectRatioState : JsExotic {
    std::shared_ptr<Element> element;
    void trace(Heap& heap) override { trace_node(heap, element.get()); }
};

// The alignment keywords in constant order: index 0 is UNKNOWN, which no
// keyword names, so the table starts at NONE.
const char* const kPreserveAspectRatioAlignKeywords[] = {
    nullptr, "none", "xMinYMin", "xMidYMin", "xMaxYMin", "xMinYMid", "xMidYMid", "xMaxYMid",
    "xMinYMax", "xMidYMax", "xMaxYMax",
};
constexpr unsigned short kPreserveAspectRatioAlignInitial = 6;      // xMidYMid
constexpr unsigned short kMeetOrSliceMeet = 1;
constexpr unsigned short kMeetOrSliceSlice = 2;
constexpr unsigned short kMeetOrSliceInitial = kMeetOrSliceMeet;

// Parse the attribute's grammar into the two constants. `defer` is SVG 1.1
// syntax the current specification no longer lists; a value carrying it is
// invalid here and falls back with everything else.
void parse_svg_preserve_aspect_ratio(const std::string& text, unsigned short& align, unsigned short& meet_or_slice) {
    align = kPreserveAspectRatioAlignInitial;
    meet_or_slice = kMeetOrSliceInitial;
    std::size_t at = 0;
    auto skip = [&]() { while (at < text.size() && svg_space(text[at])) ++at; };
    skip();
    const std::size_t start = at;
    while (at < text.size() && !svg_space(text[at])) ++at;
    if (start == at) return;
    const std::string first = text.substr(start, at - start);
    if (first == "none") {
        align = 1;
    } else {
        for (unsigned short i = 2; i <= 10; ++i) {
            if (first == kPreserveAspectRatioAlignKeywords[i]) { align = i; break; }
        }
        if (align == kPreserveAspectRatioAlignInitial && first != "xMidYMid") return;  // invalid
    }
    skip();
    if (at == text.size()) return;
    const std::size_t second = at;
    while (at < text.size() && !svg_space(text[at])) ++at;
    const std::string tail = text.substr(second, at - second);
    if (tail == "meet") meet_or_slice = kMeetOrSliceMeet;
    else if (tail == "slice") meet_or_slice = kMeetOrSliceSlice;
    else return;                                                                    // invalid
    skip();
    if (at != text.size()) return;                                                  // trailing junk
}

void sync_svg_preserve_aspect_ratio(SvgPreserveAspectRatioState& state, unsigned short& align,
                                    unsigned short& meet_or_slice) {
    const std::string* attribute = state.element->attribute("preserveAspectRatio");
    parse_svg_preserve_aspect_ratio(attribute ? *attribute : std::string(), align, meet_or_slice);
}

// Write the pair back as the attribute, in the order and spacing SVG gives it.
void write_svg_preserve_aspect_ratio(Element& element, unsigned short align, unsigned short meet_or_slice) {
    if (align == 0) return;                        // unreachable: the setter refuses UNKNOWN
    // §8.14.4 writes both parts, always: align, one space, then the
    // meet-or-slice keyword. It does not make an exception for `none`, even
    // though the attribute grammar ignores `<meetOrSlice>` after it -- so
    // `none slice` is written and reads back as (`NONE`, `SLICE`), which is
    // what a browser writes too.
    std::string text = kPreserveAspectRatioAlignKeywords[align];
    text += ' ';
    text += meet_or_slice == kMeetOrSliceSlice ? "slice" : "meet";
    element.set_attribute("preserveAspectRatio", text);
}

bool is_svg_preserve_aspect_ratio_holder(const Node& node) {
    return svg_fits_to_view_box(node);
}

// An SVGLength either reflects one entry of an element's attribute
// (`element` and `attribute_name` set) or stands on its own, as
// `createSVGLength()` and `new SVGLength(...)` make one. The reflected kind
// reads and writes the attribute; the standalone kind owns its value.
//
// `index` is which entry of the attribute it reflects. It is 0 for every
// single-length attribute (`rect.x`, `circle.r`), and for the text elements'
// `x`/`y`/`dx`/`dy` it is the position in the list that the item was taken
// from -- `text.x.baseVal.getItem(1)` is an SVGLength reflecting entry 1.
struct SvgLengthState : JsExotic {
    std::shared_ptr<Element> element;
    std::string attribute_name;
    std::size_t index{0};
    SvgLengthValue standalone;
    bool animated{false};
    void trace(Heap& heap) override { if (element) trace_node(heap, element.get()); }
};

struct SvgAnimatedLengthState : JsExotic {
    std::shared_ptr<Element> element;
    std::string attribute_name;
    void trace(Heap& heap) override { trace_node(heap, element.get()); }
};

// The whole attribute as a list, whatever kind it is. Every length-valued
// attribute is read this way: a single length is a list of one, so the two
// kinds differ only in how many entries the text has.
SvgLengthListValue read_svg_length_list(Element& element, const std::string& attribute_name) {
    SvgLengthListValue list;
    const std::string* text = element.attribute(attribute_name);
    // An attribute that is absent or unparseable is an empty list, which is
    // the initial value §4.5.3 gives every SVGLengthList.
    if (text && !parse_svg_length_list(*text, list)) list.values.clear();
    return list;
}

void write_svg_length_list(Element& element, const std::string& attribute_name, const SvgLengthListValue& list) {
    // An empty list has no text, so the attribute is removed: `clear()` on a
    // text element's `x` leaves no `x` behind, which is what a browser does.
    if (list.values.empty()) {
        if (element.has_attribute(attribute_name)) element.remove_attribute(attribute_name);
        return;
    }
    element.set_attribute(attribute_name, serialize_svg_length_list(list));
}

// A list of one that the attribute cannot express is still a list of one: an
// unparseable single length (Phase 395's `3ch`) keeps its source text.
SvgLengthValue read_svg_length(const SvgLengthState& state) {
    if (!state.element || state.attribute_name.empty()) return state.standalone;
    const SvgLengthListValue list = read_svg_length_list(*state.element, state.attribute_name);
    // An attribute that is absent or unparseable reads as 0 user units, which
    // is the initial value §A.1.1 gives every SVGLength; so does an index the
    // list does not have.
    if (state.index >= list.values.size()) return SvgLengthValue{};
    return list.values[state.index];
}

void write_svg_length(const SvgLengthState& state, const SvgLengthValue& value) {
    if (!state.element || state.attribute_name.empty()) return;
    SvgLengthListValue list = read_svg_length_list(*state.element, state.attribute_name);
    // Writing an index the attribute does not have would silently do nothing,
    // so it appends: the list grows to hold what was written.
    while (list.values.size() <= state.index) list.values.push_back(SvgLengthValue{});
    list.values[state.index] = value;
    write_svg_length_list(*state.element, state.attribute_name, list);
}

void setup_svg_length(Iface& I) {
    const std::pair<const char*, double> constants[] = {
        {"SVG_LENGTHTYPE_UNKNOWN", 0}, {"SVG_LENGTHTYPE_NUMBER", 1}, {"SVG_LENGTHTYPE_PERCENTAGE", 2},
        {"SVG_LENGTHTYPE_EMS", 3}, {"SVG_LENGTHTYPE_EXS", 4}, {"SVG_LENGTHTYPE_PX", 5},
        {"SVG_LENGTHTYPE_CM", 6}, {"SVG_LENGTHTYPE_MM", 7}, {"SVG_LENGTHTYPE_IN", 8},
        {"SVG_LENGTHTYPE_PT", 9}, {"SVG_LENGTHTYPE_PC", 10},
    };
    for (const auto& entry : constants) I.constant(entry.first, entry.second);

    I.getter("unitType", [](Realm&) {
        auto& state = this_state<SvgLengthState>("SVGLength");
        return num(static_cast<double>(read_svg_length(state).unit));
    });
    // `value` is in user units; `valueInSpecifiedUnits` is the number as
    // written. They differ for every unit but NUMBER (§4.5.2) -- and so does
    // what a write to each one does. `value = 100` on `x="1cm"` asks for 100
    // user units, which is `2.64583cm`; writing the 100 into the number alone
    // would make it `100cm`, 37.795 times too long. Only the member that is
    // *in specified units* takes the number as written.
    I.accessor("value",
        [](Realm&) {
            auto& state = this_state<SvgLengthState>("SVGLength");
            const SvgLengthValue value = read_svg_length(state);
            if (!value.scalar) return num(0.0);
            return num(svg_length_in_user_units(value));
        },
        [](Realm& realm, const JsValue& assigned) {
            auto& state = this_state<SvgLengthState>("SVGLength");
            const double converted = to_double(assigned);
            const float number = svg_float_from_double(converted);
            if (state.animated) throw_dom(realm, "NoModificationAllowedError", "The animated length is read-only.");
            SvgLengthValue value = read_svg_length(state);
            // §4.5.2: a scalar keeps its unit and takes the length in that
            // unit; a non-scalar value becomes a plain number in user units.
            if (value.scalar) {
                value.number = svg_length_number_in_unit(value.unit, number);
            } else {
                value = SvgLengthValue{SvgLengthUnit::Number, number, true};
            }
            if (!state.element || state.attribute_name.empty()) state.standalone = value;
            else write_svg_length(state, value);
        });
    I.accessor("valueInSpecifiedUnits",
        [](Realm&) {
            auto& state = this_state<SvgLengthState>("SVGLength");
            const SvgLengthValue value = read_svg_length(state);
            return num(value.scalar ? static_cast<double>(value.number) : 0.0);
        },
        [](Realm& realm, const JsValue& assigned) {
            auto& state = this_state<SvgLengthState>("SVGLength");
            const float number = svg_float_from_double(to_double(assigned));
            if (state.animated) throw_dom(realm, "NoModificationAllowedError", "The animated length is read-only.");
            SvgLengthValue value = read_svg_length(state);
            if (value.scalar) value.number = number;
            else value = SvgLengthValue{SvgLengthUnit::Number, number, true};
            if (!state.element || state.attribute_name.empty()) state.standalone = value;
            else write_svg_length(state, value);
        });
    I.accessor("valueAsString",
        [](Realm&) {
            auto& state = this_state<SvgLengthState>("SVGLength");
            return str(serialize_svg_length(read_svg_length(state)));
        },
        [](Realm& realm, const JsValue& assigned) {
            auto& state = this_state<SvgLengthState>("SVGLength");
            const std::string text = to_str(assigned);
            if (state.animated) throw_dom(realm, "NoModificationAllowedError", "The animated length is read-only.");
            SvgLengthValue value;
            if (!parse_svg_length(text, value)) {
                throw_dom(realm, "SyntaxError", "The value is not a length.");
                return;
            }
            if (!state.element || state.attribute_name.empty()) state.standalone = value;
            else write_svg_length(state, value);
        });
    // §4.5.2: UNKNOWN and values past PC are not supported unit types.
    I.method("newValueSpecifiedUnits", 2, [](Realm& realm, Args args) {
        auto& state = this_state<SvgLengthState>("SVGLength");
        const unsigned long unit = to_ulong(args[0]);
        const float number = static_cast<float>(to_double(args[1]));
        if (state.animated) throw_dom(realm, "NoModificationAllowedError", "The animated length is read-only.");
        if (unit == 0 || unit > 10) throw_dom(realm, "NotSupportedError", "Not a length unit type.");
        SvgLengthValue value;
        value.unit = static_cast<SvgLengthUnit>(unit);
        value.number = number;
        value.scalar = true;
        if (!state.element || state.attribute_name.empty()) state.standalone = value;
        else write_svg_length(state, value);
        return undefined();
    });
    // The same absolute length in another unit. A percentage has no absolute
    // length to convert, so it goes through the unassociated basis of 100.
    I.method("convertToSpecifiedUnits", 1, [](Realm& realm, Args args) {
        auto& state = this_state<SvgLengthState>("SVGLength");
        const unsigned long unit = to_ulong(args[0]);
        if (state.animated) throw_dom(realm, "NoModificationAllowedError", "The animated length is read-only.");
        if (unit == 0 || unit > 10) throw_dom(realm, "NotSupportedError", "Not a length unit type.");
        SvgLengthValue value = read_svg_length(state);
        if (!value.scalar) { value = SvgLengthValue{}; }
        else {
            const auto target = static_cast<SvgLengthUnit>(unit);
            value.number = svg_length_number_in_unit(target, static_cast<float>(svg_length_in_user_units(value)));
            value.unit = target;
        }
        if (!state.element || state.attribute_name.empty()) state.standalone = value;
        else write_svg_length(state, value);
        return undefined();
    });
}

void setup_svg_animated_length(Iface& I) {
    for (const auto& member : {std::pair<const char*, bool>{"baseVal", false}, {"animVal", true}}) {
        const bool animated = member.second;
        I.getter(member.first, [animated](Realm& realm) {
            auto& state = this_state<SvgAnimatedLengthState>("SVGAnimatedLength");
            return cached(JsValue::current_this(), animated ? "svgLenAnim" : "svgLenBase", [&]() {
                auto length = std::make_shared<SvgLengthState>();
                length->element = state.element;
                length->attribute_name = state.attribute_name;
                length->index = 0;
                length->animated = animated;
                return realm.make("SVGLength", length);
            });
        });
    }
}

// An item handed to a list mutator. §4.5.3 takes an `SVGLength*`, and a
// browser refuses anything else with a TypeError before the index is even
// looked at; the item's own value is then copied into the list.
SvgLengthValue length_from_item_argument(Realm& realm, const JsValue& value) {
    auto* state = state_of<SvgLengthState>(value);
    if (!state) throw_dom(realm, "TypeError", "The item is not an SVGLength.");
    return read_svg_length(*state);
}

// A list index the interface checks rather than clamps: `getItem`, `removeItem`
// and `replaceItem` all throw IndexSizeError past the end (§4.5.3). WebIDL has
// already converted the argument to unsigned long, so -1 arrives as 4294967295
// and is simply past the end.
std::size_t checked_list_index(Realm& realm, const JsValue& value, std::size_t size) {
    const std::size_t index = static_cast<std::size_t>(to_ulong(value));
    if (index >= size) throw_dom(realm, "IndexSizeError", "The index is greater than the maximum bound.");
    return index;
}

// ---------------------------------------------------------------------------
// SVGLengthList and SVGAnimatedLengthList
// ---------------------------------------------------------------------------
//
// The list is a live view of the attribute, not a copy: `text.x.baseVal` is
// the same object every time it is asked for (§4.6.3's [SameObject]), and the
// values in it are the attribute's. Every mutator therefore reads the
// attribute, changes what it read, and writes the whole attribute back -- so
// an item taken from the list (`getItem(0)`) is connected to the attribute it
// came from: that item is an SVGLength reflecting entry 0, and writing its
// `value` rewrites the list.
//
// `numberOfItems` and `length` are the same number. §4.5.3 gives the list
// both, and a browser answers both.
struct SvgLengthListState : JsExotic {
    std::shared_ptr<Element> element;
    std::string attribute_name;

    SvgLengthListValue read() const {
        if (!element) return SvgLengthListValue{};
        return read_svg_length_list(*element, attribute_name);
    }
    void write(const SvgLengthListValue& list) const {
        if (!element) return;
        write_svg_length_list(*element, attribute_name, list);
    }
    bool get(JsObject*, const std::string& name, JsValue& out) override {
        std::size_t index = 0;
        if (!parse_index(name, index)) return false;
        if (index >= read().values.size()) return false;
        out = make_item(current_realm(), index);
        return true;
    }
    bool has(JsObject*, const std::string& name) override {
        std::size_t index = 0;
        return parse_index(name, index) && index < read().values.size();
    }
    JsExotic::SetResult set(JsObject*, const std::string& name, const JsValue& value) override {
        std::size_t index = 0;
        if (!parse_index(name, index)) return JsExotic::SetResult::NotHandled;
        // The list *does* take an indexed write, of an SVGLength, into the
        // place it names: `list[0] = other` puts `other`'s value at 0 and
        // keeps the count. §4.5.3 gives the interface an indexed setter, and a
        // browser enforces its argument type before the range -- which is why
        // `list[0] = 5` throws a TypeError in sloppy code and not the silent
        // nothing a refused write would be.
        auto* item = state_of<SvgLengthState>(value);
        if (!item) throw_dom(current_realm(), "TypeError", "The value is not an SVGLength.");
        SvgLengthListValue list = read();
        if (index >= list.values.size()) {
            throw_dom(current_realm(), "IndexSizeError", "The index is greater than the maximum bound.");
        }
        list.values[index] = read_svg_length(*item);
        write(list);
        return JsExotic::SetResult::Handled;
    }
    void keys(JsObject*, std::vector<std::string>& out) override {
        const std::size_t count = read().values.size();
        for (std::size_t i = 0; i < count; ++i) out.push_back(std::to_string(i));
    }
    /// A fresh SVGLength reflecting entry `index` of this list's attribute.
    /// A browser answers a new object every time (`getItem(0) !== getItem(0)`),
    /// so there is nothing to cache here.
    JsValue make_item(Realm& realm, std::size_t index) const;
    /// True while this list is an `animVal`: every item it answers is
    /// read-only (§4.6.3, §4.5.2 -- writing one throws).
    bool animated_items{false};
    void trace(Heap& heap) override { if (element) trace_node(heap, element.get()); }
};

struct SvgAnimatedLengthListState : JsExotic {
    std::shared_ptr<Element> element;
    std::string attribute_name;
    void trace(Heap& heap) override { if (element) trace_node(heap, element.get()); }
};

// The item needs a Realm to be made in, which the state does not keep (a state
// can outlive the realm that made it), so it is made from the getter's realm
// and handed the article it reflects.
JsValue SvgLengthListState::make_item(Realm& realm, std::size_t index) const {
    auto state = std::make_shared<SvgLengthState>();
    state->element = element;
    state->attribute_name = attribute_name;
    state->index = index;
    state->animated = animated_items;
    return realm.make("SVGLength", state);
}

void setup_svg_length_list(Iface& I) {
    // §4.5.3's members. `length` is the [LegacyArrayClass] one and
    // `numberOfItems` the SVG 1.1 one; both answer the size, and both are
    // read-only.
    for (const char* name : {"length", "numberOfItems"}) {
        I.getter(name, [](Realm&) {
            auto& state = this_state<SvgLengthListState>("SVGLengthList");
            return num(static_cast<double>(state.read().values.size()));
        });
    }
    I.method("clear", 0, [](Realm&, Args) {
        this_state<SvgLengthListState>("SVGLengthList").write(SvgLengthListValue{});
        return undefined();
    });
    I.method("initialize", 1, [](Realm& realm, Args args) {
        auto& state = this_state<SvgLengthListState>("SVGLengthList");
        SvgLengthListValue list;
        list.values.push_back(length_from_item_argument(realm, args[0]));
        state.write(list);
        return state.make_item(realm, 0);
    });
    I.method("insertItemBefore", 2, [](Realm& realm, Args args) {
        auto& state = this_state<SvgLengthListState>("SVGLengthList");
        SvgLengthListValue list = state.read();
        const SvgLengthValue entry = length_from_item_argument(realm, args[0]);
        // The index is clamped rather than checked (§4.5.3: past the end is
        // the end, and a negative converts to 0 by WebIDL's unsigned long).
        const std::size_t at = std::min(std::size_t(to_ulong(args[1])), list.values.size());
        list.values.insert(list.values.begin() + static_cast<std::ptrdiff_t>(at), entry);
        state.write(list);
        return state.make_item(realm, at);
    });
    I.method("removeItem", 1, [](Realm& realm, Args args) {
        auto& state = this_state<SvgLengthListState>("SVGLengthList");
        SvgLengthListValue list = state.read();
        const std::size_t at = checked_list_index(realm, args[0], list.values.size());
        SvgLengthListValue removed;
        removed.values.push_back(list.values[at]);
        list.values.erase(list.values.begin() + static_cast<std::ptrdiff_t>(at));
        state.write(list);
        // The removed item keeps reading the attribute it no longer belongs to,
        // so it is answered as a length the attribute no longer has: 0.
        auto item = std::make_shared<SvgLengthState>();
        item->standalone = removed.values[0];
        return realm.make("SVGLength", item);
    });
    I.method("appendItem", 1, [](Realm& realm, Args args) {
        auto& state = this_state<SvgLengthListState>("SVGLengthList");
        SvgLengthListValue list = state.read();
        list.values.push_back(length_from_item_argument(realm, args[0]));
        state.write(list);
        return state.make_item(realm, list.values.size() - 1);
    });
    I.method("replaceItem", 2, [](Realm& realm, Args args) {
        auto& state = this_state<SvgLengthListState>("SVGLengthList");
        SvgLengthListValue list = state.read();
        const SvgLengthValue entry = length_from_item_argument(realm, args[0]);
        const std::size_t at = checked_list_index(realm, args[1], list.values.size());
        list.values[at] = entry;
        state.write(list);
        return state.make_item(realm, at);
    });
    I.method("getItem", 1, [](Realm& realm, Args args) {
        auto& state = this_state<SvgLengthListState>("SVGLengthList");
        const std::size_t at = checked_list_index(realm, args[0], state.read().values.size());
        return state.make_item(realm, at);
    });
}

void setup_svg_animated_length_list(Iface& I) {
    for (const auto& member : {std::pair<const char*, bool>{"baseVal", false}, {"animVal", true}}) {
        const bool animated = member.second;
        I.getter(member.first, [animated](Realm& realm) {
            auto& state = this_state<SvgAnimatedLengthListState>("SVGAnimatedLengthList");
            return cached(JsValue::current_this(), animated ? "svgLenListAnim" : "svgLenListBase", [&]() {
                auto list = std::make_shared<SvgLengthListState>();
                list->element = state.element;
                list->attribute_name = state.attribute_name;
                list->animated_items = animated;
                return realm.make("SVGLengthList", list);
            });
        });
    }
}

void setup_svg_preserve_aspect_ratio(Iface& I) {
    const std::pair<const char*, double> constants[] = {
        {"SVG_PRESERVEASPECTRATIO_UNKNOWN", 0}, {"SVG_PRESERVEASPECTRATIO_NONE", 1},
        {"SVG_PRESERVEASPECTRATIO_XMINYMIN", 2}, {"SVG_PRESERVEASPECTRATIO_XMIDYMIN", 3},
        {"SVG_PRESERVEASPECTRATIO_XMAXYMIN", 4}, {"SVG_PRESERVEASPECTRATIO_XMINYMID", 5},
        {"SVG_PRESERVEASPECTRATIO_XMIDYMID", 6}, {"SVG_PRESERVEASPECTRATIO_XMAXYMID", 7},
        {"SVG_PRESERVEASPECTRATIO_XMINYMAX", 8}, {"SVG_PRESERVEASPECTRATIO_XMIDYMAX", 9},
        {"SVG_PRESERVEASPECTRATIO_XMAXYMAX", 10},
        {"SVG_MEETORSLICE_UNKNOWN", 0}, {"SVG_MEETORSLICE_MEET", 1}, {"SVG_MEETORSLICE_SLICE", 2},
    };
    for (const auto& entry : constants) I.constant(entry.first, entry.second);

    for (const auto& member : {std::pair<const char*, bool>{"align", false}, {"meetOrSlice", true}}) {
        const bool is_meet_or_slice = member.second;
        I.accessor(member.first, [is_meet_or_slice](Realm&) {
            auto& state = this_state<SvgPreserveAspectRatioState>("SVGPreserveAspectRatio");
            unsigned short align = 0, meet_or_slice = 0;
            sync_svg_preserve_aspect_ratio(state, align, meet_or_slice);
            return num(static_cast<double>(is_meet_or_slice ? meet_or_slice : align));
        }, [is_meet_or_slice](Realm& realm, const JsValue& value) {
            auto& state = this_state<SvgPreserveAspectRatioState>("SVGPreserveAspectRatio");
            // WebIDL converts to unsigned short before the read-only half refuses.
            const unsigned long converted = to_ulong(value);
            const unsigned short number = static_cast<unsigned short>(converted & 0xFFFF);
            if (state.animated) throw_dom(realm, "NoModificationAllowedError", "The animated value is read-only.");
            unsigned short align = 0, meet_or_slice = 0;
            sync_svg_preserve_aspect_ratio(state, align, meet_or_slice);
            if (is_meet_or_slice) {
                // §8.14.4: a value with no keyword (UNKNOWN, or past SLICE) throws.
                if (number != kMeetOrSliceMeet && number != kMeetOrSliceSlice) {
                    throw_dom(realm, "TypeError", "Not a meet-or-slice keyword.");
                }
                meet_or_slice = number;
            } else {
                // UNKNOWN (0) has no keyword, so it cannot be written either;
                // §8.14.4 rejects anything without an entry in the keyword
                // table, which is 0 and everything past XMAXYMAX.
                if (number == 0 || number > 10) throw_dom(realm, "TypeError", "Not an alignment keyword.");
                align = number;
            }
            write_svg_preserve_aspect_ratio(*state.element, align, meet_or_slice);
        });
    }
}

void setup_svg_animated_preserve_aspect_ratio(Iface& I) {
    for (const auto& member : {std::pair<const char*, bool>{"baseVal", false}, {"animVal", true}}) {
        const bool animated = member.second;
        I.getter(member.first, [animated](Realm& realm) {
            auto& state = this_state<SvgAnimatedPreserveAspectRatioState>("SVGAnimatedPreserveAspectRatio");
            return cached(JsValue::current_this(), animated ? "svgParAnim" : "svgParBase", [&]() {
                auto part = std::make_shared<SvgPreserveAspectRatioState>();
                part->element = state.element;
                part->animated = animated;
                return realm.make("SVGPreserveAspectRatio", part);
            });
        });
    }
}

void setup_svg_fit_to_view_box(Iface& I) {
    // SVGFitToViewBox has two attributes, and both live here: `viewBox` and
    // `preserveAspectRatio` (SVG 2 §4.6.9). The elements that include it are
    // the same five, so one setup serves both.
    I.node_getter(&is_svg_preserve_aspect_ratio_holder, "preserveAspectRatio", [](Realm& realm, Node& node) {
        return cached(wrap(realm, node), "svgPAR", [&]() {
            auto state = std::make_shared<SvgAnimatedPreserveAspectRatioState>();
            state->element = shared(static_cast<Element&>(node));
            return realm.make("SVGAnimatedPreserveAspectRatio", state);
        });
    });
    I.node_getter(&svg_fits_to_view_box, "viewBox", [](Realm& realm, Node& node) {
        return cached(wrap(realm, node), "svgViewBox", [&]() {
            auto state = std::make_shared<SvgAnimatedRectState>();
            state->box = std::make_shared<SvgViewBoxState>();
            state->box->element = shared(static_cast<Element&>(node));
            return realm.make("SVGAnimatedRect", state);
        });
    });
}

// A graphics element's bounding box: the geometry the layout gave it, as an
// SVGRect of its own. SVG 2 §4.4.2 defines it in current user space; with no
// transforms implemented that is the painted border box, which is what the
// layout already computes for getBoundingClientRect.
bool is_svg_graphics_element(const Node& node) {
    if (!is_svg_node(node)) return false;
    const std::string& tag = static_cast<const Element&>(node).tag_name();
    return tag == "svg" || tag == "symbol" || tag == "marker" || tag == "pattern" || tag == "view" ||
           tag == "g" || tag == "rect" || tag == "circle" || tag == "ellipse" || tag == "line" ||
           tag == "path" || tag == "polygon" || tag == "polyline" || tag == "text" || tag == "tspan" ||
           tag == "textPath" || tag == "use" || tag == "image" || tag == "foreignObject" || tag == "a";
}

// The geometry attributes that are lengths, and the elements that carry each.
// SVG 2 gives every one of these an SVGAnimatedLength, so `el.x` is an object
// whose baseVal is an SVGLength -- not a number, and not undefined.
bool svg_length_attribute(const std::string& tag, const std::string& attribute) {
    static const std::unordered_map<std::string, std::vector<std::string>> s_table = {
        {"svg", {"x", "y", "width", "height"}},
        {"rect", {"x", "y", "width", "height", "rx", "ry"}},
        {"circle", {"cx", "cy", "r"}},
        {"ellipse", {"cx", "cy", "rx", "ry"}},
        {"line", {"x1", "y1", "x2", "y2"}},
        {"image", {"x", "y", "width", "height"}},
        {"use", {"x", "y", "width", "height"}},
        {"foreignObject", {"x", "y", "width", "height"}},
        {"pattern", {"x", "y", "width", "height"}},
        {"marker", {"refX", "refY", "markerWidth", "markerHeight"}},
        {"filter", {"x", "y", "width", "height"}},
        {"mask", {"x", "y", "width", "height"}},
        {"feImage", {"x", "y", "width", "height"}},
        // `text`, `tspan` and `textPath` are *not* here: their `x` and `y` are
        // SVGAnimatedLengthLists (§4.6.3), not single lengths. Phase 395 put
        // them in this table, which made `x="10 20 30"` read as one length --
        // 0 -- and a write to it replace all three positions with one.
    };
    const auto found = s_table.find(tag);
    if (found == s_table.end()) return false;
    for (const auto& name : found->second) {
        if (name == attribute) return true;
    }
    return false;
}
// Every length-valued geometry attribute, as an SVGAnimatedLength. The getter
// is installed once per interface and answers for whichever attributes that
// interface's elements actually carry (`svg_length_attribute`), so `rect.rx`
// exists and `circle.rx` does not.
void setup_svg_length_attributes(Iface& I) {
    static const char* const kLengthAttributes[] = {
        "x", "y", "width", "height", "rx", "ry", "cx", "cy", "r", "x1", "y1", "x2", "y2",
        "refX", "refY", "markerWidth", "markerHeight",
    };
    for (const char* attribute : kLengthAttributes) {
        const std::string name = attribute;
        // `text` and `tspan` do not carry a single length called `x` or `y`:
        // theirs are lists, installed by setup_svg_text_positioning_element on
        // the interface that inherits this one. The getter stays out of their
        // way so the two setups cannot answer for the same name, whichever
        // order the prototypes are built in.
        if (name == "x" || name == "y") {
            I.node_getter(&is_svg_node, attribute, [name](Realm& realm, Node& node) {
                auto& element = static_cast<Element&>(node);
                const std::string tag = element.tag_name();
                if (tag == "text" || tag == "tspan") return JsValue();
                if (!svg_length_attribute(tag, name)) return JsValue();
                return cached(wrap(realm, node), "svgLen:" + name, [&]() {
                    auto state = std::make_shared<SvgAnimatedLengthState>();
                    state->element = shared(element);
                    state->attribute_name = name;
                    return realm.make("SVGAnimatedLength", state);
                });
            });
            continue;
        }
        // The interface this getter belongs to is not known here, so the
        // element decides: a tag that does not carry the attribute in SVG 2
        // does not have it at all, and asking answers `undefined`.
        I.node_getter(&is_svg_node, attribute, [name](Realm& realm, Node& node) {
            auto& element = static_cast<Element&>(node);
            // Only the elements SVG 2 gives this attribute carry it; asking a
            // `<circle>` for `rx` answers undefined, as a browser does.
            if (!svg_length_attribute(element.tag_name(), name)) return JsValue();
            return cached(wrap(realm, node), "svgLen:" + name, [&]() {
                auto state = std::make_shared<SvgAnimatedLengthState>();
                state->element = shared(element);
                state->attribute_name = name;
                return realm.make("SVGAnimatedLength", state);
            });
        });
    }
}


// `SVGTextPositioningElement`'s four lists (SVG 2 §4.6.3): `x` and `y` place
// each glyph run, `dx` and `dy` shift it. All four are SVGAnimatedLengthLists
// and all four are read and written through the attribute, so one setup serves
// both elements that have them, `text` and `tspan`.
//
// `rotate` sits beside them as an SVGAnimatedNumberList (§4.6.3); it is a list
// of numbers rather than lengths and is not built here.
void setup_svg_text_positioning_element(Iface& I) {
    for (const char* attribute : {"x", "y", "dx", "dy"}) {
        const std::string name = attribute;
        I.node_getter(&is_svg_node, attribute, [name](Realm& realm, Node& node) {
            auto& element = static_cast<Element&>(node);
            return cached(wrap(realm, node), "svgLenList:" + name, [&]() {
                auto state = std::make_shared<SvgAnimatedLengthListState>();
                state->element = shared(element);
                state->attribute_name = name;
                return realm.make("SVGAnimatedLengthList", state);
            });
        });
    }
}

void setup_svg_text_element(Iface& I) {
    setup_svg_length_attributes(I);
    setup_svg_text_positioning_element(I);
}

void setup_svg_tspan_element(Iface& I) {
    setup_svg_length_attributes(I);
    setup_svg_text_positioning_element(I);
}

void setup_svg_text_path_element(Iface& I) {
    // `SVGTextPathElement` inherits `SVGTextContentElement` directly, not
    // `SVGTextPositioningElement`: a text path takes its positions from the
    // path, so it has none of the four lists, and a browser answers all four
    // as absent. Only `text` and `tspan` carry them.
    setup_svg_length_attributes(I);
}

// `getBBox()` is the box of the geometry the element *draws*, in its own user
// space, with its own transform excluded and its children's included (SVG 2
// §8.10). It is not a layout box -- an SVG element has none, and Phase 374
// shipped this reading `Element::bounding_box()`, the HTML layout border box,
// which is empty for every SVG element. Every call answered 0,0,0,0.
//
// The box comes from the renderer's own geometry, so what a page measures and
// what the painter draws cannot drift apart.
void setup_svg_graphics_element(Iface& I) {
    I.node_method(&is_svg_graphics_element, "getBBox", 0, [](Realm& realm, Node& node, Args) {
        auto rect = std::make_shared<SvgRectState>();
        const SvgUserBBox box = svg_element_bbox(shared(static_cast<Element&>(node)));
        rect->values = {box.x, box.y, box.width, box.height};
        return realm.make("SVGRect", rect);
    });
}

// `createSVGRect()` is SVGSVGElement's, SVG 2 §5.13.2: a fresh empty rect.
void setup_svg_marker_element(Iface& I) {
    setup_svg_fit_to_view_box(I);
    setup_svg_length_attributes(I);
}

void setup_svg_svg_element(Iface& I) {
    setup_svg_fit_to_view_box(I);
    setup_svg_length_attributes(I);
    I.method("createSVGRect", 0, [](Realm& realm, Args) {
        return realm.make("SVGRect", std::make_shared<SvgRectState>());
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

/// A built-in from the realm's global object or, when there is no Window, its
/// global environment. Worker-safe DOM interfaces must not treat "global" and
/// "Window" as synonyms.
JsValue realm_global_binding(Realm& realm, const std::string& name) {
    if (realm.window.is_object()) return realm.window.get_property(name);
    if (auto scope = realm.global_scope.lock()) return scope->get(name);
    return JsValue();
}

/// The realm's `Uint8ClampedArray`, `length` bytes of zero.
JsValue clamped_array(Realm& realm, std::size_t length) {
    const JsValue array_ctor = realm_global_binding(realm, "Uint8ClampedArray");
    if (!array_ctor.is_function()) {
        js_throw_type_error("ImageData needs a realm with Uint8ClampedArray.");
    }
    // Constructed, not called: a typed-array constructor refuses a plain call
    // now (`Constructor Uint8ClampedArray requires 'new'`), and this is the
    // engine making the array for an ImageData, not a script. `js_call` here
    // reached the native through [[Call]] and threw.
    return js_construct(array_ctor, {JsValue::make_number(static_cast<double>(length))});
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
    const JsValue array_ctor = realm_global_binding(realm, "Uint8ClampedArray");
    JsValue tag_of;
    if (array_ctor.is_function() && first.is_object_like()) {
        // `[object Uint8ClampedArray]` and nothing else: the internal slot the
        // interface is defined over, not a shape that looks like one.
        const JsValue object_ctor = realm_global_binding(realm, "Object");
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

// ---------------------------------------------------------------------------
// §4.12.5.1.7's `Path2D`
// ---------------------------------------------------------------------------

/// The script object's own state: the recorded calls. Held on the object as its
/// `JsExotic`, and mirrored into the value's payload by `construct_path2d` so the
/// four context methods can recognise a path argument in one `std::get_if`.
struct Path2DState : JsExotic {
    std::shared_ptr<Path2D> path = std::make_shared<Path2D>();
};

Path2DState& this_path2d() { return this_state<Path2DState>("Path2D"); }

/// The `(unrestricted double or DOMPointInit)` member used by `roundRect`.
/// Inner arrays retain this engine's `[x, y]` point spelling as well.
Point path_corner_radius(const JsValue& value) {
    Point radius;
    if (value.is_array()) {
        const auto& pair = value.array_elements();
        radius.x = pair.empty() ? 0.0f : static_cast<float>(to_double(pair[0]));
        radius.y = pair.size() < 2 ? 0.0f : static_cast<float>(to_double(pair[1]));
    } else if (value.is_object_like()) {
        const JsValue x = value.get_property("x");
        const JsValue y = value.get_property("y");
        radius.x = x.is_undefined() ? 0.0f : static_cast<float>(to_double(x));
        radius.y = y.is_undefined() ? 0.0f : static_cast<float>(to_double(y));
    } else {
        radius.x = radius.y = static_cast<float>(to_double(value));
    }
    if (radius.x < 0.0f || radius.y < 0.0f) {
        js_throw_range_error("Path2D.roundRect radius components must be non-negative");
    }
    return radius;
}

/// One recorded call, from an argument list. Written once and used by every
/// method so the offsets cannot drift between them.
bool op_from_args(const std::string& name, Args args, Path2D::Op& op) {
    const auto number = [&args](std::size_t i) { return static_cast<float>(to_double(args[i])); };
    const auto need = [&args](std::size_t n, const char* what) {
        require_args(args, n, what);
    };
    if (name == "moveTo") { need(2, "Path2D.moveTo"); op.kind = Path2D::Kind::MoveTo; op.a = number(0); op.b = number(1); return true; }
    if (name == "lineTo") { need(2, "Path2D.lineTo"); op.kind = Path2D::Kind::LineTo; op.a = number(0); op.b = number(1); return true; }
    if (name == "closePath") { op.kind = Path2D::Kind::ClosePath; return true; }
    if (name == "quadraticCurveTo") {
        need(4, "Path2D.quadraticCurveTo"); op.kind = Path2D::Kind::QuadraticCurveTo;
        op.a = number(0); op.b = number(1); op.c = number(2); op.d = number(3); return true;
    }
    if (name == "bezierCurveTo") {
        need(6, "Path2D.bezierCurveTo"); op.kind = Path2D::Kind::BezierCurveTo;
        op.a = number(0); op.b = number(1); op.c = number(2); op.d = number(3); op.e = number(4); op.f = number(5);
        return true;
    }
    if (name == "arcTo") {
        need(5, "Path2D.arcTo"); op.kind = Path2D::Kind::ArcTo;
        op.a = number(0); op.b = number(1); op.c = number(2); op.d = number(3); op.e = number(4); return true;
    }
    if (name == "arc") {
        need(5, "Path2D.arc"); op.kind = Path2D::Kind::Arc;
        op.a = number(0); op.b = number(1); op.c = number(2); op.d = number(3); op.e = number(4);
        op.clockwise = args.size() > 5 && args[5].is_truthy(); return true;
    }
    if (name == "ellipse") {
        need(7, "Path2D.ellipse"); op.kind = Path2D::Kind::Ellipse;
        op.a = number(0); op.b = number(1); op.c = number(2); op.d = number(3);
        op.e = number(4); op.f = number(5); op.g = number(6);
        op.clockwise = args.size() > 7 && args[7].is_truthy(); return true;
    }
    if (name == "rect") { need(4, "Path2D.rect"); op.kind = Path2D::Kind::Rect; op.a = number(0); op.b = number(1); op.c = number(2); op.d = number(3); return true; }
    if (name == "roundRect") {
        need(4, "Path2D.roundRect");
        op.a = number(0); op.b = number(1); op.c = number(2); op.d = number(3);
        // `radii` is one number or a sequence of up to four, each of which may be
        // itself a pair `[x, y]` for an elliptical corner. The context's binding
        // already reads that grammar for its own `roundRect`; this records the
        // same four corners so the replay goes through the same overload, and the
        // two cannot drift. A single number, or no argument, stays the one-radius
        // kind, which is what the context's own no-argument case does.
        if (args.size() <= 4 || args[4].is_nullish()) {
            op.kind = Path2D::Kind::RoundRect;
            op.e = 0.0f;
            return true;
        }
        std::vector<Point> corners;  // top-left, top-right, bottom-right, bottom-left
        const JsValue& radii = args[4];
        if (radii.is_array()) {
            const auto& a = radii.array_elements();
            if (a.empty() || a.size() > 4) {
                js_throw_range_error("Path2D.roundRect requires between one and four radii");
            }
            std::vector<Point> given;
            given.reserve(a.size());
            for (const JsValue& value : a) given.push_back(path_corner_radius(value));
            // CSS's clockwise spreading rule, now spreading both ellipse axes.
            if (given.size() == 1) corners = {given[0], given[0], given[0], given[0]};
            else if (given.size() == 2) corners = {given[0], given[1], given[0], given[1]};
            else if (given.size() == 3) corners = {given[0], given[1], given[2], given[1]};
            else corners = {given[0], given[1], given[2], given[3]};
        } else {
            const Point radius = path_corner_radius(radii);
            corners = {radius, radius, radius, radius};
        }
        const auto same = [](Point left, Point right) {
            return left.x == right.x && left.y == right.y;
        };
        // A circular radius common to all four keeps the compact one-radius kind.
        if (corners[0].x == corners[0].y && same(corners[0], corners[1]) &&
            same(corners[1], corners[2]) && same(corners[2], corners[3])) {
            op.kind = Path2D::Kind::RoundRect;
            op.e = corners[0].x;
            return true;
        }
        op.kind = Path2D::Kind::RoundRectRadii;
        for (int i = 0; i < 4; ++i) op.rr[i] = corners[static_cast<std::size_t>(i)];
        return true;
    }
    return false;
}

void install_path2d_methods(Realm& r, JsValue object, const std::shared_ptr<Path2D>& path) {
    for (const char* name : {"moveTo", "lineTo", "closePath", "quadraticCurveTo", "bezierCurveTo",
                             "arcTo", "arc", "ellipse", "rect", "roundRect"}) {
        const std::string which = name;
        const int length = (which == "closePath") ? 0 : (which == "rect" || which == "roundRect") ? 4
                          : (which == "quadraticCurveTo") ? 4 : (which == "bezierCurveTo") ? 6
                          : (which == "arcTo" || which == "arc") ? 5 : (which == "ellipse") ? 7 : 2;
        object.set_property(name, make_function(r, name, length,
            [path, which](Realm&, Args args) {
                Path2D::Op op;
                if (op_from_args(which, args, op)) path->add_op(std::move(op));
                return undefined();
            }));
    }
    // `addPath(path, transform)`: another `Path2D`, and optionally the matrix to
    // apply to its points. The path is kept by pointer, so adding the same path
    // twice adds its calls twice, which is what the specification says.
    object.set_property("addPath", make_function(r, "addPath", 1,
        [path](Realm&, Args args) {
            require_args(args, 1, "Path2D.addPath");
            auto other = args[0].path2d_value();
            if (!other) return undefined();
            Path2D::Op op;
            op.kind = Path2D::Kind::AddPath;
            op.path = other;
            if (args.size() >= 7) {
                op.transform = TransformMatrix2D{
                    static_cast<float>(to_double(args[1])), static_cast<float>(to_double(args[2])),
                    static_cast<float>(to_double(args[3])), static_cast<float>(to_double(args[4])),
                    static_cast<float>(to_double(args[5])), static_cast<float>(to_double(args[6]))};
            }
            path->add_op(std::move(op));
            return undefined();
        }));
}

JsValue construct_path2d(Realm& r, const InterfaceDef&, Args args, const JsValue* derived) {
    // Three forms in §4.12.5.1.7: `new Path2D()`, `new Path2D(svgPathString)` and
    // `new Path2D(otherPath)`, which copies the other path's recorded calls at
    // the moment of the copy.
    auto state = std::make_shared<Path2DState>();
    if (!args.empty() && !args[0].is_undefined()) {
        if (auto other = args[0].path2d_value()) {
            state->path = std::make_shared<Path2D>(*other);
        } else if (args[0].type == JsType::String) {
            // SVG path data. `build_svg_path` in SvgRenderer walks the grammar into
            // a sink, so the string is recorded as the equivalent calls and is
            // replayed into a context the same way a hand-built path is.
            state->path = parse_svg_path_data(args[0].to_string());
            if (!state->path) js_throw_type_error("Path2D: the path data could not be parsed");
        } else {
            js_throw_type_error("Path2D: the argument must be path data or a Path2D");
        }
    }
    // The value the page holds carries the path in its payload as well as in the
    // exotic state, so `ctx.fill(p)` recognises it with one `std::get_if`.
    JsValue value = r.make("Path2D", state, derived);
    value.set_path2d_value(state->path);
    install_path2d_methods(r, value, state->path);
    return value;
}

void setup_path2d(Iface&) {}

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

// Defined below, beside toDataURL(): both need the same answer to "what
// bitmap does this canvas have", including for a canvas that never had a
// context.
std::shared_ptr<ImageData> canvas_bitmap_for_serialization(Element& el);

// --- CanvasRenderingContext2D.drawImage() ----------------------------------
//
// HTML §4.12.5.1 and the 2D context's drawing model: `drawImage(image, dx, dy)`
// and its three- and five-argument forms, where the source is an
// `HTMLCanvasElement`, an `ImageBitmap`, or an `HTMLImageElement` whose
// bitmap the layout pass has already decoded.
//
// The origin-clean flag is what this method is *for*. Drawing a source that is
// not known to be CORS-same-origin makes the destination canvas's bitmap
// unclean, and `toDataURL()` then refuses it. The flag is only ever cleared
// here, and only ever set by a resize, which replaces the bitmap.
//
// What this engine can read decides what it can be asked. A canvas and an
// ImageBitmap have pixels it holds; an `<img>` has pixels only if the loader
// decoded them, and the loader records whether the fetch was cross-origin
// (ResourceResponse::cross_origin) — a cross-origin `<img>` without
// `crossorigin` therefore taints. A source whose pixels are unreadable is
// rejected rather than drawn as a blank box, because a blank draw would be a
// silent wrong answer where the standard has a thrown one.

namespace {

/// The pixels a drawImage source holds, and whether they are known to be
/// CORS-same-origin. False when the source is not something this engine can
/// read at all.
struct DrawSource {
    std::shared_ptr<ImageData> pixels;
    bool origin_clean{true};
};

bool draw_source_from(const JsValue& value, DrawSource& out) {
    // A canvas: its bitmap is the context's, and a canvas that never had a
    // context still has a blank bitmap of its size.
    if (std::shared_ptr<Element> el = element_of(value); el && el->tag_name() == "canvas") {
        if (auto ctx = el->canvas_context()) {
            if (ctx->has_backing_store()) {
                out.pixels = ctx->image_data();
                out.origin_clean = ctx->origin_clean();
                return out.pixels != nullptr;
            }
            return false;
        }
        out.pixels = canvas_bitmap_for_serialization(*el);
        // A fresh blank bitmap of the element's size is clean: no pixel in it
        // came from anywhere.
        out.origin_clean = true;
        return out.pixels != nullptr;
    }
    // An ImageBitmap: the pixels are the state's, and the state carries
    // whether the bitmap was made from something origin-clean.
    if (ImageBitmapState* bm = state_of<ImageBitmapState>(value)) {
        if (bm->closed) return false;
        auto image = std::make_shared<ImageData>();
        image->width = bm->width;
        image->height = bm->height;
        image->pixels = bm->pixels;
        out.pixels = std::move(image);
        out.origin_clean = bm->origin_clean;
        return out.pixels->is_valid();
    }
    // An <img>: whatever the layout pass decoded for it. `decoded_cross_origin`
    // records a fetch that was not CORS-same-origin.
    if (std::shared_ptr<Element> el = element_of(value); el && el->tag_name() == "img") {
        std::shared_ptr<ImageData> decoded = load_image_for_element(*el);
        if (!decoded || !decoded->is_valid()) return false;
        out.pixels = std::move(decoded);
        out.origin_clean = !el->image_source_cross_origin();
        return true;
    }
    return false;
}

} // namespace

void install_draw_image(Realm& r, JsValue context_object,
                        const std::shared_ptr<CanvasRenderingContext2D>& context) {
    auto get_ctx = [context]() -> CanvasRenderingContext2D* { return context.get(); };

    context_object.set_property("drawImage", make_function(r, "drawImage", 3, [get_ctx](Realm& realm, Args args) {
        require_args(args, 3, "CanvasRenderingContext2D.drawImage");
        DrawSource source;
        if (!draw_source_from(args[0], source)) {
            js_throw_type_error(
                "Failed to execute 'drawImage' on 'CanvasRenderingContext2D': the provided value is not "
                "an image source this engine can read.");
        }
        // §4.12.5.1 step: a source that is not origin-clean clears the flag.
        if (!source.origin_clean) get_ctx()->set_origin_clean(false);

        // 3, 5 and 9 argument forms. The 9-argument form takes a source
        // rectangle first; only the forms this engine draws are accepted.
        double dx = 0, dy = 0, dw = 0, dh = 0;
        const std::size_t n = args.size();
        if (n == 3) {
            dx = to_double(args[1]);
            dy = to_double(args[2]);
        } else if (n == 5) {
            dx = to_double(args[1]);
            dy = to_double(args[2]);
            dw = to_double(args[3]);
            dh = to_double(args[4]);
        } else if (n == 9) {
            // Source rectangle, cropped out of the pixels before drawing.
            const double sx = to_double(args[1]);
            const double sy = to_double(args[2]);
            const double sw = to_double(args[3]);
            const double sh = to_double(args[4]);
            dx = to_double(args[5]);
            dy = to_double(args[6]);
            dw = to_double(args[7]);
            dh = to_double(args[8]);
            const int ix = static_cast<int>(std::lround(sx));
            const int iy = static_cast<int>(std::lround(sy));
            const int iw = static_cast<int>(std::lround(sw));
            const int ih = static_cast<int>(std::lround(sh));
            if (iw <= 0 || ih <= 0) return undefined();
            auto cropped = std::make_shared<ImageData>();
            cropped->width = iw;
            cropped->height = ih;
            cropped->pixels.assign(static_cast<std::size_t>(iw) * static_cast<std::size_t>(ih), 0);
            for (int y = 0; y < ih; ++y) {
                for (int x = 0; x < iw; ++x) {
                    cropped->pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(iw) +
                                    static_cast<std::size_t>(x)] = source.pixels->get_pixel(ix + x, iy + y);
                }
            }
            source.pixels = std::move(cropped);
        } else {
            js_throw_type_error(
                "Failed to execute 'drawImage' on 'CanvasRenderingContext2D': 3, 5 or 9 arguments required.");
        }
        get_ctx()->draw_image(source.pixels, static_cast<float>(dx), static_cast<float>(dy),
                              static_cast<float>(dw), static_cast<float>(dh));
        (void)realm;
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
// `drawImage` draws from sources that carry the origin-clean bit
// (Phase 237): an `<img>` whose fetch was not CORS-same-origin, a canvas or
// an ImageBitmap that is itself not clean, clears the flag this method must
// consult. A bitmap that never saw one of those holds only a script's own
// drawing and is clean.

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

    // HTML §4.12.5.1 and §4.12.5.4: a bitmap that is not origin-clean must not
    // be handed back, whatever type was asked for — the check comes before the
    // type is looked at, because the pixels are what is protected, not the
    // encoding. This is the whole point of the flag: it is the only thing
    // standing between a page and a cross-origin image it drew.
    if (auto context = el.canvas_context(); context && !context->origin_clean()) {
        throw_dom(current_realm(), "SecurityError",
                  "Failed to execute 'toDataURL' on 'HTMLCanvasElement': the canvas has been tainted by "
                  "cross-origin data.");
    }

    std::string canonical;
    if (!png_mime_type(type, canonical)) return str("data:,");

    const std::shared_ptr<ImageData> bitmap = canvas_bitmap_for_serialization(el);
    if (!bitmap || !bitmap->is_valid()) return str("data:,");

    std::vector<uint8_t> png;
    if (!ImageDecoder::encode_png(*bitmap, png)) return str("data:,");
    return str("data:" + canonical + ";base64," + base64_encode(png.data(), png.size()));
}

/// The same serialization as `canvas_to_data_url`, into a `Blob`, with the
/// callback fired in a task of its own (WHATWG HTML §4.12.5.4). The engine
/// encodes one type, `image/png`; anything else — a type with no encoder, a
/// string that is not a MIME type, a bitmap there is nothing to put in a
/// Blob — hands the callback `null`, the answer the encoding failure takes,
/// rather than a promise the page can neither use nor detect. The quality
/// argument is accepted and ignored: it selects between lossy encoders, of
/// which there are none.
JsValue canvas_to_blob(Realm& r, Element& el, Args args) {
    require_args(args, 1, "HTMLCanvasElement.toBlob");
    const JsValue callback = js_arg(args, 0);
    if (!callback.is_callable()) {
        js_throw_type_error(
            "Failed to execute 'toBlob' on 'HTMLCanvasElement': parameter 1 is not of type 'Function'.");
    }
    // Only an omitted type means the default, exactly as `toDataURL` reads it:
    // an explicit `null` is the string "null", which is not a MIME type.
    std::string type = "image/png";
    if (args.size() > 1 && !args[1].is_undefined()) type = to_str(args[1]);

    // §4.12.5.1 and §4.12.5.4: an unclean bitmap is not handed back,
    // whatever type was asked for. The check comes before the type is looked
    // at, because the pixels are what is protected, not the encoding — the
    // same rule `toDataURL()` enforces, and the reason this method is part
    // of the origin-clean flag's contract rather than an inconvenience.
    if (auto context = el.canvas_context(); context && !context->origin_clean()) {
        throw_dom(r, "SecurityError",
                  "Failed to execute 'toBlob' on 'HTMLCanvasElement': the canvas has been tainted by "
                  "cross-origin data.");
    }

    std::string canonical;
    std::string bytes;
    if (png_mime_type(type, canonical)) {
        if (const std::shared_ptr<ImageData> bitmap = canvas_bitmap_for_serialization(el);
            bitmap && bitmap->is_valid()) {
            std::vector<uint8_t> png;
            if (ImageDecoder::encode_png(*bitmap, png)) {
                bytes.assign(reinterpret_cast<const char*>(png.data()), png.size());
            }
        }
    }
    const bool produced = !bytes.empty();
    // The callback runs in its own task, with the blob (or null when the
    // encoding failed): a script that did not expect a synchronous call and
    // a script that did both see the standard's shape.
    Realm* realm = &r;
    EventLoop::default_loop().post_task(
        [realm, callback, bytes = std::move(bytes), canonical = std::move(canonical), produced]() {
            if (!realm->alive) return;
            JsValue argument = produced ? make_blob(*realm, bytes, canonical) : null();
            js_call(callback, JsValue(), {argument});
        },
        {js_roots({callback})});
    return undefined();
}

/// §4.12.5.1's `createPattern(image, repetition)`, bound here because the
/// `CanvasPattern` it answers is a script object and only this file makes
/// those.
///
/// **What this phase answers, and what it does not.** The method exists, takes
/// the two arguments, and answers `null` in each of the two cases the standard
/// makes `null`: an unrecognised `repetition`, which is step 2 (the step's
/// wording is "throw" in older drafts and "return null" in HTML's current
/// text), and an image this engine cannot read. It does **not** yet paint the
/// tile when it is assigned to `fillStyle`: a pattern colour is sampled through
/// the paint the rasteriser keeps, and a `CanvasPattern` value has no slot
/// there — `fillStyle` accepts a `CanvasGradient` and a colour, and a pattern
/// needs a third kind. That is a separate change to `JsValue`'s canvas paint
/// and to `rasterize_paths`, and it is named in the finding rather than
/// half-done here. A page that gets a `CanvasPattern` from this method and
/// assigns it gets the same silent no-op it got from the method being
/// `undefined`, and now a pattern object to inspect — which is the smaller
/// lie, but it is still not the whole method.
void install_canvas_create_pattern(Realm& r, JsValue context_object,
                                   const std::shared_ptr<CanvasRenderingContext2D>& context) {
    auto get_ctx = [context]() -> CanvasRenderingContext2D* { return context.get(); };
    context_object.set_property("createPattern", make_function(r, "createPattern", 2,
        [get_ctx, &r](Realm&, Args args) {
            require_args(args, 2, "CanvasRenderingContext2D.createPattern");
            // A source this engine cannot read is `null`, which is what a
            // source that is not an image is: §4.12.5.1's "check the
            // usability of the image argument" failing takes this branch.
            DrawSource source;
            if (!draw_source_from(args[0], source) || !source.pixels) return null();
            const std::string repetition = args[1].to_string();
            auto pattern = get_ctx()->create_pattern(source.pixels, repetition);
            if (!pattern) return null();
            JsValue object = JsValue::make_object();
            // The object carries the pattern as its payload, which is how
            // `fillStyle` recognises it: `JsValue::set_property` looks for
            // `canvas_pattern_value()` beside `canvas_gradient_value()`.
            object.set_canvas_pattern_value(pattern);
            object.set_property("setTransform", make_function(r, "setTransform", 0,
                [pattern, &r](Realm&, Args set_args) {
                    // §4.12.5.1: no argument (or null) clears it; six numbers
                    // are the matrix in column order, which is the same order
                    // `setTransform` on the context takes.
                    if (set_args.empty() || set_args[0].is_nullish()) {
                        pattern->transform = TransformMatrix2D::identity();
                    } else if (set_args.size() >= 6) {
                        pattern->transform = TransformMatrix2D{
                            static_cast<float>(set_args[0].to_number()),
                            static_cast<float>(set_args[1].to_number()),
                            static_cast<float>(set_args[2].to_number()),
                            static_cast<float>(set_args[3].to_number()),
                            static_cast<float>(set_args[4].to_number()),
                            static_cast<float>(set_args[5].to_number())};
                    }
                    return undefined();
                }));
            return object;
        }));
}

/// Removes an **own data property** from `object`, so that a live read falls
/// through to `JsValue::get_property`'s canvas fast-path. `clear_slot` is not
/// this: it erases the hidden `%name` slot, and the property the context object
/// shadows the fast-path with is an ordinary one called `name`.
void drop_own(JsValue& object, const std::string& name) {
    if (JsObject* cell = object.property_cell()) cell->properties.erase(name);
}

/// §4.12.5.1's `reset()`, bound here rather than in `Canvas2D.cpp` because
/// four of the properties it resets were copied onto **this object** by
/// `CanvasRenderingContext2D::to_js_value` — `fillStyle`, `strokeStyle`,
/// `lineWidth` and `globalCompositeOperation` are own data properties, and an
/// own property shadows the live read in `JsValue::get_property`. The
/// renderer's `reset()` puts its fields back and this writes the same four
/// back to the object, so that the object and the renderer agree afterwards.
/// Without the second half the context keeps answering with the colour it had
/// before the reset, which is what a test that only checks `.lineWidth` catches
/// and one that only checks that `reset` exists does not.
void install_canvas_reset(Realm& r, JsValue context_object,
                          const std::shared_ptr<CanvasRenderingContext2D>& context) {
    auto get_ctx = [context]() -> CanvasRenderingContext2D* { return context.get(); };
    // The object is captured through a shared_ptr because a native function's
    // body must be callable when it is const, and writing these four
    // properties is a write to the object.
    auto self = std::make_shared<JsValue>(context_object);
    context_object.set_property("reset", make_function(r, "reset", 0,
        [get_ctx, self](Realm&, Args) {
            get_ctx()->reset();
            // The four `to_js_value` copies, from the state reset left.
            const CanvasRenderingContext2D* c = get_ctx();
            // `reset()` leaves a colour, never a paint, so both are colours here;
            // a slot left from before is cleared so the read cannot find one.
            drop_own(*self, "fillStyle");
            drop_own(*self, "strokeStyle");
            self->set_property("fillStyle", JsValue::make_string(c->fill_style.to_hex()));
            self->set_property("strokeStyle", JsValue::make_string(c->stroke_style.to_hex()));
            self->set_property("lineWidth", JsValue::make_number(c->line_width));
            self->set_property("globalCompositeOperation",
                              JsValue::make_string(c->global_composite_operation));
            return undefined();
        }));
}

/// `save()` and `restore()`, wrapped here for the same reason `reset()` is:
/// `to_js_value` copied `fillStyle` and `strokeStyle` onto **this object** as
/// own data properties, and an own property shadows the live read. The renderer
/// puts the saved styles back; without writing them to the object afterwards the
/// context keeps answering with the colour it had inside the `save()` — measured
/// with a gradient too, so this was wrong before patterns existed: a gradient set
/// before `save()`, a colour inside it, and `restore()` left `g.fillStyle` as
/// the colour while the renderer painted the gradient.
void install_canvas_save_restore(Realm& r, JsValue context_object,
                                 const std::shared_ptr<CanvasRenderingContext2D>& context) {
    auto get_ctx = [context]() -> CanvasRenderingContext2D* { return context.get(); };
    auto self = std::make_shared<JsValue>(context_object);
    // The two styles are the only state `to_js_value` copies that `save()` can
    // change; `lineWidth` and `globalCompositeOperation` are here for the day a
    // saved state carries them, and cost two writes.
    auto sync = [get_ctx, self]() {
        const CanvasRenderingContext2D* c = get_ctx();
        // A style is a colour, a gradient or a pattern, and only the first has a
        // string form here. Writing `to_hex()` over a restored gradient or
        // pattern would replace the paint with the colour it happens to have
        // beside it, which is a wrong drawing and not a wrong string: measured
        // as `save()`, `fillStyle = pattern`, `restore()` leaving the canvas
        // black. So a style that is not a colour has its own property **removed**
        // and the live read in `JsValue::get_property` answers it, which is what
        // answers a gradient set by `createLinearGradient` today.
        const bool fill_is_colour = !c->fill_gradient && !c->fill_pattern;
        const bool stroke_is_colour = !c->stroke_gradient && !c->stroke_pattern;
        if (fill_is_colour) self->set_property("fillStyle", JsValue::make_string(c->fill_style.to_hex()));
        else drop_own(*self, "fillStyle");
        if (stroke_is_colour) self->set_property("strokeStyle", JsValue::make_string(c->stroke_style.to_hex()));
        else drop_own(*self, "strokeStyle");
        self->set_property("lineWidth", JsValue::make_number(c->line_width));
        self->set_property("globalCompositeOperation",
                           JsValue::make_string(c->global_composite_operation));
    };
    context_object.set_property("save", make_function(r, "save", 0,
        [get_ctx, sync](Realm&, Args) { get_ctx()->save(); (void)sync; return undefined(); }));
    context_object.set_property("restore", make_function(r, "restore", 0,
        [get_ctx, sync](Realm&, Args) { get_ctx()->restore(); sync(); return undefined(); }));
}

// --- WebGL 1.0 / 2.0 WebIDL bindings (Khronos WebGL 1.0 §5 / 2.0 §3) --------

struct WebGLHandleState : JsExotic {
    std::weak_ptr<WebGLContext> ctx;
    uint32_t id = 0;
};

struct WebGLUniformLocationState : JsExotic {
    std::weak_ptr<WebGLContext> ctx;
    uint32_t program_id = 0;
    int location = -1;
};

struct WebGLActiveInfoState : JsExotic {
    WebGLActiveVar info;
};

struct WebGLPrecisionFormatState : JsExotic {
    int range_min = 127;
    int range_max = 127;
    int precision = 23;
};

struct WebGLContextState : JsExotic {
    std::shared_ptr<WebGLContext> ctx;
};

std::string normalize_canvas_context_type(const std::string& raw) {
    const std::string t = ascii_lower(raw);
    if (t == "experimental-webgl") return "webgl";
    if (t == "2d" || t == "webgl" || t == "webgl2") return t;
    return "";
}

JsValue make_webgl_handle(Realm& r, const char* iface, const std::shared_ptr<WebGLContext>& ctx, uint32_t id) {
    if (id == 0) return null();
    auto st = std::make_shared<WebGLHandleState>();
    st->ctx = ctx;
    st->id = id;
    return r.make(iface, st);
}

uint32_t webgl_handle_id(const JsValue& v, const std::shared_ptr<WebGLContext>& ctx) {
    if (v.is_nullish()) return 0;
    if (auto* st = state_of<WebGLHandleState>(v)) {
        if (st->ctx.lock() == ctx) return st->id;
    }
    return 0;
}

WebGLContext& this_webgl_context() {
    auto* st = state_of<WebGLContextState>(JsValue::current_this());
    if (!st || !st->ctx) {
        js_throw_type_error("Illegal invocation on WebGLRenderingContext");
    }
    return *st->ctx;
}

std::shared_ptr<WebGLContext> this_webgl_context_ptr() {
    auto* st = state_of<WebGLContextState>(JsValue::current_this());
    if (!st || !st->ctx) {
        js_throw_type_error("Illegal invocation on WebGLRenderingContext");
    }
    return st->ctx;
}

std::vector<float> webgl_read_float_array(const JsValue& v, std::size_t max_elems = 64) {
    std::vector<float> out;
    if (v.is_nullish()) return out;
    const std::size_t len = static_cast<std::size_t>(std::max(0.0, to_double(v.get_property("length"))));
    const std::size_t n = std::min(len, max_elems);
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(static_cast<float>(to_double(v.get_property(std::to_string(i)))));
    }
    return out;
}

std::vector<int> webgl_read_int_array(const JsValue& v, std::size_t max_elems = 64) {
    std::vector<int> out;
    if (v.is_nullish()) return out;
    const std::size_t len = static_cast<std::size_t>(std::max(0.0, to_double(v.get_property("length"))));
    const std::size_t n = std::min(len, max_elems);
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(static_cast<int>(to_double(v.get_property(std::to_string(i)))));
    }
    return out;
}

void setup_webgl_active_info(Iface& I) {
    I.getter("name", [](Realm&) { return str(this_state<WebGLActiveInfoState>("WebGLActiveInfo").info.name); });
    I.getter("size", [](Realm&) { return num(static_cast<double>(this_state<WebGLActiveInfoState>("WebGLActiveInfo").info.size)); });
    I.getter("type", [](Realm&) { return num(static_cast<double>(this_state<WebGLActiveInfoState>("WebGLActiveInfo").info.type)); });
}

void setup_webgl_precision_format(Iface& I) {
    I.getter("rangeMin", [](Realm&) { return num(static_cast<double>(this_state<WebGLPrecisionFormatState>("WebGLShaderPrecisionFormat").range_min)); });
    I.getter("rangeMax", [](Realm&) { return num(static_cast<double>(this_state<WebGLPrecisionFormatState>("WebGLShaderPrecisionFormat").range_max)); });
    I.getter("precision", [](Realm&) { return num(static_cast<double>(this_state<WebGLPrecisionFormatState>("WebGLShaderPrecisionFormat").precision)); });
}

void install_webgl_constants(Iface& I, bool webgl2) {
    using namespace litebird::webgl;
    I.constant("DEPTH_BUFFER_BIT", DEPTH_BUFFER_BIT);
    I.constant("STENCIL_BUFFER_BIT", STENCIL_BUFFER_BIT);
    I.constant("COLOR_BUFFER_BIT", COLOR_BUFFER_BIT);
    I.constant("POINTS", POINTS);
    I.constant("LINES", LINES);
    I.constant("LINE_LOOP", LINE_LOOP);
    I.constant("LINE_STRIP", LINE_STRIP);
    I.constant("TRIANGLES", TRIANGLES);
    I.constant("TRIANGLE_STRIP", TRIANGLE_STRIP);
    I.constant("TRIANGLE_FAN", TRIANGLE_FAN);
    I.constant("ZERO", ZERO);
    I.constant("ONE", ONE);
    I.constant("SRC_COLOR", SRC_COLOR);
    I.constant("ONE_MINUS_SRC_COLOR", ONE_MINUS_SRC_COLOR);
    I.constant("SRC_ALPHA", SRC_ALPHA);
    I.constant("ONE_MINUS_SRC_ALPHA", ONE_MINUS_SRC_ALPHA);
    I.constant("DST_ALPHA", DST_ALPHA);
    I.constant("ONE_MINUS_DST_ALPHA", ONE_MINUS_DST_ALPHA);
    I.constant("DST_COLOR", DST_COLOR);
    I.constant("ONE_MINUS_DST_COLOR", ONE_MINUS_DST_COLOR);
    I.constant("SRC_ALPHA_SATURATE", SRC_ALPHA_SATURATE);
    I.constant("CONSTANT_COLOR", CONSTANT_COLOR);
    I.constant("ONE_MINUS_CONSTANT_COLOR", ONE_MINUS_CONSTANT_COLOR);
    I.constant("CONSTANT_ALPHA", CONSTANT_ALPHA);
    I.constant("ONE_MINUS_CONSTANT_ALPHA", ONE_MINUS_CONSTANT_ALPHA);
    I.constant("FUNC_ADD", FUNC_ADD);
    I.constant("FUNC_SUBTRACT", FUNC_SUBTRACT);
    I.constant("FUNC_REVERSE_SUBTRACT", FUNC_REVERSE_SUBTRACT);
    I.constant("ARRAY_BUFFER", ARRAY_BUFFER);
    I.constant("ELEMENT_ARRAY_BUFFER", ELEMENT_ARRAY_BUFFER);
    I.constant("ARRAY_BUFFER_BINDING", ARRAY_BUFFER_BINDING);
    I.constant("ELEMENT_ARRAY_BUFFER_BINDING", ELEMENT_ARRAY_BUFFER_BINDING);
    I.constant("STREAM_DRAW", STREAM_DRAW);
    I.constant("STATIC_DRAW", STATIC_DRAW);
    I.constant("DYNAMIC_DRAW", DYNAMIC_DRAW);
    I.constant("BUFFER_SIZE", BUFFER_SIZE);
    I.constant("BUFFER_USAGE", BUFFER_USAGE);
    I.constant("CULL_FACE", CULL_FACE);
    I.constant("BLEND", BLEND);
    I.constant("DITHER", DITHER);
    I.constant("STENCIL_TEST", STENCIL_TEST);
    I.constant("DEPTH_TEST", DEPTH_TEST);
    I.constant("SCISSOR_TEST", SCISSOR_TEST);
    I.constant("POLYGON_OFFSET_FILL", POLYGON_OFFSET_FILL);
    I.constant("FRONT", FRONT);
    I.constant("BACK", BACK);
    I.constant("FRONT_AND_BACK", FRONT_AND_BACK);
    I.constant("CW", CW);
    I.constant("CCW", CCW);
    I.constant("NO_ERROR", NO_ERROR);
    I.constant("INVALID_ENUM", INVALID_ENUM);
    I.constant("INVALID_VALUE", INVALID_VALUE);
    I.constant("INVALID_OPERATION", INVALID_OPERATION);
    I.constant("OUT_OF_MEMORY", OUT_OF_MEMORY);
    I.constant("INVALID_FRAMEBUFFER_OPERATION", INVALID_FRAMEBUFFER_OPERATION);
    I.constant("NEVER", NEVER);
    I.constant("LESS", LESS);
    I.constant("EQUAL", EQUAL);
    I.constant("LEQUAL", LEQUAL);
    I.constant("GREATER", GREATER);
    I.constant("NOTEQUAL", NOTEQUAL);
    I.constant("GEQUAL", GEQUAL);
    I.constant("ALWAYS", ALWAYS);
    I.constant("BYTE", BYTE);
    I.constant("UNSIGNED_BYTE", UNSIGNED_BYTE);
    I.constant("SHORT", SHORT);
    I.constant("UNSIGNED_SHORT", UNSIGNED_SHORT);
    I.constant("INT", INT);
    I.constant("UNSIGNED_INT", UNSIGNED_INT);
    I.constant("FLOAT", FLOAT);
    I.constant("DEPTH_COMPONENT", DEPTH_COMPONENT);
    I.constant("ALPHA", ALPHA);
    I.constant("RGB", RGB);
    I.constant("RGBA", RGBA);
    I.constant("LUMINANCE", LUMINANCE);
    I.constant("LUMINANCE_ALPHA", LUMINANCE_ALPHA);
    I.constant("RGBA4", RGBA4);
    I.constant("RGB5_A1", RGB5_A1);
    I.constant("RGB565", RGB565);
    I.constant("DEPTH_COMPONENT16", DEPTH_COMPONENT16);
    I.constant("STENCIL_INDEX8", STENCIL_INDEX8);
    I.constant("DEPTH_STENCIL", DEPTH_STENCIL);
    I.constant("FRAGMENT_SHADER", FRAGMENT_SHADER);
    I.constant("VERTEX_SHADER", VERTEX_SHADER);
    I.constant("SHADER_TYPE", SHADER_TYPE);
    I.constant("DELETE_STATUS", DELETE_STATUS);
    I.constant("COMPILE_STATUS", COMPILE_STATUS);
    I.constant("LINK_STATUS", LINK_STATUS);
    I.constant("VALIDATE_STATUS", VALIDATE_STATUS);
    I.constant("ATTACHED_SHADERS", ATTACHED_SHADERS);
    I.constant("ACTIVE_UNIFORMS", ACTIVE_UNIFORMS);
    I.constant("ACTIVE_ATTRIBUTES", ACTIVE_ATTRIBUTES);
    I.constant("CURRENT_PROGRAM", CURRENT_PROGRAM);
    I.constant("FLOAT_VEC2", FLOAT_VEC2);
    I.constant("FLOAT_VEC3", FLOAT_VEC3);
    I.constant("FLOAT_VEC4", FLOAT_VEC4);
    I.constant("INT_VEC2", INT_VEC2);
    I.constant("INT_VEC3", INT_VEC3);
    I.constant("INT_VEC4", INT_VEC4);
    I.constant("BOOL", BOOL);
    I.constant("BOOL_VEC2", BOOL_VEC2);
    I.constant("BOOL_VEC3", BOOL_VEC3);
    I.constant("BOOL_VEC4", BOOL_VEC4);
    I.constant("FLOAT_MAT2", FLOAT_MAT2);
    I.constant("FLOAT_MAT3", FLOAT_MAT3);
    I.constant("FLOAT_MAT4", FLOAT_MAT4);
    I.constant("SAMPLER_2D", SAMPLER_2D);
    I.constant("TEXTURE_2D", TEXTURE_2D);
    I.constant("TEXTURE_CUBE_MAP", TEXTURE_CUBE_MAP);
    I.constant("TEXTURE_BINDING_2D", TEXTURE_BINDING_2D);
    I.constant("ACTIVE_TEXTURE", ACTIVE_TEXTURE);
    for (int u = 0; u < 16; ++u) {
        static const char* kTexNames[16] = {
            "TEXTURE0", "TEXTURE1", "TEXTURE2", "TEXTURE3",
            "TEXTURE4", "TEXTURE5", "TEXTURE6", "TEXTURE7",
            "TEXTURE8", "TEXTURE9", "TEXTURE10", "TEXTURE11",
            "TEXTURE12", "TEXTURE13", "TEXTURE14", "TEXTURE15"
        };
        I.constant(kTexNames[u], TEXTURE0 + static_cast<uint32_t>(u));
    }
    I.constant("NEAREST", NEAREST);
    I.constant("LINEAR", LINEAR);
    I.constant("NEAREST_MIPMAP_NEAREST", NEAREST_MIPMAP_NEAREST);
    I.constant("LINEAR_MIPMAP_NEAREST", LINEAR_MIPMAP_NEAREST);
    I.constant("NEAREST_MIPMAP_LINEAR", NEAREST_MIPMAP_LINEAR);
    I.constant("LINEAR_MIPMAP_LINEAR", LINEAR_MIPMAP_LINEAR);
    I.constant("TEXTURE_MAG_FILTER", TEXTURE_MAG_FILTER);
    I.constant("TEXTURE_MIN_FILTER", TEXTURE_MIN_FILTER);
    I.constant("TEXTURE_WRAP_S", TEXTURE_WRAP_S);
    I.constant("TEXTURE_WRAP_T", TEXTURE_WRAP_T);
    I.constant("REPEAT", REPEAT);
    I.constant("CLAMP_TO_EDGE", CLAMP_TO_EDGE);
    I.constant("MIRRORED_REPEAT", MIRRORED_REPEAT);
    I.constant("UNPACK_ALIGNMENT", UNPACK_ALIGNMENT);
    I.constant("PACK_ALIGNMENT", PACK_ALIGNMENT);
    I.constant("UNPACK_FLIP_Y_WEBGL", UNPACK_FLIP_Y_WEBGL);
    I.constant("UNPACK_PREMULTIPLY_ALPHA_WEBGL", UNPACK_PREMULTIPLY_ALPHA_WEBGL);
    I.constant("FRAMEBUFFER", FRAMEBUFFER);
    I.constant("RENDERBUFFER", RENDERBUFFER);
    I.constant("FRAMEBUFFER_BINDING", FRAMEBUFFER_BINDING);
    I.constant("RENDERBUFFER_BINDING", RENDERBUFFER_BINDING);
    I.constant("COLOR_ATTACHMENT0", COLOR_ATTACHMENT0);
    I.constant("DEPTH_ATTACHMENT", DEPTH_ATTACHMENT);
    I.constant("STENCIL_ATTACHMENT", STENCIL_ATTACHMENT);
    I.constant("FRAMEBUFFER_COMPLETE", FRAMEBUFFER_COMPLETE);
    I.constant("FRAMEBUFFER_INCOMPLETE_ATTACHMENT", FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
    I.constant("FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT", FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT);
    I.constant("FRAMEBUFFER_INCOMPLETE_DIMENSIONS", FRAMEBUFFER_INCOMPLETE_DIMENSIONS);
    I.constant("LINE_WIDTH", LINE_WIDTH);
    I.constant("CULL_FACE_MODE", CULL_FACE_MODE);
    I.constant("FRONT_FACE", FRONT_FACE);
    I.constant("DEPTH_RANGE", DEPTH_RANGE);
    I.constant("DEPTH_WRITEMASK", DEPTH_WRITEMASK);
    I.constant("DEPTH_CLEAR_VALUE", DEPTH_CLEAR_VALUE);
    I.constant("DEPTH_FUNC", DEPTH_FUNC);
    I.constant("VIEWPORT", VIEWPORT);
    I.constant("SCISSOR_BOX", SCISSOR_BOX);
    I.constant("COLOR_CLEAR_VALUE", COLOR_CLEAR_VALUE);
    I.constant("COLOR_WRITEMASK", COLOR_WRITEMASK);
    I.constant("BLEND_EQUATION_RGB", BLEND_EQUATION_RGB);
    I.constant("BLEND_EQUATION_ALPHA", BLEND_EQUATION_ALPHA);
    I.constant("BLEND_DST_RGB", BLEND_DST_RGB);
    I.constant("BLEND_SRC_RGB", BLEND_SRC_RGB);
    I.constant("BLEND_DST_ALPHA", BLEND_DST_ALPHA);
    I.constant("BLEND_SRC_ALPHA", BLEND_SRC_ALPHA);
    I.constant("BLEND_COLOR", BLEND_COLOR);
    I.constant("MAX_TEXTURE_SIZE", MAX_TEXTURE_SIZE);
    I.constant("MAX_VIEWPORT_DIMS", MAX_VIEWPORT_DIMS);
    I.constant("MAX_VERTEX_ATTRIBS", MAX_VERTEX_ATTRIBS);
    I.constant("MAX_VERTEX_UNIFORM_VECTORS", MAX_VERTEX_UNIFORM_VECTORS);
    I.constant("MAX_VARYING_VECTORS", MAX_VARYING_VECTORS);
    I.constant("MAX_COMBINED_TEXTURE_IMAGE_UNITS", MAX_COMBINED_TEXTURE_IMAGE_UNITS);
    I.constant("MAX_VERTEX_TEXTURE_IMAGE_UNITS", MAX_VERTEX_TEXTURE_IMAGE_UNITS);
    I.constant("MAX_TEXTURE_IMAGE_UNITS", MAX_TEXTURE_IMAGE_UNITS);
    I.constant("MAX_FRAGMENT_UNIFORM_VECTORS", MAX_FRAGMENT_UNIFORM_VECTORS);
    I.constant("MAX_RENDERBUFFER_SIZE", MAX_RENDERBUFFER_SIZE);
    I.constant("VENDOR", VENDOR);
    I.constant("RENDERER", RENDERER);
    I.constant("VERSION", VERSION);
    I.constant("SHADING_LANGUAGE_VERSION", SHADING_LANGUAGE_VERSION);
    I.constant("LOW_FLOAT", LOW_FLOAT);
    I.constant("MEDIUM_FLOAT", MEDIUM_FLOAT);
    I.constant("HIGH_FLOAT", HIGH_FLOAT);
    I.constant("LOW_INT", LOW_INT);
    I.constant("MEDIUM_INT", MEDIUM_INT);
    I.constant("HIGH_INT", HIGH_INT);
    if (webgl2) {
        I.constant("MIN", MIN);
        I.constant("MAX", MAX);
        I.constant("RGB8", RGB8);
        I.constant("RGBA8", RGBA8);
        I.constant("DEPTH_COMPONENT24", DEPTH_COMPONENT24);
        I.constant("VERTEX_ARRAY_BINDING", VERTEX_ARRAY_BINDING);
    }
}

void install_webgl_common_members(Iface& I, bool webgl2) {
    install_webgl_constants(I, webgl2);

    I.getter("canvas", [](Realm&) {
        if (const JsValue* c = slot(JsValue::current_this(), "canvas")) return *c;
        return null();
    });
    I.getter("drawingBufferWidth", [](Realm&) {
        return num(static_cast<double>(this_webgl_context().drawing_buffer_width()));
    });
    I.getter("drawingBufferHeight", [](Realm&) {
        return num(static_cast<double>(this_webgl_context().drawing_buffer_height()));
    });

    I.method("getError", 0, [](Realm&, Args) {
        return num(static_cast<double>(this_webgl_context().get_error()));
    });
    I.method("isContextLost", 0, [](Realm&, Args) { return boolean(false); });
    I.method("flush", 0, [](Realm&, Args) { return undefined(); });
    I.method("finish", 0, [](Realm&, Args) { return undefined(); });
    I.method("getContextAttributes", 0, [](Realm&, Args) {
        JsValue o = JsValue::make_object();
        o.set_property("alpha", boolean(true));
        o.set_property("antialias", boolean(false));
        o.set_property("depth", boolean(true));
        o.set_property("failIfMajorPerformanceCaveat", boolean(false));
        o.set_property("powerPreference", str("default"));
        o.set_property("premultipliedAlpha", boolean(true));
        o.set_property("preserveDrawingBuffer", boolean(true));
        o.set_property("stencil", boolean(false));
        return o;
    });
    I.method("getSupportedExtensions", 0, [](Realm&, Args) { return JsValue::make_array(); });
    I.method("getExtension", 1, [](Realm&, Args) { return null(); });

    I.method("clearColor", 4, [](Realm&, Args a) {
        this_webgl_context().clear_color(
            static_cast<float>(to_double(js_arg(a, 0))), static_cast<float>(to_double(js_arg(a, 1))),
            static_cast<float>(to_double(js_arg(a, 2))), static_cast<float>(to_double(js_arg(a, 3))));
        return undefined();
    });
    I.method("clearDepth", 1, [](Realm&, Args a) {
        this_webgl_context().clear_depth(static_cast<float>(to_double(js_arg(a, 0))));
        return undefined();
    });
    I.method("clearStencil", 1, [](Realm&, Args a) {
        this_webgl_context().clear_stencil(static_cast<int>(to_double(js_arg(a, 0))));
        return undefined();
    });
    I.method("clear", 1, [](Realm&, Args a) {
        this_webgl_context().clear(static_cast<uint32_t>(to_double(js_arg(a, 0))));
        return undefined();
    });
    I.method("viewport", 4, [](Realm&, Args a) {
        this_webgl_context().viewport(
            static_cast<int>(to_double(js_arg(a, 0))), static_cast<int>(to_double(js_arg(a, 1))),
            static_cast<int>(to_double(js_arg(a, 2))), static_cast<int>(to_double(js_arg(a, 3))));
        return undefined();
    });
    I.method("scissor", 4, [](Realm&, Args a) {
        this_webgl_context().scissor(
            static_cast<int>(to_double(js_arg(a, 0))), static_cast<int>(to_double(js_arg(a, 1))),
            static_cast<int>(to_double(js_arg(a, 2))), static_cast<int>(to_double(js_arg(a, 3))));
        return undefined();
    });
    I.method("enable", 1, [](Realm&, Args a) {
        this_webgl_context().enable(static_cast<uint32_t>(to_double(js_arg(a, 0))));
        return undefined();
    });
    I.method("disable", 1, [](Realm&, Args a) {
        this_webgl_context().disable(static_cast<uint32_t>(to_double(js_arg(a, 0))));
        return undefined();
    });
    I.method("isEnabled", 1, [](Realm&, Args a) {
        return boolean(this_webgl_context().is_enabled(static_cast<uint32_t>(to_double(js_arg(a, 0)))));
    });
    I.method("colorMask", 4, [](Realm&, Args a) {
        this_webgl_context().color_mask(to_bool(js_arg(a, 0)), to_bool(js_arg(a, 1)),
                                        to_bool(js_arg(a, 2)), to_bool(js_arg(a, 3)));
        return undefined();
    });
    I.method("depthMask", 1, [](Realm&, Args a) {
        this_webgl_context().depth_mask(to_bool(js_arg(a, 0)));
        return undefined();
    });
    I.method("depthFunc", 1, [](Realm&, Args a) {
        this_webgl_context().depth_func(static_cast<uint32_t>(to_double(js_arg(a, 0))));
        return undefined();
    });
    I.method("depthRange", 2, [](Realm&, Args a) {
        this_webgl_context().depth_range(static_cast<float>(to_double(js_arg(a, 0))),
                                         static_cast<float>(to_double(js_arg(a, 1))));
        return undefined();
    });
    I.method("cullFace", 1, [](Realm&, Args a) {
        this_webgl_context().cull_face(static_cast<uint32_t>(to_double(js_arg(a, 0))));
        return undefined();
    });
    I.method("frontFace", 1, [](Realm&, Args a) {
        this_webgl_context().front_face(static_cast<uint32_t>(to_double(js_arg(a, 0))));
        return undefined();
    });
    I.method("lineWidth", 1, [](Realm&, Args a) {
        this_webgl_context().line_width(static_cast<float>(to_double(js_arg(a, 0))));
        return undefined();
    });
    I.method("blendFunc", 2, [](Realm&, Args a) {
        this_webgl_context().blend_func(static_cast<uint32_t>(to_double(js_arg(a, 0))),
                                        static_cast<uint32_t>(to_double(js_arg(a, 1))));
        return undefined();
    });
    I.method("blendFuncSeparate", 4, [](Realm&, Args a) {
        this_webgl_context().blend_func_separate(
            static_cast<uint32_t>(to_double(js_arg(a, 0))), static_cast<uint32_t>(to_double(js_arg(a, 1))),
            static_cast<uint32_t>(to_double(js_arg(a, 2))), static_cast<uint32_t>(to_double(js_arg(a, 3))));
        return undefined();
    });
    I.method("blendEquation", 1, [](Realm&, Args a) {
        this_webgl_context().blend_equation(static_cast<uint32_t>(to_double(js_arg(a, 0))));
        return undefined();
    });
    I.method("blendEquationSeparate", 2, [](Realm&, Args a) {
        this_webgl_context().blend_equation_separate(static_cast<uint32_t>(to_double(js_arg(a, 0))),
                                                     static_cast<uint32_t>(to_double(js_arg(a, 1))));
        return undefined();
    });
    I.method("blendColor", 4, [](Realm&, Args a) {
        this_webgl_context().blend_color(
            static_cast<float>(to_double(js_arg(a, 0))), static_cast<float>(to_double(js_arg(a, 1))),
            static_cast<float>(to_double(js_arg(a, 2))), static_cast<float>(to_double(js_arg(a, 3))));
        return undefined();
    });
    I.method("pixelStorei", 2, [](Realm&, Args a) {
        this_webgl_context().pixel_storei(static_cast<uint32_t>(to_double(js_arg(a, 0))),
                                          static_cast<int>(to_double(js_arg(a, 1))));
        return undefined();
    });

    // Shaders & Programs
    I.method("createShader", 1, [](Realm& r, Args a) {
        auto ctx = this_webgl_context_ptr();
        return make_webgl_handle(r, "WebGLShader", ctx, ctx->create_shader(static_cast<uint32_t>(to_double(js_arg(a, 0)))));
    });
    I.method("deleteShader", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->delete_shader(webgl_handle_id(js_arg(a, 0), ctx));
        return undefined();
    });
    I.method("isShader", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        return boolean(ctx->is_shader(webgl_handle_id(js_arg(a, 0), ctx)));
    });
    I.method("shaderSource", 2, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->shader_source(webgl_handle_id(js_arg(a, 0), ctx), to_str(js_arg(a, 1)));
        return undefined();
    });
    I.method("getShaderSource", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        return str(ctx->get_shader_source(webgl_handle_id(js_arg(a, 0), ctx)));
    });
    I.method("compileShader", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->compile_shader(webgl_handle_id(js_arg(a, 0), ctx));
        return undefined();
    });
    I.method("getShaderParameter", 2, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        int out = 0; bool is_bool = false;
        if (!ctx->get_shader_parameter(webgl_handle_id(js_arg(a, 0), ctx),
                                       static_cast<uint32_t>(to_double(js_arg(a, 1))), out, is_bool)) {
            return null();
        }
        return is_bool ? boolean(out != 0) : num(static_cast<double>(out));
    });
    I.method("getShaderInfoLog", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        return str(ctx->get_shader_info_log(webgl_handle_id(js_arg(a, 0), ctx)));
    });
    I.method("getShaderPrecisionFormat", 2, [](Realm& r, Args a) {
        auto st = std::make_shared<WebGLPrecisionFormatState>();
        const uint32_t prec = static_cast<uint32_t>(to_double(js_arg(a, 1)));
        if (prec == webgl::LOW_INT || prec == webgl::MEDIUM_INT || prec == webgl::HIGH_INT) {
            st->range_min = 31; st->range_max = 30; st->precision = 0;
        }
        return r.make("WebGLShaderPrecisionFormat", st);
    });

    I.method("createProgram", 0, [](Realm& r, Args) {
        auto ctx = this_webgl_context_ptr();
        return make_webgl_handle(r, "WebGLProgram", ctx, ctx->create_program());
    });
    I.method("deleteProgram", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->delete_program(webgl_handle_id(js_arg(a, 0), ctx));
        return undefined();
    });
    I.method("isProgram", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        return boolean(ctx->is_program(webgl_handle_id(js_arg(a, 0), ctx)));
    });
    I.method("attachShader", 2, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->attach_shader(webgl_handle_id(js_arg(a, 0), ctx), webgl_handle_id(js_arg(a, 1), ctx));
        return undefined();
    });
    I.method("detachShader", 2, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->detach_shader(webgl_handle_id(js_arg(a, 0), ctx), webgl_handle_id(js_arg(a, 1), ctx));
        return undefined();
    });
    I.method("bindAttribLocation", 3, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->bind_attrib_location(webgl_handle_id(js_arg(a, 0), ctx),
                                  static_cast<uint32_t>(to_double(js_arg(a, 1))), to_str(js_arg(a, 2)));
        return undefined();
    });
    I.method("linkProgram", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->link_program(webgl_handle_id(js_arg(a, 0), ctx));
        return undefined();
    });
    I.method("validateProgram", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->validate_program(webgl_handle_id(js_arg(a, 0), ctx));
        return undefined();
    });
    I.method("useProgram", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->use_program(webgl_handle_id(js_arg(a, 0), ctx));
        return undefined();
    });
    I.method("getProgramParameter", 2, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        int out = 0; bool is_bool = false;
        if (!ctx->get_program_parameter(webgl_handle_id(js_arg(a, 0), ctx),
                                        static_cast<uint32_t>(to_double(js_arg(a, 1))), out, is_bool)) {
            return null();
        }
        return is_bool ? boolean(out != 0) : num(static_cast<double>(out));
    });
    I.method("getProgramInfoLog", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        return str(ctx->get_program_info_log(webgl_handle_id(js_arg(a, 0), ctx)));
    });
    I.method("getAttribLocation", 2, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        return num(static_cast<double>(ctx->get_attrib_location(webgl_handle_id(js_arg(a, 0), ctx), to_str(js_arg(a, 1)))));
    });
    I.method("getUniformLocation", 2, [](Realm& r, Args a) {
        auto ctx = this_webgl_context_ptr();
        const uint32_t pid = webgl_handle_id(js_arg(a, 0), ctx);
        const int loc = ctx->get_uniform_location(pid, to_str(js_arg(a, 1)));
        if (loc < 0) return null();
        auto st = std::make_shared<WebGLUniformLocationState>();
        st->ctx = ctx;
        st->program_id = pid;
        st->location = loc;
        return r.make("WebGLUniformLocation", st);
    });
    I.method("getActiveAttrib", 2, [](Realm& r, Args a) {
        auto ctx = this_webgl_context_ptr();
        WebGLActiveVar info;
        if (!ctx->get_active_attrib(webgl_handle_id(js_arg(a, 0), ctx),
                                    static_cast<uint32_t>(to_double(js_arg(a, 1))), info)) return null();
        auto st = std::make_shared<WebGLActiveInfoState>();
        st->info = std::move(info);
        return r.make("WebGLActiveInfo", st);
    });
    I.method("getActiveUniform", 2, [](Realm& r, Args a) {
        auto ctx = this_webgl_context_ptr();
        WebGLActiveVar info;
        if (!ctx->get_active_uniform(webgl_handle_id(js_arg(a, 0), ctx),
                                     static_cast<uint32_t>(to_double(js_arg(a, 1))), info)) return null();
        auto st = std::make_shared<WebGLActiveInfoState>();
        st->info = std::move(info);
        return r.make("WebGLActiveInfo", st);
    });

    auto uniform_loc_of = [](const JsValue& v, const std::shared_ptr<WebGLContext>& ctx) -> int {
        if (v.is_nullish()) return -1;
        if (auto* st = state_of<WebGLUniformLocationState>(v)) {
            if (st->ctx.lock() == ctx && st->program_id == ctx->current_program_id()) return st->location;
            ctx->set_error(webgl::INVALID_OPERATION);
        }
        return -1;
    };

    I.method("uniform1f", 2, [uniform_loc_of](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        float v = static_cast<float>(to_double(js_arg(a, 1)));
        ctx->uniform_floats(uniform_loc_of(js_arg(a, 0), ctx), webgl::FLOAT, &v, 1, 1);
        return undefined();
    });
    I.method("uniform2f", 3, [uniform_loc_of](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        float v[2] = {static_cast<float>(to_double(js_arg(a, 1))), static_cast<float>(to_double(js_arg(a, 2)))};
        ctx->uniform_floats(uniform_loc_of(js_arg(a, 0), ctx), webgl::FLOAT_VEC2, v, 1, 2);
        return undefined();
    });
    I.method("uniform3f", 4, [uniform_loc_of](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        float v[3] = {static_cast<float>(to_double(js_arg(a, 1))), static_cast<float>(to_double(js_arg(a, 2))),
                      static_cast<float>(to_double(js_arg(a, 3)))};
        ctx->uniform_floats(uniform_loc_of(js_arg(a, 0), ctx), webgl::FLOAT_VEC3, v, 1, 3);
        return undefined();
    });
    I.method("uniform4f", 5, [uniform_loc_of](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        float v[4] = {static_cast<float>(to_double(js_arg(a, 1))), static_cast<float>(to_double(js_arg(a, 2))),
                      static_cast<float>(to_double(js_arg(a, 3))), static_cast<float>(to_double(js_arg(a, 4)))};
        ctx->uniform_floats(uniform_loc_of(js_arg(a, 0), ctx), webgl::FLOAT_VEC4, v, 1, 4);
        return undefined();
    });
    I.method("uniform1i", 2, [uniform_loc_of](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        int v = static_cast<int>(to_double(js_arg(a, 1)));
        ctx->uniform_ints(uniform_loc_of(js_arg(a, 0), ctx), webgl::INT, &v, 1, 1);
        return undefined();
    });
    I.method("uniform2i", 3, [uniform_loc_of](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        int v[2] = {static_cast<int>(to_double(js_arg(a, 1))), static_cast<int>(to_double(js_arg(a, 2)))};
        ctx->uniform_ints(uniform_loc_of(js_arg(a, 0), ctx), webgl::INT_VEC2, v, 1, 2);
        return undefined();
    });
    I.method("uniform3i", 4, [uniform_loc_of](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        int v[3] = {static_cast<int>(to_double(js_arg(a, 1))), static_cast<int>(to_double(js_arg(a, 2))),
                    static_cast<int>(to_double(js_arg(a, 3)))};
        ctx->uniform_ints(uniform_loc_of(js_arg(a, 0), ctx), webgl::INT_VEC3, v, 1, 3);
        return undefined();
    });
    I.method("uniform4i", 5, [uniform_loc_of](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        int v[4] = {static_cast<int>(to_double(js_arg(a, 1))), static_cast<int>(to_double(js_arg(a, 2))),
                    static_cast<int>(to_double(js_arg(a, 3))), static_cast<int>(to_double(js_arg(a, 4)))};
        ctx->uniform_ints(uniform_loc_of(js_arg(a, 0), ctx), webgl::INT_VEC4, v, 1, 4);
        return undefined();
    });
    for (int comp = 1; comp <= 4; ++comp) {
        static const char* kFNames[4] = {"uniform1fv", "uniform2fv", "uniform3fv", "uniform4fv"};
        static const char* kINames[4] = {"uniform1iv", "uniform2iv", "uniform3iv", "uniform4iv"};
        I.method(kFNames[comp - 1], 2, [uniform_loc_of, comp](Realm&, Args a) {
            auto ctx = this_webgl_context_ptr();
            auto vals = webgl_read_float_array(js_arg(a, 1));
            if (static_cast<int>(vals.size()) >= comp) {
                ctx->uniform_floats(uniform_loc_of(js_arg(a, 0), ctx), webgl::FLOAT,
                                    vals.data(), static_cast<int>(vals.size()) / comp, comp);
            }
            return undefined();
        });
        I.method(kINames[comp - 1], 2, [uniform_loc_of, comp](Realm&, Args a) {
            auto ctx = this_webgl_context_ptr();
            auto vals = webgl_read_int_array(js_arg(a, 1));
            if (static_cast<int>(vals.size()) >= comp) {
                ctx->uniform_ints(uniform_loc_of(js_arg(a, 0), ctx), webgl::INT,
                                  vals.data(), static_cast<int>(vals.size()) / comp, comp);
            }
            return undefined();
        });
    }
    for (int dim = 2; dim <= 4; ++dim) {
        static const char* kMNames[3] = {"uniformMatrix2fv", "uniformMatrix3fv", "uniformMatrix4fv"};
        I.method(kMNames[dim - 2], 3, [uniform_loc_of, dim](Realm&, Args a) {
            auto ctx = this_webgl_context_ptr();
            auto vals = webgl_read_float_array(js_arg(a, 2));
            if (static_cast<int>(vals.size()) >= dim * dim) {
                ctx->uniform_matrix(uniform_loc_of(js_arg(a, 0), ctx), dim,
                                    to_bool(js_arg(a, 1)), vals.data(), 1);
            }
            return undefined();
        });
    }

    // Buffers & Vertex Attributes
    I.method("createBuffer", 0, [](Realm& r, Args) {
        auto ctx = this_webgl_context_ptr();
        return make_webgl_handle(r, "WebGLBuffer", ctx, ctx->create_buffer());
    });
    I.method("deleteBuffer", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->delete_buffer(webgl_handle_id(js_arg(a, 0), ctx));
        return undefined();
    });
    I.method("isBuffer", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        return boolean(ctx->is_buffer(webgl_handle_id(js_arg(a, 0), ctx)));
    });
    I.method("bindBuffer", 2, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->bind_buffer(static_cast<uint32_t>(to_double(js_arg(a, 0))), webgl_handle_id(js_arg(a, 1), ctx));
        return undefined();
    });
    I.method("bufferData", 3, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        const uint32_t target = static_cast<uint32_t>(to_double(js_arg(a, 0)));
        const JsValue src = js_arg(a, 1);
        const uint32_t usage = static_cast<uint32_t>(to_double(js_arg(a, 2)));
        if (src.type == JsType::Number) {
            const double sz = to_double(src);
            if (sz < 0.0) { ctx->set_error(webgl::INVALID_VALUE); return undefined(); }
            ctx->buffer_data_size(target, static_cast<std::size_t>(sz), usage);
        } else if (is_buffer_source(src)) {
            std::string bytes;
            if (typed_array_bytes(src, bytes)) {
                ctx->buffer_data_bytes(target, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), usage);
            }
        } else if (src.is_nullish()) {
            ctx->buffer_data_size(target, 0, usage);
        }
        return undefined();
    });
    I.method("bufferSubData", 3, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        const uint32_t target = static_cast<uint32_t>(to_double(js_arg(a, 0)));
        const double off = to_double(js_arg(a, 1));
        if (off < 0.0) { ctx->set_error(webgl::INVALID_VALUE); return undefined(); }
        std::string bytes;
        if (typed_array_bytes(js_arg(a, 2), bytes)) {
            ctx->buffer_sub_data(target, static_cast<std::size_t>(off),
                                 reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
        }
        return undefined();
    });
    I.method("getBufferParameter", 2, [](Realm&, Args a) {
        return num(static_cast<double>(this_webgl_context().get_buffer_parameter(
            static_cast<uint32_t>(to_double(js_arg(a, 0))), static_cast<uint32_t>(to_double(js_arg(a, 1))))));
    });

    I.method("enableVertexAttribArray", 1, [](Realm&, Args a) {
        this_webgl_context().enable_vertex_attrib_array(static_cast<uint32_t>(to_double(js_arg(a, 0))));
        return undefined();
    });
    I.method("disableVertexAttribArray", 1, [](Realm&, Args a) {
        this_webgl_context().disable_vertex_attrib_array(static_cast<uint32_t>(to_double(js_arg(a, 0))));
        return undefined();
    });
    I.method("vertexAttribPointer", 6, [](Realm&, Args a) {
        this_webgl_context().vertex_attrib_pointer(
            static_cast<uint32_t>(to_double(js_arg(a, 0))), static_cast<int>(to_double(js_arg(a, 1))),
            static_cast<uint32_t>(to_double(js_arg(a, 2))), to_bool(js_arg(a, 3)),
            static_cast<int>(to_double(js_arg(a, 4))),
            static_cast<std::size_t>(std::max(0.0, to_double(js_arg(a, 5)))));
        return undefined();
    });
    I.method("vertexAttrib1f", 2, [](Realm&, Args a) {
        this_webgl_context().vertex_attrib_4f(static_cast<uint32_t>(to_double(js_arg(a, 0))),
            static_cast<float>(to_double(js_arg(a, 1))), 0.0f, 0.0f, 1.0f);
        return undefined();
    });
    I.method("vertexAttrib2f", 3, [](Realm&, Args a) {
        this_webgl_context().vertex_attrib_4f(static_cast<uint32_t>(to_double(js_arg(a, 0))),
            static_cast<float>(to_double(js_arg(a, 1))), static_cast<float>(to_double(js_arg(a, 2))), 0.0f, 1.0f);
        return undefined();
    });
    I.method("vertexAttrib3f", 4, [](Realm&, Args a) {
        this_webgl_context().vertex_attrib_4f(static_cast<uint32_t>(to_double(js_arg(a, 0))),
            static_cast<float>(to_double(js_arg(a, 1))), static_cast<float>(to_double(js_arg(a, 2))),
            static_cast<float>(to_double(js_arg(a, 3))), 1.0f);
        return undefined();
    });
    I.method("vertexAttrib4f", 5, [](Realm&, Args a) {
        this_webgl_context().vertex_attrib_4f(static_cast<uint32_t>(to_double(js_arg(a, 0))),
            static_cast<float>(to_double(js_arg(a, 1))), static_cast<float>(to_double(js_arg(a, 2))),
            static_cast<float>(to_double(js_arg(a, 3))), static_cast<float>(to_double(js_arg(a, 4))));
        return undefined();
    });

    // Textures
    I.method("createTexture", 0, [](Realm& r, Args) {
        auto ctx = this_webgl_context_ptr();
        return make_webgl_handle(r, "WebGLTexture", ctx, ctx->create_texture());
    });
    I.method("deleteTexture", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->delete_texture(webgl_handle_id(js_arg(a, 0), ctx));
        return undefined();
    });
    I.method("isTexture", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        return boolean(ctx->is_texture(webgl_handle_id(js_arg(a, 0), ctx)));
    });
    I.method("activeTexture", 1, [](Realm&, Args a) {
        this_webgl_context().active_texture(static_cast<uint32_t>(to_double(js_arg(a, 0))));
        return undefined();
    });
    I.method("bindTexture", 2, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->bind_texture(static_cast<uint32_t>(to_double(js_arg(a, 0))), webgl_handle_id(js_arg(a, 1), ctx));
        return undefined();
    });
    I.method("texParameteri", 3, [](Realm&, Args a) {
        this_webgl_context().tex_parameteri(static_cast<uint32_t>(to_double(js_arg(a, 0))),
            static_cast<uint32_t>(to_double(js_arg(a, 1))), static_cast<int>(to_double(js_arg(a, 2))));
        return undefined();
    });
    I.method("texParameterf", 3, [](Realm&, Args a) {
        this_webgl_context().tex_parameteri(static_cast<uint32_t>(to_double(js_arg(a, 0))),
            static_cast<uint32_t>(to_double(js_arg(a, 1))), static_cast<int>(to_double(js_arg(a, 2))));
        return undefined();
    });
    I.method("texImage2D", 6, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        if (a.size() >= 9) {
            const uint32_t target = static_cast<uint32_t>(to_double(a[0]));
            const int level = static_cast<int>(to_double(a[1]));
            const uint32_t ifmt = static_cast<uint32_t>(to_double(a[2]));
            const int w = static_cast<int>(to_double(a[3]));
            const int h = static_cast<int>(to_double(a[4]));
            const int border = static_cast<int>(to_double(a[5]));
            const uint32_t fmt = static_cast<uint32_t>(to_double(a[6]));
            const uint32_t type = static_cast<uint32_t>(to_double(a[7]));
            std::string bytes;
            if (!a[8].is_nullish() && typed_array_bytes(a[8], bytes)) {
                ctx->tex_image_2d(target, level, ifmt, w, h, border, fmt, type,
                                  reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
            } else {
                ctx->tex_image_2d(target, level, ifmt, w, h, border, fmt, type, nullptr, 0);
            }
        } else if (a.size() >= 6) {
            const uint32_t target = static_cast<uint32_t>(to_double(a[0]));
            const int level = static_cast<int>(to_double(a[1]));
            const uint32_t ifmt = static_cast<uint32_t>(to_double(a[2]));
            const uint32_t fmt = static_cast<uint32_t>(to_double(a[3]));
            const uint32_t type = static_cast<uint32_t>(to_double(a[4]));
            if (auto* img = state_of<ImageDataState>(a[5])) {
                std::string bytes;
                if (typed_array_bytes(img->data, bytes)) {
                    ctx->tex_image_2d(target, level, ifmt, img->width, img->height, 0, fmt, type,
                                      reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
                }
            } else if (auto el = element_of(a[5]); el && is_canvas(*el) && el->canvas_context() &&
                       el->canvas_context()->image_data()) {
                auto im = el->canvas_context()->image_data();
                std::vector<uint8_t> rgba(static_cast<std::size_t>(im->width) * im->height * 4, 0);
                for (std::size_t i = 0; i < im->pixels.size() && i * 4 + 3 < rgba.size(); ++i) {
                    const uint32_t p = im->pixels[i];
                    rgba[i * 4] = static_cast<uint8_t>((p >> 16) & 0xFF);
                    rgba[i * 4 + 1] = static_cast<uint8_t>((p >> 8) & 0xFF);
                    rgba[i * 4 + 2] = static_cast<uint8_t>(p & 0xFF);
                    rgba[i * 4 + 3] = static_cast<uint8_t>((p >> 24) & 0xFF);
                }
                ctx->tex_image_2d(target, level, ifmt, im->width, im->height, 0, fmt, type,
                                  rgba.data(), rgba.size());
            }
        }
        return undefined();
    });
    I.method("texSubImage2D", 7, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        if (a.size() >= 9) {
            std::string bytes;
            if (typed_array_bytes(a[8], bytes)) {
                ctx->tex_sub_image_2d(
                    static_cast<uint32_t>(to_double(a[0])), static_cast<int>(to_double(a[1])),
                    static_cast<int>(to_double(a[2])), static_cast<int>(to_double(a[3])),
                    static_cast<int>(to_double(a[4])), static_cast<int>(to_double(a[5])),
                    static_cast<uint32_t>(to_double(a[6])), static_cast<uint32_t>(to_double(a[7])),
                    reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
            }
        }
        return undefined();
    });
    I.method("generateMipmap", 1, [](Realm&, Args a) {
        this_webgl_context().generate_mipmap(static_cast<uint32_t>(to_double(js_arg(a, 0))));
        return undefined();
    });

    // Framebuffers & Renderbuffers
    I.method("createFramebuffer", 0, [](Realm& r, Args) {
        auto ctx = this_webgl_context_ptr();
        return make_webgl_handle(r, "WebGLFramebuffer", ctx, ctx->create_framebuffer());
    });
    I.method("deleteFramebuffer", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->delete_framebuffer(webgl_handle_id(js_arg(a, 0), ctx));
        return undefined();
    });
    I.method("isFramebuffer", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        return boolean(ctx->is_framebuffer(webgl_handle_id(js_arg(a, 0), ctx)));
    });
    I.method("bindFramebuffer", 2, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->bind_framebuffer(static_cast<uint32_t>(to_double(js_arg(a, 0))), webgl_handle_id(js_arg(a, 1), ctx));
        return undefined();
    });
    I.method("createRenderbuffer", 0, [](Realm& r, Args) {
        auto ctx = this_webgl_context_ptr();
        return make_webgl_handle(r, "WebGLRenderbuffer", ctx, ctx->create_renderbuffer());
    });
    I.method("deleteRenderbuffer", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->delete_renderbuffer(webgl_handle_id(js_arg(a, 0), ctx));
        return undefined();
    });
    I.method("isRenderbuffer", 1, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        return boolean(ctx->is_renderbuffer(webgl_handle_id(js_arg(a, 0), ctx)));
    });
    I.method("bindRenderbuffer", 2, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->bind_renderbuffer(static_cast<uint32_t>(to_double(js_arg(a, 0))), webgl_handle_id(js_arg(a, 1), ctx));
        return undefined();
    });
    I.method("renderbufferStorage", 4, [](Realm&, Args a) {
        this_webgl_context().renderbuffer_storage(
            static_cast<uint32_t>(to_double(js_arg(a, 0))), static_cast<uint32_t>(to_double(js_arg(a, 1))),
            static_cast<int>(to_double(js_arg(a, 2))), static_cast<int>(to_double(js_arg(a, 3))));
        return undefined();
    });
    I.method("framebufferTexture2D", 5, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->framebuffer_texture_2d(
            static_cast<uint32_t>(to_double(js_arg(a, 0))), static_cast<uint32_t>(to_double(js_arg(a, 1))),
            static_cast<uint32_t>(to_double(js_arg(a, 2))), webgl_handle_id(js_arg(a, 3), ctx),
            static_cast<int>(to_double(js_arg(a, 4))));
        return undefined();
    });
    I.method("framebufferRenderbuffer", 4, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        ctx->framebuffer_renderbuffer(
            static_cast<uint32_t>(to_double(js_arg(a, 0))), static_cast<uint32_t>(to_double(js_arg(a, 1))),
            static_cast<uint32_t>(to_double(js_arg(a, 2))), webgl_handle_id(js_arg(a, 3), ctx));
        return undefined();
    });
    I.method("checkFramebufferStatus", 1, [](Realm&, Args a) {
        return num(static_cast<double>(this_webgl_context().check_framebuffer_status(
            static_cast<uint32_t>(to_double(js_arg(a, 0))))));
    });

    // Draw & Readback
    I.method("drawArrays", 3, [](Realm&, Args a) {
        this_webgl_context().draw_arrays(
            static_cast<uint32_t>(to_double(js_arg(a, 0))),
            static_cast<int>(to_double(js_arg(a, 1))),
            static_cast<int>(to_double(js_arg(a, 2))));
        return undefined();
    });
    I.method("drawElements", 4, [](Realm&, Args a) {
        this_webgl_context().draw_elements(
            static_cast<uint32_t>(to_double(js_arg(a, 0))),
            static_cast<int>(to_double(js_arg(a, 1))),
            static_cast<uint32_t>(to_double(js_arg(a, 2))),
            static_cast<std::size_t>(std::max(0.0, to_double(js_arg(a, 3)))));
        return undefined();
    });
    I.method("readPixels", 7, [](Realm&, Args a) {
        auto ctx = this_webgl_context_ptr();
        const int x = static_cast<int>(to_double(js_arg(a, 0)));
        const int y = static_cast<int>(to_double(js_arg(a, 1)));
        const int w = static_cast<int>(to_double(js_arg(a, 2)));
        const int h = static_cast<int>(to_double(js_arg(a, 3)));
        const uint32_t fmt = static_cast<uint32_t>(to_double(js_arg(a, 4)));
        const uint32_t type = static_cast<uint32_t>(to_double(js_arg(a, 5)));
        const JsValue dst = js_arg(a, 6);
        std::vector<uint8_t> rgba;
        if (!ctx->read_pixels(x, y, w, h, fmt, type, rgba)) return undefined();
        if (!rgba.empty() && !typed_array_write(dst, rgba.data(), rgba.size())) {
            ctx->set_error(webgl::INVALID_OPERATION);
        }
        return undefined();
    });

    I.method("getParameter", 1, [](Realm&, Args a) {
        auto& ctx = this_webgl_context();
        const uint32_t p = static_cast<uint32_t>(to_double(js_arg(a, 0)));
        auto arr_f4 = [](const std::array<float, 4>& v) {
            JsValue r = JsValue::make_array();
            for (int i = 0; i < 4; ++i) r.set_property(std::to_string(i), num(v[i]));
            return r;
        };
        auto arr_i4 = [](const std::array<int, 4>& v) {
            JsValue r = JsValue::make_array();
            for (int i = 0; i < 4; ++i) r.set_property(std::to_string(i), num(static_cast<double>(v[i])));
            return r;
        };
        switch (p) {
            case webgl::VENDOR: return str("Litebird");
            case webgl::RENDERER: return str("Litebird Software WebGL");
            case webgl::VERSION: return str(ctx.is_webgl2() ? "WebGL 2.0 (Litebird)" : "WebGL 1.0 (Litebird)");
            case webgl::SHADING_LANGUAGE_VERSION:
                return str(ctx.is_webgl2() ? "WebGL GLSL ES 3.00" : "WebGL GLSL ES 1.00");
            case webgl::MAX_TEXTURE_SIZE:
            case webgl::MAX_RENDERBUFFER_SIZE: return num(WebGLContext::kMaxTextureSize);
            case webgl::MAX_VERTEX_ATTRIBS: return num(WebGLContext::kMaxAttribs);
            case webgl::MAX_TEXTURE_IMAGE_UNITS:
            case webgl::MAX_VERTEX_TEXTURE_IMAGE_UNITS:
            case webgl::MAX_COMBINED_TEXTURE_IMAGE_UNITS: return num(WebGLContext::kMaxTextureUnits);
            case webgl::MAX_VERTEX_UNIFORM_VECTORS:
            case webgl::MAX_FRAGMENT_UNIFORM_VECTORS: return num(128);
            case webgl::MAX_VARYING_VECTORS: return num(16);
            case webgl::MAX_VIEWPORT_DIMS: {
                JsValue r = JsValue::make_array();
                r.set_property("0", num(WebGLContext::kMaxTextureSize));
                r.set_property("1", num(WebGLContext::kMaxTextureSize));
                return r;
            }
            case webgl::VIEWPORT: return arr_i4(ctx.viewport_box());
            case webgl::SCISSOR_BOX: return arr_i4(ctx.scissor_box());
            case webgl::COLOR_CLEAR_VALUE: return arr_f4(ctx.clear_color_value());
            case webgl::BLEND_COLOR: return arr_f4(ctx.blend_color_value());
            case webgl::DEPTH_CLEAR_VALUE: return num(ctx.clear_depth_value());
            case webgl::DEPTH_FUNC: return num(static_cast<double>(ctx.depth_func_value()));
            case webgl::DEPTH_WRITEMASK: return boolean(ctx.depth_mask_value());
            case webgl::COLOR_WRITEMASK: {
                auto m = ctx.color_mask_value();
                JsValue r = JsValue::make_array();
                for (int i = 0; i < 4; ++i) r.set_property(std::to_string(i), boolean(m[i]));
                return r;
            }
            case webgl::CULL_FACE_MODE: return num(static_cast<double>(ctx.cull_face_value()));
            case webgl::FRONT_FACE: return num(static_cast<double>(ctx.front_face_value()));
            case webgl::LINE_WIDTH: return num(ctx.line_width_value());
            case webgl::ACTIVE_TEXTURE: return num(static_cast<double>(ctx.active_texture_enum()));
            case webgl::BLEND_SRC_RGB: return num(static_cast<double>(ctx.blend_src_rgb()));
            case webgl::BLEND_DST_RGB: return num(static_cast<double>(ctx.blend_dst_rgb()));
            case webgl::BLEND_SRC_ALPHA: return num(static_cast<double>(ctx.blend_src_alpha()));
            case webgl::BLEND_DST_ALPHA: return num(static_cast<double>(ctx.blend_dst_alpha()));
            case webgl::BLEND_EQUATION_RGB: return num(static_cast<double>(ctx.blend_eq_rgb()));
            case webgl::BLEND_EQUATION_ALPHA: return num(static_cast<double>(ctx.blend_eq_alpha()));
            case webgl::DEPTH_RANGE: {
                auto dr = ctx.depth_range_value();
                JsValue r = JsValue::make_array();
                r.set_property("0", num(dr[0]));
                r.set_property("1", num(dr[1]));
                return r;
            }
            case webgl::DEPTH_TEST:
            case webgl::SCISSOR_TEST:
            case webgl::BLEND:
            case webgl::CULL_FACE:
            case webgl::DITHER:
            case webgl::STENCIL_TEST:
                return boolean(ctx.is_enabled(p));
            default: return null();
        }
    });

    if (webgl2) {
        I.method("createVertexArray", 0, [](Realm& r, Args) {
            auto ctx = this_webgl_context_ptr();
            return make_webgl_handle(r, "WebGLVertexArrayObject", ctx, ctx->create_vertex_array());
        });
        I.method("deleteVertexArray", 1, [](Realm&, Args a) {
            auto ctx = this_webgl_context_ptr();
            ctx->delete_vertex_array(webgl_handle_id(js_arg(a, 0), ctx));
            return undefined();
        });
        I.method("isVertexArray", 1, [](Realm&, Args a) {
            auto ctx = this_webgl_context_ptr();
            return boolean(ctx->is_vertex_array(webgl_handle_id(js_arg(a, 0), ctx)));
        });
        I.method("bindVertexArray", 1, [](Realm&, Args a) {
            auto ctx = this_webgl_context_ptr();
            ctx->bind_vertex_array(webgl_handle_id(js_arg(a, 0), ctx));
            return undefined();
        });
    }
}

void setup_webgl_rendering_context(Iface& I) { install_webgl_common_members(I, false); }
void setup_webgl2_rendering_context(Iface& I) { install_webgl_common_members(I, true); }

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
        const std::string type = normalize_canvas_context_type(to_str(args[0]));
        if (type.empty()) return null();
        JsValue wrapper = wrap(r, el);
        if (const JsValue* locked = slot(wrapper, "contextKind")) {
            if (to_str(*locked) != type) return null();
        }
        auto backing = el.get_or_create_canvas_context_2d();
        if (!backing) return null();
        set_slot(wrapper, "contextKind", str(type));
        if (type == "2d") {
            return cached(wrapper, "context2d", [&]() {
                // `direction: "inherit"` walks the element's ancestors for a
                // `dir`, so the context is told which element it belongs to.
                backing->set_owner_element(el.shared_from_this());
                JsValue object = backing->to_js_value(backing);
                // ImageData needs a realm, so the three methods that speak it are
                // added here rather than in Canvas2D.cpp.
                install_image_data_methods(r, object, backing);
                install_draw_image(r, object, backing);
                // Phase 280: `reset()` and `createPattern()` are object-level
                // state, so they are installed here with the rest.
                install_canvas_reset(r, object, backing);
                install_canvas_save_restore(r, object, backing);
                install_canvas_create_pattern(r, object, backing);
                return object;
            });
        }
        const bool is_gl2 = (type == "webgl2");
        return cached(wrapper, is_gl2 ? "contextWebGL2" : "contextWebGL", [&]() {
            auto st = std::make_shared<WebGLContextState>();
            st->ctx = std::make_shared<WebGLContext>(backing, is_gl2);
            JsValue gl = r.make(is_gl2 ? "WebGL2RenderingContext" : "WebGLRenderingContext", st);
            set_slot(gl, "canvas", wrapper);
            return gl;
        });
    });
    C.method("toDataURL", 0, [](Realm&, Element& el, Args args) { return canvas_to_data_url(el, args); });
    C.method("toBlob", 1, [](Realm& r, Element& el, Args args) { return canvas_to_blob(r, el, args); });
}

void setup_media(Iface& I) {
    ElementMembers<&is_media> M(I);
    reflect(I, "s:preload b:autoplay b:loop b:controls b:defaultMuted:muted s:crossOrigin:crossorigin");
    M.accessor("src",
        [](Realm& r, Element& el) {
            const std::string raw = el.get_attribute("src");
            if (raw.empty()) return str("");
            return str(resolve_url(r, el, raw));
        },
        [](Realm& r, Element& el, const JsValue& value) {
            el.set_attribute("src", to_str(value));
            media_sync_source(r, el, true);
        });
    I.constant("NETWORK_EMPTY", 0);
    I.constant("NETWORK_IDLE", 1);
    I.constant("NETWORK_LOADING", 2);
    I.constant("NETWORK_NO_SOURCE", 3);
    I.constant("HAVE_NOTHING", 0);
    I.constant("HAVE_METADATA", 1);
    I.constant("HAVE_CURRENT_DATA", 2);
    I.constant("HAVE_FUTURE_DATA", 3);
    I.constant("HAVE_ENOUGH_DATA", 4);
    M.getter("currentSrc", [](Realm& r, Element& el) {
        media_sync_source(r, el);
        return str(media_slot_string(wrap(r, el), "mediaCurrentSrc"));
    });
    M.getter("paused", [](Realm& r, Element& el) {
        return boolean(media_slot_bool(wrap(r, el), "mediaPaused", true));
    });
    M.getter("ended", [](Realm& r, Element& el) {
        return boolean(media_slot_bool(wrap(r, el), "mediaEnded", false));
    });
    M.getter("duration", [](Realm& r, Element& el) {
        media_sync_source(r, el);
        return num(media_slot_number(wrap(r, el), "mediaDuration", std::numeric_limits<double>::quiet_NaN()));
    });
    M.getter("readyState", [](Realm& r, Element& el) {
        media_sync_source(r, el);
        return num(media_slot_number(wrap(r, el), "mediaReadyState", 0.0));
    });
    M.getter("networkState", [](Realm& r, Element& el) {
        media_sync_source(r, el);
        return num(media_slot_number(wrap(r, el), "mediaNetworkState", 0.0));
    });
    M.getter("error", [](Realm& r, Element& el) {
        media_sync_source(r, el);
        JsValue w = wrap(r, el);
        const int code = static_cast<int>(media_slot_number(w, "mediaErrorCode", 0.0));
        if (code == 0) return null();
        return make_media_error(r, code, media_slot_string(w, "mediaErrorMsg"));
    });
    M.getter("buffered", [](Realm& r, Element& el) {
        media_sync_source(r, el);
        const double dur = media_slot_number(wrap(r, el), "mediaDuration", std::numeric_limits<double>::quiet_NaN());
        if (std::isfinite(dur) && dur > 0.0) return make_time_ranges(r, {{0.0, dur}});
        return make_time_ranges(r);
    });
    M.getter("seekable", [](Realm& r, Element& el) {
        media_sync_source(r, el);
        const double dur = media_slot_number(wrap(r, el), "mediaDuration", std::numeric_limits<double>::quiet_NaN());
        if (std::isfinite(dur) && dur > 0.0) return make_time_ranges(r, {{0.0, dur}});
        return make_time_ranges(r);
    });
    M.getter("played", [](Realm& r, Element&) { return make_time_ranges(r); });
    M.accessor("currentTime",
        [](Realm& r, Element& el) { return num(media_slot_number(wrap(r, el), "mediaCurrentTime", 0.0)); },
        [](Realm& r, Element& el, const JsValue& value) {
            const double asked = media_finite_double(r, value, "currentTime");
            media_slot_set_number(wrap(r, el), "mediaCurrentTime", asked);
        });
    M.accessor("volume",
        [](Realm& r, Element& el) { return num(media_slot_number(wrap(r, el), "mediaVolume", 1.0)); },
        [](Realm& r, Element& el, const JsValue& value) {
            const double asked = media_finite_double(r, value, "volume");
            if (asked < 0.0 || asked > 1.0) {
                throw_dom(r, "IndexSizeError",
                          "Failed to set the 'volume' property on 'HTMLMediaElement': "
                          "The provided double value is outside the range [0, 1].");
            }
            media_slot_set_number(wrap(r, el), "mediaVolume", asked);
        });
    // `muted` is the live state and does not touch the `muted` content
    // attribute (`defaultMuted` reflects that, per HTML §4.8.11.3).
    M.accessor("muted",
        [](Realm& r, Element& el) { return boolean(media_slot_bool(wrap(r, el), "mediaMuted", false)); },
        [](Realm& r, Element& el, const JsValue& value) {
            media_slot_set_bool(wrap(r, el), "mediaMuted", to_bool(value));
        });
    M.accessor("playbackRate",
        [](Realm& r, Element& el) { return num(media_slot_number(wrap(r, el), "mediaPlaybackRate", 1.0)); },
        [](Realm& r, Element& el, const JsValue& value) {
            const double asked = media_finite_double(r, value, "playbackRate");
            media_slot_set_number(wrap(r, el), "mediaPlaybackRate", asked);
        });
    M.method("pause", 0, [](Realm& r, Element& el, Args) {
        media_slot_set_bool(wrap(r, el), "mediaPaused", true);
        return undefined();
    });
    M.method("load", 0, [](Realm& r, Element& el, Args) {
        JsValue w = wrap(r, el);
        media_slot_set_bool(w, "mediaPaused", true);
        media_slot_set_bool(w, "mediaEnded", false);
        media_slot_set_number(w, "mediaCurrentTime", 0.0);
        media_sync_source(r, el, true);
        return undefined();
    });
    M.method("play", 0, [](Realm& r, Element& el, Args) {
        media_sync_source(r, el);
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        if (!promise.is_function()) return undefined();
        JsValue w = wrap(r, el);
        if (media_slot_number(w, "mediaReadyState", 0.0) >= 1.0) {
            media_slot_set_bool(w, "mediaPaused", false);
            media_slot_set_bool(w, "mediaEnded", false);
            return js_call(promise.get_property("resolve"), promise, {undefined()});
        }
        JsValue err = make_dom_exception(r, "NotSupportedError",
                                         "The element has no supported sources.");
        return js_call(promise.get_property("reject"), promise, {err});
    });
    M.method("canPlayType", 1, [](Realm&, Element&, Args args) {
        if (js_arg(args, 0).is_undefined()) return str("");
        return str(can_play_type(to_str(js_arg(args, 0))));
    });
}

struct OffscreenCanvasState : JsExotic {
    int width = 0;
    int height = 0;
    std::string context_kind;
    decltype(std::declval<Element>().get_or_create_canvas_context_2d()) context;
    JsValue context_obj = JsValue::make_undefined();
    void trace(Heap& heap) override { heap.mark_value(context_obj); }
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
        const std::string type = normalize_canvas_context_type(to_str(args[0]));
        if (type.empty()) return null();
        if (!st.context_kind.empty() && st.context_kind != type) return null();
        if (!st.context) {
            using CtxPtr = decltype(st.context);
            st.context = std::make_shared<CtxPtr::element_type>(std::max(st.width, 1), std::max(st.height, 1));
            st.context_kind = type;
            if (type == "2d") {
                JsValue object = st.context->to_js_value(st.context);
                install_image_data_methods(r, object, st.context);
                install_draw_image(r, object, st.context);
                st.context_obj = object;
            } else {
                const bool is_gl2 = (type == "webgl2");
                auto gl_st = std::make_shared<WebGLContextState>();
                gl_st->ctx = std::make_shared<WebGLContext>(st.context, is_gl2);
                JsValue gl = r.make(is_gl2 ? "WebGL2RenderingContext" : "WebGLRenderingContext", gl_st);
                set_slot(gl, "canvas", JsValue::current_this());
                st.context_obj = gl;
            }
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
        // OffscreenCanvas is exposed in both Window and worker realms. Promise
        // is a realm-global dependency, not a Window dependency.
        JsValue promise = realm_global_binding(r, "Promise");
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
    register_interface({"SVGLength", nullptr, setup_svg_length, nullptr, 0, nullptr});
    register_interface({"SVGAnimatedLength", nullptr, setup_svg_animated_length, nullptr, 0, nullptr});
    register_interface({"SVGLengthList", nullptr, setup_svg_length_list, nullptr, 0, nullptr});
    register_interface({"SVGAnimatedLengthList", nullptr, setup_svg_animated_length_list, nullptr, 0, nullptr});
    register_interface({"SVGPreserveAspectRatio", nullptr, setup_svg_preserve_aspect_ratio, nullptr, 0, nullptr});
    register_interface({"SVGAnimatedPreserveAspectRatio", nullptr, setup_svg_animated_preserve_aspect_ratio, nullptr, 0, nullptr});
    register_interface({"SVGElement", "Element", setup_svg_element, nullptr, 0, nullptr});
    register_interface({"SVGGraphicsElement", "SVGElement", setup_svg_graphics_element, nullptr, 0, nullptr});
    register_interface({"SVGSVGElement", "SVGGraphicsElement", setup_svg_svg_element, nullptr, 0, nullptr});
    register_interface({"SVGSymbolElement", "SVGGraphicsElement", setup_svg_fit_to_view_box, nullptr, 0, nullptr});
    register_interface({"SVGMarkerElement", "SVGElement", setup_svg_marker_element, nullptr, 0, nullptr});
    register_interface({"SVGPatternElement", "SVGElement", setup_svg_fit_to_view_box, nullptr, 0, nullptr});
    register_interface({"SVGViewElement", "SVGElement", setup_svg_fit_to_view_box, nullptr, 0, nullptr});
    // The graphics elements: place in the chain is all that is set here, because
    // `getBBox` is SVGGraphicsElement's and is reached through the parent.
    for (const char* name : {"SVGGElement", "SVGRectElement", "SVGCircleElement", "SVGEllipseElement",
                             "SVGLineElement", "SVGPathElement", "SVGPolygonElement", "SVGPolylineElement",
                             "SVGImageElement", "SVGForeignObjectElement", "SVGUseElement", "SVGAElement",
                             "SVGSwitchElement"}) {
        register_interface({name, "SVGGraphicsElement", setup_svg_length_attributes, nullptr, 0, nullptr});
    }
    // The text family is a chain of its own: `text` and `tspan` are
    // SVGTextPositioningElements (the four length *lists*), `textPath` is only
    // an SVGTextContentElement, and all three are graphics elements.
    register_interface(InterfaceDef{"SVGTextContentElement", "SVGGraphicsElement", setup_svg_length_attributes, nullptr, 0, nullptr});
    register_interface(InterfaceDef{"SVGTextPositioningElement", "SVGTextContentElement", setup_svg_text_positioning_element, nullptr, 0, nullptr});
    register_interface(InterfaceDef{"SVGTextElement", "SVGTextPositioningElement", setup_svg_text_element, nullptr, 0, nullptr});
    register_interface(InterfaceDef{"SVGTSpanElement", "SVGTextPositioningElement", setup_svg_tspan_element, nullptr, 0, nullptr});
    register_interface(InterfaceDef{"SVGTextPathElement", "SVGTextContentElement", setup_svg_text_path_element, nullptr, 0, nullptr});
    // Interfaces of their own, but not graphics elements: they inherit
    // SVGElement, so none of them has `getBBox()`.
    for (const char* name : {"SVGDefsElement", "SVGDescElement", "SVGMetadataElement", "SVGStyleElement",
                             "SVGScriptElement", "SVGStopElement", "SVGLinearGradientElement",
                             "SVGRadialGradientElement", "SVGClipPathElement", "SVGMaskElement",
                             "SVGFilterElement", "SVGFEGaussianBlurElement", "SVGFEOffsetElement",
                             "SVGFEBlendElement", "SVGFEColorMatrixElement", "SVGFEFloodElement",
                             "SVGFECompositeElement", "SVGFEMergeElement", "SVGFEImageElement",
                             "SVGFETileElement", "SVGFETurbulenceElement", "SVGFEDropShadowElement",
                             "SVGAnimateElement", "SVGSetElement", "SVGAnimateTransformElement"}) {
        register_interface({name, "SVGElement", nullptr, nullptr, 0, nullptr});
    }
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
    register_interface({"Path2D", nullptr, setup_path2d, construct_path2d, 0, nullptr});
    register_interface({"ImageData", nullptr, setup_image_data, construct_image_data, 2, nullptr});
    register_interface({"WebGLRenderingContext", nullptr, setup_webgl_rendering_context, nullptr, 0, nullptr});
    register_interface({"WebGL2RenderingContext", nullptr, setup_webgl2_rendering_context, nullptr, 0, nullptr});
    register_interface({"WebGLShader", nullptr, nullptr, nullptr, 0, nullptr});
    register_interface({"WebGLProgram", nullptr, nullptr, nullptr, 0, nullptr});
    register_interface({"WebGLBuffer", nullptr, nullptr, nullptr, 0, nullptr});
    register_interface({"WebGLTexture", nullptr, nullptr, nullptr, 0, nullptr});
    register_interface({"WebGLFramebuffer", nullptr, nullptr, nullptr, 0, nullptr});
    register_interface({"WebGLRenderbuffer", nullptr, nullptr, nullptr, 0, nullptr});
    register_interface({"WebGLUniformLocation", nullptr, nullptr, nullptr, 0, nullptr});
    register_interface({"WebGLActiveInfo", nullptr, setup_webgl_active_info, nullptr, 0, nullptr});
    register_interface({"WebGLShaderPrecisionFormat", nullptr, setup_webgl_precision_format, nullptr, 0, nullptr});
    register_interface({"WebGLVertexArrayObject", nullptr, nullptr, nullptr, 0, nullptr});
    register_interface({"HTMLMediaElement", "HTMLElement", setup_media, element_ctor, 0, nullptr});
    register_interface({"HTMLAudioElement", "HTMLMediaElement", nullptr, element_ctor, 0, nullptr});
    register_interface({"HTMLVideoElement", "HTMLMediaElement", setup_video, element_ctor, 0, nullptr});
    // HTML §4.8.11.3 and §"TimeRanges": constructible by name from a script,
    // not constructible by `new` (Chrome and Firefox agree).
    register_interface({"TimeRanges", nullptr, setup_time_ranges, nullptr, 0, nullptr});
    register_interface({"MediaError", nullptr, setup_media_error, nullptr, 0, nullptr});
    register_interface({"HTMLTableElement", "HTMLElement", setup_table, element_ctor, 0, nullptr});
    register_interface({"HTMLTableSectionElement", "HTMLElement", setup_table_section, element_ctor, 0, nullptr});
    register_interface({"HTMLTableRowElement", "HTMLElement", setup_table_row, element_ctor, 0, nullptr});
    register_interface({"HTMLTableCellElement", "HTMLElement", setup_table_cell, element_ctor, 0, nullptr});
    register_interface({"HTMLTemplateElement", "HTMLElement", setup_template, element_ctor, 0, nullptr});
    register_interface({"HTMLSlotElement", "HTMLElement", setup_slot, element_ctor, 0, nullptr});
    register_interface({"HTMLDialogElement", "HTMLElement", setup_dialog, element_ctor, 0, nullptr});
    register_media_interfaces();
}

JsValue make_time_ranges_with(Realm& realm, std::vector<std::pair<double, double>> ranges) {
    return make_time_ranges(realm, std::move(ranges));
}

} // namespace dom
} // namespace litebird
