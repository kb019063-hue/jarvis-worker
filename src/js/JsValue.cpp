#include "litebird/js/JsValue.hpp"
#include "litebird/dom/Node.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace litebird {

bool JsValue::is_truthy() const {
    switch (type) {
        case JsType::Undefined:
        case JsType::Null:
            return false;
        case JsType::Boolean:
            return bool_val;
        case JsType::Number:
            return number_val != 0.0 && !std::isnan(number_val);
        case JsType::String:
            return !string_val.empty();
        case JsType::Object:
        case JsType::Function:
        case JsType::NativeFunction:
        case JsType::DomNode:
            return true;
    }
    return false;
}

std::string JsValue::to_string() const {
    switch (type) {
        case JsType::Undefined: return "undefined";
        case JsType::Null: return "null";
        case JsType::Boolean: return bool_val ? "true" : "false";
        case JsType::Number: {
            if (number_val == static_cast<int64_t>(number_val)) {
                return std::to_string(static_cast<int64_t>(number_val));
            }
            std::stringstream ss;
            ss << number_val;
            return ss.str();
        }
        case JsType::String: return string_val;
        case JsType::Object: return "[object Object]";
        case JsType::Function: return "[Function]";
        case JsType::NativeFunction: return "[Native Function]";
        case JsType::DomNode: {
            if (dom_node && dom_node->is_element()) {
                auto el = std::static_pointer_cast<Element>(dom_node);
                return "[object HTML" + el->tag_name() + "Element]";
            }
            return "[object Node]";
        }
    }
    return "undefined";
}

double JsValue::to_number() const {
    switch (type) {
        case JsType::Undefined: return 0.0;
        case JsType::Null: return 0.0;
        case JsType::Boolean: return bool_val ? 1.0 : 0.0;
        case JsType::Number: return number_val;
        case JsType::String: {
            try { return std::stod(string_val); } catch (...) { return 0.0; }
        }
        default: return 0.0;
    }
}

JsValue JsValue::get_property(const std::string& name) const {
    if (type == JsType::DomNode && dom_node) {
        if (auto el = std::dynamic_pointer_cast<Element>(dom_node)) {
            if (name == "tagName") return JsValue::make_string(el->tag_name());
            if (name == "id") return JsValue::make_string(el->id());
            if (name == "textContent" || name == "innerText") return JsValue::make_string(el->text_content());
            if (name == "style") {
                // Return a style object proxy
                JsValue style_obj = JsValue::make_object();
                style_obj.set_property("color", JsValue::make_string(el->get_attribute("style")));
                return style_obj;
            }
        }
    }

    if (properties) {
        auto it = properties->find(name);
        if (it != properties->end()) return it->second;
    }
    return JsValue::make_undefined();
}

void JsValue::set_property(const std::string& name, JsValue val) {
    if (type == JsType::DomNode && dom_node) {
        if (auto el = std::dynamic_pointer_cast<Element>(dom_node)) {
            if (name == "textContent" || name == "innerText") {
                el->children().clear();
                el->append_child(std::make_shared<Text>(val.to_string()));
                return;
            }
        }
    }

    if (!properties) {
        properties = std::make_shared<std::unordered_map<std::string, JsValue>>();
    }
    (*properties)[name] = std::move(val);
}

JsValue JsValue::make_dom_node(std::shared_ptr<Node> node) {
    JsValue v;
    v.type = JsType::DomNode;
    v.dom_node = std::move(node);
    v.properties = std::make_shared<std::unordered_map<std::string, JsValue>>();
    return v;
}

} // namespace litebird
