#include "litebird/dom/ShadowRoot.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/html/HtmlParser.hpp"

namespace litebird {

ShadowRoot::ShadowRoot(std::shared_ptr<Element> host, ShadowRootMode mode)
    : Node(NodeType::DocumentFragment), m_host(std::move(host)), m_mode(mode) {}

std::string ShadowRoot::inner_html() const {
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

void ShadowRoot::set_inner_html(const std::string& html) {
    auto children_copy = m_children;
    for (const auto& ch : children_copy) {
        remove_child(ch);
    }
    m_stylesheets.clear();
    m_parsed_stylesheets.clear();

    auto parsed = HtmlParser::parse_fragment(html, &m_stylesheets);
    for (const auto& ch : parsed) {
        append_child(ch);
    }

    for (const auto& css : m_stylesheets) {
        m_parsed_stylesheets.push_back(CssParser::parse(css));
    }

    if (auto h = host()) {
        h->mark_needs_layout();
    }
}

void ShadowRoot::add_stylesheet(std::string css) {
    m_parsed_stylesheets.push_back(CssParser::parse(css));
    m_stylesheets.push_back(std::move(css));
}

static std::shared_ptr<Element> find_by_id_recursive(const std::shared_ptr<Node>& node, const std::string& id) {
    if (auto el = std::dynamic_pointer_cast<Element>(node)) {
        if (el->id() == id) return el;
    }
    for (const auto& ch : node->children()) {
        auto found = find_by_id_recursive(ch, id);
        if (found) return found;
    }
    return nullptr;
}

std::shared_ptr<Element> ShadowRoot::get_element_by_id(const std::string& id) const {
    for (const auto& ch : m_children) {
        auto found = find_by_id_recursive(ch, id);
        if (found) return found;
    }
    return nullptr;
}

static void query_selector_all_recursive(const std::shared_ptr<Node>& node, const std::string& sel, std::vector<std::shared_ptr<Element>>& out) {
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
        query_selector_all_recursive(ch, sel, out);
    }
}

std::vector<std::shared_ptr<Element>> ShadowRoot::query_selector_all(const std::string& selector) const {
    std::vector<std::shared_ptr<Element>> res;
    for (const auto& ch : m_children) {
        query_selector_all_recursive(ch, selector, res);
    }
    return res;
}

std::shared_ptr<Element> ShadowRoot::query_selector(const std::string& selector) const {
    auto all = query_selector_all(selector);
    return all.empty() ? nullptr : all.front();
}

void ShadowRoot::dump(std::ostream& os, int indent) const {
    std::string spaces(indent * 2, ' ');
    os << spaces << "#shadow-root (" << mode_str() << ")\n";
    for (const auto& ch : m_children) {
        ch->dump(os, indent + 1);
    }
}

std::vector<std::shared_ptr<Node>> ShadowRoot::assigned_nodes(const std::shared_ptr<Element>& slot_el) {
    std::vector<std::shared_ptr<Node>> result;
    if (!slot_el || slot_el->tag_name() != "slot") return result;

    // Find host element by traversing up to ShadowRoot
    std::shared_ptr<Element> host_element = nullptr;
    auto curr = slot_el->parent();
    while (curr) {
        if (curr->is_document_fragment()) {
            if (auto sr = dynamic_cast<ShadowRoot*>(curr.get())) {
                host_element = sr->host();
                break;
            }
        }
        curr = curr->parent();
    }

    if (!host_element) return result;

    std::string slot_name = slot_el->get_attribute("name");

    for (const auto& light_ch : host_element->children()) {
        if (light_ch->is_element()) {
            auto light_el = std::static_pointer_cast<Element>(light_ch);
            std::string target_slot = light_el->get_attribute("slot");
            if (target_slot == slot_name) {
                result.push_back(light_ch);
            }
        } else if (slot_name.empty()) {
            // Default slot matches unassigned non-elements
            result.push_back(light_ch);
        }
    }

    return result;
}

std::vector<std::shared_ptr<Element>> ShadowRoot::assigned_elements(const std::shared_ptr<Element>& slot_el) {
    auto nodes = assigned_nodes(slot_el);
    std::vector<std::shared_ptr<Element>> elements;
    for (const auto& n : nodes) {
        if (n->is_element()) {
            elements.push_back(std::static_pointer_cast<Element>(n));
        }
    }
    return elements;
}

} // namespace litebird
