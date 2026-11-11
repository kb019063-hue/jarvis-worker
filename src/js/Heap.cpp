#include "litebird/js/Heap.hpp"
#include "litebird/js/JsValue.hpp"
#include <algorithm>

namespace litebird {

namespace {
Heap::EnvironmentTracer g_environment_tracer;
}

void JsObject::trace(Heap& heap) {
    if (prototype) {
        heap.mark_object(prototype);
    }
    for (auto& entry : properties) {
        heap.mark_value(entry.second);
        if (entry.setter) heap.mark_value(*entry.setter);
    }
    for (auto& elem : array_elements) {
        heap.mark_value(elem);
    }
    if (trace_extra) trace_extra(heap);
}

void Heap::set_environment_tracer(EnvironmentTracer tracer) {
    g_environment_tracer = tracer;
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
    std::lock_guard<std::mutex> lock(m_mutex);
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
    if (val.type == JsType::Function) {
        // A function keeps its own-property cell and the scopes it closed over.
        if (const auto* fd = val.function_data()) {
            if (fd->cell) mark_object(fd->cell);
            if (fd->closure) mark_environment(fd->closure.get());
        }
    }
    // Native functions keep their properties in a shared table, not a cell.
    if (const PropertyMap* map = val.loose_properties()) mark_property_map(map);
}

void Heap::mark_property_map(const PropertyMap* map) {
    if (!map || map->empty()) return;
    if (std::find(m_visited_maps.begin(), m_visited_maps.end(), map) != m_visited_maps.end()) return;
    m_visited_maps.push_back(map);
    m_gray_maps.push_back(map);
}

void Heap::mark_environment(void* env) {
    if (!env || !g_environment_tracer.enter) return;
    if (g_environment_tracer.enter(env, m_stats.collections_count)) m_gray_environments.push_back(env);
}

void Heap::mark_object(HeapObject* obj) {
    if (!obj || obj->is_marked) return;
    obj->is_marked = true;
    m_gray_stack.push_back(obj);
}

void Heap::register_root(JsValue* val) {
    if (val) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_registered_roots.push_back(val);
    }
}

void Heap::unregister_root(JsValue* val) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_registered_roots.begin(); it != m_registered_roots.end(); ++it) {
        if (*it == val) {
            m_registered_roots.erase(it);
            break;
        }
    }
}

void Heap::add_root_provider(const void* owner, RootProvider provider) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& entry : m_root_providers) {
        if (entry.owner == owner) {
            entry.provider = std::move(provider);
            return;
        }
    }
    m_root_providers.push_back({owner, std::move(provider)});
}

void Heap::remove_root_provider(const void* owner) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_root_providers.erase(
        std::remove_if(m_root_providers.begin(), m_root_providers.end(),
                       [owner](const RootProviderEntry& e) { return e.owner == owner; }),
        m_root_providers.end());
}

// Called with m_mutex already held by collect_garbage, so this must not lock.
void Heap::collect_roots(std::vector<JsValue*>& roots) const {
    for (const auto& entry : m_root_providers) {
        if (entry.provider) entry.provider(roots);
    }
}

GCStats Heap::collect_garbage(const std::vector<JsValue*>& roots) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.collections_count++;
    m_gray_stack.clear();
    m_gray_environments.clear();
    m_gray_maps.clear();
    m_visited_maps.clear();

    // Values held by registered owners (interpreter environments, pending
    // microtasks, ...) are roots too. Without this, a collection that is handed
    // an empty root list frees objects that scripts can still reach.
    std::vector<JsValue*> all_roots = roots;
    collect_roots(all_roots);

    // 1. Mark phase
    for (const auto* root : all_roots) {
        if (root) mark_value(*root);
    }
    for (const auto* root : m_registered_roots) {
        if (root) mark_value(*root);
    }

    // Process the gray worklists until nothing new is discovered.
    while (!m_gray_stack.empty() || !m_gray_environments.empty() || !m_gray_maps.empty()) {
        if (!m_gray_stack.empty()) {
            HeapObject* obj = m_gray_stack.back();
            m_gray_stack.pop_back();
            obj->trace(*this);
        } else if (!m_gray_environments.empty()) {
            void* env = m_gray_environments.back();
            m_gray_environments.pop_back();
            // Walk outwards; a scope already seen in this collection ends the walk.
            while (env) {
                void* parent = g_environment_tracer.trace(*this, env);
                env = (parent && g_environment_tracer.enter(parent, m_stats.collections_count)) ? parent : nullptr;
            }
        } else {
            const PropertyMap* map = m_gray_maps.back();
            m_gray_maps.pop_back();
            for (const auto& entry : *map) {
                mark_value(entry.second);
                if (entry.setter) mark_value(*entry.setter);
            }
        }
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
