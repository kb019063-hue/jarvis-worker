#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <functional>
#include <cstdint>
#include <mutex>

namespace litebird {

class Heap;
class JsValue;
class Node;

class HeapObject {
public:
    bool is_marked{false};
    HeapObject* next_cell{nullptr};

    virtual ~HeapObject() = default;
    virtual void trace(Heap& heap) = 0;
};

class JsObject : public HeapObject {
public:
    JsObject* prototype{nullptr};
    std::unordered_map<std::string, JsValue> properties;
    std::vector<JsValue> array_elements;
    bool is_array{false};

    // Set when this JsObject is the script-visible identity of a DOM node.
    // Holding a strong reference here means the wrapper keeps its node alive, which
    // is what makes `document.body` stable across accesses and across collections.
    std::shared_ptr<Node> dom_node;

    void trace(Heap& heap) override;
};

struct GCStats {
    size_t total_cells{0};
    size_t alive_cells{0};
    size_t collected_cells{0};
    size_t bytes_allocated{0};
    size_t collections_count{0};
};

class Heap {
public:
    Heap();
    ~Heap();

    static Heap& default_heap();

    JsObject* allocate_object();

    GCStats collect_garbage(const std::vector<JsValue*>& roots = {});

    void register_root(JsValue* val);
    void unregister_root(JsValue* val);

    // --- Root-set providers -------------------------------------------------
    //
    // A `JsObject` is a raw pointer with no owner, so the collector has to be
    // told, explicitly, everything that can reach one. Values reachable only
    // through a closure environment, a promise callback or a queued microtask
    // live in `std::function` captures that the collector cannot see, which is
    // why a collection with an incomplete root set frees live objects.
    //
    // A provider is a callback that pushes the addresses of its live JsValues.
    // Providers must stay valid for as long as they are registered.
    using RootProvider = std::function<void(std::vector<JsValue*>&)>;

    void add_root_provider(const void* owner, RootProvider provider);
    void remove_root_provider(const void* owner);

    /// Runs every registered provider. The supplied roots are appended to.
    void collect_roots(std::vector<JsValue*>& roots) const;

    const GCStats& stats() const { return m_stats; }

    void mark_value(const JsValue& val);
    void mark_object(HeapObject* obj);

private:
    void sweep();

    struct RootProviderEntry {
        const void* owner;
        RootProvider provider;
    };

    mutable std::mutex m_mutex;
    std::vector<RootProviderEntry> m_root_providers;
    HeapObject* m_head{nullptr};
    std::vector<HeapObject*> m_gray_stack;
    std::vector<JsValue*> m_registered_roots;
    GCStats m_stats;
};

} // namespace litebird
