#include "litebird/shell/BrowserShell.hpp"
#include "litebird/common/StackLimit.hpp"
#include "litebird/css/CssParser.hpp"
#include "litebird/css/DocumentStyles.hpp"
#include "litebird/dom/Event.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/html/HtmlParser.hpp"
#include "litebird/js/DomBindings.hpp"
#include "litebird/js/DomEditing.hpp"
#include "litebird/net/DataUrl.hpp"
#include "litebird/render/BitmapRenderer.hpp"
#include "litebird/render/ImageDecoder.hpp"
#include "litebird/xml/XmlDom.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <unordered_set>

#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

namespace litebird {

namespace {

struct RawTerminalMode {
    struct termios orig_termios;
    bool enabled{false};

    RawTerminalMode() {
        if (!isatty(STDIN_FILENO)) return;
        if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
            struct termios raw = orig_termios;
            raw.c_lflag &= ~(ECHO | ICANON);
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 1; // 100ms
            if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) {
                enabled = true;
            }
        }
    }

    ~RawTerminalMode() {
        if (enabled) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
            std::cout << "\033[?25h" << std::flush; // Ensure cursor is visible
        }
    }
};

} // namespace

BrowserShell::BrowserShell(float viewport_w, float viewport_h, int term_cols, int term_rows)
    : m_viewport_w(viewport_w), m_viewport_h(viewport_h),
      m_term_cols(term_cols), m_term_rows(term_rows) {}

bool BrowserShell::can_go_back() const {
    return m_history_index > 0 && !m_history.empty();
}

bool BrowserShell::can_go_forward() const {
    return !m_history.empty() && m_history_index + 1 < m_history.size();
}

// Shows the page whose markup is `html`, loaded from m_current_url: the
// tree, its scripts run as the parser reaches them (see
// JsInterpreter::load_page), and the style sheets of the page as the scripts
// left it. The page's address is set first: it is what its scripts see as
// location.href and what its relative links are relative to.
void BrowserShell::show_page(const std::string& html, const std::string& content_type) {
    // The old page goes first, so that there is never a moment with two.
    m_js_engine.reset();
    m_document = std::make_shared<Document>();
    m_document->set_url(ResourceLoader::address_of(m_current_url));
    m_document->set_viewport_size(m_viewport_w, m_viewport_h);

    MimeType mime;
    const bool parsed_mime = MimeType::parse(content_type, mime);
    const bool is_svg = parsed_mime && mime.type == "image" && mime.subtype == "svg+xml";
    const bool is_xml = is_svg ||
                        (parsed_mime && ((mime.type == "application" && (mime.subtype == "xml" || mime.subtype == "xhtml+xml")) ||
                                         (mime.type == "text" && mime.subtype == "xml")));

    std::string transport_charset;
    if (parsed_mime) {
        if (const std::string* cs = mime.parameter("charset")) {
            transport_charset = *cs;
        }
    }

    if (is_xml) {
        m_document->set_content_type(mime.essence());
        std::string xml_text;
        std::string xml_enc;
        xml::XmlError xml_err;
        if (xml::decode_xml(html, xml_text, xml_enc, xml_err, transport_charset)) {
            m_document->set_character_set(xml_enc);
            if (!xml::parse_xml_document(m_document, xml_text, xml_err)) {
                m_document = std::make_shared<Document>();
                m_document->set_url(ResourceLoader::address_of(m_current_url));
                m_document->set_viewport_size(m_viewport_w, m_viewport_h);
            }
        }
        m_js_engine = std::make_unique<JsInterpreter>(m_document);
    } else if (parsed_mime && mime.type == "image") {
        // Standalone raster image URL: wrap in a clean viewer document.
        const std::string safe_url = m_document->url();
        const std::string img_html =
            "<!DOCTYPE html><html><head><title>Image</title>"
            "<style>body{margin:0;background:#0f172a;padding:24px;}img{display:block;}</style>"
            "</head><body><img src=\"" + safe_url + "\"></body></html>";
        m_js_engine = std::make_unique<JsInterpreter>(m_document);
        m_js_engine->load_page(img_html, false);
    } else {
        std::string resolved_cs;
        const std::string decoded_html = HtmlParser::decode_bytes(html, transport_charset, "UTF-8", &resolved_cs);
        m_document->set_character_set(resolved_cs);
        m_js_engine = std::make_unique<JsInterpreter>(m_document);
        m_js_engine->load_page(decoded_html);
    }

    m_document->set_layout_updater([this](Document&) {
        m_resolver = DocumentStyles::build_resolver(
            *m_document, &m_document->stylesheet_cache(), m_viewport_w, m_viewport_h
        );
        m_layout_root = LayoutTree::build(m_document, m_resolver, m_viewport_w, m_viewport_h);
    });

    m_js_engine->notify_document_loaded();
    EventLoop::default_loop().run_until_idle(std::chrono::milliseconds(150));

    m_resolver = DocumentStyles::build_resolver(
        *m_document, &m_document->stylesheet_cache(), m_viewport_w, m_viewport_h
    );
}

bool BrowserShell::back() {
    if (!can_go_back()) return false;
    m_history_index--;
    std::string prev_url = m_history[m_history_index];

    // Load without altering history vector
    auto resp = ResourceLoader::load_navigation(prev_url);
    if (!resp.success) {
        m_status_message = "Failed to load: " + resp.error_message;
        return false;
    }

    m_current_url = (!resp.url.empty() && (prev_url.rfind("http://", 0) == 0 || prev_url.rfind("https://", 0) == 0))
                        ? resp.url
                        : prev_url;
    show_page(resp.body, resp.content_type);

    m_scroll_y = 0.0f;
    m_scroll_x = 0.0f;
    rebuild_layout();
    m_status_message = "Navigated Back to " + m_current_url;
    m_dirty = true;
    return true;
}

bool BrowserShell::forward() {
    if (!can_go_forward()) return false;
    m_history_index++;
    std::string next_url = m_history[m_history_index];

    auto resp = ResourceLoader::load_navigation(next_url);
    if (!resp.success) {
        m_status_message = "Failed to load: " + resp.error_message;
        return false;
    }

    m_current_url = (!resp.url.empty() && (next_url.rfind("http://", 0) == 0 || next_url.rfind("https://", 0) == 0))
                        ? resp.url
                        : next_url;
    show_page(resp.body, resp.content_type);

    m_scroll_y = 0.0f;
    m_scroll_x = 0.0f;
    rebuild_layout();
    m_status_message = "Navigated Forward to " + m_current_url;
    m_dirty = true;
    return true;
}

bool BrowserShell::reload() {
    if (m_current_url.empty()) return false;
    auto resp = ResourceLoader::load_navigation(m_current_url);
    if (!resp.success) {
        m_status_message = "Reload failed: " + resp.error_message;
        return false;
    }

    if (!resp.url.empty() && (m_current_url.rfind("http://", 0) == 0 || m_current_url.rfind("https://", 0) == 0)) {
        m_current_url = resp.url;
    }
    show_page(resp.body, resp.content_type);

    rebuild_layout();
    m_status_message = "Reloaded " + m_current_url;
    m_dirty = true;
    return true;
}

bool BrowserShell::navigate_to(const std::string& url) {
    auto resp = ResourceLoader::load_navigation(url);
    if (!resp.success) {
        m_status_message = "Error loading " + url + ": " + resp.error_message;
        std::string error_html = "<html><head><title>Error</title><style>body{font-size:16px;color:#ef4444;background-color:#0f172a;margin:20px;}h1{color:#f87171;}</style></head><body><h1>Navigation Error</h1><p>" + resp.error_message + "</p></body></html>";
        m_document = HtmlParser::parse(error_html);
        m_document->set_viewport_size(m_viewport_w, m_viewport_h);
        m_resolver = DocumentStyles::build_resolver(*m_document, nullptr, m_viewport_w, m_viewport_h);
        m_js_engine.reset();
        m_js_engine = std::make_unique<JsInterpreter>(m_document);
        rebuild_layout();
        m_dirty = true;
        return false;
    }

    const bool is_http_nav = (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0);
    const std::string final_url = (is_http_nav && !resp.url.empty()) ? resp.url : url;

    // Update history
    if (!m_history.empty() && m_history_index + 1 < m_history.size()) {
        m_history.erase(m_history.begin() + m_history_index + 1, m_history.end());
    }
    m_history.push_back(final_url);
    m_history_index = m_history.size() - 1;
    m_current_url = final_url;

    show_page(resp.body, resp.content_type);

    m_scroll_y = 0.0f;
    m_scroll_x = 0.0f;
    rebuild_layout();
    m_status_message = "Loaded " + m_current_url;
    m_dirty = true;
    return true;
}

void BrowserShell::set_viewport_size(float width, float height) {
    width = std::clamp(width, 200.0f, 4096.0f);
    height = std::clamp(height, 150.0f, 4096.0f);
    if (m_viewport_w == width && m_viewport_h == height) return;
    m_viewport_w = width;
    m_viewport_h = height;
    if (m_document) {
        m_document->set_viewport_size(m_viewport_w, m_viewport_h);
    }
    if (m_js_engine) {
        m_js_engine->notify_window_resized();
    }
    rebuild_layout();
    m_dirty = true;
}

void BrowserShell::rebuild_layout() {
    if (!m_document) return;

    m_resolver = DocumentStyles::build_resolver(
        *m_document, &m_document->stylesheet_cache(), m_viewport_w, m_viewport_h
    );
    m_layout_root = LayoutTree::build(m_document, m_resolver, m_viewport_w, m_viewport_h);
    if (m_layout_root) {
        m_display_list = DisplayList::build(m_layout_root);
        m_content_height = m_layout_root->dimensions.border_box().height;
    } else {
        m_content_height = 0.0f;
    }

    scan_interactive_elements();
}

void BrowserShell::relayout() {
    rebuild_layout();
    m_dirty = true;
}

Rect BrowserShell::compute_element_bounds(const std::shared_ptr<Element>& el, const std::shared_ptr<LayoutBox>& box) const {
    if (!el) return Rect();
    if (el->has_layout_box()) {
        return el->bounding_box();
    }
    if (!box || stack_is_exhausted()) return Rect();
    return el->bounding_box();
}

void BrowserShell::scan_interactive_elements() {
    m_interactive_elements.clear();
    if (!m_document || !m_layout_root) return;

    std::vector<std::shared_ptr<Element>> candidates;

    // Every element of the document, in tree order, without recursion.
    for_each_descendant(*m_document, [&](Node& n) {
        if (!n.is_element()) return true;
        auto el = std::static_pointer_cast<Element>(n.shared_from_this());
        std::string tag = el->tag_name();
        std::string cls = el->get_attribute("class");
        std::string id = el->id();
        bool is_link = (tag == "a" && el->has_attribute("href"));
        bool is_btn = (tag == "button" || cls.find("btn") != std::string::npos || id.find("btn") != std::string::npos || el->has_listeners("click"));
        bool is_inp = (tag == "input");

        if (is_link || is_btn || is_inp) {
            candidates.push_back(el);
        }
        return true;
    });

    for (const auto& el : candidates) {
        Rect bounds = compute_element_bounds(el, m_layout_root);
        InteractiveElement item;
        item.element = el;
        item.bounds = bounds;
        item.tag = el->tag_name();
        item.text = el->text_content();
        item.href = el->get_attribute("href");
        item.is_link = (item.tag == "a" && !item.href.empty());
        item.is_button = (item.tag == "button" || el->get_attribute("class").find("btn") != std::string::npos || el->has_listeners("click"));
        item.is_input = (item.tag == "input");

        m_interactive_elements.push_back(std::move(item));
    }

    if (!m_interactive_elements.empty()) {
        if (m_focused_index < 0 || m_focused_index >= static_cast<int>(m_interactive_elements.size())) {
            m_focused_index = 0;
        }
    } else {
        m_focused_index = -1;
    }
}

std::shared_ptr<Element> BrowserShell::focused_element() const {
    if (m_focused_index >= 0 && m_focused_index < static_cast<int>(m_interactive_elements.size())) {
        return m_interactive_elements[m_focused_index].element;
    }
    return nullptr;
}

void BrowserShell::focus_next() {
    if (m_interactive_elements.empty()) return;
    m_focused_index = (m_focused_index + 1) % m_interactive_elements.size();
    m_status_message = "Focused: <" + m_interactive_elements[m_focused_index].tag + "> \"" + m_interactive_elements[m_focused_index].text + "\"";
    m_dirty = true;
}

void BrowserShell::focus_prev() {
    if (m_interactive_elements.empty()) return;
    m_focused_index = (m_focused_index - 1 + m_interactive_elements.size()) % m_interactive_elements.size();
    m_status_message = "Focused: <" + m_interactive_elements[m_focused_index].tag + "> \"" + m_interactive_elements[m_focused_index].text + "\"";
    m_dirty = true;
}

void BrowserShell::focus_element(int index) {
    if (index >= 0 && index < static_cast<int>(m_interactive_elements.size())) {
        m_focused_index = index;
        m_dirty = true;
    }
}

bool BrowserShell::activate_focused() {
    auto el = focused_element();
    if (!el) return false;

    const auto& item = m_interactive_elements[m_focused_index];
    if (item.is_link && !item.href.empty()) {
        std::string next_url = ResourceLoader::resolve_url(m_current_url, item.href);
        m_status_message = "Following link to " + next_url;
        return navigate_to(next_url);
    }

    // Trigger click on element
    m_status_message = "Clicked <" + item.tag + "> #" + el->id();
    el->click();
    rebuild_layout();
    m_dirty = true;
    return true;
}

std::shared_ptr<Element> BrowserShell::element_at(float x, float y) const {
    if (!m_layout_root) return nullptr;

    float adjusted_y = y + m_scroll_y;
    float adjusted_x = x + m_scroll_x;

    std::shared_ptr<Element> hit_element = nullptr;

    std::function<void(const std::shared_ptr<LayoutBox>&)> search = [&](const std::shared_ptr<LayoutBox>& box) {
        if (!box) return;
        if (stack_is_exhausted()) return;
        Rect b_rect = box->dimensions.border_box();
        Point local_p(adjusted_x, adjusted_y);
        if (box->style->has_transform && !box->style->transform_matrix.is_identity()) {
            local_p = box->style->transform_matrix.inverse().map_point(adjusted_x, adjusted_y);
        }
        const bool inside = b_rect.contains(local_p.x, local_p.y);

        if (inside) {
            if (box->node) {
                if (auto el = std::dynamic_pointer_cast<Element>(box->node)) {
                    hit_element = el;
                } else if (box->node->parent()) {
                    hit_element = std::dynamic_pointer_cast<Element>(box->node->parent());
                }
            }

            // Check inline formatting context fragments (`<a>`, `<span>`, `<b>`, `<img>`, ...)
            for (const auto& line : box->line_boxes) {
                for (const auto& frag : line.fragments) {
                    if (frag.is_space || !frag.box) continue;
                    if (frag.rect.contains(local_p.x, local_p.y)) {
                        if (frag.box->node) {
                            if (auto el = std::dynamic_pointer_cast<Element>(frag.box->node)) {
                                hit_element = el;
                            } else if (frag.box->node->parent()) {
                                hit_element = std::dynamic_pointer_cast<Element>(frag.box->node->parent());
                            }
                        }
                    }
                }
            }

            // The painter emits these line fragments *instead of* painting
            // their backing child boxes. The latter have unfragmented bounds
            // and can steal a hit from a neighbouring, visibly painted link.
        }
        if (!box->line_boxes.empty()) return;

        // A fixed descendant can escape an anonymous wrapper with no area.
        // Its parent need not cover the point. Only prune a subtree at a
        // clip, not merely because the cursor lies outside its border box.
        if (!inside && (box->style->overflow != Overflow::Visible || box->style->contains_paint())) return;
        // Visit overlapping siblings in the same stable z order as paint.
        auto z = [](const auto& child) {
            const auto& style = *child->style;
            return style.is_positioned() && style.has_z_index ? style.z_index : 0;
        };
        auto below = [&](const auto& a, const auto& b) { return z(a) < z(b); };
        if (std::is_sorted(box->children.begin(), box->children.end(), below)) {
            for (const auto& ch : box->children) search(ch);
        } else {
            auto ordered = box->children;
            std::stable_sort(ordered.begin(), ordered.end(), below);
            for (const auto& ch : ordered) search(ch);
        }
    };

    search(m_layout_root);
    return hit_element;
}

bool BrowserShell::click_at(float x, float y) {
    auto el = element_at(x, y);
    if (!el) return false;

    auto send_mouse = [&](const std::shared_ptr<Element>& target, const char* type, int buttons, bool click) {
        if (auto* realm = dom::realm_if_any(*target)) {
            JsIntrinsics::set_current(realm->intrinsics);
            JsScriptScope scope;
            std::vector<dom::EventField> fields{
                {"view", dom::window_of(*realm)}, {"clientX", JsValue::make_number(x)},
                {"clientY", JsValue::make_number(y)}, {"button", JsValue::make_number(0)},
                {"buttons", JsValue::make_number(buttons)}, {"detail", JsValue::make_number(1)}
            };
            if (click) {
                fields.push_back({"pointerType", JsValue::make_string("mouse")});
                fields.push_back({"pointerId", JsValue::make_number(1)});
                fields.push_back({"isPrimary", JsValue::make_boolean(true)});
            }
            auto ev = dom::make_event(*realm, click ? "PointerEvent" : "MouseEvent", type, true, true,
                                      std::move(fields), true);
            return dom::dispatch(*realm, dom::wrap(*realm, *target), ev, true);
        }
        return click ? target->click() : target->dispatch_event(std::make_shared<Event>(type, true, true));
    };
    // This entry point models one primary click at a single viewport point,
    // not a drag. A canceled press suppresses focus, not release or click.
    if (send_mouse(el, "mousedown", 1, false) && el->is_connected()) {
        if (dom::plain_editable(*el)) {
            el->focus();
            dom::select_edit_text(*el, false);
        } else {
            const auto& tag = el->tag_name();
            if (tag == "input" || tag == "textarea" || tag == "select" || tag == "button" ||
                el->has_attribute("tabindex")) el->focus();
        }
    }
    // A press listener can hide/move the target before the release. Do not
    // activate a stale link simply because it was under the initial press.
    rebuild_layout();
    auto up = element_at(x, y);
    bool canceled = true;
    if (up) {
        send_mouse(up, "mouseup", 0, false);
        if (up != el) {
            std::unordered_set<const Node*> ancestors;
            for (auto at = el->parent(); at; at = at->parent()) ancestors.insert(at.get());
            auto common = std::static_pointer_cast<Node>(up);
            while (common && !ancestors.count(common.get()) && common != el) common = common->parent();
            el = std::dynamic_pointer_cast<Element>(common);
        }
        if (el && el->is_connected()) canceled = !send_mouse(el, "click", 0, true);
    }

    EventLoop::default_loop().run_until_idle(std::chrono::milliseconds(100));

    if (!canceled && el->is_connected()) {
        std::shared_ptr<Node> curr = el;
        while (curr) {
            if (auto elem = std::dynamic_pointer_cast<Element>(curr)) {
                if (elem->tag_name() == "a" && elem->has_attribute("href")) {
                    const std::string target = elem->get_attribute("href");
                    if (!target.empty() && target[0] == '#') {
                        if (target.size() > 1 && m_document) {
                            rebuild_layout();
                            if (auto anchor = m_document->get_element_by_id(target.substr(1))) {
                                scroll_to(m_scroll_x, anchor->bounding_box().y);
                                m_status_message = "Scrolled to " + target;
                                return true;
                            }
                        }
                        scroll_to(m_scroll_x, 0.0f);
                        return true;
                    }
                    const std::string base_url = m_document
                                                     ? DocumentStyles::request_context(*m_document).base_url
                                                     : m_current_url;
                    const std::string next_url = ResourceLoader::resolve_url(base_url, target);
                    if (!next_url.empty()) {
                        return navigate_to(next_url);
                    }
                    break;
                }
            }
            curr = curr->parent();
        }
    }

    m_status_message = el ? "Clicked <" + el->tag_name() + (el->id().empty() ? "" : ("#" + el->id())) + ">" : "Released";
    rebuild_layout();
    m_dirty = true;
    return true;
}

bool BrowserShell::hover_at(float x, float y) {
    if (!m_document) return false;
    auto hit = element_at(x, y);
    bool changed = false;
    for_each_descendant(*m_document, [&](Node& n) {
        if (!n.is_element()) return true;
        auto& elem = static_cast<Element&>(n);
        bool want = false;
        for (std::shared_ptr<Node> cur = hit; cur; cur = cur->parent()) {
            if (cur.get() == &elem) {
                want = true;
                break;
            }
        }
        if (elem.is_hovered() != want) {
            elem.set_hovered(want);
            changed = true;
        }
        return true;
    });
    if (changed) {
        Node::bump_tree_version();
        rebuild_layout();
        m_dirty = true;
    }
    return changed;
}

bool BrowserShell::type_text(const std::string& text) {
    if (!m_document || text.empty()) return false;
    auto target = m_document->focused_element();
    if (!target) target = focused_element();
    if (!target) return false;
    const std::string tag = target->tag_name();
    if (dom::plain_editable(*target) && m_document->focused_element() == target) {
        if (!dom::edit_selected_text(*target, text, false)) return false;
        EventLoop::default_loop().run_until_idle(std::chrono::milliseconds(50));
        rebuild_layout();
        m_dirty = true;
        return true;
    }
    if (tag != "input" && tag != "textarea") return false;

    target->set_value(target->value() + text);
    if (dom::Realm* realm = dom::realm_if_any(*target)) {
        JsIntrinsics::set_current(realm->intrinsics);
        JsScriptScope scope;
        dom::fire(*realm, *target, "input", true);
    } else {
        target->dispatch_event(std::make_shared<Event>("input", true, false));
    }
    EventLoop::default_loop().run_until_idle(std::chrono::milliseconds(50));
    rebuild_layout();
    m_dirty = true;
    return true;
}

bool BrowserShell::handle_key(const std::string& key) {
    if (!m_document) return false;
    // A page sees an arrow before the browser performs its scrolling default.
    // Like the GUI, this entry point receives one keydown, not a key release.
    const int arrow = key == "ArrowLeft" ? 37 : key == "ArrowUp" ? 38 :
                      key == "ArrowRight" ? 39 : key == "ArrowDown" ? 40 : 0;
    if (arrow) {
        auto target = m_document->focused_element();
        if (!target) target = m_document->body();
        std::shared_ptr<Node> recipient = target ? std::static_pointer_cast<Node>(target) : m_document;
        bool allow_default = true;
        if (auto* realm = dom::realm_if_any(*recipient)) {
            JsIntrinsics::set_current(realm->intrinsics);
            JsScriptScope scope;
            const auto event = dom::make_event(*realm, "KeyboardEvent", "keydown", true, true, {
                {"view", dom::window_of(*realm)}, {"key", JsValue::make_string(key)},
                {"code", JsValue::make_string(key)}, {"keyCode", JsValue::make_number(arrow)}
            }, true);
            allow_default = dom::dispatch(*realm, dom::wrap(*realm, recipient), event, true);
        }
        EventLoop::default_loop().run_until_idle(std::chrono::milliseconds(100));
        rebuild_layout();
        if (allow_default && !m_document->focused_element()) {
            if (arrow == 38) scroll_by(0.0f, -48.0f);
            if (arrow == 40) scroll_by(0.0f, 48.0f);
        }
        m_dirty = true;
        return true;
    }
    if (key == "PageDown") { scroll_by(0.0f, m_viewport_h * 0.8f); return true; }
    if (key == "PageUp") { scroll_by(0.0f, -m_viewport_h * 0.8f); return true; }
    if (key == "Home" && !m_document->focused_element()) { scroll_to(0.0f, 0.0f); return true; }
    if (key == "End" && !m_document->focused_element()) { scroll_to(0.0f, m_content_height); return true; }
    if (key == "Tab") { focus_next(); return true; }

    auto target = m_document->focused_element();
    if (target) {
        const std::string tag = target->tag_name();
        if (dom::plain_editable(*target)) {
            if (key == "SelectAll") return dom::select_edit_text(*target, true);
            if (key == "Backspace") {
                if (!dom::edit_selected_text(*target, "", true)) return false;
                EventLoop::default_loop().run_until_idle(std::chrono::milliseconds(50));
                rebuild_layout();
                m_dirty = true;
                return true;
            }
        }
        if (key == "Backspace" && (tag == "input" || tag == "textarea")) {
            std::string val = target->value();
            if (!val.empty()) {
                while (!val.empty() && (static_cast<uint8_t>(val.back()) & 0xC0u) == 0x80u) {
                    val.pop_back();
                }
                if (!val.empty()) val.pop_back();
                target->set_value(val);
                if (dom::Realm* realm = dom::realm_if_any(*target)) {
                    JsIntrinsics::set_current(realm->intrinsics);
                    JsScriptScope scope;
                    dom::fire(*realm, *target, "input", true);
                }
                EventLoop::default_loop().run_until_idle(std::chrono::milliseconds(50));
                rebuild_layout();
                m_dirty = true;
            }
            return true;
        }
        if (key == "Enter") {
            if (tag == "textarea") {
                return type_text("\n");
            }
            if (dom::Realm* realm = dom::realm_if_any(*target)) {
                JsIntrinsics::set_current(realm->intrinsics);
                JsScriptScope scope;
                dom::fire(*realm, *target, "change", true);
            }
            target->click();
            EventLoop::default_loop().run_until_idle(std::chrono::milliseconds(50));
            rebuild_layout();
            m_dirty = true;
            return true;
        }
    }

    // DOM KeyboardEvent.key is UTF-8 here, not necessarily one byte. Keep
    // named keys out, and reject malformed sequences and control characters.
    const JsString ch(key);
    if (!key.empty() && ch.view()==key && ch.unit_at(0)>=32 && ch.unit_at(0)!=127 &&
        (ch.length()==1 || (ch.length()==2 && ch.unit_at(0)>=0xd800 && ch.unit_at(0)<=0xdbff &&
                             ch.unit_at(1)>=0xdc00 && ch.unit_at(1)<=0xdfff))) {
        return type_text(key);
    }
    return false;
}

std::vector<uint8_t> BrowserShell::render_png_frame() {
    const int w = std::clamp(static_cast<int>(std::lround(m_viewport_w)), 1, 4096);
    const int h = std::clamp(static_cast<int>(std::lround(m_viewport_h)), 1, 4096);

    BitmapRenderer renderer(w, h);
    Color canvas_bg = Color::white();
    if (m_layout_root && !m_layout_root->style->background_color.is_transparent()) {
        canvas_bg = m_layout_root->style->background_color;
    }
    renderer.clear(canvas_bg);

    DisplayList scrolled;
    if (m_scroll_x != 0.0f || m_scroll_y != 0.0f) {
        TransformMatrix2D scroll_tf = TransformMatrix2D::translation(-m_scroll_x, -m_scroll_y);
        scrolled.add(PushTransformCommand{scroll_tf});
        for (const auto& cmd : m_display_list.commands()) {
            scrolled.add(cmd);
        }
        scrolled.add(PopTransformCommand{});
        renderer.render(scrolled);
    } else {
        renderer.render(m_display_list);
    }

    // Draw subtle focus outline and text input value overlay on form controls so
    // interactive `<input>` / `<button>` elements are visually clear in `--gui`.
    if (m_document) {
        DisplayList overlay;
        for_each_descendant(*m_document, [&](Node& n) {
            if (!n.is_element()) return true;
            auto& el = static_cast<Element&>(n);
            if (!el.has_layout_box()) return true;
            Rect r = el.bounding_box();
            r.x -= m_scroll_x;
            r.y -= m_scroll_y;
            if (r.width <= 0.0f || r.height <= 0.0f || r.y + r.height < 0.0f || r.y > m_viewport_h) {
                return true;
            }
            const std::string& tag = el.tag_name();
            if (tag == "input" || tag == "textarea") {
                const bool focused = (m_document->focused_element().get() == &el);
                DrawRectCommand bg;
                bg.rect = r;
                bg.color = Color(255, 255, 255, 255);
                bg.border_radius = 4.0f;
                overlay.add(bg);

                DrawBorderCommand bdr;
                bdr.rect = r;
                bdr.width = focused ? EdgeSizes(2, 2, 2, 2) : EdgeSizes(1, 1, 1, 1);
                bdr.color = focused ? Color(37, 99, 235, 255) : Color(148, 163, 184, 255);
                bdr.border_radius = 4.0f;
                overlay.add(bdr);

                std::string shown = el.value();
                Color txt_color(15, 23, 42, 255);
                if (shown.empty() && el.has_attribute("placeholder")) {
                    shown = el.get_attribute("placeholder");
                    txt_color = Color(148, 163, 184, 255);
                }
                if (!shown.empty()) {
                    DrawTextCommand txt;
                    txt.position = Point(r.x + 8.0f, r.y + std::max(2.0f, (r.height - 18.0f) * 0.5f));
                    txt.text = shown;
                    txt.color = txt_color;
                    txt.font_size = 14.0f;
                    txt.font_weight = 400;
                    overlay.add(txt);
                }
            } else if (m_document->focused_element().get() == &el) {
                DrawBorderCommand focus_ring;
                focus_ring.rect = Rect(r.x - 2.0f, r.y - 2.0f, r.width + 4.0f, r.height + 4.0f);
                focus_ring.width = EdgeSizes(2, 2, 2, 2);
                focus_ring.color = Color(59, 130, 246, 220);
                focus_ring.border_radius = 4.0f;
                overlay.add(focus_ring);
            }
            return true;
        });
        if (!overlay.commands().empty()) {
            renderer.render(overlay);
        }
    }

    ImageData img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<std::size_t>(w * h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Color c = renderer.get_pixel(x, y);
            img.pixels[static_cast<std::size_t>(y * w + x)] =
                (static_cast<uint32_t>(c.a) << 24) |
                (static_cast<uint32_t>(c.r) << 16) |
                (static_cast<uint32_t>(c.g) << 8) |
                static_cast<uint32_t>(c.b);
        }
    }
    return ImageDecoder::encode_png(img);
}

void BrowserShell::scroll_to(float x, float y) {
    m_scroll_x = std::max(0.0f, x);
    const float visible_h = std::min(m_viewport_h, static_cast<float>(std::max(1, m_term_rows - 3)) * 20.0f);
    float max_y = std::max(0.0f, m_content_height - visible_h);
    m_scroll_y = std::clamp(y, 0.0f, max_y);
    m_dirty = true;
}

void BrowserShell::scroll_by(float dx, float dy) {
    scroll_to(m_scroll_x + dx, m_scroll_y + dy);
}

void BrowserShell::scroll_up(float lines) {
    scroll_by(0.0f, -lines * 20.0f);
}

void BrowserShell::scroll_down(float lines) {
    scroll_by(0.0f, lines * 20.0f);
}

void BrowserShell::scroll_page_up() {
    scroll_by(0.0f, -static_cast<float>(m_term_rows - 4) * 20.0f);
}

void BrowserShell::scroll_page_down() {
    scroll_by(0.0f, static_cast<float>(m_term_rows - 4) * 20.0f);
}

size_t BrowserShell::tick(std::chrono::steady_clock::time_point now) {
    size_t executed = EventLoop::default_loop().tick(now);
    if (executed > 0) {
        rebuild_layout();
        m_dirty = true;
    }
    return executed;
}

std::string BrowserShell::render_terminal_frame(bool devtools) {
    std::ostringstream out;

    // 1. Navigation Header Bar (Line 0)
    out << "\033[48;2;30;41;59m\033[38;2;255;255;255m"; // Dark slate background, white text
    out << (can_go_back() ? "\033[1;32m[◄ Back]\033[0;48;2;30;41;59m\033[38;2;255;255;255m " : "\033[38;2;100;116;139m[◄ Back]\033[38;2;255;255;255m ");
    out << (can_go_forward() ? "\033[1;32m[► Fwd]\033[0;48;2;30;41;59m\033[38;2;255;255;255m " : "\033[38;2;100;116;139m[► Fwd]\033[38;2;255;255;255m ");
    out << "\033[1;34m[⟳ Reload]\033[0;48;2;30;41;59m\033[38;2;255;255;255m ";
    out << "URL: [\033[1m" << (m_current_url.empty() ? "about:blank" : m_current_url) << "\033[22m] ";
    out << "\033[0m\n";

    // 2. Viewport Content
    int content_rows = devtools ? std::max(6, m_term_rows - 8) : std::max(6, m_term_rows - 3);
    TerminalRenderer renderer(m_term_cols, content_rows, 10.0f, 20.0f);
    renderer.set_scroll(m_scroll_x, m_scroll_y);

    // If an element is focused, draw active highlight ring
    if (m_focused_index >= 0 && m_focused_index < static_cast<int>(m_interactive_elements.size())) {
        renderer.draw_focus_ring(m_interactive_elements[m_focused_index].bounds, Color(245, 158, 11)); // Amber
    }

    out << renderer.to_ansi_string(m_display_list);

    // 3. Status Bar
    int scroll_pct = (m_content_height > 0.0f) ?
        static_cast<int>((m_scroll_y / std::max(1.0f, m_content_height - content_rows * 20.0f)) * 100.0f) : 0;
    scroll_pct = std::clamp(scroll_pct, 0, 100);

    out << "\033[48;2;15;23;42m\033[38;2;148;163;184m"; // Dark slate background, light text
    out << "Scroll: " << std::setw(3) << scroll_pct << "% | ";
    out << "Links/Btns: " << m_interactive_elements.size() << " | ";
    if (m_focused_index >= 0 && m_focused_index < static_cast<int>(m_interactive_elements.size())) {
        const auto& item = m_interactive_elements[m_focused_index];
        out << "Focus: <" << item.tag;
        if (!item.element->id().empty()) out << "#" << item.element->id();
        out << "> | ";
    }
    out << m_status_message;
    out << "\033[0m\n";

    // 4. Shortcut Help Bar
    out << "\033[48;2;2;6;23m\033[38;2;226;232;240m";
    out << "[Tab/S-Tab] Focus  [Enter] Click/Go  [↑/↓/PgUp/PgDn] Scroll  [o] Open  [b] Back  [d] DevTools  [q] Quit";
    out << "\033[0m\n";

    // 5. Optional DevTools Pane
    if (devtools) {
        // Rendering the DevTools pane used to collect with an EMPTY root set,
        // which freed every JsObject the page could still reach and left the
        // interpreter holding dangling pointers (reproduced as an ASan
        // heap-use-after-free). The collector now pulls roots from registered
        // providers, so this is safe -- and `stats` is the sane thing to
        // report, since collect_garbage() already returns them.
        auto heap_stats = Heap::default_heap().collect_garbage();
        (void)heap_stats;
        out << "\033[48;2;30;41;59m\033[38;2;251;191;36m";
        out << "--- [Litebird DevTools] ---\n";
        out << "DOM Elements: " << (m_document ? m_document->get_elements_by_tag_name("*").size() : 0)
            << " | Layout Boxes: " << (m_layout_root ? 1 + m_layout_root->children.size() : 0)
            << " | Content Height: " << m_content_height << "px\n";
        out << "Heap GC Cells: " << heap_stats.alive_cells << "/" << heap_stats.total_cells
            << " | Memory: " << heap_stats.bytes_allocated << " B"
            << " | Collections: " << heap_stats.collections_count << "\n";
        out << "\033[0m";
    }

    return out.str();
}

void BrowserShell::print_terminal_frame(std::ostream& os, bool devtools) {
    os << render_terminal_frame(devtools) << std::flush;
}

void BrowserShell::run_interactive() {
    RawTerminalMode raw_mode;
    if (!raw_mode.enabled) {
        std::cerr << "Warning: Terminal does not support raw mode; printing static frame\n";
        print_terminal_frame(std::cout, false);
        return;
    }

    std::cout << "\033[2J\033[H\033[?25l" << std::flush; // Clear screen, hide cursor

    bool running = true;
    bool devtools = false;
    m_dirty = true;

    while (running) {
        // Run timers
        tick();

        if (m_dirty) {
            std::cout << "\033[H" << std::flush; // Move cursor to top-left
            print_terminal_frame(std::cout, devtools);
            m_dirty = false;
        }

        // Wait for keyboard input with 50ms timeout
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000; // 50ms

        int ret = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            char ch = 0;
            if (read(STDIN_FILENO, &ch, 1) > 0) {
                if (ch == 'q' || ch == 3) { // 'q' or Ctrl+C
                    running = false;
                } else if (ch == '\t') { // Tab
                    focus_next();
                } else if (ch == '\r' || ch == '\n') { // Enter
                    activate_focused();
                } else if (ch == 'k') {
                    scroll_up(2.0f);
                } else if (ch == 'j') {
                    scroll_down(2.0f);
                } else if (ch == 'b') {
                    back();
                } else if (ch == 'f') {
                    forward();
                } else if (ch == 'r') {
                    reload();
                } else if (ch == 'd') {
                    devtools = !devtools;
                    m_dirty = true;
                } else if (ch == 'o' || ch == 'g') {
                    // Open URL prompt
                    std::cout << "\033[?25h\033[H\033[K" << std::flush; // Show cursor
                    std::cout << "\033[48;2;37;99;235m\033[38;2;255;255;255m Enter URL or File: \033[0m " << std::flush;
                    // Read line in canonical mode
                    struct termios canon = raw_mode.orig_termios;
                    tcsetattr(STDIN_FILENO, TCSANOW, &canon);
                    std::string input_url;
                    std::getline(std::cin, input_url);
                    // Restore raw mode
                    struct termios raw = raw_mode.orig_termios;
                    raw.c_lflag &= ~(ECHO | ICANON);
                    raw.c_cc[VMIN] = 0;
                    raw.c_cc[VTIME] = 1;
                    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
                    std::cout << "\033[?25l" << std::flush;

                    if (!input_url.empty()) {
                        navigate_to(input_url);
                    }
                } else if (ch == '\033') { // Escape sequence
                    char seq[3] = {0};
                    if (read(STDIN_FILENO, &seq[0], 1) > 0 && read(STDIN_FILENO, &seq[1], 1) > 0) {
                        if (seq[0] == '[') {
                            if (seq[1] == 'A') { // Up arrow
                                scroll_up(2.0f);
                            } else if (seq[1] == 'B') { // Down arrow
                                scroll_down(2.0f);
                            } else if (seq[1] == 'C') { // Right arrow
                                forward();
                            } else if (seq[1] == 'D') { // Left arrow
                                back();
                            } else if (seq[1] == 'Z') { // Shift+Tab
                                focus_prev();
                            } else if (seq[1] == '5') { // Page Up (~ followed)
                                char t; (void)read(STDIN_FILENO, &t, 1);
                                scroll_page_up();
                            } else if (seq[1] == '6') { // Page Down (~ followed)
                                char t; (void)read(STDIN_FILENO, &t, 1);
                                scroll_page_down();
                            }
                        }
                    }
                }
            }
        }
    }

    std::cout << "\033[2J\033[H\033[?25h" << std::flush; // Clear and restore cursor
}

} // namespace litebird
