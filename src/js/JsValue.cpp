#include "litebird/js/JsValue.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/Storage.hpp"
#include "litebird/dom/Node.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/dom/Event.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include "litebird/render/Canvas2D.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace litebird {

static thread_local JsValue::FunctionCaller s_function_caller = nullptr;

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

// ECMAScript Strict Equality Comparison (7.2.16).
// No type coercion happens here, which is the entire point of `===`.
bool strict_equals(const JsValue& a, const JsValue& b) {
    // Different types are never strictly equal, with no coercion.
    if (a.type != b.type) return false;

    switch (a.type) {
        case JsType::Undefined:
        case JsType::Null:
            return true;              // undefined === undefined, null === null
        case JsType::Boolean:
            return a.bool_val == b.bool_val;
        case JsType::Number:
            return a.number_val == b.number_val;   // NaN !== NaN, as required
        case JsType::String:
            return a.string_val == b.string_val;
        case JsType::DomNode:
            // A DOM wrapper is minted fresh on every property access, so its
            // JsObject address is NOT a stable identity. The node behind it is.
            // Comparing nodes is what makes `document.body === document.body`
            // and `el.shadowRoot === sr` behave the way script expects.
            return a.dom_node == b.dom_node;
        case JsType::Object:
        case JsType::Function:
        case JsType::NativeFunction:
            // Reference identity, never structural equality.
            return a.object_val == b.object_val;
        default:
            return false;
    }
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

std::vector<JsValue>& JsValue::array_elements() {
    if (!object_val) {
        object_val = Heap::default_heap().allocate_object();
        object_val->is_array = true;
    }
    return object_val->array_elements;
}

const std::vector<JsValue>& JsValue::array_elements() const {
    static const std::vector<JsValue> s_empty;
    return (object_val && object_val->is_array) ? object_val->array_elements : s_empty;
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
    if (type == JsType::Function && !is_generator && s_function_caller) {
        return s_function_caller(*this, args, nullptr);
    }
    if (native_fn) {
        return native_fn(args);
    }
    return JsValue::make_undefined();
}

JsValue JsValue::call_with_this(const JsValue& this_val, const std::vector<JsValue>& args) const {
    if (type == JsType::Function && !is_generator && s_function_caller) {
        return s_function_caller(*this, args, &this_val);
    }
    if (native_fn) {
        return native_fn(args);
    }
    return JsValue::make_undefined();
}

bool JsValue::has_property(const std::string& name) const {
    if (object_val) {
        if (object_val->properties.find(name) != object_val->properties.end()) return true;
        if (object_val->is_array && (name == "length" || name == "@@iterator" || name == "Symbol.iterator")) return true;
    }
    if (properties && properties->find(name) != properties->end()) return true;
    return false;
}

JsValue JsValue::get_property(const std::string& name) const {
    if (type == JsType::DomNode && dom_node) {
        if (auto el = std::dynamic_pointer_cast<Element>(dom_node)) {
            if (name == "tagName") return JsValue::make_string(el->tag_name());
            if (name == "id") return JsValue::make_string(el->id());
            if (name == "textContent" || name == "innerText") return JsValue::make_string(el->text_content());
            if (name == "innerHTML") return JsValue::make_string(el->inner_html());
            if (name == "outerHTML") return JsValue::make_string(el->outer_html());
            if (name == "style") {
                JsValue style_obj = JsValue::make_object();
                style_obj.set_property("color", JsValue::make_string(el->get_attribute("style")));
                return style_obj;
            }
            if (name == "value") return JsValue::make_string(el->value());
            if (name == "disabled") return JsValue::make_boolean(el->disabled());
            if (name == "checked") return JsValue::make_boolean(el->checked());
            if (name == "type") return JsValue::make_string(el->input_type());
            if (name == "shadowRoot") {
                auto sr = el->shadow_root();
                if (sr && sr->mode() == ShadowRootMode::Open) {
                    return JsValue::make_dom_node(sr);
                }
                return JsValue::make_null();
            }
            if (name == "attachShadow") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>& args) {
                    std::string mode = "open";
                    if (!args.empty() && args[0].type == JsType::Object) {
                        mode = args[0].get_property("mode").to_string();
                    }
                    auto sr = elem->attach_shadow(mode);
                    if (!sr) return JsValue::make_null();
                    return JsValue::make_dom_node(sr);
                });
            }
            if (name == "getBoundingClientRect") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>&) {
                    Rect r = elem->bounding_box();
                    if (r.width <= 0.0f && r.height <= 0.0f) {
                        r.width = 100.0f;
                        r.height = 30.0f;
                    }
                    JsValue ro = JsValue::make_object();
                    ro.set_property("x", JsValue::make_number(r.x));
                    ro.set_property("y", JsValue::make_number(r.y));
                    ro.set_property("width", JsValue::make_number(r.width));
                    ro.set_property("height", JsValue::make_number(r.height));
                    ro.set_property("top", JsValue::make_number(r.top()));
                    ro.set_property("bottom", JsValue::make_number(r.bottom()));
                    ro.set_property("left", JsValue::make_number(r.left()));
                    ro.set_property("right", JsValue::make_number(r.right()));
                    return ro;
                });
            }
            if (name == "setAttribute") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>& args) {
                    if (args.size() >= 2) {
                        elem->set_attribute(args[0].to_string(), args[1].to_string());
                    }
                    return JsValue::make_undefined();
                });
            }
            if (name == "getAttribute") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>& args) {
                    if (args.empty()) return JsValue::make_null();
                    std::string attr = args[0].to_string();
                    if (elem->has_attribute(attr)) {
                        return JsValue::make_string(elem->get_attribute(attr));
                    }
                    return JsValue::make_null();
                });
            }
            if (name == "hasAttribute") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>& args) {
                    if (args.empty()) return JsValue::make_boolean(false);
                    return JsValue::make_boolean(elem->has_attribute(args[0].to_string()));
                });
            }
            if (name == "removeAttribute") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>& args) {
                    if (!args.empty()) {
                        elem->remove_attribute(args[0].to_string());
                    }
                    return JsValue::make_undefined();
                });
            }
            if (name == "assignedNodes") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>&) {
                    auto nodes = ShadowRoot::assigned_nodes(elem);
                    JsValue arr = JsValue::make_array();
                    for (const auto& n : nodes) {
                        arr.array_elements().push_back(JsValue::make_dom_node(n));
                    }
                    return arr;
                });
            }
            if (name == "assignedElements") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>&) {
                    auto elems = ShadowRoot::assigned_elements(elem);
                    JsValue arr = JsValue::make_array();
                    for (const auto& e : elems) {
                        arr.array_elements().push_back(JsValue::make_dom_node(e));
                    }
                    return arr;
                });
            }
            if (name == "querySelector") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>& args) {
                    if (args.empty()) return JsValue::make_null();
                    auto found = elem->query_selector(args[0].to_string());
                    return found ? JsValue::make_dom_node(found) : JsValue::make_null();
                });
            }
            if (name == "querySelectorAll") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>& args) {
                    JsValue arr = JsValue::make_array();
                    if (!args.empty()) {
                        auto list = elem->query_selector_all(args[0].to_string());
                        for (const auto& item : list) {
                            arr.array_elements().push_back(JsValue::make_dom_node(item));
                        }
                    }
                    return arr;
                });
            }
            if (name == "focus") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>&) {
                    elem->focus();
                    return JsValue::make_undefined();
                });
            }
            if (name == "blur") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>&) {
                    elem->blur();
                    return JsValue::make_undefined();
                });
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
                    bool use_capture = (args.size() > 2) ? args[2].is_truthy() : false;
                    node->add_event_listener(ev_type, [cb](const std::shared_ptr<Event>& ev) mutable {
                        JsValue ev_obj = JsValue::make_object();
                        ev_obj.set_property("type", JsValue::make_string(ev->type()));
                        ev_obj.set_property("eventPhase", JsValue::make_number(static_cast<int>(ev->event_phase())));
                        ev_obj.set_property("bubbles", JsValue::make_boolean(ev->bubbles()));
                        ev_obj.set_property("cancelable", JsValue::make_boolean(ev->cancelable()));
                        ev_obj.set_property("defaultPrevented", JsValue::make_boolean(ev->default_prevented()));
                        ev_obj.set_property("data", JsValue::make_string(ev->data));
                        ev_obj.set_property("key", JsValue::make_string(ev->key));

                        if (ev->target()) ev_obj.set_property("target", JsValue::make_dom_node(ev->target()));
                        if (ev->current_target()) ev_obj.set_property("currentTarget", JsValue::make_dom_node(ev->current_target()));

                        auto ev_ptr = ev;
                        ev_obj.set_property("stopPropagation", JsValue::make_native_function([ev_ptr](const std::vector<JsValue>&) {
                            ev_ptr->stop_propagation();
                            return JsValue::make_undefined();
                        }));
                        ev_obj.set_property("preventDefault", JsValue::make_native_function([ev_ptr](const std::vector<JsValue>&) {
                            ev_ptr->prevent_default();
                            return JsValue::make_undefined();
                        }));

                        cb.call({ev_obj});
                    }, use_capture);
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
            if (name == "getContext") {
                auto node = dom_node;
                return JsValue::make_native_function([node](const std::vector<JsValue>& args) {
                    if (args.empty()) return JsValue::make_null();
                    std::string ctx_type = args[0].to_string();
                    if (ctx_type == "2d" || ctx_type == "2D") {
                        if (auto el = std::dynamic_pointer_cast<Element>(node)) {
                            auto ctx = el->get_or_create_canvas_context_2d();
                            if (ctx) {
                                return ctx->to_js_value(ctx);
                            }
                        }
                    }
                    return JsValue::make_null();
                });
            }
            if (name == "width") {
                if (auto el = std::dynamic_pointer_cast<Element>(dom_node)) {
                    if (el->tag_name() == "canvas") {
                        auto ctx = el->get_or_create_canvas_context_2d();
                        return JsValue::make_number(ctx->width());
                    }
                }
            }
            if (name == "height") {
                if (auto el = std::dynamic_pointer_cast<Element>(dom_node)) {
                    if (el->tag_name() == "canvas") {
                        auto ctx = el->get_or_create_canvas_context_2d();
                        return JsValue::make_number(ctx->height());
                    }
                }
            }
        }

        if (auto sr = std::dynamic_pointer_cast<ShadowRoot>(dom_node)) {
            if (name == "host") {
                auto h = sr->host();
                return h ? JsValue::make_dom_node(h) : JsValue::make_null();
            }
            if (name == "mode") {
                return JsValue::make_string(sr->mode_str());
            }
            if (name == "innerHTML") {
                return JsValue::make_string(sr->inner_html());
            }
            if (name == "getElementById") {
                auto sroot = sr;
                return JsValue::make_native_function([sroot](const std::vector<JsValue>& args) {
                    if (args.empty()) return JsValue::make_null();
                    auto found = sroot->get_element_by_id(args[0].to_string());
                    return found ? JsValue::make_dom_node(found) : JsValue::make_null();
                });
            }
            if (name == "querySelector") {
                auto sroot = sr;
                return JsValue::make_native_function([sroot](const std::vector<JsValue>& args) {
                    if (args.empty()) return JsValue::make_null();
                    auto found = sroot->query_selector(args[0].to_string());
                    return found ? JsValue::make_dom_node(found) : JsValue::make_null();
                });
            }
            if (name == "querySelectorAll") {
                auto sroot = sr;
                return JsValue::make_native_function([sroot](const std::vector<JsValue>& args) {
                    JsValue arr = JsValue::make_array();
                    if (!args.empty()) {
                        auto list = sroot->query_selector_all(args[0].to_string());
                        for (const auto& item : list) {
                            arr.array_elements().push_back(JsValue::make_dom_node(item));
                        }
                    }
                    return arr;
                });
            }
        }

        // Generic Node properties & methods
        if (name == "appendChild") {
            auto node = dom_node;
            return JsValue::make_native_function([node](const std::vector<JsValue>& args) {
                if (args.empty() || !args[0].dom_node) return JsValue::make_null();
                node->append_child(args[0].dom_node);
                return args[0];
            });
        }
        if (name == "removeChild") {
            auto node = dom_node;
            return JsValue::make_native_function([node](const std::vector<JsValue>& args) {
                if (args.empty() || !args[0].dom_node) return JsValue::make_null();
                node->remove_child(args[0].dom_node);
                return args[0];
            });
        }
        if (name == "insertBefore") {
            auto node = dom_node;
            return JsValue::make_native_function([node](const std::vector<JsValue>& args) {
                if (args.empty() || !args[0].dom_node) return JsValue::make_null();
                auto new_ch = args[0].dom_node;
                auto ref_ch = (args.size() > 1) ? args[1].dom_node : nullptr;
                node->insert_before(new_ch, ref_ch);
                return args[0];
            });
        }
        if (name == "parentNode" || name == "parentElement") {
            auto p = dom_node->parent();
            return p ? JsValue::make_dom_node(p) : JsValue::make_null();
        }
        if (name == "firstChild") {
            auto fc = dom_node->first_child();
            return fc ? JsValue::make_dom_node(fc) : JsValue::make_null();
        }
        if (name == "lastChild") {
            auto lc = dom_node->last_child();
            return lc ? JsValue::make_dom_node(lc) : JsValue::make_null();
        }
        if (name == "previousSibling") {
            auto ps = dom_node->previous_sibling();
            return ps ? JsValue::make_dom_node(ps) : JsValue::make_null();
        }
        if (name == "nextSibling") {
            auto ns = dom_node->next_sibling();
            return ns ? JsValue::make_dom_node(ns) : JsValue::make_null();
        }
        if (name == "childNodes") {
            JsValue arr = JsValue::make_array();
            for (const auto& ch : dom_node->children()) {
                arr.array_elements().push_back(JsValue::make_dom_node(ch));
            }
            return arr;
        }
        if (name == "children") {
            JsValue arr = JsValue::make_array();
            for (const auto& ch : dom_node->children()) {
                if (ch->is_element()) {
                    arr.array_elements().push_back(JsValue::make_dom_node(ch));
                }
            }
            return arr;
        }
        if (name == "isConnected") {
            return JsValue::make_boolean(dom_node->is_connected());
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
            if (name == "@@iterator" || name == "Symbol.iterator") {
                auto arr_obj = *this;
                return JsValue::make_native_function([arr_obj](const std::vector<JsValue>&) {
                    JsValue iter = JsValue::make_object();
                    auto idx = std::make_shared<size_t>(0);
                    iter.set_property("next", JsValue::make_native_function([arr_obj, idx](const std::vector<JsValue>&) {
                        size_t len = static_cast<size_t>(arr_obj.get_property("length").to_number());
                        JsValue res = JsValue::make_object();
                        if (*idx < len) {
                            res.set_property("value", arr_obj.get_property(std::to_string(*idx)));
                            res.set_property("done", JsValue::make_boolean(false));
                            (*idx)++;
                        } else {
                            res.set_property("value", JsValue::make_undefined());
                            res.set_property("done", JsValue::make_boolean(true));
                        }
                        return res;
                    }));
                    auto self_iter = JsValue::make_native_function([iter](const std::vector<JsValue>&) { return iter; });
                    iter.set_property("@@iterator", self_iter);
                    iter.set_property("Symbol.iterator", self_iter);
                    return iter;
                });
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

        if (name == "size") {
            auto it_size = object_val->properties.find("__get_size__");
            if (it_size != object_val->properties.end() && it_size->second.native_fn) {
                return it_size->second.native_fn({});
            }
        }
        if (name == "readyState") {
            auto it_rs = object_val->properties.find("__get_readyState__");
            if (it_rs != object_val->properties.end() && it_rs->second.native_fn) {
                return it_rs->second.native_fn({});
            }
        }
        if (name == "body") {
            auto it_b = object_val->properties.find("__get_body__");
            if (it_b != object_val->properties.end() && it_b->second.native_fn) {
                return it_b->second.native_fn({});
            }
        }
        if (name == "head") {
            auto it_h = object_val->properties.find("__get_head__");
            if (it_h != object_val->properties.end() && it_h->second.native_fn) {
                return it_h->second.native_fn({});
            }
        }
        if (name == "documentElement") {
            auto it_d = object_val->properties.find("__get_documentElement__");
            if (it_d != object_val->properties.end() && it_d->second.native_fn) {
                return it_d->second.native_fn({});
            }
        }

        auto it_getter = object_val->properties.find("__get_" + name + "__");
        if (it_getter != object_val->properties.end() && it_getter->second.native_fn) {
            return it_getter->second.native_fn({});
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

    if (canvas_ctx) {
        if (name == "fillStyle") {
            if (canvas_ctx->fill_gradient) return JsValue::make_string("[CanvasGradient]");
            return JsValue::make_string(canvas_ctx->fill_style.to_hex());
        }
        if (name == "strokeStyle") {
            if (canvas_ctx->stroke_gradient) return JsValue::make_string("[CanvasGradient]");
            return JsValue::make_string(canvas_ctx->stroke_style.to_hex());
        }
        if (name == "lineWidth") return JsValue::make_number(canvas_ctx->line_width);
        if (name == "globalCompositeOperation") return JsValue::make_string(canvas_ctx->global_composite_operation);
    }

    if (name == "cookie") {
        return JsValue::make_string(CookieJar::default_jar().get_document_cookie());
    }

    if (properties) {
        if (name == "size") {
            auto it_sz = properties->find("__get_size__");
            if (it_sz != properties->end() && it_sz->second.native_fn) {
                return it_sz->second.native_fn({});
            }
        }
        if (name == "readyState") {
            auto it_rs = properties->find("__get_readyState__");
            if (it_rs != properties->end() && it_rs->second.native_fn) {
                return it_rs->second.native_fn({});
            }
        }
        if (name == "body") {
            auto it_b = properties->find("__get_body__");
            if (it_b != properties->end() && it_b->second.native_fn) {
                return it_b->second.native_fn({});
            }
        }
        if (name == "head") {
            auto it_h = properties->find("__get_head__");
            if (it_h != properties->end() && it_h->second.native_fn) {
                return it_h->second.native_fn({});
            }
        }
        if (name == "documentElement") {
            auto it_d = properties->find("__get_documentElement__");
            if (it_d != properties->end() && it_d->second.native_fn) {
                return it_d->second.native_fn({});
            }
        }
        auto it_prop_getter = properties->find("__get_" + name + "__");
        if (it_prop_getter != properties->end() && it_prop_getter->second.native_fn) {
            return it_prop_getter->second.native_fn({});
        }

        auto it = properties->find(name);
        if (it != properties->end()) return it->second;
    }
    return JsValue::make_undefined();
}

void JsValue::set_property(const std::string& name, JsValue val) {
    if (canvas_ctx) {
        if (name == "fillStyle") {
            if (val.canvas_gradient) {
                canvas_ctx->fill_gradient = val.canvas_gradient;
            } else {
                canvas_ctx->fill_gradient = nullptr;
                canvas_ctx->fill_style = Color::parse(val.to_string());
            }
        } else if (name == "strokeStyle") {
            if (val.canvas_gradient) {
                canvas_ctx->stroke_gradient = val.canvas_gradient;
            } else {
                canvas_ctx->stroke_gradient = nullptr;
                canvas_ctx->stroke_style = Color::parse(val.to_string());
            }
        } else if (name == "lineWidth") {
            canvas_ctx->line_width = static_cast<float>(val.to_number());
        } else if (name == "globalCompositeOperation") {
            canvas_ctx->global_composite_operation = val.to_string();
        }
    }

    if (name == "cookie") {
        CookieJar::default_jar().set_document_cookie(val.to_string());
        if (object_val) object_val->properties[name] = JsValue::make_string(CookieJar::default_jar().get_document_cookie());
        else if (properties) (*properties)[name] = JsValue::make_string(CookieJar::default_jar().get_document_cookie());
        return;
    }

    if (type == JsType::DomNode && dom_node) {
        if (name == "innerHTML") {
            if (auto el = std::dynamic_pointer_cast<Element>(dom_node)) {
                el->set_inner_html(val.to_string());
                return;
            }
            if (auto sr = std::dynamic_pointer_cast<ShadowRoot>(dom_node)) {
                sr->set_inner_html(val.to_string());
                return;
            }
        }
        if (auto el = std::dynamic_pointer_cast<Element>(dom_node)) {
            if (name == "textContent" || name == "innerText") {
                el->children().clear();
                el->append_child(std::make_shared<Text>(val.to_string()));
                return;
            }
            if (name == "value") {
                el->set_value(val.to_string());
                return;
            }
            if (name == "disabled") {
                el->set_disabled(val.is_truthy());
                return;
            }
            if (name == "checked") {
                el->set_checked(val.is_truthy());
                return;
            }
            if (name == "width" && el->tag_name() == "canvas") {
                int w = std::max(1, static_cast<int>(val.to_number()));
                el->set_attribute("width", std::to_string(w));
                auto ctx = el->get_or_create_canvas_context_2d();
                ctx->resize(w, ctx->height());
                return;
            }
            if (name == "height" && el->tag_name() == "canvas") {
                int h = std::max(1, static_cast<int>(val.to_number()));
                el->set_attribute("height", std::to_string(h));
                auto ctx = el->get_or_create_canvas_context_2d();
                ctx->resize(ctx->width(), h);
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
