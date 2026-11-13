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
    DocumentFragment,
    /// `<!DOCTYPE html>` as a script sees it (`document.doctype`). Such a node
    /// is never among anybody's children(): the document keeps the doctype in
    /// a field, and the bindings show it between the right two children.
    DocumentType
};

/// What the tree tells the script bindings. A member that is null is skipped,
/// so a program with no scripting pays one test per event.
struct DomHooks {
    /// A node that has a script object was put under `parent`, which has
    /// none. The bindings keep one rule about script objects (a node that has
    /// one, its parent has one), and this is where it would break.
    void (*wrapper_needs_parent)(Node& parent, const Node& child) = nullptr;
    /// Native code dispatched `event` at `target`. True when the bindings ran
    /// the dispatch (script listeners included); `result` is then what
    /// dispatchEvent returns. False: do the native dispatch.
    bool (*dispatch)(Node& target, const std::shared_ptr<Event>& event, bool& result) = nullptr;
    /// Native code asked for a click, focus or blur. True when dealt with.
    bool (*click)(Element& element) = nullptr;
    bool (*focus)(Element& element, bool focus) = nullptr;

    // What changed, for mutation observers and custom elements. Each is
    // called after the change has been made.
    void (*children_changed)(Node& parent, const std::vector<std::shared_ptr<Node>>& added,
                             const std::vector<std::shared_ptr<Node>>& removed,
                             const std::shared_ptr<Node>& previous, const std::shared_ptr<Node>& next) = nullptr;
    /// `old_value` is null for an attribute that was added, `new_value` for
    /// one that was removed.
    void (*attribute_changed)(Element& element, const std::string& name, const std::string* old_value,
                              const std::string* new_value) = nullptr;
    void (*character_data_changed)(Node& node, const std::string& old_value) = nullptr;
    /// An element entered (true) or left (false) a document.
    void (*connected)(Element& element, bool connected) = nullptr;
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
    bool is_document_type() const { return m_type == NodeType::DocumentType; }

    /// The document this node belongs to, connected or not: the one that
    /// created it, or the one it was last inserted into. Empty for a node
    /// nobody has told (a tree built by hand); the bindings then answer with
    /// the page's document.
    std::shared_ptr<Document> owner_document() const { return m_owner.lock(); }
    /// Sets the owner of this node and of everything below it (and inside its
    /// shadow tree). A node and its descendants always have one owner, so the
    /// walk stops where the owner is already `owner`.
    void set_owner_document(const std::shared_ptr<Document>& owner);

    // --- Script object --------------------------------------------------------
    //
    // The object a script holds for this node, if one has been made: an opaque
    // pointer to the heap cell and the number of the realm it belongs to. The
    // cell owns the node (a shared_ptr); the node only points back, and the
    // cell clears the pointer when the collector frees it. See DomBindings.hpp.
    void* script_object() const { return m_script_object; }
    uint64_t script_realm() const { return m_script_realm; }
    void set_script_object(void* cell, uint64_t realm) { m_script_object = cell; m_script_realm = realm; }

    static DomHooks& hooks();

    /// A number that grows with every change to any tree: children added or
    /// removed, an attribute set or removed, a form control's state changed.
    /// Live collections remember the number they were computed at and compute
    /// again when it has moved.
    static uint64_t tree_version() { return s_tree_version; }
    static void bump_tree_version() { ++s_tree_version; }

    std::shared_ptr<Node> parent() const { return m_parent.lock(); }
    void set_parent(std::shared_ptr<Node> p);

    const std::vector<std::shared_ptr<Node>>& children() const { return m_children; }
    std::vector<std::shared_ptr<Node>>& children() { return m_children; }

    std::shared_ptr<Node> first_child() const { return m_children.empty() ? nullptr : m_children.front(); }
    std::shared_ptr<Node> last_child() const { return m_children.empty() ? nullptr : m_children.back(); }
    std::shared_ptr<Node> previous_sibling() const;
    std::shared_ptr<Node> next_sibling() const;
    /// This node's index among the children of `parent`; their number when
    /// it is not one of them.
    std::size_t index_in(const Node& parent) const;

    bool is_connected() const { return m_is_connected; }

    void append_child(std::shared_ptr<Node> child);
    void remove_child(std::shared_ptr<Node> child);
    void insert_before(std::shared_ptr<Node> new_child, std::shared_ptr<Node> ref_child);

    /// Removes every child in one step and returns them, in order. Each of
    /// them ends up with no parent; observers get one record that lists them
    /// all, and custom elements among them are told they were disconnected.
    ///
    /// Removing the children one at a time with remove_child() moves every
    /// remaining child down a place each time, which is quadratic in their
    /// number; emptying a list of 100,000 items took seconds that way.
    std::vector<std::shared_ptr<Node>> take_children();

    /// Removes every child, then appends `nodes` in order. What setting
    /// textContent does, with one text node or none.
    void replace_children(const std::vector<std::shared_ptr<Node>>& nodes);

    /// True when this node is `other`, contains it, or hosts a shadow tree
    /// that contains it (the DOM standard's "host-including inclusive
    /// ancestor"). Inserting a node under something it contains would make it
    /// its own ancestor; append_child and insert_before refuse that, and the
    /// script bindings throw HierarchyRequestError for it.
    ///
    /// A node without children and without a shadow tree answers at once, so
    /// building a tree one new node at a time costs nothing here. Otherwise
    /// the cost is the smaller of the depth of `other` and the size of the
    /// subtree of this node.
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
    /// The dispatch without script: capture, target and bubble over the
    /// listeners added with add_event_listener. What dispatch_event does when
    /// no bindings are attached.
    bool dispatch_event_natively(const std::shared_ptr<Event>& event);
    /// Runs this node's own native listeners for `event`: at the target all of
    /// them, otherwise those of the phase given. For the bindings' dispatch,
    /// which decides the path and the phases itself.
    void run_native_listeners(const std::shared_ptr<Event>& event, bool capture_phase, bool at_target);
    bool has_native_listeners() const { return !m_listeners.empty(); }
    bool has_listeners(const std::string& type) const {
        auto it = m_listeners.find(type);
        return it != m_listeners.end() && !it->second.empty();
    }

    /// The children of this node as markup, written into one buffer by one
    /// walk, by the HTML standard's serializing algorithm. What innerHTML
    /// returns; for a <template> it is the template's contents.
    std::string inner_markup() const;
    /// This node and what is below it as markup: outerHTML. With `xml`, the
    /// way XMLSerializer writes an HTML tree.
    std::string outer_markup(bool xml = false) const;

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
    std::weak_ptr<Document> m_owner;
    void* m_script_object{nullptr};
    uint64_t m_script_realm{0};
    /// Where this node was among its parent's children the last time anybody
    /// looked. Checked before it is used; it makes a walk along siblings
    /// (`for (n = first; n; n = n.nextSibling)`) one step per node instead
    /// of a search of the whole list per node.
    mutable uint32_t m_sibling_hint{0};
    static inline uint64_t s_tree_version = 1;
    std::vector<std::shared_ptr<Node>> m_children;
    std::unordered_map<std::string, std::vector<EventListenerEntry>> m_listeners;
    bool m_needs_layout{true};
    bool m_child_needs_layout{true};
};

} // namespace litebird
