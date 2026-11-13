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
// The interface is the part of unordered_map the engine used, so the callers
// did not change: find/end, count, operator[], erase, range-for over pairs.

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace litebird {

class AttributeList {
public:
    using value_type = std::pair<std::string, std::string>;
    using iterator = std::vector<value_type>::iterator;
    using const_iterator = std::vector<value_type>::const_iterator;

    iterator begin() { return m_items.begin(); }
    iterator end() { return m_items.end(); }
    const_iterator begin() const { return m_items.begin(); }
    const_iterator end() const { return m_items.end(); }

    std::size_t size() const { return m_items.size(); }
    bool empty() const { return m_items.empty(); }
    const value_type& operator[](std::size_t index) const { return m_items[index]; }

    iterator find(const std::string& name) {
        return std::find_if(m_items.begin(), m_items.end(), [&](const value_type& a) { return a.first == name; });
    }
    const_iterator find(const std::string& name) const {
        return std::find_if(m_items.begin(), m_items.end(), [&](const value_type& a) { return a.first == name; });
    }
    std::size_t count(const std::string& name) const { return find(name) == end() ? 0 : 1; }

    /// The value of `name`, added at the end with an empty value when absent.
    std::string& operator[](const std::string& name) {
        auto it = find(name);
        if (it != m_items.end()) return it->second;
        m_items.emplace_back(name, std::string());
        return m_items.back().second;
    }

    std::size_t erase(const std::string& name) {
        auto it = find(name);
        if (it == m_items.end()) return 0;
        m_items.erase(it);
        return 1;
    }
    iterator erase(const_iterator position) { return m_items.erase(position); }
    void clear() { m_items.clear(); }

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
    std::vector<value_type> m_items;
};

} // namespace litebird
