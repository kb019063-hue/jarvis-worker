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
    /// Its constructor threw while `document.createElement` was making it: it
    /// stays a plain element, and the DOM standard hands the caller an
    /// HTMLUnknownElement in its place (see create_object).
    Failed,
    /// Its upgrade failed: the constructor threw, never reached the element, or
    /// returned something else. The element keeps its own interface (an
    /// `HTMLElement`, or the built-in it extends) and is never `:defined`, and
    /// it is not offered to a definition again.
    FailedUpgrade
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
    void set_bounding_box(const Rect& r) {
        m_bounding_box = r;
        m_has_layout_box = (r.width > 0.0f || r.height > 0.0f || r.x != 0.0f || r.y != 0.0f);
    }
    bool has_layout_box() const { return m_has_layout_box; }
    /// True when the box above was assembled from inline fragments rather than
    /// being one box the layout made: a non-replaced inline element. CSSOM's
    /// resolved `width` is the computed value for one of those (CSS 2.1
    /// §10.3.1 leaves its width to its content), which is what browsers do.
    bool layout_box_from_fragments() const { return m_layout_from_fragments; }
    void clear_layout_box() {
        m_bounding_box = Rect();
        m_has_layout_box = false;
        m_layout_from_fragments = false;
        if (m_rare) {
            m_rare->layout_border = EdgeSizes();
            m_rare->layout_padding = EdgeSizes();
        }
    }
    void set_layout_box(const Rect& border_box, const EdgeSizes& border = EdgeSizes(),
                        const EdgeSizes& padding = EdgeSizes()) {
        m_bounding_box = border_box;
        m_has_layout_box = true;
        if (border.top != 0.0f || border.right != 0.0f || border.bottom != 0.0f || border.left != 0.0f) {
            rare().layout_border = border;
        } else if (m_rare) {
            m_rare->layout_border = EdgeSizes();
        }
        // The padding is kept for the same reason the border is: the content
        // box is what CSSOM's resolved `width` and `height` are, and it is the
        // border box minus both edges.
        if (padding.top != 0.0f || padding.right != 0.0f || padding.bottom != 0.0f || padding.left != 0.0f) {
            rare().layout_padding = padding;
        } else if (m_rare) {
            m_rare->layout_padding = EdgeSizes();
        }
    }
    void union_layout_box(const Rect& r) {
        m_layout_from_fragments = true;
        if (!m_has_layout_box) {
            m_bounding_box = r;
            m_has_layout_box = true;
            return;
        }
        const float min_x = std::min(m_bounding_box.x, r.x);
        const float min_y = std::min(m_bounding_box.y, r.y);
        const float max_x = std::max(m_bounding_box.x + m_bounding_box.width, r.x + r.width);
        const float max_y = std::max(m_bounding_box.y + m_bounding_box.height, r.y + r.height);
        m_bounding_box = Rect(min_x, min_y, max_x - min_x, max_y - min_y);
    }
    EdgeSizes layout_border() const {
        return m_rare ? m_rare->layout_border : EdgeSizes();
    }
    EdgeSizes layout_padding() const {
        return m_rare ? m_rare->layout_padding : EdgeSizes();
    }
    Rect padding_box() const {
        const EdgeSizes b = layout_border();
        return Rect(
            m_bounding_box.x + b.left,
            m_bounding_box.y + b.top,
            std::max(0.0f, m_bounding_box.width - b.left - b.right),
            std::max(0.0f, m_bounding_box.height - b.top - b.bottom)
        );
    }

    bool has_attribute(const std::string& name) const {
        return m_attributes.find(name) != m_attributes.end();
    }

    std::string get_attribute(const std::string& name) const {
        auto it = m_attributes.find(name);
        return it != m_attributes.end() ? it->value : "";
    }

    /// The value of `name`, or null when the element has no such attribute.
    const std::string* attribute(const std::string& name) const {
        auto it = m_attributes.find(name);
        return it != m_attributes.end() ? &it->value : nullptr;
    }

    /// The first attribute with this qualified name, whatever its namespace:
    /// what `getAttribute` and `hasAttribute` ask for. Null when there is none.
    const Attribute* find_attribute(const std::string& name) const {
        auto it = m_attributes.find(name);
        return it == m_attributes.end() ? nullptr : &*it;
    }

    // An attribute is identified by its expanded name (namespace and local
    // name), as the DOM standard says: `setAttributeNS("ab", "attr", …)` and
    // `setAttributeNS("kl", "attr", …)` are two attributes, and the first of
    // them answers to `getAttribute("attr")`. The namespace of an attribute
    // with none is the empty string; the empty string and null are one there,
    // because DOMString? is turned into a string before it gets here.

    /// The namespace of the attribute first stored as `qualified_name`;
    /// empty for one in no namespace.
    const std::string& attribute_namespace(const std::string& qualified_name) const;

    /// `setAttribute`: the first attribute with this qualified name changes
    /// its value (keeping its namespace), or a new one in no namespace is
    /// added at the end.
    void set_attribute(const std::string& name, std::string value);
    /// `setAttributeNS`: the attribute with this namespace and local name
    /// changes its value, or a new one is added at the end. An empty
    /// namespace is none, and the qualified name keeps its prefix.
    void set_attribute_ns(const std::string& namespace_uri, const std::string& qualified_name, std::string value);
    /// `setAttributeNode`: adds an attribute that has no place on this
    /// element yet, at the end, or changes the one with its expanded name.
    void add_attribute(const std::string& namespace_uri, const std::string& qualified_name, std::string value);
    /// `removeAttribute`: the first attribute with this qualified name.
    void remove_attribute(const std::string& name);
    /// `removeAttributeNS`: the attribute with this namespace and local name.
    void remove_attribute_ns(const std::string& namespace_uri, const std::string& local_name);
    /// The attribute with this namespace (empty for none) and local name;
    /// null when there is none.
    const Attribute* find_attribute_ns(const std::string& namespace_uri, const std::string& local_name) const;

    /// Changes the value of an attribute that is there, by expanded name.
    void set_attribute_value(const Attribute& attribute, std::string value);
    /// `setAttributeNodeNS` on an attribute that is already there: it keeps
    /// its place and its namespace, and takes the new qualified name and
    /// value. (The prefix is the attribute's, not its namespace's.)
    void replace_attribute(const Attribute& attribute, const std::string& name, std::string value);

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
        return attribute == m_attributes.end() ? empty_string() : attribute->value;
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
        const std::string& attr = it->value;
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

    /// A dialog `showModal()` opened. The flag is what `close()` reads to
    /// decide whether the focus goes back: an ordinary `show()` takes no
    /// "previously focused element", so there is nothing for it to give back.
    /// The top layer itself is not implemented — this is the state that a
    /// painter and a hit test would consult, and the only reader today is the
    /// dialog's own close.
    bool is_modal_dialog() const { return m_modal_dialog; }
    void set_modal_dialog(bool value) { m_modal_dialog = value; }

    /// The element that had the focus when `showModal()` ran, held until the
    /// dialog closes. Empty for a dialog opened with `show()`.
    const std::shared_ptr<Element>& dialog_previously_focused() const { return m_dialog_previous_focus; }
    void set_dialog_previously_focused(std::shared_ptr<Element> element) {
        m_dialog_previous_focus = std::move(element);
    }

    /// With script bindings attached this is `element.click()`: the listeners
    /// run and then what the click does (a checkbox toggles, a submit button
    /// submits). Without them: one click event.
    bool click();

    std::shared_ptr<CanvasRenderingContext2D> canvas_context() const { return m_canvas_context; }
    void set_canvas_context(std::shared_ptr<CanvasRenderingContext2D> ctx) { m_canvas_context = std::move(ctx); }
    std::shared_ptr<CanvasRenderingContext2D> get_or_create_canvas_context_2d();

    /// Nested frame document hosted by an `<iframe>` / `<frame>`, if active.
    std::shared_ptr<Document> content_document() const {
        return m_rare ? m_rare->content_document.lock() : nullptr;
    }
    void set_content_document(const std::shared_ptr<Document>& doc) {
        if (!doc && !m_rare) return;
        rare().content_document = doc;
    }

protected:
    void dump_open(std::ostream& os, const std::string& indent, bool has_content) const override;
    void dump_close(std::ostream& os, const std::string& indent) const override;

private:
    /// What every change to an attribute does after the list is updated:
    /// layout, the tree version, a form control's state, and the hook that
    /// tells a script (the `Attr` objects and MutationObserver).
    void attribute_changed(const Attribute& attribute, const std::string* old_value);
    void attribute_removed(const Attribute& attribute, const std::string& old_value);

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
        /// Layout border widths (stored only when non-zero so Element pays 0 bytes).
        EdgeSizes layout_border;
        EdgeSizes layout_padding;
        /// Weak reference to nested browsing context Document for `<iframe>` layout/paint.
        std::weak_ptr<Document> content_document;
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
    bool m_modal_dialog{false};
    std::shared_ptr<Element> m_dialog_previous_focus;
    bool m_has_layout_box{false};
    bool m_layout_from_fragments{false};
};

} // namespace litebird
