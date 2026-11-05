#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <functional>
#include <cstdint>

namespace litebird {

class Heap;
class JsValue;

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

    const GCStats& stats() const { return m_stats; }

    void mark_value(const JsValue& val);
    void mark_object(HeapObject* obj);

private:
    void sweep();

    HeapObject* m_head{nullptr};
    std::vector<HeapObject*> m_gray_stack;
    std::vector<JsValue*> m_registered_roots;
    GCStats m_stats;
};

} // namespace litebird
