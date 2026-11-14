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
// index from name to position, built the first time it is searched: without
// it every setAttribute and every attribute the parser adds scans the whole
// list, and the element costs the square of its size.
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
    AttributeList(const AttributeList& other) : m_items(other.m_items) {}
    AttributeList(AttributeList&&) noexcept = default;
    AttributeList& operator=(const AttributeList& other) {
        if (this != &other) {
            m_items = other.m_items;
            m_index.reset();
        }
        return *this;
    }
    AttributeList& operator=(AttributeList&&) noexcept = default;

    iterator begin() { return m_items.begin(); }
    iterator end() { return m_items.end(); }
    const_iterator begin() const { return m_items.begin(); }
    const_iterator end() const { return m_items.end(); }

    std::size_t size() const { return m_items.size(); }
    bool empty() const { return m_items.empty(); }
    const value_type& operator[](std::size_t index) const { return m_items[index]; }

    iterator find(const std::string& name) {
        const std::size_t at = position(name);
        return at == kNone ? m_items.end() : m_items.begin() + static_cast<std::ptrdiff_t>(at);
    }
    const_iterator find(const std::string& name) const {
        const std::size_t at = position(name);
        return at == kNone ? m_items.end() : m_items.begin() + static_cast<std::ptrdiff_t>(at);
    }
    std::size_t count(const std::string& name) const { return position(name) == kNone ? 0 : 1; }

    /// The value of `name`, added at the end with an empty value when absent.
    std::string& operator[](const std::string& name) {
        const std::size_t at = position(name);
        if (at != kNone) return m_items[at].second;
        m_items.emplace_back(name, std::string());
        if (m_index) m_index->emplace(name, m_items.size() - 1);
        return m_items.back().second;
    }

    std::size_t erase(const std::string& name) {
        auto it = find(name);
        if (it == m_items.end()) return 0;
        erase(it);
        return 1;
    }
    iterator erase(const_iterator position) {
        if (m_index) {
            // Taking the last one away moves nothing; anything else moves
            // every attribute after it, and the index is built again when
            // it is next needed.
            if (position + 1 == m_items.cend()) m_index->erase(position->first);
            else m_index.reset();
        }
        return m_items.erase(position);
    }
    void clear() {
        m_items.clear();
        m_index.reset();
    }

    /// The same attributes with the same values, in any order: what "the same
    /// attributes" means wherever the HTML standard compares two elements.
    bool operator==(const AttributeList& other) const {
        if (m_items.size() != other.m_items.size()) return false;
        for (const auto& item : m_items) {
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

    std::size_t position(const std::string& name) const {
        if (m_items.size() < kIndexFrom) {
            for (std::size_t i = 0; i < m_items.size(); ++i) {
                if (m_items[i].first == name) return i;
            }
            return kNone;
        }
        if (!m_index) {
            m_index = std::make_unique<std::unordered_map<std::string, std::size_t>>();
            m_index->reserve(m_items.size() * 2);
            // The first of two with one name is the one a scan would find.
            for (std::size_t i = 0; i < m_items.size(); ++i) m_index->emplace(m_items[i].first, i);
        }
        const auto found = m_index->find(name);
        return found == m_index->end() ? kNone : found->second;
    }

    std::vector<value_type> m_items;
    mutable std::unique_ptr<std::unordered_map<std::string, std::size_t>> m_index;
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
        if (m_items.size() < kIndexFrom) {
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
            m_items[at].uri = interned;
            m_index.reset();
            return;
        }
        // With a namespace, what is before the first colon is the prefix.
        const auto colon = qualified.find(':');
        m_items.push_back({qualified, interned, colon == std::string::npos ? 0 : colon + 1});
        if (m_index) {
            m_index->by_name.emplace(qualified, m_items.size() - 1);
            m_index->by_expanded.emplace(expanded_key(uri, qualified.substr(m_items.back().local_at)), m_items.size() - 1);
        }
    }

    void erase(const std::string& qualified) {
        const std::size_t at = position(qualified);
        if (at == kNone) return;
        m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(at));
        m_index.reset();
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

    void build_index() const {
        if (m_index) return;
        m_index = std::make_unique<Index>();
        m_index->by_name.reserve(m_items.size() * 2);
        m_index->by_expanded.reserve(m_items.size() * 2);
        for (std::size_t i = 0; i < m_items.size(); ++i) {
            m_index->by_name.emplace(m_items[i].qualified, i);
            m_index->by_expanded.emplace(expanded_key(*m_items[i].uri, m_items[i].qualified.substr(m_items[i].local_at)), i);
        }
    }

    std::size_t position(const std::string& qualified) const {
        if (m_items.size() < kIndexFrom) {
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
