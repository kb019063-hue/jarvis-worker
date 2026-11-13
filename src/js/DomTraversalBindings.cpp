// Walking and selecting parts of a tree: NodeFilter, TreeWalker,
// NodeIterator, Range and Selection.

#include "litebird/js/DomBindings.hpp"
#include "litebird/js/DomLive.hpp"
#include "litebird/dom/TreeWalk.hpp"

#include <algorithm>

namespace litebird {
namespace dom {

namespace {

// ---------------------------------------------------------------------------
// Filtering
// ---------------------------------------------------------------------------

constexpr int kAccept = 1;
constexpr int kReject = 2;
constexpr int kSkip = 3;

struct TraversalState : JsExotic {
    Realm* realm{nullptr};
    std::shared_ptr<Node> root;
    uint32_t what_to_show{0xFFFFFFFFu};
    JsValue filter;
    bool active{false};
    // TreeWalker
    std::shared_ptr<Node> current;
    // NodeIterator
    std::shared_ptr<Node> reference;
    bool before_reference{true};
    bool is_iterator{false};

    void trace(Heap& heap) override {
        heap.mark_value(filter);
        trace_node(heap, root.get());
        trace_node(heap, current.get());
        trace_node(heap, reference.get());
    }

    int accept(const std::shared_ptr<Node>& node) {
        if (active) throw_dom(*realm, "InvalidStateError", "The filter is already running.");
        int type = 0;
        switch (node->type()) {
            case NodeType::Element: type = 1; break;
            case NodeType::Text: type = 3; break;
            case NodeType::Comment: type = 8; break;
            case NodeType::Document: type = 9; break;
            case NodeType::DocumentType: type = 10; break;
            case NodeType::DocumentFragment: type = 11; break;
        }
        if (!(what_to_show & (1u << (type - 1)))) return kSkip;
        if (filter.is_nullish()) return kAccept;
        active = true;
        struct Reset {
            bool& flag;
            ~Reset() { flag = false; }
        } reset{active};
        JsValue result;
        if (filter.is_function()) {
            result = js_call(filter, JsValue(), {wrap(*realm, node)});
        } else {
            JsValue method = filter.get_property("acceptNode");
            if (!method.is_function()) js_throw_type_error("The NodeFilter has no acceptNode method.");
            result = js_call(method, filter, {wrap(*realm, node)});
        }
        return static_cast<int>(js_to_uint32(js_to_number(result)) & 0xFFFF);
    }
};

TraversalState& this_walker() {
    TraversalState& state = this_state<TraversalState>("TreeWalker");
    if (state.is_iterator) throw_illegal_invocation("TreeWalker");
    return state;
}

TraversalState& this_iterator() {
    TraversalState& state = this_state<TraversalState>("NodeIterator");
    if (!state.is_iterator) throw_illegal_invocation("NodeIterator");
    return state;
}

JsValue filter_value(const JsValue& value) {
    if (value.is_nullish()) return null();
    if (!value.is_object_like()) js_throw_type_error("The filter is not a function or an object.");
    return value;
}

JsValue traverse_children(Realm& r, TraversalState& state, bool first) {
    std::shared_ptr<Node> node = first ? state.current->first_child() : state.current->last_child();
    while (node) {
        const int result = state.accept(node);
        if (result == kAccept) {
            state.current = node;
            return wrap(r, node);
        }
        if (result == kSkip) {
            auto child = first ? node->first_child() : node->last_child();
            if (child) {
                node = child;
                continue;
            }
        }
        while (node) {
            auto sibling = first ? node->next_sibling() : node->previous_sibling();
            if (sibling) {
                node = sibling;
                break;
            }
            auto parent = node->parent();
            if (!parent || parent == state.root || parent == state.current) return null();
            node = parent;
        }
    }
    return null();
}

JsValue traverse_siblings(Realm& r, TraversalState& state, bool next) {
    std::shared_ptr<Node> node = state.current;
    if (node == state.root) return null();
    for (;;) {
        auto sibling = next ? node->next_sibling() : node->previous_sibling();
        while (sibling) {
            node = sibling;
            const int result = state.accept(node);
            if (result == kAccept) {
                state.current = node;
                return wrap(r, node);
            }
            sibling = next ? node->first_child() : node->last_child();
            if (result == kReject || !sibling) sibling = next ? node->next_sibling() : node->previous_sibling();
        }
        node = node->parent();
        if (!node || node == state.root) return null();
        if (state.accept(node) == kAccept) return null();
    }
}

void setup_tree_walker(Iface& I) {
    I.getter("root", [](Realm& r) { return wrap(r, this_walker().root); });
    I.getter("whatToShow", [](Realm&) { return num(this_walker().what_to_show); });
    I.getter("filter", [](Realm&) { return this_walker().filter; });
    I.accessor("currentNode",
        [](Realm& r) { return wrap(r, this_walker().current); },
        [](Realm&, const JsValue& value) {
            TraversalState& state = this_walker();
            auto node = node_of(value);
            if (!node) js_throw_type_error("Failed to set the 'currentNode' property on 'TreeWalker': the value is not a Node.");
            state.current = node;
        });
    I.method("parentNode", 0, [](Realm& r, Args) {
        TraversalState& state = this_walker();
        std::shared_ptr<Node> node = state.current;
        while (node && node != state.root) {
            node = node->parent();
            if (node && state.accept(node) == kAccept) {
                state.current = node;
                return wrap(r, node);
            }
        }
        return null();
    });
    I.method("firstChild", 0, [](Realm& r, Args) { return traverse_children(r, this_walker(), true); });
    I.method("lastChild", 0, [](Realm& r, Args) { return traverse_children(r, this_walker(), false); });
    I.method("nextSibling", 0, [](Realm& r, Args) { return traverse_siblings(r, this_walker(), true); });
    I.method("previousSibling", 0, [](Realm& r, Args) { return traverse_siblings(r, this_walker(), false); });
    I.method("previousNode", 0, [](Realm& r, Args) {
        TraversalState& state = this_walker();
        std::shared_ptr<Node> node = state.current;
        while (node != state.root) {
            auto sibling = node->previous_sibling();
            while (sibling) {
                node = sibling;
                int result = state.accept(node);
                while (result != kReject && node->last_child()) {
                    node = node->last_child();
                    result = state.accept(node);
                }
                if (result == kAccept) {
                    state.current = node;
                    return wrap(r, node);
                }
                sibling = node->previous_sibling();
            }
            if (node == state.root || !node->parent()) return null();
            node = node->parent();
            if (state.accept(node) == kAccept) {
                state.current = node;
                return wrap(r, node);
            }
        }
        return null();
    });
    I.method("nextNode", 0, [](Realm& r, Args) {
        TraversalState& state = this_walker();
        std::shared_ptr<Node> node = state.current;
        int result = kAccept;
        for (;;) {
            while (result != kReject && node->first_child()) {
                node = node->first_child();
                result = state.accept(node);
                if (result == kAccept) {
                    state.current = node;
                    return wrap(r, node);
                }
            }
            std::shared_ptr<Node> temporary = node;
            std::shared_ptr<Node> sibling;
            while (temporary) {
                if (temporary == state.root) return null();
                sibling = temporary->next_sibling();
                if (sibling) break;
                temporary = temporary->parent();
            }
            if (!sibling) return null();
            node = sibling;
            result = state.accept(node);
            if (result == kAccept) {
                state.current = node;
                return wrap(r, node);
            }
        }
    });
}

// The node after `node` in tree order, inside `root`.
std::shared_ptr<Node> following_in(const std::shared_ptr<Node>& node, const std::shared_ptr<Node>& root) {
    if (auto child = node->first_child()) return child;
    for (std::shared_ptr<Node> at = node; at && at != root; at = at->parent()) {
        if (auto sibling = at->next_sibling()) return sibling;
    }
    return nullptr;
}

std::shared_ptr<Node> preceding_in(const std::shared_ptr<Node>& node, const std::shared_ptr<Node>& root) {
    if (node == root) return nullptr;
    auto sibling = node->previous_sibling();
    if (!sibling) return node->parent();
    while (auto last = sibling->last_child()) sibling = last;
    return sibling;
}

JsValue iterate(Realm& r, TraversalState& state, bool next) {
    std::shared_ptr<Node> node = state.reference;
    bool before = state.before_reference;
    for (;;) {
        if (next) {
            if (!before) {
                node = following_in(node, state.root);
                if (!node) return null();
            } else {
                before = false;
            }
        } else {
            if (before) {
                node = preceding_in(node, state.root);
                if (!node) return null();
            } else {
                before = true;
            }
        }
        if (state.accept(node) == kAccept) break;
    }
    state.reference = node;
    state.before_reference = before;
    return wrap(r, node);
}

void setup_node_iterator(Iface& I) {
    I.getter("root", [](Realm& r) { return wrap(r, this_iterator().root); });
    I.getter("referenceNode", [](Realm& r) { return wrap(r, this_iterator().reference); });
    I.getter("pointerBeforeReferenceNode", [](Realm&) { return boolean(this_iterator().before_reference); });
    I.getter("whatToShow", [](Realm&) { return num(this_iterator().what_to_show); });
    I.getter("filter", [](Realm&) { return this_iterator().filter; });
    I.method("nextNode", 0, [](Realm& r, Args) { return iterate(r, this_iterator(), true); });
    I.method("previousNode", 0, [](Realm& r, Args) { return iterate(r, this_iterator(), false); });
    I.method("detach", 0, [](Realm&, Args) {
        this_iterator();
        return undefined();
    });
}

void setup_node_filter(Iface& I) {
    static const std::pair<const char*, double> kConstants[] = {
        {"FILTER_ACCEPT", 1}, {"FILTER_REJECT", 2}, {"FILTER_SKIP", 3}, {"SHOW_ALL", 4294967295.0},
        {"SHOW_ELEMENT", 1}, {"SHOW_ATTRIBUTE", 2}, {"SHOW_TEXT", 4}, {"SHOW_CDATA_SECTION", 8},
        {"SHOW_ENTITY_REFERENCE", 16}, {"SHOW_ENTITY", 32}, {"SHOW_PROCESSING_INSTRUCTION", 64},
        {"SHOW_COMMENT", 128}, {"SHOW_DOCUMENT", 256}, {"SHOW_DOCUMENT_TYPE", 512},
        {"SHOW_DOCUMENT_FRAGMENT", 1024}, {"SHOW_NOTATION", 2048},
    };
    for (const auto& entry : kConstants) I.constant(entry.first, entry.second);
}

// ---------------------------------------------------------------------------
// Range
// ---------------------------------------------------------------------------

struct Boundary {
    std::shared_ptr<Node> node;
    std::size_t offset{0};
};

std::size_t node_length(const Node& node) {
    if (is_character_data(node)) return utf16_length(character_data(node));
    if (node.is_document_type()) return 0;
    return node.children().size();
}

std::shared_ptr<Node> root_of(const std::shared_ptr<Node>& node) {
    std::shared_ptr<Node> at = node;
    while (auto parent = at->parent()) at = parent;
    return at;
}

bool is_inclusive_ancestor(const Node& ancestor, const Node& node) {
    for (std::shared_ptr<Node> at = const_cast<Node&>(node).shared_from_this(); at; at = at->parent()) {
        if (at.get() == &ancestor) return true;
    }
    return false;
}

// -1 when `a` is before `b`, 0 when they are the same point, 1 when after.
int compare_points(const Boundary& a, const Boundary& b) {
    if (a.node == b.node) return a.offset < b.offset ? -1 : (a.offset > b.offset ? 1 : 0);
    const int position = compare_document_position(*b.node, *a.node);       // where a is, seen from b
    if (position & 4) {
        // a follows b in tree order (or is inside it).
        return -compare_points(b, a);
    }
    if (position & 8) {
        // a contains b: which child of a holds b decides.
        std::shared_ptr<Node> child = b.node;
        while (child->parent() != a.node) child = child->parent();
        if (child->index_in(*a.node) < a.offset) return 1;
    }
    return -1;
}

struct RangeState : JsExotic {
    Realm* realm{nullptr};
    Boundary start;
    Boundary end;
    void trace(Heap& heap) override {
        trace_node(heap, start.node.get());
        trace_node(heap, end.node.get());
    }
    bool collapsed() const { return start.node == end.node && start.offset == end.offset; }
};

RangeState& this_range() {
    return this_state<RangeState>("Range");
}

// The ranges and iterators of a realm that script can still reach; see
// DomLive.hpp. Weak, so that a range lives as long as script holds it and no
// longer; what the collector has freed is cleared out as the lists grow.
struct LivePart : RealmPart {
    std::vector<std::weak_ptr<RangeState>> ranges;
    std::vector<std::weak_ptr<TraversalState>> iterators;
    std::size_t tidy_at{32};

    template <typename T> static void tidy(std::vector<std::weak_ptr<T>>& list) {
        list.erase(std::remove_if(list.begin(), list.end(), [](const std::weak_ptr<T>& entry) { return entry.expired(); }),
                   list.end());
    }
    void before_adding() {
        if (ranges.size() + iterators.size() < tidy_at) return;
        tidy(ranges);
        tidy(iterators);
        tidy_at = std::max<std::size_t>(32, (ranges.size() + iterators.size()) * 2);
    }
};

JsValue new_range(Realm& realm, const Boundary& start, const Boundary& end, const JsValue* derived = nullptr) {
    auto state = std::make_shared<RangeState>();
    state->realm = &realm;
    state->start = start;
    state->end = end;
    LivePart& live = realm.part<LivePart>();
    live.before_adding();
    live.ranges.push_back(state);
    return realm.make("Range", state, derived);
}

void set_boundary(Realm& r, RangeState& state, const std::shared_ptr<Node>& node, std::size_t offset, bool is_start) {
    if (node->is_document_type()) throw_dom(r, "InvalidNodeTypeError", "The node provided is a doctype.");
    if (offset > node_length(*node)) {
        throw_dom(r, "IndexSizeError", "The offset " + std::to_string(offset) + " is larger than the node's length (" +
                                       std::to_string(node_length(*node)) + ").");
    }
    const Boundary point{node, offset};
    if (is_start) {
        if (root_of(node) != root_of(state.end.node) || compare_points(point, state.end) > 0) state.end = point;
        state.start = point;
    } else {
        if (root_of(node) != root_of(state.start.node) || compare_points(point, state.start) < 0) state.start = point;
        state.end = point;
    }
}

std::shared_ptr<Node> common_ancestor(const RangeState& state) {
    std::shared_ptr<Node> at = state.start.node;
    while (at && !is_inclusive_ancestor(*at, *state.end.node)) at = at->parent();
    return at;
}

std::u16string units_of(const Node& node) { return js_utf8_to_utf16(character_data(node)); }

// True when `node` lies wholly inside the range.
bool contained(const RangeState& state, const std::shared_ptr<Node>& node) {
    return compare_points({node, 0}, state.start) > 0 && compare_points({node, node_length(*node)}, state.end) < 0;
}

enum class ContentsMode { Delete, Extract, Clone };

// What deleteContents, extractContents and cloneContents share: the DOM
// standard's "extract" with the parts that differ switched by `mode`.
std::shared_ptr<Node> process_contents(Realm& r, Boundary start, Boundary end, ContentsMode mode, Boundary* collapse_to) {
    js_check_stack();
    auto document = document_of(r, *start.node);
    auto fragment = std::make_shared<Node>(NodeType::DocumentFragment);
    if (document) fragment->set_owner_document(document);
    const bool keep = mode != ContentsMode::Delete;
    const bool change = mode != ContentsMode::Clone;
    if (start.node == end.node && start.offset == end.offset) return fragment;

    auto take_text = [&](const std::shared_ptr<Node>& node, std::size_t from, std::size_t to) {
        const std::u16string data = units_of(*node);
        from = std::min(from, data.size());
        to = std::min(std::max(to, from), data.size());
        if (keep) {
            auto copy = clone_node(r, *node, false, document);
            set_character_data(*copy, js_utf16_to_utf8(std::u16string_view(data).substr(from, to - from)));
            fragment->append_child(copy);
        }
        if (change) {
            std::u16string rest = data;
            rest.erase(from, to - from);
            live_note_data_replaced(*node, from, to - from, 0);
            store_character_data(*node, js_utf16_to_utf8(rest));
        }
    };

    if (start.node == end.node && is_character_data(*start.node)) {
        take_text(start.node, start.offset, end.offset);
        if (collapse_to) *collapse_to = start;
        return fragment;
    }

    // The common ancestor and its children that are partly inside.
    std::shared_ptr<Node> common = start.node;
    while (common && !is_inclusive_ancestor(*common, *end.node)) common = common->parent();
    if (!common) return fragment;
    std::shared_ptr<Node> first_partial, last_partial;
    if (!is_inclusive_ancestor(*start.node, *end.node)) {
        for (const auto& child : common->children()) {
            if (is_inclusive_ancestor(*child, *start.node)) {
                first_partial = child;
                break;
            }
        }
    }
    if (!is_inclusive_ancestor(*end.node, *start.node)) {
        for (const auto& child : common->children()) {
            if (is_inclusive_ancestor(*child, *end.node)) last_partial = child;
        }
    }
    RangeState probe;
    probe.start = start;
    probe.end = end;
    NodeVector whole;
    for (const auto& child : common->children()) {
        if (contained(probe, child)) whole.push_back(child);
    }

    // Where the range ends up once its contents are gone.
    Boundary after = start;
    if (!is_inclusive_ancestor(*start.node, *end.node)) {
        std::shared_ptr<Node> reference = start.node;
        while (reference->parent() && !is_inclusive_ancestor(*reference->parent(), *end.node)) reference = reference->parent();
        after = {reference->parent(), reference->index_in(*reference->parent()) + 1};
    }

    auto take_partial = [&](const std::shared_ptr<Node>& child, bool is_first) {
        const Boundary inner_start = is_first ? start : Boundary{child, 0};
        const Boundary inner_end = is_first ? Boundary{child, node_length(*child)} : end;
        if (is_character_data(*child)) {
            take_text(child, inner_start.offset, is_first ? node_length(*child) : inner_end.offset);
            return;
        }
        auto inner = process_contents(r, inner_start, inner_end, mode, nullptr);
        if (keep) {
            auto copy = clone_node(r, *child, false, document);
            copy->append_child(inner);
            fragment->append_child(copy);
        }
    };

    if (first_partial) take_partial(first_partial, true);
    for (const auto& child : whole) {
        if (mode == ContentsMode::Clone) fragment->append_child(clone_node(r, *child, true, document));
        else if (mode == ContentsMode::Extract) fragment->append_child(child);
        else if (auto parent = child->parent()) parent->remove_child(child);
    }
    if (last_partial) take_partial(last_partial, false);
    if (collapse_to) *collapse_to = after;
    return fragment;
}

std::string range_text(const RangeState& state) {
    if (state.collapsed()) return std::string();
    if (state.start.node == state.end.node && state.start.node->is_text()) {
        const std::u16string data = units_of(*state.start.node);
        const std::size_t from = std::min(state.start.offset, data.size());
        const std::size_t to = std::min(std::max(state.end.offset, from), data.size());
        return js_utf16_to_utf8(std::u16string_view(data).substr(from, to - from));
    }
    std::string out;
    if (state.start.node->is_text()) {
        const std::u16string data = units_of(*state.start.node);
        out += js_utf16_to_utf8(std::u16string_view(data).substr(std::min(state.start.offset, data.size())));
    }
    auto common = common_ancestor(state);
    if (common) {
        for_each_descendant(*common, [&](Node& node) {
            if (node.is_text() && &node != state.start.node.get() && &node != state.end.node.get() &&
                contained(state, node.shared_from_this())) {
                out += character_data(node);
            }
            return true;
        });
    }
    if (state.end.node->is_text()) {
        const std::u16string data = units_of(*state.end.node);
        out += js_utf16_to_utf8(std::u16string_view(data).substr(0, std::min(state.end.offset, data.size())));
    }
    return out;
}

std::shared_ptr<Node> range_node_argument(Args args, std::size_t index, const char* what) {
    return node_argument(args, index, what).shared_from_this();
}

JsValue construct_range(Realm& realm, const InterfaceDef&, Args, const JsValue* derived) {
    std::shared_ptr<Node> document = realm.document;
    if (!document) document = make_document(realm, false, nullptr);
    return new_range(realm, {document, 0}, {document, 0}, derived);
}

// A point a script names must exist: comparePoint() and isPointInRange().
void check_point(Realm& r, const Boundary& point) {
    if (point.node->is_document_type()) throw_dom(r, "InvalidNodeTypeError", "The node provided is a doctype.");
    if (point.offset > node_length(*point.node)) {
        throw_dom(r, "IndexSizeError", "The offset " + std::to_string(point.offset) + " is larger than the node's length (" +
                                       std::to_string(node_length(*point.node)) + ").");
    }
}

// Range.insertNode(): `node` goes in at the start of the range. A range that
// starts inside text splits the text there.
void insert_into_range(Realm& r, RangeState& state, const std::shared_ptr<Node>& node) {
    const std::shared_ptr<Node> start = state.start.node;
    if (start->is_comment() || (start->is_text() && !start->parent()) || start == node) {
        throw_dom(r, "HierarchyRequestError", "The node cannot be inserted at the start of this range.");
    }
    std::shared_ptr<Node> reference;
    if (start->is_text()) reference = start;
    else if (state.start.offset < start->children().size()) reference = start->children()[state.start.offset];
    const std::shared_ptr<Node> parent = reference ? reference->parent() : start;
    ensure_pre_insert_validity(r, *parent, *node, reference.get());
    if (start->is_text()) reference = split_text_node(r, *start, state.start.offset);
    if (node == reference) reference = reference->next_sibling();
    if (node->parent()) remove_node(r, *node);
    std::size_t offset = reference ? reference->index_in(*parent) : node_length(*parent);
    offset += node->is_document_fragment() ? node_length(*node) : 1;
    pre_insert(r, *parent, node, reference.get());
    if (state.collapsed()) state.end = {parent, offset};
}

void setup_range(Iface& I) {
    I.constant("START_TO_START", 0);
    I.constant("START_TO_END", 1);
    I.constant("END_TO_END", 2);
    I.constant("END_TO_START", 3);
    I.getter("startContainer", [](Realm& r) { return wrap(r, this_range().start.node); });
    I.getter("startOffset", [](Realm&) { return num(static_cast<double>(this_range().start.offset)); });
    I.getter("endContainer", [](Realm& r) { return wrap(r, this_range().end.node); });
    I.getter("endOffset", [](Realm&) { return num(static_cast<double>(this_range().end.offset)); });
    I.getter("collapsed", [](Realm&) { return boolean(this_range().collapsed()); });
    I.getter("commonAncestorContainer", [](Realm& r) { return wrap(r, common_ancestor(this_range())); });
    I.method("setStart", 2, [](Realm& r, Args args) {
        require_args(args, 2, "Range.setStart");
        set_boundary(r, this_range(), range_node_argument(args, 0, "Range.setStart"), to_ulong(args[1]), true);
        return undefined();
    });
    I.method("setEnd", 2, [](Realm& r, Args args) {
        require_args(args, 2, "Range.setEnd");
        set_boundary(r, this_range(), range_node_argument(args, 0, "Range.setEnd"), to_ulong(args[1]), false);
        return undefined();
    });
    auto relative = [](Iface& iface, const char* name, bool is_start, bool after) {
        iface.method(name, 1, [name, is_start, after](Realm& r, Args args) {
            require_args(args, 1, name);
            auto node = range_node_argument(args, 0, name);
            auto parent = node->parent();
            if (!parent) throw_dom(r, "InvalidNodeTypeError", "The node provided has no parent.");
            set_boundary(r, this_range(), parent, node->index_in(*parent) + (after ? 1 : 0), is_start);
            return undefined();
        });
    };
    relative(I, "setStartBefore", true, false);
    relative(I, "setStartAfter", true, true);
    relative(I, "setEndBefore", false, false);
    relative(I, "setEndAfter", false, true);
    I.method("collapse", 0, [](Realm&, Args args) {
        RangeState& state = this_range();
        if (to_bool(js_arg(args, 0))) state.end = state.start;
        else state.start = state.end;
        return undefined();
    });
    I.method("selectNode", 1, [](Realm& r, Args args) {
        require_args(args, 1, "Range.selectNode");
        RangeState& state = this_range();
        auto node = range_node_argument(args, 0, "Range.selectNode");
        auto parent = node->parent();
        if (!parent) throw_dom(r, "InvalidNodeTypeError", "The node provided has no parent.");
        const std::size_t index = node->index_in(*parent);
        state.start = {parent, index};
        state.end = {parent, index + 1};
        return undefined();
    });
    I.method("selectNodeContents", 1, [](Realm& r, Args args) {
        require_args(args, 1, "Range.selectNodeContents");
        RangeState& state = this_range();
        auto node = range_node_argument(args, 0, "Range.selectNodeContents");
        if (node->is_document_type()) throw_dom(r, "InvalidNodeTypeError", "The node provided is a doctype.");
        state.start = {node, 0};
        state.end = {node, node_length(*node)};
        return undefined();
    });
    I.method("compareBoundaryPoints", 2, [](Realm& r, Args args) {
        require_args(args, 2, "Range.compareBoundaryPoints");
        RangeState& state = this_range();
        const uint32_t how = to_ulong(args[0]);
        RangeState* other = state_of<RangeState>(args[1]);
        if (!other) js_throw_type_error("Failed to execute 'compareBoundaryPoints' on 'Range': parameter 2 is not of type 'Range'.");
        if (how > 3) throw_dom(r, "NotSupportedError", "The comparison method provided must be one of the four constants.");
        if (root_of(state.start.node) != root_of(other->start.node)) {
            throw_dom(r, "WrongDocumentError", "The two ranges are in different trees.");
        }
        const Boundary& mine = (how == 0 || how == 3) ? state.start : state.end;
        const Boundary& theirs = (how == 0 || how == 1) ? other->start : other->end;
        return num(compare_points(mine, theirs));
    });
    I.method("comparePoint", 2, [](Realm& r, Args args) {
        require_args(args, 2, "Range.comparePoint");
        RangeState& state = this_range();
        auto node = range_node_argument(args, 0, "Range.comparePoint");
        if (root_of(node) != root_of(state.start.node)) throw_dom(r, "WrongDocumentError", "The node is in a different tree.");
        const Boundary point{node, to_ulong(args[1])};
        check_point(r, point);
        if (compare_points(point, state.start) < 0) return num(-1);
        if (compare_points(point, state.end) > 0) return num(1);
        return num(0);
    });
    I.method("isPointInRange", 2, [](Realm& r, Args args) {
        require_args(args, 2, "Range.isPointInRange");
        RangeState& state = this_range();
        auto node = range_node_argument(args, 0, "Range.isPointInRange");
        if (root_of(node) != root_of(state.start.node)) return boolean(false);
        const Boundary point{node, to_ulong(args[1])};
        check_point(r, point);
        return boolean(compare_points(point, state.start) >= 0 && compare_points(point, state.end) <= 0);
    });
    I.method("intersectsNode", 1, [](Realm&, Args args) {
        require_args(args, 1, "Range.intersectsNode");
        RangeState& state = this_range();
        auto node = range_node_argument(args, 0, "Range.intersectsNode");
        if (root_of(node) != root_of(state.start.node)) return boolean(false);
        auto parent = node->parent();
        if (!parent) return boolean(true);
        const std::size_t index = node->index_in(*parent);
        return boolean(compare_points({parent, index}, state.end) < 0 && compare_points({parent, index + 1}, state.start) > 0);
    });
    I.method("cloneRange", 0, [](Realm& r, Args) {
        RangeState& state = this_range();
        return new_range(r, state.start, state.end);
    });
    I.method("detach", 0, [](Realm&, Args) {
        this_range();
        return undefined();
    });
    I.method("toString", 0, [](Realm&, Args) { return str(range_text(this_range())); });
    I.method("deleteContents", 0, [](Realm& r, Args) {
        RangeState& state = this_range();
        Boundary after = state.start;
        process_contents(r, state.start, state.end, ContentsMode::Delete, &after);
        state.start = state.end = after;
        return undefined();
    });
    I.method("extractContents", 0, [](Realm& r, Args) {
        RangeState& state = this_range();
        Boundary after = state.start;
        auto fragment = process_contents(r, state.start, state.end, ContentsMode::Extract, &after);
        state.start = state.end = after;
        return wrap(r, fragment);
    });
    I.method("cloneContents", 0, [](Realm& r, Args) {
        RangeState& state = this_range();
        return wrap(r, process_contents(r, state.start, state.end, ContentsMode::Clone, nullptr));
    });
    I.method("insertNode", 1, [](Realm& r, Args args) {
        require_args(args, 1, "Range.insertNode");
        RangeState& state = this_range();
        insert_into_range(r, state, range_node_argument(args, 0, "Range.insertNode"));
        return undefined();
    });
    I.method("surroundContents", 1, [](Realm& r, Args args) {
        require_args(args, 1, "Range.surroundContents");
        RangeState& state = this_range();
        auto parent = range_node_argument(args, 0, "Range.surroundContents");
        // A node the range cuts through cannot be moved into the new parent
        // in one piece, unless it is text, which is split.
        const Boundary start = state.start, end = state.end;
        std::shared_ptr<Node> common = start.node;
        while (common && !is_inclusive_ancestor(*common, *end.node)) common = common->parent();
        for (const Boundary* side : {&start, &end}) {
            for (std::shared_ptr<Node> at = side->node; at && at != common; at = at->parent()) {
                if (!at->is_text()) throw_dom(r, "InvalidStateError", "The Range has partially selected a non-Text node.");
            }
        }
        if (parent->is_document() || parent->is_document_type() || parent->is_document_fragment()) {
            throw_dom(r, "InvalidNodeTypeError", "The node provided cannot be the parent of what a range holds.");
        }
        Boundary after = start;
        auto fragment = process_contents(r, start, end, ContentsMode::Extract, &after);
        state.start = state.end = after;
        replace_all_children(r, *parent, {});
        insert_into_range(r, state, parent);
        pre_insert(r, *parent, fragment, nullptr);
        if (auto holder = parent->parent()) {
            const std::size_t index = parent->index_in(*holder);
            state.start = {holder, index};
            state.end = {holder, index + 1};
        }
        return undefined();
    });
    I.method("createContextualFragment", 1, [](Realm& r, Args args) {
        require_args(args, 1, "Range.createContextualFragment");
        RangeState& state = this_range();
        std::shared_ptr<Node> node = state.start.node;
        std::shared_ptr<Element> context;
        if (node->is_element()) context = std::static_pointer_cast<Element>(node);
        else if (node->parent() && node->parent()->is_element()) context = std::static_pointer_cast<Element>(node->parent());
        if (!context || (!context->is_foreign() && context->tag_name() == "html")) {
            context = std::make_shared<Element>("body");
            if (auto document = document_of(r, *node)) context->set_owner_document(document);
        }
        return wrap(r, parse_fragment(r, *context, to_str(args[0])));
    });
    I.method("getBoundingClientRect", 0, [](Realm& r, Args) {
        this_range();
        return js_construct(r.ctor("DOMRect"), {});
    });
    I.method("getClientRects", 0, [](Realm&, Args) {
        this_range();
        return JsValue::make_array();
    });
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

struct SelectionState : JsExotic {
    /// The one range of the selection, or undefined.
    JsValue range;
    void trace(Heap& heap) override { heap.mark_value(range); }
    RangeState* state() { return state_of<RangeState>(range); }
};

SelectionState& this_selection() {
    return this_state<SelectionState>("Selection");
}

void setup_selection(Iface& I) {
    I.getter("rangeCount", [](Realm&) { return num(this_selection().state() ? 1 : 0); });
    I.getter("isCollapsed", [](Realm&) {
        RangeState* range = this_selection().state();
        return boolean(!range || range->collapsed());
    });
    I.getter("type", [](Realm&) {
        RangeState* range = this_selection().state();
        return str(!range ? "None" : (range->collapsed() ? "Caret" : "Range"));
    });
    I.getter("anchorNode", [](Realm& r) {
        RangeState* range = this_selection().state();
        return range ? wrap(r, range->start.node) : null();
    });
    I.getter("anchorOffset", [](Realm&) {
        RangeState* range = this_selection().state();
        return num(range ? static_cast<double>(range->start.offset) : 0);
    });
    I.getter("focusNode", [](Realm& r) {
        RangeState* range = this_selection().state();
        return range ? wrap(r, range->end.node) : null();
    });
    I.getter("focusOffset", [](Realm&) {
        RangeState* range = this_selection().state();
        return num(range ? static_cast<double>(range->end.offset) : 0);
    });
    I.method("getRangeAt", 1, [](Realm& r, Args args) {
        require_args(args, 1, "Selection.getRangeAt");
        SelectionState& selection = this_selection();
        if (to_ulong(args[0]) != 0 || !selection.state()) {
            throw_dom(r, "IndexSizeError", "The index is not a valid range index.");
        }
        return selection.range;
    });
    I.method("addRange", 1, [](Realm&, Args args) {
        require_args(args, 1, "Selection.addRange");
        SelectionState& selection = this_selection();
        if (!state_of<RangeState>(args[0])) js_throw_type_error("Failed to execute 'addRange' on 'Selection': parameter 1 is not of type 'Range'.");
        if (!selection.state()) selection.range = args[0];
        return undefined();
    });
    I.method("removeRange", 1, [](Realm& r, Args args) {
        require_args(args, 1, "Selection.removeRange");
        SelectionState& selection = this_selection();
        if (!selection.state() || !strict_equals(selection.range, args[0])) {
            throw_dom(r, "NotFoundError", "The range is not part of the selection.");
        }
        selection.range = JsValue();
        return undefined();
    });
    I.method("removeAllRanges", 0, [](Realm&, Args) {
        this_selection().range = JsValue();
        return undefined();
    });
    I.alias("empty", "removeAllRanges");
    I.method("collapse", 1, [](Realm& r, Args args) {
        SelectionState& selection = this_selection();
        if (js_arg(args, 0).is_nullish()) {
            selection.range = JsValue();
            return undefined();
        }
        auto node = range_node_argument(args, 0, "Selection.collapse");
        const std::size_t offset = to_ulong(js_arg(args, 1));
        if (offset > node_length(*node)) throw_dom(r, "IndexSizeError", "The offset is larger than the node's length.");
        selection.range = new_range(r, {node, offset}, {node, offset});
        return undefined();
    });
    I.alias("setPosition", "collapse");
    I.method("collapseToStart", 0, [](Realm& r, Args) {
        RangeState* range = this_selection().state();
        if (!range) throw_dom(r, "InvalidStateError", "There is no selection.");
        range->end = range->start;
        return undefined();
    });
    I.method("collapseToEnd", 0, [](Realm& r, Args) {
        RangeState* range = this_selection().state();
        if (!range) throw_dom(r, "InvalidStateError", "There is no selection.");
        range->start = range->end;
        return undefined();
    });
    I.method("selectAllChildren", 1, [](Realm& r, Args args) {
        require_args(args, 1, "Selection.selectAllChildren");
        SelectionState& selection = this_selection();
        auto node = range_node_argument(args, 0, "Selection.selectAllChildren");
        selection.range = new_range(r, {node, 0}, {node, node->children().size()});
        return undefined();
    });
    I.method("setBaseAndExtent", 4, [](Realm& r, Args args) {
        require_args(args, 4, "Selection.setBaseAndExtent");
        SelectionState& selection = this_selection();
        Boundary anchor{range_node_argument(args, 0, "Selection.setBaseAndExtent"), to_ulong(args[1])};
        Boundary focus{range_node_argument(args, 2, "Selection.setBaseAndExtent"), to_ulong(args[3])};
        if (anchor.offset > node_length(*anchor.node) || focus.offset > node_length(*focus.node)) {
            throw_dom(r, "IndexSizeError", "An offset is larger than its node's length.");
        }
        if (compare_points(anchor, focus) <= 0) selection.range = new_range(r, anchor, focus);
        else selection.range = new_range(r, focus, anchor);
        return undefined();
    });
    I.method("containsNode", 1, [](Realm&, Args args) {
        require_args(args, 1, "Selection.containsNode");
        RangeState* range = this_selection().state();
        if (!range) return boolean(false);
        auto node = range_node_argument(args, 0, "Selection.containsNode");
        if (root_of(node) != root_of(range->start.node)) return boolean(false);
        if (!to_bool(js_arg(args, 1))) return boolean(contained(*range, node));
        // Partly inside: the selection starts before the node ends and ends
        // after it starts.
        return boolean(compare_points(range->start, {node, node_length(*node)}) <= 0 &&
                       compare_points(range->end, {node, 0}) >= 0);
    });
    I.method("deleteFromDocument", 0, [](Realm& r, Args) {
        if (RangeState* range = this_selection().state()) {
            Boundary after = range->start;
            process_contents(r, range->start, range->end, ContentsMode::Delete, &after);
            range->start = range->end = after;
        }
        return undefined();
    });
    I.method("toString", 0, [](Realm&, Args) {
        RangeState* range = this_selection().state();
        return str(range ? range_text(*range) : std::string());
    });
}

} // namespace

JsValue selection_object(Realm& realm) {
    if (!realm.window.is_object()) return null();
    return cached(realm.window, "selection", [&]() { return realm.make("Selection", std::make_shared<SelectionState>()); });
}

/// document.createRange(), createTreeWalker() and createNodeIterator().
JsValue create_range(Realm& realm, const std::shared_ptr<Node>& document) {
    return new_range(realm, {document, 0}, {document, 0});
}

JsValue create_traversal(Realm& realm, bool iterator, const std::shared_ptr<Node>& root, uint32_t what_to_show,
                         const JsValue& filter) {
    auto state = std::make_shared<TraversalState>();
    state->realm = &realm;
    state->root = root;
    state->what_to_show = what_to_show;
    state->filter = filter_value(filter);
    state->current = root;
    state->reference = root;
    state->is_iterator = iterator;
    if (iterator) {
        LivePart& live = realm.part<LivePart>();
        live.before_adding();
        live.iterators.push_back(state);
    }
    return realm.make(iterator ? "NodeIterator" : "TreeWalker", state);
}

// ---------------------------------------------------------------------------
// Following the tree (DomLive.hpp)
// ---------------------------------------------------------------------------

namespace {

LivePart* live_part_for(const Node& node) {
    Realm* realm = realm_if_any(node);
    if (!realm || !realm->alive) return nullptr;
    LivePart* live = realm->find_part<LivePart>();
    return live && !(live->ranges.empty() && live->iterators.empty()) ? live : nullptr;
}

// Every boundary of every range that is still alive. Ranges the collector
// has freed since the last time are dropped from the list here, so that a
// page that made a hundred thousand of them pays for that once and not at
// every change to the tree afterwards.
template <typename Fn> void each_boundary(LivePart& live, Fn&& fn) {
    bool freed = false;
    for (const auto& entry : live.ranges) {
        if (auto range = entry.lock()) {
            fn(range->start);
            fn(range->end);
        } else {
            freed = true;
        }
    }
    if (freed) LivePart::tidy(live.ranges);
}

// The top of the tree `node` is in. After a removal that is the removed node
// for everything that was under it, which is how "was this inside what went
// away" is asked with one walk up and no walk down.
const Node* top_of(const Node& node) {
    const Node* top = &node;
    std::shared_ptr<Node> hold;
    while (auto parent = top->parent()) {
        hold = parent;
        top = hold.get();
    }
    return top;
}

bool one_of(const NodeVector& nodes, const Node* node) {
    for (const auto& candidate : nodes) {
        if (candidate.get() == node) return true;
    }
    return false;
}

} // namespace

void live_note_children_changed(Node& parent, const NodeVector& added, const NodeVector& removed,
                                const std::shared_ptr<Node>& previous, const std::shared_ptr<Node>& next) {
    LivePart* live = live_part_for(parent);
    if (!live) return;
    const std::shared_ptr<Node> self = parent.shared_from_this();
    // The index where it happened. Found only when something asks: it is a
    // search among the siblings, and most changes concern no range at all.
    std::size_t index = 0;
    bool have_index = false;
    auto where = [&]() {
        if (!have_index) index = previous ? previous->index_in(parent) + 1 : 0;
        have_index = true;
        return index;
    };

    if (!removed.empty()) {
        each_boundary(*live, [&](Boundary& boundary) {
            if (boundary.node.get() == &parent) {
                // A step back for each child removed before it, and never
                // further back than where they were.
                if (boundary.offset > 0 && boundary.offset > where()) {
                    const std::size_t stepped = boundary.offset > removed.size() ? boundary.offset - removed.size() : 0;
                    boundary.offset = std::max(where(), stepped);
                }
            } else if (one_of(removed, top_of(*boundary.node))) {
                boundary = Boundary{self, where()};
            }
        });
        bool freed = false;
        for (const auto& entry : live->iterators) {
            auto iterator = entry.lock();
            if (!iterator) freed = true;
            if (!iterator || !iterator->reference || !iterator->root) continue;
            if (!one_of(removed, top_of(*iterator->reference))) continue;
            // Only what was removed from inside the iterator's root concerns it.
            if (!is_inclusive_ancestor(*iterator->root, parent)) continue;
            if (iterator->before_reference) {
                // On to the first node after what was removed, if the root has one.
                std::shared_ptr<Node> following = next;
                for (std::shared_ptr<Node> at = self; !following && at && at != iterator->root; at = at->parent()) {
                    following = at->next_sibling();
                }
                if (following) {
                    iterator->reference = following;
                    continue;
                }
                iterator->before_reference = false;
            }
            // Back to the last node before it.
            std::shared_ptr<Node> last = previous;
            while (last && !last->children().empty()) last = last->children().back();
            iterator->reference = last ? last : self;
        }
        if (freed) LivePart::tidy(live->iterators);
    }
    if (!added.empty()) {
        each_boundary(*live, [&](Boundary& boundary) {
            if (boundary.node.get() == &parent && boundary.offset > 0 && boundary.offset > where()) {
                boundary.offset += added.size();
            }
        });
    }
}

void live_note_data_replaced(Node& node, std::size_t offset, std::size_t count, std::size_t added) {
    LivePart* live = live_part_for(node);
    if (!live) return;
    each_boundary(*live, [&](Boundary& boundary) {
        if (boundary.node.get() != &node || boundary.offset <= offset) return;
        // Inside what is replaced: to its start. After it: moved by the difference.
        if (boundary.offset <= offset + count) boundary.offset = offset;
        else boundary.offset = boundary.offset - count + added;
    });
}

void live_note_data_set(Node& node) {
    LivePart* live = live_part_for(node);
    if (!live) return;
    each_boundary(*live, [&](Boundary& boundary) {
        if (boundary.node.get() == &node) boundary.offset = 0;
    });
}

void live_note_text_split(Node& node, std::size_t offset, const std::shared_ptr<Node>& tail) {
    LivePart* live = live_part_for(node);
    if (!live) return;
    const std::shared_ptr<Node> parent = node.parent();
    std::size_t after = 0;
    bool have_after = false;
    each_boundary(*live, [&](Boundary& boundary) {
        if (boundary.node.get() == &node) {
            if (boundary.offset > offset) {
                boundary.node = tail;
                boundary.offset -= offset;
            }
        } else if (parent && boundary.node == parent) {
            // Just after the node that was split is now after both halves.
            // (Inserting the second half moved the boundaries further on.)
            if (!have_after) after = node.index_in(*parent) + 1;
            have_after = true;
            if (boundary.offset == after) ++boundary.offset;
        }
    });
}

void live_note_text_merged(Node& gone, const std::shared_ptr<Node>& into, std::size_t length) {
    LivePart* live = live_part_for(gone);
    if (!live) return;
    const std::shared_ptr<Node> parent = gone.parent();
    std::size_t index = 0;
    bool have_index = false;
    each_boundary(*live, [&](Boundary& boundary) {
        if (boundary.node.get() == &gone) {
            boundary.node = into;
            boundary.offset += length;
        } else if (parent && boundary.node == parent) {
            if (!have_index) index = gone.index_in(*parent);
            have_index = true;
            if (boundary.offset == index) boundary = Boundary{into, length};
        }
    });
}

void register_traversal_interfaces() {
    register_interface({"NodeFilter", nullptr, setup_node_filter, nullptr, 0, nullptr});
    register_interface({"TreeWalker", nullptr, setup_tree_walker, nullptr, 0, nullptr});
    register_interface({"NodeIterator", nullptr, setup_node_iterator, nullptr, 0, nullptr});
    register_interface({"AbstractRange", nullptr, nullptr, nullptr, 0, nullptr});
    register_interface({"Range", "AbstractRange", setup_range, construct_range, 0, nullptr});
    register_interface({"Selection", nullptr, setup_selection, nullptr, 0, nullptr});
}

} // namespace dom
} // namespace litebird
