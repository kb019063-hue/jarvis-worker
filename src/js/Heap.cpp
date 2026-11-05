#include "litebird/js/Heap.hpp"
#include "litebird/js/JsValue.hpp"

namespace litebird {

void JsObject::trace(Heap& heap) {
    if (prototype) {
        heap.mark_object(prototype);
    }
    for (auto& [k, v] : properties) {
        heap.mark_value(v);
    }
    for (auto& elem : array_elements) {
        heap.mark_value(elem);
    }
}

Heap::Heap() = default;

Heap::~Heap() {
    HeapObject* curr = m_head;
    while (curr) {
        HeapObject* next = curr->next_cell;
        delete curr;
        curr = next;
    }
    m_head = nullptr;
}

Heap& Heap::default_heap() {
    static Heap s_heap;
    return s_heap;
}

JsObject* Heap::allocate_object() {
    auto* obj = new JsObject();
    obj->is_marked = false;
    obj->next_cell = m_head;
    m_head = obj;

    m_stats.total_cells++;
    m_stats.alive_cells++;
    m_stats.bytes_allocated += sizeof(JsObject);

    return obj;
}

void Heap::mark_value(const JsValue& val) {
    if (val.object_val) {
        mark_object(val.object_val);
    }
}

void Heap::mark_object(HeapObject* obj) {
    if (!obj || obj->is_marked) return;
    obj->is_marked = true;
    m_gray_stack.push_back(obj);
}

void Heap::register_root(JsValue* val) {
    if (val) {
        m_registered_roots.push_back(val);
    }
}

void Heap::unregister_root(JsValue* val) {
    for (auto it = m_registered_roots.begin(); it != m_registered_roots.end(); ++it) {
        if (*it == val) {
            m_registered_roots.erase(it);
            break;
        }
    }
}

GCStats Heap::collect_garbage(const std::vector<JsValue*>& roots) {
    m_stats.collections_count++;
    m_gray_stack.clear();

    // 1. Mark phase
    for (const auto* root : roots) {
        if (root) mark_value(*root);
    }
    for (const auto* root : m_registered_roots) {
        if (root) mark_value(*root);
    }

    // Process gray worklist
    while (!m_gray_stack.empty()) {
        HeapObject* obj = m_gray_stack.back();
        m_gray_stack.pop_back();
        obj->trace(*this);
    }

    // 2. Sweep phase
    sweep();

    return m_stats;
}

void Heap::sweep() {
    HeapObject** curr = &m_head;
    size_t alive = 0;
    size_t collected = 0;

    while (*curr) {
        HeapObject* cell = *curr;
        if (!cell->is_marked) {
            *curr = cell->next_cell;
            delete cell;
            collected++;
        } else {
            cell->is_marked = false;
            curr = &cell->next_cell;
            alive++;
        }
    }

    m_stats.alive_cells = alive;
    m_stats.collected_cells += collected;
    m_stats.total_cells = alive;
    m_stats.bytes_allocated = alive * sizeof(JsObject);
}

} // namespace litebird
