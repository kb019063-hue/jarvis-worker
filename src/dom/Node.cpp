#include "litebird/dom/Node.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/dom/FormState.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/common/ReleaseQueue.hpp"
#include "litebird/html/HtmlParser.hpp"
#include "litebird/render/Canvas2D.hpp"
#include <iomanip>
#include <unordered_set>

namespace litebird {

// Destruction.
//
// A node owns its children, so the compiler-generated destructor destroys a
// tree the way a recursive walk visits it: the root's destructor releases its
// children, whose destructors release theirs, one set of native frames per
// level. For a tree a script built two hundred thousand levels deep that is a
// stack overflow, at the moment the script lets go of it -- an assignment, the
// end of a function -- and a destructor has no way to stop early or to report
// anything.
//
// So a dying node does not destroy its children in place. The first node to
// die on a thread opens a queue; it and every node that dies while the queue
// is open hand their child lists to it, and the first node empties the queue
// in a loop before it returns. However deep the tree, the native stack holds
// one destructor for the node that started it and one for the node currently
// being destroyed. (See ReleaseQueue.hpp: the same scheme frees chains of
// closures, promises and syntax trees.)
//
// The other things a node can own that are nodes themselves -- an element's
// shadow root and a <template>'s contents -- need nothing extra: each is a
// Node, so its own children go through here.
Node::~Node() {
    if (m_children.empty()) return;
    using Pending = ReleaseQueue<std::vector<std::shared_ptr<Node>>>;
    if (Pending::busy()) {
        Pending::defer(std::move(m_children));
        return;
    }
    Pending::Drain drain;
    // Destroyed before `drain` (declared after it): each child that dies here
    // finds the queue open and defers its own children, and `drain` then works
    // through them one list at a time.
    std::vector<std::shared_ptr<Node>> children = std::move(m_children);
}

DomHooks& Node::hooks() {
    static DomHooks s_hooks;
    return s_hooks;
}

void Node::set_owner_document(const std::shared_ptr<Document>& owner) {
    auto same = [&](const Node& node) {
        return !node.m_owner.owner_before(owner) && !owner.owner_before(node.m_owner);
    };
    if (same(*this)) return;
    // No recursion: a detached subtree can be as deep as memory allows.
    std::vector<Node*> pending{this};
    while (!pending.empty()) {
        Node* node = pending.back();
        pending.pop_back();
        if (same(*node)) continue;
        node->m_owner = owner;
        for (const auto& child : node->m_children) pending.push_back(child.get());
        if (node->is_element()) {
            if (const auto& shadow = static_cast<Element*>(node)->m_shadow_root) pending.push_back(shadow.get());
        }
    }
}

namespace {

// A node belongs to the document it is inserted into. `parent` knows its
// owner unless it is part of a tree nobody has told (see owner_document()).
void adopt_child(Node& parent, Node& child) {
    std::shared_ptr<Document> owner = parent.is_document()
        ? std::static_pointer_cast<Document>(parent.shared_from_this()) : parent.owner_document();
    if (owner) child.set_owner_document(owner);
}

// One rule the script bindings rely on; see DomHooks::wrapper_needs_parent.
inline void keep_wrapper_rule(Node& parent, const Node& child) {
    if (child.script_object() && !parent.script_object()) {
        if (auto hook = Node::hooks().wrapper_needs_parent) hook(parent, child);
    }
}

} // namespace

std::size_t Node::index_in(const Node& parent) const {
    const auto& ch = parent.children();
    if (m_sibling_hint < ch.size() && ch[m_sibling_hint].get() == this) return m_sibling_hint;
    for (std::size_t i = 0; i < ch.size(); ++i) {
        if (ch[i].get() == this) {
            m_sibling_hint = static_cast<uint32_t>(i);
            return i;
        }
    }
    return ch.size();
}

std::shared_ptr<Node> Node::previous_sibling() const {
    auto p = parent();
    if (!p) return nullptr;
    const auto& ch = p->children();
    const std::size_t i = index_in(*p);
    if (i == 0 || i >= ch.size()) return nullptr;
    ch[i - 1]->m_sibling_hint = static_cast<uint32_t>(i - 1);
    return ch[i - 1];
}

std::shared_ptr<Node> Node::next_sibling() const {
    auto p = parent();
    if (!p) return nullptr;
    const auto& ch = p->children();
    const std::size_t i = index_in(*p);
    if (i + 1 >= ch.size()) return nullptr;
    ch[i + 1]->m_sibling_hint = static_cast<uint32_t>(i + 1);
    return ch[i + 1];
}

void Node::set_parent(std::shared_ptr<Node> p) {
    m_parent = p;
    set_connected_state_subtree(p && p->is_connected());
}

void Node::set_connected_state_subtree(bool connected) {
    // Use an explicit worklist. A detached subtree may already be deeply nested
    // when attached to a document, and connectivity bookkeeping must not add
    // another native-recursive walk to that operation.
    std::vector<Node*> pending{this};
    while (!pending.empty()) {
        Node* node = pending.back();
        pending.pop_back();
        if (node->m_is_connected == connected) continue;
        node->m_is_connected = connected;

        for (const auto& child : node->m_children) pending.push_back(child.get());

        // A shadow root is connected through its host, not through the ordinary
        // parent chain. Include it when the host enters or leaves a document.
        if (node->is_element()) {
            auto element = static_cast<Element*>(node);
            if (auto root = element->shadow_root()) pending.push_back(root.get());
        }
    }
}

// Every walk over the tree (textContent, serialisation, selectors, layout,
// and the destructor) assumes there are no loops. With one, they never
// return. The DOM standard forbids an insertion that would make a node its
// own ancestor (HierarchyRequestError); the script bindings throw that, and
// append_child/insert_before refuse the insertion for every other caller.
bool Node::is_host_including_inclusive_ancestor_of(const Node& other) const {
    if (this == &other) return true;
    // A node with no children and no shadow tree is nobody's ancestor. That is
    // every node the HTML parser appends and every node a script appends right
    // after creating it, so building a tree pays nothing for this check. (It
    // used to walk to the root for each appendChild from script, which made
    // building a chain of N nodes cost N*N steps.)
    bool can_be_ancestor = !m_children.empty();
    if (!can_be_ancestor && is_element()) {
        can_be_ancestor = static_cast<const Element*>(this)->m_shadow_root != nullptr;
    }
    if (!can_be_ancestor) return false;

    // Two searches for the same answer take turns, one step each: up from
    // `other`, looking for this node, and down through this node's subtree,
    // looking for `other`. The first to finish decides. Alone, the walk up
    // costs the depth of `other` and the search down costs the size of this
    // subtree; together they cost twice the smaller of the two. So a script
    // that appends small pieces to the bottom of a deep tree (a fragment with
    // one element in it, two hundred thousand times) pays for the pieces, not
    // for the depth, and a script that hangs a large tree under a new node
    // pays one step.
    //
    // Both follow the same links in opposite directions: child and parent,
    // shadow root and host.
    struct Frame {
        const Node* node;
        std::size_t next;       // index of the next child to look at
        bool shadow_seen;       // the node's shadow root has been looked at
    };
    std::vector<Frame> open;
    open.push_back({this, 0, false});

    std::shared_ptr<Node> keep;             // keeps the node `at` points to alive
    const Node* at = &other;
    for (;;) {
        // One step up.
        std::shared_ptr<Node> up = at->parent();
        if (!up) {
            auto* shadow = dynamic_cast<const ShadowRoot*>(at);
            if (!shadow) return false;
            up = shadow->host();
            if (!up) return false;
        }
        if (up.get() == this) return true;
        keep = std::move(up);
        at = keep.get();

        // One step down.
        if (open.empty()) return false;     // everything below this node has been seen
        Frame& top = open.back();
        const Node* found = nullptr;
        if (!top.shadow_seen) {
            top.shadow_seen = true;
            if (top.node->is_element()) {
                found = static_cast<const Element*>(top.node)->m_shadow_root.get();
            }
        }
        if (!found) {
            if (top.next >= top.node->m_children.size()) {
                open.pop_back();
                continue;
            }
            found = top.node->m_children[top.next++].get();
            if (!found) continue;
        }
        if (found == &other) return true;
        // `top` is not used after this: push_back may move the frames.
        open.push_back({found, 0, false});
    }
}

std::vector<std::shared_ptr<Element>> subtree_elements(const std::shared_ptr<Node>& root) {
    std::vector<std::shared_ptr<Element>> elements;
    if (!root) return elements;
    if (root->is_element()) elements.push_back(std::static_pointer_cast<Element>(root));
    for_each_descendant(*root, [&](Node& node) {
        if (node.is_element()) elements.push_back(std::static_pointer_cast<Element>(node.shared_from_this()));
        return true;
    });
    return elements;
}

// Custom element reactions for a subtree that entered or left a document.
//
// The elements are listed before the first callback runs. A callback is
// script: it can append to, empty or move the subtree it is being told about,
// and a walk that reads the live child lists while that happens reads freed
// memory. The DOM standard has the same shape for the same reason: the nodes
// are determined at insertion, and their reactions are queued.
static void notify_subtree_connected(const std::shared_ptr<Node>& node, bool connected) {
    if (!node) return;
    const auto hook = Node::hooks().connected;
    if (!hook) return;
    auto notify = [&](const std::shared_ptr<Element>& element) { hook(*element, connected); };
    if (node->children().empty()) {
        // One node, nothing to list: every insertion the HTML parser makes.
        if (node->is_element()) notify(std::static_pointer_cast<Element>(node));
        return;
    }
    for (const auto& element : subtree_elements(node)) notify(element);
}

void Node::append_child(std::shared_ptr<Node> child) {
    if (!child) return;

    if (child->is_host_including_inclusive_ancestor_of(*this)) return;

    if (child->is_document_fragment()) {
        for (const auto& node : child->take_children()) append_child(node);
        return;
    }

    if (auto old_p = child->parent()) {
        old_p->remove_child(child);
    }

    auto prev = m_children.empty() ? nullptr : m_children.back();
    adopt_child(*this, *child);
    child->set_parent(shared_from_this());
    m_children.push_back(child);
    mark_needs_layout();
    bump_tree_version();
    keep_wrapper_rule(*this, *child);

    if (auto hook = hooks().children_changed) hook(*this, {child}, {}, prev, nullptr);

    if (is_connected()) {
        notify_subtree_connected(child, true);
    }
}

void Node::insert_before(std::shared_ptr<Node> new_child, std::shared_ptr<Node> ref_child) {
    if (!new_child) return;
    if (!ref_child) {
        append_child(new_child);
        return;
    }

    if (new_child->is_host_including_inclusive_ancestor_of(*this)) return;

    if (new_child->is_document_fragment()) {
        for (const auto& node : new_child->take_children()) insert_before(node, ref_child);
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
    adopt_child(*this, *new_child);
    new_child->set_parent(shared_from_this());
    m_children.insert(it, new_child);
    mark_needs_layout();
    bump_tree_version();
    keep_wrapper_rule(*this, *new_child);

    if (auto hook = hooks().children_changed) hook(*this, {new_child}, {}, prev, ref_child);

    if (is_connected()) {
        notify_subtree_connected(new_child, true);
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
        bump_tree_version();

        if (auto hook = hooks().children_changed) hook(*this, {}, {removed}, prev, next);

        // disconnectedCallback is for a node that leaves a document. Taking a
        // node out of a tree that was never in one is not that.
        if (is_connected()) notify_subtree_connected(removed, false);
    }
}

std::vector<std::shared_ptr<Node>> Node::take_children() {
    std::vector<std::shared_ptr<Node>> removed;
    removed.swap(m_children);
    if (removed.empty()) return removed;
    for (const auto& child : removed) child->set_parent(nullptr);
    mark_needs_layout();
    bump_tree_version();

    if (auto hook = hooks().children_changed) hook(*this, {}, removed, nullptr, nullptr);

    if (is_connected()) {
        for (const auto& child : removed) notify_subtree_connected(child, false);
    }
    return removed;
}

void Node::replace_children(const std::vector<std::shared_ptr<Node>>& nodes) {
    take_children();
    for (const auto& node : nodes) append_child(node);
}

void Node::add_event_listener(const std::string& type, EventListener listener, bool use_capture,
                              const void* key) {
    auto& entries = m_listeners[type];
    if (key) {
        // The same callback registered twice for the same phase is one listener.
        for (const auto& entry : entries) {
            if (entry.key == key && entry.use_capture == use_capture) return;
        }
    }
    entries.push_back({std::move(listener), use_capture, key});
}

void Node::remove_event_listener(const std::string& type, const void* key, bool use_capture) {
    if (!key) return;
    auto it = m_listeners.find(type);
    if (it == m_listeners.end()) return;
    auto& entries = it->second;
    for (auto entry = entries.begin(); entry != entries.end(); ++entry) {
        if (entry->key == key && entry->use_capture == use_capture) {
            entries.erase(entry);
            break;
        }
    }
    if (entries.empty()) m_listeners.erase(it);
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

    // With script bindings attached there is one dispatch for everybody: it
    // runs the script listeners and the native ones below, in one pass.
    if (auto hook = hooks().dispatch) {
        bool result = true;
        if (hook(*this, event, result)) return result;
    }
    return dispatch_event_natively(event);
}

void Node::run_native_listeners(const std::shared_ptr<Event>& event, bool capture_phase, bool at_target) {
    auto it = m_listeners.find(event->type());
    if (it == m_listeners.end()) return;
    auto copy = it->second;
    for (const auto& entry : copy) {
        if (!at_target && entry.use_capture != capture_phase) continue;
        js_run_guarded([&] { entry.listener(event); });
        if (event->propagation_stopped()) return;
    }
}

bool Node::dispatch_event_natively(const std::shared_ptr<Event>& event) {
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
                    js_run_guarded([&] { entry.listener(event); });
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
            js_run_guarded([&] { entry.listener(event); });
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
                        js_run_guarded([&] { entry.listener(event); });
                        if (event->propagation_stopped()) return !event->default_prevented();
                    }
                }
            }
        }
    }

    return !event->default_prevented();
}

// One buffer, filled by one walk. The first version returned a fresh string
// per subtree, which copied all of a node's text once per ancestor; the second
// appended into one buffer but called itself once per level, which a deep
// tree turns into a stack overflow.
//
// Only Text nodes count. Comment data is not part of an element's text (an
// earlier override in Element included it).
std::string Node::text_content() const {
    std::string text;
    for_each_descendant(*this, [&](Node& node) {
        if (node.type() == NodeType::Text) text += static_cast<const Text&>(node).text();
        return true;
    });
    return text;
}

// --- dump -------------------------------------------------------------------

void Node::dump_open(std::ostream& os, const std::string& indent, bool) const {
    os << indent << "[Node type=" << static_cast<int>(m_type) << "]\n";
}

void Node::dump_close(std::ostream&, const std::string&) const {}

namespace {

// What dump() shows inside a node: an element's shadow root first, then the
// children.
const Node* dump_child_at(const Node& node, std::size_t index) {
    if (node.is_element()) {
        if (const auto& shadow = static_cast<const Element&>(node).m_shadow_root) {
            if (index == 0) return shadow.get();
            --index;
        }
    }
    const auto& children = node.children();
    return index < children.size() ? children[index].get() : nullptr;
}

} // namespace

void Node::dump(std::ostream& os, int indent) const {
    struct Frame {
        const Node* node;
        std::size_t next;
    };
    // open.size() - 1 is the depth, below this node, of the node on top.
    std::vector<Frame> open;
    std::string pad(static_cast<std::size_t>(std::max(indent, 0)) * 2, ' ');
    const std::size_t base = pad.size();
    auto pad_for = [&](std::size_t depth) -> const std::string& {
        pad.resize(base + depth * 2, ' ');
        return pad;
    };

    // Below kMaxDumpDepth nothing is written; the walk goes on only to count.
    const std::size_t max_depth = static_cast<std::size_t>(kMaxDumpDepth);
    std::size_t hidden_nodes = 0;
    std::size_t hidden_levels = 0;

    dump_open(os, pad_for(0), dump_child_at(*this, 0) != nullptr);
    open.push_back({this, 0});
    while (!open.empty()) {
        Frame& top = open.back();
        const Node* child = dump_child_at(*top.node, top.next);
        if (!child) {
            const Node* done = top.node;
            const std::size_t depth = open.size() - 1;
            const bool had_content = top.next > 0;
            open.pop_back();
            if (depth > max_depth) continue;
            if (depth == max_depth && hidden_nodes > 0) {
                os << pad_for(depth + 1) << "... " << hidden_nodes
                   << (hidden_nodes == 1 ? " node" : " nodes") << " not shown, nested up to "
                   << hidden_levels << (hidden_levels == 1 ? " level" : " levels") << " deeper\n";
                hidden_nodes = 0;
                hidden_levels = 0;
            }
            if (had_content) done->dump_close(os, pad_for(depth));
            continue;
        }
        ++top.next;
        const std::size_t depth = open.size();          // of `child`
        if (depth > max_depth) {
            ++hidden_nodes;
            hidden_levels = std::max(hidden_levels, depth - max_depth);
        } else {
            child->dump_open(os, pad_for(depth), dump_child_at(*child, 0) != nullptr);
        }
        // `top` is not used after this: push_back may move the frames.
        if (dump_child_at(*child, 0)) open.push_back({child, 0});
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

    const bool had_old = it != m_attributes.end();
    m_attributes[name] = value;
    mark_needs_layout();
    bump_tree_version();
    if (m_tag_name == "input" || m_tag_name == "option") forms::attribute_changed(*this, name, had_old, old_val);

    if (auto hook = hooks().attribute_changed) {
        // The element may be changed again by what the hook runs.
        auto keep = shared_from_this();
        hook(*this, name, had_old ? &old_val : nullptr, &value);
    }
}

void Element::remove_attribute(const std::string& name) {
    auto it = m_attributes.find(name);
    if (it == m_attributes.end()) return;
    std::string old_val = it->second;
    m_attributes.erase(it);
    mark_needs_layout();
    bump_tree_version();
    if (m_tag_name == "input" || m_tag_name == "option") forms::attribute_changed(*this, name, true, old_val);

    if (auto hook = hooks().attribute_changed) {
        auto keep = shared_from_this();
        hook(*this, name, &old_val, nullptr);
    }
}

void Element::focus() {
    if (auto hook = hooks().focus) {
        if (hook(*this, true)) return;
    }
    m_is_focused = true;
    dispatch_event(std::make_shared<Event>("focus", false, false));
}

void Element::blur() {
    if (auto hook = hooks().focus) {
        if (hook(*this, false)) return;
    }
    m_is_focused = false;
    dispatch_event(std::make_shared<Event>("blur", false, false));
}

bool Element::click() {
    if (auto hook = hooks().click) {
        if (hook(*this)) return true;
    }
    return dispatch_event(std::make_shared<Event>("click", true, true));
}

// --- serialization ------------------------------------------------------------

namespace {

bool is_void_tag(const std::string& tag) {
    static const std::unordered_set<std::string> VOID_TAGS = {
        "area", "base", "basefont", "bgsound", "br", "col", "embed", "frame", "hr", "img", "input",
        "keygen", "link", "meta", "param", "source", "track", "wbr"
    };
    return VOID_TAGS.count(tag) != 0;
}

// Elements whose text is written as it is: the parser reads it back raw.
bool is_raw_text_tag(const std::string& tag) {
    static const std::unordered_set<std::string> RAW = {
        "style", "script", "xmp", "iframe", "noembed", "noframes", "plaintext", "noscript"
    };
    return RAW.count(tag) != 0;
}

// "Escaping a string" from the HTML standard. U+00A0 is two bytes in UTF-8.
void append_escaped(const std::string& text, bool attribute_mode, std::string& out) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"' && attribute_mode) out += "&quot;";
        else if (static_cast<unsigned char>(c) == 0xC2 && i + 1 < text.size() &&
                 static_cast<unsigned char>(text[i + 1]) == 0xA0) {
            out += "&nbsp;";
            ++i;
        } else {
            out += c;
        }
    }
}

void append_start_tag(const Element& element, bool xml, bool declare_namespace, std::string& out) {
    out += '<';
    out += element.tag_name();
    if (declare_namespace) {
        out += " xmlns=\"";
        out += element.element_namespace() == ElementNamespace::SVG ? "http://www.w3.org/2000/svg"
             : element.element_namespace() == ElementNamespace::MathML ? "http://www.w3.org/1998/Math/MathML"
             : "http://www.w3.org/1999/xhtml";
        out += '"';
    }
    for (const auto& [name, value] : element.attributes()) {
        out += ' ';
        out += name;
        out += "=\"";
        append_escaped(value, true, out);
        out += '"';
    }
    (void)xml;
}

// Writes `root` (when `with_root`) and everything below it as markup into one
// buffer, in one walk: the HTML standard's fragment serializing algorithm.
// Attributes come in their own order, text and attribute values are escaped,
// a void element has no end tag, a <template> is written with its contents,
// and comments and the doctype are written too.
//
// (It used to write `<br />`, leave `&` and `<` in text as they were, drop
// comments and list attributes in hash order, so what innerHTML returned did
// not parse back to the same tree.)
//
// `xml` is XMLSerializer's variant for an HTML tree: the namespace is declared
// on the root and an element without children is written `<br />`.
//
// innerHTML used to be two functions calling each other, each returning its
// subtree as a new string: one pair of frames per level, and every level
// copied all the markup below it again. Reading it from the top of a chain of
// N elements cost N*N bytes of copying and, past some tens of thousands of
// levels, the stack.
void append_markup(const Node& root, bool with_root, bool xml, std::string& out) {
    struct Frame {
        const Node* node;       // the element to close when the children are done
        const Node* source;     // where the children are: the node, or a template's contents
        std::size_t next;
        bool raw_text;          // children that are text are written unescaped
    };
    std::vector<Frame> open;

    auto children_of = [](const Node& node) -> const Node* {
        if (node.is_element()) {
            const auto& element = static_cast<const Element&>(node);
            if (element.template_contents() && element.tag_name() == "template" && !element.is_foreign()) {
                return element.template_contents().get();
            }
        }
        return &node;
    };

    // Writes what comes before a node's children. Returns whether the node
    // has an end to write after them, that is, whether to go into it.
    auto enter = [&](const Node& node, bool raw_parent, bool is_root) {
        switch (node.type()) {
            case NodeType::Text:
                if (raw_parent && !xml) out += static_cast<const Text&>(node).text();
                else append_escaped(static_cast<const Text&>(node).text(), false, out);
                return false;
            case NodeType::Comment:
                out += "<!--";
                out += node.text_content();
                out += "-->";
                return false;
            case NodeType::DocumentType:
                out += "<!DOCTYPE ";
                out += static_cast<const DocumentTypeNode&>(node).name();
                out += '>';
                return false;
            case NodeType::Element: {
                const auto& element = static_cast<const Element&>(node);
                append_start_tag(element, xml, xml && is_root, out);
                const bool empty = children_of(element)->children().empty();
                if (xml) {
                    if (empty) { out += " />"; return false; }
                } else if (!element.is_foreign() && is_void_tag(element.tag_name())) {
                    out += '>';
                    return false;
                }
                out += '>';
                return true;
            }
            default:
                return false;
        }
    };
    auto leave = [&](const Node& node) {
        out += "</";
        out += static_cast<const Element&>(node).tag_name();
        out += '>';
    };
    auto raw_inside = [](const Node& node) {
        return node.is_element() && !static_cast<const Element&>(node).is_foreign() &&
               is_raw_text_tag(static_cast<const Element&>(node).tag_name());
    };

    if (with_root) {
        if (!enter(root, false, true)) return;
    }
    open.push_back({&root, children_of(root), 0, raw_inside(root)});
    while (!open.empty()) {
        Frame& top = open.back();
        const auto& children = top.source->children();
        if (top.next >= children.size()) {
            const Node* done = top.node;
            open.pop_back();
            if (with_root || !open.empty()) leave(*done);
            continue;
        }
        const Node* child = children[top.next++].get();
        // `top` is not used after this: push_back may move the frames.
        if (child && enter(*child, top.raw_text, false)) {
            open.push_back({child, children_of(*child), 0, raw_inside(*child)});
        }
    }
}

bool matches_simple_selector(const Element& element, const std::string& selector) {
    if (selector.starts_with("#")) return element.id() == selector.substr(1);
    if (selector.starts_with(".")) return element.has_class(selector.substr(1));
    return selector == "*" || element.tag_name() == selector;
}

// The elements below `root` for which `wanted` holds, in tree order; only the
// first one when `first_only`.
template <typename Wanted>
std::vector<std::shared_ptr<Element>> collect_descendants(const Node& root, bool first_only, Wanted&& wanted) {
    std::vector<std::shared_ptr<Element>> found;
    for_each_descendant(root, [&](Node& node) {
        if (!node.is_element()) return true;
        auto& element = static_cast<Element&>(node);
        if (!wanted(element)) return true;
        found.push_back(std::static_pointer_cast<Element>(element.shared_from_this()));
        return !first_only;
    });
    return found;
}

} // namespace

std::string Node::inner_markup() const {
    std::string out;
    append_markup(*this, false, false, out);
    return out;
}

std::string Node::outer_markup(bool xml) const {
    std::string out;
    if (m_type == NodeType::Document || m_type == NodeType::DocumentFragment) {
        // No tag of its own. A document's doctype is not among its children.
        if (m_type == NodeType::Document) {
            const auto& doctype = static_cast<const Document*>(this)->doctype();
            if (doctype.present) out += "<!DOCTYPE " + doctype.name + ">";
        }
        for (const auto& child : m_children) append_markup(*child, true, xml, out);
        return out;
    }
    append_markup(*this, true, xml, out);
    return out;
}

std::string Element::inner_html() const {
    return inner_markup();
}

std::string Element::outer_html() const {
    return outer_markup();
}

// --- searches -----------------------------------------------------------------

std::shared_ptr<Element> Node::descendant_with_id(const std::string& id) const {
    auto found = collect_descendants(*this, true, [&](const Element& element) { return element.id() == id; });
    return found.empty() ? nullptr : found.front();
}

std::vector<std::shared_ptr<Element>> Node::descendants_with_tag(const std::string& tag) const {
    return collect_descendants(*this, false, [&](const Element& element) {
        return tag == "*" || element.tag_name() == tag;
    });
}

std::vector<std::shared_ptr<Element>> Node::descendants_matching(const std::string& simple_selector) const {
    return collect_descendants(*this, false, [&](const Element& element) {
        return matches_simple_selector(element, simple_selector);
    });
}

std::shared_ptr<Element> Node::first_descendant_matching(const std::string& simple_selector) const {
    auto found = collect_descendants(*this, true, [&](const Element& element) {
        return matches_simple_selector(element, simple_selector);
    });
    return found.empty() ? nullptr : found.front();
}

std::vector<std::shared_ptr<Element>> Element::query_selector_all(const std::string& selector) const {
    return descendants_matching(selector);
}

std::shared_ptr<Element> Element::query_selector(const std::string& selector) const {
    return first_descendant_matching(selector);
}

void Element::set_inner_html(const std::string& html) {
    // Parsed as the content of this element, not of a <body>: in a <tr> or
    // a <table> "<td>x" has to become a cell. In a <body> the <td> is
    // dropped, and a table filled in this way came out as loose text.
    auto parsed = HtmlParser::parse_inner_html(*this, html);
    take_children();
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

void Element::dump_open(std::ostream& os, const std::string& indent, bool has_content) const {
    os << indent << "<" << m_tag_name;
    for (const auto& [k, v] : m_attributes) {
        os << " " << k << "=\"" << v << "\"";
    }
    os << (has_content ? ">\n" : " />\n");
}

void Element::dump_close(std::ostream& os, const std::string& indent) const {
    os << indent << "</" << m_tag_name << ">\n";
}

void Text::dump_open(std::ostream& os, const std::string& indent, bool) const {
    os << indent << "#text: \"";
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
