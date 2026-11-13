#include "litebird/js/Heap.hpp"
#include "litebird/js/JsValue.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>

namespace litebird {

namespace {
Heap::EnvironmentTracer g_environment_tracer;

thread_local uint32_t t_agent = 0;
std::atomic<uint32_t> g_next_agent{1};
// Collections are numbered across all heaps, so a scope visited by one heap
// is never mistaken for visited by another.
std::atomic<uint64_t> g_epoch{0};

// CellRef converts between JsObject* and HeapObject* without seeing JsObject's
// definition, which is only sound if the base sits at the start of the object.
[[maybe_unused]] const bool g_layout_checked = [] {
    JsObject probe;
    if (static_cast<void*>(static_cast<HeapObject*>(&probe)) != static_cast<void*>(&probe)) std::abort();
    return true;
}();
}

void JsObject::trace(Heap& heap) {
    if (prototype) {
        heap.mark_object(prototype);
    }
    for (auto& entry : properties) {
        heap.mark_value(entry.second);
        if (entry.setter) heap.mark_shared_value(entry.setter);
    }
    for (auto& elem : array_elements) {
        heap.mark_value(elem);
    }
    if (trace_extra) trace_extra(heap);
    if (exotic) exotic->trace(heap);
    if (dom_node && trace_dom_node) trace_dom_node(heap, *this);
}

void JsObject::release_contents() {
    // Everything is moved out before anything is destroyed: destroying one
    // member can run code (a DOM node's destructor, a suspended function being
    // unwound) that must find this cell empty, not half torn down.
    if (dom_node && release_dom_node) release_dom_node(*this);
    PropertyMap dropped_properties = std::move(properties);
    properties.clear();
    std::vector<JsValue> dropped_elements = std::move(array_elements);
    array_elements.clear();
    auto dropped_internal = std::move(internal);
    auto dropped_trace = std::move(trace_extra);
    auto dropped_fallback = std::move(lookup_fallback);
    auto dropped_hook = std::move(assign_hook);
    auto dropped_node = std::move(dom_node);
    auto dropped_exotic = std::move(exotic);
    exotic.reset();
    internal.reset();
    trace_extra = nullptr;
    lookup_fallback.reset();
    assign_hook.reset();
    dom_node.reset();
    prototype = nullptr;
}

void Heap::set_environment_tracer(EnvironmentTracer tracer) {
    g_environment_tracer = tracer;
}

uint32_t Heap::current_agent() {
    return t_agent;
}

uint32_t Heap::enter_new_agent() {
    t_agent = g_next_agent.fetch_add(1);
    return t_agent;
}

void Heap::join_agent(uint32_t agent) {
    t_agent = agent;
}

Heap::Heap() = default;

Heap::~Heap() {
    // Cells refer to each other, so every handle is dropped first and the
    // memory is freed only afterwards.
    for (HeapObject* cell = m_head; cell; cell = cell->next_cell) cell->release_contents();
    HeapObject* curr = m_head;
    while (curr) {
        HeapObject* next = curr->next_cell;
        delete curr;
        curr = next;
    }
    m_head = nullptr;
}

Heap& Heap::default_heap() {
    // Never destroyed. Values that live in static storage release their
    // handles while the process exits, in an order nobody controls, and a
    // handle must always point at a cell that still exists.
    static Heap* s_heap = new Heap();
    return *s_heap;
}

JsObject* Heap::allocate_object() {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto* obj = new JsObject();
    obj->is_marked = false;
    obj->agent = t_agent;
    obj->next_cell = m_head;
    m_head = obj;

    m_stats.total_cells++;
    m_stats.alive_cells++;
    m_stats.bytes_allocated += sizeof(JsObject);

    return obj;
}

void Heap::mark_value(const JsValue& val) {
    if (m_counting) {
        count_value(val);
        return;
    }
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

void Heap::mark_shared_value(const std::shared_ptr<JsValue>& value) {
    if (!value) return;
    if (m_counting) {
        // Counted only when this is the one holder: a second holder may be
        // native code, which would keep the value alive without us knowing.
        if (value.use_count() == 1) count_value(*value);
        return;
    }
    mark_value(*value);
}

// Counting pass: `val` is stored somewhere the collector can enumerate (a
// cell, a scope, a function's own property table), so every reference it
// holds is one the heap accounts for.
void Heap::count_value(const JsValue& val) {
    if (JsObject* cell = val.object_val) ++cell->gc_seen;

    const void* payload = val.payload_identity();
    if (!payload) return;
    auto [slot, fresh] = m_payloads.try_emplace(payload);
    PayloadInfo& info = slot->second;
    ++info.seen;
    if (!fresh) return;

    // First copy of this payload: note what the payload itself holds.
    info.owners = val.payload_owners();
    info.function = val.function_data();
    info.properties = val.payload_properties();
    if (info.function) {
        if (JsObject* cell = info.function->cell) ++cell->gc_seen;
        if (info.function->closure) {
            auto scope = m_scopes.find(info.function->closure.get());
            if (scope != m_scopes.end()) ++scope->second.seen;
        }
    }
    if (info.properties) m_count_maps.push_back(info.properties);
}

// A payload native code holds a copy of: what it refers to is alive.
void Heap::mark_payload(const PayloadInfo& info) {
    if (info.function) {
        if (info.function->cell) mark_object(info.function->cell);
        if (info.function->closure) mark_environment(info.function->closure.get());
    }
    if (info.properties) mark_property_map(info.properties);
}

void Heap::mark_property_map(const PropertyMap* map) {
    if (m_counting) return;     // counted through the payload that owns it
    if (!map || map->empty()) return;
    if (std::find(m_visited_maps.begin(), m_visited_maps.end(), map) != m_visited_maps.end()) return;
    m_visited_maps.push_back(map);
    m_gray_maps.push_back(map);
}

void Heap::mark_environment(void* env) {
    if (m_counting) return;     // counted through the payload that closes over it
    if (!env || !g_environment_tracer.enter) return;
    if (g_environment_tracer.enter(env, m_epoch)) m_gray_environments.push_back(env);
}

void Heap::mark_object(HeapObject* obj) {
    // A prototype link is a plain pointer, not a handle: nothing to count.
    if (m_counting) return;
    if (!obj || obj->is_marked) return;
    obj->is_marked = true;
    // Another agent's cell is kept, but never looked into: its owner may be
    // changing it on another thread right now.
    if (obj->agent != m_collecting_agent) return;
    m_gray_stack.push_back(obj);
}

bool Heap::first_visit(const void* storage) {
    return m_visited_storage.insert(storage).second;
}

void Heap::for_each_cell(const std::function<void(const JsObject&)>& visit) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const HeapObject* cell = m_head; cell; cell = cell->next_cell) {
        visit(*static_cast<const JsObject*>(cell));
    }
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

// How a collection decides what is alive
// ------------------------------------
//
// Three kinds of things refer to each other: cells (objects), scopes (the
// variables of a function call or block) and payloads (what the copies of one
// function value share: its closure, its property cell). Each knows how many
// references to it exist in total: a cell counts its handles (CellRef), a
// scope and a payload are reference-counted by shared_ptr.
//
// Some of those references are stored where the collector can enumerate them:
// in a cell's properties, in a scope's bindings, in a payload. The rest are
// everywhere else: C++ locals of the interpreter, std::function captures of
// timers and DOM listeners, promise reactions, the native stack of a suspended
// generator. The collector never tries to find the second kind. It counts the
// first kind and compares with the total. Whatever has more references than
// the heap can account for is being held from outside, and is a root.
//
// From the roots it marks as usual. What is left unmarked is referenced only
// by other unmarked things: garbage, including cycles that run through
// closures. Unmarked scopes are emptied and unmarked cells freed.
//
// Two rules keep this sound:
//   - A reference is counted only if whatever stores it is itself subject to
//     this collection and cannot be kept alive behind the collector's back.
//     Storage that native code may co-own (a promise's reactions, a shared
//     property table) is left uncounted, which makes its contents roots.
//   - Every stored reference is counted at most once.
// Counting too little only keeps garbage alive. Counting too much would free
// live objects, so anything that looks over-counted is treated as a root.
GCStats Heap::collect_garbage(const std::vector<JsValue*>& roots) {
    std::vector<HeapObject*> garbage;
    std::vector<void*> dead_scopes;
    std::unordered_map<void*, ScopeInfo> scopes;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stats.collections_count++;
        m_epoch = g_epoch.fetch_add(1) + 1;
        m_collecting_agent = t_agent;
        m_gray_stack.clear();
        m_gray_environments.clear();
        m_gray_maps.clear();
        m_visited_maps.clear();
        m_visited_storage.clear();
        m_payloads.clear();
        m_scopes.clear();
        m_count_maps.clear();

        // Scopes belong to the interpreter's heap. A separately constructed
        // Heap (unit tests) collects cells only and treats scopes as outside.
        const bool with_scopes = this == &default_heap() && g_environment_tracer.snapshot;
        if (with_scopes) {
            std::vector<std::shared_ptr<void>> alive_scopes;
            g_environment_tracer.snapshot(m_collecting_agent, alive_scopes);
            m_scopes.reserve(alive_scopes.size());
            for (auto& scope : alive_scopes) {
                void* key = scope.get();
                m_scopes[key].hold = std::move(scope);
            }
        }

        // 1. Count the references stored inside the heap.
        for (HeapObject* cell = m_head; cell; cell = cell->next_cell) {
            cell->gc_seen = 0;
            cell->is_marked = false;
        }
        m_counting = true;
        for (HeapObject* cell = m_head; cell; cell = cell->next_cell) {
            if (cell->agent == m_collecting_agent) cell->trace(*this);
        }
        for (auto& [scope, info] : m_scopes) {
            void* parent = g_environment_tracer.trace(*this, scope);
            if (!parent) continue;
            auto found = m_scopes.find(parent);
            if (found != m_scopes.end()) ++found->second.seen;
        }
        while (!m_count_maps.empty()) {
            const PropertyMap* map = m_count_maps.back();
            m_count_maps.pop_back();
            for (const auto& entry : *map) {
                count_value(entry.second);
                if (entry.setter) mark_shared_value(entry.setter);
            }
        }
        m_counting = false;
        m_visited_storage.clear();

        // 2. Roots: whatever is referenced from outside the heap.
        for (HeapObject* cell = m_head; cell; cell = cell->next_cell) {
            if (cell->agent != m_collecting_agent) {
                cell->is_marked = true;         // not ours to judge
            } else if (cell->handle_count != cell->gc_seen) {
                mark_object(cell);
            }
        }
        for (const auto& [payload, info] : m_payloads) {
            if (info.owners != info.seen) mark_payload(info);
        }
        for (const auto& [scope, info] : m_scopes) {
            // One owner is the snapshot's own.
            if (info.hold.use_count() - 1 != info.seen) mark_environment(scope);
        }
        // Roots handed over explicitly are marked as well.
        std::vector<JsValue*> all_roots = roots;
        collect_roots(all_roots);
        for (const auto* root : all_roots) {
            if (root) mark_value(*root);
        }
        for (const auto* root : m_registered_roots) {
            if (root) mark_value(*root);
        }

        // 3. Mark everything reachable from the roots.
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
                    env = (parent && g_environment_tracer.enter(parent, m_epoch)) ? parent : nullptr;
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
        m_visited_storage.clear();

        // 4. Take the garbage off the cell list and note the dead scopes. Both
        //    are torn down below, once the heap is unlocked.
        HeapObject** link = &m_head;
        size_t alive = 0;
        while (*link) {
            HeapObject* cell = *link;
            if (cell->is_marked) {
                cell->is_marked = false;
                link = &cell->next_cell;
                ++alive;
            } else {
                *link = cell->next_cell;
                cell->next_cell = nullptr;
                garbage.push_back(cell);
            }
        }
        for (const auto& [scope, info] : m_scopes) {
            if (!g_environment_tracer.reached(scope, m_epoch)) dead_scopes.push_back(scope);
        }
        scopes = std::move(m_scopes);
        m_scopes.clear();
        m_payloads.clear();

        m_stats.alive_cells = alive;
        m_stats.total_cells = alive;
        m_stats.bytes_allocated = alive * sizeof(JsObject);
    }

    // 5. Tear the garbage down. Dropping what a cell or scope holds can run
    //    arbitrary destructors (a DOM node, a worker, a suspended function's
    //    thread), and those may call back into the heap, so the lock is not
    //    held here. Every garbage cell stays allocated, and every dead scope
    //    stays alive, until all of them are empty: they refer to each other.
    for (void* scope : dead_scopes) g_environment_tracer.clear(scope);
    for (HeapObject* cell : garbage) cell->release_contents();
    scopes.clear();

    size_t collected = 0;
    std::vector<HeapObject*> survivors;
    for (HeapObject* cell : garbage) {
        if (cell->handle_count == 0) {
            delete cell;
            ++collected;
        } else {
            // Something still holds a handle to this cell. Freeing it would
            // leave that handle dangling, so it goes back on the list, empty.
            survivors.push_back(cell);
        }
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    for (HeapObject* cell : survivors) {
        cell->next_cell = m_head;
        m_head = cell;
        m_stats.alive_cells++;
        m_stats.total_cells++;
    }
    m_stats.bytes_allocated = m_stats.alive_cells * sizeof(JsObject);
    m_stats.collected_cells += collected;
    return m_stats;
}

} // namespace litebird
