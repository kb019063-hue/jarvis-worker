// Node and the interfaces right under it: CharacterData, Text, Comment,
// DocumentFragment, DocumentType; the ChildNode and ParentNode mixins; and
// the tree operations with the checks the DOM standard puts in front of them.

#include "litebird/js/DomBindings.hpp"
#include "litebird/js/DomLive.hpp"
#include "litebird/dom/ProcessingInstruction.hpp"
#include "litebird/xml/XmlDom.hpp"
#include "litebird/dom/FormState.hpp"
#include "litebird/dom/TreeWalk.hpp"

#include <algorithm>

namespace litebird {
namespace dom {

bool is_any_node(const Node&) { return true; }
bool is_element_node(const Node& node) { return node.is_element(); }
bool is_character_data(const Node& node) {
    return node.is_text() || node.is_comment() || node.is_processing_instruction();
}

namespace {

bool is_text_node(const Node& node) { return node.is_text(); }
bool is_fragment_node(const Node& node) { return node.is_document_fragment(); }
bool is_doctype_node(const Node& node) { return node.is_document_type(); }
bool is_instruction_node(const Node& node) { return node.is_processing_instruction(); }

std::u16string units(const std::string& text) { return js_utf8_to_utf16(text); }
std::string utf8(std::u16string_view text) { return js_utf16_to_utf8(text); }

const std::string kEmpty;

std::shared_ptr<Node> root_of(const Node& node) {
    std::shared_ptr<Node> at = const_cast<Node&>(node).shared_from_this();
    for (;;) {
        auto parent = script_parent(*at);
        if (!parent) return at;
        at = std::move(parent);
    }
}

// The index of `child` among the children of `parent` as scripts count them.
std::size_t script_index(const Node& parent, const Node& child) {
    if (!parent.is_document()) return child.index_in(parent);
    const auto children = script_children(parent);
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (children[i].get() == &child) return i;
    }
    return children.size();
}

std::shared_ptr<Node> script_sibling(const Node& node, int direction) {
    auto parent = script_parent(node);
    if (!parent) return nullptr;
    if (!parent->is_document()) return direction > 0 ? node.next_sibling() : node.previous_sibling();
    const auto children = script_children(*parent);
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (children[i].get() != &node) continue;
        if (direction > 0) return i + 1 < children.size() ? children[i + 1] : nullptr;
        return i > 0 ? children[i - 1] : nullptr;
    }
    return nullptr;
}

std::shared_ptr<Node> script_first_child(const Node& node) {
    if (!node.is_document()) return node.first_child();
    const auto children = script_children(node);
    return children.empty() ? nullptr : children.front();
}

std::shared_ptr<Node> script_last_child(const Node& node) {
    if (!node.is_document()) return node.last_child();
    const auto children = script_children(node);
    return children.empty() ? nullptr : children.back();
}

bool can_have_children(const Node& node) {
    return node.is_document() || node.is_document_fragment() || node.is_element();
}

std::size_t element_child_count(const Node& node) {
    std::size_t count = 0;
    for (const auto& child : node.children()) {
        if (child->is_element()) ++count;
    }
    return count;
}

std::string node_name(const Node& node) {
    switch (node.type()) {
        case NodeType::Document: return "#document";
        case NodeType::Element: {
            const auto& element = static_cast<const Element&>(node);
            const std::string name = qualified_name(element);
            return is_html_in_html_document(element) ? ascii_upper(name) : name;
        }
        case NodeType::Text: return node.is_cdata_section() ? "#cdata-section" : "#text";
        case NodeType::Comment: return "#comment";
        case NodeType::DocumentFragment: return "#document-fragment";
        case NodeType::DocumentType: return static_cast<const DocumentTypeNode&>(node).name();
        case NodeType::ProcessingInstruction: return static_cast<const ProcessingInstruction&>(node).target();
    }
    return std::string();
}

int node_type_number(const Node& node) {
    switch (node.type()) {
        case NodeType::Element: return 1;
        case NodeType::Text: return node.is_cdata_section() ? 4 : 3;
        case NodeType::ProcessingInstruction: return 7;
        case NodeType::Comment: return 8;
        case NodeType::Document: return 9;
        case NodeType::DocumentType: return 10;
        case NodeType::DocumentFragment: return 11;
    }
    return 0;
}

} // namespace

int compare_document_position(const Node& self, const Node& other) {
    if (&self == &other) return 0;
    // Chains are raw pointers into trees held by the two arguments' roots.
    std::vector<std::shared_ptr<Node>> self_chain, other_chain;
    for (auto at = const_cast<Node&>(self).shared_from_this(); at; at = script_parent(*at)) self_chain.push_back(at);
    for (auto at = const_cast<Node&>(other).shared_from_this(); at; at = script_parent(*at)) other_chain.push_back(at);
    if (self_chain.back() != other_chain.back()) {
        // Different trees: disconnected, in an order that is at least stable.
        return 1 | 32 | (self_chain.back().get() < other_chain.back().get() ? 4 : 2);
    }
    // Walk down from the root while the chains agree.
    std::size_t i = self_chain.size(), j = other_chain.size();
    while (i > 0 && j > 0 && self_chain[i - 1] == other_chain[j - 1]) {
        --i;
        --j;
    }
    if (j == 0) return 8 | 2;           // other is an ancestor of self: contains, preceding
    if (i == 0) return 16 | 4;          // other is inside self: contained by, following
    const Node& common = *self_chain[i];
    const std::size_t self_index = script_index(common, *self_chain[i - 1]);
    const std::size_t other_index = script_index(common, *other_chain[j - 1]);
    return other_index < self_index ? 2 : 4;
}

namespace {

bool nodes_equal(const Node& a, const Node& b) {
    // Pairs still to compare; no recursion, a tree can be very deep.
    std::vector<std::pair<const Node*, const Node*>> pending{{&a, &b}};
    while (!pending.empty()) {
        const auto [x, y] = pending.back();
        pending.pop_back();
        if (x->type() != y->type()) return false;
        switch (x->type()) {
            case NodeType::Element: {
                const auto& ex = static_cast<const Element&>(*x);
                const auto& ey = static_cast<const Element&>(*y);
                if (ex.element_namespace() != ey.element_namespace() || ex.tag_name() != ey.tag_name()) return false;
                if (ex.other_namespace_uri() != ey.other_namespace_uri() || ex.prefix() != ey.prefix()) return false;
                // Attributes are the same by namespace, local name and
                // value: the prefix is not part of it.
                if (ex.attributes().size() != ey.attributes().size()) return false;
                for (const auto& [name, value] : ex.attributes()) {
                    const std::string& uri = ex.attribute_namespace(name);
                    const std::string* other = nullptr;
                    if (uri.empty()) {
                        other = ey.attribute_namespace(name).empty() ? ey.attribute(name) : nullptr;
                    } else {
                        const auto colon = name.find(':');
                        const std::string local = colon == std::string::npos ? name : name.substr(colon + 1);
                        if (const std::string* stored = ey.find_attribute_ns(uri, local)) other = ey.attribute(*stored);
                    }
                    if (!other || *other != value) return false;
                }
                break;
            }
            case NodeType::Text:
                if (x->is_cdata_section() != y->is_cdata_section()) return false;
                if (character_data(*x) != character_data(*y)) return false;
                break;
            case NodeType::Comment:
                if (character_data(*x) != character_data(*y)) return false;
                break;
            case NodeType::ProcessingInstruction:
                if (static_cast<const ProcessingInstruction&>(*x).target() !=
                    static_cast<const ProcessingInstruction&>(*y).target()) {
                    return false;
                }
                if (character_data(*x) != character_data(*y)) return false;
                break;
            case NodeType::DocumentType: {
                const auto& dx = static_cast<const DocumentTypeNode&>(*x);
                const auto& dy = static_cast<const DocumentTypeNode&>(*y);
                if (dx.name() != dy.name() || dx.public_id() != dy.public_id() || dx.system_id() != dy.system_id()) return false;
                break;
            }
            default:
                break;
        }
        const auto cx = script_children(*x);
        const auto cy = script_children(*y);
        if (cx.size() != cy.size()) return false;
        for (std::size_t i = 0; i < cx.size(); ++i) pending.push_back({cx[i].get(), cy[i].get()});
    }
    return true;
}

void normalize_subtree(Node& root) {
    // Parents whose children hold text, listed first: merging changes child lists.
    std::vector<std::shared_ptr<Node>> parents{root.shared_from_this()};
    for_each_descendant(root, [&](Node& node) {
        if (!node.children().empty()) parents.push_back(node.shared_from_this());
        return true;
    });
    for (const auto& parent : parents) {
        const NodeVector children = parent->children();
        std::shared_ptr<Node> run;         // the text node the following ones are merged into
        for (const auto& child : children) {
            // A CDATA section stays what it is: only plain text is merged.
            if (!child->is_text() || child->is_cdata_section()) {
                run.reset();
                continue;
            }
            const std::string& data = character_data(*child);
            if (data.empty()) {
                parent->remove_child(child);
                continue;
            }
            if (!run) {
                run = child;
                continue;
            }
            // Boundaries in the node that goes move into the one that stays.
            live_note_text_merged(*child, run, utf16_length(character_data(*run)));
            store_character_data(*run, character_data(*run) + data);
            parent->remove_child(child);
        }
    }
}

// The text a string argument of append() and friends turns into.
std::shared_ptr<Node> text_node_for(Realm& realm, Node& context, const std::string& data) {
    auto text = std::make_shared<Text>(data);
    if (auto document = document_of(realm, context)) text->set_owner_document(document);
    return text;
}

// A document has at most one doctype and one element, the doctype first.
// Where they stand next to `child`: the node an insertion goes before, or
// the node a replacement takes out. The doctype is not among children();
// its place is a count of the children before it.
struct DocumentShape {
    bool has_doctype{false};
    /// An element child; when replacing, one other than `child`.
    bool has_element{false};
    /// "A doctype is following child."
    bool doctype_follows_child{false};
    /// "An element is preceding child."
    bool element_precedes_child{false};
};

DocumentShape document_shape(const Node& parent, const Node* child, bool replacing) {
    const auto& document = static_cast<const Document&>(parent);
    const auto& children = parent.children();
    DocumentShape shape;
    shape.has_doctype = document.has_doctype();
    const std::size_t doctype_at = std::min(document.doctype().preceding_children, children.size());
    const bool child_is_doctype = child && child->is_document_type();
    bool seen_child = false;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const Node* node = children[i].get();
        if (node == child) {
            seen_child = true;
            if (shape.has_doctype && i < doctype_at) shape.doctype_follows_child = true;
            if (replacing) continue;
        }
        if (!node->is_element()) continue;
        shape.has_element = true;
        if (node == child) continue;
        if (child_is_doctype ? i < doctype_at : (child && !seen_child)) shape.element_precedes_child = true;
    }
    return shape;
}

// Whether `node` may go into the document `parent`: before `child` (null for
// the end), or in place of `child` when replacing.
void ensure_document_shape(Realm& realm, const Node& parent, const Node& node, const Node* child, bool replacing) {
    std::size_t elements = 0;
    if (node.is_document_fragment()) {
        for (const auto& inner : node.children()) {
            if (inner->is_text()) throw_dom(realm, "HierarchyRequestError", "A document cannot have text as a child.");
            if (inner->is_element()) ++elements;
        }
        if (elements > 1) throw_dom(realm, "HierarchyRequestError", "Only one element on document allowed.");
        if (elements == 0) return;
    } else if (node.is_element()) {
        elements = 1;
    } else if (!node.is_document_type()) {
        return;         // a comment or a processing instruction goes anywhere
    }
    const DocumentShape shape = document_shape(parent, child, replacing);
    if (elements == 1) {
        if (shape.has_element) throw_dom(realm, "HierarchyRequestError", "Only one element on document allowed.");
        if ((!replacing && child && child->is_document_type()) || shape.doctype_follows_child) {
            throw_dom(realm, "HierarchyRequestError", "An element cannot come before the doctype.");
        }
        return;
    }
    if (shape.has_doctype && !(replacing && child && child->is_document_type())) {
        throw_dom(realm, "HierarchyRequestError", "Only one doctype on document allowed.");
    }
    if (shape.element_precedes_child || (!replacing && !child && shape.has_element)) {
        throw_dom(realm, "HierarchyRequestError", "A doctype cannot come after the document element.");
    }
}

// Runs the scripts among the nodes that have just been connected.
void after_insertion(Realm& realm, const NodeVector& inserted) {
    for (const auto& node : inserted) {
        if (!node->is_connected()) continue;
        if (is_html_element(*node, "script")) {
            run_inserted_script(realm, static_cast<Element&>(*node));
            continue;
        }
        if (!node->is_element() || node->children().empty()) continue;
        std::vector<std::shared_ptr<Element>> scripts;
        for_each_descendant(*node, [&](Node& inner) {
            if (is_html_element(inner, "script")) scripts.push_back(shared(static_cast<Element&>(inner)));
            return true;
        });
        for (const auto& script : scripts) {
            if (script->is_connected()) run_inserted_script(realm, *script);
        }
    }
}

// The insertion itself, after the checks.
void insert_node(Realm& realm, Node& parent, const std::shared_ptr<Node>& node, std::shared_ptr<Node> reference) {
    if (node->is_document_type()) {
        // A doctype is not kept among the children; the document records
        // it, with its place among them, and the node the script holds is
        // the one document.doctype gives back.
        if (auto* document = parent.as_document()) {
            auto type = std::static_pointer_cast<DocumentTypeNode>(node);
            if (auto old = type->document()) old->remove_doctype();
            const auto& children = parent.children();
            std::size_t before = children.size();
            if (reference) {
                const auto at = std::find(children.begin(), children.end(), reference);
                if (at != children.end()) before = static_cast<std::size_t>(at - children.begin());
            }
            document->adopt_doctype_node(type, before);
        }
        return;
    }
    const bool before_doctype = reference && reference->is_document_type();
    if (before_doctype) {
        // "Before the doctype" is where the child that follows it stands;
        // the doctype is moved past what goes in, below.
        const auto& document = static_cast<const Document&>(parent);
        const std::size_t index = std::min(document.doctype().preceding_children, parent.children().size());
        reference = index < parent.children().size() ? parent.children()[index] : nullptr;
    }
    const std::size_t children_before = parent.children().size();
    // A node from another document belongs to this one from now on.
    if (auto to = document_of(realm, parent)) {
        auto from = node->owner_document();
        if (from && from != to) adopt_node(realm, *node, to);
    }
    NodeVector inserted;
    if (node->is_document_fragment()) {
        // The children of a fragment arrive as one change: one record.
        inserted = node->children();
        if (inserted.empty()) return;
        auto previous = reference ? reference->previous_sibling() : parent.last_child();
        ChildListBatch batch(realm, parent);
        if (reference) parent.insert_before(node, reference);
        else parent.append_child(node);
        batch.commit(inserted, {}, previous, reference);
    } else {
        inserted.push_back(node);
        if (reference) parent.insert_before(node, reference);
        else parent.append_child(node);
    }
    if (before_doctype && parent.children().size() > children_before) {
        static_cast<Document&>(parent).move_doctype_after(parent.children().size() - children_before);
    }
    after_insertion(realm, inserted);
}

} // namespace

// ---------------------------------------------------------------------------
// What the rest of the bindings use
// ---------------------------------------------------------------------------

std::vector<std::shared_ptr<Node>> script_children(const Node& node) {
    if (!node.is_document()) return node.children();
    const auto& document = static_cast<const Document&>(node);
    std::vector<std::shared_ptr<Node>> out = node.children();
    if (document.has_doctype()) {
        auto type = const_cast<Document&>(document).doctype_node();
        const std::size_t at = std::min(document.doctype().preceding_children, out.size());
        out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), type);
    }
    return out;
}

std::shared_ptr<Node> script_parent(const Node& node) {
    if (node.is_document_type()) return static_cast<const DocumentTypeNode&>(node).document();
    return node.parent();
}

const std::string& character_data(const Node& node) {
    if (node.is_text()) return static_cast<const Text&>(node).text();
    if (node.is_comment()) return static_cast<const Comment&>(node).data();
    if (node.is_processing_instruction()) return static_cast<const ProcessingInstruction&>(node).data();
    return kEmpty;
}

void set_character_data(Node& node, const std::string& data) {
    live_note_data_set(node);
    store_character_data(node, data);
}

void store_character_data(Node& node, const std::string& data) {
    std::string old;
    if (node.is_text()) {
        auto& text = static_cast<Text&>(node);
        old = text.text();
        text.set_text(data);
    } else if (node.is_comment()) {
        auto& comment = static_cast<Comment&>(node);
        old = comment.data();
        comment.set_data(data);
    } else if (node.is_processing_instruction()) {
        auto& instruction = static_cast<ProcessingInstruction&>(node);
        old = instruction.data();
        instruction.set_data(data);
    } else {
        return;
    }
    node.mark_needs_layout();
    Node::bump_tree_version();
    note_character_data_changed(node, old);
}

void set_text_content(Node& node, const std::string& text) {
    if (is_character_data(node)) {
        set_character_data(node, text);
        return;
    }
    if (!node.is_element() && !node.is_document_fragment()) return;
    Realm& realm = realm_for_node(node);
    NodeVector nodes;
    if (!text.empty()) nodes.push_back(text_node_for(realm, node, text));
    replace_all_children(realm, node, nodes);
}

void replace_all_children(Realm& realm, Node& parent, const NodeVector& nodes) {
    const NodeVector removed = parent.children();
    NodeVector added;
    for (const auto& node : nodes) {
        if (node->is_document_fragment()) added.insert(added.end(), node->children().begin(), node->children().end());
        else added.push_back(node);
    }
    if (removed.empty() && added.empty()) return;
    {
        ChildListBatch batch(realm, parent);
        parent.take_children();
        for (const auto& node : nodes) parent.append_child(node);
        batch.commit(added, removed, nullptr, nullptr);
    }
    after_insertion(realm, added);
}

void remove_node(Realm&, Node& node) {
    if (node.is_document_type()) {
        if (auto document = static_cast<DocumentTypeNode&>(node).document()) document->remove_doctype();
        return;
    }
    if (auto parent = node.parent()) parent->remove_child(node.shared_from_this());
}

void adopt_node(Realm& realm, Node& node, const std::shared_ptr<Document>& to) {
    auto keep = node.shared_from_this();
    const auto from = node.owner_document();
    remove_node(realm, node);
    if (!to || from == to) return;
    // Its custom elements, found before they change hands.
    std::vector<std::shared_ptr<Element>> custom;
    auto consider = [&](Node& candidate) {
        if (!candidate.is_element()) return;
        auto& element = static_cast<Element&>(candidate);
        if (element.custom_state() == CustomElementState::Custom) custom.push_back(shared(element));
    };
    consider(node);
    for_each_descendant(node, [&](Node& inner) {
        consider(inner);
        return true;
    });
    node.set_owner_document(to);
    for (const auto& element : custom) note_adopted(realm, *element, from, to);
}

Node& node_argument(Args args, std::size_t index, const char* what) {
    Node* node = index < args.size() ? node_ptr(args[index]) : nullptr;
    if (!node) {
        js_throw_type_error(std::string("Failed to execute '") + what + "': parameter " + std::to_string(index + 1) +
                            " is not of type 'Node'.");
    }
    return *node;
}

void ensure_pre_insert_validity(Realm& realm, Node& parent, Node& node, Node* child) {
    if (!can_have_children(parent)) {
        throw_dom(realm, "HierarchyRequestError", "This node type does not support this method.");
    }
    if (node.is_host_including_inclusive_ancestor_of(parent)) {
        throw_dom(realm, "HierarchyRequestError", "The new child element contains the parent.");
    }
    if (child && script_parent(*child).get() != &parent) {
        throw_dom(realm, "NotFoundError", "The node before which the new node is to be inserted is not a child of this node.");
    }
    if (node.is_document()) {
        throw_dom(realm, "HierarchyRequestError", "Nodes of type '#document' may not be inserted inside nodes of type '" +
                                                  node_name(parent) + "'.");
    }
    if (node.is_text() && parent.is_document()) {
        throw_dom(realm, "HierarchyRequestError", "Nodes of type '#text' may not be inserted inside nodes of type '#document'.");
    }
    if (node.is_document_type() && !parent.is_document()) {
        throw_dom(realm, "HierarchyRequestError", "A doctype may only be a child of a document.");
    }
    if (parent.is_document()) ensure_document_shape(realm, parent, node, child, false);
}

void pre_insert(Realm& realm, Node& parent, const std::shared_ptr<Node>& node, Node* child) {
    ensure_pre_insert_validity(realm, parent, *node, child);
    std::shared_ptr<Node> reference = child ? child->shared_from_this() : nullptr;
    if (reference == node) reference = script_sibling(*node, +1);
    insert_node(realm, parent, node, reference);
}

std::shared_ptr<Node> nodes_from_arguments(Realm& realm, Node& context, Args args) {
    NodeVector nodes;
    for (const JsValue& arg : args) {
        if (auto node = node_of(arg)) nodes.push_back(node);
        else nodes.push_back(text_node_for(realm, context, to_str(arg)));
    }
    if (nodes.size() == 1) return nodes.front();
    auto fragment = std::make_shared<Node>(NodeType::DocumentFragment);
    if (auto document = document_of(realm, context)) fragment->set_owner_document(document);
    for (const auto& node : nodes) {
        // The same checks an insertion into the fragment would make.
        ensure_pre_insert_validity(realm, *fragment, *node, nullptr);
        fragment->append_child(node);
    }
    return fragment;
}

std::shared_ptr<Node> clone_node(Realm& realm, const Node& node, bool deep, const std::shared_ptr<Document>& into) {
    std::vector<std::shared_ptr<Element>> custom;
    auto copy_one = [&](const Node& source) -> std::shared_ptr<Node> {
        std::shared_ptr<Node> copy;
        switch (source.type()) {
            case NodeType::Element: {
                const auto& from = static_cast<const Element&>(source);
                auto element = std::make_shared<Element>(from.tag_name(), from.element_namespace());
                // The name as it is, whatever its case, and the namespace
                // and prefix: a copy of <p:Item xmlns:p="urn:x"> is one too.
                element->set_tag_name_verbatim(from.tag_name());
                element->set_prefix(from.prefix());
                element->set_other_namespace_uri(from.other_namespace_uri());
                element->m_attributes = from.m_attributes;
                element->copy_attribute_namespaces(from);
                if (const FormControlState* state = from.form_state()) {
                    // What a control holds is cloned with it.
                    FormControlState& made = element->ensure_form_state();
                    made = *state;
                    made.none_options.clear();
                    made.select_none = false;
                }
                if (from.template_contents()) {
                    auto owner = into ? into->template_document() : nullptr;
                    auto contents = deep ? clone_node(realm, *from.template_contents(), true, owner)
                                         : std::make_shared<Node>(NodeType::DocumentFragment);
                    if (owner) contents->set_owner_document(owner);
                    element->set_template_contents(contents);
                }
                if (!from.is_value().empty()) element->set_is_value(from.is_value());
                // A copy is a new element: if its name is defined, it is
                // upgraded like one that was just created.
                if (!from.is_foreign() && (from.tag_name().find('-') != std::string::npos || !from.is_value().empty())) {
                    custom.push_back(element);
                }
                copy = element;
                break;
            }
            case NodeType::Text: {
                auto text = std::make_shared<Text>(static_cast<const Text&>(source).text());
                text->set_cdata_section(source.is_cdata_section());
                copy = text;
                break;
            }
            case NodeType::Comment:
                copy = std::make_shared<Comment>(static_cast<const Comment&>(source).data());
                break;
            case NodeType::ProcessingInstruction: {
                const auto& from = static_cast<const ProcessingInstruction&>(source);
                copy = std::make_shared<ProcessingInstruction>(from.target(), from.data());
                break;
            }
            case NodeType::DocumentFragment:
                copy = std::make_shared<Node>(NodeType::DocumentFragment);
                break;
            case NodeType::DocumentType: {
                const auto& type = static_cast<const DocumentTypeNode&>(source);
                copy = std::make_shared<DocumentTypeNode>(type.name(), type.public_id(), type.system_id());
                break;
            }
            case NodeType::Document: {
                const auto& from = static_cast<const Document&>(source);
                auto document = std::make_shared<Document>();
                // The doctype is a child: a copy that is not deep has none.
                if (deep) document->set_doctype(from.doctype());
                document->set_quirks_mode(from.quirks_mode());
                document->set_url(from.url());
                document->set_content_type(from.content_type());
                document->set_xml_document_object(from.is_xml_document_object());
                copy = document;
                break;
            }
        }
        if (copy && !copy->is_document() && into) copy->set_owner_document(into);
        return copy;
    };

    std::shared_ptr<Node> result = copy_one(node);
    if (deep && result) {
        // (source, copy) pairs whose children are still to be copied.
        std::vector<std::pair<const Node*, std::shared_ptr<Node>>> pending{{&node, result}};
        while (!pending.empty()) {
            auto [source, copy] = pending.back();
            pending.pop_back();
            for (const auto& child : source->children()) {
                auto child_copy = copy_one(*child);
                if (!child_copy) continue;
                copy->append_child(child_copy);
                if (!child->children().empty()) pending.push_back({child.get(), child_copy});
            }
        }
    }
    for (const auto& element : custom) try_upgrade(realm, *element);
    return result;
}

// ---------------------------------------------------------------------------
// Mixins
// ---------------------------------------------------------------------------

namespace {

bool in_list(const NodeVector& nodes, const Node* node) {
    for (const auto& entry : nodes) {
        if (entry.get() == node) return true;
    }
    return false;
}

NodeVector node_arguments(Args args) {
    NodeVector nodes;
    for (const JsValue& arg : args) {
        if (auto node = node_of(arg)) nodes.push_back(node);
    }
    return nodes;
}

} // namespace

void define_child_node(Iface& I, bool (*accepts)(const Node&)) {
    I.node_method(accepts, "before", 0, [](Realm& r, Node& self, Args args) {
        auto parent = script_parent(self);
        if (!parent) return undefined();
        const NodeVector given = node_arguments(args);
        std::shared_ptr<Node> viable = script_sibling(self, -1);
        while (viable && in_list(given, viable.get())) viable = script_sibling(*viable, -1);
        auto node = nodes_from_arguments(r, self, args);
        std::shared_ptr<Node> reference = viable ? script_sibling(*viable, +1) : script_first_child(*parent);
        pre_insert(r, *parent, node, reference.get());
        return undefined();
    });
    I.node_method(accepts, "after", 0, [](Realm& r, Node& self, Args args) {
        auto parent = script_parent(self);
        if (!parent) return undefined();
        const NodeVector given = node_arguments(args);
        std::shared_ptr<Node> viable = script_sibling(self, +1);
        while (viable && in_list(given, viable.get())) viable = script_sibling(*viable, +1);
        auto node = nodes_from_arguments(r, self, args);
        pre_insert(r, *parent, node, viable.get());
        return undefined();
    });
    I.node_method(accepts, "replaceWith", 0, [](Realm& r, Node& self, Args args) {
        auto parent = script_parent(self);
        if (!parent) return undefined();
        auto keep = self.shared_from_this();
        const NodeVector given = node_arguments(args);
        std::shared_ptr<Node> viable = script_sibling(self, +1);
        while (viable && in_list(given, viable.get())) viable = script_sibling(*viable, +1);
        auto node = nodes_from_arguments(r, self, args);
        if (script_parent(self) == parent) {
            // Still in place: one replacement.
            ensure_pre_insert_validity(r, *parent, *node, viable.get());
            NodeVector added;
            if (node->is_document_fragment()) added = node->children();
            else added.push_back(node);
            auto previous = script_sibling(self, -1);
            {
                ChildListBatch batch(r, *parent);
                if (viable) parent->insert_before(node, viable);
                else parent->append_child(node);
                if (self.parent() == parent && !in_list(added, &self)) parent->remove_child(keep);
                batch.commit(added, {keep}, previous, viable);
            }
            after_insertion(r, added);
        } else {
            pre_insert(r, *parent, node, viable.get());
        }
        return undefined();
    });
    I.node_method(accepts, "remove", 0, [](Realm& r, Node& self, Args) {
        remove_node(r, self);
        return undefined();
    });
}

void define_element_siblings(Iface& I, bool (*accepts)(const Node&)) {
    I.node_getter(accepts, "previousElementSibling", [](Realm& r, Node& self) {
        for (auto at = self.previous_sibling(); at; at = at->previous_sibling()) {
            if (at->is_element()) return wrap(r, at);
        }
        return null();
    });
    I.node_getter(accepts, "nextElementSibling", [](Realm& r, Node& self) {
        for (auto at = self.next_sibling(); at; at = at->next_sibling()) {
            if (at->is_element()) return wrap(r, at);
        }
        return null();
    });
}

void define_parent_node(Iface& I, bool (*accepts)(const Node&)) {
    I.node_getter(accepts, "children", [](Realm& r, Node& self) {
        JsValue owner = wrap(r, self);
        auto root = self.shared_from_this();
        return cached(owner, "children", [&]() {
            return make_live_list(r, "HTMLCollection", root, [](Node& node, NodeVector& out) {
                for (const auto& child : node.children()) {
                    if (child->is_element()) out.push_back(child);
                }
            });
        });
    });
    I.node_getter(accepts, "firstElementChild", [](Realm& r, Node& self) {
        for (const auto& child : self.children()) {
            if (child->is_element()) return wrap(r, child);
        }
        return null();
    });
    I.node_getter(accepts, "lastElementChild", [](Realm& r, Node& self) {
        const auto& children = self.children();
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if ((*it)->is_element()) return wrap(r, *it);
        }
        return null();
    });
    I.node_getter(accepts, "childElementCount", [](Realm&, Node& self) {
        return num(static_cast<double>(element_child_count(self)));
    });
    I.node_method(accepts, "prepend", 0, [](Realm& r, Node& self, Args args) {
        auto node = nodes_from_arguments(r, self, args);
        auto first = script_first_child(self);
        pre_insert(r, self, node, first.get());
        return undefined();
    });
    I.node_method(accepts, "append", 0, [](Realm& r, Node& self, Args args) {
        auto node = nodes_from_arguments(r, self, args);
        pre_insert(r, self, node, nullptr);
        return undefined();
    });
    I.node_method(accepts, "replaceChildren", 0, [](Realm& r, Node& self, Args args) {
        auto node = nodes_from_arguments(r, self, args);
        ensure_pre_insert_validity(r, self, *node, nullptr);
        replace_all_children(r, self, {node});
        return undefined();
    });
    I.node_method(accepts, "querySelector", 1, [](Realm& r, Node& self, Args args) {
        require_args(args, 1, "querySelector");
        return wrap(r, query_first(r, self, to_str(args[0])));
    });
    I.node_method(accepts, "querySelectorAll", 1, [](Realm& r, Node& self, Args args) {
        require_args(args, 1, "querySelectorAll");
        return make_static_list(r, query_all(r, self, to_str(args[0])));
    });
}

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------

namespace {

void setup_node(Iface& I) {
    static const std::pair<const char*, int> kTypes[] = {
        {"ELEMENT_NODE", 1}, {"ATTRIBUTE_NODE", 2}, {"TEXT_NODE", 3}, {"CDATA_SECTION_NODE", 4},
        {"ENTITY_REFERENCE_NODE", 5}, {"ENTITY_NODE", 6}, {"PROCESSING_INSTRUCTION_NODE", 7},
        {"COMMENT_NODE", 8}, {"DOCUMENT_NODE", 9}, {"DOCUMENT_TYPE_NODE", 10},
        {"DOCUMENT_FRAGMENT_NODE", 11}, {"NOTATION_NODE", 12},
        {"DOCUMENT_POSITION_DISCONNECTED", 1}, {"DOCUMENT_POSITION_PRECEDING", 2},
        {"DOCUMENT_POSITION_FOLLOWING", 4}, {"DOCUMENT_POSITION_CONTAINS", 8},
        {"DOCUMENT_POSITION_CONTAINED_BY", 16}, {"DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC", 32},
    };
    for (const auto& entry : kTypes) I.constant(entry.first, entry.second);

    const auto any = &is_any_node;
    I.node_getter(any, "nodeType", [](Realm&, Node& self) { return num(node_type_number(self)); });
    I.node_getter(any, "nodeName", [](Realm&, Node& self) { return str(node_name(self)); });
    I.node_getter(any, "baseURI", [](Realm& r, Node& self) {
        Url base;
        return str(document_base_url(r, self, base) ? base.href() : std::string("about:blank"));
    });
    I.node_getter(any, "isConnected", [](Realm&, Node& self) {
        if (self.is_document_type()) return boolean(static_cast<DocumentTypeNode&>(self).document() != nullptr);
        return boolean(self.is_connected());
    });
    I.node_getter(any, "ownerDocument", [](Realm& r, Node& self) {
        if (self.is_document()) return null();
        return wrap(r, document_of(r, self));
    });
    I.node_method(any, "getRootNode", 0, [](Realm& r, Node& self, Args args) {
        const bool composed = to_bool(member(js_arg(args, 0), "composed"));
        auto root = root_of(self);
        while (composed) {
            auto* shadow = dynamic_cast<ShadowRoot*>(root.get());
            auto host = shadow ? shadow->host() : nullptr;
            if (!host) break;
            root = root_of(*host);
        }
        return wrap(r, root);
    });
    I.node_getter(any, "parentNode", [](Realm& r, Node& self) { return wrap(r, script_parent(self)); });
    I.node_getter(any, "parentElement", [](Realm& r, Node& self) {
        auto parent = self.parent();
        return parent && parent->is_element() ? wrap(r, parent) : null();
    });
    I.node_method(any, "hasChildNodes", 0, [](Realm&, Node& self, Args) {
        if (self.is_document()) return boolean(!script_children(self).empty());
        return boolean(!self.children().empty());
    });
    I.node_getter(any, "childNodes", [](Realm& r, Node& self) {
        JsValue owner = wrap(r, self);
        auto root = self.shared_from_this();
        return cached(owner, "childNodes", [&]() {
            return make_live_list(r, "NodeList", root, [](Node& node, NodeVector& out) { out = script_children(node); });
        });
    });
    I.node_getter(any, "firstChild", [](Realm& r, Node& self) { return wrap(r, script_first_child(self)); });
    I.node_getter(any, "lastChild", [](Realm& r, Node& self) { return wrap(r, script_last_child(self)); });
    I.node_getter(any, "previousSibling", [](Realm& r, Node& self) { return wrap(r, script_sibling(self, -1)); });
    I.node_getter(any, "nextSibling", [](Realm& r, Node& self) { return wrap(r, script_sibling(self, +1)); });
    I.node_accessor(any, "nodeValue",
        [](Realm&, Node& self) { return is_character_data(self) ? str(character_data(self)) : null(); },
        [](Realm&, Node& self, const JsValue& value) {
            if (is_character_data(self)) set_character_data(self, to_str_nullable(value));
        });
    I.node_accessor(any, "textContent",
        [](Realm&, Node& self) {
            if (self.is_document() || self.is_document_type()) return null();
            return str(self.text_content());
        },
        [](Realm&, Node& self, const JsValue& value) { set_text_content(self, to_str_nullable(value)); });
    I.node_method(any, "normalize", 0, [](Realm&, Node& self, Args) {
        normalize_subtree(self);
        return undefined();
    });
    I.node_method(any, "cloneNode", 0, [](Realm& r, Node& self, Args args) {
        if (dynamic_cast<ShadowRoot*>(&self)) throw_dom(r, "NotSupportedError", "ShadowRoot nodes are not clonable.");
        return wrap(r, clone_node(r, self, to_bool(js_arg(args, 0)), document_of(r, self)));
    });
    I.node_method(any, "isEqualNode", 1, [](Realm&, Node& self, Args args) {
        require_args(args, 1, "Node.isEqualNode");
        if (args[0].is_nullish()) return boolean(false);
        return boolean(nodes_equal(self, node_argument(args, 0, "Node.isEqualNode")));
    });
    I.node_method(any, "isSameNode", 1, [](Realm&, Node& self, Args args) {
        require_args(args, 1, "Node.isSameNode");
        return boolean(node_ptr(args[0]) == &self);
    });
    I.node_method(any, "compareDocumentPosition", 1, [](Realm&, Node& self, Args args) {
        require_args(args, 1, "Node.compareDocumentPosition");
        return num(compare_document_position(self, node_argument(args, 0, "Node.compareDocumentPosition")));
    });
    I.node_method(any, "contains", 1, [](Realm&, Node& self, Args args) {
        require_args(args, 1, "Node.contains");
        if (args[0].is_nullish()) return boolean(false);
        Node& other = node_argument(args, 0, "Node.contains");
        for (std::shared_ptr<Node> at = other.shared_from_this(); at; at = script_parent(*at)) {
            if (at.get() == &self) return boolean(true);
        }
        return boolean(false);
    });
    // The DOM standard's "locate a namespace" and "locate a namespace
    // prefix": the element's own name first, then its xmlns attributes, then
    // the same for each ancestor.
    I.node_method(any, "lookupNamespaceURI", 1, [](Realm&, Node& self, Args args) {
        const std::string prefix = js_arg(args, 0).is_nullish() ? std::string() : to_str(js_arg(args, 0));
        const std::string uri = xml::locate_namespace(self, prefix);
        return uri.empty() ? null() : str(uri);
    });
    I.node_method(any, "lookupPrefix", 1, [](Realm&, Node& self, Args args) {
        const std::string uri = js_arg(args, 0).is_nullish() ? std::string() : to_str(js_arg(args, 0));
        if (uri.empty()) return null();
        std::shared_ptr<Node> hold;
        const Node* at = &self;
        if (self.is_document()) {
            hold = static_cast<const Document&>(self).document_element();
            at = hold.get();
        } else if (self.is_document_type() || self.is_document_fragment()) {
            at = nullptr;
        } else if (!self.is_element()) {
            hold = self.parent();
            at = hold && hold->is_element() ? hold.get() : nullptr;
        }
        std::string prefix;
        if (!at || !xml::locate_prefix(static_cast<const Element&>(*at), uri, prefix)) return null();
        return str(prefix);
    });
    I.node_method(any, "isDefaultNamespace", 1, [](Realm&, Node& self, Args args) {
        const std::string wanted = js_arg(args, 0).is_nullish() ? std::string() : to_str(js_arg(args, 0));
        return boolean(xml::locate_namespace(self, std::string()) == wanted);
    });

    I.node_method(any, "insertBefore", 2, [](Realm& r, Node& self, Args args) {
        require_args(args, 2, "Node.insertBefore");
        Node& node = node_argument(args, 0, "Node.insertBefore");
        Node* child = args[1].is_nullish() ? nullptr : &node_argument(args, 1, "Node.insertBefore");
        pre_insert(r, self, node.shared_from_this(), child);
        return args[0];
    });
    I.node_method(any, "appendChild", 1, [](Realm& r, Node& self, Args args) {
        require_args(args, 1, "Node.appendChild");
        Node& node = node_argument(args, 0, "Node.appendChild");
        pre_insert(r, self, node.shared_from_this(), nullptr);
        return args[0];
    });
    I.node_method(any, "replaceChild", 2, [](Realm& r, Node& self, Args args) {
        require_args(args, 2, "Node.replaceChild");
        Node& node = node_argument(args, 0, "Node.replaceChild");
        Node& child = node_argument(args, 1, "Node.replaceChild");
        if (!can_have_children(self)) throw_dom(r, "HierarchyRequestError", "This node type does not support this method.");
        if (node.is_host_including_inclusive_ancestor_of(self)) {
            throw_dom(r, "HierarchyRequestError", "The new child element contains the parent.");
        }
        if (script_parent(child).get() != &self) {
            throw_dom(r, "NotFoundError", "The node to be replaced is not a child of this node.");
        }
        if (node.is_document() || (node.is_text() && self.is_document()) ||
            (node.is_document_type() && !self.is_document())) {
            throw_dom(r, "HierarchyRequestError", "This node cannot be inserted here.");
        }
        if (self.is_document()) ensure_document_shape(r, self, node, &child, true);
        auto keep_node = node.shared_from_this();
        auto keep_child = child.shared_from_this();
        if (child.is_document_type() || node.is_document_type()) {
            // A doctype is not among the children: one goes out and the
            // other goes in where it stood.
            if (&node != &child) {
                std::shared_ptr<Node> after = script_sibling(child, +1);
                if (after == keep_node) after = script_sibling(node, +1);
                remove_node(r, child);
                insert_node(r, self, keep_node, after);
            }
            return args[1];
        }
        std::shared_ptr<Node> reference = child.next_sibling();
        if (reference == keep_node) reference = node.next_sibling();
        auto previous = child.previous_sibling();
        NodeVector added;
        if (node.is_document_fragment()) added = node.children();
        else added.push_back(keep_node);
        if (&node != &child) {
            ChildListBatch batch(r, self);
            self.remove_child(keep_child);
            if (reference) self.insert_before(keep_node, reference);
            else self.append_child(keep_node);
            batch.commit(added, {keep_child}, previous, reference);
        }
        after_insertion(r, added);
        return args[1];
    });
    I.node_method(any, "removeChild", 1, [](Realm& r, Node& self, Args args) {
        require_args(args, 1, "Node.removeChild");
        Node& child = node_argument(args, 0, "Node.removeChild");
        if (script_parent(child).get() != &self) {
            throw_dom(r, "NotFoundError", "The node to be removed is not a child of this node.");
        }
        remove_node(r, child);
        return args[0];
    });
}

// ---------------------------------------------------------------------------
// CharacterData, Text, Comment
// ---------------------------------------------------------------------------

void replace_data(Realm& realm, Node& node, std::size_t offset, std::size_t count, const std::string& data) {
    std::u16string text = units(character_data(node));
    if (offset > text.size()) {
        throw_dom(realm, "IndexSizeError", "The offset " + std::to_string(offset) + " is larger than the node's length (" +
                                           std::to_string(text.size()) + ").");
    }
    if (count > text.size() - offset) count = text.size() - offset;
    const std::u16string replacement = units(data);
    live_note_data_replaced(node, offset, count, replacement.size());
    text.replace(offset, count, replacement);
    store_character_data(node, utf8(text));
}

} // namespace

std::shared_ptr<Node> split_text_node(Realm& realm, Node& node, std::size_t offset) {
    const std::u16string data = units(character_data(node));
    if (offset > data.size()) {
        throw_dom(realm, "IndexSizeError", "The offset " + std::to_string(offset) + " is larger than the Text node's length.");
    }
    auto tail = std::make_shared<Text>(utf8(std::u16string_view(data).substr(offset)));
    if (auto document = document_of(realm, node)) tail->set_owner_document(document);
    if (auto parent = node.parent()) {
        auto next = node.next_sibling();
        if (next) parent->insert_before(tail, next);
        else parent->append_child(tail);
    }
    live_note_text_split(node, offset, tail);
    store_character_data(node, utf8(std::u16string_view(data).substr(0, offset)));
    return tail;
}

namespace {

void setup_character_data(Iface& I) {
    const auto cd = &is_character_data;
    I.node_accessor(cd, "data",
        [](Realm&, Node& self) { return str(character_data(self)); },
        [](Realm&, Node& self, const JsValue& value) { set_character_data(self, to_str_null_empty(value)); });
    I.node_getter(cd, "length", [](Realm&, Node& self) {
        return num(static_cast<double>(utf16_length(character_data(self))));
    });
    I.node_method(cd, "substringData", 2, [](Realm& r, Node& self, Args args) {
        require_args(args, 2, "CharacterData.substringData");
        const std::u16string text = units(character_data(self));
        const std::size_t offset = to_ulong(args[0]);
        std::size_t count = to_ulong(args[1]);
        if (offset > text.size()) {
            throw_dom(r, "IndexSizeError", "The offset " + std::to_string(offset) + " is larger than the node's length (" +
                                           std::to_string(text.size()) + ").");
        }
        if (count > text.size() - offset) count = text.size() - offset;
        return str(utf8(std::u16string_view(text).substr(offset, count)));
    });
    I.node_method(cd, "appendData", 1, [](Realm&, Node& self, Args args) {
        require_args(args, 1, "CharacterData.appendData");
        // Nothing a range points at moves when text is added at the end.
        store_character_data(self, character_data(self) + to_str(args[0]));
        return undefined();
    });
    I.node_method(cd, "insertData", 2, [](Realm& r, Node& self, Args args) {
        require_args(args, 2, "CharacterData.insertData");
        replace_data(r, self, to_ulong(args[0]), 0, to_str(args[1]));
        return undefined();
    });
    I.node_method(cd, "deleteData", 2, [](Realm& r, Node& self, Args args) {
        require_args(args, 2, "CharacterData.deleteData");
        replace_data(r, self, to_ulong(args[0]), to_ulong(args[1]), std::string());
        return undefined();
    });
    I.node_method(cd, "replaceData", 3, [](Realm& r, Node& self, Args args) {
        require_args(args, 3, "CharacterData.replaceData");
        replace_data(r, self, to_ulong(args[0]), to_ulong(args[1]), to_str(args[2]));
        return undefined();
    });
    define_child_node(I, cd);
    define_element_siblings(I, cd);
}

JsValue construct_text(Realm& realm, const InterfaceDef&, Args args, const JsValue*) {
    auto node = std::make_shared<Text>(js_arg(args, 0).is_undefined() ? std::string() : to_str(args[0]));
    if (realm.document) node->set_owner_document(realm.document);
    return wrap(realm, node);
}

JsValue construct_comment(Realm& realm, const InterfaceDef&, Args args, const JsValue*) {
    auto node = std::make_shared<Comment>(js_arg(args, 0).is_undefined() ? std::string() : to_str(args[0]));
    if (realm.document) node->set_owner_document(realm.document);
    return wrap(realm, node);
}

JsValue construct_fragment(Realm& realm, const InterfaceDef&, Args, const JsValue*) {
    auto node = std::make_shared<Node>(NodeType::DocumentFragment);
    if (realm.document) node->set_owner_document(realm.document);
    return wrap(realm, node);
}

void setup_text(Iface& I) {
    const auto text = &is_text_node;
    I.node_method(text, "splitText", 1, [](Realm& r, Node& self, Args args) {
        require_args(args, 1, "Text.splitText");
        return wrap(r, split_text_node(r, self, to_ulong(args[0])));
    });
    I.node_getter(text, "wholeText", [](Realm&, Node& self) {
        std::shared_ptr<Node> first = self.shared_from_this();
        for (auto before = first->previous_sibling(); before && before->is_text(); before = first->previous_sibling()) {
            first = before;
        }
        std::string whole;
        for (auto at = first; at && at->is_text(); at = at->next_sibling()) whole += character_data(*at);
        return str(whole);
    });
    I.node_getter(text, "assignedSlot", [](Realm&, Node&) { return null(); });
}

void setup_comment(Iface&) {}

// A CDATA section is a Text node with another name; everything it can do is
// on Text.prototype.
void setup_cdata_section(Iface&) {}

void setup_processing_instruction(Iface& I) {
    const auto instruction = &is_instruction_node;
    I.node_getter(instruction, "target", [](Realm&, Node& self) {
        return str(static_cast<ProcessingInstruction&>(self).target());
    });
    // The style sheet an <?xml-stylesheet?> names: none is ever loaded.
    I.node_getter(instruction, "sheet", [](Realm&, Node&) { return null(); });
}

void setup_fragment(Iface& I) {
    const auto fragment = &is_fragment_node;
    define_parent_node(I, fragment);
    I.node_method(fragment, "getElementById", 1, [](Realm& r, Node& self, Args args) {
        require_args(args, 1, "DocumentFragment.getElementById");
        const std::string id = to_str(args[0]);
        return wrap(r, id.empty() ? nullptr : self.descendant_with_id(id));
    });
}

void setup_doctype(Iface& I) {
    const auto type = &is_doctype_node;
    I.node_getter(type, "name", [](Realm&, Node& self) { return str(static_cast<DocumentTypeNode&>(self).name()); });
    I.node_getter(type, "publicId", [](Realm&, Node& self) { return str(static_cast<DocumentTypeNode&>(self).public_id()); });
    I.node_getter(type, "systemId", [](Realm&, Node& self) { return str(static_cast<DocumentTypeNode&>(self).system_id()); });
    define_child_node(I, type);
}

} // namespace

void register_node_interfaces() {
    register_interface({"Node", "EventTarget", setup_node, nullptr, 0, nullptr});
    register_interface({"CharacterData", "Node", setup_character_data, nullptr, 0, nullptr});
    register_interface({"Text", "CharacterData", setup_text, construct_text, 0, nullptr});
    register_interface({"Comment", "CharacterData", setup_comment, construct_comment, 0, nullptr});
    register_interface({"CDATASection", "Text", setup_cdata_section, nullptr, 0, nullptr});
    register_interface({"ProcessingInstruction", "CharacterData", setup_processing_instruction, nullptr, 0, nullptr});
    register_interface({"DocumentFragment", "Node", setup_fragment, construct_fragment, 0, nullptr});
    register_interface({"DocumentType", "Node", setup_doctype, nullptr, 0, nullptr});
}

} // namespace dom
} // namespace litebird
