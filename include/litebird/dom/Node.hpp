#pragma once

#include "litebird/dom/Event.hpp"
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <functional>

namespace litebird {

class Element;
class Text;
class Document;

enum class NodeType {
    Document,
    Element,
    Text,
    Comment,
    DocumentFragment
};

using EventListener = std::function<void(const std::shared_ptr<Event>&)>;

class Node : public std::enable_shared_from_this<Node> {
public:
    explicit Node(NodeType type)
        : m_type(type), m_is_connected(type == NodeType::Document) {}
    /// Releases the children without recursion: a tree of any depth is
    /// destroyed in a loop, so dropping the last reference to a deep tree
    /// cannot overflow the native stack. See Node.cpp.
    virtual ~Node();

    NodeType type() const { return m_type; }
    bool is_document() const { return m_type == NodeType::Document; }
    bool is_element() const { return m_type == NodeType::Element; }
    bool is_text() const { return m_type == NodeType::Text; }
    bool is_comment() const { return m_type == NodeType::Comment; }
    bool is_document_fragment() const { return m_type == NodeType::DocumentFragment; }

    std::shared_ptr<Node> parent() const { return m_parent.lock(); }
    void set_parent(std::shared_ptr<Node> p);

    const std::vector<std::shared_ptr<Node>>& children() const { return m_children; }
    std::vector<std::shared_ptr<Node>>& children() { return m_children; }

    std::shared_ptr<Node> first_child() const { return m_children.empty() ? nullptr : m_children.front(); }
    std::shared_ptr<Node> last_child() const { return m_children.empty() ? nullptr : m_children.back(); }
    std::shared_ptr<Node> previous_sibling() const;
    std::shared_ptr<Node> next_sibling() const;

    bool is_connected() const { return m_is_connected; }

    void append_child(std::shared_ptr<Node> child);
    void remove_child(std::shared_ptr<Node> child);
    void insert_before(std::shared_ptr<Node> new_child, std::shared_ptr<Node> ref_child);

    /// True when this node is `other`, contains it, or hosts a shadow tree
    /// that contains it (the DOM standard's "host-including inclusive
    /// ancestor"). Inserting a node under something it contains would make it
    /// its own ancestor; append_child and insert_before refuse that, and the
    /// script bindings throw HierarchyRequestError for it.
    ///
    /// A node without children and without a shadow tree answers at once, so
    /// building a tree one new node at a time costs nothing here.
    bool is_host_including_inclusive_ancestor_of(const Node& other) const;

    // Searches over the descendants of this node, in tree order. None of them
    // recurses, so they work on a tree of any depth. A "simple selector" is
    // `#id`, `.class`, a tag name or `*`.
    std::shared_ptr<Element> descendant_with_id(const std::string& id) const;
    std::vector<std::shared_ptr<Element>> descendants_with_tag(const std::string& tag) const;
    std::vector<std::shared_ptr<Element>> descendants_matching(const std::string& simple_selector) const;
    std::shared_ptr<Element> first_descendant_matching(const std::string& simple_selector) const;

    // Invalidation / dirty tracking
    bool needs_layout() const { return m_needs_layout; }
    bool child_needs_layout() const { return m_child_needs_layout; }
    void mark_needs_layout();
    void clear_layout_flags() {
        m_needs_layout = false;
        m_child_needs_layout = false;
    }

    // EventTarget implementation
    struct EventListenerEntry {
        EventListener listener;
        bool use_capture{false};
        /// Identifies the callback so that it can be removed again, and so
        /// that adding the same callback twice has no effect. Null for
        /// listeners that are never removed individually.
        const void* key{nullptr};
    };

    void add_event_listener(const std::string& type, EventListener listener, bool use_capture = false,
                            const void* key = nullptr);
    /// Removes the listener registered with this type, key and capture flag.
    void remove_event_listener(const std::string& type, const void* key, bool use_capture = false);
    void remove_event_listeners(const std::string& type);
    bool dispatch_event(const std::shared_ptr<Event>& event);
    bool has_listeners(const std::string& type) const {
        auto it = m_listeners.find(type);
        return it != m_listeners.end() && !it->second.empty();
    }

    /// The children of this node as markup: elements and text, in tree
    /// order, written into one buffer by one walk. What innerHTML returns.
    std::string inner_markup() const;

    /// The text of every Text node below this node, in tree order. A Text or
    /// Comment node answers with its own data.
    virtual std::string text_content() const;

    /// Writes this node and what is below it, one node per line, indented by
    /// depth. Does not recurse. Nesting deeper than kMaxDumpDepth levels below
    /// this node is not written out (every line would carry that much
    /// indentation, so the dump of a chain of N nodes would be N*N bytes): one
    /// line says how many nodes were left out.
    void dump(std::ostream& os, int indent = 0) const;
    static constexpr int kMaxDumpDepth = 1024;

    Element* as_element();
    const Element* as_element() const;
    Text* as_text();
    const Text* as_text() const;
    Document* as_document();
    const Document* as_document() const;

protected:
    /// This node's own line in dump(). `has_content` says whether nodes will
    /// be written inside it.
    virtual void dump_open(std::ostream& os, const std::string& indent, bool has_content) const;
    /// The line that ends what dump_open() started. Called only when
    /// `has_content` was true.
    virtual void dump_close(std::ostream& os, const std::string& indent) const;

    // Connectivity is maintained as the tree is attached/detached. Keeping it
    // cached makes append_child's connectedness check O(1) instead of walking
    // every ancestor for every parser insertion.
    void set_connected_state_subtree(bool connected);

    NodeType m_type;
    bool m_is_connected{false};
    std::weak_ptr<Node> m_parent;
    std::vector<std::shared_ptr<Node>> m_children;
    std::unordered_map<std::string, std::vector<EventListenerEntry>> m_listeners;
    bool m_needs_layout{true};
    bool m_child_needs_layout{true};
};

} // namespace litebird
