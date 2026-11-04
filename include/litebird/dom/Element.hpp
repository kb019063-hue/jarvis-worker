#pragma once

#include "litebird/dom/Node.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <algorithm>

namespace litebird {

class Element : public Node {
public:
    explicit Element(std::string tag_name)
        : Node(NodeType::Element), m_tag_name(std::move(tag_name)) {
        std::transform(m_tag_name.begin(), m_tag_name.end(), m_tag_name.begin(), ::tolower);
    }

    const std::string& tag_name() const { return m_tag_name; }

    bool has_attribute(const std::string& name) const {
        return m_attributes.find(name) != m_attributes.end();
    }

    std::string get_attribute(const std::string& name) const {
        auto it = m_attributes.find(name);
        return it != m_attributes.end() ? it->second : "";
    }

    void set_attribute(const std::string& name, std::string value) {
        m_attributes[name] = std::move(value);
    }

    const std::unordered_map<std::string, std::string>& attributes() const {
        return m_attributes;
    }

    std::string id() const {
        return get_attribute("id");
    }

    std::vector<std::string> classes() const {
        std::vector<std::string> result;
        std::string class_attr = get_attribute("class");
        if (class_attr.empty()) return result;

        std::stringstream ss(class_attr);
        std::string item;
        while (ss >> item) {
            result.push_back(item);
        }
        return result;
    }

    bool has_class(const std::string& class_name) const {
        auto list = classes();
        return std::find(list.begin(), list.end(), class_name) != list.end();
    }

    std::string text_content() const override {
        std::string result;
        for (const auto& child : m_children) {
            result += child->text_content();
        }
        return result;
    }

    void dump(std::ostream& os, int indent = 0) const override;

private:
    std::string m_tag_name;
    std::unordered_map<std::string, std::string> m_attributes;
};

} // namespace litebird
