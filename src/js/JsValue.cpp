#include "litebird/js/JsValue.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/dom/Node.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/dom/Event.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace litebird {

static JsValue::FunctionCaller s_function_caller = nullptr;

void JsValue::set_function_caller(FunctionCaller caller) {
    s_function_caller = std::move(caller);
}

JsValue JsValue::make_object(JsObject* obj) {
    JsValue v;
    v.type = JsType::Object;
    if (obj) {
        v.object_val = obj;
    } else {
        v.object_val = Heap::default_heap().allocate_object();
    }
    return v;
}

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

bool JsValue::is_array() const {
    return type == JsType::Object && object_val && object_val->is_array;
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
        case JsType::Object: {
            if (object_val && object_val->is_array) {
                std::string res = "[";
                for (size_t i = 0; i < object_val->array_elements.size(); ++i) {
                    res += object_val->array_elements[i].to_string();
                    if (i + 1 < object_val->array_elements.size()) res += ", ";
                }
                res += "]";
                return res;
            }
            return "[object Object]";
        }
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

JsValue JsValue::call(const std::vector<JsValue>& args) const {
    if (native_fn) {
        return native_fn(args);
    }
    if (type == JsType::Function && s_function_caller) {
        return s_function_caller(*this, args);
    }
    return JsValue::make_undefined();
}

JsValue JsValue::get_property(const std::string& name) const {
    if (type == JsType::DomNode && dom_node) {
        if (auto el = std::dynamic_pointer_cast<Element>(dom_node)) {
            if (name == "tagName") return JsValue::make_string(el->tag_name());
            if (name == "id") return JsValue::make_string(el->id());
            if (name == "textContent" || name == "innerText") return JsValue::make_string(el->text_content());
            if (name == "style") {
                JsValue style_obj = JsValue::make_object();
                style_obj.set_property("color", JsValue::make_string(el->get_attribute("style")));
                return style_obj;
            }
            if (name == "click") {
                auto node = dom_node;
                return JsValue::make_native_function([node](const std::vector<JsValue>&) {
                    if (auto elem = std::dynamic_pointer_cast<Element>(node)) {
                        elem->click();
                    }
                    return JsValue::make_undefined();
                });
            }
            if (name == "addEventListener") {
                auto node = dom_node;
                return JsValue::make_native_function([node](const std::vector<JsValue>& args) {
                    if (args.size() < 2) return JsValue::make_undefined();
                    std::string ev_type = args[0].to_string();
                    JsValue cb = args[1];
                    node->add_event_listener(ev_type, [cb](const std::shared_ptr<Event>& ev) mutable {
                        JsValue ev_obj = JsValue::make_object();
                        ev_obj.set_property("type", JsValue::make_string(ev->type()));
                        cb.call({ev_obj});
                    });
                    return JsValue::make_undefined();
                });
            }
            if (name == "dispatchEvent") {
                auto node = dom_node;
                return JsValue::make_native_function([node](const std::vector<JsValue>& args) {
                    std::string ev_type = args.empty() ? "custom" : args[0].to_string();
                    auto ev = std::make_shared<Event>(ev_type, true, true);
                    bool ok = node->dispatch_event(ev);
                    return JsValue::make_boolean(ok);
                });
            }
        }
    }

    if (object_val) {
        if (name == "__proto__") {
            if (object_val->prototype) {
                return JsValue::make_object(object_val->prototype);
            }
            return JsValue::make_null();
        }

        if (object_val->is_array) {
            if (name == "length") {
                return JsValue::make_number(object_val->array_elements.size());
            }
            if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0]))) {
                try {
                    size_t idx = std::stoul(name);
                    if (idx < object_val->array_elements.size()) {
                        return object_val->array_elements[idx];
                    }
                    return JsValue::make_undefined();
                } catch (...) {}
            }
        }

        // Own properties
        auto it = object_val->properties.find(name);
        if (it != object_val->properties.end()) return it->second;

        // Prototype chain delegation
        JsObject* curr_proto = object_val->prototype;
        while (curr_proto) {
            auto pit = curr_proto->properties.find(name);
            if (pit != curr_proto->properties.end()) {
                return pit->second;
            }
            curr_proto = curr_proto->prototype;
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

    if (object_val) {
        if (name == "__proto__") {
            if (val.type == JsType::Object && val.object_val) {
                object_val->prototype = val.object_val;
            } else if (val.type == JsType::Null) {
                object_val->prototype = nullptr;
            }
            return;
        }

        if (object_val->is_array) {
            if (name == "length") {
                size_t new_len = static_cast<size_t>(std::max(0.0, val.to_number()));
                object_val->array_elements.resize(new_len, JsValue::make_undefined());
                return;
            }
            if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0]))) {
                try {
                    size_t idx = std::stoul(name);
                    if (idx >= object_val->array_elements.size()) {
                        object_val->array_elements.resize(idx + 1, JsValue::make_undefined());
                    }
                    object_val->array_elements[idx] = std::move(val);
                    return;
                } catch (...) {}
            }
        }

        object_val->properties[name] = std::move(val);
        return;
    }

    if (!properties) {
        properties = std::make_shared<std::unordered_map<std::string, JsValue>>();
    }
    (*properties)[name] = std::move(val);
}

JsValue JsValue::make_array(const std::vector<JsValue>& elements) {
    JsObject* obj = Heap::default_heap().allocate_object();
    obj->is_array = true;
    obj->array_elements = elements;

    JsValue arr = JsValue::make_object(obj);

    arr.set_property("push", JsValue::make_native_function([obj](const std::vector<JsValue>& args) {
        for (const auto& a : args) {
            obj->array_elements.push_back(a);
        }
        return JsValue::make_number(obj->array_elements.size());
    }));

    arr.set_property("pop", JsValue::make_native_function([obj](const std::vector<JsValue>&) {
        if (obj->array_elements.empty()) return JsValue::make_undefined();
        JsValue last = std::move(obj->array_elements.back());
        obj->array_elements.pop_back();
        return last;
    }));

    arr.set_property("map", JsValue::make_native_function([obj](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_array();
        JsValue callback = args[0];
        std::vector<JsValue> res;
        for (size_t i = 0; i < obj->array_elements.size(); ++i) {
            res.push_back(callback.call({obj->array_elements[i], JsValue::make_number(i)}));
        }
        return JsValue::make_array(res);
    }));

    arr.set_property("filter", JsValue::make_native_function([obj](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_array();
        JsValue callback = args[0];
        std::vector<JsValue> res;
        for (size_t i = 0; i < obj->array_elements.size(); ++i) {
            JsValue keep = callback.call({obj->array_elements[i], JsValue::make_number(i)});
            if (keep.is_truthy()) {
                res.push_back(obj->array_elements[i]);
            }
        }
        return JsValue::make_array(res);
    }));

    arr.set_property("forEach", JsValue::make_native_function([obj](const std::vector<JsValue>& args) {
        if (!args.empty()) {
            JsValue callback = args[0];
            for (size_t i = 0; i < obj->array_elements.size(); ++i) {
                callback.call({obj->array_elements[i], JsValue::make_number(i)});
            }
        }
        return JsValue::make_undefined();
    }));

    arr.set_property("join", JsValue::make_native_function([obj](const std::vector<JsValue>& args) {
        std::string sep = args.empty() ? "," : args[0].to_string();
        std::string res;
        for (size_t i = 0; i < obj->array_elements.size(); ++i) {
            res += obj->array_elements[i].to_string();
            if (i + 1 < obj->array_elements.size()) res += sep;
        }
        return JsValue::make_string(res);
    }));

    return arr;
}

JsValue JsValue::make_dom_node(std::shared_ptr<Node> node) {
    JsValue v;
    v.type = JsType::DomNode;
    v.dom_node = std::move(node);
    v.object_val = Heap::default_heap().allocate_object();
    return v;
}

} // namespace litebird
