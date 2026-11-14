#pragma once

#include "litebird/dom/Node.hpp"
#include "litebird/dom/AttributeList.hpp"
#include "litebird/common/Geometry.hpp"
#include "litebird/js/JsValue.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <algorithm>

namespace litebird {

class CanvasRenderingContext2D;
class ShadowRoot;
struct FormControlState;

/// Where an element stands with custom elements.
enum class CustomElementState : uint8_t {
    /// An ordinary element, or one whose name could be a custom element's and
    /// has no definition yet (see Element::is_defined).
    Uncustomized,
    /// Its constructor has run: it is an instance of the definition's class.
    Custom,
    /// Its constructor threw. It stays a plain element.
    Failed
};

/// Namespace of an element, per the WHATWG DOM standard.
enum class ElementNamespace {
    HTML,
    SVG,
    MathML,
    /// Any other namespace, or none at all. The HTML parser never makes such
    /// an element; the XML parser and `createElementNS` do. Its URI is
    /// other_namespace_uri().
    Other
};

class Element : public Node {
public:
    explicit Element(std::string tag_name)
        : Node(NodeType::Element), m_tag_name(std::move(tag_name)) {
        std::transform(m_tag_name.begin(), m_tag_name.end(), m_tag_name.begin(), ::tolower);
    }

    /// Constructs an element in an explicit namespace. Foreign (SVG/MathML)
    /// element names are case-adjusting, so they are stored verbatim.
    Element(std::string tag_name, ElementNamespace ns)
        : Node(NodeType::Element), m_tag_name(std::move(tag_name)), m_namespace(ns) {
        if (ns == ElementNamespace::HTML) {
            std::transform(m_tag_name.begin(), m_tag_name.end(), m_tag_name.begin(), ::tolower);
        }
    }

    const std::string& tag_name() const { return m_tag_name; }
    /// Replaces the name as given. The constructors lower-case an HTML name,
    /// which is what markup means; `createElementNS` keeps the case, and an
    /// HTML element named "DIV" is not a div.
    void set_tag_name_verbatim(std::string name) { m_tag_name = std::move(name); }

    /// The namespace prefix the element was created with
    /// (`createElementNS(svg, 'svg:rect')`); empty for none.
    const std::string& prefix() const { return m_rare ? m_rare->prefix : empty_string(); }
    void set_prefix(std::string prefix) {
        if (prefix.empty() && !m_rare) return;
        rare().prefix = std::move(prefix);
    }
    /// The URI of an element in ElementNamespace::Other; empty for an
    /// element in no namespace.
    const std::string& other_namespace_uri() const { return m_rare ? m_rare->other_namespace : empty_string(); }
    void set_other_namespace_uri(std::string uri) {
        if (uri.empty() && !m_rare) return;
        rare().other_namespace = std::move(uri);
    }

    ElementNamespace element_namespace() const { return m_namespace; }
    bool is_foreign() const { return m_namespace != ElementNamespace::HTML; }

    /// Template contents (a DocumentFragment), per the HTML template element rules.
    const std::shared_ptr<Node>& template_contents() const { return m_template_contents; }
    void set_template_contents(std::shared_ptr<Node> frag) { m_template_contents = std::move(frag); }

    const Rect& bounding_box() const { return m_bounding_box; }
    void set_bounding_box(const Rect& r) { m_bounding_box = r; }

    bool has_attribute(const std::string& name) const {
        return m_attributes.find(name) != m_attributes.end();
    }

    std::string get_attribute(const std::string& name) const {
        auto it = m_attributes.find(name);
        return it != m_attributes.end() ? it->second : "";
    }

    /// The value of `name`, or null when the element has no such attribute.
    const std::string* attribute(const std::string& name) const {
        auto it = m_attributes.find(name);
        return it != m_attributes.end() ? &it->second : nullptr;
    }

    void set_attribute(const std::string& name, std::string value);
    void remove_attribute(const std::string& name);

    // Attributes are kept under their qualified names ("xlink:href"). The
    // few that have a namespace have it recorded next to the list: what the
    // XML parser read with a prefix, and what setAttributeNS was given. (So
    // an element cannot hold two attributes whose qualified names are the
    // same and whose namespaces differ; only a script can ask for that.)
    /// The namespace of the attribute stored as `qualified_name`; empty for
    /// an attribute in no namespace.
    const std::string& attribute_namespace(const std::string& qualified_name) const;
    /// Sets an attribute and its namespace. An empty namespace is none.
    void set_attribute_ns(const std::string& namespace_uri, const std::string& qualified_name, std::string value);
    /// The stored name of the attribute with this namespace and local name;
    /// null when there is none.
    const std::string* find_attribute_ns(const std::string& namespace_uri, const std::string& local_name) const;
    /// Copies the namespaces of `other`'s attributes: for a clone.
    void copy_attribute_namespaces(const Element& other) {
        if (other.m_rare && !other.m_rare->attribute_namespaces.empty()) {
            rare().attribute_namespaces = other.m_rare->attribute_namespaces;
        }
    }

    /// In the order they were added; see AttributeList.hpp.
    const AttributeList& attributes() const {
        return m_attributes;
    }

    std::string inner_html() const;
    void set_inner_html(const std::string& html);
    std::string outer_html() const;

    std::shared_ptr<Element> query_selector(const std::string& selector) const;
    std::vector<std::shared_ptr<Element>> query_selector_all(const std::string& selector) const;

    // Shadow DOM
    std::shared_ptr<ShadowRoot> shadow_root() const { return m_shadow_root; }
    std::shared_ptr<ShadowRoot> attach_shadow(const std::string& mode);

    // Custom elements. The instance of the custom element's class is the
    // element's own script object, so there is nothing else to keep here.
    CustomElementState custom_state() const { return m_custom_state; }
    void set_custom_state(CustomElementState state) { m_custom_state = state; }
    /// For a customized built-in element (`<button is="fancy-button">`), the
    /// name of the definition it is an instance of or waits for. It is fixed
    /// when the element is created: by `createElement(tag, {is})`, or from
    /// the `is` attribute the element was parsed with.
    const std::string& is_value() const {
        if (m_rare && !m_rare->is_value.empty()) return m_rare->is_value;
        auto attribute = m_attributes.find("is");
        return attribute == m_attributes.end() ? empty_string() : attribute->second;
    }
    void set_is_value(std::string value) {
        if (value.empty() && !m_rare) return;
        rare().is_value = std::move(value);
    }
    /// What `:defined` asks: false only for an element whose name is a valid
    /// custom element name and which has not been upgraded.
    bool is_defined() const;

    /// The state of a form control; see FormState.hpp. Null until needed.
    FormControlState* form_state() const { return m_form_state.get(); }
    FormControlState& ensure_form_state();

    std::string id() const {
        return get_attribute("id");
    }

    std::vector<std::string> classes() const {
        std::vector<std::string> result;
        std::string class_attr = get_attribute("class");
        if (class_attr.empty()) return result;

        std::stringstream ss(class_attr);
        std::string item;
        while (ss >> item) {
            result.push_back(item);
        }
        return result;
    }

    bool has_class(const std::string& class_name) const {
        // Hot path: the cascade calls this once per class selector per element.
        // The old version went through classes(), which built a std::stringstream,
        // allocated a std::string per token and a vector to hold them -- roughly
        // a microsecond for what is a substring scan. Tokenise in place instead.
        auto it = m_attributes.find("class");
        if (it == m_attributes.end()) return false;
        const std::string& attr = it->second;
        if (class_name.empty()) return false;

        size_t pos = 0;
        while (pos < attr.size()) {
            while (pos < attr.size() && is_class_separator(attr[pos])) ++pos;
            size_t start = pos;
            while (pos < attr.size() && !is_class_separator(attr[pos])) ++pos;
            if (pos > start && (pos - start) == class_name.size() &&
                attr.compare(start, pos - start, class_name) == 0) {
                return true;
            }
        }
        return false;
    }

    // Form controls. A control has a state of its own next to its
    // attributes: `value` and `checked` here are what the control holds now
    // (what the user typed, what a script set), which is what a script reads
    // and what `:checked` matches. The attributes give the defaults. The
    // rules are in FormState.hpp; these are shorthands for them.
    std::string value() const;
    void set_value(std::string val);
    bool checked() const;
    void set_checked(bool c);

    /// The control cannot be used: its own `disabled` attribute, or a
    /// disabled fieldset around it.
    bool disabled() const;

    void set_disabled(bool d) {
        if (d) set_attribute("disabled", "");
        else remove_attribute("disabled");
    }

    std::string input_type() const {
        std::string t = get_attribute("type");
        return t.empty() ? "text" : t;
    }

    /// With script bindings attached these do what the script methods of the
    /// same names do (only a focusable element in a document takes focus; the
    /// element that had it is blurred; focusin and focusout are fired).
    /// Without them: the flag, and one event.
    void focus();
    void blur();

    bool is_hovered() const { return m_is_hovered; }
    void set_hovered(bool h) { m_is_hovered = h; }

    bool is_focused() const { return m_is_focused; }
    void set_focused(bool f) { m_is_focused = f; }

    bool is_active() const { return m_is_active; }
    void set_active(bool a) { m_is_active = a; }

    /// With script bindings attached this is `element.click()`: the listeners
    /// run and then what the click does (a checkbox toggles, a submit button
    /// submits). Without them: one click event.
    bool click();

    std::shared_ptr<CanvasRenderingContext2D> canvas_context() const { return m_canvas_context; }
    void set_canvas_context(std::shared_ptr<CanvasRenderingContext2D> ctx) { m_canvas_context = std::move(ctx); }
    std::shared_ptr<CanvasRenderingContext2D> get_or_create_canvas_context_2d();

protected:
    void dump_open(std::ostream& os, const std::string& indent, bool has_content) const override;
    void dump_close(std::ostream& os, const std::string& indent) const override;

private:
    /// HTML treats class as a space-separated token list; only ASCII whitespace
    /// separates tokens (a non-breaking space is a legitimate class character).
    static bool is_class_separator(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
    }

public:
    std::string m_tag_name;
    ElementNamespace m_namespace{ElementNamespace::HTML};
    std::shared_ptr<Node> m_template_contents;
    AttributeList m_attributes;
    std::shared_ptr<CanvasRenderingContext2D> m_canvas_context;
    std::shared_ptr<ShadowRoot> m_shadow_root;
    CustomElementState m_custom_state{CustomElementState::Uncustomized};
    /// What almost no element has, kept out of the element itself: a page of
    /// 800,000 elements should not pay for three empty strings each.
    struct Rare {
        std::string is_value;
        std::string prefix;
        std::string other_namespace;
        /// Qualified name and namespace of each attribute that has one.
        AttributeNamespaces attribute_namespaces;
    };
    std::unique_ptr<Rare> m_rare;
    Rare& rare() {
        if (!m_rare) m_rare = std::make_unique<Rare>();
        return *m_rare;
    }
    static const std::string& empty_string() {
        static const std::string empty;
        return empty;
    }
    std::shared_ptr<FormControlState> m_form_state;
    Rect m_bounding_box;
    bool m_is_hovered{false};
    bool m_is_focused{false};
    bool m_is_active{false};
};

} // namespace litebird
