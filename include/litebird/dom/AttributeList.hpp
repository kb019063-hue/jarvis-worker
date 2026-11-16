#pragma once

// The attributes of an element, in the order they were added.
//
// They used to be an unordered_map, so `el.outerHTML`, `el.attributes[0]` and
// `getAttributeNames()` gave the attributes back in whatever order the hash
// put them, which differs between standard libraries and from every browser.
// The DOM says an element has an attribute *list*: a new attribute goes at the
// end, changing a value keeps its place, and that order is what a script and
// a serializer see.
//
// An attribute is identified by its *expanded name*: a namespace and a local
// name. Two attributes may have the same qualified name (`ab:attr` and
// `kl:attr` are both "attr" when the prefix is not part of it) as long as
// their namespaces differ, and the standard says they are two attributes.
// Only a script can ask for that — `setAttributeNS("ab", "attr", …)` next to
// `setAttributeNS("kl", "attr", …)` — so a name that appears once is the usual
// case and a name that appears twice is a rare one, and the lookup by
// qualified name answers with the first of them.
//
// An element has a handful of attributes, so lookup is a scan of a vector.
// One with very many (a hostile page can give one element 50,000) gets an
// index, built the first time it is searched: without it every setAttribute
// and every attribute the parser adds scans the whole list, and the element
// costs the square of its size.
//
// The index does not hold positions: taking an attribute out of the middle
// moves the tail, and an index of positions would have to be
// written again each time (removing half of 50,000 attributes took 88
// seconds that way). Each attribute gets a number when it is added, the
// numbers only grow, so the list is always in the order of its numbers; the
// index maps an expanded name to its number and the position is found by
// bisection. The items and the index numbers live in vectors with a logical
// start offset. Removing the last one pops the back; removing the first
// advances the start and leaves a dead prefix that is compacted only once it
// is at least as large as the live tail. Draining 50,000 attributes from
// either end is therefore linear, and an empty list still allocates nothing.
// A middle removal still moves the tail of the vector.
//
// The qualified-name index is kept as a map to the number of the first
// attribute with that name, plus a set of the names that appear more than
// once: looking one of those up is a scan, which costs nothing anywhere it
// is reachable (a handful of attributes, made by a script).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace litebird {

/// One interned copy of a namespace name for the whole program: an SVG file
/// with 100,000 xlink:href attributes holds the name once, and an attribute
/// record holds a pointer to it instead of 36 bytes. The set is node-based,
/// so a pointer stays valid as it grows, and the names are never changed.
const std::string* intern_namespace_name(const std::string& uri);

/// One attribute: its qualified name, its value, and the namespace it stands
/// in with the place its local name starts. This is the DOM's attribute; the
/// `Attr` objects a script sees stand for one of these.
struct Attribute {
    Attribute() = default;
    Attribute(std::string name_in, std::string value_in)
        : name(std::move(name_in)), value(std::move(value_in)) {}
    Attribute(std::string name_in, const std::string* ns_in, std::uint32_t local_at_in, std::string value_in)
        : name(std::move(name_in)), value(std::move(value_in)), ns(ns_in), local_at(local_at_in) {}

    /// The qualified name: "href", "xlink:href".
    std::string name;
    std::string value;
    /// The namespace URI, interned; null for an attribute in no namespace.
    const std::string* ns{nullptr};
    /// Where the local name starts in `name`: after the colon when the name
    /// has a prefix, otherwise 0. (Without a namespace a colon is one more
    /// character of the name: `setAttribute("a:b", …)` has that local name.)
    std::uint32_t local_at{0};

    bool has_namespace() const { return ns != nullptr; }

    /// The namespace URI, empty for an attribute in no namespace.
    const std::string& namespace_uri() const {
        static const std::string none;
        return ns ? *ns : none;
    }

    std::string_view local_name() const { return std::string_view(name).substr(local_at); }

    /// The same expanded name: this namespace and this local name.
    bool is_expanded(const std::string* other_ns, std::string_view other_local) const {
        return ns == other_ns && local_name() == other_local;
    }
};

class AttributeList {
public:
    using value_type = Attribute;
    using iterator = std::vector<value_type>::iterator;
    using const_iterator = std::vector<value_type>::const_iterator;

    AttributeList() = default;
    AttributeList(const AttributeList& other) : m_storage(other.begin(), other.end()) {}
    AttributeList(AttributeList&& other) noexcept
        : m_storage(std::move(other.m_storage)), m_start(std::exchange(other.m_start, 0)),
          m_index(std::move(other.m_index)) {}
    AttributeList& operator=(const AttributeList& other) {
        if (this != &other) {
            m_storage.assign(other.begin(), other.end());
            m_start = 0;
            m_index.reset();
        }
        return *this;
    }
    AttributeList& operator=(AttributeList&& other) noexcept {
        if (this != &other) {
            m_storage = std::move(other.m_storage);
            m_start = std::exchange(other.m_start, 0);
            m_index = std::move(other.m_index);
        }
        return *this;
    }

    iterator begin() { return m_storage.begin() + static_cast<std::ptrdiff_t>(m_start); }
    iterator end() { return m_storage.end(); }
    const_iterator begin() const { return m_storage.cbegin() + static_cast<std::ptrdiff_t>(m_start); }
    const_iterator end() const { return m_storage.cend(); }

    std::size_t size() const { return m_storage.size() - m_start; }
    bool empty() const { return size() == 0; }
    const value_type& operator[](std::size_t index) const { return m_storage[m_start + index]; }
    value_type& operator[](std::size_t index) { return m_storage[m_start + index]; }

    /// The first attribute with this qualified name, whatever its namespace.
    iterator find(const std::string& name) {
        const std::size_t at = position(name);
        return at == kNone ? end() : begin() + static_cast<std::ptrdiff_t>(at);
    }
    const_iterator find(const std::string& name) const {
        const std::size_t at = position(name);
        return at == kNone ? end() : begin() + static_cast<std::ptrdiff_t>(at);
    }
    std::size_t count(const std::string& name) const { return position(name) == kNone ? 0 : 1; }

    /// The attribute with this namespace (null for none) and local name.
    iterator find_expanded(const std::string* ns, std::string_view local) {
        const std::size_t at = position_expanded(ns, local);
        return at == kNone ? end() : begin() + static_cast<std::ptrdiff_t>(at);
    }
    const_iterator find_expanded(const std::string* ns, std::string_view local) const {
        const std::size_t at = position_expanded(ns, local);
        return at == kNone ? end() : begin() + static_cast<std::ptrdiff_t>(at);
    }

    /// The value of `name`, added at the end with an empty value when absent.
    /// The attribute added stands in no namespace.
    std::string& operator[](const std::string& name) {
        const std::size_t at = position(name);
        if (at != kNone) return m_storage[m_start + at].value;
        m_storage.emplace_back(name, std::string());
        index_added(m_storage.size() - 1);
        return m_storage.back().value;
    }

    /// Adds an attribute at the end. The expanded name must be a new one:
    /// changing the value of an attribute that is there is the caller's job
    /// (see Element).
    iterator push_back(std::string name, const std::string* ns, std::uint32_t local_at, std::string value) {
        m_storage.emplace_back(std::move(name), ns, local_at, std::move(value));
        index_added(m_storage.size() - 1);
        return end() - 1;
    }

    /// Gives an attribute the qualified name it is being replaced with,
    /// keeping its place, its namespace and its value. `setAttributeNodeNS`
    /// does this: the attribute stays where it was and answers to the new
    /// name from then on.
    void rename(iterator position, std::string new_name, std::uint32_t local_at) {
        const std::size_t at = static_cast<std::size_t>(position - begin());
        Attribute& item = m_storage[m_start + at];
        if (!m_index) {
            item.name = std::move(new_name);
            item.local_at = local_at;
            return;
        }
        const std::size_t number = m_index->numbers[m_index->numbers_start + at];
        const auto named = m_index->number_of.find(item.name);
        if (named != m_index->number_of.end() && named->second == number) {
            bool another = false;
            for (std::size_t i = 0; i < size(); ++i) {
                if (i != at && m_storage[m_start + i].name == item.name) {
                    another = true;
                    break;
                }
            }
            if (another) named->second = kRepeated;
            else m_index->number_of.erase(named);
        }
        item.name = std::move(new_name);
        item.local_at = local_at;
        const auto inserted = m_index->number_of.emplace(item.name, number);
        if (!inserted.second) inserted.first->second = kRepeated;
    }

    std::size_t erase(const std::string& name) {
        auto it = find(name);
        if (it == end()) return 0;
        erase(it);
        return 1;
    }
    iterator erase(const_iterator position) {
        const std::size_t at = static_cast<std::size_t>(position - m_storage.cbegin()) - m_start;
        const std::size_t live = size();
        if (m_index) index_removed(m_storage[m_start + at], at, m_index->numbers[m_index->numbers_start + at]);
        if (at == 0) {
            // Front erase: leave the dead pair in place and advance the
            // logical start. Compaction happens only when the dead prefix is
            // at least as large as the live tail, so a full drain is linear.
            ++m_start;
            if (m_index) ++m_index->numbers_start;
            if (live == 1) {
                clear();
                return end();
            }
            compact();
            return begin();
        }
        if (m_index) {
            m_index->numbers.erase(m_index->numbers.begin() + static_cast<std::ptrdiff_t>(m_index->numbers_start + at));
        }
        if (at + 1 == live) {
            m_storage.pop_back();
            if (live == 1) {
                clear();
                return end();
            }
            return end();
        }
        return m_storage.erase(position);
    }
    void clear() {
        m_storage.clear();
        m_start = 0;
        m_index.reset();
    }

    /// The same attributes with the same values, in any order: what "the same
    /// attributes" means wherever the HTML standard compares two elements.
    bool operator==(const AttributeList& other) const {
        if (size() != other.size()) return false;
        for (const auto& item : *this) {
            auto it = other.find_expanded(item.ns, item.local_name());
            if (it == other.end() || it->value != item.value) return false;
        }
        return true;
    }
    bool operator!=(const AttributeList& other) const { return !(*this == other); }

    /// The expanded name as one string, for the index: a namespace name read
    /// from XML cannot hold U+0001, so the separator cannot be part of one.
    static std::string expanded_key(const std::string* ns, std::string_view local) {
        std::string key;
        const std::string* uri = ns ? ns : &empty_namespace();
        key.reserve(uri->size() + 1 + local.size());
        key += *uri;
        key += '\x01';
        key += local;
        return key;
    }
    static std::string expanded_key(const Attribute& item) { return expanded_key(item.ns, item.local_name()); }

private:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    static constexpr std::size_t kRepeated = static_cast<std::size_t>(-2);
    /// Shorter lists are scanned: that is faster than hashing the name.
    static constexpr std::size_t kIndexFrom = 32;

    static const std::string& empty_namespace() {
        static const std::string none;
        return none;
    }

    struct Index {
        /// The number each qualified name was first given; kRepeated when
        /// more than one attribute has that name.
        std::unordered_map<std::string, std::size_t> number_of;
        /// The number of each expanded name: one attribute has each.
        std::unordered_map<std::string, std::size_t> number_of_expanded;
        /// numbers[numbers_start + i] is the number of the i-th live item:
        /// always ascending. The start advances with a front erase.
        std::vector<std::size_t> numbers;
        std::size_t numbers_start{0};
        std::size_t next{0};
    };

    void build_index() const {
        if (m_index) return;
        m_index = std::make_unique<Index>();
        m_index->number_of.reserve(size() * 2);
        m_index->number_of_expanded.reserve(size() * 2);
        m_index->numbers.reserve(size() * 2);
        for (std::size_t i = 0; i < size(); ++i) {
            const Attribute& item = m_storage[m_start + i];
            const auto inserted = m_index->number_of.emplace(item.name, i);
            if (!inserted.second) inserted.first->second = kRepeated;
            m_index->number_of_expanded.emplace(expanded_key(item), i);
            m_index->numbers.push_back(i);
        }
        m_index->next = size();
    }

    /// A new item is in m_storage.back(); `absolute_at` is `size() - 1` plus
    /// the dead prefix (the index only exists once the list is long).
    void index_added(std::size_t absolute_at) {
        if (!m_index) {
            if (size() < kIndexFrom) return;
            build_index();
            return;
        }
        const Attribute& item = m_storage[absolute_at];
        const auto inserted = m_index->number_of.emplace(item.name, m_index->next);
        if (!inserted.second) inserted.first->second = kRepeated;
        m_index->number_of_expanded.emplace(expanded_key(item), m_index->next);
        m_index->numbers.push_back(m_index->next++);
    }

    /// Takes an item that is going away out of both indexes: `relative_at`
    /// is where it is now, `number` the number it was given.
    void index_removed(const Attribute& item, std::size_t relative_at, std::size_t number) {
        const auto expanded = m_index->number_of_expanded.find(expanded_key(item));
        if (expanded != m_index->number_of_expanded.end() && expanded->second == number) {
            m_index->number_of_expanded.erase(expanded);
        }
        const auto named = m_index->number_of.find(item.name);
        if (named == m_index->number_of.end()) return;
        if (named->second != kRepeated) {
            // The one attribute with that name is going away.
            if (named->second == number) m_index->number_of.erase(named);
            return;
        }
        // A name with more than one attribute: after the removal there may be
        // one left, which then answers on its own again. A scan is right here:
        // the list this happens to has a handful of attributes.
        const std::size_t last = size() - 1;
        for (std::size_t i = 0; i <= last; ++i) {
            if (i == relative_at) continue;
            if (m_storage[m_start + i].name != item.name) continue;
            named->second = kRepeated;
            return;
        }
        m_index->number_of.erase(named);
    }

    std::size_t position_scan(const std::string& name) const {
        for (std::size_t i = 0; i < size(); ++i) {
            if (m_storage[m_start + i].name == name) return i;
        }
        return kNone;
    }

    std::size_t position(const std::string& name) const {
        if (!m_index) {
            if (size() < kIndexFrom) return position_scan(name);
            build_index();
        }
        const auto found = m_index->number_of.find(name);
        if (found == m_index->number_of.end()) return kNone;
        if (found->second == kRepeated) return position_scan(name);
        const auto first = m_index->numbers.begin() + static_cast<std::ptrdiff_t>(m_index->numbers_start);
        const auto at = std::lower_bound(first, m_index->numbers.end(), found->second);
        return static_cast<std::size_t>(at - first);
    }

    std::size_t position_expanded(const std::string* ns, std::string_view local) const {
        if (!m_index) {
            if (size() < kIndexFrom) {
                for (std::size_t i = 0; i < size(); ++i) {
                    if (m_storage[m_start + i].is_expanded(ns, local)) return i;
                }
                return kNone;
            }
            build_index();
        }
        const auto found = m_index->number_of_expanded.find(expanded_key(ns, local));
        if (found == m_index->number_of_expanded.end()) return kNone;
        const auto first = m_index->numbers.begin() + static_cast<std::ptrdiff_t>(m_index->numbers_start);
        const auto at = std::lower_bound(first, m_index->numbers.end(), found->second);
        return static_cast<std::size_t>(at - first);
    }

    /// Drop a dead prefix once it is at least as large as the live tail.
    /// Doing it only at that point keeps repeated front erases amortized O(1)
    /// and keeps an empty list allocation-free.
    void compact() {
        const std::size_t live = size();
        if (live == 0) {
            clear();
            return;
        }
        if (m_start >= live) {
            m_storage.erase(m_storage.begin(), m_storage.begin() + static_cast<std::ptrdiff_t>(m_start));
            m_start = 0;
        }
        if (m_index && m_index->numbers_start >= live) {
            m_index->numbers.erase(m_index->numbers.begin(),
                                   m_index->numbers.begin() + static_cast<std::ptrdiff_t>(m_index->numbers_start));
            m_index->numbers_start = 0;
        }
    }

    std::vector<value_type> m_storage;
    std::size_t m_start{0};
    mutable std::unique_ptr<Index> m_index;
};

} // namespace litebird
