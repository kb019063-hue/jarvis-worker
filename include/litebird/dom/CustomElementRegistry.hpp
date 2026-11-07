#pragma once

#include "litebird/dom/Element.hpp"
#include "litebird/js/JsValue.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace litebird {

struct CustomElementDefinition {
    std::string name;
    JsValue constructor;
    std::vector<std::string> observed_attributes;
};

class CustomElementRegistry {
public:
    static CustomElementRegistry& default_registry();

    bool define(const std::string& name, JsValue constructor);
    JsValue get(const std::string& name) const;
    JsValue when_defined(const std::string& name);

    bool is_defined(const std::string& name) const;
    const CustomElementDefinition* get_definition(const std::string& name) const;

    void upgrade_element(const std::shared_ptr<Element>& el);
    void upgrade_tree(const std::shared_ptr<Node>& root);

    // Lifecycle dispatch helpers
    void notify_connected(const std::shared_ptr<Element>& el);
    void notify_disconnected(const std::shared_ptr<Element>& el);
    void notify_attribute_changed(
        const std::shared_ptr<Element>& el,
        const std::string& name,
        const std::string& old_val,
        const std::string& new_val
    );

    static bool is_valid_custom_element_name(const std::string& name);

    void clear() {
        m_definitions.clear();
        m_when_defined_resolvers.clear();
    }

private:
    std::unordered_map<std::string, CustomElementDefinition> m_definitions;
    std::unordered_map<std::string, std::vector<JsValue>> m_when_defined_resolvers;
};

} // namespace litebird
