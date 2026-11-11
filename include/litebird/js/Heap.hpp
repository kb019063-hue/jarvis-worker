#pragma once

#include "litebird/js/JsValue.hpp"
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <functional>
#include <cstdint>
#include <mutex>

namespace litebird {

class Heap;
class Node;

/// What kind of built-in object a JsObject is. Drives `Object.prototype.toString`
/// and lets built-in methods check that `this` is the right kind of object.
enum class ObjectClass : uint8_t {
    Plain, Array, Function, Error, Date, RegExp, Map, Set, WeakMap, WeakSet,
    Boolean, Number, String, Symbol, Arguments, Promise, Generator, Iterator
};

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
    PropertyMap properties;
    std::vector<JsValue> array_elements;
    bool is_array{false};
    bool extensible{true};          // false after Object.preventExtensions/seal/freeze
    bool frozen{false};             // Object.freeze: no writes at all
    bool sealed{false};             // Object.seal: no adds or deletes
    bool has_accessors{false};      // some own property is a getter/setter
    bool has_legacy_getters{false}; // some own property is a "__get_<name>__" host getter
    ObjectClass klass{ObjectClass::Plain};

    /// Built-in state that is not a property: a Date's time value, a RegExp's
    /// compiled program, a Map's storage. The owner knows the concrete type.
    std::shared_ptr<void> internal;

    /// Marks values this object keeps alive outside its property table, such
    /// as the variables a closure captured or the entries of a Map.
    std::function<void(Heap&)> trace_extra;

    /// Falls back to this lookup when a property is not found on the object or
    /// its prototypes. Used by the global object to expose global variables.
    std::shared_ptr<std::function<bool(const std::string&, JsValue&)>> lookup_fallback;

    /// Offered every write first; returning true means the hook stored the
    /// value. Lets `window.x = 1` update a global variable named x.
    std::shared_ptr<std::function<bool(const std::string&, const JsValue&)>> assign_hook;

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
    void mark_property_map(const PropertyMap* map);

    // --- Lexical environments -------------------------------------------------
    //
    // A closure keeps its defining scopes alive, and those scopes hold values
    // the collector must not free. The environment type belongs to the
    // interpreter, so it supplies the two operations the collector needs.
    struct EnvironmentTracer {
        /// Records that `env` is being visited in collection `epoch`. Returns
        /// false when it was already visited, which is what ends the walk.
        bool (*enter)(void* env, uint64_t epoch){nullptr};
        /// Marks every value bound in `env` and returns the enclosing scope.
        void* (*trace)(Heap& heap, void* env){nullptr};
    };
    static void set_environment_tracer(EnvironmentTracer tracer);
    void mark_environment(void* env);

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
    std::vector<void*> m_gray_environments;
    std::vector<const PropertyMap*> m_gray_maps;
    std::vector<const PropertyMap*> m_visited_maps;
    std::vector<JsValue*> m_registered_roots;
    GCStats m_stats;
};

} // namespace litebird
