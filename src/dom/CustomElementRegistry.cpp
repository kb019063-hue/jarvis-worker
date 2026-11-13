#include "litebird/dom/CustomElementRegistry.hpp"

#include <unordered_set>

namespace litebird {

CustomElementRegistry& CustomElementRegistry::default_registry() {
    static CustomElementRegistry s_registry;
    return s_registry;
}

bool CustomElementRegistry::is_valid_custom_element_name(const std::string& name) {
    if (name.size() < 2) return false;
    if (name[0] < 'a' || name[0] > 'z') return false;
    if (name.find('-') == std::string::npos) return false;

    static const std::unordered_set<std::string> kReserved = {
        "annotation-xml", "color-profile", "font-face", "font-face-src",
        "font-face-uri", "font-face-format", "font-face-name", "missing-glyph"
    };
    if (kReserved.count(name)) return false;

    for (unsigned char c : name) {
        // Anything outside ASCII is allowed by the standard's PCENChar; inside
        // ASCII only these are.
        if (c >= 0x80) continue;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

} // namespace litebird
