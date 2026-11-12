#include "litebird/dom/CustomElementRegistry.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/js/Promise.hpp"
#include "litebird/js/Heap.hpp"
#include <algorithm>
#include <unordered_set>

namespace litebird {

CustomElementRegistry& CustomElementRegistry::default_registry() {
    static CustomElementRegistry s_registry;
    return s_registry;
}

bool CustomElementRegistry::is_valid_custom_element_name(const std::string& name) {
    if (name.size() < 2) return false;
    // Must start with ASCII lowercase
    if (name[0] < 'a' || name[0] > 'z') return false;

    // Must contain a hyphen
    if (name.find('-') == std::string::npos) return false;

    static const std::unordered_set<std::string> RESERVED = {
        "annotation-xml", "color-profile", "font-face", "font-face-src",
        "font-face-uri", "font-face-format", "font-face-name", "missing-glyph"
    };
    if (RESERVED.count(name)) return false;

    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

bool CustomElementRegistry::define(const std::string& name, JsValue constructor) {
    if (!is_valid_custom_element_name(name)) return false;
    if (m_definitions.find(name) != m_definitions.end()) return false;

    std::vector<std::string> observed;
    JsValue obs_attr = constructor.get_property("observedAttributes");
    if (obs_attr.is_function()) {
        obs_attr = obs_attr.call({});
    }
    if (obs_attr.is_array()) {
        for (const auto& item : obs_attr.array_elements()) {
            observed.push_back(item.to_string());
        }
    }

    CustomElementDefinition def;
    def.name = name;
    def.constructor = constructor;
    def.observed_attributes = std::move(observed);
    m_definitions[name] = def;

    // Resolve any waiting whenDefined promises
    auto it = m_when_defined_resolvers.find(name);
    if (it != m_when_defined_resolvers.end()) {
        for (auto& resolve_fn : it->second) {
            if (resolve_fn.is_callable()) {
                resolve_fn.call({constructor});
            }
        }
        m_when_defined_resolvers.erase(it);
    }

    return true;
}

JsValue CustomElementRegistry::get(const std::string& name) const {
    auto it = m_definitions.find(name);
    if (it != m_definitions.end()) {
        return it->second.constructor;
    }
    return JsValue::make_undefined();
}

JsValue CustomElementRegistry::when_defined(const std::string& name) {
    if (is_defined(name)) {
        return Promise::make_resolved(get(name));
    }

    auto promise_data = std::make_shared<PromiseData>();
    JsValue resolve_fn = JsValue::make_native_function([promise_data](const std::vector<JsValue>& args) {
        JsValue val = args.empty() ? JsValue::make_undefined() : args[0];
        Promise::resolve(promise_data, val);
        return JsValue::make_undefined();
    });

    m_when_defined_resolvers[name].push_back(resolve_fn);
    return Promise::create_promise_instance(promise_data);
}

bool CustomElementRegistry::is_defined(const std::string& name) const {
    return m_definitions.find(name) != m_definitions.end();
}

const CustomElementDefinition* CustomElementRegistry::get_definition(const std::string& name) const {
    auto it = m_definitions.find(name);
    return (it != m_definitions.end()) ? &it->second : nullptr;
}

void CustomElementRegistry::upgrade_element(const std::shared_ptr<Element>& el) {
    if (!el) return;
    if (!el->custom_element_instance().is_undefined()) return;

    auto it = m_definitions.find(el->tag_name());
    if (it == m_definitions.end()) return;

    const auto& def = it->second;

    JsValue inst = JsValue::make_dom_node(el);
    JsValue proto = def.constructor.get_property("prototype");
    if (proto.type == JsType::Object && proto.object_val && inst.object_val) {
        inst.object_val->prototype = proto.object_val;
    }

    el->set_custom_element_instance(inst);

    // Run the constructor as `new` would, on the element's own wrapper. A
    // class constructor refuses to be called any other way.
    if (def.constructor.is_callable()) {
        const JsValue ctor = def.constructor;
        js_run_guarded([&] { ctor.construct({}, &inst); });
    }

    // Call attributeChangedCallback for any existing attributes that are observed
    for (const auto& [attr_name, attr_val] : el->attributes()) {
        if (std::find(def.observed_attributes.begin(), def.observed_attributes.end(), attr_name) != def.observed_attributes.end()) {
            JsValue attr_cb = inst.get_property("attributeChangedCallback");
            if (attr_cb.is_callable()) {
                js_call_guarded(attr_cb, inst, {
                    JsValue::make_string(attr_name),
                    JsValue::make_null(),
                    JsValue::make_string(attr_val)
                });
            }
        }
    }

    // If already connected to document, invoke connectedCallback
    if (el->is_connected()) {
        JsValue conn_cb = inst.get_property("connectedCallback");
        if (conn_cb.is_callable()) {
            js_call_guarded(conn_cb, inst, {});
        }
    }
}

void CustomElementRegistry::upgrade_tree(const std::shared_ptr<Node>& root) {
    if (!root) return;
    if (root->is_element()) {
        upgrade_element(std::static_pointer_cast<Element>(root));
    }
    for (const auto& ch : root->children()) {
        upgrade_tree(ch);
    }
}

void CustomElementRegistry::notify_connected(const std::shared_ptr<Element>& el) {
    if (!el) return;
    if (el->custom_element_instance().is_undefined()) {
        if (is_defined(el->tag_name())) {
            upgrade_element(el);
        }
        return;
    }

    auto inst = el->custom_element_instance();
    JsValue conn_cb = inst.get_property("connectedCallback");
    if (conn_cb.is_callable()) {
        js_call_guarded(conn_cb, inst, {});
    }
}

void CustomElementRegistry::notify_disconnected(const std::shared_ptr<Element>& el) {
    if (!el) return;
    auto inst = el->custom_element_instance();
    if (inst.is_undefined()) return;

    JsValue dis_cb = inst.get_property("disconnectedCallback");
    if (dis_cb.is_callable()) {
        js_call_guarded(dis_cb, inst, {});
    }
}

void CustomElementRegistry::notify_attribute_changed(
    const std::shared_ptr<Element>& el,
    const std::string& name,
    const std::string& old_val,
    const std::string& new_val
) {
    if (!el || old_val == new_val) return;
    auto inst = el->custom_element_instance();
    if (inst.is_undefined()) return;

    auto def = get_definition(el->tag_name());
    if (!def) return;

    if (std::find(def->observed_attributes.begin(), def->observed_attributes.end(), name) == def->observed_attributes.end()) {
        return;
    }

    JsValue attr_cb = inst.get_property("attributeChangedCallback");
    if (attr_cb.is_callable()) {
        js_call_guarded(attr_cb, inst, {
            JsValue::make_string(name),
            old_val.empty() ? JsValue::make_null() : JsValue::make_string(old_val),
            new_val.empty() ? JsValue::make_null() : JsValue::make_string(new_val)
        });
    }
}

} // namespace litebird
