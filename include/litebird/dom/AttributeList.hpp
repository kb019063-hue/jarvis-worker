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
// index maps a name to its number and the position is found by bisection.
// The items and the index numbers live in vectors with a logical start
// offset. Removing the last one pops the back; removing the first advances
// the start and leaves a dead prefix that is compacted only once it is at
// least as large as the live tail. Draining 50,000 attributes from either end
// is therefore linear, and an empty list still allocates nothing. A middle
// removal still moves the tail of the vector.
//
// The interface is the part of unordered_map the engine used, so the callers
// did not change: find/end, count, operator[], erase, range-for over pairs.
// A name must not be changed through an iterator.

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace litebird {

class AttributeList {
public:
    using value_type = std::pair<std::string, std::string>;
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

    iterator find(const std::string& name) {
        const std::size_t at = position(name);
        return at == kNone ? end() : begin() + static_cast<std::ptrdiff_t>(at);
    }
    const_iterator find(const std::string& name) const {
        const std::size_t at = position(name);
        return at == kNone ? end() : begin() + static_cast<std::ptrdiff_t>(at);
    }
    std::size_t count(const std::string& name) const { return position(name) == kNone ? 0 : 1; }

    /// The value of `name`, added at the end with an empty value when absent.
    std::string& operator[](const std::string& name) {
        const std::size_t at = position(name);
        if (at != kNone) return m_storage[m_start + at].second;
        m_storage.emplace_back(name, std::string());
        if (m_index) {
            m_index->number_of.emplace(name, m_index->next);
            m_index->numbers.push_back(m_index->next++);
        }
        return m_storage.back().second;
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
        if (m_index) m_index->number_of.erase(position->first);
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
            auto it = other.find(item.first);
            if (it == other.end() || it->second != item.second) return false;
        }
        return true;
    }
    bool operator!=(const AttributeList& other) const { return !(*this == other); }

private:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    /// Shorter lists are scanned: that is faster than hashing the name.
    static constexpr std::size_t kIndexFrom = 32;

    struct Index {
        /// The number each name was given when it was added.
        std::unordered_map<std::string, std::size_t> number_of;
        /// numbers[numbers_start + i] is the number of the i-th live item:
        /// always ascending. The start advances with a front erase.
        std::vector<std::size_t> numbers;
        std::size_t numbers_start{0};
        std::size_t next{0};
    };

    std::size_t position(const std::string& name) const {
        if (!m_index) {
            if (size() < kIndexFrom) {
                for (std::size_t i = 0; i < size(); ++i) {
                    if (m_storage[m_start + i].first == name) return i;
                }
                return kNone;
            }
            m_index = std::make_unique<Index>();
            m_index->number_of.reserve(size() * 2);
            m_index->numbers.reserve(size() * 2);
            // The first of two with one name is the one a scan would find.
            for (std::size_t i = 0; i < size(); ++i) {
                m_index->number_of.emplace(m_storage[m_start + i].first, i);
                m_index->numbers.push_back(i);
            }
            m_index->next = size();
        }
        const auto found = m_index->number_of.find(name);
        if (found == m_index->number_of.end()) return kNone;
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

// The namespaces of the attributes of one element that have one, next to
// its AttributeList: the list keeps qualified names ("xlink:href") and this
// says which namespace each stands in. Most elements have none and most of
// the rest have one or two; an element with thousands (50,000 xmlns:
// declarations on one root) gets two indexes, as the list does.
class AttributeNamespaces {
public:
    AttributeNamespaces() = default;
    AttributeNamespaces(const AttributeNamespaces& other) : m_items(other.m_items) {}
    AttributeNamespaces(AttributeNamespaces&&) noexcept = default;
    AttributeNamespaces& operator=(const AttributeNamespaces& other) {
        if (this != &other) {
            m_items = other.m_items;
            m_index.reset();
        }
        return *this;
    }
    AttributeNamespaces& operator=(AttributeNamespaces&&) noexcept = default;

    bool empty() const { return m_items.empty(); }
    std::size_t size() const { return m_items.size(); }

    /// The namespace recorded for `qualified`, null when there is none.
    const std::string* find(const std::string& qualified) const {
        const std::size_t at = position(qualified);
        return at == kNone ? nullptr : m_items[at].uri;
    }

    /// The qualified name recorded with this namespace and local name.
    const std::string* find_expanded(const std::string& uri, const std::string& local) const {
        if (!m_index && m_items.size() < kIndexFrom) {
            for (const auto& item : m_items) {
                if (*item.uri == uri && item.qualified.compare(item.local_at, std::string::npos, local) == 0) {
                    return &item.qualified;
                }
            }
            return nullptr;
        }
        build_index();
        const auto found = m_index->by_expanded.find(expanded_key(uri, local));
        return found == m_index->by_expanded.end() ? nullptr : &m_items[found->second].qualified;
    }

    void set(const std::string& qualified, const std::string& uri) {
        const std::string* interned = intern(uri);
        const std::size_t at = position(qualified);
        if (at != kNone) {
            if (m_items[at].uri == interned) return;
            if (m_index) forget_expanded(at);
            m_items[at].uri = interned;
            if (m_index) m_index->by_expanded.emplace(expanded_key(m_items[at]), at);
            return;
        }
        // With a namespace, what is before the first colon is the prefix.
        const auto colon = qualified.find(':');
        m_items.push_back({qualified, interned, colon == std::string::npos ? 0 : colon + 1});
        if (m_index) {
            m_index->by_name.emplace(qualified, m_items.size() - 1);
            m_index->by_expanded.emplace(expanded_key(m_items.back()), m_items.size() - 1);
        }
    }

    /// The order of this table is seen by nobody (the order of the
    /// attributes is the AttributeList's), so the last entry takes the place
    /// of the one removed and nothing else moves.
    void erase(const std::string& qualified) {
        const std::size_t at = position(qualified);
        if (at == kNone) return;
        const std::size_t last = m_items.size() - 1;
        if (m_index) {
            m_index->by_name.erase(m_items[at].qualified);
            forget_expanded(at);
            if (at != last) {
                m_index->by_name[m_items[last].qualified] = at;
                const auto moved = m_index->by_expanded.find(expanded_key(m_items[last]));
                if (moved != m_index->by_expanded.end() && moved->second == last) moved->second = at;
            }
        }
        if (at != last) m_items[at] = std::move(m_items[last]);
        m_items.pop_back();
    }

    /// One copy of each namespace name for the whole program: an SVG file
    /// with 100,000 xlink:href attributes holds the name once.
    static const std::string* intern(const std::string& uri);

private:
    struct Item {
        std::string qualified;
        const std::string* uri;
        std::size_t local_at;
    };
    struct Index {
        std::unordered_map<std::string, std::size_t> by_name;
        std::unordered_map<std::string, std::size_t> by_expanded;
    };
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    static constexpr std::size_t kIndexFrom = 32;

    static std::string expanded_key(const std::string& uri, const std::string& local) {
        std::string key;
        key.reserve(uri.size() + 1 + local.size());
        key += uri;
        key += '\x01';     // not a character of a namespace name read from XML
        key += local;
        return key;
    }

    static std::string expanded_key(const Item& item) {
        return expanded_key(*item.uri, item.qualified.substr(item.local_at));
    }

    /// Takes the entry of m_items[at] out of the index by namespace and
    /// local name, if the entry is its own.
    void forget_expanded(std::size_t at) {
        const auto found = m_index->by_expanded.find(expanded_key(m_items[at]));
        if (found != m_index->by_expanded.end() && found->second == at) m_index->by_expanded.erase(found);
    }

    void build_index() const {
        if (m_index) return;
        m_index = std::make_unique<Index>();
        m_index->by_name.reserve(m_items.size() * 2);
        m_index->by_expanded.reserve(m_items.size() * 2);
        for (std::size_t i = 0; i < m_items.size(); ++i) {
            m_index->by_name.emplace(m_items[i].qualified, i);
            m_index->by_expanded.emplace(expanded_key(m_items[i]), i);
        }
    }

    std::size_t position(const std::string& qualified) const {
        if (!m_index && m_items.size() < kIndexFrom) {
            for (std::size_t i = 0; i < m_items.size(); ++i) {
                if (m_items[i].qualified == qualified) return i;
            }
            return kNone;
        }
        build_index();
        const auto found = m_index->by_name.find(qualified);
        return found == m_index->by_name.end() ? kNone : found->second;
    }

    std::vector<Item> m_items;
    mutable std::unique_ptr<Index> m_index;
};

} // namespace litebird
