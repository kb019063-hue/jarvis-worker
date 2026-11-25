#pragma once

#include "litebird/js/JsValue.hpp"
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <functional>
#include <cstdint>
#include <optional>
#include <cstddef>
#include <mutex>
#include <atomic>

namespace litebird {

class Heap;
class Node;

/// What kind of built-in object a JsObject is. Drives `Object.prototype.toString`
/// and lets built-in methods check that `this` is the right kind of object.
enum class ObjectClass : uint8_t {
    Plain, Array, Function, Error, Date, RegExp, Map, Set, WeakMap, WeakSet,
    Boolean, Number, String, Symbol, Arguments, Promise, Generator, Iterator
};

/// What a host object does beyond storing properties.
///
/// An ordinary object is a table of properties and a prototype. A NodeList
/// also answers `list[3]`, a dataset answers every name there is a `data-`
/// attribute for, and the window answers the id of every element in the page.
/// None of that can be stored: it is computed from the document each time it
/// is asked for. An object that needs it carries one of these, and property
/// access asks it at fixed points (see JsValue::get_property and friends).
///
/// It is also where a host object keeps its C++ state. Bindings recognise
/// "is this an Event?" by the dynamic type of the exotic, so a script cannot
/// forge one by setting a property.
///
/// Every hook gets the cell it is attached to. The default is to add nothing.
class JsExotic {
public:
    /// What a write to an object with a host-defined property did. The
    /// distinction matters to strict-mode assignments: `[[Set]]` answers with
    /// false when a property cannot be written, and the assignment throws a
    /// TypeError, while code that does not throw only wants to know the write
    /// was dealt with.
    enum class SetResult {
        /// Not the host's property: the ordinary rules of the engine apply.
        NotHandled,
        /// Dealt with (stored somewhere else, or thrown from).
        Handled,
        /// Refused: this object does not accept the write (a getter-only
        /// indexed property of a collection, say). A strict-mode assignment
        /// throws a TypeError, as [[Set]]'s false requires.
        Refused,
    };

    virtual ~JsExotic() = default;
    /// Asked before the object's own properties. True when the object computes
    /// a property called `name`; `out` is its value.
    virtual bool get(JsObject* self, const std::string& name, JsValue& out) { (void)self; (void)name; (void)out; return false; }
    /// Asked last, when neither the object nor its prototypes have `name`.
    virtual bool get_last(JsObject* self, const std::string& name, JsValue& out) { (void)self; (void)name; (void)out; return false; }
    /// Offered every write first (see SetResult).
    virtual SetResult set(JsObject* self, const std::string& name, const JsValue& value) { (void)self; (void)name; (void)value; return SetResult::NotHandled; }
    /// True when `name` is a computed property: `name in obj`, hasOwnProperty.
    virtual bool has(JsObject* self, const std::string& name) { (void)self; (void)name; return false; }
    /// `delete obj[name]`. True when dealt with; `result` is what delete yields.
    virtual bool remove(JsObject* self, const std::string& name, bool& result) { (void)self; (void)name; (void)result; return false; }
    /// Appends the computed property names that enumerate, in order. They come
    /// before the stored properties.
    virtual void keys(JsObject* self, std::vector<std::string>& out) { (void)self; (void)out; }
    /// Appends every computed property name, the ones that do not enumerate
    /// included (`Object.getOwnPropertyNames` asks for those too). Defaults to
    /// the enumerable ones.
    virtual void own_keys(JsObject* self, std::vector<std::string>& out) { keys(self, out); }
    /// What the computed property `name` is, when it is one: whether it
    /// enumerates and whether it can be written. False when `name` is not a
    /// computed property.
    virtual bool computed_property(JsObject* self, const std::string& name, bool& enumerable, bool& writable) {
        (void)self;
        (void)name;
        (void)enumerable;
        (void)writable;
        return false;
    }
    /// As much of a descriptor as a host object needs, for
    /// [[DefineOwnProperty]]: `value` is null when the descriptor is an
    /// accessor one, and `unwritable` is set when it asks for a property that
    /// cannot be written.
    struct PropertyDefinition {
        const JsValue* value{nullptr};
        bool unwritable{false};
        /// True for a descriptor with a getter or a setter in it.
        bool accessor{false};
        /// `false` or `true` when the descriptor asks for that attribute, and
        /// nullopt when it says nothing about it.
        std::optional<bool> enumerable{};
        std::optional<bool> configurable{};
    };
    /// What `defining` did with a name.
    enum class Definition {
        /// The name is none of the host object's business.
        NotMine,
        /// The host object keeps the name and applies the descriptor itself:
        /// an arguments place that is still mapped lives in its parameter's
        /// binding, so nothing goes into the property table and nothing beside
        /// the elements.
        Kept,
        /// The host object has just given the name up and left the value it
        /// held in the table for it (`[[DefineOwnProperty]]` step 5 writes the
        /// map's value into the place before it unmaps). The table gets the
        /// descriptor's attributes and keeps that value.
        Given,
    };
    /// True when `name` is a property the host object answers for, so that a
    /// definition has to go through `defining` and not into the dense elements
    /// or beside them. Asked before the descriptor is applied, because asking
    /// `defining` has consequences (it can unmap the place).
    virtual bool owns_place(JsObject* self, const std::string& name) { (void)self; (void)name; return false; }
    /// A property of the object is about to be defined by
    /// `Object.defineProperty` (or anything else that goes through
    /// [[DefineOwnProperty]]). A host object that computes a property keeps
    /// its own state beside the table, and this is where it says whether the
    /// name is still its own: an arguments object's `[[ParameterMap]]` gives an
    /// index up here (§10.6 step 5.a.i for an accessor, step 4.c for
    /// `writable: false`).
    virtual Definition defining(JsObject* self, const std::string& name, const PropertyDefinition& definition) {
        (void)self; (void)name; (void)definition;
        return Definition::NotMine;
    }
    /// Defined `true` when `name` is a place whose *value* the host object
    /// holds rather than the property table. `Object.getOwnPropertyDescriptor`
    /// asks, because a mapped arguments place keeps a table entry for its
    /// attributes once a descriptor touched it, and that entry's value is a
    /// placeholder: the value is the parameter's, and only the host object can
    /// read it.
    virtual bool host_value(JsObject* self, const std::string& name, JsValue& value) {
        (void)self; (void)name; (void)value;
        return false;
    }
    /// Reports every value held outside the property table to the collector.
    virtual void trace(Heap& heap) { (void)heap; }
};

class JsObject : public HeapObject {
public:
    JsObject* prototype{nullptr};
    PropertyMap properties;

    /// The private fields, methods and accessors this object carries, keyed by
    /// a name only the class that made them can spell (see PrivateName in
    /// JsParser.hpp). They are not properties: no string-keyed read, write,
    /// `in`, `delete` or key walk reaches them, and `Object.keys` never sees
    /// them, so `obj['#x']` is an ordinary absent property rather than the
    /// field. Null for every object that has no private members, which is
    /// nearly all of them: the table is made on the first private write.
    std::unique_ptr<PropertyMap> private_slots;

    /// The private table, made if `create`.
    PropertyMap* private_map(bool create = false);
    const PropertyMap* private_map() const { return private_slots.get(); }
    // Deque-backed, so shift() and splice() at the front are not a memmove of the whole array.
    std::deque<JsValue> array_elements;
    /// Which of `array_elements` are holes: an index that was never written,
    /// or was deleted, and so is not an own property of the array at all
    /// (`1 in a` is false, `Object.keys` leaves it out, `forEach` skips it).
    /// They are kept in the vector all the same, so an index costs nothing
    /// more than it did before; the bits live in a side struct that only an
    /// array that has a hole ever allocates, so JsObject does not grow.
    struct Holes {
        std::vector<uint64_t> bits;
        bool any() const {
            for (uint64_t word : bits) if (word) return true;
            return false;
        }
        bool empty_slot(size_t index) const {
            size_t word = index >> 6;
            if (word >= bits.size()) return false;
            return (bits[word] >> (index & 63)) & 1u;
        }
        void set(size_t index) {
            size_t word = index >> 6;
            if (word >= bits.size()) bits.resize(word + 1, 0);
            bits[word] |= uint64_t(1) << (index & 63);
        }
        void clear(size_t index) {
            size_t word = index >> 6;
            if (word < bits.size()) bits[word] &= ~(uint64_t(1) << (index & 63));
        }
        /// Shifts the bits at and after `from` by `delta` places, for the
        /// insert and erase that shift the elements themselves.
        void shift(size_t from, ptrdiff_t delta);
    };
    std::unique_ptr<Holes> array_holes;
    bool is_array{false};
    bool extensible{true};          // false after Object.preventExtensions/seal/freeze
    bool frozen{false};             // Object.freeze: no writes at all
    bool sealed{false};             // Object.seal: no adds or deletes
    bool has_accessors{false};      // some own property is a getter/setter
    bool has_legacy_getters{false}; // some own property is a "__get_<name>__" host getter
    bool not_constructor{false};    // the cell of a built-in function that `new` refuses (js_is_constructor)
    /// The cell of a `JSON.rawJSON` object: its text is in `internal` and
    /// `JSON.stringify` writes that text as it stands (§25.5.3.1's
    /// [[IsRawJSON]]). A flag rather than a new `ObjectClass`, because the
    /// standard's tag for one is "Object" — `Object.prototype.toString.call`
    /// on it answers `[object Object]` — and `internal` alone cannot say
    /// which built-in filled it. Ninth bool: it sits in the padding before
    /// `klass`, so `JsObject` does not grow.
    bool raw_json_slot{false};
    ObjectClass klass{ObjectClass::Plain};

    /// Built-in state that is not a property: a Date's time value, a RegExp's
    /// compiled program, a Map's storage. The owner knows the concrete type.
    /// In the cell of a function it is the function: see function_value().
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

    /// Asked when `[[OwnPropertyKeys]]` enumerates this object: the names it
    /// adds are own properties that are not in the property table. Used by the
    /// global object, whose declarations live in the global environment.
    /// `lookup_fallback` and `assign_hook` each answer for one name the caller
    /// already has; until this hook existed nothing could ask for *all* of
    /// them, so `Object.getOwnPropertyNames(globalThis)` reported only the
    /// names that happened to be table entries.
    ///
    /// The bool is `enumerable_only`, the way `own_keys` takes it.
    std::shared_ptr<std::function<bool(bool, std::vector<std::string>&)>> own_keys_hook;

    /// Asked by `[[Delete]]` when the name is not in the property table, and by
    /// `has_own_property` for the same reason: `true` means this object owns
    /// the name. The second bool is `remove`; the third is the answer.
    /// Without this, `delete globalThis.Error` erased nothing and answered
    /// `true` — the wrong half of the pair, since `true` means it is gone.
    std::shared_ptr<std::function<bool(const std::string&, bool, bool&)>> own_property_hook;

    /// Host behaviour and state; see JsExotic. Null for ordinary objects.
    std::shared_ptr<JsExotic> exotic;

    // Set when this JsObject is the script-visible identity of a DOM node.
    // Holding a strong reference here means the wrapper keeps its node alive, which
    // is what makes `document.body` stable across accesses and across collections.
    std::shared_ptr<Node> dom_node;

    /// Set by the DOM bindings for objects that have a `dom_node`. The first
    /// reports the objects of the nodes around it (see DomBindings.hpp); the
    /// second runs when the object is freed, and clears the node's pointer
    /// back to it.
    static inline void (*trace_dom_node)(Heap&, JsObject&) = nullptr;
    static inline void (*release_dom_node)(JsObject&) = nullptr;

    /// For a function's cell that stands for the function in a prototype
    /// chain: the function itself. Null for every other cell.
    ///
    /// A function is a value (a payload its copies share), not a cell, and a
    /// prototype link is a pointer to a cell. This is the way back, so that
    /// `Object.getPrototypeOf(Derived)` is `Base` and can be called. It is
    /// set by JsValue::as_prototype() and read by JsValue::make_object().
    ///
    /// The cell then owns a copy of the function, and the function's payload
    /// owns a handle to the cell: a cycle, which only the collector takes
    /// apart. That is why a function's cell is not given one until the
    /// function becomes a prototype. The copy is kept in `internal`, which a
    /// function's cell has no other use for, so no object grows for it.
    const JsValue* function_value() const {
        return klass == ObjectClass::Function ? static_cast<const JsValue*>(internal.get()) : nullptr;
    }
    void set_function_value(const JsValue& function);

    void trace(Heap& heap) override;
    void release_contents() override;
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

    // --- Collection stress ---------------------------------------------------
    //
    // Nothing forces a script to have its garbage collected: a page runs and
    // the collector runs when `gc()` is called or a shell asks for it. So the
    // question "does a collection ever free something that is still alive?"
    // is asked by no test that runs the program the way a page runs it.
    //
    // Stress mode asks it. The heap counts allocations and, every `interval`
    // allocations on average, raises a request; whoever holds the program's
    // state at that moment -- the interpreter, the VM -- serves it at a safe
    // point of its own choosing (a statement boundary, a bytecode boundary),
    // where no half-built value is on the C++ stack that the collector cannot
    // see. That is the same shape as V8's --gc-interval and HotSpot's
    // -XX:+StressGCM: the observable behaviour of a program must not depend on
    // when its garbage is collected, and a run that differs is a real bug.
    //
    // `seed` picks which allocations the requests land on, so one seed is
    // reproducible and two seeds collect at different moments. Requests are
    // counted per heap, and `gc_stress_collections()` says how many were
    // served -- a harness can require that a run actually collected something
    // rather than passing because stress mode was never armed.
    void set_gc_stress(uint64_t seed, uint32_t interval);
    bool gc_stress_enabled() const { return m_stress_enabled.load(std::memory_order_relaxed); }
    /// True when a collection is due (either from stress mode or from the
    /// automatic allocation policy), and clears the request. The caller
    /// collects at a safe point; the heap never collects inside an allocation.
    bool take_gc_stress_request();
    uint64_t gc_stress_collections() const { return m_stress_collections; }

    // --- Automatic collection policy -----------------------------------------
    //
    // Each agent (page or worker) counts allocations since its last collection
    // and requests a collection at the next safe point once that count reaches
    // the agent's threshold. After a collection, the next threshold is
    // `max(min_threshold, surviving_agent_cells)`:
    //   - under churn (small live set), collections fire every `min_threshold`
    //     allocations, keeping peak memory flat without requiring `gc()`;
    //   - under a growing live set, the threshold doubles geometrically (2x
    //     heap growth), keeping mark work O(N) amortized instead of O(N^2).
    //
    // Stress mode (`set_gc_stress`) overrides the automatic policy so seeded
    // stress runs remain deterministic.
    static constexpr size_t kDefaultAutoGcMinThreshold = 8192;
    void set_auto_gc_enabled(bool enabled);
    bool auto_gc_enabled() const { return m_auto_gc_enabled.load(std::memory_order_relaxed); }
    void set_auto_gc_min_threshold(size_t min_cells);
    size_t auto_gc_min_threshold() const { return m_auto_min_threshold.load(std::memory_order_relaxed); }
    uint64_t auto_gc_collections() const { return m_auto_collections.load(std::memory_order_relaxed); }

    void register_root(JsValue* val);
    void unregister_root(JsValue* val);

    // --- Root-set providers -------------------------------------------------
    //
    // The collector finds its own roots (see collect_garbage in Heap.cpp), so
    // nothing has to be registered for a value held in a JsValue. Providers
    // remain for callers that want to name roots explicitly; what they report
    // is marked in addition to what the collector finds.
    //
    // A provider is a callback that pushes the addresses of its live JsValues.
    // Providers must stay valid for as long as they are registered.
    using RootProvider = std::function<void(std::vector<JsValue*>&)>;

    void add_root_provider(const void* owner, RootProvider provider);
    void remove_root_provider(const void* owner);

    /// Runs every registered provider. The supplied roots are appended to.
    void collect_roots(std::vector<JsValue*>& roots) const;

    const GCStats& stats() const { return m_stats; }

    /// Calls `visit` for every cell, for diagnostics. The heap is locked for
    /// the duration, so `visit` must not allocate or collect.
    void for_each_cell(const std::function<void(const JsObject&)>& visit) const;

    void mark_value(const JsValue& val);
    void mark_object(HeapObject* obj);
    void mark_property_map(const PropertyMap* map);
    /// Reports a value that several holders may share through a shared_ptr
    /// (an accessor's setter).
    void mark_shared_value(const std::shared_ptr<JsValue>& value);
    /// The same for a value kept behind a pointer of another type; `holders`
    /// is that pointer's use count.
    void mark_held_value(const JsValue& value, long holders);

    /// For trace functions that walk storage several cells may share (a
    /// collection's entries after `class X extends Map`): true the first time
    /// `storage` is reported in the current pass, false afterwards. Each stored
    /// value must be reported exactly once per pass.
    bool first_visit(const void* storage);
    /// True while the collector is counting the references stored inside the
    /// heap, as opposed to marking what is reachable. Storage that native code
    /// can also keep alive must not be reported during this pass.
    bool counting() const { return m_counting; }

    // --- Agents -----------------------------------------------------------------
    //
    // A page and each of its workers run on their own threads and never share
    // objects: messages between them are copied. They do share this heap's
    // cell list, so every cell and scope is tagged with the agent that created
    // it, and a collection only examines, marks through and frees what belongs
    // to the agent running it. Everything else is left exactly as it is.
    static uint32_t current_agent();
    /// Makes the calling thread a new agent. A worker thread calls this first.
    static uint32_t enter_new_agent();
    /// Makes the calling thread part of `agent`: for threads an agent starts
    /// to run its own script (suspended function frames).
    static void join_agent(uint32_t agent);

    // --- Lexical environments -------------------------------------------------
    //
    // A closure keeps its defining scopes alive, and those scopes hold values
    // the collector must not free. The environment type belongs to the
    // interpreter, so it supplies the two operations the collector needs.
    struct EnvironmentTracer {
        /// Records that `env` is being visited in collection `epoch`. Returns
        /// false when it was already visited, which is what ends the walk.
        bool (*enter)(void* env, uint64_t epoch){nullptr};
        /// Reports every value bound in `env` and returns the enclosing scope.
        void* (*trace)(Heap& heap, void* env){nullptr};
        /// Lists every scope of `agent` that exists right now. Each entry
        /// keeps its scope alive until the collection is over.
        void (*snapshot)(uint32_t agent, std::vector<std::shared_ptr<void>>& out){nullptr};
        /// Whether `env` was visited in collection `epoch`.
        bool (*reached)(void* env, uint64_t epoch){nullptr};
        /// Drops every binding of a scope nothing can reach any more.
        void (*clear)(void* env){nullptr};
    };
    static void set_environment_tracer(EnvironmentTracer tracer);
    void mark_environment(void* env);
    /// Records `refs` heap-owned `shared_ptr<JsEnvironment>` references to `env`
    /// during the counting pass (e.g. a generator frame owned solely by an
    /// in-heap generator object). Ignored outside the counting pass.
    void count_environment(void* env, size_t refs = 1);

private:
    struct RootProviderEntry {
        const void* owner;
        RootProvider provider;
    };

    // Bookkeeping for one collection; see collect_garbage.
    struct PayloadInfo {
        long seen{0};               // copies of the value found inside the heap
        long owners{0};             // copies that exist in total
        const JsValue::FunctionData* function{nullptr};
        const PropertyMap* properties{nullptr};
        JsObject* cell{nullptr};    // the function's cell, which the payload holds a handle to
        const std::vector<JsValue>* captured{nullptr};  // what the entry point captures
        const JsValue* bound_target{nullptr};           // a bound function's target
    };
    struct ScopeInfo {
        long seen{0};               // closures and child scopes found inside the heap
        std::shared_ptr<void> hold; // keeps the scope alive during the collection
    };
    void count_value(const JsValue& val);
    void mark_payload(const PayloadInfo& info);

    std::unordered_map<const void*, PayloadInfo> m_payloads;
    std::unordered_map<void*, ScopeInfo> m_scopes;
    std::vector<const PropertyMap*> m_count_maps;
    uint64_t m_epoch{0};
    uint32_t m_collecting_agent{0};

    mutable std::mutex m_mutex;
    std::vector<RootProviderEntry> m_root_providers;
    HeapObject* m_head{nullptr};
    std::vector<HeapObject*> m_gray_stack;
    std::vector<void*> m_gray_environments;
    std::vector<const PropertyMap*> m_gray_maps;
    std::vector<const PropertyMap*> m_visited_maps;
    std::unordered_set<const void*> m_visited_storage;
    bool m_counting{false};
    std::vector<JsValue*> m_registered_roots;
    GCStats m_stats;

    // Collection stress (see set_gc_stress). The countdown is read and written
    // only with m_mutex held; the request flag is read by a thread's own safe
    // point, so it is atomic.
    std::atomic<bool> m_stress_enabled{false};
    uint64_t m_stress_rng{0};
    uint32_t m_stress_interval{16};
    uint32_t m_stress_countdown{0};
    std::atomic<bool> m_stress_pending{false};
    uint64_t m_stress_collections{0};
    uint64_t next_stress_random();

    // Per-agent automatic collection state (see set_auto_gc_enabled).
    // Protected by m_mutex; hot-path safe points check m_main_auto_pending
    // (for agent 0) or m_worker_pending_mask (for worker agents) lock-free.
    struct AgentAutoState {
        size_t allocs_since_gc{0};
        size_t threshold{kDefaultAutoGcMinThreshold};
        bool pending{false};
    };
    AgentAutoState& agent_auto_state_locked(uint32_t agent);
    AgentAutoState* find_agent_auto_state_locked(uint32_t agent);
    void clear_auto_pending_locked(uint32_t agent, AgentAutoState& st);
    void clear_all_auto_pending_locked();
    void reset_auto_gc_after_collect_locked(uint32_t agent, size_t agent_alive);

    std::atomic<bool> m_auto_gc_enabled{true};
    std::atomic<size_t> m_auto_min_threshold{kDefaultAutoGcMinThreshold};
    AgentAutoState m_main_agent_auto;
    std::unordered_map<uint32_t, AgentAutoState> m_worker_agent_auto;
    std::atomic<bool> m_main_auto_pending{false};
    std::atomic<uint64_t> m_worker_pending_mask{0};
    std::atomic<uint64_t> m_auto_collections{0};
};

} // namespace litebird
