#pragma once

#include <string>

namespace litebird {

/// What the tree itself needs to know about custom elements: which names
/// can be one.
///
/// The definitions (`customElements.define`) are not here. They belong to a
/// window, and live with the script bindings (js/DomObserverBindings.cpp),
/// one registry per realm. There used to be one registry for the whole
/// process: a second page in the same process saw the first one's elements,
/// and every test had to clear it.
class CustomElementRegistry {
public:
    /// A valid custom element name: lower case, starts with a letter,
    /// contains a hyphen, and is none of the reserved names.
    static bool is_valid_custom_element_name(const std::string& name);

    // For code written against the process-wide registry: there is nothing
    // left to clear.
    static CustomElementRegistry& default_registry();
    void clear() {}
};

} // namespace litebird
