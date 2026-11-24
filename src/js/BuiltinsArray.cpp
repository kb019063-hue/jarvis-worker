// Array, Array.prototype and the iterator objects the built-ins hand out.

#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Heap.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace litebird {

namespace {

using Args = const std::vector<JsValue>&;

JsValue self() { return JsValue::current_this(); }

/// The array a method operates on.
///
/// This used to *become* the receiver: an array-like was copied into a real
/// array by reading index 0 up to its `length`, before the method read a
/// single argument. That made `{ 0: 9, length: Infinity }` four billion
/// property reads pushing four billion values — `std::bad_alloc` if the
/// machine had the patience, the out-of-memory killer if it did not — and it
/// made `{ length: 2**32 }` an allocation rather than the RangeError the
/// standard asks for. Phase 129 removed the copy; the methods that need the
/// places now take the receiver whole and ask `present_indices` which ones are
/// there, or ask `HasProperty` one index at a time for a length small enough
/// to be worth it.
///
/// What remains is what the old form was right about: the receiver has to
/// exist, and a method that would write it has to have something writable.
JsValue receiver(const char* method, bool mutates = false) {
    JsValue me = self();
    if (me.is_nullish()) {
        js_throw_type_error(std::string("Array.prototype.") + method + " called on null or undefined");
    }
    if (mutates) {
        js_throw_type_error(std::string("Array.prototype.") + method + " requires an array");
    }
    return me;
}

std::deque<JsValue>& elements_of(const JsValue& array) {
    return array.object_val->array_elements;
}

/// True when `index` of `array` is a hole and must not be visited: absent from
/// the own keys, so the callback methods skip it and the copying methods keep
/// it a hole (`HasProperty` in the standard, which an array answers with
/// `!is_array_hole`).
bool hole_at(const JsValue& array, size_t index) {
    return array.is_array_hole(index);
}

/// `HasProperty(O, k)` as the callback methods ask it: the standard says a hole
/// is not the end of the question, because an index can be inherited from
/// `Array.prototype` (a setter-only accessor there still makes the index one
/// that `every` must visit). `has_property` walks the chain, so asking it is
/// the whole answer; `hole_at` is only right for the *own* half, which the
/// copying methods ask when they are carrying holes forward.
bool has_index(const JsValue& array, size_t index) {
    return array.has_property(std::to_string(index));
}

/// The value `O[k]` reads, holes included: a hole is `undefined` unless the
/// prototype has the index.
JsValue index_value(const JsValue& array, size_t index) {
    return array.get_property(std::to_string(index));
}

/// The array a method that copies reads from: `array` itself, unless one of
/// its holes reads as something, an accessor that `Object.defineProperty` put
/// on the index or an index the prototype has. Then it is a copy in which
/// each such hole is an element, the value `a[i]` reads: what the standard's
/// `HasProperty` and `Get` make of the place for `slice`, `concat` and `flat`,
/// and its `Get` for the methods that give every place a value. A hole that
/// nothing answers is a hole in the copy too. An array that never had a hole
/// is told apart by one pointer.
///
/// The copy has `count` places: the standard takes the length once, and the
/// caller that took it before something else could run (`with` and
/// `toSpliced` ask what their arguments are first) passes it; otherwise it
/// is the length now. A getter that makes the array longer adds nothing to
/// the copy, and a place that is gone by the time it is reached is read like
/// any other that is not there. The places from `skip_from` up to `skip_to`
/// are not read at all (`with` does not read the index it replaces, and
/// `toSpliced` not the ones it drops) and are holes in the copy.
///
/// The array itself is the answer when it has no hole to ask about and no
/// place is missing, or when nothing would answer for one: the caller copies
/// its elements, and makes the copy `count` long when it passed one.
JsValue read_through(const JsValue& array, size_t skip_from = 0, size_t skip_to = 0,
                     size_t count = std::numeric_limits<size_t>::max()) {
    if (count == std::numeric_limits<size_t>::max()) count = elements_of(array).size();
    const bool all_there = !array.object_val->array_holes && count <= elements_of(array).size();
    if (all_there || js_array_holes_read_undefined(array)) return array;
    JsValue copy = JsValue::make_array();
    auto& out = copy.object_val->array_elements;
    for (size_t i = 0; i < count; ++i) {
        if (i >= skip_from && i < skip_to) {
            out.push_back(JsValue::make_undefined());
            copy.set_array_hole(i);
            continue;
        }
        if (i < elements_of(array).size() && !array.is_array_hole(i)) {
            out.push_back(elements_of(array)[i]);
            continue;
        }
        const std::string key = std::to_string(i);
        const bool there = array.has_property(key);
        out.push_back(there ? array.get_property(key) : JsValue::make_undefined());
        if (!there) copy.set_array_hole(i);
    }
    return copy;
}


/// ToLength(value), clamped to the standard's 2^53 - 1.
size_t to_length(const JsValue& value) {
    const double number = js_to_number(value);
    if (!(number > 0)) return 0;
    if (number > 9007199254740991.0) return static_cast<size_t>(9007199254740991ull);
    return static_cast<size_t>(number);
}

/// LengthOfArrayLike(O): `? ToLength(? Get(O, "length"))`, and that is a
/// property lookup, not an own-property read. A `String` object keeps `length`
/// on `String.prototype` and its characters on the object, so reading it any
/// other way made `Array.prototype.every.call(new String("432"), f)` visit
/// nothing: the receiver answered a length of 0 and the run was skipped.
size_t length_of(JsValue object) {
    if (object.type == JsType::String) return object.string_val.length();
    return to_length(object.get_property("length"));
}

/// LengthOfArrayLike for a method, with the receiver null check first.
size_t array_like_length(const JsValue& object, const char* method) {
    if (object.is_nullish()) {
        js_throw_type_error(std::string("Array.prototype.") + method + " called on null or undefined");
    }
    return length_of(object);
}

/// The length a method that *builds* an array may have. `map`, `slice` and
/// their relatives make a real array of their result, and a real array is at
/// most 2^32-1 long; a longer source is the RangeError of ArrayCreate, thrown
/// before the first callback runs, which is what `create-non-array-invalid-len`
/// asks for.
size_t array_result_length(size_t length, const char* method) {
    if (length > 4294967295ull) {
        js_throw_range_error(std::string("Array.prototype.") + method +
                             ": the result would be longer than 2^32 - 1");
    }
    return length;
}

/// The largest index the standard will visit one at a time. Past this a run of
/// holes is not walked: 16 million indices is already a test of this engine's
/// patience, not a page.
constexpr size_t kMaxWalkedIndices = 1u << 24;

/// The largest string `join` will build (512 MiB). A `length` whose *minimum*
/// result — every place empty and only the separators — already exceeds it is
/// a RangeError, which is what V8 and SpiderMonkey answer for
/// `{ length: 2**53 - 1 }.join()`; the alternative is a loop that never ends.
constexpr size_t kMaxJoinedBytes = 1u << 29;

/// The integer key a property key is: "0", "17", "9007199254740989", but not
/// "01", "-1" or "1.5". Not an *array index* — one of those is below 2^32-1 —
/// because these methods ask `Get(O, ToString(k))` of an object whose keys are
/// ordinary strings, and test262's `length-exceeding-integer-limit` and
/// `15.4.4.15-3-28` both put a key past 2^32-1 on one. Keys go up to 2^53-1,
/// the last integer a double holds exactly, which is the longest `length` the
/// standard allows. `JsValue.cpp` has its own copy of a narrower rule; it is
/// private to that file and answers a different question.
static bool as_array_index(const std::string& key, size_t& out) {
    if (key.empty() || key.size() > 16) return false;
    if (key[0] == '0' && key.size() > 1) return false;
    size_t value = 0;
    for (char c : key) {
        if (c < '0' || c > '9') return false;
        const size_t digit = static_cast<size_t>(c - '0');
        if (value > (9007199254740991ull - digit) / 10) return false;
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

/// Keys that are engine bookkeeping rather than script-visible properties:
/// symbols ("@@..."), internal slots ("%...") and the legacy "__name__"
/// markers. `JsValue.cpp` has the same list; it is private to that file.
static bool hidden_key(const std::string& key) {
    if (key.size() >= 2 && key[0] == '@' && key[1] == '@') return true;
    if (!key.empty() && key[0] == '%') return true;
    return key.size() >= 5 && key[0] == '_' && key[1] == '_' &&
           key[key.size() - 1] == '_' && key[key.size() - 2] == '_';
}

/// The indices `cell` itself answers HasProperty for, below `limit`, added to
/// `out`: its property map's integer keys, and — because `Array.prototype` is
/// an array — its elements. `Array.prototype[0] = "x"` writes an element, not
/// a property, and every array below it answers yes to index 0 afterwards.
static void collect_link_indices(const JsObject* cell, size_t limit, std::vector<size_t>& out) {
    if (!cell) return;
    for (const auto& entry : cell->properties) {
        if (hidden_key(entry.first)) continue;
        size_t index = 0;
        if (as_array_index(entry.first, index) && index < limit) out.push_back(index);
    }
    if (cell->is_array) {
        const size_t count = std::min(cell->array_elements.size(), limit);
        for (size_t i = 0; i < count; ++i) {
            if (!cell->array_holes || !cell->array_holes->empty_slot(i)) out.push_back(i);
        }
    }
}

/// True when any link in `object`'s prototype chain has an index of its own.
/// Such a link can make `HasProperty` say yes to an index `object` does not
/// have, which is the one case a walk of the object's own places cannot answer
/// on its own.
static bool prototype_holds_an_index(const JsValue& object) {
    // A receiver that is not an object (`[].forEach.call(5)`) has no chain for
    // a property to reach, so it cannot be made to answer for an index.
    if (!object.is_object_like()) return false;
    size_t guard = 0;
    for (JsObject* cell = object.prototype_object(); cell && guard < 512; cell = cell->prototype, ++guard) {
        for (const auto& entry : cell->properties) {
            size_t index = 0;
            if (!hidden_key(entry.first) && as_array_index(entry.first, index)) return true;
        }
        if (cell->is_array && !cell->array_elements.empty()) {
            if (!cell->array_holes) return true;
            for (size_t i = 0; i < cell->array_elements.size(); ++i) {
                if (!cell->array_holes->empty_slot(i)) return true;
            }
        }
    }
    return false;
}

/// The places of `object` that are there, below `limit`, ascending: what
/// `HasProperty(O, k)` would say yes to, without asking it once per index.
///
/// This is the difference between an answer and a dead process. The standard
/// says `for k = 0 to len-1: HasProperty`; read literally, `{ 0: 9, length:
/// Infinity }` is four billion questions to find one element, and `{ length:
/// 2**32 }` cannot be counted at all. The places that are there is a finite
/// list — the own elements that are not holes, the map's integer keys, and the
/// chain's integer keys bounded by `limit` — so that is what is walked.
struct IndexWalk {
    std::vector<size_t> indices;
};

/// A `String` object (`new String("432")`) keeps its characters and its
/// `length` on `String.prototype`, not on itself, so neither the element walk
/// nor the property map below finds them. It is a string: its places are
/// 0 … len-1.
static bool is_string_object(const JsValue& value) {
    return value.is_object_like() && value.object_val && value.object_val->klass == ObjectClass::String;
}

static IndexWalk present_indices(const JsValue& object, size_t limit) {
    IndexWalk walk;
    std::vector<size_t> found;
    if (object.type == JsType::String) {
        const size_t length = std::min<size_t>(object.string_val.length(), limit);
        found.reserve(length);
        for (size_t i = 0; i < length; ++i) found.push_back(i);
        walk.indices = std::move(found);
        return walk;
    }
    if (is_string_object(object)) {
        const size_t length = std::min<size_t>(length_of(object), limit);
        found.reserve(length);
        for (size_t i = 0; i < length; ++i) found.push_back(i);
    }
    if (object.is_array()) {
        // The places that are elements. A hole is not one, unless the map has
        // it (an accessor on an index, `Object.defineProperty`), which the
        // property scan below adds.
        const auto& elements = object.object_val->array_elements;
        const size_t count = std::min(elements.size(), limit);
        for (size_t i = 0; i < count; ++i) {
            if (!object.is_array_hole(i)) found.push_back(i);
        }
    }
    // The receiver's own integer keys, through `own_keys`: an exotic object
    // (`NodeList`, a typed array) keeps its indices outside the property map,
    // and only the object knows they are there. A real array's elements are
    // already in `found`, and `own_keys` repeats them, so the duplicates are
    // removed by the sort below.
    for (const std::string& key : object.own_keys(true)) {
        size_t index = 0;
        if (as_array_index(key, index) && index < limit) found.push_back(index);
    }
    // The chain's own indices, bounded by the length like everything else: an
    // index the prototype has past the length is not visited, because the loop
    // stops at the length.
    if (prototype_holds_an_index(object)) {
        size_t guard = 0;
        for (JsObject* proto = object.prototype_object(); proto && guard < 512; proto = proto->prototype, ++guard) {
            collect_link_indices(proto, limit, found);
        }
        // A `String` object's characters are on its prototype as one link's
        // own indices; they are already merged above.
    }
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    walk.indices = std::move(found);
    return walk;
}

/// The value at `index`, asked as the standard asks it: `Get(O, key)`. A
/// string receiver answers its own characters, which are properties of the
/// primitive and not of any object this engine made for it.
static JsValue get_index(const JsValue& object, size_t index) {
    if (object.type == JsType::String) {
        if (index < object.string_val.length()) {
            return JsValue::make_string(JsString::from_unit(object.string_val.unit_at(index)));
        }
        return JsValue::make_undefined();
    }
    return object.get_property(js_number_to_string(static_cast<double>(index)));
}

/// `HasProperty(O, key)` the same way. A string has its own indices; every
/// other receiver asks the ordinary lookup, which walks the prototype chain.
static bool has_index_property(const JsValue& object, size_t index) {
    if (object.type == JsType::String) return index < object.string_val.length();
    return object.has_property(std::to_string(index));
}

/// True when a method may keep using this engine's dense element vector: a real
/// array, no accessor on a place, and nothing that would refuse a write.
/// Everything else takes the standard's index-by-index form, which is what
/// makes these methods work on `{ 0: "a", length: 1 }` at all, and what calls a
/// getter that `Object.defineProperty` put on an index.
bool dense_path(const JsValue& object) {
    if (!object.is_array()) return false;
    const JsObject* cell = object.object_val;
    if (cell == nullptr || cell->has_accessors || cell->frozen || cell->sealed ||
        !cell->extensible) {
        return false;
    }
    // Every one of these methods ends by setting `length`, and an array whose
    // length was made read-only has to refuse that write loudly.
    const auto entry = cell->properties.find("length");
    if (entry != cell->properties.end() && !(entry->flags & PropertyMap::Writable)) return false;
    // A place the prototype answers for — a setter on `Array.prototype[0]`,
    // say — is a place every write goes through its chain to reach, and a
    // setter there can make this array's length read-only halfway through the
    // method (test262's `push/set-length-array-length-is-non-writable.js`).
    // Such a receiver takes the standard's index-by-index path, where that
    // second write finds the length and refuses it (finding 253).
    return !prototype_holds_an_index(object);
}

/// Visit each index below `limit` whose place is there, in ascending order,
/// asking `HasProperty` as the standard does — once per index that is
/// reached — but skipping a run of places that cannot be there in one step.
///
/// `visit` returns false to stop. The function returns true when the walk ran
/// to the end, false when `visit` stopped it. A callback may make a *later*
/// place appear or disappear, and both are seen: the place is asked about when
/// it is reached, not when the walk started.
///
/// The skipping is what keeps a length of 2^53 affordable. `chain_has_index`
/// says whether anything above the receiver can answer for an index; when it
/// is false, a place is there exactly when the receiver's own element is not a
/// hole, and past the last element no index is there at all, so the walk ends.
template <typename Visit>
static bool walk_present_indices(const JsValue& object, size_t limit, bool chain_has_index, Visit visit) {
    if (limit == 0) return true;
    const bool sparse = object.is_array() && !object.object_val->has_accessors && !chain_has_index;
    if (!sparse) {
        // Ask each index as it is reached, because a callback can make a place
        // appear or a getter can throw — but only while the length is short
        // enough for that to be a walk rather than a wait. A length of 2^53
        // with one place on it takes the places that are there, in order, and
        // asks about each of them as it reaches it. That is the same answer for
        // every place the list names, and it is the only affordable one for a
        // length that cannot be counted.
        if (limit > kMaxWalkedIndices) {
            for (size_t index : present_indices(object, limit).indices) {
                if (!has_index_property(object, index)) continue;
                if (!visit(index)) return false;
            }
            return true;
        }
        for (size_t index = 0; index < limit; ++index) {
            if (!has_index_property(object, index)) continue;
            if (!visit(index)) return false;
        }
        return true;
    }
    size_t index = 0;
    while (index < limit) {
        // A real array with no accessor and no prototype index: the hole bit
        // is the answer, and the first index past the elements is not there.
        for (const auto& elements = object.object_val->array_elements; index < limit; ++index) {
            // The length is read again each step: a callback can shorten the
            // array (`arr.length = 3` inside a forEach stops the loop there),
            // and templating the size once ran the old one to the end.
            if (index >= elements.size()) return true;
            if (object.is_array_hole(index)) continue;
            if (!visit(index)) return false;
        }
        return true;   // nothing past the elements can be there
    }
    if (index >= limit) return true;
    return true;
}

/// Set(O, k, v, true), defined below where the write helpers live: the copy
/// that writes into an array a species constructor made needs it first.
void set_place_or_throw(JsValue object, size_t index, const JsValue& value);

/// CreateDataPropertyOrThrow(O, k, v) — §7.3.6. Not `[[Set]]`: a property the
/// constructor made non-writable but *configurable* is replaced here, which is
/// `map` on `target-array-with-non-writable-property`, while one it made
/// non-configurable cannot be (`target-array-with-non-configurable-property`,
/// a TypeError). `set_place_or_throw` is Set with Throw=true and refuses the
/// first of those, so a method that *defines* its result's places cannot use
/// it (finding 253).
void create_data_property_or_throw(JsValue object, size_t index, const JsValue& value) {
    const std::string key = std::to_string(index);
    JsObject* cell = object.object_val.get();
    if (cell && cell->is_array) {
        // A place that exists as an element is replaced outright — its value
        // and its three attributes, which is what a definition does — unless
        // it was made non-configurable, the one thing a definition may not
        // touch. Nothing is written to the property map: the map is what a
        // *hole* reads through, and this place is not one afterwards.
        const auto entry = cell->properties.find(key);
        if (entry != cell->properties.end() && !(entry->flags & PropertyMap::Configurable) &&
            !object.is_array_hole(index)) {
            js_throw_type_error("Cannot redefine property: " + key);
        }
        // A place past the end is a new property, and an object that takes no
        // new properties refuses it — `target-array-non-extensible` in each of
        // the six methods that build their result this way.
        const bool is_new_place = index >= cell->array_elements.size();
        if (is_new_place && !cell->extensible) {
            js_throw_type_error("Cannot define property " + key + ", object is not extensible");
        }
        if (is_new_place) {
            // The places between the old end and this one are not properties:
            // they are holes, and they must be marked as such or a later
            // `hasOwnProperty` finds places no write ever made (finding 253).
            const size_t previous = cell->array_elements.size();
            cell->array_elements.resize(index + 1, JsValue::make_undefined());
            if (!cell->array_holes) cell->array_holes = std::make_unique<JsObject::Holes>();
            for (size_t i = previous; i < index; ++i) cell->array_holes->set(i);
        }
        cell->array_elements[index] = value;
        object.clear_array_hole(index);
        return;
    }
    // Not an array: the place is a property like any other, and defining a new
    // one is what an object that takes no new properties refuses.
    const bool exists = object.has_own_property(key);
    if (!exists && cell && !cell->extensible) {
        js_throw_type_error("Cannot define property " + key + ", object is not extensible");
    }
    object.define_property(key, value,
                           static_cast<uint8_t>(PropertyMap::Enumerable | PropertyMap::Writable |
                                                PropertyMap::Configurable));
}

/// The places `begin` up to `end` of `object`, written into `target` — the
/// array `Array.prototype.slice` had made for it by ArraySpeciesCreate. A
/// place the source has is its value; a place it does not have is a hole.
static void copy_places_into(const JsValue& object, size_t begin, size_t end, const char* method,
                             JsValue target) {
    const size_t count = end - begin;
    // The dense source and a dense result of the same length: the places are
    // copied as places, and the result is no longer the holes it was made with.
    if (dense_path(object) && !object.object_val->array_holes && end <= elements_of(object).size() &&
        target.is_array() && elements_of(target).size() == count) {
        const auto& elements = elements_of(object);
        auto& into = elements_of(target);
        std::copy(elements.begin() + static_cast<std::ptrdiff_t>(begin),
                  elements.begin() + static_cast<std::ptrdiff_t>(end), into.begin());
        for (size_t i = 0; i < count; ++i) target.clear_array_hole(i);
        return;
    }
    const IndexWalk walk = present_indices(object, end);
    // Every place between `begin` and `end` is asked for: the walk says which
    // ones exist, and a place it did not name stays the hole it is.
    if (count > kMaxWalkedIndices) {
        js_throw_range_error(std::string("Array.prototype.") + method +
                             ": the result would be too long to build");
    }
    for (size_t index : walk.indices) {
        if (index < begin || index >= end) continue;
        create_data_property_or_throw(target, index - begin, get_index(object, index));
    }
}


/// The dense form for a method that reads the places as well as writing them:
/// a place the array does not have must read `undefined`, or the vector would
/// answer differently from Get (which finds what the prototype has there, and
/// runs a getter). `js_array_holes_read_undefined` is that question.
bool dense_readable(const JsValue& object) {
    return dense_path(object) && js_array_holes_read_undefined(object);
}

/// ToObject(this), with the standard's TypeError for null and undefined: a
/// method that writes takes any other value, a primitive's wrapper included
/// (`Array.prototype.reverse.call(true) instanceof Boolean`, as test262 asks).
JsValue this_object(const char* method) {
    JsValue me = self();
    if (me.is_nullish()) {
        js_throw_type_error(std::string("Array.prototype.") + method +
                            " called on null or undefined");
    }
    return js_to_object(me);
}

/// HasProperty(O, k).
bool has_place(JsValue object, size_t index) {
    return object.has_property(std::to_string(index));
}

/// Get(O, k): `a[k]` for an array, holes included.
JsValue get_place(JsValue object, size_t index) {
    return object.is_array() ? js_array_get(object, index)
                             : object.get_property(std::to_string(index));
}

/// True when `key` is an own data property with [[Writable]] false. The
/// engine's `set_property` reports success when the value written is the one a
/// read-only property already holds; the standard's Set with Throw = true calls
/// that a refusal and throws, so the attribute is looked at here.
bool read_only_own(JsValue object, const std::string& key) {
    const JsObject* cell = object.property_cell();
    if (cell != nullptr) {
        const auto entry = cell->properties.find(key);
        if (entry != cell->properties.end()) {
            return !(entry->flags & PropertyMap::Accessor) &&
                   !(entry->flags & PropertyMap::Writable);
        }
    }
    // A function's `length` lives in the function rather than in the property
    // table (a script function may have no table at all yet), and the standard
    // makes it non-writable, which is why
    // `Array.prototype.shift.call(function () {})` is a TypeError.
    return key == "length" && object.is_function();
}

/// Set(O, k, v, true). A refused write is a TypeError, which is what the
/// standard's "true" asks for: a frozen array, a read-only index, a string's
/// own characters.
void set_place_or_throw(JsValue object, size_t index, const JsValue& value) {
    const std::string key = std::to_string(index);
    // Asked before the write: writing the value a read-only property already
    // holds would store a new entry, and then nothing would look read-only.
    if (read_only_own(object, key) || !object.set_property(key, value)) {
        js_throw_type_error("Cannot assign to read only property '" + key + "' of object");
    }
}

/// DeletePropertyOrThrow(O, k).
void delete_place_or_throw(JsValue object, size_t index) {
    if (!object.delete_property(std::to_string(index))) {
        js_throw_type_error("Cannot delete property '" + std::to_string(index) + "' of object");
    }
}

/// Set(O, "length", n, true).
void set_length_or_throw(JsValue object, size_t length) {
    if (read_only_own(object, "length") ||
        !object.set_property("length", JsValue::make_number(static_cast<double>(length)))) {
        js_throw_type_error("Cannot assign to read only property 'length' of object");
    }
}

/// A new array of `length` places, every one of them a hole: what `splice`
/// hands back for a place the source did not have. (The methods that copy —
/// `toReversed`, `toSorted`, `toSpliced`, `with` — are different: they write
/// every place with CreateDataPropertyOrThrow, so `[1, , 3].toReversed()` is
/// three own places with the middle one `undefined`, not a hole. test262's
/// `holes-not-preserved.js` in each of those four directories says so.)
JsValue make_holey_array(size_t length) {
    JsValue array = JsValue::make_array();
    array.object_val->array_elements.assign(length, JsValue::make_undefined());
    for (size_t i = 0; i < length; ++i) array.set_array_hole(i);
    return array;
}

/// The `%Array%` of this realm: what ArraySpeciesCreate falls back to when the
/// object names no species of its own.
JsValue array_intrinsic() {
    const JsValue* ctor = JsIntrinsics::get().find_global("Array");
    return ctor ? *ctor : JsValue::make_undefined();
}

/// ArraySpeciesCreate(originalArray, length) — ECMA-262 §10.4.2.3.
///
/// A receiver that is not an array makes a plain array: the standard asks
/// `IsArray` first, so `Array.prototype.map.call({length: 2, ...})` does *not*
/// consult `Object.prototype.constructor`. An array asks its `constructor` for
/// `Symbol.species`, and that constructor is asked for a new array of `length`.
/// This is what makes `new List().map(f)` a `List`: the engine used to build
/// every result with `JsValue::make_array`, so a subclass could never be made
/// by the built-in it was called on.
JsValue array_species_create(const JsValue& original, size_t length, const char* method) {
    // The two paths that make the array here are ArrayCreate(length): its own
    // limit of 2^32 - 1 places, and the RangeError comes before any
    // allocation. A species constructor of the script's is asked for a length
    // and may answer however it likes, so the limit is not applied to it.
    if (!original.is_array()) {
        array_result_length(length, method);
        return make_holey_array(length);
    }
    JsValue ctor = species_constructor(original, array_intrinsic());
    if (js_same_value(ctor, array_intrinsic())) {
        array_result_length(length, method);
        return make_holey_array(length);
    }
    JsValue made = js_construct(ctor, {JsValue::make_number(static_cast<double>(length))});
    if (!made.is_object_like()) {
        js_throw_type_error(std::string("Array.prototype.") + method +
                            ": the species constructor did not return an object");
    }
    return made;
}

/// Fills an array a species constructor made: CreateDataPropertyOrThrow at
/// every place that has a value, a hole left alone, then the length
/// (§23.1.3.2 steps f–h and the same three steps in its relatives). A species
/// constructor can hand back anything an object can be — a frozen array, an
/// object with setters — and its refusals reach the script as type errors.
JsValue fill_species_result(JsValue target, const std::vector<JsValue>& values,
                            const std::vector<bool>& holes) {
    for (size_t i = 0; i < values.size(); ++i) {
        if (holes[i]) continue;
        create_data_property_or_throw(target, i, values[i]);
    }
    set_length_or_throw(target, values.size());
    return target;
}

// Array iteration uses ToLength on the original receiver. In particular,
// Infinity and lengths above 2^53 - 1 must not be cast directly to size_t:
// out-of-range floating-to-integer conversion is undefined in C++.
size_t iteration_length(const JsValue& receiver) {
    const double length = js_to_integer(js_to_number(receiver.get_property("length")));
    if (length <= 0) return 0;
    return static_cast<size_t>(std::min(length, 9007199254740991.0));
}

/// Resolves a relative index argument (negative counts from the end) into
/// [0, length].
size_t relative_index(const JsValue& arg, size_t length, size_t fallback) {
    if (arg.is_undefined()) return fallback;
    double k = js_to_integer(js_to_number(arg));
    if (k < 0) {
        k += static_cast<double>(length);
        return k < 0 ? 0 : static_cast<size_t>(k);
    }
    return k > static_cast<double>(length) ? length : static_cast<size_t>(k);
}

JsValue require_callback(const JsValue& fn, const char* method) {
    if (!fn.is_function()) {
        js_throw_type_error(js_describe_thrown(fn) + " is not a function (in Array.prototype." + method + ")");
    }
    return fn;
}

thread_local std::vector<const JsObject*> t_joining;

std::string join_array(const JsValue& array, const std::string& separator) {
    const JsObject* cell = array.object_val;
    // An array that contains itself joins as an empty string instead of
    // recursing forever.
    if (std::find(t_joining.begin(), t_joining.end(), cell) != t_joining.end()) return std::string();
    t_joining.push_back(cell);
    // The guard erases *this* cell, not whatever happens to be last: joining
    // an element runs a script (`[1,2].toString()` joins while the outer join
    // is still running), and popping the back there removed the inner cell and
    // left this one in the list for good, so every later join answered an
    // empty string.
    struct Pop {
        const JsObject* cell;
        ~Pop() {
            auto at = std::find(t_joining.begin(), t_joining.end(), cell);
            if (at != t_joining.end()) t_joining.erase(at);
        }
    } pop{cell};

    const size_t length = length_of(array);
    // The places that are there, and nothing about the places that are not: a
    // hole joins as `undefined` does, which is empty, and there can be 2^53-1
    // of them. `{ length: 2**31 }` used to be a copy of every place first.
    const size_t separator_bytes = separator.size();
    if (length > 1 && separator_bytes > 0 && length - 1 > kMaxJoinedBytes / separator_bytes) {
        // No engine builds a string that long: a string has a maximum length,
        // and going past it is a RangeError rather than a process that never
        // answers.
        js_throw_range_error("Invalid string length");
    }
    const IndexWalk walk = present_indices(array, length);
    std::string out;
    size_t place = 0;
    for (size_t index : walk.indices) {
        if (!has_index_property(array, index)) continue;
        for (; place < index; ++place) { if (place) out += separator; }
        if (place) out += separator;
        ++place;
        JsValue item = get_index(array, index);
        if (item.type == JsType::String) out += item.string_val.view();
        else if (!item.is_nullish()) out += js_to_string(item);
    }
    // The places after the last one that is there are empty but still
    // separated: a separator before each of them, and nothing to read.
    // `place` has already been counted, so every remaining place owes one
    // separator, not one fewer — `{length:4, 1:1}` is ",1,,": two places
    // after the last that is there, and two separators.
    if (place < length) {
        // Every place after the first owes one separator, whether or not it
        // has a value: the first of these owes one only if something was
        // already written, and the rest owe one each.
        size_t owed = length - place;
        if (place == 0) --owed;              // the leading place owes none
        if (separator.empty()) {
            out.append(owed, ',');
        } else {
            for (size_t k = 0; k < owed; ++k) out += separator;
        }
    }
    return out;
}

/// Stable merge sort. Unlike std::sort it stays memory-safe when a script
/// comparator is inconsistent.
template <typename Less>
void merge_sort(std::vector<JsValue>& items, Less less) {
    if (items.size() < 2) return;
    std::vector<JsValue> buffer(items.size());
    for (size_t width = 1; width < items.size(); width *= 2) {
        for (size_t lo = 0; lo < items.size(); lo += 2 * width) {
            size_t mid = std::min(lo + width, items.size());
            size_t hi = std::min(lo + 2 * width, items.size());
            size_t a = lo, b = mid, out = lo;
            while (a < mid && b < hi) {
                if (less(items[b], items[a])) buffer[out++] = items[b++];
                else buffer[out++] = items[a++];
            }
            while (a < mid) buffer[out++] = items[a++];
            while (b < hi) buffer[out++] = items[b++];
        }
        items.swap(buffer);
    }
}

/// `holes` is carried alongside `items` so that sort can put the holes at the
/// end, as the standard does, without asking the comparator about them.
void sort_values(std::vector<JsValue>& items, std::vector<bool>& holes, const JsValue& comparator) {
    // undefined always sorts to the end and is never passed to the comparator;
    // a hole sorts after it, and is never passed either (the standard's SortCompare
    // is not reached for either, and the holes end up after every element).
    std::vector<JsValue> defined;
    size_t undefined_count = 0;
    size_t hole_count = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (holes[i]) { ++hole_count; continue; }
        if (items[i].is_undefined()) { ++undefined_count; continue; }
        defined.push_back(std::move(items[i]));
    }
    if (comparator.is_function()) {
        merge_sort(defined, [&](const JsValue& a, const JsValue& b) {
            double order = js_to_number(js_call(comparator, JsValue(), {a, b}));
            return order < 0;
        });
    } else {
        // Default order compares the string forms, so [10, 9, 1] sorts as 1, 10, 9.
        // The order is by UTF-16 code unit, like `<`. An element that is a
        // string already lends its text as the key; nothing is copied.
        std::vector<std::pair<JsString, JsValue>> keyed;
        keyed.reserve(defined.size());
        for (auto& item : defined) keyed.emplace_back(js_to_js_string(item), std::move(item));
        std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) {
            return JsString::compare(a.first.view(), b.first.view()) < 0;
        });
        defined.clear();
        for (auto& entry : keyed) defined.push_back(std::move(entry.second));
    }
    items = std::move(defined);
    items.resize(items.size() + undefined_count + hole_count);
    holes.assign(items.size(), false);
    // The holes are the last places of all, after the elements that are undefined.
    for (size_t i = items.size() - hole_count; i < items.size(); ++i) holes[i] = true;
}

/// `holes` says, per place of `out`, whether it is a hole. A hole of the array
/// being flattened is skipped (it has no element to move); a hole of an inner
/// array is a place of the result that is itself a hole — `flat` copies from
/// the inner array, and copying a hole leaves one.
void flatten_into(std::vector<JsValue>& out, std::vector<bool>& holes, const JsValue& array, double depth) {
    js_check_stack();       // [[[[...]]]].flat(Infinity) recurses once per level
    const size_t length = length_of(array);
    // `flat` asks HasProperty: a place that is not there is neither flattened
    // nor an element, so a receiver with a huge `length` and no places costs
    // nothing (there used to be a copy of every place first).
    const IndexWalk walk = present_indices(array, length);
    for (size_t i : walk.indices) {
        if (!has_index_property(array, i)) continue;
        JsValue item = get_index(array, i);
        if (depth >= 1 && item.is_array()) flatten_into(out, holes, item, depth - 1);
        else { out.push_back(std::move(item)); holes.push_back(false); }
    }
}JsValue iteration_result(const JsValue& value, bool done) {
    JsValue result = JsValue::make_object();
    result.set_property("value", value);
    result.set_property("done", JsValue::make_boolean(done));
    return result;
}

} // namespace

namespace {

/// What an iterator keeps: the step, and whether it has finished. The step is a
/// std::function the collector cannot see into, so the values it captured are
/// held separately by trace_extra (see js_make_iterator).
struct IteratorState {
    std::function<bool(JsValue& value)> step;
    bool finished{false};
};

/// The state behind `this`, for the `next` on an iterator's prototype. A
/// `next` called on anything that is not one of these iterators throws, which
/// is what the language says a prototype method does with a wrong receiver.
IteratorState& iterator_state(const JsValue& me) {
    if (!me.is_object_like() || me.object_val->klass != ObjectClass::Iterator || !me.object_val->internal) {
        js_throw_type_error("next method called on an object that is not an iterator");
    }
    return *static_cast<IteratorState*>(me.object_val->internal.get());
}

/// GetIteratorFromMethod + the loop over IteratorStep (ECMA-262 7.4.3,
/// 7.4.8) for a method the caller has already looked up.
///
/// `js_iterate()` looks the method up itself, so a caller that has to know
/// whether the source is iterable before it builds the result — `Array.from`
/// does: the iterable path constructs with no argument and the array-like path
/// with the length — read `@@iterator` twice, and a getter for it ran twice
/// for one iteration. `[...a]`, `for (var x of a)` and `Array.from(a)` are one
/// read each in the standard and are one read each here now.
///
/// The direct read of an array or a string whose method and `next` are still
/// the realm's is kept, and runs no script.
void iterate_with_method(const JsValue& iterable, const JsValue& method,
                         const std::function<bool(const JsValue&)>& visit) {
    if (js_iterates_as_builtin(iterable)) {
        js_iterate(iterable, visit);
        return;
    }
    JsValue iterator = method.call_with_this(iterable, {});
    if (!iterator.is_object_like()) {
        js_throw_type_error("Result of the Symbol.iterator method is not an object");
    }
    JsValue next = iterator.get_property("next");
    while (true) {
        const JsValue step = js_call(next, iterator, {});
        if (!step.is_object_like()) {
            js_throw_type_error("Iterator result " + js_describe_thrown(step) + " is not an object");
        }
        if (step.get_property("done").is_truthy()) return;
        const JsValue value = step.get_property("value");
        bool keep_going;
        try {
            keep_going = visit(value);
        } catch (...) {
            // IteratorClose with an error of the loop's own: that error is the
            // one the script sees, and what `return` does is dropped.
            try {
                JsValue closing = iterator.get_property("return");
                if (closing.is_function()) closing.call_with_this(iterator, {});
            } catch (...) {
            }
            throw;
        }
        if (!keep_going) {
            // IteratorClose for a loop that stopped: what `return` throws is
            // thrown. Nothing here stops early — the visit always answers true
            // — but the rule is the loop's, not the caller's.
            JsValue closing = iterator.get_property("return");
            if (!closing.is_nullish()) {
                if (!closing.is_function()) {
                    js_throw_type_error(js_describe_thrown(closing) + " is not a function");
                }
                const JsValue result = closing.call_with_this(iterator, {});
                if (!result.is_object_like()) {
                    js_throw_type_error("Iterator result " + js_describe_thrown(result) + " is not an object");
                }
            }
            return;
        }
    }
}

} // namespace

JsValue js_make_iterator(std::function<bool(JsValue& value)> step, std::vector<JsValue> roots, JsObject* proto) {
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = proto ? proto : JsIntrinsics::get().iterator_proto;
    cell->klass = ObjectClass::Iterator;
    cell->internal = std::make_shared<IteratorState>(IteratorState{std::move(step), false});
    if (!roots.empty()) {
        // `step` holds these inside a std::function, where the collector cannot see them.
        auto kept = std::make_shared<std::vector<JsValue>>(std::move(roots));
        cell->trace_extra = [kept](Heap& heap) {
            if (!heap.first_visit(kept.get())) return;
            for (const auto& value : *kept) heap.mark_value(value);
        };
    }
    return JsValue::make_object(cell);
}

void js_install_iterator_prototype(JsObject* proto, const char* tag) {
    // One `next` for every iterator of the kind: the step is the object's, the
    // function is the prototype's, and replacing the property here changes
    // what every loop over one of these iterators does -- which is why the
    // iteration fast paths ask whether it is still this function.
    js_define_method(proto, "next", 0, [](Args) {
        IteratorState& state = iterator_state(self());
        JsValue value;
        if (!state.finished && state.step(value)) return iteration_result(value, false);
        state.finished = true;
        return iteration_result(JsValue::make_undefined(), true);
    });
    if (tag) {
        auto& entry = proto->properties.entry("@@toStringTag");
        entry.second = JsValue::make_string(tag);
        entry.flags = PropertyMap::Configurable;   // { writable: false, enumerable: false, configurable: true }
    }
}

void install_array_builtins(JsIntrinsics& realm) {
    JsObject* ap = realm.array_proto;

    // Iterators are themselves iterable: %IteratorPrototype% carries @@iterator
    // and nothing else, and each kind of iterator's prototype sits between it
    // and the iterator (see js_install_iterator_prototype).
    js_define_method(realm.iterator_proto, kIteratorKey, 0, [](Args) { return self(); });
    js_define_method(realm.iterator_proto, "Symbol.iterator", 0, [](Args) { return self(); });
    js_install_iterator_prototype(realm.array_iterator_proto, "Array Iterator");

    // ---- Constructor ------------------------------------------------------
    JsValue array_ctor = js_make_native("Array", 1, [](Args args) {
        if (args.size() == 1 && args[0].type == JsType::Number) {
            double length = args[0].number_val;
            if (!(length >= 0) || length != std::floor(length) || length > 4294967295.0) {
                js_throw_range_error("Invalid array length");
            }
            if (length > (1u << 26)) js_throw_range_error("Array length exceeds the supported maximum");
            // `new Array(3)` makes three holes, not three elements that hold undefined.
            JsValue created = JsValue::make_array(std::vector<JsValue>(static_cast<size_t>(length)));
            for (size_t i = 0; i < static_cast<size_t>(length); ++i) created.set_array_hole(i);
            return created;
        }
        return JsValue::make_array(args);
    });
    array_ctor.define_property("prototype", js_object_value(ap), 0);
    js_define_hidden(ap, "constructor", array_ctor);
    // Array[Symbol.species] (§23.1.2.5): the constructor itself, so that a
    // subclass which names nothing gets its own constructor back from
    // SpeciesConstructor and `Array.prototype.map` makes one of *it*.
    JsValue species_getter = js_make_native("get [Symbol.species]", 0, [](Args) { return self(); });
    // A getter has [[Call]] and not [[Construct]]: without this the species
    // getter could be `new`-ed, and the standard says it cannot.
    species_getter.function_cell()->not_constructor = true;
    array_ctor.define_accessor(kSymbolSpeciesKey, species_getter,
                               JsValue::make_undefined(), PropertyMap::Configurable);

    js_define_method(array_ctor, "isArray", 1, [](Args args) {
        const JsValue& value = js_arg(args, 0);
        return JsValue::make_boolean(value.is_array() && value.object_val->klass != ObjectClass::Arguments);
    });
    // Array.of ( ...items ) (§23.1.2.3): the *constructor* the method was
    // called on makes the result, so `List.of(1)` is a `List` and
    // `Array.of(1)` is an `Array`. `this` is asked before the first place is
    // written, and what it makes is filled with CreateDataPropertyOrThrow.
    js_define_method(array_ctor, "of", 0, [](Args args) {
        const JsValue ctor = self();
        // A receiver that is not a constructor makes a plain array rather than
        // a TypeError: that is what V8 does, and Node is this corpus's
        // reference engine (`Array.of.call({}, 1, 2)` is `[1, 2]` there).
        JsValue made;
        if (!js_is_constructor(ctor)) {
            array_result_length(args.size(), "of");
            made = make_holey_array(args.size());
        } else {
            made = js_construct(ctor, {JsValue::make_number(static_cast<double>(args.size()))});
        }
        if (!made.is_object_like()) {
            js_throw_type_error("Array.of: the constructor did not return an object");
        }
        std::vector<JsValue> values(args.begin(), args.end());
        return fill_species_result(made, values, std::vector<bool>(values.size(), false));
    });
    js_define_method(array_ctor, "from", 1, [](Args args) {
        // Array.from ( items [ , mapfn [ , thisArg ] ] ), in the order of the
        // standard: the mapper is checked, then the source is *asked* for its
        // iterator (GetMethod(@@iterator)), and only a source that has none is
        // read as an array-like. An array and a string are asked like anything
        // else, so deleting the method from `Array.prototype` makes an array
        // array-like and not a `TypeError`, and a number whose prototype has
        // one is iterated (Phase 94).
        const JsValue& source = js_arg(args, 0);
        const JsValue& map_fn = js_arg(args, 1);
        const JsValue& this_arg = js_arg(args, 2);
        const bool mapping = !map_fn.is_undefined();
        if (mapping && !map_fn.is_function()) js_throw_type_error("Array.from: mapper is not a function");
        // `this` is the constructor that makes the result (§23.1.2.1 step 2).
        // A receiver that is not a constructor makes a plain array, which is
        // what V8 does and what this corpus is measured against.
        const JsValue ctor = self();
        const bool constructible = js_is_constructor(ctor);
        // The iterable path constructs with no arguments and grows the result
        // one place at a time; the array-like path knows its length first and
        // constructs with it. Both are in the standard, and the difference is
        // visible to a constructor that looks at its argument.
        auto make_result = [&](size_t length) {
            if (!constructible) {
                const size_t plain_length = length == static_cast<size_t>(-1) ? 0 : length;
                array_result_length(plain_length, "from");
                return make_holey_array(plain_length);
            }
            JsValue made = length == static_cast<size_t>(-1)
                               ? js_construct(ctor, {})
                               : js_construct(ctor, {JsValue::make_number(static_cast<double>(length))});
            if (!made.is_object_like()) {
                js_throw_type_error("Array.from: the constructor did not return an object");
            }
            return made;
        };

        auto mapped = [&](const JsValue& value, size_t index) {
            if (!mapping) return value;
            return map_fn.call_with_this(this_arg, {value, JsValue::make_number(static_cast<double>(index))});
        };

        std::vector<JsValue> items;
        JsValue method;
        const bool iterable = js_iterator_method(source, method);
        if (iterable) {
            // Mapped as each value arrives, not after the last one: a mapper
            // that throws closes the iterator, and an iterator without an end
            // costs one value, not all of them.
            JsValue made = make_result(static_cast<size_t>(-1));
            iterate_with_method(source, method, [&](const JsValue& value) {
                const JsValue item = mapped(value, items.size());
                // §23.1.2.1 step 5.g.iii calls CreateDataPropertyOrThrow: `made`
                // is the answer being built, not a receiver being moved, so a
                // property the chain answers for must never redirect it
                // (finding 269).
                create_data_property_or_throw(made, items.size(), item);
                items.push_back(item);
                return true;
            });
            set_length_or_throw(made, items.size());
            return made;
        } else {
            // ToObject(items), then ToLength(Get(items, "length")), then each
            // index as a property — which a string answers one code unit at a
            // time, and a hole in an array answers as `undefined`.
            if (source.is_nullish()) js_throw_type_error(js_describe_thrown(source) + " is not iterable");
            const double length_number = js_to_number(source.get_property("length"));
            double length = std::isnan(length_number) ? 0.0 : std::floor(length_number);
            if (!(length > 0)) length = 0;
            if (length > 9007199254740991.0) length = 9007199254740991.0; // 2^53 - 1
            if (length > (1u << 26)) js_throw_range_error("Invalid array length");
            JsValue made = make_result(static_cast<size_t>(length));
            for (double i = 0; i < length; ++i) {
                const JsValue item = mapped(source.get_property(js_number_to_string(i)), static_cast<size_t>(i));
                create_data_property_or_throw(made, static_cast<size_t>(i), item);
                items.push_back(item);
            }
            set_length_or_throw(made, items.size());
            return made;
        }
    });
    realm.add_global("Array", array_ctor);

    // ---- Stack and queue --------------------------------------------------
    js_define_method(ap, "push", 1, [](Args args) {
        JsValue array = this_object("push");
        if (!dense_path(array)) {
            const size_t length = length_of(array);
            // The standard's own limit, which `push` names because it is the
            // one method here that can pass it without being asked for a count.
            if (static_cast<double>(length) + static_cast<double>(args.size()) >
                9007199254740991.0) {
                js_throw_type_error("Invalid array length");
            }
            for (size_t i = 0; i < args.size(); ++i) {
                set_place_or_throw(array, length + i, args[i]);
            }
            set_length_or_throw(array, length + args.size());
            return JsValue::make_number(static_cast<double>(length + args.size()));
        }
        if (array.object_val->frozen) js_throw_type_error("Cannot add property, object is not extensible");
        // The elements go in at the end and the length follows them, so an
        // array whose length cannot be raised takes nothing: `push` on a
        // frozen length with places to add is the TypeError of the Set that
        // `length` refuses (finding 253).
        if (!args.empty() && read_only_own(array, "length")) {
            js_throw_type_error("Cannot assign to read only property 'length' of object");
        }
        auto& elements = elements_of(array);
        const size_t start = elements.size();
        elements.insert(elements.end(), args.begin(), args.end());
        for (size_t i = start; i < elements.size(); ++i) array.clear_array_hole(i);
        return JsValue::make_number(static_cast<double>(elements.size()));
    });
    js_define_method(ap, "pop", 0, [](Args) {
        JsValue array = this_object("pop");
        if (!dense_readable(array)) {
            const size_t length = length_of(array);
            if (length == 0) {
                set_length_or_throw(array, 0);
                return JsValue::make_undefined();
            }
            const JsValue last = get_place(array, length - 1);
            delete_place_or_throw(array, length - 1);
            set_length_or_throw(array, length - 1);
            return last;
        }
        auto& elements = elements_of(array);
        if (elements.empty()) return JsValue::make_undefined();
        // `pop` reads the last place as `a[i]` does and then deletes it. For
        // a hole that is `undefined`, unless the index has an accessor of its
        // own or the prototype has the index; and an accessor goes with the
        // place, or it would be there again when the array grew back.
        const size_t last_index = elements.size() - 1;
        if (!array.is_array_hole(last_index)) {
            JsValue last = std::move(elements.back());
            elements.pop_back();
            return last;
        }
        JsValue last = js_array_get(array, last_index);
        // The getter may have changed the array: take the place only if it is still the last.
        if (elements.size() == last_index + 1) {
            if (array.object_val->has_accessors) {
                const std::string key = std::to_string(last_index);
                auto mapped = array.object_val->properties.find(key);
                if (mapped != array.object_val->properties.end()) {
                    if (!(mapped->flags & PropertyMap::Configurable)) {
                        js_throw_type_error("Cannot delete property '" + key + "' of [object Array]");
                    }
                    array.object_val->properties.erase(key);
                }
            }
            elements.pop_back();
        }
        return last;
    });
    js_define_method(ap, "shift", 0, [](Args) {
        JsValue array = this_object("shift");
        if (!dense_readable(array)) {
            const size_t length = length_of(array);
            if (length == 0) {
                set_length_or_throw(array, 0);
                return JsValue::make_undefined();
            }
            const JsValue first = get_place(array, 0);
            for (size_t k = 1; k < length; ++k) {
                if (has_place(array, k)) set_place_or_throw(array, k - 1, get_place(array, k));
                else delete_place_or_throw(array, k - 1);
            }
            delete_place_or_throw(array, length - 1);
            set_length_or_throw(array, length - 1);
            return first;
        }
        auto& elements = elements_of(array);
        if (elements.empty()) return JsValue::make_undefined();
        // The first place as `a[0]` reads it (see `pop`).
        JsValue first = array.is_array_hole(0) ? js_array_get(array, 0) : std::move(elements.front());
        if (elements.empty()) return first;       // a getter emptied the array
        elements.erase(elements.begin());
        if (array.object_val->array_holes) array.object_val->array_holes->shift(0, -1);
        return first;
    });
    js_define_method(ap, "unshift", 1, [](Args args) {
        JsValue array = this_object("unshift");
        if (!dense_readable(array)) {
            const size_t length = length_of(array);
            if (static_cast<double>(length) + static_cast<double>(args.size()) > 9007199254740991.0) {
                js_throw_type_error("Invalid array length");
            }
            if (!args.empty()) {
                for (size_t k = length; k > 0; --k) {
                    const size_t from = k - 1;
                    const size_t to = from + args.size();
                    if (has_place(array, from)) {
                        set_place_or_throw(array, to, get_place(array, from));
                    } else {
                        delete_place_or_throw(array, to);
                    }
                }
                for (size_t i = 0; i < args.size(); ++i) {
                    set_place_or_throw(array, i, args[i]);
                }
            }
            set_length_or_throw(array, length + args.size());
            return JsValue::make_number(static_cast<double>(length + args.size()));
        }
        // Places at the front, and the length under them: an array whose
        // length cannot grow takes nothing, for the same reason `push` does
        // (finding 253).
        if (!args.empty() && read_only_own(array, "length")) {
            js_throw_type_error("Cannot assign to read only property 'length' of object");
        }
        auto& elements = elements_of(array);
        if (array.object_val->array_holes) {
            array.object_val->array_holes->shift(0, static_cast<std::ptrdiff_t>(args.size()));
        }
        elements.insert(elements.begin(), args.begin(), args.end());
        for (size_t i = 0; i < args.size(); ++i) array.clear_array_hole(i);
        return JsValue::make_number(static_cast<double>(elements.size()));
    });

    // ---- Copying and slicing ----------------------------------------------
    js_define_method(ap, "slice", 2, [](Args args) {
        JsValue source = receiver("slice");
        const size_t length = array_like_length(source, "slice");
        const size_t begin = relative_index(js_arg(args, 0), length, 0);
        const size_t end = relative_index(js_arg(args, 1), length, length);
        const size_t count = begin >= end ? 0 : end - begin;
        JsValue kept = array_species_create(source, count, "slice");
        if (count > 0) copy_places_into(source, begin, end, "slice", kept);
        set_length_or_throw(kept, count);
        return kept;
    });
    auto splice_impl = []<typename Seq>(Seq& elements, Args args) {
        size_t start = relative_index(js_arg(args, 0), elements.size(), 0);
        size_t delete_count = 0;
        if (args.size() == 1) {
            delete_count = elements.size() - start;
        } else if (args.size() >= 2) {
            double requested = js_to_integer(js_to_number(args[1]));
            if (requested < 0) requested = 0;
            delete_count = static_cast<size_t>(std::min(requested, static_cast<double>(elements.size() - start)));
        }
        std::vector<JsValue> removed(elements.begin() + static_cast<std::ptrdiff_t>(start),
                                     elements.begin() + static_cast<std::ptrdiff_t>(start + delete_count));
        elements.erase(elements.begin() + static_cast<std::ptrdiff_t>(start),
                       elements.begin() + static_cast<std::ptrdiff_t>(start + delete_count));
        if (args.size() > 2) {
            elements.insert(elements.begin() + static_cast<std::ptrdiff_t>(start), args.begin() + 2, args.end());
        }
        return removed;
    };
    js_define_method(ap, "splice", 2, [splice_impl](Args args) {
        JsValue array = this_object("splice");
        // The length is taken once, before any argument is asked for its
        // number: a `valueOf` can shorten the array, and the places that move
        // are the ones that length names, not the ones there are now.
        const size_t length = length_of(array);
        if (args.empty()) {
            // `splice()` removes nothing, but step 19 still writes `length`,
            // and that write is the one a read-only `length` refuses.
            set_length_or_throw(array, length);
            JsValue none = array_species_create(array, 0, "splice");
            set_length_or_throw(none, 0);
            return none;
        }
        const size_t start = relative_index(js_arg(args, 0), length, 0);
        size_t dropped = 0;
        if (args.size() == 1) {
            dropped = length - start;
        } else {
            double requested = js_to_integer(js_to_number(args[1]));
            if (requested < 0) requested = 0;
            dropped = static_cast<size_t>(std::min(requested, static_cast<double>(length - start)));
        }
        const size_t inserted = args.size() > 2 ? args.size() - 2 : 0;
        if (static_cast<double>(length) + static_cast<double>(inserted) -
                static_cast<double>(dropped) > 9007199254740991.0) {
            js_throw_type_error("Invalid array length");
        }
        if (!dense_readable(array) || elements_of(array).size() != length) {
            // The standard's form: the removed places are read through Get,
            // the places that move are read and written one at a time (so a
            // getter runs, in the order the standard names), and a place the
            // object does not have is deleted where it lands.
            // ArraySpeciesCreate(O, actualDeleteCount), before the places move
            // (§23.1.3.31 step 14).
            JsValue removed = array_species_create(array, dropped, "splice");
            for (size_t i = 0; i < dropped; ++i) {
                // The array `splice` returns is built, not moved: §23.1.3.31
                // step 20.b calls CreateDataPropertyOrThrow on it, so a place
                // the chain answers for cannot swallow the value
                // (finding 269).
                if (has_place(array, start + i)) create_data_property_or_throw(removed, i, get_place(array, start + i));
            }
            set_length_or_throw(removed, dropped);
            if (inserted < dropped) {
                for (size_t k = start; k < length - dropped; ++k) {
                    if (has_place(array, k + dropped)) {
                        set_place_or_throw(array, k + inserted, get_place(array, k + dropped));
                    } else {
                        delete_place_or_throw(array, k + inserted);
                    }
                }
                for (size_t k = length; k > length - dropped + inserted; --k) {
                    delete_place_or_throw(array, k - 1);
                }
            } else if (inserted > dropped) {
                for (size_t k = length - dropped; k > start; --k) {
                    if (has_place(array, k + dropped - 1)) {
                        set_place_or_throw(array, k + inserted - 1, get_place(array, k + dropped - 1));
                    } else {
                        delete_place_or_throw(array, k + inserted - 1);
                    }
                }
            }
            for (size_t i = 0; i < inserted; ++i) set_place_or_throw(array, start + i, args[i + 2]);
            set_length_or_throw(array, length - dropped + inserted);
            return removed;
        }
        // Which of the removed places were holes, before the erase moves them.
        std::vector<bool> removed_holes;
        {
            for (size_t i = 0; i < dropped; ++i) removed_holes.push_back(array.is_array_hole(start + i));
            const std::ptrdiff_t delta = static_cast<std::ptrdiff_t>(args.size() - 2) -
                                         static_cast<std::ptrdiff_t>(dropped);
            if (array.object_val->array_holes && delta != 0) {
                array.object_val->array_holes->shift(start, delta);
            }
            for (size_t i = start; i < start + dropped; ++i) array.clear_array_hole(i);
        }
        JsValue result = array_species_create(array, removed_holes.size(), "splice");
        return fill_species_result(result, splice_impl(elements_of(array), args), removed_holes);
    });
    js_define_method(ap, "toSpliced", 2, [](Args args) {
        // The length, then where to start and how many to drop, then the
        // places: the standard's order. The places that are dropped are not
        // read, so a getter on one of them is not called; and what the
        // arguments are is asked once, before any place is read.
        JsValue source = this_object("toSpliced");
        const size_t length = length_of(source);
        const size_t start = relative_index(js_arg(args, 0), length, 0);
        size_t dropped = 0;
        if (args.size() == 1) {
            dropped = length - start;
        } else if (args.size() >= 2) {
            double requested = js_to_integer(js_to_number(args[1]));
            if (requested < 0) requested = 0;
            dropped = static_cast<size_t>(std::min(requested, static_cast<double>(length - start)));
        }
        const size_t inserted = args.size() > 2 ? args.size() - 2 : 0;
        // The answer is what has to be a legal array length: dropping a huge
        // range from a huge length leaves a small array, and that is fine. The
        // standard's two limits, in its order: past 2^53 - 1 the answer cannot
        // be a length at all (TypeError, before a single place is read), and
        // past 2^32 - 1 this engine cannot make an array of it (RangeError).
        const double new_length = static_cast<double>(length) - static_cast<double>(dropped) +
                                  static_cast<double>(inserted);
        if (new_length > 9007199254740991.0) js_throw_type_error("Invalid array length");
        if (new_length > 4294967295.0) js_throw_range_error("Invalid array length");
        if (dense_readable(source) && !source.object_val->array_holes) {
            std::vector<JsValue> copy(elements_of(source).begin(), elements_of(source).end());
            copy.resize(length, JsValue::make_undefined());
            copy.erase(copy.begin() + static_cast<std::ptrdiff_t>(start),
                       copy.begin() + static_cast<std::ptrdiff_t>(start + dropped));
            if (inserted > 0) {
                copy.insert(copy.begin() + static_cast<std::ptrdiff_t>(start), args.begin() + 2,
                            args.end());
            }
            return JsValue::make_array(copy);
        }
        // The three runs copied place by place, every place written: before
        // the drop, the inserted places, and what follows. A place the source
        // does not have is read as `undefined` and written as own.
        std::vector<JsValue> copy(length - dropped + inserted, JsValue::make_undefined());
        size_t index = 0;
        for (size_t k = 0; k < start; ++k, ++index) copy[index] = get_place(source, k);
        for (size_t i = 0; i < inserted; ++i) copy[index++] = args[i + 2];
        for (size_t k = start + dropped; k < length; ++k, ++index) copy[index] = get_place(source, k);
        return JsValue::make_array(copy);
    });
    js_define_method(ap, "concat", 1, [](Args args) {
        JsValue source = receiver("concat");
        std::vector<JsValue> values;
        std::vector<bool> holes;
        auto append_places = [&](const JsValue& from) {
            const size_t length = array_like_length(from, "concat");
            const IndexWalk walk = present_indices(from, length);
            size_t next = 0;
            for (size_t index : walk.indices) {
                for (; next < index; ++next) { values.push_back(JsValue::make_undefined()); holes.push_back(true); }
                values.push_back(get_index(from, index));
                holes.push_back(false);
                ++next;
            }
            // The places after the last one there is are holes too, and the
            // result has them: `[0].length = 3` concatenates to a length of 3
            // whose last two places are the holes they were (§23.1.3.2 step
            // 5.b.iii — no definition, so no own property).
            for (; next < length; ++next) { values.push_back(JsValue::make_undefined()); holes.push_back(true); }
        };
        append_places(source);
        for (const auto& given : args) {
            if (given.is_array()) append_places(given);
            else { values.push_back(given); holes.push_back(false); }
        }
        // ArraySpeciesCreate(O, 0), and each element written into the result
        // the species constructor made (§23.1.3.2 steps 5–9).
        JsValue joined = array_species_create(source, 0, "concat");
        return fill_species_result(joined, values, holes);
    });
    js_define_method(ap, "join", 1, [](Args args) {
        JsValue array = receiver("join");
        std::string separator = js_arg(args, 0).is_undefined() ? "," : js_to_string(js_arg(args, 0));
        return JsValue::make_string(join_array(array, separator));
    });
    // Array.prototype.toString is the standard's generic form: `join`, looked
    // up **on this** by name, not this engine's join called directly. Without
    // it an array fell through to Object.prototype.toString and
    // `[1,2].toString()` was "[object Array]", which is why joining an array
    // into itself was not the text the test262 cases expect.
    js_define_method(ap, "toString", 0, [](Args) {
        JsValue array = self();
        if (array.is_nullish()) {
            js_throw_type_error("Array.prototype.toString called on null or undefined");
        }
        JsValue join = array.get_property("join");
        if (!join.is_function()) {
            // Object.prototype.toString's answer for this receiver, which is
            // what the standard's fallback produces.
            return JsValue::make_string(array.is_array() ? "[object Array]" : "[object Object]");
        }
        return join.call_with_this(array, {});
    });
    // §23.1.3.28: not a join. Every place is asked for the element first
    // (Get(O, k)), and the element that is not `undefined` or `null` is asked
    // for its own `toLocaleString` and that is called with the *boxed*
    // element as `this`. A hole is never asked for, and nor is `undefined`:
    // both are the empty string, and only the separators remain.
    //
    // Until this it was `join_array(receiver, ",")`, so
    // `Number.prototype.toLocaleString = () => "X"` left `[1,2]` as "1,2"
    // and `[1,,3]` as "1,,3" (finding 196).
    js_define_method(ap, "toLocaleString", 0, [](Args) {
        JsValue array = receiver("toLocaleString");
        const JsValue* primitive = js_wrapped_primitive(array);
        const JsValue object = (primitive && (primitive->type == JsType::String ||
                                              primitive->type == JsType::Number ||
                                              primitive->type == JsType::Boolean))
                                   ? *primitive
                                   : array;
        const size_t length = length_of(object);
        std::string out;
        for (size_t index = 0; index < length; ++index) {
            if (index) out += ",";
            // Get, not HasProperty: a place the prototype has is read, and a
            // hole reads `undefined` without running anything.
            const JsValue element = get_index(object, index);
            if (element.is_nullish()) continue;
            // Invoke(nextElement, "toLocaleString"): §23.1.3.28 step 7.c
            // passes the element it read, primitive and all — there is no
            // ToObject in the step. A strict method then sees the number it
            // was given and a sloppy one the call boxes it into, which is what
            // test262's `primitive_this_value` and `…_getter` ask for.
            const JsValue method = element.get_property("toLocaleString");
            if (!method.is_function()) {
                js_throw_type_error("Array.prototype.toLocaleString: the element has no toLocaleString");
            }
            out += js_to_string(method.call_with_this(element, {}));
        }
        return JsValue::make_string(out);
    });
    js_define_method(ap, "reverse", 0, [](Args) {
        JsValue array = this_object("reverse");
        if (!dense_readable(array)) {
            // The standard's form: every place asked for, read and written,
            // because a place can be an accessor, an index the object does not
            // have, or an index only its prototype has.
            const size_t length = length_of(array);
            for (size_t lower = 0; lower < length / 2; ++lower) {
                const size_t upper = length - 1 - lower;
                // Asked and read one place at a time, in the standard's
                // order: reading the lower one can run a getter that changes
                // whether the upper one is there at all.
                const bool lower_exists = has_place(array, lower);
                JsValue lower_value;
                if (lower_exists) lower_value = get_place(array, lower);
                const bool upper_exists = has_place(array, upper);
                JsValue upper_value;
                if (upper_exists) upper_value = get_place(array, upper);
                if (upper_exists) set_place_or_throw(array, lower, upper_value);
                else delete_place_or_throw(array, lower);
                if (lower_exists) set_place_or_throw(array, upper, lower_value);
                else delete_place_or_throw(array, upper);
            }
            return array;
        }
        std::reverse(elements_of(array).begin(), elements_of(array).end());
        if (array.object_val->array_holes) {
            auto& bits = array.object_val->array_holes->bits;
            const size_t total = elements_of(array).size();
            std::vector<bool> before;
            for (size_t i = 0; i < total; ++i) before.push_back(array.is_array_hole(i));
            for (size_t i = 0; i < total; ++i) {
                if (before[total - 1 - i]) array.set_array_hole(i);
                else array.clear_array_hole(i);
            }
            (void)bits;
        }
        return array;
    });
    js_define_method(ap, "toReversed", 0, [](Args) {
        JsValue source = this_object("toReversed");
        if (dense_readable(source) && !source.object_val->array_holes) {
            std::vector<JsValue> copy(elements_of(source).begin(), elements_of(source).end());
            std::reverse(copy.begin(), copy.end());
            return JsValue::make_array(copy);
        }
        // Every place of the answer is written: `CreateDataPropertyOrThrow`
        // for each one, with the value Get hands back, so a place the source
        // does not have becomes an own `undefined` (and takes a value from the
        // prototype if one is there).
        const size_t length = length_of(source);
        if (length > 4294967295u) js_throw_range_error("Invalid array length");
        std::vector<JsValue> copy(length, JsValue::make_undefined());
        for (size_t k = 0; k < length; ++k) copy[k] = get_place(source, length - 1 - k);
        return JsValue::make_array(copy);
    });
    js_define_method(ap, "sort", 1, [](Args args) {
        JsValue array = this_object("sort");
        const JsValue& comparator = js_arg(args, 0);
        if (!comparator.is_undefined() && !comparator.is_function()) {
            js_throw_type_error("The comparison function must be either a function or undefined");
        }
        if (!dense_readable(array)) {
            // SortIndexedProperties with skip-holes: the places the object has
            // are read, sorted, written back from 0, and the places after them
            // are deleted, so a hole stays at the end.
            const size_t length = length_of(array);
            std::vector<JsValue> items;
            for (size_t k = 0; k < length; ++k) {
                if (has_place(array, k)) items.push_back(get_place(array, k));
            }
            std::vector<bool> holes(items.size(), false);
            sort_values(items, holes, comparator);
            for (size_t i = 0; i < items.size(); ++i) set_place_or_throw(array, i, items[i]);
            for (size_t i = items.size(); i < length; ++i) delete_place_or_throw(array, i);
            return array;
        }
        // Sort a copy: the comparator is script and may touch the array.
        std::vector<JsValue> copy(elements_of(array).begin(), elements_of(array).end());
        std::vector<bool> holes;
        for (size_t i = 0; i < copy.size(); ++i) holes.push_back(array.is_array_hole(i));
        sort_values(copy, holes, comparator);
        elements_of(array).assign(copy.begin(), copy.end());
        for (size_t i = 0; i < holes.size(); ++i) {
            if (holes[i]) array.set_array_hole(i);
            else array.clear_array_hole(i);
        }
        return array;
    });
    js_define_method(ap, "toSorted", 1, [](Args args) {
        JsValue source = this_object("toSorted");
        const JsValue& comparator = js_arg(args, 0);
        if (!comparator.is_undefined() && !comparator.is_function()) {
            js_throw_type_error("The comparison function must be either a function or undefined");
        }
        if (dense_readable(source) && !source.object_val->array_holes) {
            std::vector<JsValue> copy(elements_of(source).begin(), elements_of(source).end());
            std::vector<bool> holes(copy.size(), false);
            sort_values(copy, holes, comparator);
            return JsValue::make_array(copy);
        }
        // SortIndexedProperties with skip-holes reads only the places the
        // source has, and then every place of the answer is written, so the
        // places past the values read come out as own `undefined`s.
        const size_t length = length_of(source);
        if (length > 4294967295u) js_throw_range_error("Invalid array length");
        std::vector<JsValue> items;
        for (size_t k = 0; k < length; ++k) {
            if (has_place(source, k)) items.push_back(get_place(source, k));
        }
        std::vector<bool> holes(items.size(), false);
        sort_values(items, holes, comparator);
        items.resize(length, JsValue::make_undefined());
        return JsValue::make_array(items);
    });
    js_define_method(ap, "with", 2, [](Args args) {
        // The length, then the index, then the places: the standard's order,
        // and the index that is replaced is not read, so a getter on it is
        // not called. Turning the index into a number can run a script that
        // changes the array, and the answer still has the length taken first.
        JsValue source = this_object("with");
        const size_t length = length_of(source);
        if (length > 4294967295u) js_throw_range_error("Invalid array length");
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0) index += static_cast<double>(length);
        if (index < 0 || index >= static_cast<double>(length)) js_throw_range_error("Invalid index");
        const size_t at = static_cast<size_t>(index);
        if (dense_readable(source) && !source.object_val->array_holes) {
            // The length was taken before the index was asked for, and asking
            // for it can run a script that grows or shortens the array: the
            // answer is the length taken then, not the one there is now.
            std::vector<JsValue> copy(elements_of(source).begin(), elements_of(source).end());
            copy.resize(length, JsValue::make_undefined());
            copy[at] = js_arg(args, 1);
            return JsValue::make_array(copy);
        }
        std::vector<JsValue> copy(length, JsValue::make_undefined());
        for (size_t k = 0; k < length; ++k) {
            copy[k] = (k == at) ? js_arg(args, 1) : get_place(source, k);
        }
        return JsValue::make_array(copy);
    });
    js_define_method(ap, "fill", 1, [](Args args) {
        JsValue array = this_object("fill");
        // The length first, then the two places, as the standard has them:
        // asking a place for its number can run a script that changes the
        // array, and the places that are filled are the ones that length names.
        const size_t length = length_of(array);
        const size_t begin = relative_index(js_arg(args, 1), length, 0);
        const size_t end = relative_index(js_arg(args, 2), length, length);
        if (dense_path(array) && elements_of(array).size() == length) {
            auto& elements = elements_of(array);
            for (size_t i = begin; i < end; ++i) {
                elements[i] = js_arg(args, 0);
                array.clear_array_hole(i);       // fill writes every place it covers
            }
            return array;
        }
        for (size_t i = begin; i < end; ++i) set_place_or_throw(array, i, js_arg(args, 0));
        return array;
    });
    js_define_method(ap, "copyWithin", 2, [](Args args) {
        JsValue array = this_object("copyWithin");
        // The length is taken once, then the three places in the standard's
        // order. Asking one of them for its number can run a script that
        // changes the array, so both forms below work from that length.
        const size_t length = length_of(array);
        size_t to = relative_index(js_arg(args, 0), length, 0);
        size_t from = relative_index(js_arg(args, 1), length, 0);
        const size_t final = args.size() > 2 ? relative_index(js_arg(args, 2), length, length) : length;
        size_t count = std::min(final > from ? final - from : 0, length > to ? length - to : 0);
        int direction = 1;
        if (from < to && to < from + count) {
            // Overlapping, and the copy runs backwards so it does not read a
            // place it has already written.
            direction = -1;
            from += count - 1;
            to += count - 1;
        }
        if (dense_readable(array) && elements_of(array).size() == length) {
            auto& elements = elements_of(array);
            while (count > 0) {
                elements[to] = elements[from];
                // Copying a hole leaves a hole at the target (the standard
                // calls DeletePropertyOrThrow for it).
                if (array.is_array_hole(from)) array.set_array_hole(to);
                else array.clear_array_hole(to);
                if (direction > 0) { ++from; ++to; } else { --from; --to; }
                --count;
            }
            return array;
        }
        while (count > 0) {
            if (has_place(array, from)) set_place_or_throw(array, to, get_place(array, from));
            else delete_place_or_throw(array, to);
            if (direction > 0) { ++from; ++to; } else { --from; --to; }
            --count;
        }
        return array;
    });
    js_define_method(ap, "sort", 1, [](Args args) {
        JsValue array = this_object("sort");
        const JsValue& comparator = js_arg(args, 0);
        if (!comparator.is_undefined() && !comparator.is_function()) {
            js_throw_type_error("The comparison function must be either a function or undefined");
        }
        if (!dense_readable(array)) {
            // SortIndexedProperties with skip-holes: the places the object has
            // are read, sorted, written back from 0, and the places after them
            // are deleted, so a hole stays at the end.
            const size_t length = length_of(array);
            std::vector<JsValue> items;
            for (size_t k = 0; k < length; ++k) {
                if (has_place(array, k)) items.push_back(get_place(array, k));
            }
            std::vector<bool> holes(items.size(), false);
            sort_values(items, holes, comparator);
            for (size_t i = 0; i < items.size(); ++i) set_place_or_throw(array, i, items[i]);
            for (size_t i = items.size(); i < length; ++i) delete_place_or_throw(array, i);
            return array;
        }
        // Sort a copy: the comparator is script and may touch the array.
        std::vector<JsValue> copy(elements_of(array).begin(), elements_of(array).end());
        std::vector<bool> holes;
        for (size_t i = 0; i < copy.size(); ++i) holes.push_back(array.is_array_hole(i));
        sort_values(copy, holes, comparator);
        elements_of(array).assign(copy.begin(), copy.end());
        for (size_t i = 0; i < holes.size(); ++i) {
            if (holes[i]) array.set_array_hole(i);
            else array.clear_array_hole(i);
        }
        return array;
    });
    js_define_method(ap, "toSorted", 1, [](Args args) {
        JsValue source = this_object("toSorted");
        const JsValue& comparator = js_arg(args, 0);
        if (!comparator.is_undefined() && !comparator.is_function()) {
            js_throw_type_error("The comparison function must be either a function or undefined");
        }
        if (dense_readable(source) && !source.object_val->array_holes) {
            std::vector<JsValue> copy(elements_of(source).begin(), elements_of(source).end());
            std::vector<bool> holes(copy.size(), false);
            sort_values(copy, holes, comparator);
            return JsValue::make_array(copy);
        }
        // SortIndexedProperties with skip-holes reads only the places the
        // source has, and then every place of the answer is written, so the
        // places past the values read come out as own `undefined`s.
        const size_t length = length_of(source);
        if (length > 4294967295u) js_throw_range_error("Invalid array length");
        std::vector<JsValue> items;
        for (size_t k = 0; k < length; ++k) {
            if (has_place(source, k)) items.push_back(get_place(source, k));
        }
        std::vector<bool> holes(items.size(), false);
        sort_values(items, holes, comparator);
        items.resize(length, JsValue::make_undefined());
        return JsValue::make_array(items);
    });
    js_define_method(ap, "with", 2, [](Args args) {
        // The length, then the index, then the places: the standard's order,
        // and the index that is replaced is not read, so a getter on it is
        // not called. Turning the index into a number can run a script that
        // changes the array, and the answer still has the length taken first.
        JsValue source = this_object("with");
        const size_t length = length_of(source);
        if (length > 4294967295u) js_throw_range_error("Invalid array length");
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0) index += static_cast<double>(length);
        if (index < 0 || index >= static_cast<double>(length)) js_throw_range_error("Invalid index");
        const size_t at = static_cast<size_t>(index);
        if (dense_readable(source) && !source.object_val->array_holes) {
            // The length was taken before the index was asked for, and asking
            // for it can run a script that grows or shortens the array: the
            // answer is the length taken then, not the one there is now.
            std::vector<JsValue> copy(elements_of(source).begin(), elements_of(source).end());
            copy.resize(length, JsValue::make_undefined());
            copy[at] = js_arg(args, 1);
            return JsValue::make_array(copy);
        }
        std::vector<JsValue> copy(length, JsValue::make_undefined());
        for (size_t k = 0; k < length; ++k) {
            copy[k] = (k == at) ? js_arg(args, 1) : get_place(source, k);
        }
        return JsValue::make_array(copy);
    });
    js_define_method(ap, "fill", 1, [](Args args) {
        JsValue array = this_object("fill");
        // The length first, then the two places, as the standard has them:
        // asking a place for its number can run a script that changes the
        // array, and the places that are filled are the ones that length names.
        const size_t length = length_of(array);
        const size_t begin = relative_index(js_arg(args, 1), length, 0);
        const size_t end = relative_index(js_arg(args, 2), length, length);
        if (dense_path(array) && elements_of(array).size() == length) {
            auto& elements = elements_of(array);
            for (size_t i = begin; i < end; ++i) {
                elements[i] = js_arg(args, 0);
                array.clear_array_hole(i);       // fill writes every place it covers
            }
            return array;
        }
        for (size_t i = begin; i < end; ++i) set_place_or_throw(array, i, js_arg(args, 0));
        return array;
    });
    js_define_method(ap, "copyWithin", 2, [](Args args) {
        JsValue array = this_object("copyWithin");
        if (!dense_readable(array)) {
            // The standard's form, which reads every place it copies through
            // Get and writes it through Set: the direction matters when the
            // ranges overlap, and a place the object does not have is deleted
            // at the target rather than written as `undefined`.
            const size_t length = length_of(array);
            size_t to = relative_index(js_arg(args, 0), length, 0);
            size_t from = relative_index(js_arg(args, 1), length, 0);
            const size_t final = args.size() > 2 ? relative_index(js_arg(args, 2), length, length)
                                                 : length;
            size_t count = std::min(final > from ? final - from : 0, length > to ? length - to : 0);
            int direction = 1;
            if (from < to && to < from + count) {
                direction = -1;
                from += count - 1;
                to += count - 1;
            }
            while (count > 0) {
                if (has_place(array, from)) set_place_or_throw(array, to, get_place(array, from));
                else delete_place_or_throw(array, to);
                if (direction > 0) { ++from; ++to; } else { --from; --to; }
                --count;
            }
            return array;
        }
        auto& elements = elements_of(array);
        size_t target = relative_index(js_arg(args, 0), elements.size(), 0);
        size_t begin = relative_index(js_arg(args, 1), elements.size(), 0);
        size_t end = relative_index(js_arg(args, 2), elements.size(), elements.size());
        if (begin >= end || target >= elements.size()) return array;
        std::vector<JsValue> chunk(elements.begin() + static_cast<std::ptrdiff_t>(begin),
                                   elements.begin() + static_cast<std::ptrdiff_t>(end));
        std::vector<bool> chunk_holes;
        for (size_t i = begin; i < end; ++i) chunk_holes.push_back(array.is_array_hole(i));
        for (size_t i = 0; i < chunk.size() && target + i < elements.size(); ++i) {
            elements[target + i] = chunk[i];
            // Copying a hole leaves a hole at the target (the standard calls
            // DeletePropertyOrThrow for it).
            if (chunk_holes[i]) array.set_array_hole(target + i);
            else array.clear_array_hole(target + i);
        }
        return array;
    });
    js_define_method(ap, "flat", 0, [](Args args) {
        double depth = js_arg(args, 0).is_undefined() ? 1 : js_to_integer(js_to_number(js_arg(args, 0)));
        JsValue array = receiver("flat");
        std::vector<JsValue> result;
        std::vector<bool> holes;
        flatten_into(result, holes, array, depth);
        // ArraySpeciesCreate(O, 0), one place per flattened element (§23.1.3.13).
        return fill_species_result(array_species_create(array, 0, "flat"), result, holes);
    });
    js_define_method(ap, "at", 1, [](Args args) {
        JsValue array = receiver("at");
        const size_t length = array_like_length(array, "at");
        // One index, counted from either end: the length is all that is needed,
        // so `{ length: 2**53 }` answers without a walk.
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0) index += static_cast<double>(length);
        if (index < 0 || index >= static_cast<double>(length)) return JsValue::make_undefined();
        return get_index(array, static_cast<size_t>(index));
    });
    // ---- Searching --------------------------------------------------------
    js_define_method(ap, "indexOf", 1, [](Args args) {
        JsValue array = receiver("indexOf");
        const size_t length = array_like_length(array, "indexOf");
        // indexOf asks HasProperty before it reads, so a hole is skipped. It
        // starts from `fromIndex`, taken before the first question is asked.
        const size_t start = relative_index(js_arg(args, 1), length, 0);
        if (start >= length) return JsValue::make_number(-1);
        size_t found = length;
        walk_present_indices(array, length, prototype_holds_an_index(array), [&](size_t i) {
            if (i < start) return true;
            if (!strict_equals(get_index(array, i), js_arg(args, 0))) return true;
            found = i;
            return false;
        });
        return JsValue::make_number(found == length ? -1 : static_cast<double>(found));
    });
    js_define_method(ap, "lastIndexOf", 1, [](Args args) {
        JsValue array = receiver("lastIndexOf");
        const size_t length = array_like_length(array, "lastIndexOf");
        if (length == 0) return JsValue::make_number(-1);
        const double len = static_cast<double>(length);
        double from = args.size() > 1 ? js_to_integer(js_to_number(args[1])) : len - 1;
        if (from < 0) from += len;
        if (from < 0) return JsValue::make_number(-1);
        const size_t start = static_cast<size_t>(std::min(from, len - 1));
        // Backwards without counting backwards: the places that are there are
        // a finite list, and the answer is its last member at or below `start`.
        const IndexWalk walk = present_indices(array, length);
        // Descending, asking about each place as it is reached: a getter at a
        // higher index can define a lower one that was a hole when the walk
        // started, and `lastIndexOf` must find it.
        size_t k = walk.indices.size();
        for (size_t i = start + 1; i-- > 0;) {
            while (k > 0 && walk.indices[k - 1] > i) --k;
            const bool in_walk = k > 0 && walk.indices[k - 1] == i;
            if (!in_walk && !has_index_property(array, i)) continue;
            if (!has_index_property(array, i)) continue;
            if (strict_equals(get_index(array, i), js_arg(args, 0))) return JsValue::make_number(static_cast<double>(i));
        }
        return JsValue::make_number(-1);
    });
    js_define_method(ap, "includes", 1, [](Args args) {
        // Unlike indexOf, includes does not ask HasProperty: it reads the place
        // as `a[i]` does, so a hole is `undefined` to it. A place that is not
        // there and that nothing on the chain answers for is still `undefined`,
        // which one comparison covers for the whole run.
        JsValue array = receiver("includes");
        const size_t length = array_like_length(array, "includes");
        const size_t start = relative_index(js_arg(args, 1), length, 0);
        if (start >= length) return JsValue::make_boolean(false);
        const bool sparse = array.is_array() && !array.object_val->has_accessors &&
                            !prototype_holds_an_index(array);
        for (size_t i = start; i < length; ++i) {
            if (sparse) {
                // A real array with no accessor and no prototype index: a hole
                // reads `undefined`, and past the elements so does every index.
                const auto& elements = array.object_val->array_elements;
                const JsValue item = i < elements.size() && !array.is_array_hole(i)
                                         ? elements[i] : JsValue::make_undefined();
                if (js_same_value_zero(item, js_arg(args, 0))) return JsValue::make_boolean(true);
                if (i >= elements.size()) break;   // nothing more to read
                continue;
            }
            // Read the place as `a[i]` does, one at a time: an index can be a
            // getter that throws, and `includes` must let it.
            if (js_same_value_zero(get_index(array, i), js_arg(args, 0))) return JsValue::make_boolean(true);
        }
        return JsValue::make_boolean(false);
    });
    // ---- Iteration with a callback ----------------------------------------
    //
    // The element vector can be replaced by the callback, so every step
    // re-reads it and bounds-checks against the current length.
    enum class Find { Value, Index };
    auto make_find = [](const char* name, bool from_end, Find what) {
        return [name, from_end, what](Args args) {
            JsValue array = receiver(name);
            const size_t length = array_like_length(array, name);
            JsValue callback = require_callback(js_arg(args, 0), name);
            const JsValue this_arg = js_arg(args, 1);
            auto test = [&](size_t i, JsValue& found) {
                JsValue item = get_index(array, i);
                if (!callback.call_with_this(this_arg, {item, JsValue::make_number(static_cast<double>(i)), array}).is_truthy()) return false;
                found = what == Find::Value ? item : JsValue::make_number(static_cast<double>(i));
                return true;
            };
            JsValue found;
            // `find` and its relatives are the one family that does not skip a
            // hole: the standard reads `kValue = O[k]` for every index and
            // calls the predicate with it. A length past the bound is hours of
            // callbacks with the same `undefined`, so the places that are there
            // are visited in the standard's order and the run of missing places
            // past them is not walked. That is a deviation, stated where it is
            // made, and it changes only an answer that could not be produced.
            if (length <= kMaxWalkedIndices) {
                for (size_t step = 0; step < length; ++step) {
                    const size_t i = from_end ? length - 1 - step : step;
                    if (test(i, found)) return found;
                }
                return what == Find::Value ? JsValue::make_undefined() : JsValue::make_number(-1);
            }
            const IndexWalk walk = present_indices(array, length);
            if (from_end) {
                for (size_t k = walk.indices.size(); k-- > 0;) if (test(walk.indices[k], found)) return found;
            } else {
                for (size_t i : walk.indices) if (test(i, found)) return found;
            }
            return what == Find::Value ? JsValue::make_undefined() : JsValue::make_number(-1);
        };
    };
    js_define_method(ap, "find", 1, make_find("find", false, Find::Value));    js_define_method(ap, "find", 1, make_find("find", false, Find::Value));
    js_define_method(ap, "findIndex", 1, make_find("findIndex", false, Find::Index));
    js_define_method(ap, "findLast", 1, make_find("findLast", true, Find::Value));
    js_define_method(ap, "findLastIndex", 1, make_find("findLastIndex", true, Find::Index));

    // Generic, like the spec's forEach: the receiver itself is the third
    // argument of the callback, and `length` and the indices are read with
    // plain property access, so it works on array-likes (strings, NodeLists,
    // arguments) and reports the receiver rather than a snapshot.
    js_define_method(ap, "forEach", 1, [](Args args) {
        JsValue me = self();
        const size_t length = array_like_length(me, "forEach");  // LengthOfArrayLike precedes IsCallable.
        JsValue callback = require_callback(js_arg(args, 0), "forEach");
        const JsValue this_arg = js_arg(args, 1);
        walk_present_indices(me, length, prototype_holds_an_index(me), [&](size_t i) {
            callback.call_with_this(this_arg, {get_index(me, i), JsValue::make_number(static_cast<double>(i)), me});
            return true;
        });
        return JsValue::make_undefined();
    });
    js_define_method(ap, "map", 1, [](Args args) {
        JsValue array = receiver("map");
        const size_t length = array_like_length(array, "map");
        JsValue callback = require_callback(js_arg(args, 0), "map");
        const JsValue this_arg = js_arg(args, 1);
        // The result is a real array, so it is at most 2^32-1 long: a longer
        // source is the RangeError of ArrayCreate, thrown before the first
        // callback rather than after four billion of them.
        array_result_length(length, "map");
        // The result is made by the species constructor, not by this file
        // (§23.1.3.2 step 5): `new List().map(f)` is a `List`, and a place the
        // source did not have stays a hole in it.
        JsValue mapped = array_species_create(array, length, "map");
        walk_present_indices(array, length, prototype_holds_an_index(array), [&](size_t i) {
            create_data_property_or_throw(mapped, i, callback.call_with_this(this_arg, {get_index(array, i), JsValue::make_number(static_cast<double>(i)), array}));
            return true;
        });
        set_length_or_throw(mapped, length);
        return mapped;
    });
    js_define_method(ap, "filter", 1, [](Args args) {
        JsValue array = receiver("filter");
        const size_t length = array_like_length(array, "filter");
        JsValue callback = require_callback(js_arg(args, 0), "filter");
        const JsValue this_arg = js_arg(args, 1);
        // ArraySpeciesCreate(O, 0) and one place per kept element (§23.1.3.3).
        JsValue kept = array_species_create(array, 0, "filter");
        size_t count = 0;
        walk_present_indices(array, length, prototype_holds_an_index(array), [&](size_t i) {
            JsValue item = get_index(array, i);
            if (callback.call_with_this(this_arg, {item, JsValue::make_number(static_cast<double>(i)), array}).is_truthy()) {
                create_data_property_or_throw(kept, count, item);
                ++count;
            }
            return true;
        });
        set_length_or_throw(kept, count);
        return kept;
    });
    js_define_method(ap, "flatMap", 1, [](Args args) {
        JsValue array = receiver("flatMap");
        const size_t length = array_like_length(array, "flatMap");
        JsValue callback = require_callback(js_arg(args, 0), "flatMap");
        const JsValue this_arg = js_arg(args, 1);
        std::vector<JsValue> result;
        std::vector<bool> holes;
        walk_present_indices(array, length, prototype_holds_an_index(array), [&](size_t i) {
            JsValue mapped = callback.call_with_this(this_arg, {get_index(array, i), JsValue::make_number(static_cast<double>(i)), array});
            if (mapped.is_array()) {
                const auto& inner = elements_of(mapped);
                result.insert(result.end(), inner.begin(), inner.end());
                for (size_t k = 0; k < inner.size(); ++k) holes.push_back(mapped.is_array_hole(k));
            } else {
                result.push_back(std::move(mapped));
                holes.push_back(false);
            }
            return true;
        });
        // flatMap is a map that flattens one level, and its result is made the
        // same way: by the species constructor (§23.1.3.11 step 7).
        return fill_species_result(array_species_create(array, 0, "flatMap"), result, holes);
    });
    js_define_method(ap, "some", 1, [](Args args) {
        JsValue array = receiver("some");
        const size_t length = array_like_length(array, "some");  // LengthOfArrayLike precedes IsCallable.
        JsValue callback = require_callback(js_arg(args, 0), "some");
        const JsValue this_arg = js_arg(args, 1);
        bool answered = false;
        walk_present_indices(array, length, prototype_holds_an_index(array), [&](size_t i) {
            if (callback.call_with_this(this_arg, {get_index(array, i), JsValue::make_number(static_cast<double>(i)), array}).is_truthy()) {
                answered = true;
                return false;
            }
            return true;
        });
        return JsValue::make_boolean(answered);
    });
    js_define_method(ap, "every", 1, [](Args args) {
        JsValue array = receiver("every");
        const size_t length = array_like_length(array, "every");
        JsValue callback = require_callback(js_arg(args, 0), "every");
        const JsValue this_arg = js_arg(args, 1);
        bool answered = false;
        walk_present_indices(array, length, prototype_holds_an_index(array), [&](size_t i) {
            if (!callback.call_with_this(this_arg, {get_index(array, i), JsValue::make_number(static_cast<double>(i)), array}).is_truthy()) {
                answered = true;
                return false;
            }
            return true;
        });
        return JsValue::make_boolean(!answered);
    });
    auto make_reduce = [](const char* name, bool from_end) {
        return [name, from_end](Args args) {
            JsValue array = receiver(name);
            const size_t length = array_like_length(array, name);
            JsValue callback = require_callback(js_arg(args, 0), name);
            // reduce visits the places that are there, in order, and never
            // asks about one that is not: a receiver with a huge `length` and
            // one place reduces in one step.
            //
            // The places are asked for as the walk reaches them, not listed
            // before it starts. A callback can make a place appear ahead of
            // itself — `reduce/15.4.4.21-9-1.js` writes `arr[2]` from the
            // callback while two is still a hole in front of the walk — and
            // the standard asks `HasProperty` at each index as it arrives, so
            // the place that appeared is visited. Listing them first answered
            // "75" where Node.js v22.22.3 answers "105".
            JsValue accumulator;
            bool started = args.size() >= 2;
            if (started) accumulator = args[1];
            // One place, in the order the walk reaches it: the first one starts
            // the accumulator when there is no initial value, and every other
            // one is handed to the callback.
            auto visit = [&](size_t i) {
                if (!started) {
                    accumulator = get_index(array, i);
                    started = true;
                    return;
                }
                JsValue item = get_index(array, i);
                accumulator = callback.call({accumulator, item, JsValue::make_number(static_cast<double>(i)), array});
            };
            if (from_end) {
                // §23.1.3.22 walks k downwards from len-1 to 0 and asks
                // `HasProperty` at each index as it arrives — so a place a
                // callback creates *below* itself is still reached. Walking
                // the places upwards and then reversing the list answers
                // differently: `[1,,,9]` with a callback that fills 1 and 2
                // from index 3 reaches neither of them, and answered 18
                // where Node.js v22.22.3 answers 21.
                //
                // A length that cannot be counted cannot be walked downwards
                // either, so it takes the places that are there, in reverse.
                // That is the same deviation `find` makes at the same bound,
                // and it changes only an answer that could not be produced.
                if (length > kMaxWalkedIndices) {
                    const IndexWalk walk = present_indices(array, length);
                    for (size_t k = walk.indices.size(); k-- > 0;) {
                        if (!has_index_property(array, walk.indices[k])) continue;
                        visit(walk.indices[k]);
                    }
                } else {
                    for (size_t step = 0; step < length; ++step) {
                        const size_t i = length - 1 - step;
                        if (!has_index_property(array, i)) continue;
                        visit(i);
                    }
                }
                if (!started) js_throw_type_error("Reduce of empty array with no initial value");
                return accumulator;
            }
            walk_present_indices(array, length, prototype_holds_an_index(array), [&](size_t i) {
                visit(i);
                return true;
            });
            if (!started) js_throw_type_error("Reduce of empty array with no initial value");
            return accumulator;
        };
    };
    js_define_method(ap, "reduce", 1, make_reduce("reduce", false));
    js_define_method(ap, "reduceRight", 1, make_reduce("reduceRight", true));

    // ---- Iterators --------------------------------------------------------
    enum class Kind { Keys, Values, Entries };
    // %Array.prototype.keys/values/entries% are generic: they read `length`
    // and the indices off the receiver each step, so they work on any
    // array-like, `this` stays the object the iterator was made from, and a
    // shrinking array-like shortens the iteration — as the spec has it.
    auto make_array_iterator = [](Kind kind) {
        return [kind](Args) {
            JsValue me = self();
            if (me.is_nullish()) {
                js_throw_type_error(std::string("Array.prototype.") + (kind == Kind::Keys ? "keys" : kind == Kind::Values ? "values" : "entries") + " called on null or undefined");
            }
            auto index = std::make_shared<size_t>(0);
            return js_make_iterator([me, index, kind](JsValue& value) {
                if (*index >= iteration_length(me)) return false;
                size_t i = (*index)++;
                switch (kind) {
                    case Kind::Keys: value = JsValue::make_number(static_cast<double>(i)); break;
                    case Kind::Values: value = me.get_property(js_number_to_string(static_cast<double>(i))); break;
                    case Kind::Entries:
                        value = JsValue::make_array({JsValue::make_number(static_cast<double>(i)), me.get_property(js_number_to_string(static_cast<double>(i)))});
                        break;
                }
                return true;
            }, {me}, JsIntrinsics::get().array_iterator_proto);
        };
    };
    js_define_method(ap, "keys", 0, make_array_iterator(Kind::Keys));
    js_define_method(ap, "entries", 0, make_array_iterator(Kind::Entries));
    // %Array.prototype.values% is one and the same function object under all
    // three keys (`Array.prototype[Symbol.iterator] === Array.prototype.values`),
    // which is what platform object prototypes alias when Web IDL says so.
    JsValue values_iterator = js_make_native("values", 0, make_array_iterator(Kind::Values));
    js_define_hidden(ap, "values", values_iterator);
    js_define_hidden(ap, kIteratorKey, values_iterator);
    js_define_hidden(ap, "Symbol.iterator", values_iterator);

    // Array.prototype[@@unscopables] (§23.1.3.34): the names a `with (array)`
    // block must not resolve to the array, so a page written before these
    // methods existed keeps seeing its own `values`, `keys` or `find` in the
    // block. A null-prototype object, `true` under each name, and — unlike the
    // methods it hides — neither writable nor enumerable on the prototype, but
    // configurable. The list is the standard's, in its order: the twelve the
    // 2015 edition named plus `findLast`, `findLastIndex` (2022) and
    // `toReversed`, `toSorted`, `toSpliced` (2023).
    {
        JsValue unscopables = JsValue::make_object();
        unscopables.object_val->prototype = nullptr;
        for (const char* name : {"at", "copyWithin", "entries", "fill", "find", "findIndex",
                                 "findLast", "findLastIndex", "flat", "flatMap", "includes",
                                 "keys", "toReversed", "toSorted", "toSpliced", "values"}) {
            unscopables.define_property(name, JsValue::make_boolean(true),
                                       PropertyMap::Enumerable | PropertyMap::Writable |
                                           PropertyMap::Configurable);
        }
        js_object_value(ap).define_property("@@unscopables", unscopables,
                                           static_cast<uint8_t>(PropertyMap::Configurable));
    }

    // Every method here is a built-in function, and a built-in function has no
    // [[Construct]]: `new Array.prototype.reverse()` is a TypeError, which
    // test262's not-a-constructor.js in each method's directory asks for. The
    // keys are copied first because asking a method for its cell can move its
    // property table.
    std::vector<JsValue> methods;
    for (const auto& entry : ap->properties) {
        if (entry.first != "constructor" && entry.second.is_function()) {
            methods.push_back(entry.second);
        }
    }
    for (const auto& method : methods) {
        if (JsObject* cell = method.function_cell()) cell->not_constructor = true;
    }
}

} // namespace litebird
