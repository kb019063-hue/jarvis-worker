#include "litebird/dom/Node.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/dom/MutationObserver.hpp"
#include "litebird/dom/CustomElementRegistry.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include "litebird/html/HtmlParser.hpp"
#include "litebird/render/Canvas2D.hpp"
#include <iomanip>
#include <unordered_set>

namespace litebird {

std::shared_ptr<Node> Node::previous_sibling() const {
    auto p = parent();
    if (!p) return nullptr;
    const auto& ch = p->children();
    for (size_t i = 1; i < ch.size(); ++i) {
        if (ch[i].get() == this) return ch[i - 1];
    }
    return nullptr;
}

std::shared_ptr<Node> Node::next_sibling() const {
    auto p = parent();
    if (!p) return nullptr;
    const auto& ch = p->children();
    for (size_t i = 0; i + 1 < ch.size(); ++i) {
        if (ch[i].get() == this) return ch[i + 1];
    }
    return nullptr;
}

bool Node::is_connected() const {
    if (is_document()) return true;
    auto curr = parent();
    while (curr) {
        if (curr->is_document()) return true;
        if (curr->is_document_fragment()) {
            if (auto sr = dynamic_cast<const ShadowRoot*>(curr.get())) {
                auto host = sr->host();
                if (!host) return false;
                curr = host;
                continue;
            }
        }
        curr = curr->parent();
    }
    return false;
}

static void trigger_connected_recursive(const std::shared_ptr<Node>& node) {
    if (!node) return;
    if (node->is_element()) {
        auto el = std::static_pointer_cast<Element>(node);
        CustomElementRegistry::default_registry().notify_connected(el);
    }
    for (const auto& ch : node->children()) {
        trigger_connected_recursive(ch);
    }
}

static void trigger_disconnected_recursive(const std::shared_ptr<Node>& node) {
    if (!node) return;
    if (node->is_element()) {
        auto el = std::static_pointer_cast<Element>(node);
        CustomElementRegistry::default_registry().notify_disconnected(el);
    }
    for (const auto& ch : node->children()) {
        trigger_disconnected_recursive(ch);
    }
}

void Node::append_child(std::shared_ptr<Node> child) {
    if (!child) return;

    if (child->is_document_fragment()) {
        auto frag_children = child->children();
        for (const auto& ch : frag_children) {
            append_child(ch);
        }
        child->children().clear();
        return;
    }

    if (auto old_p = child->parent()) {
        old_p->remove_child(child);
    }

    auto prev = m_children.empty() ? nullptr : m_children.back();
    child->set_parent(shared_from_this());
    m_children.push_back(child);
    mark_needs_layout();

    MutationObserver::notify_child_list(shared_from_this(), {child}, {}, prev, nullptr);

    if (is_connected()) {
        trigger_connected_recursive(child);
    }
}

void Node::insert_before(std::shared_ptr<Node> new_child, std::shared_ptr<Node> ref_child) {
    if (!new_child) return;
    if (!ref_child) {
        append_child(new_child);
        return;
    }

    if (new_child->is_document_fragment()) {
        auto frag_children = new_child->children();
        for (const auto& ch : frag_children) {
            insert_before(ch, ref_child);
        }
        new_child->children().clear();
        return;
    }

    if (auto old_p = new_child->parent()) {
        old_p->remove_child(new_child);
    }

    auto it = std::find(m_children.begin(), m_children.end(), ref_child);
    if (it == m_children.end()) {
        append_child(new_child);
        return;
    }

    auto prev = (it == m_children.begin()) ? nullptr : *(it - 1);
    new_child->set_parent(shared_from_this());
    m_children.insert(it, new_child);
    mark_needs_layout();

    MutationObserver::notify_child_list(shared_from_this(), {new_child}, {}, prev, ref_child);

    if (is_connected()) {
        trigger_connected_recursive(new_child);
    }
}

void Node::remove_child(std::shared_ptr<Node> child) {
    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        auto prev = (it == m_children.begin()) ? nullptr : *(it - 1);
        auto next = (it + 1 == m_children.end()) ? nullptr : *(it + 1);
        auto removed = *it;
        removed->set_parent(nullptr);
        m_children.erase(it);
        mark_needs_layout();

        MutationObserver::notify_child_list(shared_from_this(), {}, {removed}, prev, next);

        trigger_disconnected_recursive(removed);
    }
}

void Node::add_event_listener(const std::string& type, EventListener listener, bool use_capture) {
    m_listeners[type].push_back({std::move(listener), use_capture});
}

void Node::mark_needs_layout() {
    m_needs_layout = true;
    auto curr = parent();
    while (curr) {
        if (curr->m_child_needs_layout) break;
        curr->m_child_needs_layout = true;
        curr = curr->parent();
    }
}

void Node::remove_event_listeners(const std::string& type) {
    m_listeners.erase(type);
}

bool Node::dispatch_event(const std::shared_ptr<Event>& event) {
    if (!event) return false;

    event->set_target(shared_from_this());

    // Build ancestor list from root down to target's parent
    std::vector<std::shared_ptr<Node>> ancestors;
    auto curr = parent();
    while (curr) {
        ancestors.push_back(curr);
        curr = curr->parent();
    }
    std::reverse(ancestors.begin(), ancestors.end());

    // Phase 1: Capturing Phase (downwards from document to parent)
    event->set_event_phase(EventPhase::Capturing);
    for (const auto& anc : ancestors) {
        event->set_current_target(anc);
        auto it = anc->m_listeners.find(event->type());
        if (it != anc->m_listeners.end()) {
            auto copy = it->second;
            for (const auto& entry : copy) {
                if (entry.use_capture) {
                    entry.listener(event);
                    if (event->propagation_stopped()) return !event->default_prevented();
                }
            }
        }
    }

    // Phase 2: Target Phase (at target)
    event->set_event_phase(EventPhase::AtTarget);
    event->set_current_target(shared_from_this());
    auto it = m_listeners.find(event->type());
    if (it != m_listeners.end()) {
        auto copy = it->second;
        for (const auto& entry : copy) {
            entry.listener(event);
            if (event->propagation_stopped()) return !event->default_prevented();
        }
    }

    // Phase 3: Bubbling Phase (upwards from parent to document)
    if (event->bubbles()) {
        event->set_event_phase(EventPhase::Bubbling);
        for (auto r_it = ancestors.rbegin(); r_it != ancestors.rend(); ++r_it) {
            const auto& anc = *r_it;
            event->set_current_target(anc);
            auto p_it = anc->m_listeners.find(event->type());
            if (p_it != anc->m_listeners.end()) {
                auto p_copy = p_it->second;
                for (const auto& entry : p_copy) {
                    if (!entry.use_capture) {
                        entry.listener(event);
                        if (event->propagation_stopped()) return !event->default_prevented();
                    }
                }
            }
        }
    }

    return !event->default_prevented();
}

// Appends into one buffer instead of returning a fresh string per subtree.
// The old form was O(n^2) in depth: every level copied all of its descendants'
// text again, so reading textContent on a deeply nested document copied the
// whole document once per level.
static void append_text_content(const Node& node, std::string& out) {
    if (node.type() == NodeType::Text) {
        if (const auto* text = node.as_text()) {
            out += text->text();
        }
        return;
    }
    for (const auto& ch : node.children()) {
        if (ch) append_text_content(*ch, out);
    }
}

std::string Node::text_content() const {
    std::string res;
    append_text_content(*this, res);
    return res;
}

void Node::dump(std::ostream& os, int indent) const {
    std::string spaces(indent * 2, ' ');
    os << spaces << "[Node type=" << static_cast<int>(m_type) << "]\n";
    for (const auto& ch : m_children) {
        ch->dump(os, indent + 1);
    }
}

Element* Node::as_element() {
    return m_type == NodeType::Element ? static_cast<Element*>(this) : nullptr;
}

const Element* Node::as_element() const {
    return m_type == NodeType::Element ? static_cast<const Element*>(this) : nullptr;
}

Text* Node::as_text() {
    return m_type == NodeType::Text ? static_cast<Text*>(this) : nullptr;
}

const Text* Node::as_text() const {
    return m_type == NodeType::Text ? static_cast<const Text*>(this) : nullptr;
}

Document* Node::as_document() {
    return m_type == NodeType::Document ? static_cast<Document*>(this) : nullptr;
}

const Document* Node::as_document() const {
    return m_type == NodeType::Document ? static_cast<const Document*>(this) : nullptr;
}

void Element::set_attribute(const std::string& name, std::string value) {
    std::string old_val;
    auto it = m_attributes.find(name);
    if (it != m_attributes.end()) old_val = it->second;

    m_attributes[name] = value;
    mark_needs_layout();

    MutationObserver::notify_attributes(std::static_pointer_cast<Element>(shared_from_this()), name, old_val);
    CustomElementRegistry::default_registry().notify_attribute_changed(
        std::static_pointer_cast<Element>(shared_from_this()), name, old_val, value
    );
}

void Element::remove_attribute(const std::string& name) {
    auto it = m_attributes.find(name);
    if (it == m_attributes.end()) return;
    std::string old_val = it->second;
    m_attributes.erase(it);
    mark_needs_layout();

    MutationObserver::notify_attributes(std::static_pointer_cast<Element>(shared_from_this()), name, old_val);
    CustomElementRegistry::default_registry().notify_attribute_changed(
        std::static_pointer_cast<Element>(shared_from_this()), name, old_val, ""
    );
}

std::string Element::inner_html() const {
    std::string res;
    for (const auto& ch : m_children) {
        if (ch->is_element()) {
            res += ch->as_element()->outer_html();
        } else if (ch->is_text()) {
            res += ch->as_text()->text();
        }
    }
    return res;
}

std::string Element::outer_html() const {
    std::string res = "<" + m_tag_name;
    for (const auto& [k, v] : m_attributes) {
        res += " " + k + "=\"" + v + "\"";
    }
    static const std::unordered_set<std::string> VOID_TAGS = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr"
    };
    if (VOID_TAGS.count(m_tag_name)) {
        res += " />";
        return res;
    }
    res += ">";
    res += inner_html();
    res += "</" + m_tag_name + ">";
    return res;
}

static void element_query_selector_all_recursive(const std::shared_ptr<Node>& node, const std::string& sel, std::vector<std::shared_ptr<Element>>& out) {
    if (auto el = std::dynamic_pointer_cast<Element>(node)) {
        bool match = false;
        if (sel.starts_with("#")) {
            match = (el->id() == sel.substr(1));
        } else if (sel.starts_with(".")) {
            match = el->has_class(sel.substr(1));
        } else {
            match = (sel == "*" || el->tag_name() == sel);
        }
        if (match) out.push_back(el);
    }
    for (const auto& ch : node->children()) {
        element_query_selector_all_recursive(ch, sel, out);
    }
}

std::vector<std::shared_ptr<Element>> Element::query_selector_all(const std::string& selector) const {
    std::vector<std::shared_ptr<Element>> res;
    for (const auto& ch : m_children) {
        element_query_selector_all_recursive(ch, selector, res);
    }
    return res;
}

std::shared_ptr<Element> Element::query_selector(const std::string& selector) const {
    auto all = query_selector_all(selector);
    return all.empty() ? nullptr : all.front();
}

void Element::set_inner_html(const std::string& html) {
    auto children_copy = m_children;
    for (const auto& ch : children_copy) {
        remove_child(ch);
    }
    auto parsed = HtmlParser::parse_fragment(html);
    for (const auto& ch : parsed) {
        append_child(ch);
    }
    mark_needs_layout();
}

std::shared_ptr<ShadowRoot> Element::attach_shadow(const std::string& mode_str) {
    if (m_shadow_root) return m_shadow_root;
    ShadowRootMode m = (mode_str == "closed") ? ShadowRootMode::Closed : ShadowRootMode::Open;
    m_shadow_root = std::make_shared<ShadowRoot>(std::static_pointer_cast<Element>(shared_from_this()), m);
    mark_needs_layout();
    return m_shadow_root;
}

void Element::dump(std::ostream& os, int indent) const {
    std::string spaces(indent * 2, ' ');
    os << spaces << "<" << m_tag_name;
    for (const auto& [k, v] : m_attributes) {
        os << " " << k << "=\"" << v << "\"";
    }
    if (m_children.empty() && !m_shadow_root) {
        os << " />\n";
    } else {
        os << ">\n";
        if (m_shadow_root) {
            m_shadow_root->dump(os, indent + 1);
        }
        for (const auto& ch : m_children) {
            ch->dump(os, indent + 1);
        }
        os << spaces << "</" << m_tag_name << ">\n";
    }
}

void Text::dump(std::ostream& os, int indent) const {
    std::string spaces(indent * 2, ' ');
    os << spaces << "#text: \"";
    for (char c : m_text) {
        if (c == '\n') os << "\\n";
        else if (c == '\t') os << "\\t";
        else os << c;
    }
    os << "\"\n";
}

std::shared_ptr<CanvasRenderingContext2D> Element::get_or_create_canvas_context_2d() {
    if (!m_canvas_context) {
        int w = 300;
        int h = 150;
        if (has_attribute("width")) {
            try { w = std::max(1, std::stoi(get_attribute("width"))); } catch (...) {}
        }
        if (has_attribute("height")) {
            try { h = std::max(1, std::stoi(get_attribute("height"))); } catch (...) {}
        }
        m_canvas_context = std::make_shared<CanvasRenderingContext2D>(w, h);
    }
    return m_canvas_context;
}

} // namespace litebird
